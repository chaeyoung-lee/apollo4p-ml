#ifndef MODEL_INFERENCE_H_
#define MODEL_INFERENCE_H_

#include <stdint.h>

// Initialize the model and allocate resources
// Returns 0 on success, non-zero on failure
int model_init(void);

// Run inference on the provided image data
// image_data: pointer to uint8_t array of size kInputSize (32x32x3 = 3072 bytes)
// Returns 0 on success, non-zero on failure
int model_run_inference(const uint8_t *image_data);

// Get the predicted class index from the last inference
// Returns class index (0-9) or -1 if no inference has been run
int model_get_predicted_class(void);

// Get the prediction score for the predicted class
// Returns the logit score
float model_get_prediction_score(void);

// Print all class logits from the last inference
void model_print_results(void);

#endif // MODEL_INFERENCE_H_
