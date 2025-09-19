#ifndef _RAMDISK_H
#define _RAMDISK_H

#ifdef __cplusplus
extern "C" {
#endif

bool GetRamdiskEnabled();
void EnableRamdisk();
void DisableRamdisk();
uint8_t* GetRamdiskDataPointer();
size_t GetRamdiskSize(); //RAM Disk Size in bytes
void EraseRamdiskQuick();
void EraseRamdisk();
void FormatRamdiskOnce();

uint32_t GetUnitCountRamdisk();
uint32_t GetBlockCountRamdisk();
uint32_t GetBlockCountRamdiskActual();
void GetDIBRamdisk(uint8_t *destBuffer);
rwerror_t ReadBlockRamdisk(const uint blockNum, uint8_t* destBuffer);
rwerror_t WriteBlockRamdisk(const uint blockNum, const uint8_t* srcBuffer);

#ifdef __cplusplus
}
#endif

#endif