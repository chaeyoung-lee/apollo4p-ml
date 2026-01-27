#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "uart.h"
#include "model/model_inference.h"
#include "model/model_settings.h"
#include "model/cifar10_test_image.h"

int main(int argc, char *argv[])
{
    // Initialize
    am_bsp_low_power_init(); // Initialize Apollo 4 Plus system
    uart_init();             // Initialize UART for printf

    // Print welcome message
    am_util_stdio_printf("\r\n========================================\r\n");
    am_util_stdio_printf("\r\nCIFAR-10 Inference on Apollo 4 Plus\r\n\r\n");
    am_util_stdio_printf("\r\n========================================\r\n\r\n");

    // Initialize model
    if (model_init() != 0)
    {
        am_util_stdio_printf("Failed to initialize model. Halting.\r\n"); // Print error message
        while (1)
        {
        } // Halt the program
    }

    // Use default CIFAR-10 test image
    const uint8_t *image_data = cifar10_test_image;

    // Run inference
    if (model_run_inference(image_data) == 0)
    {
        // Print results
        model_print_results();
    }
    else
    {
        am_util_stdio_printf("Inference failed.\r\n");
    }

    am_util_stdio_printf("\r\nInference complete. Entering idle loop.\r\n");
    while (1)
    {
        am_hal_delay_us(1000000);
    }

    return 0;
}
