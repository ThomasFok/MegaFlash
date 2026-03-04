#include "a2bus.h"
#include "busloop_wa.h"

//---------------------------------------------------------------------
//After Power on reset, MegaFlash is in Slinky emulation mode
//MegaFlash can be switched to native mode by reading the following 
//addresses in sequence.
//
//$C0C2
//$C0C0
//$C0C0
//$C0C3
//$C0C1
//
//Then, this function returns and MegaFlash switches to native mode.
//
//Note: The stock firmware has serious bugs in testsize and makecat
//routines. Both routines use the slinky address registers as loop
//counters. The problem is if there is no card, those registers
//do not exist.  The code can get out of the loop because the floating
//bus can have any random value. By chance, the loop exits.
//
//After studying the firmware code, it is found that the following
//criteria is required to avoid dead-loop. 
//  $C0C0 == 0
//  Upper Nibble of $C0C1 != 0
//  Lower Nibble of $C0C2 == 0
//
//Also, the boot code try to load block 0 to $800. If address $801 is not 0,
//the code assumes a valid boot block is loaded and execute the code at $801.
//Thus, data register $C0C3 should be initalize to 0
//
//So, we initialize slinky registers to 0x00f0f000.
//---------------------------------------------------------------------

/**********************************************************************

RP2040 does not have enough RAM to emulate a Skinly RAM card.
This bus loop simply waits for the Magic sequence to switch to
native mode.

**********************************************************************/


// No need to put this function to RAM since
// we don't rush to write any results back to MegaFlash I/O
// registers.
void BusLoopWaitActiviation() {
  const uint32_t REGINITVAL = 0x00f0f000;
  
  //Initalize Slinky Registers
  UpdateMegaFlashRegisters(0,REGINITVAL);
  
  enum {
    STATENULL,
    STATE2,   //$C0C2 accessed
    STATE20,  //$C0C2, $C0C0 accessed
    STATE200, //and so on
    STATE2003,
    STATE20031
  } state = STATENULL;
    
  const uint READFLAG = (1<<4); //Read flag is at bit 4
  do {
    //8-bit data from Apple + RnW Flag + 4-bit address from Apple
    uint32_t busdata = GetAppleBusBlocking();
    uint32_t addr = busdata & 0b1111;     //Lower nibble of Apple Address
    uint32_t data = (busdata >>5) & 0xff; //8-bit data from Apple
    
    if (busdata & READFLAG) {
      //6502 is reading from us

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
    } else {
        //6502 is writing to us
        state = STATENULL;
    }   
  } while(state!=STATE20031); 
}

