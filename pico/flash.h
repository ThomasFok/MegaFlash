#ifndef _FLASH_H
#define _FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

void InitSpi();
void InitFlash();

//Flash Chip ID routines
uint32_t ReadJEDECID(const uint device_num);
uint64_t ReadUniqueIDDevice0();

//Erase entire flash chips
void EraseEverything();

//UserConfig Block Access routines
bool ReadUserConfigBlock(uint8_t* dest);
bool WriteUserConfigBlock(const uint8_t *src);

//Flash Size/Block count
uint32_t GetFlashSize();
uint32_t GetUnitCountFlashActual();
uint32_t GetBlockCountFlash(const uint unit_num);
uint32_t GetBlockCountFlashActual(const uint unit_num);
void GetDIBFlash(const uint unit_num, uint8_t *dest_buffer);

//
// All ProDOS blocks must be handled by routines with _Public suffix.
// Read Bit Inversion note in flash.c file
//
rwerror_t ReadBlockFlash(const uint unit_num, const uint block_num, uint8_t* dest_buffer);
rwerror_t WriteBlockFlash(const uint unit_num, const uint block_num, const uint8_t* src_buffer);
bool WriteBlockFlashForImageTransfer(const uint unit_num, const uint block_num, const uint8_t* src_buffer);

//
// Erase Flash Disk
//
void EraseFlashDisk(const uint uint_num);
void AbortEraseFlashDisk();

#ifdef __cplusplus
}
#endif

#endif