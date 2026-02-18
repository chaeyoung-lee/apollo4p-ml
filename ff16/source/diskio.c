/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */

#include "sd_spi.h"
#include "spi.h"
#include "uart.h"
#include "am_hal_rtc.h"

void *phSPI_ = NULL;

/* Example: Mapping of physical drive number for each drive */
#define DEV_FLASH	0	/* Map FTL to physical drive 0 */
#define DEV_MMC		1	/* Map MMC/SD card to physical drive 1 */
#define DEV_USB		2	/* Map USB MSD to physical drive 2 */


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat = 0;
	// int result;

	if (phSPI_ == NULL) { stat |= STA_NOINIT; }
	if (!sd_spi_card_detect()) { stat |= STA_NODISK; }
	
	return stat;
	/*switch (pdrv) {
	case DEV_RAM :
		result = RAM_disk_status();

		// translate the reslut code here

		return stat;

	case DEV_MMC :
		result = MMC_disk_status();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_status();

		// translate the reslut code here

		return stat;
	}*/
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat = 0;
	// int result;

	void *phSPI = sd_spi_init(6, AM_HAL_IOM_16MHZ);
	if (phSPI == NULL) {
		stat |= STA_NOINIT;
		return stat;
	}

	phSPI_ = phSPI;

	return stat;

	/*switch (pdrv) {
	case DEV_RAM :
		result = RAM_disk_initialize();

		// translate the reslut code here

		return stat;

	case DEV_MMC :
		result = MMC_disk_initialize();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_initialize();

		// translate the reslut code here

		return stat;
	}
	return STA_NOINIT;*/
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res = RES_OK;
	// int result;

	uint32_t block_num = (uint32_t)sector;
	uint32_t status;
	if (count == 1) {
		status = sd_spi_read_single_block(phSPI_, block_num, buff, 512);
	} else {
		status = sd_spi_read_multi_block(phSPI_, block_num, count, buff, count * 512);
	}
	if (status != AM_HAL_STATUS_SUCCESS) {
		res = RES_ERROR;
	}

	return res;

	/*switch (pdrv) {
	case DEV_RAM :
		// translate the arguments here

		result = RAM_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_MMC :
		// translate the arguments here

		result = MMC_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_USB :
		// translate the arguments here

		result = USB_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;
	}
		
	return RES_PARERR;*/
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res = RES_OK;
	
	uint32_t block_num = (uint32_t)sector;
	uint32_t status;
	if (count == 1) {
		status = sd_spi_write_single_block(phSPI_, block_num, buff, 512);
	} else {
		status = sd_spi_write_multi_block(phSPI_, block_num, count, buff, count * 512);
	}
	if (status != AM_HAL_STATUS_SUCCESS) {
		res = RES_ERROR;
	}
	
	return res;

	/*switch (pdrv) {
	case DEV_RAM :
		// translate the arguments here

		result = RAM_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_MMC :
		// translate the arguments here

		result = MMC_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_USB :
		// translate the arguments here

		result = USB_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;
	}

	return RES_PARERR;*/
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
// this function is not used in this project.
DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res;
	BYTE csd[16];
	DWORD st, ed, csize;
	LBA_t *dp;

	sd_spi_cmd_t spi_cmd;
	uint8_t response[1];
	uint32_t status;
	// does not care about pdrv
	switch (cmd) {
		case CTRL_SYNC:
			if (sd_spi_check_busy_status(phSPI_)) { res = RES_OK; }
			break;
		case GET_SECTOR_COUNT:
			spi_cmd.cmd = CMD9;
			spi_cmd.arg = 0x00;
			spi_cmd.crc = 0x00;
			status = sd_spi_write_command(phSPI_, &spi_cmd, response, 1, true);
			if (status != AM_HAL_STATUS_SUCCESS || response[0] != R1_SUCCESS) { return RES_ERROR; }
			// get CSD
			status = spi_read_bytes(phSPI_, csd, 16, false);
			while (sd_spi_check_busy_status(phSPI_)); // purge remaining data
			if (status != AM_HAL_STATUS_SUCCESS) { return RES_ERROR; }
			/* SDC CSD ver 2 */
			csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
			*(LBA_t*)buff = csize << 10;
			res = RES_OK;
			break;
		case GET_BLOCK_SIZE:
			// send ACMD13
			spi_cmd.cmd = CMD55;
			spi_cmd.arg = 0x00;
			spi_cmd.crc = 0x00;
			status = sd_spi_write_command(phSPI_, &spi_cmd, response, 1, true);
			spi_cmd.cmd = CMD13;
			status = sd_spi_write_command(phSPI_, &spi_cmd, response, 1, true);
			if (status != AM_HAL_STATUS_SUCCESS || response[0] != R1_SUCCESS) { return RES_ERROR; }
			// get CSD
			status = spi_read_bytes(phSPI_, csd, 16, false);
			while (sd_spi_check_busy_status(phSPI_)); // purge remaining data
			if (status != AM_HAL_STATUS_SUCCESS) { return RES_ERROR; }
			*(DWORD*)buff = 16UL << (csd[10] >> 4);
			res = RES_OK;
			break;
		// no implementation of GET_SECTOR_SIZE since it is fixed
		case CTRL_TRIM:
			spi_cmd.cmd = CMD9;
			spi_cmd.arg = 0x00;
			spi_cmd.crc = 0x00;
			status = sd_spi_write_command(phSPI_, &spi_cmd, response, 1, true);
			if (status != AM_HAL_STATUS_SUCCESS || response[0] != R1_SUCCESS) { return RES_ERROR; }
			// get CSD
			status = spi_read_bytes(phSPI_, csd, 16, false);
			while (sd_spi_check_busy_status(phSPI_)); // purge remaining data
			if (status != AM_HAL_STATUS_SUCCESS) { return RES_ERROR; }
			if (!(csd[10] & 0x40)) break;					/* Check if ERASE_BLK_EN = 1 */
			dp = buff; st = (DWORD)dp[0]; ed = (DWORD)dp[1];	/* Load sector block */
			break;
		
		default:
			res = RES_OK;
			break;
	}
	return res;
}

DWORD get_fattime(void) {
	am_hal_rtc_time_t rtc_time;
	am_hal_rtc_time_get(&rtc_time);
	return (DWORD) (rtc_time.ui32Year << 25) | 
					(rtc_time.ui32Month << 21) | 
					(rtc_time.ui32DayOfMonth << 16) | 
					(rtc_time.ui32Hour << 11) | 
					(rtc_time.ui32Minute << 5) | 
					(rtc_time.ui32Second >> 1);
}

