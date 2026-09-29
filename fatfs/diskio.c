/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#include "supercard_driver.h"

#ifdef EMU_HARNESS
// Emulator test harness (tools/emu): sector I/O through a magic register block
// that the host-side harness intercepts. Never used on real hardware builds.
#define EMU_DISK_REGS ((volatile uint32_t *)0x09F00000)
static unsigned emu_disk_io(unsigned op, const void *buf, LBA_t sector, UINT count) {
  EMU_DISK_REGS[1] = (uintptr_t)buf;
  EMU_DISK_REGS[2] = sector;
  EMU_DISK_REGS[3] = count;
  EMU_DISK_REGS[0] = op;   // Triggers the transfer
  return 0;
}
#define sdcard_read_blocks(b, s, c)  emu_disk_io(1, b, s, c)
#define sdcard_write_blocks(b, s, c) emu_disk_io(2, b, s, c)
#endif

DSTATUS disk_status (BYTE pdrv) {
  return 0;
}

DSTATUS disk_initialize (BYTE pdrv) {
  return 0;
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
  unsigned err = sdcard_read_blocks(buff, sector, count);
  return err ? RES_ERROR : RES_OK;
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
  unsigned err = sdcard_write_blocks(buff, sector, count);
  return err ? RES_ERROR : RES_OK;
}

#endif

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
  switch (cmd) {
  case CTRL_SYNC:
  case CTRL_TRIM:
  default:
    return 0;
  case GET_SECTOR_SIZE:
    *(WORD*)buff = 512;
    return 0;
  case GET_SECTOR_COUNT:
    // This is only used to create a new filesystem really!
    return 0;
  };
}

