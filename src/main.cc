#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "hal/am_hal_gpio.h"
#include "am_util.h"
#include "uart.h"
#include "ff.h"
#include "model/model_inference.h"
#include "model/model_settings.h"
#include "cifar10_test_images.h"
#include "ivf/ivf_retrieval.h"
#include <cstdio>
#include <cstring>

#define SD_IMAGE_DIR "img"
#define SD_NUM_IMAGES 20
#define SD_IMAGE_BYTES (INPUT_HEIGHT * INPUT_WIDTH * INPUT_CHANNELS)

/* Enable PROFILING (e.g. make CFLAGS+=-DPROFILING) to disable per-query prints and report timing. */
#ifdef PROFILING
/* DWT cycle counter: enable once so we can measure IVF and TFLite query time. */
static void profiler_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static inline uint32_t profiler_cycles(void) { return DWT->CYCCNT; }

/* Calibration test: measure a known delay (100ms) to verify DWT cycle counter matches CPU clock. */
static void profiler_calibrate(void)
{
    am_util_stdio_printf("\r\n--- DWT Cycle Counter Calibration ---\r\n");

    // Test 1: Measure 100ms delay (100000 microseconds)
    const uint32_t delay_us = 100000;                     // 100ms
    const uint32_t expected_cycles_96mhz = delay_us * 96; // At 96 MHz: 100ms = 9,600,000 cycles

    uint32_t t0 = profiler_cycles();
    am_hal_delay_us(delay_us);
    uint32_t t1 = profiler_cycles();
    uint32_t measured_cycles = t1 - t0;

    // Calculate effective clock rate (MHz)
    float effective_mhz = (float)measured_cycles / (float)delay_us;

    am_util_stdio_printf("Delay test: %u us delay\r\n", delay_us);
    am_util_stdio_printf("Measured: %lu cycles\r\n", (unsigned long)measured_cycles);
    am_util_stdio_printf("Expected at 96 MHz: %lu cycles\r\n", (unsigned long)expected_cycles_96mhz);
    am_util_stdio_printf("Effective clock: %.2f MHz\r\n", effective_mhz);

    // Test 2: Measure 10ms delay for shorter test
    const uint32_t delay_us2 = 10000;                       // 10ms
    const uint32_t expected_cycles_96mhz2 = delay_us2 * 96; // At 96 MHz: 10ms = 960,000 cycles

    t0 = profiler_cycles();
    am_hal_delay_us(delay_us2);
    t1 = profiler_cycles();
    measured_cycles = t1 - t0;
    effective_mhz = (float)measured_cycles / (float)delay_us2;

    am_util_stdio_printf("Delay test 2: %u us delay\r\n", delay_us2);
    am_util_stdio_printf("Measured: %lu cycles\r\n", (unsigned long)measured_cycles);
    am_util_stdio_printf("Expected at 96 MHz: %lu cycles\r\n", (unsigned long)expected_cycles_96mhz2);
    am_util_stdio_printf("Effective clock: %.2f MHz\r\n", effective_mhz);
    am_util_stdio_printf("--- End Calibration ---\r\n\r\n");
}
#endif

static FATFS FatFs;

/** Read one 3072-byte image from SD path into buf. Returns 0 on success, -1 on error. */
static int read_image_from_sd(const char *path, uint8_t *buf, size_t buf_size)
{
    FIL file;
    UINT n;
    if (f_open(&file, path, FA_READ) != FR_OK)
        return -1;
    if (f_read(&file, buf, (UINT)buf_size, &n) != FR_OK || n != (UINT)buf_size)
    {
        f_close(&file);
        return -1;
    }
    f_close(&file);
    return 0;
}

int main(void)
{
    am_bsp_low_power_init();
    uart_init();

    am_util_stdio_printf("\r\n========================================\r\n");
    am_util_stdio_printf("CIFAR-10 IVF Retrieval on Apollo 4 Plus\r\n");
    am_util_stdio_printf("========================================\r\n\r\n");

    // ML model initialization
    if (model_init() != 0)
    {
        am_util_stdio_printf("Failed to initialize model. Halting.\r\n");
        while (1)
        {
        }
    }

    // SD card initialization
    // Please flash the micro SD card to exFAT system on your laptop; if not, init will fail.
    if (f_mount(&FatFs, "", 1) != FR_OK)
    {
        am_util_stdio_printf("Failed to mount SD card. Halting.\r\n");
        while (1)
        {
        }
    }
    am_util_stdio_printf("SD card file system mounted.\r\n");

    // Sanity check for SD + FatFs
    // Read file "log.txt" from SD card and print first line on serial
    FIL file;
    char line[128];
    if (f_open(&file, "log.txt", FA_READ) == FR_OK)
    {
        UINT n;
        if (f_read(&file, line, sizeof(line) - 1, &n) == FR_OK && n > 0)
        {
            line[n] = '\0';
            char *eol = line;
            while (*eol != '\0' && *eol != '\n' && *eol != '\r')
                eol++;
            *eol = '\0';
        }
        else
        {
            line[0] = '\0';
        }
        f_close(&file);
        am_util_stdio_printf("Read from log.txt: %s\r\n", line);
    }
    else
    {
        am_util_stdio_printf("Failed to open log.txt\r\n");
    }

    if (ivf_retrieve_init() != 0)
    {
        am_util_stdio_printf("Failed to initialize IVF retrieval. Halting.\r\n");
        while (1)
        {
        }
    }
    am_util_stdio_printf("IVF index loaded to RAM.\r\n");

    // Workspace for IVF buckets and input image buffer
    static float bucket_buf[IVF_BUCKET_BUF_VECTORS * IVF_EMB_DIM];
    static uint8_t image[INPUT_HEIGHT * INPUT_WIDTH * INPUT_CHANNELS];
    int32_t label = -1;
    float distance = -1.f;

    am_util_stdio_printf("Ready to receive CIFAR-10 images over UART.\r\n");

    // Initialize and turn on user LED (LED0)
    am_hal_gpio_pinconfig(AM_BSP_GPIO_LED0, g_AM_BSP_GPIO_LED0);
    am_hal_gpio_state_write(AM_BSP_GPIO_LED0, AM_HAL_GPIO_OUTPUT_CLEAR);

#ifdef PROFILING
    profiler_init();
    profiler_calibrate(); // Verify DWT cycle counter matches CPU clock
    uint64_t total_ivf_cycles = 0;
    uint64_t total_tflite_cycles = 0;
    uint64_t total_embedding_cyc = 0, total_embedding_preprocess_cyc = 0;
    uint64_t total_embedding_invoke_cyc = 0, total_embedding_get_cyc = 0;
    uint64_t total_centroid_cyc = 0, total_bucket_load_cyc = 0;
    uint64_t total_search_cyc = 0, total_label_read_cyc = 0;
    int successful_iterations = 0;
#endif
    for (int i = 0; i < SD_NUM_IMAGES; i++)
    // for (int j = 0; j < 18; j++)
    {
        // int i = 16;
#ifndef PROFILING
        am_util_stdio_printf("[%d/%d]\r\n", i, SD_NUM_IMAGES);
#endif
        char path[24];
        snprintf(path, sizeof(path), "%s/%d.bin", SD_IMAGE_DIR, i);
        if (read_image_from_sd(path, image, SD_IMAGE_BYTES) != 0)
        {
#ifndef PROFILING
            am_util_stdio_printf("Failed to read %s\r\n", path);
#endif
            continue;
        }
#ifdef PROFILING
        ivf_profile_t ivf_profile;
        uint32_t t0 = profiler_cycles();
#endif
        int ret = ivf_retrieve_closest(
            image,
            bucket_buf,
            &label,
            &distance,
#ifdef PROFILING
            &ivf_profile,
            profiler_cycles
#else
            NULL,
            NULL
#endif
        );
#ifdef PROFILING
        uint32_t ivf_cycles = profiler_cycles() - t0;
#endif

        int tflite_label;
#ifdef PROFILING
        t0 = profiler_cycles();
#endif
        tflite_label = model_predict_class(image);
#ifdef PROFILING
        uint32_t tflite_cycles = profiler_cycles() - t0;
        /* Report total IVF and per-step CPU cycles; 1 ms = 96k cycles */
        am_util_stdio_printf("[%d] IVF: %lu cyc (emb:%lu cen:%lu bucket:%lu search:%lu label:%lu) TFLite: %lu cyc\r\n",
                             i, (unsigned long)ivf_cycles,
                             (unsigned long)ivf_profile.embedding_cyc,
                             (unsigned long)ivf_profile.centroid_cyc,
                             (unsigned long)ivf_profile.bucket_load_cyc,
                             (unsigned long)ivf_profile.search_cyc,
                             (unsigned long)ivf_profile.label_read_cyc,
                             (unsigned long)tflite_cycles);
        am_util_stdio_printf("  embedding: preprocess %lu (%.2f ms) invoke %lu (%.2f ms) get_emb %lu (%.2f ms)\r\n",
                             (unsigned long)ivf_profile.embedding_preprocess_cyc,
                             (double)ivf_profile.embedding_preprocess_cyc / 96000.0,
                             (unsigned long)ivf_profile.embedding_invoke_cyc,
                             (double)ivf_profile.embedding_invoke_cyc / 96000.0,
                             (unsigned long)ivf_profile.embedding_get_cyc,
                             (double)ivf_profile.embedding_get_cyc / 96000.0);
        total_ivf_cycles += ivf_cycles;
        total_tflite_cycles += tflite_cycles;
        total_embedding_cyc += ivf_profile.embedding_cyc;
        total_embedding_preprocess_cyc += ivf_profile.embedding_preprocess_cyc;
        total_embedding_invoke_cyc += ivf_profile.embedding_invoke_cyc;
        total_embedding_get_cyc += ivf_profile.embedding_get_cyc;
        total_centroid_cyc += ivf_profile.centroid_cyc;
        total_bucket_load_cyc += ivf_profile.bucket_load_cyc;
        total_search_cyc += ivf_profile.search_cyc;
        total_label_read_cyc += ivf_profile.label_read_cyc;
        successful_iterations++;
#else
        (void)tflite_label; /* may be unused if only IVF result is used */
#endif
        if (ret != 0)
        {
#ifndef PROFILING
            am_util_stdio_printf("Failed to retrieve closest image: ret=%d\r\n", ret);
#endif
            continue;
        }
#ifndef PROFILING
        am_util_stdio_printf("Processed one image: IVF label=%d, distance=%.4f, TFLite label=%d\r\n",
                             (int)label, (double)distance, tflite_label);
#endif
    }

#ifdef PROFILING
    /* Print average cycles and per-step breakdown */
    if (successful_iterations > 0)
    {
        uint64_t n = (uint64_t)successful_iterations;
        uint64_t avg_ivf = total_ivf_cycles / n;
        uint64_t avg_emb = total_embedding_cyc / n, avg_cen = total_centroid_cyc / n;
        uint64_t avg_bucket = total_bucket_load_cyc / n;
        uint64_t avg_search = total_search_cyc / n, avg_label = total_label_read_cyc / n;
        uint64_t avg_tflite_cycles = total_tflite_cycles / n;
        am_util_stdio_printf("\r\n--- Summary ---\r\n");
        am_util_stdio_printf("Processed %d images\r\n", successful_iterations);
        am_util_stdio_printf("Average IVF total: %llu cyc (%.2f ms)\r\n",
                             (unsigned long long)avg_ivf, (double)avg_ivf / 96000.0);
        am_util_stdio_printf("  embedding:   %llu cyc (%.2f ms)\r\n",
                             (unsigned long long)avg_emb, (double)avg_emb / 96000.0);
        {
            uint64_t avg_pre = total_embedding_preprocess_cyc / n, avg_inv = total_embedding_invoke_cyc / n;
            uint64_t avg_get = total_embedding_get_cyc / n;
            am_util_stdio_printf("    preprocess: %llu cyc (%.2f ms)\r\n",
                                 (unsigned long long)avg_pre, (double)avg_pre / 96000.0);
            am_util_stdio_printf("    invoke:     %llu cyc (%.2f ms)\r\n",
                                 (unsigned long long)avg_inv, (double)avg_inv / 96000.0);
            am_util_stdio_printf("    get_emb:    %llu cyc (%.2f ms)\r\n",
                                 (unsigned long long)avg_get, (double)avg_get / 96000.0);
        }
        am_util_stdio_printf("  centroid:    %llu cyc (%.2f ms)\r\n",
                             (unsigned long long)avg_cen, (double)avg_cen / 96000.0);
        am_util_stdio_printf("  bucket_load: %llu cyc (%.2f ms)\r\n",
                             (unsigned long long)avg_bucket, (double)avg_bucket / 96000.0);
        am_util_stdio_printf("  search:      %llu cyc (%.2f ms)\r\n",
                             (unsigned long long)avg_search, (double)avg_search / 96000.0);
        am_util_stdio_printf("  label_read:  %llu cyc (%.2f ms)\r\n",
                             (unsigned long long)avg_label, (double)avg_label / 96000.0);
        am_util_stdio_printf("Average TFLite: %llu cyc (%.2f ms)\r\n",
                             (unsigned long long)avg_tflite_cycles, (double)avg_tflite_cycles / 96000.0);
        am_util_stdio_printf("--- End Summary ---\r\n\r\n");
    }
#endif

    // Main loop for UART testing
    while (1)
    {
        // Populate input image either from UART (live) or from a built-in test image.
#ifdef UART_TEST
        // Read one image (3072 bytes) from UART
        for (int i = 0; i < INPUT_HEIGHT * INPUT_WIDTH * INPUT_CHANNELS; i++)
        {
            int c = uart_getchar();
            image[i] = (uint8_t)c;
        }

        // Run IVF retrieval
        int ret = ivf_retrieve_closest(
            image,
            bucket_buf,
            &label,
            &distance,
            NULL,
            NULL);

        if (ret != 0)
        {
            // On error, return sentinel values.
            label = ret;
            distance = -1.0f;

            // Reset SD card
            f_mount(NULL, "", 0);
            f_mount(&FatFs, "", 1);
        }

        // Run TFLite model classification
        int tflite_label = model_predict_class(image);

        // Pack response as: int32 label, float32 distance (little-endian)
        struct __attribute__((packed))
        {
            int32_t label;
            float distance;
            int tflite_label;
        } resp;

        resp.label = label;
        resp.distance = distance;
        resp.tflite_label = tflite_label;

        uart_write_bytes((const uint8_t *)&resp, sizeof(resp));
#else
        am_hal_delay_us(1000000);
#endif
    }
    return 0;
}
