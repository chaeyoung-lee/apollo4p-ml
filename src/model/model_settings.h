#ifndef MODEL_SETTINGS_H_
#define MODEL_SETTINGS_H_

// CIFAR-10 Model Settings
// This file defines constants for the CIFAR-10 image classification model

// CIFAR-10 image dimensions
constexpr int kImageWidth = 32;
constexpr int kImageHeight = 32;
constexpr int kImageChannels = 3; // RGB

// Input size in bytes (32x32x3 = 3072)
constexpr int kInputSize = kImageWidth * kImageHeight * kImageChannels;

// Number of output classes for CIFAR-10
constexpr int kCategoryCount = 10;
constexpr int kOutputSize = kCategoryCount;

// Category labels for CIFAR-10 dataset
extern const char *kCategoryLabels[kCategoryCount];

// Tensor arena size for TensorFlow Lite Micro
// This needs to be large enough to hold all intermediate tensors
// Adjust based on your model's memory requirements
// Apollo4 Plus: Allocated in SHARED_SRAM (1MB available)
// Can use larger sizes since it's separate from TCM
constexpr int kTensorArenaSize = 1024 * 1024; // 800KB

#endif // MODEL_SETTINGS_H_
