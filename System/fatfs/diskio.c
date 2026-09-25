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

/* Example: Declarations of the platform and disk functions in the project */
#include "SD_Card.h"

/* Example: Mapping of physical drive number for each drive */
#define SPI_FLASH	0	/* Map FTL to physical drive 0 */
#define SD_CARD		1	/* Map MMC/SD card to physical drive 1 */


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

	switch (pdrv)
	{
		case SPI_FLASH :
			result = STA_NOINIT;

			// translate the reslut code here

			return stat;

		case SD_CARD :
			result = SD_GetStatus();

			// translate the reslut code here
			if(result == 0)
			{
				stat = RES_OK;
			}else
			{
				stat = RES_ERROR;
			}
			return stat;
	}
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

	switch (pdrv)
	{
		case SPI_FLASH :
			result = STA_NOINIT;

			// translate the reslut code here

			return stat;

		case SD_CARD :
			result = SD_Init();

			// translate the reslut code here
			if(result == 0)
			{
				stat = RES_OK;
			}else
			{
				stat = RES_ERROR;
			}
			return stat;
	}
	return STA_NOINIT;
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
	DRESULT res;
	int result;

	switch (pdrv)
	{
		case SPI_FLASH :
			// translate the arguments here

			result = RES_PARERR;

			// translate the reslut code here

			return res;

		case SD_CARD :
			// translate the arguments here

			result = SD_ReadMultiBlocks(buff, sector*512, 512, count);
			result = SD_WaitReadOperation();
			/* 原来是无超时�?while(SD_GetStatus() != SD_TRANSFER_OK);
			 * SD 卡一旦不应答就会【永久】卡在这里，整个读卡任务死掉、连错误都报不出来�?
			 * 正常情况下一两次就通过，这里只是给个上限兜底�?*/
			{
				uint32_t wait = 2000;
				while(SD_GetStatus() != SD_TRANSFER_OK && wait > 0)
				{
					wait--;
				}
				if(wait == 0)
				{
					result = SD_ERROR;      /* 超时 �?当读出错误，交给上层去重�?*/
				}
			}

			// translate the reslut code here
			if(result == 0)
			{
				res = RES_OK;
			}else
			{
				res = RES_ERROR;
			}
			return res;
	}

	return RES_PARERR;
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
	DRESULT res;
	int result;

	switch (pdrv)
	{
		case SPI_FLASH :
			// translate the arguments here

			result = RES_PARERR;

			// translate the reslut code here

			return res;

		case SD_CARD :
			// translate the arguments here

			result = SD_WriteMultiBlocks((uint8_t*)buff, sector*512, 512, count);
			result = SD_WaitWriteOperation();
			while(SD_GetStatus() != SD_TRANSFER_OK);

			// translate the reslut code here
			if(result == 0)
			{
				res = RES_OK;
			}else
			{
				res = RES_ERROR;
			}
			return res;
	}

	return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res = RES_PARERR;

	switch (pdrv)
	{
		case SPI_FLASH :

			// Process of the command for the RAM drive

			return res;

		case SD_CARD :

			// Process of the command for the MMC/SD card

			return res;
	}

	return RES_PARERR;
}

