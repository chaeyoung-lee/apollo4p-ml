/**
 * IVF retrieval API: closest entry and classification from an image.
 */
#ifndef IVF_RETRIEVAL_H
#define IVF_RETRIEVAL_H

#include <stdint.h>
#include "ivf_config.h"

/** Bucket workspace: IVF_BUCKET_BUF_VECTORS * IVF_EMB_DIM * sizeof(float) bytes. */
#define IVF_BUCKET_BUF_VECTORS (IVF_NUM_VECTORS / IVF_NUM_CLUSTERS + 256)

/**
 * Per-step cycle counts for IVF profiling (optional).
 * Pass non-NULL out_profile and get_cycles to ivf_retrieve_closest to fill this.
 */
typedef struct {
    uint32_t embedding_cyc;       /* Total: preprocess + invoke + get_embedding */
    uint32_t embedding_preprocess_cyc;  /* ImageNet norm + copy to input tensor */
    uint32_t embedding_invoke_cyc;      /* TFLite interpreter->Invoke() only */
    uint32_t embedding_get_cyc;         /* Copy output tensor to query buffer */
    uint32_t centroid_cyc;     /* Find nearest centroid */
    uint32_t bucket_load_cyc; /* Load bucket from SD (db_vectors.bin) */
    uint32_t search_cyc;      /* Find nearest vector in bucket */
    uint32_t label_read_cyc;  /* Read label from SD (db_labels.bin) */
} ivf_profile_t;

/**
 * Load IVF metadata (centroids, bucket_offsets, bucket_lengths) from SD into RAM once.
 * Optional: call at startup to avoid SD opens on first retrieval; otherwise
 * ivf_retrieve_closest() loads lazily on first use.
 * Returns 0 on success, <0 on error.
 */
int ivf_retrieve_init(void);

/**
 * Retrieve closest DB entry and its label.
 *
 * image       – RGB uint8, size INPUT_HEIGHT * INPUT_WIDTH * 3 (e.g. cifar10_test_image[3072])
 * bucket_buf  – workspace of size IVF_BUCKET_BUF_VECTORS * IVF_EMB_DIM * sizeof(float)
 * out_label   – classification label (0 .. NUM_CLASSES-1)
 * out_distance – cosine distance to nearest neighbor (optional, can pass NULL)
 *
 * Returns 0 on success, <0 on error.
 *
 * Optional profiling: pass non-NULL out_profile and get_cycles to record
 * per-step CPU cycle counts (embedding, centroid, bucket load, search, label read).
 */
int ivf_retrieve_closest(
    const uint8_t *image,
    float *bucket_buf,
    int32_t *out_label,
    float *out_distance,
    ivf_profile_t *out_profile,
    uint32_t (*get_cycles)(void));

#endif /* IVF_RETRIEVAL_H */
