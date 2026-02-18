#ifndef MODEL_INFERENCE_H_
#define MODEL_INFERENCE_H_

#include <stdint.h>

// Initialize the model and allocate resources. Returns 0 on success, non-zero on failure.
int model_init(void);

// --- Embedding API (for IVF) ---

// Copy image into model input buffer. Call before model_invoke_for_embedding().
void model_preprocess_for_embedding(const uint8_t *image_data);

// Run TFLite forward pass. Call after model_preprocess_for_embedding(). Returns 0 on success.
int model_invoke_for_embedding(void);

// Copy first 'dim' floats from model output (embedding) into out. Call after model_invoke_for_embedding().
void model_get_embedding(float *out, int dim);

// --- Class prediction (for testing) ---

// Run preprocess + invoke and return predicted class index (0..kCategoryCount-1), or -1 on failure.
int model_predict_class(const uint8_t *image_data);

#endif // MODEL_INFERENCE_H_
