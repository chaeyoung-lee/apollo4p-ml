#include "model_inference.h"
#include "model_data.h"
#include "model_settings.h"

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

// FP32 only: get one output value (for classifier path).
static float get_output_value(int index)
{
    return output_tensor->data.f[index];
}

// Index of first logit in output (output is [1, emb_dim + kOutputSize])
static int get_logits_start_index(void)
{
    if (output_tensor == nullptr || output_tensor->dims->size < 2)
        return 0;
    return output_tensor->dims->data[1] - kOutputSize;
}

// Find predicted class from logits (FP32). Logits start index = output dim - kOutputSize.
static int find_predicted_class(void)
{
    int logits_start = get_logits_start_index();
    int predicted_class = 0;
    float max_prob = -1e6f;
    for (int i = 0; i < kOutputSize; i++)
    {
        float prob = get_output_value(logits_start + i);
        if (prob > max_prob)
        {
            max_prob = prob;
            predicted_class = i;
        }
    }
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
    // If operators are missing, interpreter will fail to initialize.
    // Note: These Add* calls register reference (portable C) kernels only.
    // CMSIS-NN optimized kernels are for int8/uint8 quantized models only; for
    // FP32 we always use reference kernels, so inference is slower (~4s for this
    // embedding model at 96 MHz). For faster inference, use a quantized model.
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

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    size_t arena_used = interpreter->arena_used_bytes();
    am_util_stdio_printf("Model initialized. Arena used: %d / %d bytes\r\n",
               (int)arena_used, kTensorArenaSize);

    return 0;
}

/* --- Class prediction (for testing) --- */

int model_predict_class(const uint8_t *image_data)
{
    if (interpreter == nullptr || input_tensor == nullptr || output_tensor == nullptr)
        return -1;
    model_preprocess_for_embedding(image_data);
    if (model_invoke_for_embedding() != 0)
        return -1;
    return find_predicted_class();
}

/* --- IVF embedding API --- */

void model_preprocess_for_embedding(const uint8_t *image_data)
{
    if (interpreter == nullptr || input_tensor == nullptr)
        return;
    int height = input_tensor->dims->data[2];
    int width = input_tensor->dims->data[3];
    float *input_data = input_tensor->data.f;
    apply_imagenet_normalization(image_data, input_data, height, width);
}

int model_invoke_for_embedding(void)
{
    if (interpreter == nullptr)
        return -1;
    return (interpreter->Invoke() == kTfLiteOk) ? 0 : -1;
}

void model_get_embedding(float *out, int dim)
{
    if (output_tensor == nullptr || out == nullptr || dim <= 0)
        return;
    int total = 1;
    for (int i = 0; i < output_tensor->dims->size; i++)
        total *= output_tensor->dims->data[i];
    if (dim > total)
        dim = total;
    const float *src = output_tensor->data.f;
    for (int i = 0; i < dim; i++)
        out[i] = src[i];
}
