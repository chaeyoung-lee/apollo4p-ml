#include "model_inference.h"
#include "model_data.h"
#include "model_settings.h"
#include "cifar10_test_image.h"
#include "../util/quantization_helpers.h"

#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "am_util.h"
#include <cmath>

// Tensor arena for model execution - placed in SHARED_SRAM (uninitialized)
alignas(16) static uint8_t tensor_arena[kTensorArenaSize] __attribute__((section(".shared_bss")));

// Model and interpreter state
static tflite::ErrorReporter *error_reporter = nullptr;
static const tflite::Model *model = nullptr;
static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input_tensor = nullptr;
static TfLiteTensor *output_tensor = nullptr;

// Last inference results
static int last_predicted_class = -1;
static float last_prediction_score = 0.0f;

// ImageNet normalization constants (used for CIFAR-10 with pretrained models)
static const float IMAGENET_MEAN[3] = {0.485f, 0.456f, 0.406f};
static const float IMAGENET_STD[3] = {0.229f, 0.224f, 0.225f};

// Helper function to apply ImageNet normalization to image data
// image_data: HWC format (height, width, channels) - RGB uint8 [0-255]
// input_data: Output tensor data (format depends on tensor shape)
// height, width: Image dimensions (32x32 for CIFAR-10)
static void apply_imagenet_normalization(
    const uint8_t *image_data,
    float *input_data,
    int height,
    int width)
{
    // NCHW format: [C, H, W]
    // For each channel
    for (int c = 0; c < 3; c++)
    {
        // For each pixel in this channel
        for (int h = 0; h < height; h++)
        {
            for (int w = 0; w < width; w++)
            {
                // Get pixel value from HWC format: image_data[h * width * 3 + w * 3 + c]
                float pixel = static_cast<float>(image_data[h * width * 3 + w * 3 + c]);
                // Apply ImageNet normalization: (pixel/255.0 - mean) / std
                float normalized = (pixel / 255.0f - IMAGENET_MEAN[c]) / IMAGENET_STD[c];
                // Store in NCHW format: input_data[c * height * width + h * width + w]
                input_data[c * height * width + h * width + w] = normalized;
            }
        }
    }
}

// Helper function to get output value as float
static float get_output_value(int index, float scale, int zero_point)
{
    if (output_tensor->type == kTfLiteInt8)
    {
        return DequantizeInt8ToFloat(output_tensor->data.int8[index], scale, zero_point);
    }
    else if (output_tensor->type == kTfLiteFloat32)
    {
        return output_tensor->data.f[index];
    }
    return 0.0f;
}

// Helper function to find predicted class and return max probability
static int find_predicted_class(float *max_prob_out)
{
    int predicted_class = 0;
    float max_prob = -1e6f; // Use very negative value to handle negative logits
    float scale = (output_tensor->type == kTfLiteInt8) ? output_tensor->params.scale : 0.0f;
    int zero_point = (output_tensor->type == kTfLiteInt8) ? output_tensor->params.zero_point : 0;

    // Get logits start index (skip embeddings if present)
    int logits_start = output_tensor->dims->data[1] - kOutputSize;

    for (int i = 0; i < kOutputSize; i++)
    {
        float prob = get_output_value(logits_start + i, scale, zero_point);
        if (prob > max_prob)
        {
            max_prob = prob;
            predicted_class = i;
        }
    }

    *max_prob_out = max_prob;
    return predicted_class;
}

int model_init(void)
{
    // Set up error reporting
    static tflite::MicroErrorReporter micro_error_reporter;
    error_reporter = &micro_error_reporter;

    // Initialize TensorFlow Lite Micro target
    tflite::InitializeTarget();

    // Load model from flatbuffer
    am_util_stdio_printf("Loading model (size: %d bytes)...\r\n", g_model_data_len);
    model = tflite::GetModel(g_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        am_util_stdio_printf("Model schema version %d not supported. Expected %d.\r\n",
                             model->version(), TFLITE_SCHEMA_VERSION);
        return -1;
    }
    am_util_stdio_printf("Model loaded successfully (schema version %d)\r\n", model->version());

    // Run python_scripts/tflite_operators.py to get the operators in the model
    // If operators are missing, interpreter will fail to initialize
    static tflite::MicroMutableOpResolver<14> resolver(error_reporter);
    resolver.AddTranspose();
    resolver.AddConv2D();
    resolver.AddPad();
    resolver.AddDepthwiseConv2D();
    resolver.AddAveragePool2D();
    resolver.AddReshape();
    resolver.AddFullyConnected();
    resolver.AddAbs();
    resolver.AddMul();
    resolver.AddSum();
    resolver.AddSqrt();
    resolver.AddMaximum();
    resolver.AddDiv();
    resolver.AddConcatenation();

    // Build interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &static_interpreter;

    // Check interpreter initialization status
    TfLiteStatus init_status = interpreter->initialization_status();
    if (init_status != kTfLiteOk)
    {
        am_util_stdio_printf("Interpreter initialization failed with status: %d\r\n", init_status);
        return -1;
    }

    // Allocate memory for all model tensors
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk)
    {
        am_util_stdio_printf("AllocateTensors() failed with status: %d\r\n", allocate_status);
        return -1;
    }

    // Get input and output tensors
    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    // Print input tensor info for debugging
    am_util_stdio_printf("Input tensor info:\r\n");
    am_util_stdio_printf("  Type: %d\r\n", input_tensor->type);
    am_util_stdio_printf("  Dimensions: %d\r\n", input_tensor->dims->size);
    if (input_tensor->dims->size >= 4)
    {
        am_util_stdio_printf("  Shape: [%d, %d, %d, %d]\r\n",
                             input_tensor->dims->data[0],
                             input_tensor->dims->data[1],
                             input_tensor->dims->data[2],
                             input_tensor->dims->data[3]);
    }

    // Print output tensor info for debugging
    am_util_stdio_printf("Output tensor info:\r\n");
    am_util_stdio_printf("  Type: %d\r\n", output_tensor->type);
    am_util_stdio_printf("  Dimensions: %d\r\n", output_tensor->dims->size);
    if (output_tensor->dims->size >= 1)
    {
        am_util_stdio_printf("  Shape: [");
        for (int i = 0; i < output_tensor->dims->size; i++)
        {
            am_util_stdio_printf("%d", output_tensor->dims->data[i]);
            if (i < output_tensor->dims->size - 1)
                am_util_stdio_printf(", ");
        }
        am_util_stdio_printf("]\r\n");

        // Calculate output size
        int output_size = 1;
        for (int i = 0; i < output_tensor->dims->size; i++)
        {
            output_size *= output_tensor->dims->data[i];
        }
        am_util_stdio_printf("  Total size: %d\r\n", output_size);

        // Determine logits start
        int logits_start = get_logits_start_index();
        am_util_stdio_printf("  Logits start index: %d\r\n", logits_start);
        am_util_stdio_printf("  Num classes (kOutputSize): %d\r\n", kOutputSize);
    }

    size_t arena_used = interpreter->arena_used_bytes();
    am_util_stdio_printf("Model initialized. Arena used: %d / %d bytes\r\n",
                         arena_used, kTensorArenaSize);

    return 0;
}

int model_run_inference(const uint8_t *image_data)
{
    if (interpreter == nullptr || input_tensor == nullptr || output_tensor == nullptr)
    {
        return -1;
    }

    int height = input_tensor->dims->data[2];
    int width = input_tensor->dims->data[3];

    // Copy input to input tensor with proper quantization and normalization
    if (input_tensor->type == kTfLiteInt8)
    {
        int8_t *input_data = input_tensor->data.int8;
        float input_scale = input_tensor->params.scale;
        int input_zero_point = input_tensor->params.zero_point;

        // Process each pixel: normalize then quantize
        for (int h = 0; h < height; h++)
        {
            for (int w = 0; w < width; w++)
            {
                for (int c = 0; c < 3; c++)
                {
                    // Get pixel value from HWC format
                    float pixel = static_cast<float>(image_data[h * width * 3 + w * 3 + c]);
                    // Apply ImageNet normalization
                    float normalized = (pixel / 255.0f - IMAGENET_MEAN[c]) / IMAGENET_STD[c];

                    // Quantize normalized value to int8
                    float quantized_float = (normalized / input_scale) + input_zero_point;
                    int8_t quantized_value = static_cast<int8_t>(std::round(quantized_float));

                    // Clamp to int8 range
                    if (quantized_value > 127)
                        quantized_value = 127;
                    if (quantized_value < -128)
                        quantized_value = -128;

                    input_data[c * height * width + h * width + w] = quantized_value;
                }
            }
        }
    }
    else if (input_tensor->type == kTfLiteUInt8)
    {
        uint8_t *input_data = input_tensor->data.uint8;
        for (int i = 0; i < kInputSize; i++)
        {
            input_data[i] = image_data[i];
        }
    }
    else if (input_tensor->type == kTfLiteFloat32)
    {
        float *input_data = input_tensor->data.f;
        // Apply ImageNet normalization (handles format internally)
        apply_imagenet_normalization(image_data, input_data, height, width);

        // Debug: Print a few sample input values
        am_util_stdio_printf("Debug: Sample normalized input values (first 9):\r\n");
        for (int i = 0; i < 9 && i < height * width * 3; i++)
        {
            am_util_stdio_printf("  input[%d] = %.6f\r\n", i, input_data[i]);
        }
    }

    // Run inference
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk)
    {
        am_util_stdio_printf("Invoke() failed\r\n");
        return -1;
    }

    // Find predicted class
    float max_prob;
    last_predicted_class = find_predicted_class(&max_prob);
    last_prediction_score = max_prob;

    return 0;
}

int model_get_predicted_class(void)
{
    return last_predicted_class;
}

float model_get_prediction_score(void)
{
    return last_prediction_score;
}

void model_print_results(void)
{
    if (output_tensor == nullptr || last_predicted_class < 0)
    {
        return;
    }

    // Process and display results
    float scale = (output_tensor->type == kTfLiteInt8) ? output_tensor->params.scale : 0.0f;
    int zero_point = (output_tensor->type == kTfLiteInt8) ? output_tensor->params.zero_point : 0;

    // Get logits start index; (embedding, logits) -> logits
    int logits_start = output_tensor->dims->data[1] - kOutputSize;

    // Print all class logits
    am_util_stdio_printf("Class logits:\r\n");
    for (int i = 0; i < kOutputSize; i++)
    {
        float prob = get_output_value(logits_start + i, scale, zero_point);
        am_util_stdio_printf("  %s: %.4f\r\n", kCategoryLabels[i], prob);
    }

    // Print predicted class
    am_util_stdio_printf("\r\nPredicted class: %s (score: %.4f)\r\n",
                         kCategoryLabels[last_predicted_class], last_prediction_score);

// Verify prediction with expected label
#ifdef CIFAR10_TEST_IMAGE_LABEL
    int expected_label = CIFAR10_TEST_IMAGE_LABEL;
    am_util_stdio_printf("\r\nVERIFICATION:\r\n");
    am_util_stdio_printf("  Expected: %s, Predicted: %s\r\n",
                         kCategoryLabels[expected_label], kCategoryLabels[last_predicted_class]);
    am_util_stdio_printf("  Result: %s\r\n",
                         (last_predicted_class == expected_label) ? "CORRECT" : "INCORRECT");
#endif
}
