/*
 * This file contains the functions for SPI communication.
 * It is written by Cursor but lightly modified to work for the ArduCAM.
 */

#include "spi.h"
#include "am_util.h"
#include <stdbool.h>

uint8_t spi_cs = 0; // by default. This does not correspond to a specific pin number.

/*
 * initialize the SPI bus
 *
 * @param module_no: the module number of the SPI bus
 * @param clock_speed: the clock speed of the SPI bus
 * @return: a pointer to the SPI bus
 */
void *spi_init(uint32_t module_no, uint32_t clock_speed)
{
    // configure each SPI pin. This is specific to module 1.
    if (module_no == 0)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM0_SCK, g_AM_BSP_GPIO_IOM0_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM0_MOSI, g_AM_BSP_GPIO_IOM0_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM0_MISO, g_AM_BSP_GPIO_IOM0_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM0_CS, g_AM_BSP_GPIO_IOM0_CS);
    }
    else if (module_no == 1)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM1_SCK, g_AM_BSP_GPIO_IOM1_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM1_MOSI, g_AM_BSP_GPIO_IOM1_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM1_MISO, g_AM_BSP_GPIO_IOM1_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM1_CS, g_AM_BSP_GPIO_IOM1_CS);
    }
    else if (module_no == 2)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM2_SCK, g_AM_BSP_GPIO_IOM2_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM2_MOSI, g_AM_BSP_GPIO_IOM2_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM2_MISO, g_AM_BSP_GPIO_IOM2_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM2_CS, g_AM_BSP_GPIO_IOM2_CS);
    }
    else if (module_no == 3)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM3_SCK, g_AM_BSP_GPIO_IOM3_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM3_MOSI, g_AM_BSP_GPIO_IOM3_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM3_MISO, g_AM_BSP_GPIO_IOM3_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM3_CS, g_AM_BSP_GPIO_IOM3_CS);
    }
    else if (module_no == 4)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM4_SCK, g_AM_BSP_GPIO_IOM4_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM4_MOSI, g_AM_BSP_GPIO_IOM4_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM4_MISO, g_AM_BSP_GPIO_IOM4_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM4_CS, g_AM_BSP_GPIO_IOM4_CS);
    }
    else if (module_no == 5)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM5_SCK, g_AM_BSP_GPIO_IOM5_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM5_MOSI, g_AM_BSP_GPIO_IOM5_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM5_MISO, g_AM_BSP_GPIO_IOM5_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM5_CS, g_AM_BSP_GPIO_IOM5_CS);
    }
    else if (module_no == 6)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM6_SCK, g_AM_BSP_GPIO_IOM6_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM6_MOSI, g_AM_BSP_GPIO_IOM6_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM6_MISO, g_AM_BSP_GPIO_IOM6_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM6_CS, g_AM_BSP_GPIO_IOM6_CS); // 30 printed as 3
    }
    else if (module_no == 7)
    {
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM7_SCK, g_AM_BSP_GPIO_IOM7_SCK);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM7_MOSI, g_AM_BSP_GPIO_IOM7_MOSI);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM7_MISO, g_AM_BSP_GPIO_IOM7_MISO);
        am_hal_gpio_pinconfig(AM_BSP_GPIO_IOM7_CS, g_AM_BSP_GPIO_IOM7_CS);
    }
    else
    {
        am_util_stdio_printf("Invalid module number\n\r");
        return NULL;
    }

    am_hal_iom_config_t iomConfig = {
        .eInterfaceMode = AM_HAL_IOM_SPI_MODE,
        .ui32ClockFreq = clock_speed,      // 1MHz SPI clock
        .eSpiMode = AM_HAL_IOM_SPI_MODE_0, // SPI mode 0 (CPOL=0, CPHA=0)
        .pNBTxnBuf = NULL,
        .ui32NBTxnBufLength = 0};

    // Initialize IOM
    void *phSPI = NULL;
    if (am_hal_iom_initialize(module_no, &phSPI) != AM_HAL_STATUS_SUCCESS)
    {
        am_util_stdio_printf("Failed to initialize IOM\n\r");
        return NULL;
    }

    am_hal_iom_power_ctrl(phSPI, AM_HAL_SYSCTRL_WAKE, false);

    // Configure IOM for SPI
    if (am_hal_iom_configure(phSPI, &iomConfig) != AM_HAL_STATUS_SUCCESS)
    {
        am_util_stdio_printf("Failed to configure IOM\n\r");
        return NULL;
    }

    // Enable IOM
    am_hal_iom_enable(phSPI);
    am_util_stdio_printf("SPI initialized successfully\n\r");

    return phSPI;
}

/*
 * write a single byte to the SPI bus
 *
 * @param phSPI: the pointer to the SPI bus
 * @param data: the data to write
 * @param continue_transfer: whether to continue the transfer. (CS pin held low if true)
 * @return: the status of the transfer
 */
uint32_t spi_write_byte(void *phSPI, uint8_t data, bool continue_transfer)
{
    uint8_t tx_buffer[4] = {data, 0x00, 0x00, 0x00};
    uint8_t rx_buffer[4] = {0x00, 0x00, 0x00, 0x00};

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = sizeof(tx_buffer);
    xfer.pui32TxBuffer = (uint32_t *)tx_buffer;
    xfer.pui32RxBuffer = (uint32_t *)rx_buffer;
    xfer.bContinue = continue_transfer;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    return am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
}

/*
 * read a single byte from the SPI bus
 *
 * @param phSPI: the pointer to the SPI bus
 * @param data: the buffer to store the read data in
 * @param continue_transfer: whether to continue the transfer. (CS pin held low if true)
 * @return: the status of the transfer
 */
uint32_t spi_read_byte(void *phSPI, uint8_t *data, bool continue_transfer)
{
    uint8_t tx_buffer[4] = {0x00, 0x00, 0x00, 0x00}; // Send dummy byte to read
    uint8_t rx_buffer[4] = {0x00, 0x00, 0x00, 0x00};

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = sizeof(tx_buffer);
    xfer.pui32TxBuffer = (uint32_t *)tx_buffer;
    xfer.pui32RxBuffer = (uint32_t *)rx_buffer;
    xfer.bContinue = continue_transfer;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    uint32_t status = am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
    if (status == AM_HAL_STATUS_SUCCESS)
    {
        *data = rx_buffer[0];
    }
    return status;
}

/*
 * write multiple bytes to the SPI bus
 *
 * @param phSPI: the pointer to the SPI bus
 * @param data: the data to write
 * @param length: the length of the data to write
 * @param continue_transfer: whether to continue the transfer. (CS pin held low if true)
 * @return: the status of the transfer
 */
uint32_t spi_write_bytes(void *phSPI, uint8_t *data, uint32_t length, bool continue_transfer)
{
    uint8_t tx_buffer[(length % 4 == 0) ? length : (length + length % 4)];
    memset(tx_buffer, 0x00, (length % 4 == 0) ? length : (length + length % 4));
    memcpy(tx_buffer, data, length);
    uint8_t rx_buffer[(length % 4 == 0) ? length : (length + length % 4)];

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = length;
    xfer.pui32TxBuffer = (uint32_t *)tx_buffer;
    xfer.pui32RxBuffer = (uint32_t *)rx_buffer;
    xfer.bContinue = continue_transfer;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    uint32_t status = am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
    return status;
}

/*
 * read multiple bytes from the SPI bus
 *
 * @param phSPI: the pointer to the SPI bus
 * @param data: the buffer to store the read data in
 * @param length: the length of the data to read
 * @param continue_transfer: whether to continue the transfer. (CS pin held low if true)
 * @return: the status of the transfer
 */
uint32_t spi_read_bytes(void *phSPI, uint8_t *data, uint32_t length, bool continue_transfer)
{
    uint8_t tx_buffer[(length % 4 == 0) ? length : (length + length % 4)];
    /* 0xFF is required for SD card data phase; other SPI slaves typically accept it for read clocking */
    memset(tx_buffer, 0xFF, (length % 4 == 0) ? length : (length + length % 4));
    uint8_t rx_buffer[(length % 4 == 0) ? length : (length + length % 4)];
    memset(rx_buffer, 0x00, (length % 4 == 0) ? length : (length + length % 4));

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = length;
    xfer.pui32TxBuffer = (uint32_t *)tx_buffer;
    xfer.pui32RxBuffer = (uint32_t *)rx_buffer;
    xfer.bContinue = continue_transfer;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    uint32_t status = am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
    if (status == AM_HAL_STATUS_SUCCESS)
    {
        memcpy(data, rx_buffer, length);
    }
    return status;
}

/*
 * write a command and read a response (common SPI pattern)
 *
 * @param phSPI: the pointer to the SPI bus
 * @param command: the command to write
 * @param response: the buffer to store the read data in
 * @param continue_transfer: whether to continue the transfer. (CS pin held low if true)
 * @return: the status of the transfer
 */
uint32_t spi_write_read(void *phSPI, uint8_t command, uint8_t *response, bool continue_transfer)
{
    uint8_t tx_buffer[4] = {command, 0x00, 0x00, 0x00}; // Command + dummy byte
    uint8_t rx_buffer[4] = {0x00, 0x00, 0x00, 0x00};

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = 1; // only limited to one byte
    xfer.pui32TxBuffer = (uint32_t *)tx_buffer;
    xfer.pui32RxBuffer = (uint32_t *)rx_buffer;
    xfer.bContinue = continue_transfer;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    uint32_t status = am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
    if (status == AM_HAL_STATUS_SUCCESS && response)
    {
        *response = rx_buffer[0];
    }
    return status;
}

/*
 * read a register value (common SPI pattern)
 *
 * @param phSPI: the pointer to the SPI bus
 * @param reg_addr: the address of the register to read
 * @param value: the buffer to store the read data in
 * @param continue_transfer: whether to continue the transfer. (CS pin held low if true)
 * @return: the status of the transfer
 */
uint32_t spi_read_register(void *phSPI, uint8_t reg_addr, uint8_t *value, bool continue_transfer)
{
    uint8_t tx_buffer[4] = {reg_addr & 0x7F, 0x00}; // Read command (MSB=0)
    uint8_t rx_buffer[4] = {0x00, 0x00, 0x00, 0x00};

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = 2;
    xfer.pui32TxBuffer = (uint32_t *)tx_buffer;
    xfer.pui32RxBuffer = (uint32_t *)rx_buffer;
    xfer.bContinue = continue_transfer;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    uint32_t status = am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
    if (status == AM_HAL_STATUS_SUCCESS && value)
    {
        *value = rx_buffer[1]; // Register value is in second byte
    }
    return status;
}

/*
 * write a register value (common SPI pattern)
 *
 * @param phSPI: the pointer to the SPI bus
 * @param reg_addr: the address of the register to write
 * @param value: the value to write
 * @param continue_transfer: whether to continue the transfer. (CS pin held low if true)
 * @return: the status of the transfer
 */
uint32_t spi_write_register(void *phSPI, uint8_t reg_addr, uint8_t value, bool continue_transfer)
{
    uint8_t tx_buffer[4] = {reg_addr | 0x80, value, 0x00, 0x00}; // Write command (MSB=1)
    uint8_t rx_buffer[4] = {0x00, 0x00, 0x00, 0x00};

    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_FULLDUPLEX;
    xfer.ui32NumBytes = 2;
    xfer.pui32TxBuffer = (uint32_t *)tx_buffer;
    xfer.pui32RxBuffer = (uint32_t *)rx_buffer;
    xfer.bContinue = continue_transfer;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    return am_hal_iom_spi_blocking_fullduplex(phSPI, &xfer);
}

/*
 * read bytes to a large buffer in transfers of 4092 bytes at a time
 *
 * @param phSPI: the pointer to the SPI bus
 * @param data: the buffer to store the read data in
 * @param length: the length of the data to read
 * @return: the status of the transfer
 */
uint32_t spi_read_bytes_to_shared_buffer(void *phSPI, uint8_t *data, uint32_t length)
{
    // assume the buffer is correctly aligned
    am_hal_iom_transfer_t xfer;
    xfer.uPeerInfo.ui32SpiChipSelect = spi_cs;
    xfer.ui32InstrLen = 0;
    xfer.ui64Instr = 0;
    xfer.eDirection = AM_HAL_IOM_RX;
    xfer.ui32NumBytes = 4092; // complying with maximum spi transfer size (4095) and staying word-aligned
    xfer.pui32TxBuffer = NULL;
    xfer.pui32RxBuffer = (uint32_t *)data;
    xfer.bContinue = true;
    xfer.ui8RepeatCount = 0;
    xfer.ui32PauseCondition = 0;
    xfer.ui32StatusSetClr = 0;

    uint32_t remaining_bytes = length;
    uint32_t status = AM_HAL_STATUS_SUCCESS;

    while (remaining_bytes > 0)
    {
        status = am_hal_iom_blocking_transfer(phSPI, &xfer);
        if (status != AM_HAL_STATUS_SUCCESS)
        {
            return status;
        }
        remaining_bytes -= xfer.ui32NumBytes;

        // advance buffer by the number of bytes just read
        xfer.pui32RxBuffer = (uint32_t *)((uint8_t *)xfer.pui32RxBuffer + xfer.ui32NumBytes);

        if (remaining_bytes < 4092)
        {
            xfer.ui32NumBytes = remaining_bytes;
        }
        else
        {
            xfer.ui32NumBytes = 4092;
        }
    }

    // terminate the transfer
    status = spi_write_byte(phSPI, 0xFF, false);
    if (status != AM_HAL_STATUS_SUCCESS)
    {
        return status;
    }

    return status;
}

/*
 * reset the SPI bus
 *
 * @param phSPI: the pointer to the SPI bus
 */
void spi_bus_reset(void *phSPI)
{
    am_hal_iom_disable(phSPI);
    am_hal_gpio_state_write(AM_BSP_GPIO_IOM1_CS, AM_HAL_GPIO_OUTPUT_CLEAR);
    am_hal_gpio_state_write(AM_BSP_GPIO_IOM1_SCK, AM_HAL_GPIO_OUTPUT_SET);
    am_hal_gpio_state_write(AM_BSP_GPIO_IOM1_MOSI, AM_HAL_GPIO_OUTPUT_SET);
    am_hal_gpio_state_write(AM_BSP_GPIO_IOM1_MISO, AM_HAL_GPIO_OUTPUT_SET);
    am_hal_iom_enable(phSPI);
}
