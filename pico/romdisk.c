#include "pico/stdlib.h"
#include <string.h>
#include "defines.h"
#include "mediaaccess.h"

//Defined in romdisk.s
extern const uint8_t  romdiskImage[];
extern const uint32_t romdiskImageLen[];

//ID String padded to 16 bytes long
#define IDSTR "ROMDISK         "
#define IDSTRLEN 7

//ROMDisk Enable Flag
static bool romdiskEnabled = false;


///////////////////////////////////////
// Getter /Setter function
//
bool GetRomdiskEnabled() {
  return romdiskEnabled;
}

void EnableRomdisk() {
  romdiskEnabled = true;
}

void DisableRomdisk() {
  romdiskEnabled = false;
}

uint32_t GetUnitCountRomdisk() {
  return romdiskEnabled?1:0;
}

uint32_t GetBlockCountRomdisk() {
  return romdiskImageLen[0]/512;
}

uint32_t GetBlockCountRomdiskActual() {
  return romdiskImageLen[0]/512;
}

////////////////////////////////////////////////////////////////////
// Read Block from ROM Disk
// Assume blockNum is valid.
//
// Input: Block Number
//        destBuffer   Destination Buffer (512 Bytes)
//
// Output: SP_NOERR, SP_IOERR
//
rwerror_t ReadBlockRomdisk(const uint blockNum, uint8_t* destBuffer){
  //Validate Block Number
  if (blockNum >= GetBlockCountRomdisk()) return SP_IOERR;
  
  //No need to use DMA since ROMDisk is for disaster recovery only
  memcpy(destBuffer,romdiskImage+blockNum*BLOCKSIZE,BLOCKSIZE);
  
  return SP_NOERR;
}


/////////////////////////////////////////////////////////////
// Get DIB (Device Information Block) of a unit
//
// Input: destBuffer - Pointer to destination buffer
//        
void GetDIBRomdisk(uint8_t *destBuffer) {
  assert(sizeof(struct dib_t)==25);
  struct dib_t *dib = (struct dib_t*)destBuffer;  
  
  //Device Status Byte  
  dib->devicestatus = 0b10110100;          
  
  //Block Count
  uint32_t blockSize = GetBlockCountRomdisk();  
  dib->blocksize_l  = (uint8_t)blockSize; blockSize>>=8; 
  dib->blocksize_m  = (uint8_t)blockSize; blockSize>>=8;                    
  dib->blocksize_h  = (uint8_t)blockSize;
    
  //ID String    
  assert(strlen(IDSTR)==16);              
  assert(IDSTRLEN<=16);
  dib->idstrlen = IDSTRLEN;
  memcpy(dib->idstr,IDSTR,16);
  
  //Device Type, subtype and Firmware Version
  dib->devicetype = 0x04;                  //Device Type. $04 = ROM Disk
  dib->subtype = 0x20;                     //Subtype. $20= not removable, no extended call
  dib->fmversion_l = (uint8_t)FIRMWAREVER; //Firmware Version Word
  dib->fmversion_h = (uint8_t)(FIRMWAREVER>>8);
}
