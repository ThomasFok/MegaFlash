#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "a2bus.h"

/////////////////////////////////////////////////////////////////
// Initialize PIO Program

//instruction memory offset of a2buslistener
#define INVALIDOFFSET (-1)
#ifndef NDEBUG
static uint a2buslistener_program_offset = INVALIDOFFSET;
#else
static uint a2buslistener_program_offset;
#endif


////////////////////////////////////////////////////////////////////
// Patch an PIO program instruction to NOP
//
// Input: pio - PIO to be patched
//        offset - the instruction memory offset of first instruction of the program
//                 It should be the value returned by pio_add_program()
//        linenum  - The line number of the instruction.
//                   e.g. The first instruction is at line 0
//
static inline void PatchInstToNOP(const PIO pio,const uint offset,const uint linenum) {
  assert(offset != INVALIDOFFSET);
  assert(offset+linenum < 32);
  pio->instr_mem[offset+linenum] = pio_encode_nop();
}

////////////////////////////////////////////////////////////////////
// Restore an PIO program instruction to original value
//
// Input: pio      - PIO to be patched
//        *program - point to the PIO program
//        offset   - the instruction memory offset of first instruction of the program
//                   It should be the value returned by pio_add_program()
//        linenum  - The line number of the instruction.
//                   e.g. The first instruction is at line 0
//
static inline void RestoreInst(const PIO pio,const pio_program_t *program,const uint offset,const uint linenum) {
  assert(offset != INVALIDOFFSET); 
  assert(offset+linenum < 32);
  uint16_t instr = program->instructions[linenum];
  pio->instr_mem[offset+linenum] = pio_instr_bits_jmp != _pio_major_instr_bits(instr) ? instr : instr + offset;
}

////////////////////////////////////////////////////////////////////
// Switch the PIO program to run in Slinky mode 
// i.e. report reading from address $C0C0 to CPU
//
// Assume the program is not running
static inline void PioSwitchToSlinkyMode() {
  PatchInstToNOP(PIO_A2BUS,a2buslistener_program_offset,get_patch_linenum());
}

////////////////////////////////////////////////////////////////////
// Switch the PIO program to run in Native mode 
// i.e. Do not report reading from address $C0C0 to CPU
//
void PioSwitchToNativeMode() {
#ifdef PICO_RP2040
  RestoreInst(PIO_A2BUS,&a2bus_program,a2buslistener_program_offset,get_patch_linenum());
#else  
  RestoreInst(PIO_A2BUS,&a2buslistener_program,a2buslistener_program_offset,get_patch_linenum());
#endif
}


#ifdef PICO_RP2040
//
//RP2040 Implementation
//
void InitPIO() {
  //Get CPU clock speed
  const uint32_t sys_clk_hz = clock_get_hz(clk_sys);
  
  //Add PIO program to PIO instruction memory
  a2buslistener_program_offset = pio_add_program(PIO_A2BUS, &a2bus_program);
  
  //Patch the program to run in Slinky mode
  PioSwitchToSlinkyMode();

  //Initialize all 4 state machines
  for(uint sm=0;sm<4;++sm) {
    //Initialize the program. The function is defined in .pio file
    a2bus_program_init(PIO_A2BUS, sm, a2buslistener_program_offset, sys_clk_hz);
    
    //Start running PIO program
    pio_sm_set_enabled(PIO_A2BUS, sm, true /*=run*/);

    //Tell the state machine its ID with RnW bit set to 1
    pio_sm_put(PIO_A2BUS, sm, sm | 0b100);
    
    //Initialize MegaFlash registers to zero
    pio_sm_put(PIO_A2BUS, sm, 0x00);
  } 
}
#else
//
//RP2350 Implementation
//
void InitPIO() {
  //Get CPU clock speed  
  const uint32_t sys_clk_hz = clock_get_hz(clk_sys);

  //////////////////////////////////////////////////////////////////////////////////
  //State Machine SM_LISTENER
  
  //Initialize GPIO pin used by PIO
  a2bus_gpio_init(PIO_A2BUS);
  
  //Add PIO program to PIO instruction memory
  a2buslistener_program_offset = pio_add_program(PIO_A2BUS, &a2buslistener_program);
  
  //Patch the program to run in Slinky mode
  PioSwitchToSlinkyMode();

  //Initialize the program. The function is defined in .pio file
  a2buslistener_program_init(PIO_A2BUS, SM_LISTENER, a2buslistener_program_offset, sys_clk_hz);
    
  //Start running PIO program
  pio_sm_set_enabled(PIO_A2BUS, SM_LISTENER, true /*=run*/);

  //////////////////////////////////////////////////////////////////////////////////
  //State Machine SM_A2BUS
  
  //Add PIO program to PIO instruction memory
  uint offset = pio_add_program(PIO_A2BUS, &a2bus_program);

  //Initialize the program. The function is defined in .pio file
  a2bus_program_init(PIO_A2BUS, SM_A2BUS, offset,sys_clk_hz);
    
  //Start running PIO program
  pio_sm_set_enabled(PIO_A2BUS, SM_A2BUS, true /*=run*/); 
}  

#endif