/**
 * IVF retrieval: given an image, return the closest DB label and distance.
 *
 * Flow:
 *   1. Preprocess image → TFLite input
 *   2. Run TFLite → query embedding q[IVF_EMB_DIM]
 *   3. Find nearest centroid (optionally nprobe>1)
 *   4. Load that bucket from SD (db_vectors, bucket_offsets/lengths)
 *   5. Find nearest vector in bucket → global index
 *   6. Read db_labels[global_index] from SD → classification result
 */
#include "ivf_retrieval.h"
#include "model_inference.h"
#include "ff.h"
#include <float.h>
#include <math.h>
#include <string.h>
#include "am_util.h"

#ifndef IVF_NPROBE
#define IVF_NPROBE 1
#endif

/* Cosine distance: 1 - cos_sim. For normalized vectors, 1 - dot(a,b). */
static float cosine_distance(const float *a, const float *b, int dim)
{
    float dot = 0.f, na = 0.f, nb = 0.f;
    for (int i = 0; i < dim; i++)
    {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na <= 0.f || nb <= 0.f)
        return 1.f;
    return 1.f - (dot / (sqrtf(na) * sqrtf(nb)));
}

/* L2 squared (avoids sqrt). Use for argmin. */
static float l2_sq(const float *a, const float *b, int dim)
{
    float sum = 0.f;
    for (int i = 0; i < dim; i++)
    {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sum;
}

/**
 * Find cluster index with smallest distance to query.
 * centroids: [IVF_NUM_CLUSTERS][IVF_EMB_DIM].
 */
static int find_nearest_centroid(const float *query, const float *centroids)
{
    int best_k = 0;
    float best_d = FLT_MAX;
    for (int k = 0; k < IVF_NUM_CLUSTERS; k++)
    {
        float d = l2_sq(query, centroids + k * IVF_EMB_DIM, IVF_EMB_DIM);
        if (d < best_d)
        {
            best_d = d;
            best_k = k;
        }
    }
    return best_k;
}

/* Centroids and bucket metadata loaded from SD once (init or first retrieval). */
static float centroids_ram[IVF_NUM_CLUSTERS * IVF_EMB_DIM];
static int32_t bucket_offsets_ram[IVF_NUM_CLUSTERS];
static int32_t bucket_lengths_ram[IVF_NUM_CLUSTERS];
static int metadata_loaded = 0;

/**
 * Load centroids and bucket metadata from SD into RAM (once).
 * Call explicitly at startup to avoid SD opens on first retrieval
 * Returns 0 on success, <0 on error.
 */
int ivf_retrieve_init(void)
{
    if (metadata_loaded)
        return 0;

    FIL file;
    UINT n;

    if (f_open(&file, "centroids.bin", FA_READ) != FR_OK)
        return -1;
    if (f_read(&file, centroids_ram, sizeof(centroids_ram), &n) != FR_OK || n != sizeof(centroids_ram))
    {
        f_close(&file);
        return -2;
    }
    f_close(&file);

    if (f_open(&file, "bucket_offsets.bin", FA_READ) != FR_OK)
        return -3;
    if (f_read(&file, bucket_offsets_ram, sizeof(bucket_offsets_ram), &n) != FR_OK || n != sizeof(bucket_offsets_ram))
    {
        f_close(&file);
        return -4;
    }
    f_close(&file);

    if (f_open(&file, "bucket_lengths.bin", FA_READ) != FR_OK)
        return -5;
    if (f_read(&file, bucket_lengths_ram, sizeof(bucket_lengths_ram), &n) != FR_OK || n != sizeof(bucket_lengths_ram))
    {
        f_close(&file);
        return -6;
    }
    f_close(&file);

    metadata_loaded = 1;
    return 0;
}

/**
 * Retrieve closest entry and its label from the IVF index.
 *
 * image: pointer to RGB uint8 image, size INPUT_HEIGHT * INPUT_WIDTH * 3
 *        (e.g. cifar10_test_image[3072]).
 * bucket_buf: workspace for one bucket; size at least
 *             IVF_BUCKET_BUF_VECTORS * IVF_EMB_DIM * sizeof(float).
 * out_label: output classification label (0 .. NUM_CLASSES-1).
 * out_distance: output distance of nearest neighbor (e.g. cosine or L2).
 *
 * Returns 0 on success, <0 on error (e.g. SD read failure, empty bucket).
 */
int ivf_retrieve_closest(
    const uint8_t *image,
    float *bucket_buf,
    int32_t *out_label,
    float *out_distance,
    ivf_profile_t *out_profile,
    uint32_t (*get_cycles)(void))
{
    float query[IVF_EMB_DIM];
    int do_profile = (out_profile != nullptr && get_cycles != nullptr);
    uint32_t t0;

    // Preprocess and run TFLite to get query embedding (profiled in sub-steps when PROFILING)
    if (do_profile)
        t0 = get_cycles();
    model_preprocess_for_embedding(image);
    if (do_profile)
    {
        out_profile->embedding_preprocess_cyc = get_cycles() - t0;
        t0 = get_cycles();
    }
    if (model_invoke_for_embedding() != 0)
        return -2;
    if (do_profile)
    {
        out_profile->embedding_invoke_cyc = get_cycles() - t0;
        t0 = get_cycles();
    }
    model_get_embedding(query, IVF_EMB_DIM);
    if (do_profile)
    {
        out_profile->embedding_get_cyc = get_cycles() - t0;
        out_profile->embedding_cyc = out_profile->embedding_preprocess_cyc
                                   + out_profile->embedding_invoke_cyc
                                   + out_profile->embedding_get_cyc;
    }

    // Nearest centroid cluster index k
    if (do_profile)
        t0 = get_cycles();
    int k = find_nearest_centroid(query, centroids_ram);
    if (do_profile)
        out_profile->centroid_cyc = get_cycles() - t0;

#ifndef PROFILING
    am_util_stdio_printf("Nearest centroid cluster index: %d\r\n", k);
#endif

    // Use cached bucket_offsets and bucket_lengths
    int32_t offset = bucket_offsets_ram[k];
    int32_t length = bucket_lengths_ram[k];
    if (length <= 0)
    {
        *out_label = -1;
        *out_distance = -1.f;
        return -9; // empty bucket
    }

    // Read this bucket's vectors from db_vectors.bin (one seek + one read)
    // Layout: contiguous (N, D) float32 → byte offset = offset * IVF_EMB_DIM * 4
    if (length > IVF_BUCKET_BUF_VECTORS)
        return -10; // bucket too large for provided buffer
    size_t vec_size = (size_t)IVF_EMB_DIM * sizeof(float);
    size_t bucket_bytes = (size_t)length * vec_size;

    FIL file;
    UINT n;
    FRESULT fr;
    if (do_profile)
        t0 = get_cycles();
    fr = f_open(&file, "db_vectors.bin", FA_READ);
    if (fr != FR_OK)
    {
#ifndef PROFILING
        am_util_stdio_printf("Failed to open db_vectors.bin: %d\r\n", fr);
#endif
        return -11; // open error
    }
    fr = f_lseek(&file, (FSIZE_t)((size_t)offset * vec_size));
    if (fr != FR_OK)
    {
#ifndef PROFILING
        am_util_stdio_printf("Failed to seek db_vectors.bin: %d\r\n", fr);
#endif
        f_close(&file);
        return -12; // seek error
    }
    fr = f_read(&file, bucket_buf, (UINT)bucket_bytes, &n);
    if (fr != FR_OK || n != (UINT)bucket_bytes)
    {
#ifndef PROFILING
        am_util_stdio_printf("Failed to read db_vectors.bin: %d, bucket_bytes: %d\r\n", fr, bucket_bytes);
#endif
        f_close(&file);
        return -13; // read error
    }
    f_close(&file);
    if (do_profile)
        out_profile->bucket_load_cyc = get_cycles() - t0;

    // Find nearest vector in bucket
    if (do_profile)
        t0 = get_cycles();
    float *bucket_vectors = bucket_buf;
    int best_local = 0;
    float best_d = FLT_MAX;
    for (int i = 0; i < length; i++)
    {
        float d = l2_sq(query, bucket_vectors + i * IVF_EMB_DIM, IVF_EMB_DIM);
        if (d < best_d)
        {
            best_d = d;
            best_local = i;
        }
    }
    if (do_profile)
        out_profile->search_cyc = get_cycles() - t0;
    int32_t global_index = offset + best_local;

#ifndef PROFILING
    am_util_stdio_printf("Nearest vector in bucket: %d, distance: %f\r\n", best_local, best_d);
#endif

    if (out_distance)
        *out_distance = cosine_distance(query, bucket_vectors + best_local * IVF_EMB_DIM, IVF_EMB_DIM);

    // Read label at global_index from db_labels.bin (one seek + one read)
    if (do_profile)
        t0 = get_cycles();
    if (f_open(&file, "db_labels.bin", FA_READ) != FR_OK)
        return -14;
    if (f_lseek(&file, (FSIZE_t)((size_t)global_index * sizeof(int32_t))) != FR_OK)
    {
        f_close(&file);
        return -15;
    }
    if (f_read(&file, out_label, sizeof(int32_t), &n) != FR_OK || n != sizeof(int32_t))
    {
        f_close(&file);
        return -16;
    }
    f_close(&file);
    if (do_profile)
        out_profile->label_read_cyc = get_cycles() - t0;

    return 0;
}
