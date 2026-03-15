#ifndef _MEDIAACCESS_H
#define _MEDIAACCESS_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

uint GetTotalUnitCount();
bool IsValidUnitNum(const uint unitNum);
bool IsUnitWritable(const uint unitNum);
uint32_t GetBlockCount(const uint unitNum);
uint32_t GetBlockCountActual(const uint unitNum);
void GetDIB(const uint unitNum,uint8_t *destBuffer);
int GetMediumType(const uint unitNum);
uint ReadBlock(const uint unitNum, const uint blockNum, uint8_t* destBuffer,uint8_t* spErrorOut);
uint WriteBlock(const uint unitNum, const uint blockNum, uint8_t* srcBuffer,uint8_t* spErrorOut);
bool WriteBlockForImageTransfer(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer);
uint32_t GetBlockCountForImageTransfer(const uint32_t unitNum);
uint GetRamdiskUnitNum();
bool EraseEntireUnit(const uint unitNum);


//Device Status Byte:
//Bit 7: 1=block device, 0=char Device
//Bit 6: 1=write allowed
//Bit 5: 1=read allowed
//Bit 4: 1=Device Online or Disk in Drive
//Bit 3: 1=Format allowed
//Bit 2: 1=media write-protected (block device only)
//Bit 1: Reserved, must = 0
//Bit 0: 1=device currently open (char device only)
struct __attribute__((packed)) dib_t {
  uint8_t  devicestatus;  //Device Status Byte
  uint8_t  blocksize_l;   //Block Size Low Byte
  uint8_t  blocksize_m;   //Block Size Mid Byte
  uint8_t  blocksize_h;   //Block Size High Byte
  uint8_t  idstrlen;      //ID String Length (Max:$10)
  char     idstr[16];     //ID String Padded to 16 bytes
  uint8_t  devicetype;    //Device Type $02= harddisk, $00=RAM Disk, $04=ROM Disk
  uint8_t  subtype;       //Device Subtype. $20= not removable, no extended call
  uint8_t  fmversion_l;   //Firmware Version Word Low Byte
  uint8_t  fmversion_h;   //Firmware Version Word High Byte
  //Declare Firmware Version as two uint8_t instead of uint16_t to
  //avoid potential problem due to unaligned address
};

#ifdef __cplusplus
}
#endif

#endif