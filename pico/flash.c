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
static uint8_t __attribute__((aligned(4))) sector_buffer[SECTORSIZE];

//Number of units (ProDOS drives) on Flash chip
static uint32_t unit_count_flash0 = 0;
static uint32_t unit_count_flash1 = 0;

//Flash Capacity in MB
static uint32_t flash_size0 = 0;
static uint32_t flash_size1 = 0;

//Flash Type
typedef enum {
  NOR_WINBOND,
  NOR_ISSI
} flash_type_t;
static flash_type_t flash_type = NOR_WINBOND;


//To store the location of a block
typedef struct {
  uint device_num;         //Flash Chip device number
  uint32_t block_address;  //Address of block
} blockloc_t;

////////////////////////////////////////////////////////////////////
//Function Prototypes of Read by DMA
//Note: We run SPI at the max speed i.e. clk_peri/2. It turns out
//the software routine spi_read_blocking cannot keep up with SPI. 
//So, DMA is used to drive the transmission. It can also calculate 
//CRC32 of the data.
//For spi_write_blocking, our speed test shows that DMA has no
//speed benefits. 
static uint32_t ReadFromFlashByDMA(uint8_t *dest_buffer,const uint32_t len,bool* success_out);


//////////////////////////////////////////////////////
// Return the capacity of flash
//
// Output: uint32_t - Flash capacity in MB
//
uint32_t GetFlashSize() {
  return flash_size0+flash_size1;
}

////////////////////////////////////////////////////////////////////
// Enable SPI CS line
//
// Input: Device Number
//
// Don't remove nop instruction.
// Otherwise, TFTP Download may freeze and
// Flash Read/Write error may occur when SPI is running at 75MHz
static inline void enable_spi0(const uint device_num) {
  assert(device_num <= 1);
  #if OC_RP2350
  asm volatile("nop");  
  #endif
  asm volatile("nop");
  gpio_clr_mask(device_num==0?1ul<<CS0_PIN:1ul<<CS1_PIN); 
  #if OC_RP2350
  asm volatile("nop");  
  #endif
  asm volatile("nop");  
}


////////////////////////////////////////////////////////////////////
// Disable SPI All CS lines
//
// Don't remove nop instruction.
// Otherwise, TFTP Download may freeze and
// Flash Read/Write error may occur when SPI is running at 75MHz
static inline void disable_spi0() {
  #if OC_RP2350
  asm volatile("nop");  
  #endif
  asm volatile("nop");  
  gpio_set_mask(1ul<<CS0_PIN|1ul<<CS1_PIN);
  #if OC_RP2350
  asm volatile("nop");  
  #endif
  asm volatile("nop");  
}


////////////////////////////////////////////////////////////////////
// Enable 4 Bytes Addressing mode of Flash Chip
//
static void Enable4BytesAddressing(const uint device_num) {
  const uint8_t msg[]={0xB7}; //Enter 4-Byte Address Mode command
  
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Send Write Enable command to Flash Chip
//
// Input: Device Number
//
static void __no_inline_not_in_flash_func(WriteEnable)(const uint device_num) {
  const uint8_t msg[]={0x06};  //Write Enable command
  
  enable_spi0(device_num);
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
static void WriteEnableVSR_Winbond(const uint device_num) {
  assert(flash_type == NOR_WINBOND);  //WinBond only command
  const uint8_t msg[]={0x50};  //Write Enable for Volatile SR command
  
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Send Write Disable Command
//
// Input: Device Number
//
static void WriteDisable(const uint device_num) {
  const uint8_t msg[]={0x04};  //Write Disable command
  
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 1);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Reset Flash Chip
//
// Input: Device Number
//
static void ResetChip(const uint device_num,const bool with_delay) {
  const uint8_t msg1[]={0x66};  //Enable Reset command
  const uint8_t msg2[]={0x99};  //Reset command
  
  //Send Enable Reset Command
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg1, 1);
  disable_spi0();
  busy_wait_us_32(1); //Wait 1us before sending Reset command

  //Send Reset Command
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg2, 1);  
  disable_spi0();
  
  //It takes 30us(WinBond) or 100us(ISSI) to reset.
  if (with_delay) {
    if (flash_type == NOR_ISSI) sleep_us(100);
    else sleep_us(30);   
  }
}


////////////////////////////////////////////////////////////////////
// Software Die Select
//
static uint8_t DieSelect_Winbond(const uint device_num, const uint8_t die_id) {
  assert(flash_type == NOR_WINBOND);  //WinBond only command
  uint8_t msg[2];  
  
  msg[0]=0xc2;  //Software Die Select
  msg[1]=die_id;
  
  enable_spi0(device_num);
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
//Note Status Register-1 are the same on WinBond and ISSI
static uint8_t __no_inline_not_in_flash_func(ReadStatus1)(const uint device_num) {
  //Read Status Register-1 Command + 1 Byte Result
  uint8_t txbuffer[2]={0x05};  
  uint8_t rxbuffer[2];
  
  enable_spi0(device_num);
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
static uint8_t ReadStatus3_Winbond(const uint device_num) {
  assert(flash_type == NOR_WINBOND);  //Winbond only command
  //Read Status Register-3 Command + 1 Byte Result
  uint8_t txbuffer[2]={0x15};  
  uint8_t rxbuffer[2];
  
  enable_spi0(device_num);
  spi_write_read_blocking(spi0, txbuffer, rxbuffer, 2); 
  disable_spi0();
  
  return rxbuffer[1];
}

////////////////////////////////////////////////////////////////////
// Write to Volatile Status Register-3 (Winbond)
//
// Input: Device Number
//        Value to be written
//
static uint8_t WriteStatus3Volatile_Winbond(const uint device_num, const uint8_t value) {
  assert(flash_type == NOR_WINBOND);  //Winbond only command  
  uint8_t msg[2];  
  
  msg[0]=0x11;  //Write Status Register-3 command
  msg[1]=value; //8-bit value to be written
  
  WriteDisable(device_num);    //Make sure we are writing to Volatile register
  WriteEnableVSR_Winbond(device_num);  //Write Enable Volatile Status Register Command
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 2);
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Write to Volatile Extended Read Parameters Register (ISSI)
//
// Input: Device Number
//        Value to be written
//
static uint8_t WriteExtendedReadVolatile_ISSI(const uint device_num, const uint8_t value) {
  assert(flash_type == NOR_ISSI);  //ISSI only command  
  uint8_t msg[2];  
  
  msg[0]=0x83;  //Set Extended Read Parameter Volatile command
  msg[1]=value; //8-bit value to be written
  
  WriteEnable(device_num);
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 2);
  disable_spi0();
}



////////////////////////////////////////////////////////////////////
// Wait until busy flag is cleared
//
// Input: Device Number
//
static void WaitUntilBusyClear(const uint device_num) {
  uint8_t buffer[1] = {0x05}; //Read Status Register-1 Command

  enable_spi0(device_num);
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
static void SetFlashDriveStrength(const uint device_num) {
  if (flash_type == NOR_WINBOND) {
    uint8_t regvalue = ReadStatus3_Winbond(device_num);
    regvalue &= 0b10011111; //Clear drv1,drv0 bits
    regvalue |= 0b00100000; //Set to 75%
    WriteStatus3Volatile_Winbond(device_num, regvalue);
  } else if (flash_type == NOR_ISSI) {
    //Note: Bit 3-0 is read only
    //Bit 4 is DLPEN (Data learning Pattern Enable), which should be set to 0
    //Bit 7-5 is drive strength. Default: 0b111 (50%). 0b101=75%, 0b110=100%
    WriteExtendedReadVolatile_ISSI(device_num, 0b10100000);
  }
}

////////////////////////////////////////////////////////////////////
// Program Security Information Row (ISSI)
// Assume the security infomration row has been erased
//
// Input: Security Registers Number (0-3),
//        Pointer to Source Data
//        Length of data
//
// Note: Always write to Flash Chip #0
static void ProgramSecurityInformationRow_ISSI(const uint32_t regnum,const uint8_t* src,const size_t len) {
  assert(flash_type == NOR_ISSI);  //ISSI only command  
  if (regnum >3) {
    assert(0);
    return;
  }
  
  uint32_t address = regnum<<12;
  uint8_t msg[4];
  
  msg[0] = 0x62;    //Program Information Row
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  
  WriteEnable(DEVICE0);
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 4);
  spi_write_blocking(spi0,src, len); //Write actual data
  disable_spi0();
  
  //wait until programming finishes
  //It takes about 0.3-2.0ms
  busy_wait_us_32(300);
  WaitUntilBusyClear(DEVICE0); 
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
static void ProgramSecurityRegister_WinBond(const uint32_t regnum,const uint8_t* src,const size_t len) {
  assert(flash_type == NOR_WINBOND);  //Winbond only command  
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
static void EraseSecurityRegister_Winbond(const uint32_t regnum) {
  assert(flash_type == NOR_WINBOND);  //Winbond only command  
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
// Erase Security Information Row (512 bytes) (ISSI)
//
// Input: Security Informat Row Number (0-3),
//
// Note: Always erase flash chip #0
static void EraseSecurityInformationRow_ISSI(const uint32_t regnum) {
  assert(flash_type == NOR_ISSI);  //ISSI only command  
  if (regnum >3) {
    assert(0);
    return;
  }
  
  uint32_t address = regnum<<12;
  uint8_t msg[4];
  
  msg[0] = 0x64;    //Information Row Erase
  msg[3] = (uint8_t)(address);  address>>=8;
  msg[2] = (uint8_t)(address);  address>>=8;  
  msg[1] = (uint8_t)(address);
  
  WriteEnable(DEVICE0);
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 4);
  disable_spi0();
  
  //Datasheet does not specify the erase time
  //Assume it requires 50ms
  //Wait until the operation is completed.
  sleep_ms(50);   
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
static void ReadSecurityRegister_Winbond(const uint32_t regnum,uint8_t* dest,const uint8_t offset,const size_t len) {
  assert(flash_type == NOR_WINBOND);  //Winbond only command    
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
  msg[5] = 0;       //Dummy 8-bit 
  
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 6);
  spi_read_blocking(spi0, REPEATED_TX_DATA, dest, len);  //No need to use DMA
  disable_spi0();
}

////////////////////////////////////////////////////////////////////
// Read Security Information Row to dest (ISSI)
//
// Input: regnum - Security Register Number (0-3),
//        dest   - Pointer to Destination
//        offset - Read from offset
//        len    - Length of data
//
// Note: Always Read from flash chip #0
static void ReadSecurityInformationRow_ISSI(const uint32_t regnum,uint8_t* dest,const uint8_t offset,const size_t len) {
  assert(flash_type == NOR_ISSI);  //ISSI only command    
  if (regnum >3) {
    assert(0);
    return;
  }
  
  if ((uint32_t)offset+len > 256) {
    assert(0);
    return;
  }  
  
  uint32_t address = regnum<<12|offset;
  uint8_t msg[5];
  
  msg[0] = 0x68;    //Information Row Read
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  msg[4] = 0;       //Dummy 8-bit 
  
  enable_spi0(DEVICE0);
  spi_write_blocking(spi0, msg, 5);
  spi_read_blocking(spi0, REPEATED_TX_DATA, dest, len);
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
static void WriteSecurityRegister_Winbond(const uint32_t regnum,const uint8_t* src,const uint8_t offset,const size_t len) {
  assert(flash_type == NOR_WINBOND);  //Winbond only command    
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
    EraseSecurityRegister_Winbond(regnum);  
    ProgramSecurityRegister_WinBond(regnum,src,256);    
  }  else {
    //Read the existing data from security register
    uint8_t buffer[256];
    ReadSecurityRegister_Winbond(regnum,buffer,0,256);
    
    //Copy source data to buffer
    memcpy(buffer+offset,src,len);

    //Write the data back
    EraseSecurityRegister_Winbond(regnum);
    ProgramSecurityRegister_WinBond(regnum,buffer,256);    
  }
}


///////////////////////////////////////////////////////////////////
// Write Security Information Row from src to any offset (ISSI)
//
// Input: regnum - Security Register Number (1-3),
//        src    - Pointer to source data
//        offset - Write to offset
//        len    - Length of data
//
// Note: Always Read from flash chip #0
static void WriteSecurityInformationRow_ISSI(const uint32_t regnum,const uint8_t* src,const uint8_t offset,const size_t len) {
  assert(flash_type == NOR_ISSI);  //ISSI only command    
  if (regnum >3) {
    assert(0);
    return;
  }
  
  if ((uint32_t)offset+len > 256) {
    assert(0);
    return;
  }
  
  if (offset==0 && len==256) {
    //Overwrite the entire security register
    EraseSecurityInformationRow_ISSI(regnum);  
    ProgramSecurityInformationRow_ISSI(regnum,src,256);    
  }  else {
    //Read the existing data from security register
    uint8_t buffer[256];
    ReadSecurityInformationRow_ISSI(regnum,buffer,0,256);
    
    //Copy source data to buffer
    memcpy(buffer+offset,src,len);

    //Write the data back
    EraseSecurityInformationRow_ISSI(regnum);
    ProgramSecurityInformationRow_ISSI(regnum,buffer,256);    
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
  if (0==flash_size0) {
    //clear the dest buffer if flash chip does not exist
    memset(dest,0, 512);
    return false;
  }
  
  MUTEXLOCK();
  if (flash_type == NOR_WINBOND) {
    ReadSecurityRegister_Winbond(1,dest    ,0 /*offset*/,256);
    ReadSecurityRegister_Winbond(2,dest+256,0 /*offset*/,256);
  } else if (flash_type == NOR_ISSI){
    ReadSecurityInformationRow_ISSI(0,dest    ,0 /*offset*/,256);
    ReadSecurityInformationRow_ISSI(1,dest+256,0 /*offset*/,256);
  }
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
 if (0==flash_size0) {
    return false;
 }   

  MUTEXLOCK();
  if (flash_type == NOR_WINBOND) {
    WriteSecurityRegister_Winbond(1,src    ,0 /*offset*/,256);
    WriteSecurityRegister_Winbond(2,src+256,0 /*offset*/,256);
  } else if (flash_type == NOR_ISSI) {
    WriteSecurityInformationRow_ISSI(0,src    ,0 /*offset*/,256);
    WriteSecurityInformationRow_ISSI(1,src+256,0 /*offset*/,256);
  }
  MUTEXUNLOCK();
  
  return true;
}


////////////////////////////////////////////////////////////////////
// Translate unit_num and block_num to Device Number and Flash Address
// Assume unit_num and block_num are valid.
//
// Input: ProDOS unit number (1-N)
//        Block Number (0-0xffff)
//        Pointer to uint to receive Device Number
//
// Output: blockloc_t struct
//
static blockloc_t __no_inline_not_in_flash_func(GetBlockLoc)(uint unit_num, const uint block_num) {
  blockloc_t blockloc;

  //Make sure unit_num != 0
  if (unit_num == 0) panic("GetBlockLoc() unit_num==0");
  
  if (unit_num<=unit_count_flash0) {
    blockloc.device_num = DEVICE0;
  } else {
    blockloc.device_num = DEVICE1;
    unit_num -= unit_count_flash0;
  }
  
  uint32_t block_address;
  block_address  = (block_num & 0b1110000000000000) >>4;
  block_address |= (block_num & 0b0001111111111111) <<12;
  block_address |= (uint32_t)(unit_num-1) << 25;
  
  //The lowest 9 bits of block_address should be 0.
  assert( (block_address & 0x1ff) == 0); 
  blockloc.block_address = block_address;
  return blockloc;
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
  if (flash_size1!=0) {
    WriteEnable(DEVICE1);
    enable_spi0(DEVICE1);
    spi_write_blocking(spi0, msg, 1);
    disable_spi0();
  }
  
  //Accoridng to datasheet, Chip Erase needs at least 200s (Winbond) or 100s (ISSI IS25LP01GJ).
  if (flash_type == NOR_WINBOND) sleep_ms(180*1000);
  else if (flash_type == NOR_ISSI) sleep_ms(80*1000);

  //Wait until flash chip #0 completes its operation
  while(ReadStatus1(DEVICE0) & FLASH_BUSYFLAG){
    sleep_ms(10);
  }
  
  //Wait until flash chip #1 completes its operation
  if (flash_size1!=0) {
    while(ReadStatus1(DEVICE1) & FLASH_BUSYFLAG){
      sleep_ms(10);
    }
  }
}

////////////////////////////////////////////////////////////////////
// Erase one 4kB Sector
// Note: It takes at least 50ms(Winbond) or 100ms(ISSI) to complete. 
//
static void EraseSector(const uint device_num, uint32_t address) {
  uint8_t msg[5];
  
  
  //Make sure it aligns at the begining of a sector 
  address = address & 0xfffff000; 
  
  msg[0] = 0x21; //Sector Erase with 4-Byte Address Command
  msg[4] = (uint8_t)(address);  address>>=8;
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  
  WriteEnable(device_num);
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 5);
  disable_spi0();
  //At least 50ns delay is needed after erase/write command (CS deselect time)  
  
  //Accoridng to datasheet, Sector Erase needs at least 50ms(Winbond) or 100ms(ISSI IS25LP01GJ).
  //Actual Test on WinBond: 55-60ms
  //Wait until the operation is completed.
  sleep_ms(40); 
  
  WaitUntilBusyClear(device_num);
}

////////////////////////////////////////////////////////////////////
// Erase one 64-kB Sector
// Note: It takes at least 150ms(Winbond) or 170ms(ISSI) to complete. 
//
static void EraseSector64k(const uint device_num, uint32_t address) {
  uint8_t msg[5];
  
  //Make sure it aligns at the begining of a sector 
  address = address & 0xffff0000; 
  
  msg[0] = 0xdc; //64kB Sector Erase with 4-Byte Address Command
  msg[4] = (uint8_t)(address);  address>>=8;
  msg[3] = (uint8_t)(address);  address>>=8;  
  msg[2] = (uint8_t)(address);  address>>=8;
  msg[1] = (uint8_t)(address);
  
  WriteEnable(device_num);
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 5);
  disable_spi0();
  //At least 50ns delay is needed after erase/write command (CS deselect time)  
  
  //Accoridng to datasheet, Sector Erase needs at least 150ms(Winbond) or 130ms(ISSI IS25LP01GJ).
  //Actual Test on WinBond: 220-250ms
  //Wait until the operation is completed.
  sleep_ms(140); 
  WaitUntilBusyClear(device_num);
}
  

////////////////////////////////////////////////////////////////////
// Erase everything on chip
//
void EraseEverything() {
  MUTEXLOCK();
  if (flash_type == NOR_WINBOND) {
    EraseSecurityRegister_Winbond(1);
    EraseSecurityRegister_Winbond(2);
    EraseSecurityRegister_Winbond(3);
    EraseContent();    
  } else if (flash_type == NOR_ISSI) {
    EraseSecurityInformationRow_ISSI(0);
    EraseSecurityInformationRow_ISSI(1);
    EraseSecurityInformationRow_ISSI(2);
    EraseSecurityInformationRow_ISSI(3);
    EraseContent();    
  }
  MUTEXUNLOCK();
}

  
////////////////////////////////////////////////////////////////////
// Read one ProDOS block into dest  
//
// Input: Block Location, pointer to destination (512 bytes buffer)
//
// Output: CRC32 of the block data
//
static uint32_t __no_inline_not_in_flash_func(ReadOneBlock)(const blockloc_t blockloc, uint8_t* dest) { 
  uint32_t block_address = blockloc.block_address;
  
  //The lowest 9 bits of block_address should be 0.
  assert( (block_address & 0x1ff) == 0);

  uint8_t msg[6];
  msg[0] = 0x0C; //Fast Read with 4-Byte Address command
  msg[4] = (uint8_t)(block_address); block_address>>=8;
  msg[3] = (uint8_t)(block_address); block_address>>=8; 
  msg[2] = (uint8_t)(block_address); block_address>>=8;
  msg[1] = (uint8_t)(block_address);
  msg[5] = 0;   //Dummy 8-bit
  
  bool success;
  enable_spi0(blockloc.device_num);
  spi_write_blocking(spi0, msg, 6);
  uint32_t crc=ReadFromFlashByDMA(dest,BLOCKSIZE,&success);
  disable_spi0();
  
  //Fall back to non-DMA implementation if ReadFromFlashByDMA() failed.
  if (!success) {
    enable_spi0(blockloc.device_num);
    spi_write_blocking(spi0, msg, 6);
    spi_read_blocking(spi0, REPEATED_TX_DATA, dest, BLOCKSIZE);  
    disable_spi0();
    crc = CRC32Aligned(dest,BLOCKSIZE);  
  }
  
  return crc;
}


////////////////////////////////////////////////////////////////////
// Read a sector (4kB) to destBuffer
//
// Input: Device Number, Sector Address, Destination Buffer
//
// Output: CRC32 of the sector data
//
static uint32_t __no_inline_not_in_flash_func(ReadSector)(const uint device_num,uint32_t sector_address,uint8_t* destBuffer){
  uint8_t msg[6];
  
  //Make sure it aligns at the begining of a sector
  sector_address = sector_address & 0xfffff000; 
  
  msg[0] = 0x0C; //Fast Read with 4-Byte Address
  msg[4] = (uint8_t)(sector_address);  sector_address>>=8;
  msg[3] = (uint8_t)(sector_address);  sector_address>>=8;  
  msg[2] = (uint8_t)(sector_address);  sector_address>>=8;
  msg[1] = (uint8_t)(sector_address);
  msg[5] = 0;   //Dummy 8-bit
  
  bool success;
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 6);
  uint32_t crc=ReadFromFlashByDMA(destBuffer,SECTORSIZE,&success);
  disable_spi0();
  
  //Fall back to non-DMA implementation if ReadFromFlashByDMA() failed.  
  if (!success) {
    enable_spi0(device_num);
    spi_write_blocking(spi0, msg, 6);
    spi_read_blocking(spi0, REPEATED_TX_DATA, destBuffer, SECTORSIZE);  
    disable_spi0();
    crc = CRC32Aligned(destBuffer,SECTORSIZE);  
  }
  
  return crc;
}

////////////////////////////////////////////////////////////////////
// Program one page (256-bytes) to Flash
//
// Input: Device Number, Page Address, Pointer to Source Data (256 Bytes Buffer)
//
// Flash Programming is relative slow. No need to put this function to RAM
//
static void ProgramOnePage(const uint device_num,uint32_t page_address,const uint8_t* src) {
  //The lowest 8 bits of page_address should be 0.
  assert( (page_address & 0xff) == 0);

  uint8_t msg[5];
  msg[0] = 0x12; //Page Program with 4-Byte Address
  msg[4] = (uint8_t)(page_address);  page_address>>=8;
  msg[3] = (uint8_t)(page_address);  page_address>>=8;  
  msg[2] = (uint8_t)(page_address);  page_address>>=8;
  msg[1] = (uint8_t)(page_address);
  
  WriteEnable(device_num);
  enable_spi0(device_num);
  spi_write_blocking(spi0, msg, 5);
  spi_write_blocking(spi0, src, PAGESIZE); //Write actual data
  disable_spi0();
  
  //wait until programming finishes
  //It takes about 0.7-3.5ms
  busy_wait_us_32(300); //At least 50ns delay is needed after erase/write command (CS deselect time)
  WaitUntilBusyClear(device_num);
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
// Input: src_buffer   - The data to be written
//        flash_buffer - The data currently in flash
//
// Note: Data bits in flash can be changed from 1 to 0 only. So,
// an erase is needed if bit already in flash is 0 but the bit 
// to be written is 1.
//
// Output: true if an erase operation is needed.
//
static bool __no_inline_not_in_flash_func(IsEraseNeeded)(const uint8_t* src_buffer, const uint8_t* flash_buffer) {
  const uint32_t* src_data   = (const uint32_t*)src_buffer;   /* Data to be written */
  const uint32_t* flash_data = (const uint32_t*)flash_buffer; /* Data currently in flash */
  
  for(uint i=BLOCKSIZE/4;i!=0;--i) {    
    if (*src_data & ~*flash_data) return true;
    ++src_data;
    ++flash_data;
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
static bool __no_inline_not_in_flash_func(IsEmptyPage)(const uint8_t* src_buffer) {
  const uint32_t* src_data = (const uint32_t*)src_buffer;
  
  for(uint i=PAGESIZE/4;i!=0;--i) {   
    if (*src_data != 0xffffffff) return false;
    ++src_data;
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
static bool IsSector64kErased(const uint device_num, uint32_t address) {
  assert( (address&0xffff)==0);
  bool ret_value = false; //Assume false (Not erased)
  
  //64kB = 16 4kB-Sector
  //Check each 4kB-sector one by one
  for(uint i=0;i<16;++i) {
    //Read one 4kB Sector
    uint32_t crc32=ReadSector(device_num, address, sector_buffer);
    if (crc32 != 0xf154670a) {
      //Checksum not match. It is not erased.
      ret_value = false;
      goto exit;
    }
    address += SECTORSIZE;  //Next Sector Address

    //Check every byte in sector_buffer
    const uint32_t* src_data = (const uint32_t*)sector_buffer;
    for(uint j=SECTORSIZE/4;j!=0;--j) {   
      if (*src_data != 0xffffffff) {
        ret_value = false;
        goto exit;
      }
      ++src_data;
    }
  }
  ret_value = true;
  
exit:  
  return ret_value;
}


////////////////////////////////////////////////////////////////////
// Write one ProDOS block with erase operation
//
// Input: blockloc   - Location of the block in flash
//        src_buffer - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
// Flash Erase is relative slow. No need to put this function to RAM
static bool WriteOneBlockWithErase(const blockloc_t blockloc, const uint8_t* src_buffer) {
  //
  //Step 1: Read the entire 4kB sector to sector_buffer
  ReadSector(blockloc.device_num, blockloc.block_address, sector_buffer); 
  
  //
  //Step 2: Copy data to be written to sector_buffer in Background by DMA
  const uint32_t page_offset = blockloc.block_address & 0xfff;
  CopyMemoryAlignedBG(sector_buffer+page_offset, src_buffer, BLOCKSIZE); 
  
  //
  //Step 3: Erase entire sector in flash while data is being copied by DMA
  EraseSector(blockloc.device_num, blockloc.block_address);
  DMAWaitFinish();    //make sure step 2 is complete    
  
  //
  //Step 4: Calculate the CRC32 of sector_buffer in Background by DMA
  SetCRC32Seed(GetMemoryDMAChannel(),DEFAULT_CRC32_SEED);
  CopyMemoryAlignedBG(sector_buffer,sector_buffer,SECTORSIZE);
  
  //
  //Step 5: Program page by page
  uint32_t current_address = blockloc.block_address & 0xfffff000; //Align to the begining of the sector
  uint8_t* src_data = sector_buffer;
  for(uint i=PAGEPERSECTOR;i!=0;--i) {      
    if (!IsEmptyPage(src_data)) {
      ProgramOnePage(blockloc.device_num, current_address, src_data);
    }
    current_address += PAGESIZE;
    src_data += PAGESIZE;
  }
  
  //
  //Step 6: Verify the written data
  DMAWaitFinish();  //make sure step 4 is finished
  uint32_t crc1=GetCRC();
  uint32_t crc2=ReadSector(blockloc.device_num, blockloc.block_address, sector_buffer); 
  
  return (crc1==crc2);
}



////////////////////////////////////////////////////////////////////
// Execute Write Block command without erase operation
//
// Input: blockloc    - Location of the block in flash
//        src_buffer  - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
static bool __no_inline_not_in_flash_func(WriteOneBlockWithoutErase)(const blockloc_t blockloc, const uint8_t* src_buffer) {
  //
  //Step 1: Calculate the CRC32 of the data in src_buffer in Background by DMA
  SetCRC32Seed(GetMemoryDMAChannel(),DEFAULT_CRC32_SEED);
  CopyMemoryAlignedBG((uint8_t*)src_buffer,src_buffer,BLOCKSIZE);
  
  //
  //Step 2: Program the data to flash 
  if (!IsEmptyPage(src_buffer)) {
    ProgramOnePage(blockloc.device_num, blockloc.block_address, src_buffer);  
  }
  if (!IsEmptyPage(src_buffer+PAGESIZE)) {
    ProgramOnePage(blockloc.device_num, blockloc.block_address+PAGESIZE, src_buffer+PAGESIZE);  
  }
  
  //Step 3: Read the result of step 1
  DMAWaitFinish();
  const uint32_t crc1=GetCRC();
  
  //Step 4: Verify the data
  const uint32_t crc2=ReadOneBlock(blockloc, sector_buffer); //Use sector_buffer as temporary buffer
  
  return (crc1==crc2);
}

////////////////////////////////////////////////////////////////////
// Write one ProDOS block from srcBuffer to flash
//
// Input: blockloc    - Location of the block in flash
//        src_buffer  - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
static bool __no_inline_not_in_flash_func(WriteOneBlock)(const blockloc_t blockloc, const uint8_t* src_buffer) {
    //
    //Step 1: Read the block from Flash to sector_buffer;
    ReadOneBlock(blockloc, sector_buffer);
    
    //
    //Step 2: Is the data in flash identical to the data to be written?
    if (VerifyOneBlock(src_buffer, sector_buffer)) { 
      return true;
    }
    
    //
    //Step 3: Dispatch to WriteOneBlockWithErase or WriteOneBlockWithoutErase
    if (IsEraseNeeded(src_buffer, sector_buffer)) {  
      return WriteOneBlockWithErase(blockloc,src_buffer);
    } else {
      return WriteOneBlockWithoutErase(blockloc,src_buffer);   
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
uint32_t ReadJEDECID(const uint device_num) {
  //Command + 3 Bytes Result
  uint8_t txbuffer[4]={0x9f}; 
  uint8_t rxbuffer[4];
  
  MUTEXLOCK();
  enable_spi0(device_num);
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
static uint64_t ReadUniqueID_Winbond(const uint device_num) {
  assert(flash_type == NOR_WINBOND);  //Winbond only command      
  //2-Bytes Padding + Command + 5 Dummy Bytes + 8-Bytes Result
  uint8_t __attribute__((aligned(8))) txbuffer[16]={0,0,0x4b}; 
  uint8_t __attribute__((aligned(8))) rxbuffer[16];
  //2-bytes padding so that the result is 64-bit aligned
  
  MUTEXLOCK();
  enable_spi0(device_num);
  spi_write_read_blocking(spi0,txbuffer+2,rxbuffer+2,14);
  disable_spi0();
  MUTEXUNLOCK();
  
  uint64_t id = *(uint64_t*)(rxbuffer+8);
  
  return __builtin_bswap64(id);  //Endian Conversion
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
static uint64_t ReadUniqueID_ISSI(const uint device_num) {
  assert(flash_type == NOR_ISSI);  //ISSI only command      
  //3-Bytes Padding + Command + 3 Bytes Address + 1 Dummy Byte + 8-Bytes Result
  uint8_t __attribute__((aligned(8))) txbuffer[16]={0,0,0,0x4b,0,0,0,0}; 
  uint8_t __attribute__((aligned(8))) rxbuffer[16]; 
  //3-bytes padding so that the result is 64-bit aligned
  
  MUTEXLOCK();
  enable_spi0(device_num);
  spi_write_read_blocking(spi0,txbuffer+3,rxbuffer+3,13);
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
  if (flash_type==NOR_WINBOND) return ReadUniqueID_Winbond(DEVICE0);
  else if (flash_type == NOR_ISSI) return ReadUniqueID_ISSI(DEVICE0);
  else return 0;
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


//return type of ChipIDToFlashInfo()
typedef struct {
  uint32_t capacity_mb;
  flash_type_t flash_type;
} flash_info_t;

/**
 *  \brief Convert supported Flash chip ID to capacity in MB and flash type
 *  
 *  \param [in] id Flash JEDECID
 *  \return flash_info_t struct
 *  
 *  \details capacity_mb is 0 if the chip ID is not supported.
 */
static flash_info_t ChipIDToFlashInfo(const uint32_t id){
  uint32_t capacity = 0;
  flash_type_t flash_type = NOR_WINBOND; //Default to NOR_WINBOND
  
  if (id==0xef4021) capacity = 128;         //Winbond W25Q01JV
  else if (id==0xef7021) capacity = 128;    //Winbond W25Q01JV-DTR
  else if (id==0xef4020) capacity = 64;     //Winbond W25Q512JV
  else if (id==0xef7020) capacity = 64;     //Winbond W25Q512JV-DTR  
  else if (id==0xef7022) capacity = 256;    //Winbond W25Q02JV-DTR
  else if (id==0x204020) capacity = 64;     //Alliance AS25F3512MQ, Compatible with WinBond NOR chip
  else if (id==0x9d6020)  { //ISSI IS25LP512MG 
    flash_type = NOR_ISSI;
    capacity = 64;
  }else if (id==0x9d6021) { //ISSI IS25LP01GJ 
    flash_type = NOR_ISSI;
    capacity = 128;
  }else if (id==0x9d6022) { //ISSI IS25LP02GJ 
    flash_type = NOR_ISSI;
    capacity = 256;
  }        

  return (flash_info_t){.capacity_mb = capacity, .flash_type = flash_type};
}



/////////////////////////////////////////////////////////////////////////////////////////
// Read data from flash by DMA
// Replace spi_read_blocking routine()
//
// Input: dest_buffer - Destination Buffer
//        len - Number of Bytes
//
// Output: CRC32 of the data read from flash
//         success_out: true/false
//
//ReadFromFlashByDMA() occasionally fails if the CPU is overclocked to 225MHz and SPI
//is running at 75MHz. The problem is RX DMA does not finish. Setting DMA priority,
//bus priority and increasing core voltage do not help.
//The problem occurs only during TFTP file transfer. A disk-to-disk copy with Copy II plus
//is performed. But there was no such problem.
//It usually happens less than 6 times during the entire file transfer. 
//So, DMA failure rate < 0.01%.
//
//The workaround is to set a timeout. If the RX DMA does not finish, the DMA is aborted
//and this function returns false to success_out. The caller then falls back to non-DMA
//implementation.
static uint32_t __no_inline_not_in_flash_func(ReadFromFlashByDMA)(uint8_t *dest_buffer,const uint32_t len,bool* success_out) {
  static bool already_configured = false;
  static int rx_channel;
  static dma_channel_config_t rx_config;
  static uint32_t dma_timeout;

  if (!already_configured) {
    already_configured = true;
    
    //DMA Channel Number
    rx_channel = dma_claim_unused_channel(true);    
    
    //RX DMA Config
    rx_config = dma_channel_get_default_config(rx_channel);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_dreq(&rx_config,spi_get_dreq(spi0,false));  // false = rx dreq
    channel_config_set_write_increment(&rx_config, true); 
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_sniff_enable(&rx_config, true);
    
    //Calculate dma_timeout value
    //The time to complete DMA after TX loop has completed.
    //SPI 25MHz: 3us
    //SPI 75MHz: 1us
    //
    //dma_timeout is set to 3us if baud>=50MHz
    //Otherwise, dma_timeout = 6us * 25MHz / current_spi_baud
    const uint32_t baud = spi_get_baudrate(spi0);
    if (baud>=50000000ul) dma_timeout = 3;
    else dma_timeout = 6 * 25000000ul / baud;
  }

  //RX DMA Channel
  dma_channel_configure(rx_channel,&rx_config,
                        dest_buffer,            //destination
                        &spi_get_hw(spi0)->dr,  //source
                        len,
                        false);                 //Don't start
  SetCRC32Seed(rx_channel,DEFAULT_CRC32_SEED);

  //start RX dma 
  dma_start_channel_mask(1u<<rx_channel);
  
  //TX with software loop
  for (size_t i = 0; i < len; ++i) {
    while (!spi_is_writable(spi0))
        tight_loop_contents();
    spi_get_hw(spi0)->dr = REPEATED_TX_DATA;
  }
  //All TX data has been put into SPI FIFO
  //RX DMA should finishes in a few us
  
  //Assume success
  *success_out = true;  
  
  //Wait until rx_channel finishes or timeout
  const uint32_t start_time = time_us_32();
  do{
    if (!dma_channel_is_busy(rx_channel)) {
      //RX DMA finished!
      // Don't leave overrun flag set
      spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;      
      return GetCRC();
    }
  }while((time_us_32()-start_time) < dma_timeout);
  
  //
  //timeout - DMA failed
  //Abort DMA and clean up
  //
  *success_out = false;
  dma_channel_abort(rx_channel);
  
  //Drain SPI RX FIFO
  while (spi_is_readable(spi0))
      (void)spi_get_hw(spi0)->dr;  
  
  // Don't leave overrun flag set
  spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;  

  //DEBUG Only. Output to UART
  INFO_PRINTF("%c",len>512?'!':'@');

  return 0;
}


////////////////////////////////////////////////////////////////////
// Initalize Flash related data and flash chips
//
void InitFlash() {
  flash_size0 = 0;
  unit_count_flash0 = 0;
  flash_size1 = 0;
  unit_count_flash1 = 0;

  //Init Flash chip #0
  uint32_t id = ReadJEDECID(DEVICE0);
  flash_info_t info = ChipIDToFlashInfo(id);
  if (info.capacity_mb == 0) {
    //Single Flash chip in Flash #1 is not supported.
    //So, if flash chip #0 is not present, do not set SPI speed to SPI_SPEED_FINAL
    return;     
  }
  flash_type = info.flash_type;
  flash_size0 = info.capacity_mb;
  unit_count_flash0 = info.capacity_mb / SIZEPERUNIT_MB;
  SetFlashDriveStrength(DEVICE0);
  Enable4BytesAddressing(DEVICE0);

  //Init Flash Chip #1
  id = ReadJEDECID(DEVICE1);
  info = ChipIDToFlashInfo(id);
  //Both chips must be same type
  if (info.capacity_mb != 0 && info.flash_type == flash_type) {
    flash_size1 = info.capacity_mb;
    unit_count_flash1 = info.capacity_mb / SIZEPERUNIT_MB;
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
  return unit_count_flash0+unit_count_flash1;
}


////////////////////////////////////////////////////////////////////
// Get the total number of block of the unit reported to Prodos
// Assume unitNum is valid
//
// Input: ProDOS unit number (1-N)
//
// Output: Total Number of blocks of the unit
//
uint32_t GetBlockCountFlash(const uint uint_num) {
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
uint32_t GetBlockCountFlashActual(const uint unit_num) {
  //Blocks per unit is hard-coded in current implementation
  return BLOCKSPERUNIT_ACTUAL;
}



/////////////////////////////////////////////////////////////
// Get DIB (Device Information Block) of a unit
// Assume unit_num is valid.
//
// Input: unit_num    - Unit Number (1-N)
//        dest_buffer - Pointer to destination buffer
//
void GetDIBFlash(const uint unit_num, uint8_t *dest_buffer) {
  //ID String padded to 16 bytes long
  #define IDSTR "MEGAFLASH DRV N "
  #define IDSTRLEN        15
  #define IDSTR_DN_OFFSET 14  //Offset to Drive Number Char  
  
  assert(sizeof(struct dib_t)==25);
  struct dib_t *dib = (struct dib_t*)dest_buffer;  
  
  //Device Status Byte  
  dib->devicestatus = 0b11111000;          
  
  //Block Count
  uint32_t blockSize = GetBlockCountFlash(unit_num);  
  dib->blocksize_l  = (uint8_t)blockSize; blockSize>>=8; 
  dib->blocksize_m  = (uint8_t)blockSize; blockSize>>=8;                    
  dib->blocksize_h  = (uint8_t)blockSize;
    
  //ID String    
  assert(strlen(IDSTR)==16);              
  assert(IDSTRLEN<=16);
  dib->idstrlen = IDSTRLEN;
  memcpy(dib->idstr,IDSTR,16);
  dib->idstr[IDSTR_DN_OFFSET] = '0'+unit_num;
  
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
// Input: dest_buffer - Pointer to destination buffer
//        src_buffer  - Pointer to source buffer
//        len         - Number of bytes to be copied
//
// Note: The pointers must be 32-bit aligned. and len must be
//       multiple of 4.
//
//       It takes 7us to copy 512 Bytes on 150MHz RP2350
//
static void __no_inline_not_in_flash_func(CopyBitInversion)(uint8_t* dest_buffer,const uint8_t* src_buffer,uint32_t len) {
  assert(len%4==0);
  uint32_t* dest = (uint32_t*) dest_buffer;
  uint32_t* src  = (uint32_t*) src_buffer;
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
rwerror_t __no_inline_not_in_flash_func(ReadBlockFlash)(const uint unit_num, const uint block_num, uint8_t* dest_buffer) {
  const blockloc_t blockloc = GetBlockLoc(unit_num, block_num);
  
  MUTEXLOCK();  
  ReadOneBlock(blockloc, dest_buffer);                  //Read from flash
  CopyBitInversion(dest_buffer,dest_buffer,BLOCKSIZE);  //Bit invert in-place
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
rwerror_t __no_inline_not_in_flash_func(WriteBlockFlash)(const uint unit_num, const uint block_num, const uint8_t* src_buffer){
  const blockloc_t blockloc = GetBlockLoc(unit_num, block_num);
  
  MUTEXLOCK();   
  uint8_t __attribute__((aligned(4))) temp_write_buffer[BLOCKSIZE];  
  CopyBitInversion(temp_write_buffer,src_buffer,BLOCKSIZE);
  bool success = WriteOneBlock(blockloc, temp_write_buffer);
  MUTEXUNLOCK();

  return success? SP_NOERR : SP_IOERR;
}

////////////////////////////////////////////////////////////////////
// Write Block to Flash for Disk Image Transfer
// Assume unit_num and blockNum are valid.
//
// Input: Unit Number, Block Number
//        src_buffer    - Data to be written (512 Bytes)
//
// Output: true if write operation is successful
//
// This routine assumes the existing data in entire the ProDOS unit 
// can be erased. It erases the flash chip in 64kB chunk and does not
// preserve existing data
//
bool WriteBlockFlashForImageTransfer(const uint unit_num, const uint block_num, const uint8_t* src_buffer){
  bool success;
  const blockloc_t blockloc = GetBlockLoc(unit_num,block_num);

  MUTEXLOCK();
  //Erase 64kB sector every 16 blocks and block number <8192
  if (block_num<8192 && block_num%16 == 0) {
    assert( (blockloc.block_address&0xffff) == 0);  //Block Address should be 64k-aligned
      
    if (!IsSector64kErased(blockloc.device_num, blockloc.block_address)) {
      EraseSector64k(blockloc.device_num,blockloc.block_address);
    }
  }

  //Program the block to flash
  uint8_t __attribute__((aligned(4))) temp_write_buffer[BLOCKSIZE];  
  CopyBitInversion(temp_write_buffer,src_buffer,BLOCKSIZE);
  success = WriteOneBlockWithoutErase(blockloc,temp_write_buffer);

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
// calls AbortEraseFlashDisk() and abort_erase_flash_disk variable
// is set. Then, the process is aborted.
//
static volatile bool abort_erase_flash_disk;
void AbortEraseFlashDisk() {
  abort_erase_flash_disk = true;
}

////////////////////////////////////////////////////////////////////
// Erase entire unit
// Assume unit_num is valid.
//
// Input: Unit Number
//
void EraseFlashDisk(const uint unit_num){
  MUTEXLOCK();
  abort_erase_flash_disk = false;  //reset the flag

  //Erase 64kB sector every 16 blocks and block number <8192
  for(uint block_num=0;block_num<8192;block_num+=16) {
    blockloc_t blockloc = GetBlockLoc(unit_num,block_num);

    if (abort_erase_flash_disk) break;
    
    if (!IsSector64kErased(blockloc.device_num, blockloc.block_address)) {
      if (abort_erase_flash_disk) break;
      EraseSector64k(blockloc.device_num,blockloc.block_address);
    }
  }
  MUTEXUNLOCK();
}

