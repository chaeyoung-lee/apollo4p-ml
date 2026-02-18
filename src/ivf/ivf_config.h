// IVF Index Configuration

#ifndef IVF_CONFIG_H
#define IVF_CONFIG_H

// Index dimensions
#define IVF_NUM_CLUSTERS 512
#define IVF_EMB_DIM 64
#define IVF_NUM_VECTORS 50000
#define IVF_TOPK 5

// Input image dimensions
#define INPUT_HEIGHT 32
#define INPUT_WIDTH 32
#define INPUT_CHANNELS 3

// Classification
#define NUM_CLASSES 10

// Data types
#define IVF_QUANTIZED 0
typedef float ivf_vec_t;

// Memory sizes (bytes)
#define IVF_CENTROIDS_SIZE 131072 // 128.0 KB
#define IVF_VECTORS_SIZE 12800000 // 12500.0 KB
#define IVF_LABELS_SIZE 200000    // 195.3 KB
#define IVF_METADATA_SIZE 4096    // 4.0 KB

#define IVF_TOTAL_SD_SIZE 13004096 // 12.40 MB

#endif // IVF_CONFIG_H
