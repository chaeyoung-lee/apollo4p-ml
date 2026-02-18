#ifdef __cplusplus
extern "C" {
#endif

#ifndef SD_SPI_CMD_H
#define SD_SPI_CMD_H

#define TX_CMD_SIZE 6 // 6 bytes for CMD + CRC

// commands for communication with the SD card
#define CMD0 0x00
#define CMD6  0x06
#define CMD8  0x08
#define CMD9  0x09
#define CMD10 0x0A
#define CMD12 0x0C
#define CMD13 0x0D
#define CMD16 0x10
#define CMD17 0x11
#define CMD18 0x12
#define CMD23 0x17
#define CMD24 0x18
#define CMD25 0x19
#define CMD27 0x1B
#define CMD28 0x1C
#define CMD29 0x1D
#define CMD30 0x1E
#define CMD32 0x20
#define CMD33 0x21
#define CMD38 0x26
#define CMD41 0x29
#define CMD42 0x2A
#define CMD55 0x37
#define CMD56 0x38
#define CMD58 0x3A
#define CMD59 0x3B

// R1 response codes. Any of these can be ored together.
#define R1_SUCCESS 0x00
#define R1_IDLE 0x01
#define R1_ERASE_PARAM 0x02
#define R1_ILLEGAL_COMMAND 0x04
#define R1_COMMUNICATION_CRC_ERROR 0x08
#define R1_ERASE_SEQUENCE_ERROR 0x10
#define R1_ADDRESS_ERROR 0x20
#define R1_PARAMETER_ERROR 0x40
#define R1_ILLEGAL_VALUE 0xFF

// data token for different commands
#define DATA_TOKEN_CMD17 0xFE
#define DATA_TOKEN_CMD18 0xFE
#define DATA_TOKEN_CMD24 0xFE
#define DATA_TOKEN_CMD25 0xFC
#define STOP_TRAN_TOKEN_CMD25 0xFD

// data response codes masks
#define DATA_RESP_ACCEPTED_MASK 0x05
#define DATA_CRC_ERROR_MASK 0x0B
#define DATA_WRITE_ERROR_MASK 0x0D

// Miscellaneous constants
#define TX_CMD_SIZE 6 // 6 bytes for CMD + CRC
#define N_CR 2 // command response time (NCR), 0 to 8 bytes for SDC
#define BYTES_BTWN_CMD_RESP_AND_DATA 100 // arbitrary value
#define BYTES_BTWN_DATA_BLOCKS 8 // arbitrary value
#define BLOCK_SIZE 512
#define CRC_SIZE 2
#define VALID_DATA_TOKEN 0xFE
#define INVALID_DATA_BYTE 0xFF
#define LINE_NOT_BUSY 0xFF

#endif // SD_SPI_CMD_H

#ifdef __cplusplus
}
#endif