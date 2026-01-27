# ML Inference on Apollo 4 Plus

An image classification inference pipeline for the Ambiq Apollo 4 Plus microcontroller using TensorFlow Lite Micro.

* Hardware: Apollo 4 Plus EVB (AMAP4PEVB), USB-C cable for J-link
* Compiling: SEGGER Jlink, ARM GCC

## Quick Start

### Build and Deploy

```bash
make clean
make
make deploy
```

### View Output

Connect to UART at 115200 baud:
```bash
screen /dev/cu.usbmodem* 115200
```

The program will:
1. Load the CIFAR-10 model
2. Run inference on a default test image (frog)
3. Print class probabilities and predicted class
4. Verify the prediction against the expected label

## Project Structure

```
src/
├── main.cc                    # Simple entry point
├── model/                     # Model inference code
│   ├── model_inference.h/cc  # Model API
│   ├── model_data.h/cc       # Model weights
│   ├── model_settings.h/cc   # Model configuration
│   └── cifar10_test_image.h  # Default test image
├── am_utils/                  # UART utilities
└── util/                     # Helper functions
```

## Using Your Own Model

### 1. Convert Model

```bash
xxd -i your_model.tflite > src/model/model_data.cc
```

Ensure the file has:
- `g_model_data` (array)
- `g_model_data_len` (length)

### 2. Update Settings

Edit `src/model/model_settings.h`:
- `kInputSize` - Input dimensions (default: 3072 for 32×32×3)
- `kOutputSize` - Number of classes (default: 10)
- `kTensorArenaSize` - Memory allocation (default: 400KB)

### 3. Update Operations

Edit `src/model/model_inference.cc` in `model_init()` to add required TensorFlow Lite operations for your model.

## API

```c
int model_init(void);                                    // Initialize model
int model_run_inference(const uint8_t *image_data);     // Run inference
int model_get_predicted_class(void);                     // Get prediction
float model_get_prediction_score(void);                  // Get score
void model_print_results(void);                          // Print results
```

## Troubleshooting

- **AllocateTensors() fails**: Increase `kTensorArenaSize` in `model_settings.h`
- **Invoke() fails**: Add missing operations to the resolver in `model_init()`
- **No UART output**: Check J-Link connection and baud rate (115200)

## Dependencies

- AmbiqSuite SDK (included in `libs/` and `includes/`)
- TensorFlow Lite Micro (included in `libs/`)
- ARM GCC toolchain
- SEGGER J-Link

## License

MIT License - see [LICENSE](LICENSE) for details.
