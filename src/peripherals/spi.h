#ifdef __cplusplus
extern "C"
{
#endif

#ifndef SPI_H
#define SPI_H

#include "am_bsp.h"
#include "am_mcu_apollo.h"
#include "am_util_stdio.h"
#include "am_util_delay.h"
#include "uart.h"
#include "am_bsp_pins.h"
#include "apollo4p.h"
#include "sd_cmd.h"

#include <stdlib.h>
#include <string.h>

    void *spi_init(uint32_t module_no, uint32_t clock_speed); // initialization

    // SPI read/write functions, without register specification
    uint32_t spi_write_byte(void *phSPI, uint8_t data, bool continue_transfer);
    uint32_t spi_read_byte(void *phSPI, uint8_t *data, bool continue_transfer);
    uint32_t spi_write_bytes(void *phSPI, uint8_t *data, uint32_t length, bool continue_transfer);
    uint32_t spi_read_bytes(void *phSPI, uint8_t *data, uint32_t length, bool continue_transfer);
    uint32_t spi_write_read(void *phSPI, uint8_t command, uint8_t *response, bool continue_transfer); // specifies both the RX buffer and the TX data

    // SPI read/write functions, with register specification
    uint32_t spi_read_register(void *phSPI, uint8_t reg_addr, uint8_t *value, bool continue_transfer);
    uint32_t spi_write_register(void *phSPI, uint8_t reg_addr, uint8_t value, bool continue_transfer);

    // specifically intended to read to a large buffer in multiple large transfers.
    uint32_t spi_read_bytes_to_shared_buffer(void *phSPI, uint8_t *data, uint32_t length);

    // reset the SPI bus
    void spi_bus_reset(void *phSPI);

    // test function for SPI functions
    void spi_test(void);

#endif // SPI_H

#ifdef __cplusplus
}
#endif