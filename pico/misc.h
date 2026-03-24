#ifndef _MISC_H
#define _MISC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

//Minimum of 3 values
#define MIN3(a,b,c) MIN(MIN(a,b),c)

//Make sure l<=x<=u
#define CLAMP(x,l,u) MIN(u,MAX(l,x))

int stricmp(const char* s1, const char* s2);
char *strtrim(char *str);

void StartTimer();
uint32_t EndTimer();

bool IsAppleConnected();
bool CheckPicoW();
void InitPicoLed();
void TurnOnPicoLed();
void TurnOffPicoLed();

void DumpBuffer(const uint8_t *buffer,uint len);
uint32_t GetTotalHeap(void);
uint32_t GetFreeHeap(void);
void SystemReset();
void measure_freqs();
void OverclockCPU();

#define VOLNAMELENMAX 15
typedef struct  {
  uint32_t blockCount;
  char volName[VOLNAMELENMAX+1];
  uint8_t volNameLen;
  uint8_t type; //0=ProDOS, 1=empty, 2=Unknown as defined in VolumeType enum
} VolumeInfo;

bool GetVolumeInfo(const uint unitNum, VolumeInfo *infoOut);
void GetDeviceInfoString(char* dest);

#ifdef __cplusplus
}
#endif

#endif

