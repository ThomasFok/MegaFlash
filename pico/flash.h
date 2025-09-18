#ifndef _FLASH_H
#define _FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

void InitSpi();
void InitFlash();

//Flash Chip ID routines
uint32_t tsReadJEDECID(const uint deviceNum);
uint64_t tsReadUniqueID(const uint deviceNum);
uint64_t tsReadUniqueIDDevice0();

//Erase entire flash chips
void tsEraseEverything();

//UserConfig Block Access routines
bool ReadUserConfigBlock(uint8_t* dest);
bool WriteUserConfigBlock(const uint8_t *src);

//Flash Size/Block count
uint32_t GetFlashSize();
uint32_t GetUnitCountFlashActual();
uint32_t GetBlockCountFlash(const uint unitNum);
uint32_t GetBlockCountFlashActual(const uint unitNum);
void GetDIBFlash(const uint unitNum, uint8_t *destBuffer);

//
// All ProDOS blocks must be handled by routines with _Public suffix.
// Read Bit Inversion note in flash.c file
//
rwerror_t tsReadBlockFlash_Public(const uint unitNum, const uint blockNum, uint8_t* destBuffer);
rwerror_t tsWriteBlockFlash_Public(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer);
bool tsWriteBlockFlashForImageTransfer_Public(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer);

//
// Erase Flash Disk
//
void tsEraseFlashDisk(const uint unitNum);
void AbortEraseFlashDisk();

#ifdef __cplusplus
}
#endif

#endif