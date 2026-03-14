#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "pico/multicore.h"
#include "defines.h"
#include "debug.h"
#include "a2bus.h"
#include "busloop.h"
#include "busloop_wa.h"
#include "flash.h"
#include "flashunitmapper.h"
#include "ramdisk.h"
#include "dmamemops.h"
#include "misc.h"
#include "userconfig.h"
#include "cmdhandler.h"
#include "terminal.h"
#include "slinky.h"
#include "ipc.h"
#include "network.h"
#include "tftpstate.h"

static inline void InitActLed() {
  gpio_init(ACT_LED_PIN);
  gpio_set_dir(ACT_LED_PIN, GPIO_OUT);
  gpio_put(ACT_LED_PIN, 1); //turn it off
}

//
//GPIO Interrupt call back. 

void gpio_intr_callback(uint gpio, uint32_t events){
  if (gpio==nRESET_PIN) {
    //Abort TFTP network task if Apple is reset
    //during TFTP transfer
    if (IsUDPTaskRunning() && !IsNTPTaskRunning()) {
      UDPTask_RequestAbortIfRunning(); 
    }
    
    //Abort Erase Flash Disk
    //Read the note at AbortEraseFlashDisk() for more info
    AbortEraseFlashDisk();
  }
}

//
// Generate Interrupt if Apple Reset signal is active
// set gpio_intr_callback() as ISR (Interrupt Callback)
static void EnableAppleResetInterrupt() {
  gpio_init(nRESET_PIN);
  gpio_set_irq_enabled_with_callback(nRESET_PIN, GPIO_IRQ_EDGE_FALL, true /*enabled*/, &gpio_intr_callback);
}

//
//Use Core 1 to run Bus Loop
//
void __no_inline_not_in_flash_func(core1Main)() {
  //No interrupt on this core
  //Bus Loop is time critical
  save_and_disable_interrupts();  

  //Initalize data
  CommandHandlerInit();
   
#ifdef PICO_RP2040
  //RP2040 does not have enough memory to emulate a slinky (min: 256kB)
  //Wait for activation sequence
  BusLoopWaitActiviation();
#else
  //Emulate a slinky while waiting for activation sequence
  SlinkyInit();
  BusLoopSlinky();
#endif
  
  //Start actual bus loop
  BusLoopDataInit();    //It takes 8us to complete the initialization
  BusLoop();
}

//cmdhandler.c can set updateNTPNow to true
volatile bool updateNTPNow = false;

//
//Use Core 0 to run background task such as TFTP or NTP Time sync
//
void core0Loop() {
  const uint32_t NEXTUPDATE_SUCCESS = (24*60*60*1000);  //If last NTP update is successful, re-sync in 24hr
  const uint32_t NEXTUPDATE_FAILED  = (5*60*1000);      //If last NTP update failed, try again in 5 min
  absolute_time_t nextUpdateTime;
  
  if (CheckPicoW()) {
    do {
      int err = GetNetworkTime(); //Get current time from NTP Server
      DEBUG_PRINTF("GetNTP err=%d (%d=NETERR_NONE)\n",err,NETERR_NONE);      
      if (err==NETERR_NONE) nextUpdateTime = make_timeout_time_ms(NEXTUPDATE_SUCCESS);
      else nextUpdateTime = make_timeout_time_ms(NEXTUPDATE_FAILED);
      updateNTPNow = false; //Reset updateNTPNow after GetNetworkTime() so that the update request is ignored if 
                            //updateNTPNow is set to true during the execution of GetNetworkTime()      

        //wait until nextUpdateTime or msg from other core
        do {
          uint32_t param;
          bool msgReceived = multicore_fifo_pop_timeout_us(50*1000,&param);
          if (msgReceived) {
            struct IpcMsg* msg=(struct IpcMsg*)param;
            if (msg->command == IPCCMD_WIFITEST) {
              TestWifi((TestResult_t*)msg->data);
            } else if (msg->command == IPCCMD_TFTP) {
              ExecuteTFTP(msg->data /*taskid*/);
            }
          }
        }while (!time_reached(nextUpdateTime) && !updateNTPNow);
      
    } while(1);
  } else {
    //Not running on PicoW
    //Keep popping fifo queue to avoid blocking
    while(1) multicore_fifo_pop_blocking();
  }
}

int main() {
  #if OC_RP2350
  OverclockCPU(); //must be done before initialization of other hardware
                  //so that clock dividers of other hardware are set correctly.
  #endif
  InitPIO();  
  InitSpi();
  InitFlash();
  InitRamdisk();
  InitActLed();
  InitDMAChannel();
  InitTFTPState();
  
  //Enable Pull-down resistors of unused GPIOs
  gpio_pull_down(0);    //UART TX on devboard
  gpio_pull_down(1);    //UART RX on devboard
  gpio_pull_down(26);   //Activity LED on devboard
  gpio_pull_down(27);   //Not used on devboard and production board
  
  
#ifndef NDEBUG
  //For sending Debug Message to UART
  stdio_uart_init();    //Default baud: 115200

  //Disable stdout buffering
  //otherwise, text is not printed to uart or usb correctly.
  setbuf(stdout, NULL); 
#else
  //Disable stdio_uart for Release Build
  stdio_set_driver_enabled(&stdio_uart, false);
#endif

  //Load userConfig and Wifi Settings from security registers
  LoadAllConfigs();  
  
  //Setup Flash Units Mapping Data
  SetupFlashUnitMapping();  
  EnableFlashUnitMapping();

  //Check if we are connecting to Apple IIc
  bool appleConnected = IsAppleConnected();
  
  //Enable Apple Reset Interrupt only if we are connected to Apple IIc
  //The /Reset signal at the expansion slot connector is floating if
  //MegaFlash is not connected to Apple. There is no pull-up resistor
  //at the transceiver input on Rev 1.0PCB. So, enable interrupt only 
  // when appleConnected is true
  if (appleConnected) {
    EnableAppleResetInterrupt();
  }
  
  //
  // Core1: Apple Bus Loop
  //
  // For DEBUG Build, we want the Bus Loop is always running
  // so that we can turn on Apple computer to do testing at
  // any time. So, we run Bus Loop and User Terminal on different
  // CPU core.
  //
  // For Release Build, Bus Loop is not executed if Pico is not 
  // connected to Apple.
  //
#ifdef NDEBUG
  if (appleConnected) multicore_launch_core1(core1Main);  
#else
  multicore_launch_core1(core1Main);  //DEBUG build
#endif

  //
  //Save initialized setting from memory to flash if needed
  //See LoadAllConfigs() in userconfig.c
  //
  SaveConfigs();

  //
  //Print Debug Information to serial port
  //
  DEBUG_PRINTF("\nMegaFlash DEBUG Firmware Version %d\n",FIRMWAREVER);
  DEBUG_PRINTF("sys_pll   = %dMHz\n",frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY)/1000);
  DEBUG_PRINTF("clk_sys   = %dMHz\n",clock_get_hz(clk_sys)/1000000);
  DEBUG_PRINTF("clk_peri  = %dMHz\n",clock_get_hz(clk_peri)/1000000);
  DEBUG_PRINTF("SPI Speed = %dMHz\n",spi_get_baudrate(spi0)/1000000);
  DEBUG_PRINTF("WIFI Supported = %s\n",CheckPicoW()?"Yes":"No");
  DEBUG_PRINTF("Total heap = %d\n",GetTotalHeap());
  DEBUG_PRINTF("Free heap  = %d\n",GetFreeHeap());
  
  //
  //Core0: Running Wifi if connected to Apple IIc
  //       User Terminal if not.
  //
  if (appleConnected) {
    core0Loop();  //Running Wifi
  } else {
    stdio_usb_init();
    InitPicoLed();
    
    while(1) {
      if (stdio_usb_connected()) {
        UserTerminal();
      } else {
        sleep_ms(500);
      }
    }
  }

  while(true);
  return 0;
}

