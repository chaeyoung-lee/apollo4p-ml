#ifdef __cplusplus
extern "C"
{
#endif

#ifndef SD_SPI_H
#define SD_SPI_H

#include <stdint.h>
#include "am_bsp.h"
#include "am_mcu_apollo.h"
#include "am_util_stdio.h"
#include "am_util_delay.h"
#include "uart.h"
#include "am_bsp_pins.h"
#include "apollo4p.h"
#include "am_hal_iom.h"
#include "spi.h"
#include "sd_cmd.h"

#define spi_cs 0

    enum sd_init_status
    {
        SENDING_CMD0,
        SENDING_CMD8,
        SENDING_CMD55,
        SENDING_ACMD41
    };

    typedef struct
    {
        uint8_t cmd;
        uint32_t arg;
        uint8_t crc;
    } sd_spi_cmd_t;

    void *sd_spi_init(uint8_t module_no, uint32_t clock_speed);
    uint8_t sd_spi_card_detect(void);              // uses the CD pin of the Adafruit breakout to determine whether an SD card is present
    uint8_t sd_spi_check_busy_status(void *phSPI); // check if the SD card is busy. Operations are invalid if the card is busy.

    // read/write functions for SD commands and data blocks.
    // note: tested to be reliably working at 16MHz clock speed, but anything above that is not guaranteed
    // at 24MHz specifically, this does not work reliably
    uint32_t sd_spi_write_command(void *phSPI, sd_spi_cmd_t *cmd, uint8_t *rx_buffer, uint32_t rx_length, bool continue_transfer);
    uint32_t sd_spi_read_single_block(void *phSPI, uint32_t block_num, uint8_t *rx_buffer, uint32_t rx_length);
    uint32_t sd_spi_write_single_block(void *phSPI, uint32_t block_num, uint8_t *tx_block_data, uint32_t tx_length);
    uint32_t sd_spi_read_multi_block(void *phSPI, uint32_t start_block_num, uint32_t num_of_blocks, uint8_t *rx_buffer, uint32_t rx_length);
    uint32_t sd_spi_write_multi_block(void *phSPI, uint32_t start_block_num, uint32_t num_of_blocks, uint8_t *tx_block_data, uint32_t tx_length);

    // test function (call after init to read first line from log.txt and print; remove when no longer needed)
    void sd_spi_test(void *phSPI);

#endif // SD_SPI_H

#ifdef __cplusplus
}
#endif