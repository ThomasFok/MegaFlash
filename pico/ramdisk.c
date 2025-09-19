#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include <string.h>
#include "defines.h"
//#include "dmamemops.h"
#include "mediaaccess.h"
#include "formatter.h"
#include "ramdisk.h"

/******************************************************
After power on, the MegaFlash is in Slinky Emulation mode.
The content of RAMDisk is random. After switching to
MegaFlash native mode, Apple sends CMD_COLDSTART command.
Then, EnableRamdisk() is called if RAMDisk is enabled.
The RAMDisk is formatted. FormatRamdiskOnce() function
ensure the RAMDisk is formatted once only.
*******************************************************/

//Smartport DIB ID String padded to 16 bytes long
#define IDSTR "RAMDISK         "
#define IDSTRLEN 7

//Volume name
#define VOLNAME "RAMDISK"
#define VOLNAMELEN 7

//RAMDisk Enable Flag
static bool ramdiskEnabled = false;

//RAMDisk data
static uint8_t __attribute__((aligned(4))) ramdisk_data[RAMDISK_SIZE];

/////////////////////////////////////////////////////////////////////
// Mutex
//
// To make RAMDisk access thread-safe.
// Thread-safe is needed because both cores may access to RamDisk at the same
// time if TFTP is running.
//
// The protocol is: all exported functions (non-static functions) which access
// the RamDisk must be protected by Mutex.
//
// No need to use recursive mutex since all functions are simple and do not call other another.
// except FormatRamdiskOnce() function
auto_init_mutex(ramdiskMutex);
#define MUTEXLOCK()   mutex_enter_blocking(&ramdiskMutex)
#define MUTEXUNLOCK() mutex_exit(&ramdiskMutex)

/////////////////////////////////////////////////////////////////
// DMA 
// RAMDisk has its own DMA channel to avoid potential conflict
// with other routines
//
static dma_channel_config_t ramdisk_dma_config;
static int ramdisk_dma_channel;

////////////////////////////////////////////////////////
// Init DMA Channel for Ramdisk
//
static void InitRamdiskDMAChannel() {
  ramdisk_dma_channel = dma_claim_unused_channel(true);
  
  ramdisk_dma_config = dma_channel_get_default_config(ramdisk_dma_channel);
  channel_config_set_transfer_data_size(&ramdisk_dma_config, DMA_SIZE_32);
  channel_config_set_read_increment(&ramdisk_dma_config, true);
  channel_config_set_write_increment(&ramdisk_dma_config, true);
}

//////////////////////////////////////////////////////
// Copy Memory by DMA with 32-bit transfer size
// The src,dest pointers and len must be 32-bit aligned
// 
// Input: dest - destination pointer
//        src  - source pointer
//        len  - Number of bytes to copy
// 
static void RamdiskCopyMemory(uint8_t* dest,const uint8_t *src,const uint32_t len) {
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert((uint32_t)src%4==0);   //must be 32-bit aligned  
  assert(!dma_channel_is_busy(ramdisk_dma_channel));  
  
  dma_channel_configure(
      ramdisk_dma_channel,  // Channel to be configured
      &ramdisk_dma_config,  // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(ramdisk_dma_channel);  
}

//////////////////////////////////////////////////////
// Fill Memory with zeros with 32-bit transfer size
// The dest pointer and len must be 32-bit aligned
//
// Input: dest - destination pointer
//        len  - Number of bytes to fill
//
static void RamdiskZeroMemory(uint8_t *dest,const uint32_t len) {
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert(!dma_channel_is_busy(ramdisk_dma_channel));    
  const uint32_t src[] = {0};
  
  const dma_channel_config orgConfig = ramdisk_dma_config;        //Save Original Config
  channel_config_set_read_increment(&ramdisk_dma_config, false);  //Set read increment to false
  dma_channel_configure(
      ramdisk_dma_channel,  // Channel to be configured
      &ramdisk_dma_config,  // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );

  ramdisk_dma_config = orgConfig;   //restore original DMA Config 
  dma_channel_wait_for_finish_blocking(ramdisk_dma_channel);    
}

//////////////////////////////////////////////////////
// Initalize Ramdisk
//
void InitRamdisk(){
  InitRamdiskDMAChannel();
}

///////////////////////////////////////
// Getter /Setter function
//
bool GetRamdiskEnabled() {
  return ramdiskEnabled;
}

void EnableRamdisk() {
  ramdiskEnabled = true;
  FormatRamdiskOnce();
}

void DisableRamdisk() {
  ramdiskEnabled = false;
}

uint32_t GetUnitCountRamdisk() {
  return ramdiskEnabled?1:0;
}

uint32_t GetBlockCountRamdisk() {
  return RAMDISK_SIZE/BLOCKSIZE;
}

uint32_t GetBlockCountRamdiskActual() {
  return RAMDISK_SIZE/BLOCKSIZE;
}

//Get RAMDisk data pointer
uint8_t* GetRamdiskDataPointer() {
  return ramdisk_data;
}

//Get RAMDisk Size in bytes
size_t GetRamdiskSize() {
  return RAMDISK_SIZE;
}

////////////////////////////////////////////////////////////////////
// Read Block from RAMDisk
// Assume blockNum is valid.
//
// Input: Block Number
//        destBuffer   Destination Buffer (512 Bytes)
//
// Output: SP_NOERR, SP_IOERR
//
rwerror_t ReadBlockRamdisk(const uint blockNum, uint8_t* destBuffer){
  //Validate Block Number
  if (blockNum >= GetBlockCountRamdisk()) return SP_IOERR;
  
  MUTEXLOCK();
  //CopyMemoryAligned(destBuffer,ramdisk_data+blockNum*BLOCKSIZE,BLOCKSIZE);
  RamdiskCopyMemory(destBuffer,ramdisk_data+blockNum*BLOCKSIZE,BLOCKSIZE);
  MUTEXUNLOCK();
  
  return SP_NOERR;
}

////////////////////////////////////////////////////////////////////
// Write Block to RAMDisk
// Assume blockNum is valid.
//
// Input: Block Number
//        srcBuffer   Source Buffer (512 Bytes)
//
// Output: SP_NOERR, SP_IOERR
//
rwerror_t WriteBlockRamdisk(const uint blockNum, const uint8_t* srcBuffer){
  //Validate Block Number
  if (blockNum >= GetBlockCountRamdisk()) return SP_IOERR;
  
  MUTEXLOCK();
  //CopyMemoryAligned(ramdisk_data+blockNum*BLOCKSIZE,srcBuffer,BLOCKSIZE);
  RamdiskCopyMemory(ramdisk_data+blockNum*BLOCKSIZE,srcBuffer,BLOCKSIZE);
  MUTEXUNLOCK();
  
  return SP_NOERR;
}

/////////////////////////////////////////////////////////////
// Get DIB (Device Information Block) of a unit
//
// Input: destBuffer - Pointer to destination buffer
//        
void GetDIBRamdisk(uint8_t *destBuffer) {
  assert(sizeof(struct dib_t)==25);
  struct dib_t *dib = (struct dib_t*)destBuffer;  
  
  //Device Status Byte  
  dib->devicestatus = 0b11111000;          
  
  //Block Count
  uint32_t blockSize = GetBlockCountRamdisk();  
  dib->blocksize_l  = (uint8_t)blockSize; blockSize>>=8; 
  dib->blocksize_m  = (uint8_t)blockSize; blockSize>>=8;                    
  dib->blocksize_h  = (uint8_t)blockSize;
    
  //ID String    
  assert(strlen(IDSTR)==16);              
  assert(IDSTRLEN<=16);
  dib->idstrlen = IDSTRLEN;
  memcpy(dib->idstr,IDSTR,16);
  
  //Device Type, subtype and Firmware Version
  dib->devicetype = 0x00;                  //Device Type. $00 = RAMDisk
  dib->subtype = 0x20;                     //Subtype. $20= not removable, no extended call
  dib->fmversion_l = (uint8_t)FIRMWAREVER; //Firmware Version Word
  dib->fmversion_h = (uint8_t)(FIRMWAREVER>>8);
}

////////////////////////////////////////////////////////////////////
// Erase RAMDisk Quick
// Clear Block 0-2 so that the RAMDisk looks like unformatted
//
void EraseRamdiskQuick() {
  MUTEXLOCK();
  //ZeroMemoryAligned(ramdisk_data,BLOCKSIZE*3); //Erase Block 0-2
  RamdiskZeroMemory(ramdisk_data,BLOCKSIZE*3); //Erase Block 0-2
  MUTEXUNLOCK();
}


////////////////////////////////////////////////////////////////////
// Erase entire RAMDisk
//
void EraseRamdisk() {
  MUTEXLOCK();
  //ZeroMemoryAligned(ramdisk_data,RAMDISK_SIZE);
  RamdiskZeroMemory(ramdisk_data,RAMDISK_SIZE);
  MUTEXUNLOCK();
}


////////////////////////////////////////////////////////////////////
// Format RAMDisk
//
// Only first call is accepted. Subsequent call will be rejected
// to avoid erase of RAMDisk content
//
void FormatRamdiskOnce() {
  static bool formatted = false;
  
  if (formatted) return;    //Once only
  formatted = true;
  
  //DONT add mutex lock because FormatUnit will call WriteBlockRamdisk()
  //unless recursive mutex is used.
  FormatUnit(GetRamdiskUnitNum(),GetBlockCountRamdisk(),VOLNAME,VOLNAMELEN);
  //Ignore error from FormatUnit(). Nothing we can do if error occurs
}

