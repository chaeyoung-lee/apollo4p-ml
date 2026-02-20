#include "model_inference.h"
#include "model_data.h"
#include "model_settings.h"

#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/c/common.h"
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

// Data types detected at init (for int8 vs fp32 preprocess / output handling)
static TfLiteType input_type = kTfLiteNoType;
static TfLiteType output_type = kTfLiteNoType;

// ImageNet normalization constants (used for CIFAR-10 with pretrained models)
static const float IMAGENET_MEAN[3] = {0.485f, 0.456f, 0.406f};
static const float IMAGENET_STD[3] = {0.229f, 0.224f, 0.225f};

// Helper function to apply ImageNet normalization to image data (FP32 input tensor)
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
    for (int c = 0; c < 3; c++)
    {
        for (int h = 0; h < height; h++)
        {
            for (int w = 0; w < width; w++)
            {
                float pixel = static_cast<float>(image_data[h * width * 3 + w * 3 + c]);
                float normalized = (pixel / 255.0f - IMAGENET_MEAN[c]) / IMAGENET_STD[c];
                input_data[c * height * width + h * width + w] = normalized;
            }
        }
    }
}

// Apply ImageNet normalization and quantize to int8 for quantized input tensor.
// real_value = scale * (quantized - zero_point)  =>  quantized = round(real_value/scale) + zero_point
static void apply_imagenet_normalization_quantized(
    const uint8_t *image_data,
    int8_t *input_data,
    int height,
    int width,
    float scale,
    int32_t zero_point)
{
    for (int c = 0; c < 3; c++)
    {
        for (int h = 0; h < height; h++)
        {
            for (int w = 0; w < width; w++)
            {
                float pixel = static_cast<float>(image_data[h * width * 3 + w * 3 + c]);
                float normalized = (pixel / 255.0f - IMAGENET_MEAN[c]) / IMAGENET_STD[c];
                int32_t q = static_cast<int32_t>(roundf(normalized / scale)) + zero_point;
                if (q < -128)
                    q = -128;
                if (q > 127)
                    q = 127;
                input_data[c * height * width + h * width + w] = static_cast<int8_t>(q);
            }
        }
    }
}

// Get one output value.
static float get_output_value(int index)
{
    if (output_type == kTfLiteFloat32)
        return output_tensor->data.f[index];
    if (output_type == kTfLiteInt8)
        return (output_tensor->data.int8[index] - output_tensor->params.zero_point) * output_tensor->params.scale;
    return 0.0f;
}

// Index of first logit in output (output is [1, emb_dim + kOutputSize])
static int get_logits_start_index(void)
{
    if (output_tensor == nullptr || output_tensor->dims->size < 2)
        return 0;
    return output_tensor->dims->data[1] - kOutputSize;
}

// Find predicted class from logits.
// Logits start index = output dim - kOutputSize.
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
    // Only Conv2D, DepthwiseConv2D, FullyConnected use CMSIS-NN int8 kernels.
    // Other ops (Mul, Add, Reshape, Concatenation, Transpose, etc.) use reference
    // kernels, so total inference speedup depends on how much time the model spends
    // in conv/depthwise/FC vs the rest. Run: python tflite_operators.py <model.tflite>
    static tflite::MicroMutableOpResolver<16> resolver(error_reporter);
    resolver.AddTranspose();
    resolver.AddConv2D(tflite::Register_CONV_2D_INT8());
    resolver.AddPad();
    resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8());
    resolver.AddAveragePool2D();
    resolver.AddFullyConnected(tflite::Register_FULLY_CONNECTED_INT8());
    resolver.AddAbs();
    resolver.AddMul();
    resolver.AddSum();
    resolver.AddSqrt();
    resolver.AddReshape();
    resolver.AddMaximum();
    resolver.AddDiv();
    resolver.AddConcatenation();
    resolver.AddQuantize();
    resolver.AddDequantize();

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

    input_type = input_tensor->type;
    output_type = output_tensor->type;

    if (input_type != kTfLiteFloat32 && input_type != kTfLiteInt8)
    {
        am_util_stdio_printf("Unsupported input type: %d (only kTfLiteFloat32 or kTfLiteInt8)\r\n", static_cast<int>(input_type));
        return -1;
    }
    if (output_type != kTfLiteFloat32 && output_type != kTfLiteInt8)
    {
        am_util_stdio_printf("Unsupported output type: %d (only kTfLiteFloat32 or kTfLiteInt8)\r\n", static_cast<int>(output_type));
        return -1;
    }
    am_util_stdio_printf("Model I/O types: input=%d (1=float32, 9=int8), output=%d\r\n",
                         static_cast<int>(input_type), static_cast<int>(output_type));

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
    if (input_type == kTfLiteFloat32)
    {
        float *input_data = input_tensor->data.f;
        apply_imagenet_normalization(image_data, input_data, height, width);
    }
    else if (input_type == kTfLiteInt8)
    {
        int8_t *input_data = input_tensor->data.int8;
        float scale = input_tensor->params.scale;
        int32_t zero_point = input_tensor->params.zero_point;
        apply_imagenet_normalization_quantized(image_data, input_data, height, width, scale, zero_point);
    }
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
    if (output_type == kTfLiteFloat32)
    {
        const float *src = output_tensor->data.f;
        for (int i = 0; i < dim; i++)
            out[i] = src[i];
    }
    else if (output_type == kTfLiteInt8)
    {
        float scale = output_tensor->params.scale;
        int32_t zero_point = output_tensor->params.zero_point;
        const int8_t *src = output_tensor->data.int8;
        for (int i = 0; i < dim; i++)
            out[i] = (src[i] - zero_point) * scale;
    }
}
