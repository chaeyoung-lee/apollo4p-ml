/*
 * SD card access and read/write functions using SPI
 */

#include "sd_spi.h"
#include "am_util.h"
#include <stddef.h>

static void *g_sd_phSPI = NULL;

/*
 * this private function forces the clock line to toggle while the CS line is high.
 * This is required in the initialization process.
 *
 * @param module_no: the module number of the SPI bus
 * @param phSPI: the pointer to the SPI bus
 * @param num_cycles: the number of clock cycles to toggle
 * @return: void
 */
static void sd_spi_clock_pulse_operation(uint8_t module_no, void *phSPI, uint8_t num_cycles)
{

    uint8_t sck_pin, cs_pin;
    switch (module_no)
    {
    case 0:
        sck_pin = 5;
        cs_pin = 72;
        break;
    case 1:
        sck_pin = 8;
        cs_pin = 11;
        break;
    case 2:
        sck_pin = 25;
        cs_pin = 37;
        break;
    case 3:
        sck_pin = 31;
        cs_pin = 85;
        break;
    case 4:
        sck_pin = 34;
        cs_pin = 79;
        break;
    case 5:
        sck_pin = 47;
        cs_pin = 60;
        break;
    case 6:
        sck_pin = 61;
        cs_pin = 30;
        break; // Fixed: SCK should be 31, not 61
    case 7:
        sck_pin = 22;
        cs_pin = 88;
        break;
    default:
        am_util_stdio_printf("Invalid module number\n\r");
        return;
    }

    // for manual control over the SPI bus
    am_hal_iom_disable(phSPI);
    am_hal_gpio_pinconfig(cs_pin, am_hal_gpio_pincfg_output);
    am_hal_gpio_pinconfig(sck_pin, am_hal_gpio_pincfg_output);

    // drive CS high (inactive) and SCK low initially
    am_hal_gpio_state_write(cs_pin, AM_HAL_GPIO_OUTPUT_SET);
    am_hal_gpio_state_write(sck_pin, AM_HAL_GPIO_OUTPUT_CLEAR);

    // toggle SCK for num_cycles
    uint32_t half_period = 5; // 100kHz = 5μs half period
    for (uint8_t i = 0; i < num_cycles; i++)
    {
        am_hal_gpio_state_write(sck_pin, AM_HAL_GPIO_OUTPUT_SET);
        am_hal_delay_us(half_period);
        am_hal_gpio_state_write(sck_pin, AM_HAL_GPIO_OUTPUT_CLEAR);
        am_hal_delay_us(half_period);
    }

    // restore SPI setting
    am_hal_gpio_pinconfig(cs_pin, g_AM_BSP_GPIO_IOM6_CS);
    am_hal_gpio_pinconfig(sck_pin, g_AM_BSP_GPIO_IOM6_SCK);
    am_hal_iom_enable(phSPI);
}

/*
 * initialize the SD card using SPI
 *
 * @param module_no: the module number of the SPI bus
 * @param clock_speed: the clock speed to initialize the SPI bus to
 * @return: a pointer to the SPI bus
 */
void *sd_spi_init(uint8_t module_no, uint32_t clock_speed)
{

    // to allow for repeated attempts at initialization if it fails initially
    enum sd_init_status init_status = SENDING_CMD0;
    uint8_t response;
    uint8_t rx_buffer[5];

    // initialize the SPI bus, but first lower the clock speed to 100kHz (required for SD init)
    void *phSPI = spi_init(module_no, AM_HAL_IOM_100KHZ);
    uint32_t counter = 0;
    am_hal_gpio_pinconfig(3, am_hal_gpio_pincfg_input); // initialize the GPIO pin that is connected to card detect
    uint8_t init_done_flag = 0;

    // 74 clock cycles with CS high
    sd_spi_clock_pulse_operation(module_no, phSPI, 74);

    // command type specifies the command to send, the CRC, and the argument in the command.
    sd_spi_cmd_t cmd;

#define SD_INIT_TIMEOUT_LOOPS 500

    uint32_t init_loop_count = 0;
    while (init_done_flag == 0)
    {
        if (++init_loop_count >= SD_INIT_TIMEOUT_LOOPS)
        {
            am_util_stdio_printf("SD card init timeout (no card or not ready)\r\n");
            am_hal_iom_disable(phSPI);
            am_hal_iom_uninitialize(phSPI);
            return NULL;
        }
        switch (init_status)
        {
        case SENDING_CMD0:
            // send CMD0, expect 0x01
            cmd.cmd = CMD0;
            cmd.arg = 0;
            cmd.crc = 0x95; // CRC must be correct for CMD0
            if (sd_spi_write_command(phSPI, &cmd, &response, 1, false) != AM_HAL_STATUS_SUCCESS)
            {
                response = 0xFF;
            }
            if (response != R1_IDLE)
            {
                if (counter >= 5 && response == R1_ILLEGAL_VALUE)
                {
                    // am_util_stdio_printf("Still getting 0xFF after CMD0, moving on to CMD8...\n\r");
                    counter = 0;
                    init_status = SENDING_CMD8;
                }
                else if (counter >= 5)
                {
                    am_util_stdio_printf("SD card initialization failed after CMD0...\n\r");
                    return NULL;
                }
                else
                {
                    // am_util_stdio_printf("SD card initialization stalling after sending CMD0, retrying...\n\r");
                    am_util_delay_ms(10);
                    counter++;
                }
            }
            else
            {
                // am_util_stdio_printf("CMD0 successful, moving on to CMD8...\n\r");
                init_status = SENDING_CMD8;
            }
            break;
        case SENDING_CMD8:
            // send CMD8, expect 0x01 followed by echo of arg 0x1AA
            cmd.cmd = CMD8;
            cmd.arg = 0x1AA;
            cmd.crc = 0x87; // CRC must be correct for CMD8
            if (sd_spi_write_command(phSPI, &cmd, rx_buffer, 5, false) != AM_HAL_STATUS_SUCCESS)
            {
                am_util_stdio_printf("SD card init failed after CMD8 (timeout). Check card present and MISO wiring.\r\n");
                am_hal_iom_disable(phSPI);
                am_hal_iom_uninitialize(phSPI);
                return NULL;
            }
            if (rx_buffer[0] == (R1_IDLE | R1_ILLEGAL_COMMAND))
            {
                init_status = SENDING_CMD55;
                break;
            }
            if (rx_buffer[0] != R1_IDLE)
            {
                return NULL;
            }
            uint32_t echo = (rx_buffer[1] << 24) | (rx_buffer[2] << 16) | (rx_buffer[3] << 8) | rx_buffer[4];
            if (echo != 0x1AA)
            {
                am_util_stdio_printf("SD card init failed after CMD8 (bad echo).\r\n");
                return NULL;
            }
            init_status = SENDING_CMD55;
            break;
        case SENDING_CMD55:
            // sending CMD55, expect 0x01
            cmd.cmd = CMD55;
            cmd.arg = 0x0;
            cmd.crc = 0x65; // not required, but correct CRC produces more reliable initialization cycles
            if (sd_spi_write_command(phSPI, &cmd, &response, 1, false) != AM_HAL_STATUS_SUCCESS)
            {
                response = 0xFF;
            }
            if (response != R1_IDLE)
            {
                if (counter > 5)
                {
                    am_util_stdio_printf("SD card initialization failed after CMD55...\n\r");
                    init_status = SENDING_CMD0;
                    counter = 0;
                }
                else
                {
                    // am_util_stdio_printf("SD card initialization stalling after sending CMD55, retrying...\n\r");
                    am_util_delay_ms(10);
                    counter++;
                }
            }
            else
            {
                // am_util_stdio_printf("CMD55 successful, moving on to ACMD41...\n\r");
                init_status = SENDING_ACMD41;
            }
            break;
        case SENDING_ACMD41:
            // sending ACMD41, expect 0x00
            // if passes through CMD55, send ACMD41, expect 0x01
            cmd.cmd = CMD41;
            cmd.arg = 0x40000000;
            cmd.crc = 0x77; // not required, but correct CRC produces more reliable initialization cycles
            if (sd_spi_write_command(phSPI, &cmd, &response, 1, false) != AM_HAL_STATUS_SUCCESS)
            {
                response = 0xFF;
            }
            if (response != R1_SUCCESS)
            {
                // am_util_stdio_printf("SD card initialization stalling after sending ACMD41, trying CMD55 again..\n\r");
                am_util_delay_ms(10);
                init_status = SENDING_CMD55;
            }
            else
            {
                // am_util_stdio_printf("ACMD41 successful\n\r");;
                init_done_flag = 1;
            }
            break;
        default:
            init_status = SENDING_CMD0;
            break;
        }
    }
    // if got here, send out final 8 clock cycles
    sd_spi_clock_pulse_operation(module_no, phSPI, 8);

    am_hal_iom_config_t iomConfig = {
        .eInterfaceMode = AM_HAL_IOM_SPI_MODE,
        .ui32ClockFreq = clock_speed,      // 1MHz SPI clock
        .eSpiMode = AM_HAL_IOM_SPI_MODE_0, // SPI mode 0 (CPOL=0, CPHA=0)
        .pNBTxnBuf = NULL,
        .ui32NBTxnBufLength = 0};

    // re-set the clock speed to the user-specified speed
    am_hal_iom_disable(phSPI);
    uint32_t status = am_hal_iom_configure(phSPI, &iomConfig);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        am_util_stdio_printf("Failed to configure IOM\n\r");
        return NULL;
    }
    am_hal_iom_enable(phSPI);

    am_util_stdio_printf("SD card initialization successful!\n\r");

    g_sd_phSPI = phSPI;
    return phSPI;
}

/*
 * simple helper to detect whether SD card is present by reading GPIO
 *
 * @return: 1 if SD card is present, 0 if not or if error has occurred
 */
uint8_t sd_spi_card_detect(void)
{
    uint32_t card_detect_status;
    uint32_t status = am_hal_gpio_state_read(3, AM_HAL_GPIO_INPUT_READ, &card_detect_status);
    if (card_detect_status == 1 && status == AM_HAL_STATUS_SUCCESS)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/*
 * internal function to check whether the SD card is busy
 *
 * @param phSPI: the pointer to the SPI bus
 * @return: 0 if SD card is not busy, 1 if SD card is busy, 2 if error has occurred
 */
// does so by polling the MISO line of the SD card and checking if it is in the idle state
uint8_t sd_spi_check_busy_status(void *phSPI)
{
    uint8_t tx_buf[1] = {LINE_NOT_BUSY};
    uint8_t rx_buf[1] = {0x00};

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = 1;
    xfer.pui32TxBuffer = (uint32_t *)tx_buf;
    xfer.pui32RxBuffer = (uint32_t *)rx_buf;
    xfer.bContinue = false;

    uint32_t status = am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        am_util_stdio_printf("SD SPI error: 0x%08X\n\r", status);
        return 2;
    }

    if (rx_buf[0] == LINE_NOT_BUSY)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/*
 * this function writes an SD command to the SD card and returns the command response
 *
 * @param phSPI: the pointer to the SPI bus
 * @param cmd: the command to send, as a sd_spi_cmd_t struct
 * @param rx_buffer: the buffer to store the command response in
 * @param rx_length: the length of the expected command response
 * @param continue_transfer: whether to continue the transfer after the command is sent (CS kept low if true)
 * @return: AM_HAL_STATUS_SUCCESS if successful, AM_HAL_STATUS_FAIL if error has occurred
 */
uint32_t sd_spi_write_command(void *phSPI, sd_spi_cmd_t *cmd, uint8_t *rx_buffer, uint32_t rx_length, bool continue_transfer)
{

    // wait for busy status to be 0
    // busy wait for SD card to be ready
    uint32_t busy_error_counter = 0;
    uint8_t busy_status = 1;
    do
    {
        if (cmd->cmd == CMD25 || cmd->cmd == CMD23)
        { // because CMD55 already calls this function when doing multi-block write
            break;
        }
        busy_status = sd_spi_check_busy_status(phSPI);
        if (busy_status == 2)
        {
            return AM_HAL_STATUS_FAIL;
        }
        busy_error_counter++;
        if (busy_error_counter > 100 && cmd->cmd == CMD0)
        { // this is because sometimes the SD card MISO is open drain and will not be pulled up
            break;
        }
        if (busy_error_counter > 1000)
        {
            am_util_stdio_printf("SD error: busy status error\n\r");
            return AM_HAL_STATUS_FAIL;
        }
    } while (busy_status == 1);

    uint8_t tx_cmd_packet[6] = {(0x40 | (cmd->cmd & 0x3F)),
                                (uint8_t)(cmd->arg >> 24),
                                (uint8_t)(cmd->arg >> 16),
                                (uint8_t)(cmd->arg >> 8),
                                (uint8_t)(cmd->arg),
                                (cmd->crc ^ 0x01) | 0x01};

    // send CMD - start with a new transfer, then continue
    uint8_t tx_data_single[1];
    uint8_t rx_data_single[1] = {0xFF};
    uint32_t status = AM_HAL_STATUS_SUCCESS;

    // send bytes in a new transfer
    for (uint8_t i = 0; i < TX_CMD_SIZE; i++)
    {
        tx_data_single[0] = tx_cmd_packet[i];
        status = spi_write_read(phSPI, tx_data_single[0], rx_data_single, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
    }

    // wait for receive byte to be success byte or idle byte
    tx_data_single[0] = 0xFF;
    rx_data_single[0] = 0xFF;
    uint32_t CMD12_error_counter = 0;
#define SD_CMD_RESP_TIMEOUT 12000
    uint32_t resp_count = 0;
    if (cmd->cmd != CMD12 && cmd->cmd != CMD25)
    {
        while (rx_data_single[0] == 0xFF)
        {
            spi_write_read(phSPI, tx_data_single[0], rx_data_single, true);
            if (++resp_count >= SD_CMD_RESP_TIMEOUT)
                return AM_HAL_STATUS_FAIL;
        }
    }
    else
    { // for stop tran, should wait until success byte is received or break out
        while (rx_data_single[0] != 0x00)
        {
            spi_write_read(phSPI, tx_data_single[0], rx_data_single, true);
            CMD12_error_counter++;
            if (CMD12_error_counter > 100)
            {
                am_util_stdio_printf("SD error: CMD12 error\n\r");
                return AM_HAL_STATUS_FAIL;
            }
        }
    }

    memcpy(rx_buffer, rx_data_single, 1);

    if ((tx_cmd_packet[0] & 0x3F) == CMD8)
    {
        // receive rest of bytes one at a time
        for (uint8_t i = 1; i < 5; i++)
        {
            spi_write_read(phSPI, tx_data_single[0], &rx_buffer[i], true);
        }
    }

    // if successful, send a dummy byte to end the transfer
    uint8_t dummy_rx_byte[1] = {0xFF};
    if (!continue_transfer)
    {
        uint32_t status = spi_write_read(phSPI, 0xFF, rx_data_single, false);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
    }

    return AM_HAL_STATUS_SUCCESS;
}

/*
 * this function reads a single block from the SD card.
 *
 * @param phSPI: the pointer to the SPI bus
 * @param block_num: the block number to read
 * @param rx_buffer: the buffer to store the read data in
 * @param rx_length: the length of the data to read
 * @return: AM_HAL_STATUS_SUCCESS if successful, AM_HAL_STATUS_FAIL if error has occurred
 *
 * Note: block-sized buffers are assumed here, so if rx_buffer is not 512 bytes, or if rx_length does not reflect the size
 * of the rx_buffer, then the function will not work as intended.
 */
uint32_t sd_spi_read_single_block(void *phSPI, uint32_t block_num, uint8_t *rx_buffer, uint32_t rx_length)
{

    // check valid data length
    if (rx_length != 512)
    {
        am_util_stdio_printf("SD card read block size must be a multiple of 512 bytes\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    // send CMD17
    sd_spi_cmd_t cmd;
    cmd.cmd = CMD17;
    cmd.arg = block_num;
    cmd.crc = 0x00;
    uint8_t command_response[1] = {0xFF};
    uint32_t status = sd_spi_write_command(phSPI, &cmd, command_response, 1, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }
    // check for success byte
    if (command_response[0] != 0x00)
    {
        am_util_stdio_printf("SD read error: did not receive success byte from command\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    // Poll for data token 0xFE. Host must always send 0xFF (SD spec); never echo
    // the card's response. Use a long timeout so card has time after CMD12 etc.
#define SD_CMD17_DATA_TOKEN_TIMEOUT 100000
    uint8_t rx_dummy_receive_byte[1] = {0xFF};
    uint32_t token_poll_count = 0;
    while (rx_dummy_receive_byte[0] != DATA_TOKEN_CMD17)
    {
        status = spi_write_read(phSPI, 0xFF, rx_dummy_receive_byte, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
        token_poll_count++;
        if (token_poll_count > SD_CMD17_DATA_TOKEN_TIMEOUT)
        {
            am_util_stdio_printf("SD read error: did not receive valid data token from packet\n\r");
            spi_write_read(phSPI, 0xFF, rx_dummy_receive_byte, false);
            return AM_HAL_STATUS_FAIL;
        }
    }
#undef SD_CMD17_DATA_TOKEN_TIMEOUT

    // receive data, now that the next byte is the first byte of packet
    status = spi_read_bytes(phSPI, rx_buffer, BLOCK_SIZE, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }
    // receive CRC, disabled for now so value goes into dummy byte
    status = spi_read_bytes(phSPI, rx_dummy_receive_byte, CRC_SIZE, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    // send 4 dummy bytes in total (though could be more) and terminate the transfer
    uint8_t tx_dummy_bytes[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    status = spi_write_bytes(phSPI, tx_dummy_bytes, 4, false);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    return AM_HAL_STATUS_SUCCESS;
}

/*
 * this function writes a single block to the SD card.
 *
 * @param phSPI: the pointer to the SPI bus
 * @param block_num: the block number to write
 * @param tx_block_data: the data to write
 * @param tx_length: the length of the data to write
 * @return: AM_HAL_STATUS_SUCCESS if successful, AM_HAL_STATUS_FAIL if error has occurred
 *
 * Note: block-sized buffers are assumed here, so if tx_block_data is not 512 bytes, or if tx_length does not reflect the size
 * of the tx_block_data, then the function will not work as intended.
 */
uint32_t sd_spi_write_single_block(void *phSPI, uint32_t block_num, uint8_t *tx_block_data, uint32_t tx_length)
{

    // check valid data length
    if (tx_length != 512)
    {
        am_util_stdio_printf("SD card write block size must be a multiple of 512 bytes\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    // send CMD24
    sd_spi_cmd_t cmd;
    cmd.cmd = CMD24;
    cmd.arg = block_num;
    cmd.crc = 0x00;
    uint8_t command_response[1] = {0xFF};
    uint32_t status = sd_spi_write_command(phSPI, &cmd, command_response, 1, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }
    // check for success byte
    if (command_response[0] != R1_SUCCESS)
    {
        am_util_stdio_printf("SD write error: did not receive success byte from command\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    uint8_t tx_data_byte[1];
    uint8_t rx_byte[1] = {0xFF};

    // send data token
    uint8_t tx_data_token[1] = {DATA_TOKEN_CMD24};
    status = spi_write_read(phSPI, tx_data_token[0], rx_byte, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    // send data
    status = spi_write_bytes(phSPI, tx_block_data, BLOCK_SIZE, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    // wait for data response
    memset(rx_byte, 0xFF, 1);
    while (rx_byte[0] != 0xFF)
    {
        status = spi_write_read(phSPI, tx_data_byte[0], rx_byte, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
    }
    if ((rx_byte[0] & DATA_RESP_ACCEPTED_MASK) != DATA_RESP_ACCEPTED_MASK)
    {
        if ((rx_byte[0] & DATA_CRC_ERROR_MASK) == DATA_CRC_ERROR_MASK)
        {
            am_util_stdio_printf("SD write error: CRC error\n\r");
        }
        else if ((rx_byte[0] & DATA_WRITE_ERROR_MASK) == DATA_WRITE_ERROR_MASK)
        {
            am_util_stdio_printf("SD write error: write error\n\r");
        }
        else
        {
            am_util_stdio_printf("SD write error: unknown error\n\r");
        }
        return AM_HAL_STATUS_FAIL;
    }

    // send 8 dummy bytes in total to end the transfer
    uint8_t tx_dummy_bytes[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    // to terminate the transfer
    status = spi_write_bytes(phSPI, tx_dummy_bytes, 8, false);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    return AM_HAL_STATUS_SUCCESS;
}

/*
 * this function reads multiple blocks from the SD card.
 *
 * @param phSPI: the pointer to the SPI bus
 * @param start_block_num: the block number to start reading from
 * @param num_of_blocks: the number of blocks to read
 * @param rx_buffer: the buffer to store the read data in
 * @param rx_length: the length of the data to read
 * @return: AM_HAL_STATUS_SUCCESS if successful, AM_HAL_STATUS_FAIL if error has occurred
 *
 * Note: block-aligned buffers are assumed here, so if rx_buffer is not a multiple of 512 bytes, or if rx_length does not reflect the size
 * of the rx_buffer, then the function will not work as intended.
 */
uint32_t sd_spi_read_multi_block(void *phSPI, uint32_t start_block_num, uint32_t num_of_blocks, uint8_t *rx_buffer, uint32_t rx_length)
{

    // check valid data length
    if (rx_length != 512 * num_of_blocks)
    {
        am_util_stdio_printf("SD card read multi-block size must be a multiple of 512 bytes\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    // send CMD18
    sd_spi_cmd_t cmd;
    cmd.cmd = CMD18;
    cmd.arg = start_block_num;
    cmd.crc = 0x00;
    uint8_t command_response[1] = {0xFF};
    uint32_t status = sd_spi_write_command(phSPI, &cmd, command_response, 1, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }
    // check for success byte
    if (command_response[0] != 0x00)
    {
        am_util_stdio_printf("SD read error: did not receive success byte from command\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    uint8_t tx_dummy_byte[1] = {0xFF};
    uint8_t rx_byte[1] = {0xFF};
    uint8_t rx_dummy_byte[1] = {0xFF};
    // repeat the following process for num_of_blocks times
    for (uint32_t i = 0; i < num_of_blocks; i++)
    {
        // wait for data token before data packet
        status = spi_write_read(phSPI, tx_dummy_byte[0], rx_byte, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
        while (rx_byte[0] == 0xFF)
        {
            status = spi_write_read(phSPI, tx_dummy_byte[0], rx_byte, true);
            if (status != AM_HAL_STATUS_SUCCESS)
            {
                return status;
            }
        }
        if (rx_byte[0] != DATA_TOKEN_CMD18)
        {
            am_util_stdio_printf("SD read error: did not receive valid data token\n\r");
            return AM_HAL_STATUS_FAIL;
        }

        // receive data (one block in a single transfer, like single-block path)
        status = spi_read_bytes(phSPI, &rx_buffer[i * BLOCK_SIZE], BLOCK_SIZE, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }

        // receive CRC
        for (uint8_t j = 0; j < CRC_SIZE; j++)
        {
            status = spi_write_read(phSPI, tx_dummy_byte[0], rx_dummy_byte, true);
            if (status != AM_HAL_STATUS_SUCCESS)
            {
                return status;
            }
        }
    }

    // send CMD12 to stop the transfer
    cmd.cmd = CMD12;
    cmd.arg = 0x00;
    cmd.crc = 0x00;
    status = sd_spi_write_command(phSPI, &cmd, command_response, 1, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }
    // check for success byte
    if (command_response[0] != 0x00)
    {
        am_util_stdio_printf("SD read error: did not receive success byte from command\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    // After CMD12, clock with CS held until card drives MISO high (not busy), then
    // terminate. Per SD spec this ensures the card is ready for the next command
    // (e.g. CMD17 when FatFS does multi-block then single-block in one f_read).
    rx_dummy_byte[0] = 0x00;
    uint32_t post_cmd12_clocks = 0;
#define SD_CMD12_IDLE_MAX 10000
    while (rx_dummy_byte[0] != 0xFF && post_cmd12_clocks < SD_CMD12_IDLE_MAX)
    {
        status = spi_write_read(phSPI, 0xFF, rx_dummy_byte, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
        post_cmd12_clocks++;
    }
#undef SD_CMD12_IDLE_MAX
    // Terminate transfer (release CS)
    status = spi_write_read(phSPI, 0xFF, rx_dummy_byte, false);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    return AM_HAL_STATUS_SUCCESS;
}

/*
 * this function writes multiple blocks to the SD card.
 *
 * @param phSPI: the pointer to the SPI bus
 * @param start_block_num: the block number to start writing to
 * @param num_of_blocks: the number of blocks to write
 * @param tx_block_data: the data to write
 * @param tx_length: the length of the data to write
 * @return: AM_HAL_STATUS_SUCCESS if successful, AM_HAL_STATUS_FAIL if error has occurred
 *
 * Note: block-aligned buffers are assumed here, so if tx_block_data is not a multiple of 512 bytes, or if tx_length does not reflect the size
 * of the tx_block_data, then the function will not work as intended.
 */
uint32_t sd_spi_write_multi_block(void *phSPI, uint32_t start_block_num, uint32_t num_of_blocks, uint8_t *tx_block_data, uint32_t tx_length)
{

    if (tx_length % 512 != 0)
    {
        am_util_stdio_printf("SD card write block size must be a multiple of 512 bytes\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    // need to send ACMD23 first to specify the number of blocks to write,
    // then send CMD25 to actually write the data
    // send ACMD23, which is the same as CMD55 + CMD 23
    sd_spi_cmd_t cmd;
    cmd.cmd = CMD55;
    cmd.arg = 0x00;
    cmd.crc = 0x01;
    uint8_t command_response[1] = {0xFF};
    uint32_t status = sd_spi_write_command(phSPI, &cmd, command_response, 1, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    // send CMD23
    cmd.cmd = CMD23;
    cmd.arg = num_of_blocks & 0x0EFF; // [22:0]
    cmd.crc = 0x01;
    command_response[0] = 0xFF;
    status = sd_spi_write_command(phSPI, &cmd, command_response, 1, false);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    // bring CS high for 8 clock cycles
    sd_spi_clock_pulse_operation(6, phSPI, 8);

    // send CMD25
    cmd.cmd = CMD25;
    cmd.arg = start_block_num;
    cmd.crc = 0x00;
    status = sd_spi_write_command(phSPI, &cmd, command_response, 1, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }
    // check for success byte
    if (command_response[0] != 0x00)
    {
        am_util_stdio_printf("SD write error: did not receive success byte from command\n\r");
        return AM_HAL_STATUS_FAIL;
    }

    uint8_t tx_dummy_byte[1] = {0xFF};
    uint8_t rx_byte[1] = {0xFF};
    uint8_t rx_dummy_byte[1] = {0xFF};
    // repeat the following process for num_of_blocks times
    for (uint32_t i = 0; i < num_of_blocks; i++)
    {
        // send data token
        uint8_t tx_data_token[1] = {DATA_TOKEN_CMD25};
        status = spi_write_read(phSPI, tx_data_token[0], rx_byte, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }

        // send data
        status = spi_write_bytes(phSPI, &tx_block_data[i * BLOCK_SIZE], BLOCK_SIZE, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }

        // send dummy bytes for CRC
        uint8_t tx_dummy_bytes[CRC_SIZE] = {0xFF, 0xFF};
        status = spi_write_bytes(phSPI, tx_dummy_bytes, CRC_SIZE, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }

        // wait for data response
        memset(rx_byte, 0xFF, 1);
        while (rx_byte[0] != 0xFF)
        {
            status = spi_write_read(phSPI, tx_dummy_byte[0], rx_byte, true);
            if (status != AM_HAL_STATUS_SUCCESS)
            {
                return status;
            }
        }
        if ((rx_byte[0] & DATA_RESP_ACCEPTED_MASK) != DATA_RESP_ACCEPTED_MASK)
        {
            if ((rx_byte[0] & DATA_CRC_ERROR_MASK) == DATA_CRC_ERROR_MASK)
            {
                am_util_stdio_printf("SD write error: CRC error\n\r");
            }
            else if ((rx_byte[0] & DATA_WRITE_ERROR_MASK) == DATA_WRITE_ERROR_MASK)
            {
                am_util_stdio_printf("SD write error: write error\n\r");
            }
            return AM_HAL_STATUS_FAIL;
        }

        // wait for busy status to be 0
        uint8_t busy_check_rx[1] = {0x00};
        while (busy_check_rx[0] != 0xFF)
        {
            status = spi_write_read(phSPI, tx_dummy_byte[0], busy_check_rx, true);
            if (status != AM_HAL_STATUS_SUCCESS)
            {
                return status;
            }
        }
    }

    // wait for busy status to be 0
    uint8_t busy_check_rx[1] = {0x00};
    while (busy_check_rx[0] != 0xFF)
    {
        status = spi_write_read(phSPI, tx_dummy_byte[0], busy_check_rx, true);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
    }

    // send a stop tran token
    uint8_t tx_stop_token[1] = {STOP_TRAN_TOKEN_CMD25};
    status = spi_write_read(phSPI, tx_stop_token[0], rx_dummy_byte, true);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    // after all blocks are written, send 8 dummy bytes to end the transfer
    uint8_t tx_dummy_bytes[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    status = spi_write_bytes(phSPI, tx_dummy_bytes, 8, false);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    return AM_HAL_STATUS_SUCCESS;
}