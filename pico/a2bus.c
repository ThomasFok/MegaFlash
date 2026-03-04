#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "a2bus.h"

/////////////////////////////////////////////////////////////////
// Initialize PIO Program

#ifdef PICO_RP2040
//RP2040 Implementation
void InitPIO() {
  //Get CPU clock speed
  const uint32_t sys_clk_hz = clock_get_hz(clk_sys);
  
  //Add PIO program to PIO instruction memory
  uint offset = pio_add_program(pio0, &a2bus_program);

  //Initialize all 4 state machines
  for(uint sm=0;sm<4;++sm) {
    //Initialize the program. The function is defined in .pio file
    a2bus_program_init(pio0, sm, offset, sys_clk_hz);
    
    //Start running PIO program
    pio_sm_set_enabled(pio0, sm, true /*=run*/);

    //Tell the state machine its ID with RnW bit set to 1
    pio_sm_put(pio0, sm, sm | 0b100);
    
    //Initialize MegaFlash registers to zero
    pio_sm_put(pio0, sm, 0x00);
  } 
  
  //Initialize SM0 as early as possible
  //Read the notes in busloop_wa.c
  pio_sm_put(pio0, 0 /*sm0*/, 0x00f0f000);  
}
#else
//RP2350 Implementation
void InitPIO() {
  //Get CPU clock speed  
  const uint32_t sys_clk_hz = clock_get_hz(clk_sys);

  //////////////////////////////////////////////////////////////////////////////////
  //State Machine SM_LISTENER
  
  //Initialize GPIO pin used by PIO
  a2bus_gpio_init(pio0);
  
  //Add PIO program to PIO instruction memory
  uint offset = pio_add_program(pio0, &a2buslistener_program);

  //Initialize the program. The function is defined in .pio file
  a2buslistener_program_init(pio0, SM_LISTENER, offset,sys_clk_hz);
    
  //Start running PIO program
  pio_sm_set_enabled(pio0, SM_LISTENER, true /*=run*/);

  //////////////////////////////////////////////////////////////////////////////////
  //State Machine SM_A2BUS
  
  //Add PIO program to PIO instruction memory
  offset = pio_add_program(pio0, &a2bus_program);

  //Initialize the program. The function is defined in .pio file
  a2bus_program_init(pio0, SM_A2BUS, offset,sys_clk_hz);
    
  //Start running PIO program
  pio_sm_set_enabled(pio0, SM_A2BUS, true /*=run*/); 
}  

#endif