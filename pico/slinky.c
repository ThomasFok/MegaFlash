#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "defines.h"
#include "a2bus.h"
#include "ramdisk.h"
#include "slinky.h"

/**********************************************************************
This module emulates a Slinky Card using internal RAM.

On RP2040, the size of Slinky is 128kB. But the minimum size of actual 
Slinky is 256kB. The /RAM4 drive is not created when booting from ProDOS.

On RP2350, the size is 256kB.

The slinky data buffer is shared with RAM Disk.

************************************************************************/



//--------------------------------------------------------------
//The definitions below must be the same as the ones in a2bus.c
extern union {
  uint8_t  r[16];     //Individual 8-bit registers
  uint32_t i32[4];    //Chunks of 4 32-bit registers
} registers;
//--------------------------------------------------------------



void SlinkyInit() {
  assert(SLINKY_SIZE<=GetRamdiskSize());  //Make sure slinky size fits in RAM Disk Data Buffer
  const uint32_t initvalue = 0x00f00000;
  registers.i32[0] = initvalue;
  UpdateMegaFlashRegisters(0,initvalue);
  UpdateMegaFlashRegisters(1,initvalue);
  UpdateMegaFlashRegisters(2,initvalue);
  UpdateMegaFlashRegisters(3,initvalue);  
  EraseRamdiskQuick(); 
  //We don't format and provide a boot block on the Slinky RamDisk so that
  //it behaves like a real slinky card.
  //The stock firmware creates the root directory structure on Power Up
  //But there is no boot block. So, it is not bootable unless the RamDisk
  //is formatted by Utility like Copy II Plus.  
}

//In Apple IIc Memory Expansion Card, address bus a3,a2 are
//not connected. So, $C0C0, $C0C4, $C0C8 and $C0CC are the
//same. We try to implement the same behaviour.
void __no_inline_not_in_flash_func(BusLoopSlinky)() {
  const uint32_t READFLAG = (1<<4);
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
  } state = STATENULL;
  
  //Slinky Data Buffer
  uint8_t* slinky_data = GetRamdiskDataPointer();
  
  do {
    uint32_t busdata = GetAppleBusBlocking();    
    uint32_t addr = busdata & 0b0011;     //Ignore A3-A2
    uint32_t data = (busdata >>5) & 0xff;

    if (busdata & READFLAG) {
      //6502 is reading from us   
      
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
          if (addr==1) state=STATE20031;
          else if (addr==2) state=STATE2;
          else state=STATENULL;
          break;          
        default:
          state = STATENULL;
      }
      
      //Slinky
      if (addr == 3) {
        slinky_addr.val = (slinky_addr.val+1) & 0xfffff;  //Address is 20-bit
      } else {
        continue; //No need to update MegaFlash registers if addr is not 3.
      }
    } else {
      //6502 is writing to us
      state = STATENULL;
      
      switch(addr) {
        case 0:
          slinky_addr.byte[0] = data;
          break;
        case 1:
          slinky_addr.byte[1] = data;
          break;
        case 2:
          slinky_addr.byte[2] = data & 0x0f;    //Higher Nibble is ignored.
          break;
        case 3:
          if (slinky_addr.val < SLINKY_SIZE && slinky_data!=NULL) slinky_data[slinky_addr.val] = data;
          slinky_addr.val = (slinky_addr.val+1) & 0xfffff;  //Address is 20-bit
          break;    
        default:
          continue; //No need to update MegaFlash registers if addr is not 0-3.
      }
    }
    //Update MegaFlash Registers
    uint32_t val = (slinky_addr.val < SLINKY_SIZE && slinky_data!=NULL) ? (slinky_data[slinky_addr.val]<<24) : (0xff<<24);
    val = val | 0xf00000 | slinky_addr.val;
    
    registers.i32[0] = val;
    
    //When 6502 is reading the data register,
    //the address is increased by 1 automatically.
    //The data pointed by the new address have to
    //be sent to PIO.
    //But it should happen after the PIO has completed
    //the current 6502 read request.
    //
    //The irq 0 flag of PIO is to indicate that 
    //PIO has not yet pulled the output value during
    //a read cycle (Pico->6502).
    //
    //So, if rx fifo is empty AND irq 0 is set,
    //it means the current read cycle has not completed,
    //we must not update registers until irq 0 is cleared
    //by the state machine.
#ifndef PICO_RP2040    
    if (pio_sm_is_rx_fifo_empty(pio0, SM_LISTENER)) {
      while(pio_interrupt_get(pio0,0 /*= irq 0*/)) {
        tight_loop_contents(); //Wait until irq 0 is cleared
      }
    }
#endif    

    //Update all registers so that A3,A2 address lines are ignored.
    UpdateMegaFlashRegisters(0,val);
    UpdateMegaFlashRegisters(1,val);
    UpdateMegaFlashRegisters(2,val);
    UpdateMegaFlashRegisters(3,val);    
  }while(state!=STATE20031);
  
  //Activation Sequence Detected.
  //exit the function
}