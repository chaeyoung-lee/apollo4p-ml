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

    for (int i = 0; i < kOutputSize; i++)
    {
        float prob = get_output_value(i, scale, zero_point);
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

    // Build mutable resolver with required operations for this model
    static tflite::MicroMutableOpResolver<9> resolver(error_reporter);
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddDequantize();
    resolver.AddFullyConnected();
    resolver.AddMul();
    resolver.AddPad();
    resolver.AddTranspose();
    resolver.AddSum();
    resolver.AddQuantize();

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

    // Copy input to input tensor with proper quantization
    if (input_tensor->type == kTfLiteInt8)
    {
        int8_t *input_data = input_tensor->data.int8;
        float input_scale = input_tensor->params.scale;
        int input_zero_point = input_tensor->params.zero_point;
        
        // Quantize uint8 [0-255] to int8 using model's quantization parameters
        for (int i = 0; i < kInputSize; i++)
        {
            float real_value = static_cast<float>(image_data[i]);
            float quantized_float = (real_value / input_scale) + input_zero_point;
            int8_t quantized_value = static_cast<int8_t>(std::round(quantized_float));
            
            // Clamp to int8 range
            if (quantized_value > 127) quantized_value = 127;
            if (quantized_value < -128) quantized_value = -128;
            
            input_data[i] = quantized_value;
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
        // Normalize to [0, 1]
        for (int i = 0; i < kInputSize; i++)
        {
            input_data[i] = static_cast<float>(image_data[i]) / 255.0f;
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

    // Print all class logits
    am_util_stdio_printf("Class logits:\r\n");
    for (int i = 0; i < kOutputSize; i++)
    {
        float prob = get_output_value(i, scale, zero_point);
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
