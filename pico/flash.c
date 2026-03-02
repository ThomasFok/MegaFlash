#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "dmamemops.h"
#include "defines.h"
#include "debug.h"
#include "mediaaccess.h"
#include "flash.h"
#include "userconfig.h"
#include "misc.h"


/////////////////////////////////////////////////////////////////////
// Bit Inversion
//
// All data bits are inverted before they are written to flash.
//
// There are two benefits.
// 1) On a new flash chip, all data appears to be all zeros. It makes
//    more sense to most users.
// 2) The empty blocks of most disk images are all zeros. When those blocks,
//    are written to flash, the flash memory can be kept in erased state (all ones).
//
// The implementation is simple: All ProDOS blocks passing into this module will 
// be bit-inverted before further processing. Similarly, all ProDOS blocks 
// returned from this module will be bit-inverted.
//


/////////////////////////////////////////////////////////////////////
// Mutex
//
// To make flash access thread-safe.
// Thread-safe is needed because both cores may access to flash at the same
// time if TFTP is running.
//
// The protocol is: all exported functions (non-static functions) which access
// the flash memory must be protected by Mutex.
//
// Recursive Mutex is used because the functions are calling one another
// Recursive Mutex avoids dead lock
auto_init_recursive_mutex(flashMutex);
#define MUTEXLOCK()   recursive_mutex_enter_blocking(&flashMutex)
#define MUTEXUNLOCK() recursive_mutex_exit(&flashMutex)



//SPI pins
const uint CS0_PIN  = 5;  //Chip #0 /CS
const uint CS1_PIN  = 28; //Chip #1 /CS
const uint SCK_PIN  = 2;
const uint MOSI_PIN = 3;  //tx
const uint MISO_PIN = 4;  //rx

//SPI Speed
#define SPI_SPEED_INIT    25000000
#define SPI_SPEED_FINAL   75000000

//Constants
#define SECTORSIZE 4096
#define PAGEPERSECTOR 16
#define BLOCKSPERUNIT_P8		 0xffff  //Number of blocks per drive for ProDOS 8
#define BLOCKSPERUNIT_ACTUAL 0x10000 //Actual capacity per drive
#define SIZEPERUNIT_MB 32   

static const uint8_t REPEATED_TX_DATA = 0;
static const uint FLASH_BUSYFLAG = 0b00000001;  //Busy Flash in Flash Status Register

//Flash Chip Device Number
#define DEVICE0 0
#define DEVICE1 1

//Data Buffers
static uint8_t __attribute__((aligned(4))) sectorBuffer[SECTORSIZE];

//Number of units (ProDOS drives) on Flash chip
static uint32_t unitCountFlash0 = 0;
static uint32_t unitCountFlash1 = 0;

//Flash Capacity in MB
static uint32_t flashSize0 = 0;
static uint32_t flashSize1 = 0;



//To store the location of a block
typedef struct {
  uint deviceNum;         //Flash Chip device number
  uint32_t blockAddress;  //Address of block
} blockloc_t;

////////////////////////////////////////////////////////////////////
//Function Prototypes of Read by DMA
//Note: We run SPI at the max speed i.e. clk_peri/2. It turns out
//the software routine spi_read_blocking cannot keep up with SPI. 
//So, DMA is used to drive the transmission. It can also calculate 
//CRC32 of the data.
//For spi_write_blocking, our speed test shows that DMA has no
//speed benefits. WriteToFlashByDMA() routine is currently not used.
//But it is not removed in case it is useful in future.
static uint32_t ReadFromFlashByDMA(uint8_t *destBuffer, const uint32_t len);
static void WriteToFlashByDMA(const uint8_t *srcBuffer, const uint32_t len);


//////////////////////////////////////////////////////
// Return the capacity of flash
//
// Output: uint32_t - Flash capacity in MB
//
uint32_t GetFlashSize() {
  return flashSize0+flashSize1;
}

////////////////////////////////////////////////////////////////////
// Enable SPI CS line
//
// Input: Device Number
//
// Don't remove nop instruction.
// Otherwise, TFTP Download may freeze and
// Flash Read/Write error may occur when SPI is running at 75MHz
static inline void enable_spi0(const uint deviceNum) {
  assert(deviceNum <= 1);
  
  asm volatile("nop");
  gpio_clr_mask(deviceNum==0?1ul<<CS0_PIN:1ul<<CS1_PIN); 
  asm volatile("nop");
}


////////////////////////////////////////////////////////////////////
// Disable SPI All CS lines
//
// Don't remove nop instruction.
// Otherwise, TFTP Download may freeze and
// Flash Read/Write error may occur when SPI is running at 75MHz
static inline void disable_spi0() {
  asm volatile("nop");
  gpio_set_mask(1ul<<CS0_PIN|1ul<<CS1_PIN);
  asm volatile("nop");
}


////////////////////////////////////////////////////////////////////
// Enable 4 Bytes Addressing mode of Flash Chip
//
static void Enable4BytesAddressing(const uint deviceNum) {
  const uint8_t msg[]={0xB7}; //Enter 4-Byte Address Mode command
  
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Send Write Enable command to Flash Chip
//
// Input: Device Number
//
static void __no_inline_not_in_flash_func(WriteEnable)(const uint deviceNum) {
  const uint8_t msg[]={0x06};  //Write Enable command
  
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Send Write Enable for Volatile Status Register command to Flash Chip
//
// Input: Device Number
//
//Note:
//Status Register bits can be both volatile and non-volatile. To write to
//non-volatile bits, Write Enable command (0x06) is sent. Then, write status
//register command. To write to non-volatile bit, Write Enable for Volatile
//Status Register command (0x50) is sent. Then, write status register command. 
//The Reset command also reset volatile status bits
//
static void WriteEnableVSR(const uint deviceNum) {
  const uint8_t msg[]={0x50};  //Write Enable for Volatile SR command
  
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Send Write Disable Command
//
// Input: Device Number
//
static void WriteDisable(const uint deviceNum) {
  const uint8_t msg[]={0x04};  //Write Disable command
  
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Reset Flash Chip
//
// Input: Device Number
//
static void ResetChip(const uint deviceNum,const bool withDelay) {
  const uint8_t msg1[]={0x66};  //Enable Reset command
  const uint8_t msg2[]={0x99};  //Reset command
  
  //Send Enable Reset Command
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg1, 1);
  disable_spi0();
  busy_wait_us_32(1); //Wait 1us before sending Reset command

  //Send Reset Command
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg2, 1);  
  disable_spi0();
  
  //It takes 30us to reset.
  if (withDelay) sleep_us(30);   
}



////////////////////////////////////////////////////////////////////
// Software Die Select
//
static uint8_t DieSelect(const uint deviceNum, const uint8_t dieID) {
  uint8_t msg[2];  
  
  msg[0]=0xc2;  //Software Die Select
  msg[1]=dieID;
  
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 2);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Read Status Register-1 from Flash Chip
//
// Input: Device Number
//
// Output: Status Register-1
//
static uint8_t __no_inline_not_in_flash_func(ReadStatus1)(const uint deviceNum) {
  //Read Status Register-1 Command + 1 Byte Result
  uint8_t txbuffer[2]={0x05};  
  uint8_t rxbuffer[2];
  
  enable_spi0(deviceNum);
  spi_write_read_blocking(spi0, txbuffer, rxbuffer, 2); 
  disable_spi0();
  
  return rxbuffer[1];
}

////////////////////////////////////////////////////////////////////
// Read Status Register-3 from Flash Chip
//
// Input: Device Number
//
// Output: Status Register-3
//
static uint8_t ReadStatus3(const uint deviceNum) {
  //Read Status Register-3 Command + 1 Byte Result
  uint8_t txbuffer[2]={0x15};  
  uint8_t rxbuffer[2];
  
  enable_spi0(deviceNum);
  spi_write_read_blocking(spi0, txbuffer, rxbuffer, 2); 
  disable_spi0();
  
  return rxbuffer[1];
}

////////////////////////////////////////////////////////////////////
// Write to Volatile Status Register-3 
//
// Input: Device Number
//        Value to be written
//
static uint8_t WriteStatus3Volatile(const uint deviceNum, const uint8_t value) {
  uint8_t msg[2];  
  
  msg[0]=0x11;  //Write Status Register-3 command
  msg[1]=value; //8-bit value to be written
  
  WriteDisable(deviceNum);    //Make sure we are writing to Volatile register
  WriteEnableVSR(deviceNum);  //Write Enable Volatile Status Register Command
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 2);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Wait until busy flag is cleared
//
// Input: Device Number
//
static void WaitUntilBusyClear(const uint deviceNum) {
  uint8_t buffer[1] = {0x05}; //Read Status Register-1 Command

  enable_spi0(deviceNum);
  spi_write_blocking(spi0,buffer,1);  //Send Read Status Register-1 command
  
  //keep reading status register 1 until busy flag is cleared
  do{
    busy_wait_us_32(2);  //wait 2us before next polling    
    spi_read_blocking(spi0, REPEATED_TX_DATA, buffer, 1);
  }while(buffer[0] & FLASH_BUSYFLAG);
  
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Set Flash Drive Strength to 75%
//
// Input: Device Number
//
static void SetFlashDriveStrength(const uint deviceNum) {
  uint8_t regvalue = ReadStatus3(deviceNum);
  regvalue &= 0b10011111; //Clear drv1,drv0 bits
  regvalue |= 0b00100000; //Set to 75%
  WriteStatus3Volatile(deviceNum, regvalue);
}

////////////////////////////////////////////////////////////////////
// Program Security Register
// Assume 4 Bytes addressing is being used.
// Assume the security register has been erased
//
// Input: Security Registers Number (1-3),
//        Pointer to Source Data
//        Length of data
//
// Note: Always write to Flash Chip #0
static void ProgramSecurityRegister(const uint32_t regnum,const uint8_t* src,const size_t len) {
  if (regnum==0 || regnum >3) {
    assert(0);
    return;
  }
  
  uint32_t address = regnum<<12;
  uint8_t msg[5];
  
  msg[0] = 0x42;    //Program Security Register
  msg[4] = (uint8_t)(address);  address>>=8;
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  
  WriteEnable(DEVICE0);
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 5);
  spi_write_blocking(spi0,src, len); //Write actual data
  disable_spi0();
  
  //wait until programming finishes
  //It takes about 0.7-3.5ms
  busy_wait_us_32(300); //At least 50ns delay is needed after erase/write command (CS deselect time)
  WaitUntilBusyClear(DEVICE0); 
}

////////////////////////////////////////////////////////////////////
// Erase Security Register (256 bytes)
// Assume 4 Bytes addressing is being used.
//
// Input: Security Register Number (1-3),
//
// Note: Always erase flash chip #0
static void EraseSecurityRegister(const uint32_t regnum) {
  if (regnum==0 || regnum >3) {
    assert(0);
    return;
  }
  
  uint32_t address = regnum<<12;
  uint8_t msg[5];
  
  msg[0] = 0x44;    //Erase Security Register
  msg[4] = (uint8_t)(address);  address>>=8;
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  
  WriteEnable(DEVICE0);
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 5);
  disable_spi0();
  
  //Accoridng to datasheet, Sector Erase needs at least 50ms.
  //Actual Test:50ms
  //Wait until the operation is completed.
  sleep_ms(40);   //At least 50ns delay is needed after erase/write command (CS deselect time)
  WaitUntilBusyClear(DEVICE0);
} 

////////////////////////////////////////////////////////////////////
// Read Security Register to dest
// Assume 4 Bytes addressing is being used.
//
// Input: regnum - Security Register Number (1-3),
//        dest   - Pointer to Destination
//        offset - Read from offset
//        len    - Length of data
//
// Note: Always Read from flash chip #0
static void ReadSecurityRegister(const uint32_t regnum,uint8_t* dest,const uint8_t offset,const size_t len) {
  if (regnum==0 || regnum >3) {
    assert(0);
    return;
  }
  
  if ((uint32_t)offset+len > 256) {
    assert(0);
    return;
  }  
  
  uint32_t address = regnum<<12|offset;
  uint8_t msg[6];
  
  msg[0] = 0x48;    //Read Security Registers
  msg[4] = (uint8_t)(address);  address>>=8;
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  msg[5] = 0;   //Dummy 8-bit 
  
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 6);
  spi_read_blocking(spi0, REPEATED_TX_DATA, dest, len);  //No need to use DMA
  disable_spi0();
}



///////////////////////////////////////////////////////////////////
// Write Security Register from src to any offset
// Assume 4 Bytes addressing is being used.
//
// Input: regnum - Security Register Number (1-3),
//        src    - Pointer to source data
//        offset - Write to offset
//        len    - Length of data
//
// Note: Always Read from flash chip #0
static void WriteSecurityRegister(const uint32_t regnum,const uint8_t* src,const uint8_t offset,const size_t len) {
  if (regnum==0 || regnum >3) {
    assert(0);
    return;
  }
  
  if ((uint32_t)offset+len > 256) {
    assert(0);
    return;
  }
  
  if (offset==0 && len==256) {
    //Overwrite the entire security register
    EraseSecurityRegister(regnum);  
    ProgramSecurityRegister(regnum,src,256);    
  }  else {
    //Read the existing data from security register
    uint8_t buffer[256];
    ReadSecurityRegister(regnum,buffer,0,256);
    
    //Copy source data to buffer
    memcpy(buffer+offset,src,len);

    //Write the data back
    EraseSecurityRegister(regnum);
    ProgramSecurityRegister(regnum,buffer,256);    
  }
}

////////////////////////////////////////////////////////////////////
// Read UserConfig block from security register
// 
// Input: uint8_t* dest - destination buffer
//
// Output: bool - success
//
// Note: Security register 1 and 2 are used to store UserConfigBlock
bool ReadUserConfigBlock(uint8_t* dest) {
  if (0==flashSize0) {
    //clear the dest buffer if flash chip does not exist
    memset(dest,0, 512);
    return false;
  }
  
  MUTEXLOCK();
  ReadSecurityRegister(1,dest    ,0 /*offset*/,256);
  ReadSecurityRegister(2,dest+256,0 /*offset*/,256);
  MUTEXUNLOCK();
  
  return true;
}

////////////////////////////////////////////////////////////////////
// Write UserConfig block to security register
// 
// Input: uint8_t* src - source buffer
//
// Output: bool - success
//
// Note: Security register 1 and 2 are used to store UserConfigBlock
bool WriteUserConfigBlock(const uint8_t *src) {
 if (0==flashSize0) {
    return false;
 }   

  MUTEXLOCK();
  WriteSecurityRegister(1,src    ,0 /*offset*/,256);
  WriteSecurityRegister(2,src+256,0 /*offset*/,256);
  MUTEXUNLOCK();
  
  return true;
}




////////////////////////////////////////////////////////////////////
// Translate unitNum and blockNum to Device Number and Flash Address
// Assume unitNum and blockNum are valid.
//
// Input: ProDOS unit number (1-N)
//        Block Number (0-0xffff)
//        Pointer to uint to receive Device Number
//
// Output: blockloc_t struct
//
static blockloc_t __no_inline_not_in_flash_func(GetBlockLoc)(uint unitNum, const uint blockNum) {
  blockloc_t blockLoc;

  //Make sure unitNum != 0
  if (unitNum == 0) panic("GetBlockLoc() unitNum==0");
  
  if (unitNum<=unitCountFlash0) {
    blockLoc.deviceNum = DEVICE0;
  } else {
    blockLoc.deviceNum = DEVICE1;
    unitNum -= unitCountFlash0;
  }
  
  uint32_t blockAddress;
  blockAddress  = (blockNum & 0b1110000000000000) >>4;
  blockAddress |= (blockNum & 0b0001111111111111) <<12;
  blockAddress |= (uint32_t)(unitNum-1) << 25;
  
  //The lowest 9 bits of blockAddress should be 0.
  assert( (blockAddress & 0x1ff) == 0); 
  blockLoc.blockAddress = blockAddress;
  return blockLoc;
}



////////////////////////////////////////////////////////////////////
// Erase the content of all flash chips
// Note: It takes at least 200 seconds to complete. 
//
static void EraseContent() {
  const uint8_t msg[]= {0x60}; //chip erase command
  
  //Start erase flash chip #0
  WriteEnable(DEVICE0);
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
  
  //Start erase flash chip #1 if it exists
  if (flashSize1!=0) {
    WriteEnable(DEVICE1);
    enable_spi0(DEVICE1);
    spi_write_blocking(spi0, msg, 1);
    disable_spi0();
  }
  
  //Accoridng to datasheet, Chip Erase needs at least 200s.
  sleep_ms(180*1000);

  //Wait until flash chip #0 completes its operation
  while(ReadStatus1(DEVICE0) & FLASH_BUSYFLAG){
    sleep_ms(10);
  }
  
  //Wait until flash chip #1 completes its operation
  if (flashSize1!=0) {
    while(ReadStatus1(DEVICE1) & FLASH_BUSYFLAG){
      sleep_ms(10);
    }
  }
}

////////////////////////////////////////////////////////////////////
// Erase one 4kB Sector
// Note: It takes at least 50ms to complete. 
//
static void EraseSector(const uint deviceNum, uint32_t address) {
  uint8_t msg[5];
  
  //Make sure it aligns at the begining of a sector 
  address = address & 0xfffff000; 
  
  msg[0] = 0x21; //Sector Erase with 4-Byte Address Command
  msg[4] = (uint8_t)(address);  address>>=8;
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  
  WriteEnable(deviceNum);
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 5);
  disable_spi0();
  
  //Accoridng to datasheet, Sector Erase needs at least 50ms.
  //Actual Test: 55-60ms
  //Wait until the operation is completed.
  sleep_ms(40); //At least 50ns delay is needed after erase/write command (CS deselect time)
  WaitUntilBusyClear(deviceNum);
}

////////////////////////////////////////////////////////////////////
// Erase one 64-kB Sector
// Note: It takes at least 150ms to complete. 
//
static void EraseSector64k(const uint deviceNum, uint32_t address) {
  uint8_t msg[5];
  
  //Make sure it aligns at the begining of a sector 
  address = address & 0xffff0000; 
  
  msg[0] = 0xdc; //64kB Sector Erase with 4-Byte Address Command
  msg[4] = (uint8_t)(address);  address>>=8;
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  
  WriteEnable(deviceNum);
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 5);
  disable_spi0();
  
  //Accoridng to datasheet, Sector Erase needs at least 150ms.
  //Actual Test: 220-250ms
  //Wait until the operation is completed.
  sleep_ms(140); //At least 50ns delay is needed after erase/write command (CS deselect time)
  WaitUntilBusyClear(deviceNum);
}
  

////////////////////////////////////////////////////////////////////
// Erase everything on chip
//
void EraseEverything() {
  MUTEXLOCK();
  EraseSecurityRegister(1);
  EraseSecurityRegister(2);
  EraseSecurityRegister(3);
  EraseContent();
  MUTEXUNLOCK();
}

  
////////////////////////////////////////////////////////////////////
// Read one ProDOS block into dest  
//
// Input: Block Location, pointer to destination (512 bytes buffer)
//
// Output: CRC32 of the block data
//
static uint32_t __no_inline_not_in_flash_func(ReadOneBlock)(const blockloc_t blockLoc, uint8_t* dest) { 
  uint32_t blockAddress = blockLoc.blockAddress;
  
  //The lowest 9 bits of blockAddress should be 0.
  assert( (blockAddress & 0x1ff) == 0);

  uint8_t msg[6];
  msg[0] = 0x0C; //Fast Read with 4-Byte Address command
  msg[4] = (uint8_t)(blockAddress); blockAddress>>=8;
  msg[3] = (uint8_t)(blockAddress); blockAddress>>=8; 
  msg[2] = (uint8_t)(blockAddress); blockAddress>>=8;
  msg[1] = (uint8_t)(blockAddress);
  msg[5] = 0;   //Dummy 8-bit
  
  enable_spi0(blockLoc.deviceNum);
  spi_write_blocking(spi0, msg, 6);
  uint32_t crc=ReadFromFlashByDMA(dest,BLOCKSIZE);
  disable_spi0();
  
  return crc;
}


////////////////////////////////////////////////////////////////////
// Read a sector (4kB) to destBuffer
//
// Input: Device Number, Sector Address, Destination Buffer
//
// Output: CRC32 of the sector data
//
// todo: Add destBuffer
static uint32_t __no_inline_not_in_flash_func(ReadSector)(const uint deviceNum,uint32_t sectorAddress,uint8_t* destBuffer){
  uint8_t msg[6];
  
  //Make sure it aligns at the begining of a sector
  sectorAddress = sectorAddress & 0xfffff000; 
  
  msg[0] = 0x0C; //Fast Read with 4-Byte Address
  msg[4] = (uint8_t)(sectorAddress);  sectorAddress>>=8;
  msg[3] = (uint8_t)(sectorAddress);  sectorAddress>>=8;  
  msg[2] = (uint8_t)(sectorAddress);  sectorAddress>>=8;
  msg[1] = (uint8_t)(sectorAddress);
  msg[5] = 0;   //Dummy 8-bit
  
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 6);
  uint32_t crc=ReadFromFlashByDMA(destBuffer,SECTORSIZE);
  disable_spi0();
  
  return crc;
}

////////////////////////////////////////////////////////////////////
// Program one page (256-bytes) to Flash
//
// Input: Device Number, Page Address, Pointer to Source Data (256 Bytes Buffer)
//
static void __no_inline_not_in_flash_func(ProgramOnePage)(const uint deviceNum,uint32_t pageAddress,const uint8_t* src) {
  //The lowest 8 bits of pageAddress should be 0.
  assert( (pageAddress & 0xff) == 0);

  uint8_t msg[5];
  msg[0] = 0x12; //Page Program with 4-Byte Address
  msg[4] = (uint8_t)(pageAddress);  pageAddress>>=8;
  msg[3] = (uint8_t)(pageAddress);  pageAddress>>=8;  
  msg[2] = (uint8_t)(pageAddress);  pageAddress>>=8;
  msg[1] = (uint8_t)(pageAddress);
  
  WriteEnable(deviceNum);
  enable_spi0(deviceNum);
  spi_write_blocking(spi0, msg, 5);
  spi_write_blocking(spi0, src, PAGESIZE); //Write actual data
  disable_spi0();
  
  //wait until programming finishes
  //It takes about 0.7-3.5ms
  busy_wait_us_32(300); //At least 50ns delay is needed after erase/write command (CS deselect time)
  WaitUntilBusyClear(deviceNum);
}

////////////////////////////////////////////////////////////////////
// Verify two block buffers are identical
//
// Input: buf1 - Pointer to first buffer
//        buf2 - Pointer to second buffer
//
// Output: true if they are identical.
//
static bool __no_inline_not_in_flash_func(VerifyOneBlock)(const uint8_t* buf1, const uint8_t* buf2) {
  const uint32_t *p1 = (const uint32_t*)buf1;
  const uint32_t *p2 = (const uint32_t*)buf2; 
  
  for(uint i=BLOCKSIZE/4;i!=0;--i) {  
    if (*p1 != *p2) return false;
    ++p1;
    ++p2;
  }
  
  return true;
}


////////////////////////////////////////////////////////////////////
// Check if an erase operation is needed.
//
// Input: srcBuffer   - The data to be written
//        flashBuffer - The data currently in flash
//
// Note: Data bits in flash can be changed from 1 to 0 only. So,
// an erase is needed if bit already in flash is 0 but the bit 
// to be written is 1.
//
// Output: true if an erase operation is needed.
//
static bool __no_inline_not_in_flash_func(IsEraseNeeded)(const uint8_t* srcBuffer, const uint8_t* flashBuffer) {
  const uint32_t* srcData   = (const uint32_t*)srcBuffer;   /* Data to be written */
  const uint32_t* flashData = (const uint32_t*)flashBuffer; /* Data currently in flash */
  
  for(uint i=BLOCKSIZE/4;i!=0;--i) {    
    if (*srcData & ~*flashData) return true;
    ++srcData;
    ++flashData;
  }
  
  return false;
}

////////////////////////////////////////////////////////////////////
// Check if data page is empty (all FFh)
//
// Input: Pointer to page buffer (256 bytes)
//
// Output: true if all the bytes in the page are FFh
//
static bool __no_inline_not_in_flash_func(IsEmptyPage)(const uint8_t* srcBuffer) {
  const uint32_t* srcData = (const uint32_t*)srcBuffer;
  
  for(uint i=PAGESIZE/4;i!=0;--i) {   
    if (*srcData != 0xffffffff) return false;
    ++srcData;
  }
  return true;
}


////////////////////////////////////////////////////////////////////
// Check if a 64kB Sector in Flash is erased
//
// Input: Device Number, Sector Address
//
// Output: bool 
//
// Note:
// CRC32 Checksum of 4kB sector filled with 0xff = 0xf154670a
// 
static bool IsSector64kErased(const uint deviceNum, uint32_t address) {
  assert( (address&0xffff)==0);
  bool retValue = false; //Assume false (Not erased)
  
  //64kB = 16 4kB-Sector
  //Check each 4kB-sector one by one
  for(uint i=0;i<16;++i) {
    //Read one 4kB Sector
    uint32_t crc32=ReadSector(deviceNum, address, sectorBuffer);
    if (crc32 != 0xf154670a) {
      //Checksum not match. It is not erased.
      retValue = false;
      goto exit;
    }
    address += SECTORSIZE;  //Next Sector Address

    //Check every byte in sectorBuffer
    const uint32_t* srcData = (const uint32_t*)sectorBuffer;
    for(uint j=SECTORSIZE/4;j!=0;--j) {   
      if (*srcData != 0xffffffff) {
        retValue = false;
        goto exit;
      }
      ++srcData;
    }
  }
  retValue = true;
  
exit:  
  return retValue;
}


////////////////////////////////////////////////////////////////////
// Write one ProDOS block with erase operation
//
// Input: blockLoc - Location of the block in flash
//        srcBuffer    - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
static bool __no_inline_not_in_flash_func(WriteOneBlockWithErase)(const blockloc_t blockLoc, const uint8_t* srcBuffer) {
  //
  //Step 1: Read the entire 4kB sector to sectorBuffer
  ReadSector(blockLoc.deviceNum, blockLoc.blockAddress, sectorBuffer); 
  
  //
  //Step 2: Copy data to be written to sectorBuffer in Background by DMA
  const uint32_t pageOffset = blockLoc.blockAddress & 0xfff;
  CopyMemoryAlignedBG(sectorBuffer+pageOffset, srcBuffer, BLOCKSIZE); 
  
  //
  //Step 3: Erase entire sector in flash while data is being copied by DMA
  EraseSector(blockLoc.deviceNum, blockLoc.blockAddress);
  DMAWaitFinish();    //make sure step 2 is complete    
  
  //
  //Step 4: Calculate the CRC32 of sectorBuffer in Background by DMA
  SetCRC32Seed(GetMemoryDMAChannel(),DEFAULT_CRC32_SEED);
  CopyMemoryAlignedBG(sectorBuffer,sectorBuffer,SECTORSIZE);
  
  //
  //Step 5: Program page by page
  uint32_t currentAddress = blockLoc.blockAddress & 0xfffff000; //Align to the begining of the sector
  uint8_t* srcData = sectorBuffer;
  for(uint i=PAGEPERSECTOR;i!=0;--i) {      
    if (!IsEmptyPage(srcData)) {
      ProgramOnePage(blockLoc.deviceNum, currentAddress, srcData);
    }
    currentAddress += PAGESIZE;
    srcData += PAGESIZE;
  }
  
  //
  //Step 6: Verify the written data
  DMAWaitFinish();  //make sure step 4 is finished
  uint32_t crc1=GetCRC();
  uint32_t crc2=ReadSector(blockLoc.deviceNum, blockLoc.blockAddress, sectorBuffer); 
  
  return (crc1==crc2);
}



////////////////////////////////////////////////////////////////////
// Execute Write Block command without erase operation
//
// Input: blockAddress - Address of the block in flash
//        srcBuffer    - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
static bool __no_inline_not_in_flash_func(WriteOneBlockWithoutErase)(const blockloc_t blockLoc, const uint8_t* srcBuffer) {
  //
  //Step 1: Calculate the CRC32 of the data in srcBuffer in Background by DMA
  SetCRC32Seed(GetMemoryDMAChannel(),DEFAULT_CRC32_SEED);
  CopyMemoryAlignedBG((uint8_t*)srcBuffer,srcBuffer,BLOCKSIZE);
  
  //
  //Step 2: Program the data to flash 
  if (!IsEmptyPage(srcBuffer)) {
    ProgramOnePage(blockLoc.deviceNum, blockLoc.blockAddress, srcBuffer);  
  }
  if (!IsEmptyPage(srcBuffer+PAGESIZE)) {
    ProgramOnePage(blockLoc.deviceNum, blockLoc.blockAddress+PAGESIZE, srcBuffer+PAGESIZE);  
  }
  
  //Step 3: Read the result of step 1
  DMAWaitFinish();
  const uint32_t crc1=GetCRC();
  
  //Step 4: Verify the data
  const uint32_t crc2=ReadOneBlock(blockLoc, sectorBuffer); //Use sectorBuffer as temporary buffer
  
  return (crc1==crc2);
}

////////////////////////////////////////////////////////////////////
// Write one ProDOS block from srcBuffer to flash
//
// Input: blockAddress - Address of the block in flash
//        srcBuffer    - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
static bool __no_inline_not_in_flash_func(WriteOneBlock)(const blockloc_t blockLoc, const uint8_t* srcBuffer) {
    //
    //Step 1: Read the block from Flash to sectorBuffer;
    ReadOneBlock(blockLoc, sectorBuffer);
    
    //
    //Step 2: Is the data in flash identical to the data to be written?
    if (VerifyOneBlock(srcBuffer, sectorBuffer)) { 
      return true;
    }
    
    //
    //Step 3: Dispatch to WriteOneBlockWithErase or WriteOneBlockWithoutErase
    if (IsEraseNeeded(srcBuffer, sectorBuffer)) {  
      return WriteOneBlockWithErase(blockLoc,srcBuffer);
    } else {
      return WriteOneBlockWithoutErase(blockLoc,srcBuffer);   
    }     
}

////////////////////////////////////////////////////////////////////
// Read JEDEC ID and return it as a 32-bit integer
// Note: SPI Interface returns MSB first. ie. It is big-endian.
// So, a conversion is needed.
//
// Output:
//   JEDEC ID
//
uint32_t ReadJEDECID(const uint deviceNum) {
  //Command + 3 Bytes Result
  uint8_t txbuffer[4]={0x9f}; 
  uint8_t rxbuffer[4];
  
  MUTEXLOCK();
  enable_spi0(deviceNum);
  spi_write_read_blocking(spi0, txbuffer,rxbuffer, 4);
  disable_spi0();
  MUTEXUNLOCK();

  return (rxbuffer[1]<<16)|(rxbuffer[2]<<8)|rxbuffer[3];
}

////////////////////////////////////////////////////////////////////
// Read 64-bit Unique ID
// Note: SPI Interface returns MSB first. ie. It is big-endian.
// So, a conversion is needed.
//
// Input: Device Number
//
// Output: Unique 64-bit ID
//
uint64_t ReadUniqueID(const uint deviceNum) {
  //2-Bytes Padding + Command + 5 Dummy Bytes + 8-Bytes Result
  uint8_t __attribute__((aligned(8))) txbuffer[16]={0,0,0x4b}; 
  uint8_t __attribute__((aligned(8))) rxbuffer[16]={0,0,0x4b}; 
  //2-bytes padding so that the result is 64-bit aligned
  
  MUTEXLOCK();
  enable_spi0(deviceNum);
  spi_write_read_blocking(spi0,txbuffer+2,rxbuffer+2,14);
  disable_spi0();
  MUTEXUNLOCK();
  
  uint64_t id = *(uint64_t*)(rxbuffer+8);
  
  return __builtin_bswap64(id);  //Endian Conversion
}

////////////////////////////////////////////////////////////////////
// Read 64-bit Unique ID from Device 0
//
// Output: Unique 64-bit ID
//
uint64_t ReadUniqueIDDevice0() {
  return ReadUniqueID(DEVICE0);
}


////////////////////////////////////////////////////////////////////
// Initalize SPI module
//
void InitSpi(){
  // Initialize CS pins
  gpio_init(CS0_PIN);
  gpio_init(CS1_PIN);
  
  //Set CS pins to high and set them to output
  gpio_set_mask(1ul<<CS0_PIN|1ul<<CS1_PIN); 
  gpio_set_dir_out_masked(1ul<<CS0_PIN|1ul<<CS1_PIN);

  //SPI Clock speed
  //Set the SPI speed to lower value so that
  //we can send commands to flash reliably.
  //The InitFlash() function will set the speed
  //to SPI_SPEED_FINAL after initialization of flash
  spi_init(spi0,SPI_SPEED_INIT);    

  //Set slew rate of SPI output pins to fast and 
  //drive strength to 8ma to make SPI work reliably at 75MHz
  gpio_set_slew_rate(CS0_PIN,  GPIO_SLEW_RATE_FAST);
  gpio_set_slew_rate(CS1_PIN,  GPIO_SLEW_RATE_FAST);
  gpio_set_slew_rate(SCK_PIN,  GPIO_SLEW_RATE_FAST);
  gpio_set_slew_rate(MOSI_PIN, GPIO_SLEW_RATE_FAST); //TX
  
  gpio_set_drive_strength(CS0_PIN,  GPIO_DRIVE_STRENGTH_8MA);
  gpio_set_drive_strength(CS1_PIN,  GPIO_DRIVE_STRENGTH_8MA);
  gpio_set_drive_strength(SCK_PIN,  GPIO_DRIVE_STRENGTH_8MA);
  gpio_set_drive_strength(MOSI_PIN, GPIO_DRIVE_STRENGTH_8MA);

  //disable pull resistors of output pins
  gpio_disable_pulls(CS0_PIN);
  gpio_disable_pulls(CS1_PIN);
  gpio_disable_pulls(SCK_PIN);
  gpio_disable_pulls(MOSI_PIN);

  //Set GPIO functions
  gpio_set_function(SCK_PIN,  GPIO_FUNC_SPI);
  gpio_set_function(MOSI_PIN, GPIO_FUNC_SPI);
  gpio_set_function(MISO_PIN, GPIO_FUNC_SPI); 
  gpio_pull_down(MISO_PIN);   //Avoid floating of input pin
    
  spi_set_format(spi0,   // SPI instance
                 8,      // Number of bits per transfer
                 0,      // Polarity (CPOL)
                 1,      // Phase (CPHA)  (Mode 1)
                 SPI_MSB_FIRST);
  //According to Winbond datasheet, their chips can work in SPI mode 0 or 3.
  //Test shows that they work in mode 0,1 or 3 if the speed is <=25MHz
  //We want to run SPI at high clock speed such as 75MHz. At that speed,
  //only mode 1 works.
  
   //Do a dummy read to make sure the clock pin is at
   //correct level.
   uint8_t dummy;
   spi_read_blocking(spi0, REPEATED_TX_DATA, &dummy, 1);
   
  //
  //Notes about /CS pin
  //
  //There is a concern that the /CS is deasserted between each
  //character sent. i.e. A pulse after each byte.
  //To avoid potential problem, /CS pin is controlled manually
  //
  //Reference: https://forums.raspberrypi.com/viewtopic.php?t=322617
  //
}

//////////////////////////////////////////////////////
// Convert supported Flash chip ID to capacity in MB
//
// Input:  id - Flash JEDECID
//
// Output: uint32_t - capacity in MB
//
static uint32_t ChipIDToCapacity(const uint32_t id) {
  if (id==0xef4021) return 128;         //Winbond W25Q01JV
  else if (id==0xef7021) return 128;    //Winbond W25Q01JV-DTR
  else if (id==0xef4020) return 64;     //Winbond W25Q512JV
  else if (id==0xef7020) return 64;     //Winbond W25Q512JV-DTR  
  else if (id==0xef7022) return 256;    //Winbond W25Q02JV-DTR
    
  return 0;
}



//////////////////////////////////////////////////////
// Read data from flash by DMA
// Replace spi_read_blocking routine()
//
// Input: destBuffer - Destination Buffer
//        len - Number of Bytes
//
// Output: CRC32 of the data read from flash
//
static uint32_t __no_inline_not_in_flash_func(ReadFromFlashByDMA)(uint8_t *destBuffer,const uint32_t len) {
  static int txChannel;
  static int rxChannel;
  
  static bool alreadyConfigured = false;
  static dma_channel_config_t tx_config;
  static dma_channel_config_t rx_config;
  
  uint8_t src[] = {REPEATED_TX_DATA}; //Tx data source  

  if (!alreadyConfigured) {
    alreadyConfigured = true;
    
    //DMA Channel Number
    txChannel = dma_claim_unused_channel(true);
    rxChannel = dma_claim_unused_channel(true);    
    
    //TX DMA Config
    tx_config = dma_channel_get_default_config(txChannel);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_dreq(&tx_config,spi_get_dreq(spi0,true));  // true = tx dreq
    channel_config_set_write_increment(&tx_config, false);  
    channel_config_set_read_increment(&tx_config, false); 

    //RX DMA Config
    rx_config = dma_channel_get_default_config(rxChannel);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_dreq(&rx_config,spi_get_dreq(spi0,false));  // false = rx dreq
    channel_config_set_write_increment(&rx_config, true); 
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_sniff_enable(&rx_config, true);
  }

  //TX DMA Channel
  dma_channel_configure(txChannel,&tx_config,
                        &spi_get_hw(spi0)->dr,  //destination
                        src,                    //source
                        len,
                        false);                 //Don't start

  //RX DMA Channel
  dma_channel_configure(rxChannel,&rx_config,
                        destBuffer,             //destination
                        &spi_get_hw(spi0)->dr,  //source
                        len,
                        false);                 //Don't start
  SetCRC32Seed(rxChannel,DEFAULT_CRC32_SEED);

  //start dma 
  dma_start_channel_mask((1u<<txChannel) | (1u<<rxChannel));
  
  //tx should complete before rx
  dma_channel_wait_for_finish_blocking(rxChannel);
        
  return GetCRC();
}


//////////////////////////////////////////////////////
// Write data to flash by DMA
// Replace spi_write_blocking routine()
//
// Input: srcBuffer - Source Buffer
//        len - Number of Bytes
//
static void __no_inline_not_in_flash_func(WriteToFlashByDMA)(const uint8_t *srcBuffer, const uint32_t len) {
  static int txChannel;
  static int rxChannel;
  
  static bool alreadyConfigured = false;
  static dma_channel_config_t tx_config;
  static dma_channel_config_t rx_config;
  
  uint32_t dest[1]; //dummy data destination  

  if (!alreadyConfigured) {
    alreadyConfigured = true;
    
    //DMA Channel Number
    txChannel = dma_claim_unused_channel(true);
    rxChannel = dma_claim_unused_channel(true);
    
    //TX DMA Config
    tx_config = dma_channel_get_default_config(txChannel);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_dreq(&tx_config,spi_get_dreq(spi0,true));  // true = tx dreq
    channel_config_set_write_increment(&tx_config, false);  
    channel_config_set_read_increment(&tx_config, true);  

    //RX DMA Config
    rx_config = dma_channel_get_default_config(rxChannel);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_dreq(&rx_config,spi_get_dreq(spi0,false));  // false = rx dreq
    channel_config_set_write_increment(&rx_config, false);  
    channel_config_set_read_increment(&rx_config, false);
  }

  //TX DMA Channel
  dma_channel_configure(txChannel,&tx_config,
                        &spi_get_hw(spi0)->dr,  //destination
                        srcBuffer,              //source
                        len,
                        false);                 //Don't start

  //RX DMA Channel
  dma_channel_configure(rxChannel,&rx_config,
                        dest,                   //destination
                        &spi_get_hw(spi0)->dr,  //source
                        len,
                        false);                 //Don't start

  //start dma 
  dma_start_channel_mask((1u<<txChannel) | (1u<<rxChannel));
  
  //tx should complete before rx
  dma_channel_wait_for_finish_blocking(rxChannel);
  
  //Note: RX DMA Channel is needed because when TX DMA Channel finishes,
  //it means the data is transfered from memory to SPI. The last data 
  //byte may not be completely sent out to the Flash. It may still be 
  //in SPI of Pico. We want this function return only after all the data
  //is sent.  We wait for completion of RX channel and it guarantee
  //all the data is sent.
}


////////////////////////////////////////////////////////////////////
// Initalize Flash related data
//
void InitFlash() {
  //Init Flash chip #0
  uint32_t id = ReadJEDECID(DEVICE0);
  flashSize0 = ChipIDToCapacity(id);
  unitCountFlash0 = flashSize0 / SIZEPERUNIT_MB;
  if (flashSize0 != 0) {
    SetFlashDriveStrength(DEVICE0);
    Enable4BytesAddressing(DEVICE0);
  } else {
    //Single Flash chip in Flash #1 is not supported.
    //So, if flash chip #0 is not present, set flashSize1 to 0.
    flashSize1 = 0;
    unitCountFlash1 = 0;
    return; 
  }

  //Init Flash Chip #1
  id = ReadJEDECID(DEVICE1);
  flashSize1 = ChipIDToCapacity(id);
  unitCountFlash1 = flashSize1 / SIZEPERUNIT_MB;
  if (flashSize1 != 0) {
    SetFlashDriveStrength(DEVICE1);
    Enable4BytesAddressing(DEVICE1);
  }
  
  //Set SPI Speed to SPI_SPEED_FINAL
  spi_set_baudrate(spi0, SPI_SPEED_FINAL);
  
  //Flash Chip is present.
  //Disable the pull resistor for maximum data transmission speed
  gpio_disable_pulls(MISO_PIN);
}

//******************************************************************
//
//      Media Access Routines
//
//******************************************************************

//////////////////////////////////////////////////////
// Return the acutal unit count of Flash
//
// Output: uintCount - Number of ProDOS drives
//
uint32_t GetUnitCountFlashActual(){
  return unitCountFlash0+unitCountFlash1;
}


////////////////////////////////////////////////////////////////////
// Get the total number of block of the unit reported to Prodos
// Assume unitNum is valid
//
// Input: ProDOS unit number (1-N)
//
// Output: Total Number of blocks of the unit
//
uint32_t GetBlockCountFlash(const uint unitNum) {
  //Blocks per unit is hard-coded in current implementation
  return BLOCKSPERUNIT_P8;
}

////////////////////////////////////////////////////////////////////
// Get actual total number of block of the unit
// Assume unitNum is valid
//
// Input: ProDOS unit number (1-N)
//
// Output: Total Number of blocks of the unit
//
uint32_t GetBlockCountFlashActual(const uint unitNum) {
  //Blocks per unit is hard-coded in current implementation
  return BLOCKSPERUNIT_ACTUAL;
}



/////////////////////////////////////////////////////////////
// Get DIB (Device Information Block) of a unit
// Assume unitNum is valid.
//
// Input: unitNum    - Unit Number (1-N)
//        destBuffer - Pointer to destination buffer
//
void GetDIBFlash(const uint unitNum, uint8_t *destBuffer) {
  //ID String padded to 16 bytes long
  #define IDSTR "MEGAFLASH DRV N "
  #define IDSTRLEN        15
  #define IDSTR_DN_OFFSET 14  //Offset to Drive Number Char  
  
  assert(sizeof(struct dib_t)==25);
  struct dib_t *dib = (struct dib_t*)destBuffer;  
  
  //Device Status Byte  
  dib->devicestatus = 0b11111000;          
  
  //Block Count
  uint32_t blockSize = GetBlockCountFlash(unitNum);  
  dib->blocksize_l  = (uint8_t)blockSize; blockSize>>=8; 
  dib->blocksize_m  = (uint8_t)blockSize; blockSize>>=8;                    
  dib->blocksize_h  = (uint8_t)blockSize;
    
  //ID String    
  assert(strlen(IDSTR)==16);              
  assert(IDSTRLEN<=16);
  dib->idstrlen = IDSTRLEN;
  memcpy(dib->idstr,IDSTR,16);
  dib->idstr[IDSTR_DN_OFFSET] = '0'+unitNum;
  
  //Device Type, subtype and Firmware Version
  dib->devicetype = 0x02;                  //Device Type. $02 = Harddisk
  dib->subtype = 0x20;                     //Subtype. $20= not removable, no extended call
  dib->fmversion_l = (uint8_t)FIRMWAREVER; //Firmware Version Word
  dib->fmversion_h = (uint8_t)(FIRMWAREVER>>8);
}


/////////////////////////////////////////////////////////////////////////////////////////
//
//                    Media Access Routines with Bit Inversion 
//
/////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////
// Copy data from srcBuffer to destBuffer with bit inversion (bitwise not)
//
// Input: destBuffer - Pointer to destination buffer
//        srcBuffer  - Pointer to source buffer
//        len        - Number of bytes to be copied
//
// Note: The pointers must be 32-bit aligned. and len must be
//       multiple of 4.
//
//       It takes 7us to copy 512 Bytes on 150MHz RP2350
//
static void __no_inline_not_in_flash_func(CopyBitInversion)(uint8_t* destBuffer,const uint8_t* srcBuffer,uint32_t len) {
  assert(len%4==0);
  uint32_t* dest = (uint32_t*) destBuffer;
  uint32_t* src  = (uint32_t*) srcBuffer;
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert((uint32_t)src%4==0);   //must be 32-bit aligned    
  
  for(uint32_t i=len/4;i!=0;--i) {
    *dest++ = ~*src++;
  }
}


////////////////////////////////////////////////////////////////////
// Read Block from Flash
// Assume unitNum and blockNum are valid.
//
// Input: Unit Number, Block Number
//        destBuffer   Destination Buffer (512 Bytes)
//
// Output: SP_NOERR, SP_IOERR
//
rwerror_t __no_inline_not_in_flash_func(ReadBlockFlash)(const uint unitNum, const uint blockNum, uint8_t* destBuffer) {
  const blockloc_t blockLoc = GetBlockLoc(unitNum, blockNum);
  
  MUTEXLOCK();  
  ReadOneBlock(blockLoc, destBuffer);                 //Read from flash
  CopyBitInversion(destBuffer,destBuffer,BLOCKSIZE);  //Bit invert in-place
  MUTEXUNLOCK();  
  
  return SP_NOERR; 
}

////////////////////////////////////////////////////////////////////
// Write Block to Flash
// Assume unitNum and blockNum are valid.
//
// Input: Unit Number, Block Number
//        srcBuffer    - Data to be written (512 Bytes)
//
// Output: SP_NOERR, SP_IOERR
//
rwerror_t __no_inline_not_in_flash_func(WriteBlockFlash)(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer){
  const blockloc_t blockLoc = GetBlockLoc(unitNum, blockNum);
  
  MUTEXLOCK();   
  uint8_t __attribute__((aligned(4))) tempWriteBuffer[BLOCKSIZE];  
  CopyBitInversion(tempWriteBuffer,srcBuffer,BLOCKSIZE);
  bool success = WriteOneBlock(blockLoc, tempWriteBuffer);
  MUTEXUNLOCK();

  return success? SP_NOERR : SP_IOERR;
}

////////////////////////////////////////////////////////////////////
// Write Block to Flash for Disk Image Transfer
// Assume unitNum and blockNum are valid.
//
// Input: Unit Number, Block Number
//        srcBuffer    - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
// This routine assumes the existing data in entire the ProDOS unit 
// can be erased. It erases the flash chip in 64kB chunk and does not
// preserve existing data
//
bool WriteBlockFlashForImageTransfer(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer){
  bool success;
  const blockloc_t blockLoc = GetBlockLoc(unitNum,blockNum);

  MUTEXLOCK();
  //Erase 64kB sector every 16 blocks and block number <8192
  if (blockNum<8192 && blockNum%16 == 0) {
    assert( (blockLoc.blockAddress&0xffff) == 0);  //Block Address should be 64k-aligned
      
    if (!IsSector64kErased(blockLoc.deviceNum, blockLoc.blockAddress)) {
      EraseSector64k(blockLoc.deviceNum,blockLoc.blockAddress);
    }
  }

  //Program the block to flash
  uint8_t __attribute__((aligned(4))) tempWriteBuffer[BLOCKSIZE];  
  CopyBitInversion(tempWriteBuffer,srcBuffer,BLOCKSIZE);
  success = WriteOneBlockWithoutErase(blockLoc,tempWriteBuffer);

  MUTEXUNLOCK();
  
  return success;
}

/////////////////////////////////////////////////////////////////////////////////////////
//
//                    Erase Flash Disk Routines
//
/////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////
// Abort Erase Flash Disk Process
//
// EraseFlashDisk() takes up to 2 minutes and it is used
// by Control Panel. If Apple is reset during the erase,
// the erase process should be aborted. Otherwise, MegaFlash
// does not respond any requests from Apple.
//
// When Apple is reset, an interrupt is generated. The ISR 
// calls AbortEraseFlashDisk() and abortEraseFlashDisk variable
// is set. Then, the process is aborted.
//
static volatile bool abortEraseFlashDisk;
void AbortEraseFlashDisk() {
  abortEraseFlashDisk = true;
}

////////////////////////////////////////////////////////////////////
// Erase entire unit
// Assume unitNum is valid.
//
// Input: Unit Number
//
void EraseFlashDisk(const uint unitNum){
  MUTEXLOCK();
  abortEraseFlashDisk = false;  //reset the flag

  //Erase 64kB sector every 16 blocks and block number <8192
  for(uint blockNum=0;blockNum<8192;blockNum+=16) {
    blockloc_t blockLoc = GetBlockLoc(unitNum,blockNum);

    if (abortEraseFlashDisk) break;
    
    if (!IsSector64kErased(blockLoc.deviceNum, blockLoc.blockAddress)) {
      if (abortEraseFlashDisk) break;
      EraseSector64k(blockLoc.deviceNum,blockLoc.blockAddress);
    }
  }
  MUTEXUNLOCK();
}

