#include "mediaaccess.h"
#include "debug.h"
#include "flash.h"
#include "flashunitmapper.h"
#include "romdisk.h"
#include "ramdisk.h"
#include "misc.h"

//******************************************************************
//
//      Media Access Routines
//
// Storage media are accessed through the routines in this module.
// For example, when ReadBlock() is called, it dispatches the call
// to ReadBlockFlash or ReadBlockRomdisk according to the unit number
//******************************************************************



/////////////////////////////////////////////////////////////
// Get total number of units (ProDOS Drives)
//
// Output: uint - Number of units
//
uint GetTotalUnitCount() {
  uint unitCount;
  
  unitCount = GetUnitCountRomdisk() +
              GetUnitCountFlashEnabled()   +
              GetUnitCountRamdisk();
  
  return unitCount;
}


/////////////////////////////////////////////////////////////
// Check if a unit number is valid
//
// Input: unitNum - Unit Number
//
bool __no_inline_not_in_flash_func(IsValidUnitNum)(const uint unitNum) {
  return unitNum!=0 && unitNum<=GetTotalUnitCount();
}



//////////////////////////////////////////////////////////////////////////////
// Translate Smartport unit number to medium unit number 
//
// Input: unitNum          - Smartport unit number
//        typeOut          - Pointer to variable to receive Medium Type of the unit
//        mediumUnitNumOut - Pointer to variable to receive Medium Unit Number
//
//Currently, there are 3 different storage medium, Romdisk, Flash and Ramdisk.
//The order of their unit numbers are
// 1) Romdisk
// 2) Flash
// 3) Ramdisk
//
//When Romdisk is disabled/enabled, the unit numbers of Flash Disk and Ramdisk 
//also change.
//
//For example, when Romdisk is disabled, the first drive in Flash is unit 1. But
//when Romdisk is enabled, the same drive becomes unit 2.
//
//This routine translate the logical unit number to the unit number of the medium.
//It also reports the medium type so that ReadBlock/WriteBlock routines can dispatch
//the call to specific handler.
//
//For example, if Romdisk is enabled and the Smartport unit number = 2, it will report
//medium type is Flash and medium unit number is 1 (since it is the first drive of Flash)
//
static void __no_inline_not_in_flash_func(TranslateUnitNum)(uint unitNum, MediaType *typeOut, uint *mediumUnitNumOut) {
  assert(IsValidUnitNum(unitNum));
  assert(typeOut!=NULL);
  assert(mediumUnitNumOut!=NULL);
  
  uint romdiskCount = GetUnitCountRomdisk();
  if (unitNum <= romdiskCount) {
    *typeOut = TYPE_ROMDISK;
    *mediumUnitNumOut = unitNum;
    return;
  }
  
  unitNum -= romdiskCount;
  uint flashdiskCount = GetUnitCountFlashEnabled();
  if (unitNum <= flashdiskCount) {
    *typeOut = TYPE_FLASH;
    *mediumUnitNumOut = MapFlashUnitNum(unitNum);   //unitNum;
    return;
  }  
  
  unitNum -= flashdiskCount;
  uint ramdiskCount = GetUnitCountRamdisk();
  if (unitNum <= ramdiskCount) {
    *typeOut = TYPE_RAMDISK;
    *mediumUnitNumOut = unitNum;
    return;
  }
  
  //Should not happen
  assert(false);
}

//////////////////////////////////////////////////////////////////////////////
// Get the unit number of RAM Disk
//
uint GetRamdiskUnitNum() {
  return GetUnitCountRomdisk() +GetUnitCountFlashEnabled() +1;
}


/////////////////////////////////////////////////////////////
// Check if a unit is writable
// Assume unitNum is valid
//
// Input: unitNum - Unit Number
//
bool __no_inline_not_in_flash_func(IsUnitWritable)(const uint unitNum) {
  assert(IsValidUnitNum(unitNum));
  
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);
  
  return (type!=TYPE_ROMDISK);
}


/////////////////////////////////////////////////////////////
// Get number of blocks of a unit reported to ProDOS
// Assume unitNum is valid.
//
// Input: unitNum - Unit Number (1-N)
//
// Output: uint32 - Block Count
//
// Note: The capacity of Flash unit is actually 65536 (0x10000). But
// ProDOS 8 status call limits the capacity to 65535 (0xFFFF). SmartPort
// status call support capacity >0xFFFF.
// To avoid potential problem, the size of flash unit reported to ProDOS is
// 65535. Use GetBlockCountActual() to get the actual capacity of a unit.
//
uint32_t __no_inline_not_in_flash_func(GetBlockCount)(const uint unitNum) {
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);
  
  switch(type) {
    case TYPE_ROMDISK:
      return GetBlockCountRomdisk();
    case TYPE_FLASH:
      return GetBlockCountFlash(mediumUnitNum);
    case TYPE_RAMDISK:
      return GetBlockCountRamdisk();
  }
  
  assert(false);  //Should not happen
  return 0;
}

/////////////////////////////////////////////////////////////
// Get actual number of blocks of a unit
// Assume unitNum is valid.
//
// Input: unitNum - Unit Number (1-N)
//
// Output: uint32 - Block Count
//
uint32_t GetBlockCountActual(const uint unitNum) {
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);
  
  switch(type) {
    case TYPE_ROMDISK:
      return GetBlockCountRomdiskActual();
    case TYPE_FLASH:
      return GetBlockCountFlashActual(mediumUnitNum);
    case TYPE_RAMDISK:
      return GetBlockCountRamdiskActual();
  }
  
  assert(false);  //Should not happen
  return 0;  
  
}


/////////////////////////////////////////////////////////////
// Get DIB (Device Information Block) of a unit
// Assume unitNum is valid.
//
// Input: unitNum    - Unit Number (1-N)
//        destBuffer - Pointer to destination buffer
//
void GetDIB(const uint unitNum,uint8_t *destBuffer) {
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);
  
  switch(type) {
    case TYPE_ROMDISK:
      GetDIBRomdisk(destBuffer);
      return;
    case TYPE_FLASH:
      GetDIBFlash(mediumUnitNum,destBuffer);
      return;
    case TYPE_RAMDISK:
      GetDIBRamdisk(destBuffer);
      return;
  }
  
  assert(false);  //Should not happen
}

/////////////////////////////////////////////////////////////
// Get Media Type of a unit
// Assume unitNum is valid.
//
// Input: unitNum    - Unit Number (1-N)
//
int GetMediumType(const uint unitNum) {
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);
  
  return type;
}



/////////////////////////////////////////////////////////////
// ReadBlock - Dispatch Read Operation based on the media
// On return, ProDOS/Smartport Error code is written to
// address pointed by spErrorOut and MegaFlash error code
// is returned.
//
// Input: unitNum    - Unit Number (1-N)
//        blockNum   - Block Number
//        destBuffer - Destination Buffer
//        spErrorOut - Pointer to store ProDOS/Smartport Error Code
//
// Output: uint - MegaFlash error code
//
uint __no_inline_not_in_flash_func(ReadBlock)(const uint unitNum, const uint blockNum, uint8_t* destBuffer,uint8_t* spErrorOut) {
  rwerror_t spResult = SP_NOERR;
  uint retValue = MFERR_NONE;
  
  //Validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    retValue=MFERR_INVALIDUNIT;
    spResult = SP_IOERR;   
    goto exit;
  } 
 
  //Validate blockNum
  if (blockNum >= GetBlockCount(unitNum)) {
    retValue=MFERR_INVALIDBLK;
    spResult = SP_IOERR;
    goto exit;
  } 
  
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);
  
  switch (type) {
    case TYPE_ROMDISK:
      spResult = ReadBlockRomdisk(blockNum, destBuffer);
      if (spResult != SP_NOERR) retValue=MFERR_RWERROR;  
      goto exit;
      break;
    case TYPE_FLASH:
      spResult = ReadBlockFlash(mediumUnitNum, blockNum,destBuffer);
      if (spResult != SP_NOERR) retValue=MFERR_RWERROR;  
      goto exit;
      break;
    case TYPE_RAMDISK:
      spResult = ReadBlockRamdisk(blockNum, destBuffer);
      if (spResult != SP_NOERR) retValue=MFERR_RWERROR;  
      goto exit;
      break;    
    default:
      assert(false);  //should not happen
  }
  
exit:
  if (spErrorOut) *spErrorOut = spResult;

  return retValue;  
}

/////////////////////////////////////////////////////////////
// WriteBlock - Dispatch Write Operation based on the media
// On return, ProDOS/Smartport Error code is written to
// address pointed by spErrorOut and MegaFlash error code
// is returned.
//
// Input: unitNum    - Unit Number (1-N)
//        blockNum   - Block Number
//        srcBuffer  - Destination Buffer
//        spErrorOut - Pointer to store ProDOS/Smartport Error Code
//
// Output: uint - MegaFlash error code
//
uint __no_inline_not_in_flash_func(WriteBlock)(const uint unitNum, const uint blockNum, uint8_t* srcBuffer,uint8_t* spErrorOut) {
  rwerror_t spResult = SP_NOERR;
  uint retValue = MFERR_NONE;
  
  //Validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    retValue=MFERR_INVALIDUNIT;
    spResult = SP_IOERR;   
    goto exit;
  } 
 
  //Validate blockNum
  if (blockNum >= GetBlockCount(unitNum)) {
    retValue=MFERR_INVALIDBLK;
    spResult = SP_IOERR;
    goto exit;
  }   
  
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);

  switch(type) {
    case TYPE_ROMDISK:
      spResult = SP_NOWRITEERR;   //ROMDisk is read-only
      retValue = MFERR_RWERROR;
      goto exit;
    case TYPE_FLASH:
      spResult = WriteBlockFlash(mediumUnitNum, blockNum, srcBuffer);
      if (spResult != SP_NOERR) retValue=MFERR_RWERROR;  
      goto exit;
    case TYPE_RAMDISK:
      spResult = WriteBlockRamdisk(blockNum, srcBuffer);
      if (spResult != SP_NOERR) retValue=MFERR_RWERROR;  
      goto exit;      
    default:
      assert(false); //should not happen
  }
  
  
exit:
  if (spErrorOut) *spErrorOut = spResult;
  
  return retValue;
}


/////////////////////////////////////////////////////////////
// WriteBlock routine for transferring disk image to 
// Flash or RAMDisk
//
// For writing to Flash
//
// The entire drive is erased and there is no need to preserve
// existing data. 
// It erases a 64kB sector every 16 blocks and program the
// block to flash directly without Read-Modify-Write sequence.
// Note: One 64kB-sector = 16 4kB-Sector. 

// Input: unitNum    - Unit Number (1-N)
//        blockNum   - Block Number
//        srcBuffer  - Source Buffer
//
// Output: bool - success
//
bool WriteBlockForImageTransfer(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer) {
  bool success = false;
  
  //Validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    success = false;
    goto exit;
  } 

  //Validate blockNum
  if (blockNum >= GetBlockCountActual(unitNum)) {
    success = false;
    goto exit;
  }   

  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);

  if (type==TYPE_FLASH) {
    //
    //Writer to Flash
    //
    success = WriteBlockFlashForImageTransfer(mediumUnitNum, blockNum, srcBuffer);
  }
  else if (type==TYPE_RAMDISK) {
    //
    //Write to RAMDisk
    //
    rwerror_t spResult = WriteBlockRamdisk(blockNum, srcBuffer);
    success = (spResult == SP_NOERR);
  }
  else {
    success = false;
  }
  
exit:  
  return success;
}

/////////////////////////////////////////////////////////////
// Get Block Count for Image Transfer
// Assume unitNum is valid.
//
// Input: unitNum    - Unit Number (1-N)
//
// If the file system of the unit is ProDOS, returns 
// the formatted size of the unit.
//
// Otherwise, report the actual capacity of the unit so that
// entire unit can be transferred to host.
//
// Note: GetBlockCountActual() return 65536 (0x10000) for Flash Drives
//
uint32_t GetBlockCountForImageTransfer(const uint32_t unitNum) {
  VolumeInfo info;
  GetVolumeInfo(unitNum,&info);
  uint32_t blockCount = 0;
  
  if (info.type==TYPE_PRODOS) {
    blockCount = info.blockCount;
  } else {
    blockCount=GetBlockCountActual(unitNum);
  }

 return blockCount;
}


/////////////////////////////////////////////////////////////
// Erase entire unit
//
// Input: unitNum    - Unit Number (1-N)
//
// Output: bool - success
//
// Note: It takes 2 minutes to erase a FlashDisk
//
bool EraseEntireUnit(const uint unitNum) {
  bool success = true;  //Assume success
  
  //Validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    success = false;
    goto exit;
  } 
  
  uint mediumUnitNum;
  MediaType type;
  TranslateUnitNum(unitNum,&type,&mediumUnitNum);  
  
  switch(type) {
    case TYPE_ROMDISK:
      success = false;
      goto exit;
    case TYPE_FLASH:
      EraseFlashDisk(mediumUnitNum);
      success = true;
      goto exit;
    case TYPE_RAMDISK:
      EraseRamdisk();
      success = true;
      goto exit;      
    default:
      assert(false); //should not happen
  }  
  
exit:
    return success;
}
