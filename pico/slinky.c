#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "defines.h"
#include "a2bus.h"
#include "ramdisk.h"
#include "slinky.h"

/**********************************************************************
This module emulates a Slinky Card using internal RAM.

RP2040 only has 256kB of RAM. So, the size of Slinky is limited to 128kB.
Since the minimum size of an acutal Slinky is 256kB, a 128kB Slinky may 
cause compatibility problem. For example, the /RAM4 drive is not created
when booting from ProDOS. So, currently, Slinky is disabled on RP2040

On RP2350, the size is 256kB.

The slinky data buffer is shared with RAM Disk.

************************************************************************/


////////////////////////////////////////////////////////////////////
//Initialize Slinky RAM Disk
//
void SlinkyInit() {
  assert(SLINKY_SIZE<=GetRamdiskSize());  //Make sure slinky size fits in RAM Disk Data Buffer
  EraseRamdiskQuick();  //It erases the first 3 blocks of Ramdisk. 
  
  //We don't format and provide a boot block on the Slinky RamDisk so that
  //it behaves like a real slinky card.
  //The stock firmware creates the root directory structure on Power Up
  //But there is no boot block. So, it is not bootable unless the RamDisk
  //is formatted by Utility like Copy II Plus.  
}

///////////////////////////////////////////////////////////////////
//In Apple IIc Memory Expansion Card, address bus a3,a2 are
//not connected. So, $C0C0, $C0C4, $C0C8 and $C0CC are the
//same. We try to implement the same behaviour.
void __no_inline_not_in_flash_func(BusLoopSlinky)() {
  const uint32_t READFLAG = (1<<4);
  //To store current slinky address
  union {
    uint8_t  byte[4];
    uint32_t val;
  } slinky_addr;
  slinky_addr.val = 0;
  
  //Activation States
  enum {
    STATENULL,
    STATE2,   //$C0C2 accessed
    STATE20,  //$C0C2, $C0C0 accessed
    STATE200, //and so on
    STATE2003,
    STATE20031
  } state = STATENULL;  //set initial state to STATENULL
  
  //Slinky Data Buffer
  uint8_t* slinky_data = GetRamdiskDataPointer();

  //Indicates the first run of do-while loop
  bool firstRun = true;
  
  do {
    //Jump to update_register to initialize Slinky addresses and data registers ($C0C0-$C0C3)
    if (firstRun) {
      firstRun = false;
      goto update_registers;
    }
    
    const uint32_t busdata = GetAppleBusBlocking();    
    const uint32_t addr = busdata & 0b0011;     //Ignore A3-A2
    const uint32_t data = (busdata >>5) & 0xff;

    if (busdata & READFLAG) {
      //
      //6502 is reading from us   
      //
      
      //Activation
      switch(state) {
        case STATENULL:
          if (addr==2) state=STATE2;
          break;
        case STATE2:
          if (addr==0) state=STATE20;
          else if (addr==2) state=STATE2;
          else state=STATENULL;
          break;
        case STATE20:
          if (addr==0) state=STATE200;
          else if (addr==2) state=STATE2;
          else state=STATENULL;
          break;
        case STATE200:
          if (addr==3) state=STATE2003;
          else if (addr==2) state=STATE2;
          else state=STATENULL;
          break;
        case STATE2003:
          if (addr==1) {
            state=STATE20031; //Activation sequence detected
            continue;         //jump to end of do-while loop and exit the function
          }
          else if (addr==2) state=STATE2;
          else state=STATENULL;
          break;          
        default:
          state = STATENULL;
      }
      
      //Handle Slinky
      if (addr == 3) {
        //add 1 to address when 6502 read data from us
        slinky_addr.val = (slinky_addr.val+1) & 0xfffff;  //Address is 20-bit
      } else {
        continue; //No need to update MegaFlash registers if addr is not 3.
      }
    } else {
      //
      //6502 is writing to us
      //
      
      //Reset activation state
      state = STATENULL;
      
      //Handle Slinky
      switch(addr) {
        case 0: //address low byte
          slinky_addr.byte[0] = data;
          break;
        case 1: //address mid byte
          slinky_addr.byte[1] = data;
          break;
        case 2: //address high byte
          slinky_addr.byte[2] = data & 0x0f;    //Higher Nibble is ignored.
          break;
        case 3: //data register
          if (slinky_addr.val < SLINKY_SIZE && slinky_data!=NULL) slinky_data[slinky_addr.val] = data;
          slinky_addr.val = (slinky_addr.val+1) & 0xfffff;  //Address is 20-bit
          break;    
        default:
          continue; //No need to update MegaFlash registers if addr is not 0-3.
      }
    }
    
  //
  //Update MegaFlash Registers  
  //
  update_registers:;
    
    uint32_t newval = (slinky_addr.val < SLINKY_SIZE && slinky_data!=NULL) ? (slinky_data[slinky_addr.val]<<24) : (0xff<<24); //data register value
    newval |=  0xf00000 | slinky_addr.val; //address registers. High nibble of $C0C2 must be $F
    
    //Update all registers so that A3,A2 address lines are ignored.
    UpdateMegaFlashRegisters(0,newval);
    UpdateMegaFlashRegisters(1,newval);
    UpdateMegaFlashRegisters(2,newval);
    UpdateMegaFlashRegisters(3,newval);    
  }while(state!=STATE20031);
  
  //Activation Sequence Detected.
  //exit the function
}