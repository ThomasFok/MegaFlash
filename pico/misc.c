#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/timer.h"
#include "hardware/sync.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/spi.h"
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include "misc.h"
#include "defines.h"
#include "debug.h"
#include "flash.h"
#include "romdisk.h"
#include "cmdhandler.h"
#include "mediaaccess.h"


//compares s1 and s2 without sensitivity to case
int stricmp(const char* s1, const char* s2) {
  assert(s1 != NULL);
  assert(s2 != NULL);

  while (tolower((unsigned char) *s1) == tolower((unsigned char) *s2)) {
    if (*s1 == '\0')
      return 0;
    s1++; s2++;
  }

  return (int) tolower((unsigned char) *s1) -
    (int) tolower((unsigned char) *s2);
}

// Trim leading and trialing white space from a string
// Note: This function returns a pointer to a substring of the original string.
// If the given string was allocated dynamically, the caller must not overwrite
// that pointer with the returned value, since the original pointer must be
// deallocated using the same allocator with which it was allocated.  The return
// value must NOT be deallocated using free() etc.
char *strtrim(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}




/////////////////////////////////////////////////////////////////
//Debug routines to measure the performance
static uint32_t startTime;
void StartTimer() {
  startTime = time_us_32();
}

uint32_t EndTimer() {
  uint32_t elapsed = time_us_32()-startTime;
  return elapsed;
}


/////////////////////////////////////////////////////////////////
// Check if Apple II is connected by detecting PHI0 signal
//
// Output: true if Apple II is connected
//
bool IsAppleConnected() {
  bool phi0ClockFound = false;
  
  //Save Original Function
  gpio_function_t  orgFunc = gpio_get_function(PHI0_PIN);

  gpio_init(PHI0_PIN);
  gpio_set_dir(PHI0_PIN, GPIO_IN);

  //Disable Interrupt to make sure timing is right.
  const uint32_t status = save_and_disable_interrupts();  
  
  //If Apple is connected, PHI0 should change state every 0.5us.
  //Each loop iterations take 0.05 and 0.063us on RP2350 and RP2040 at 150MHz
  //respectively. 20 iterations should be more than enough for 150MHz clock
  //Scale up the number of iteration according to the clock speed
  #if SYS_CLK_MHZ > 150
  const uint LOOPCOUNT = (20 * SYS_CLK_MHZ / 150);
  #else 
  const uint LOOPCOUNT = 20;
  #endif

  const bool orgState = gpio_get(PHI0_PIN);
  for(int i=LOOPCOUNT;i!=0;--i) {
    if (gpio_get(PHI0_PIN) != orgState) {
      phi0ClockFound = true;
      break;
    }
  }
  
  //Restore Interrupt Status
  restore_interrupts(status); 

  //Restore Function
  gpio_set_function(PHI0_PIN, orgFunc);
  
  return phi0ClockFound;
}



/////////////////////////////////////////////////
// Check if running on Pico W
// On Pico W, pin 29 < 0.5V when pin 25 is low.
// On Pico, pin 29 is always high.
bool CheckPicoW() {
  static bool cachedResult=false;
  static bool cached=false;
  
  if (cached) return cachedResult;

  gpio_function_t pin25_func;
  gpio_function_t pin29_func;
  uint pin25_dir;
  uint pin29_dir;
  uint16_t adc_result;

  // remember pin directions
  pin25_dir = gpio_get_dir(25);
  pin29_dir = gpio_get_dir(29);

  // remember pin functions
  pin25_func = gpio_get_function(25);
  pin29_func = gpio_get_function(29);

  // initialize ADC peripheral
  adc_init();
  adc_gpio_init(29);
  adc_select_input(3);  //Input 3 for GPIO 29

  // Pull GPIO 25 low
  gpio_init(25);    
  gpio_set_dir(25, GPIO_OUT);
  gpio_put(25, 0);

  // Read ADC
  adc_result = adc_read();

  // restore pin functions
  gpio_set_function(25, pin25_func);
  gpio_set_function(29, pin29_func);

  // restore pin directions
  gpio_set_dir(25, pin25_dir);
  gpio_set_dir(29, pin29_dir);
  
  cachedResult = (adc_result<204);  //204=0.5V
  cached = true;
  
  return cachedResult;
}

//
//Pico Onboard LED
//
void InitPicoLed() {
  if (CheckPicoW()) {
    cyw43_arch_init(); //For turning Pico LED on/off       
    TurnOffPicoLed();  //cyw43 module won't load until we first access it.
                       //Call TurnOffPicoLed() to load and initalize cyw43
                       //Otherwise, file transfer doesn't work
  } else {
    //Init Pico Onboard LED GPIO
    gpio_init(PICO_LED_PIN);
    gpio_set_dir(PICO_LED_PIN, GPIO_OUT);
    gpio_put(PICO_LED_PIN, 0); //turn it off
  }  
}

void TurnOnPicoLed() {
  if (CheckPicoW()) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
  else gpio_set_mask(1ul<<PICO_LED_PIN); //Turn On
}

void TurnOffPicoLed() {
  if (CheckPicoW()) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
  else gpio_clr_mask(1ul<<PICO_LED_PIN); //Turn On
}


//
//Debug routine: Dump buffer to uart
void DumpBuffer(const uint8_t *buffer,uint len) {
  for(int i=0;i<len;++i) {
    if (i%16 ==0) DEBUG_PRINTF("\n%04X:",i);
    DEBUG_PRINTF(" %02X",buffer[i]);
  }
  DEBUG_PRINTF("\n"); 
}


uint32_t GetTotalHeap(void) {
  extern char __StackLimit, __bss_end__;
   
  return &__StackLimit  - &__bss_end__;
}

uint32_t GetFreeHeap(void) {
  struct mallinfo m = mallinfo();

  return GetTotalHeap() - m.uordblks;
}

//Reset the CPU
void SystemReset() {
  hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);
}


/////////////////////////////////////////////////
// Print the clock frequency of the system
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/clocks.h"
void measure_freqs() {
    uint f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
    uint f_pll_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY);
    uint f_rosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    uint f_clk_peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    uint f_clk_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    uint f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
#ifdef CLOCKS_FC0_SRC_VALUE_CLK_RTC
    uint f_clk_rtc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_RTC);
#endif

    printf("pll_sys  = %dkHz\n", f_pll_sys);
    printf("pll_usb  = %dkHz\n", f_pll_usb);
    printf("rosc     = %dkHz\n", f_rosc);
    printf("clk_sys  = %dkHz\n", f_clk_sys);
    printf("clk_peri = %dkHz\n", f_clk_peri);
    printf("clk_usb  = %dkHz\n", f_clk_usb);
    printf("clk_adc  = %dkHz\n", f_clk_adc);
#ifdef CLOCKS_FC0_SRC_VALUE_CLK_RTC
    printf("clk_rtc  = %dkHz\n", f_clk_rtc);
#endif

    // Can't measure clk_ref / xosc as it is the ref
}

/////////////////////////////////////////////////////////////
// Get ProDOS Volume Info
// Read Block 2 and return ProDOS volume information
//
// Input: unitNum    - Unit Number (1-N)
//        infoOut    - Pointer to VolumeInfo struct
//
// Output: bool - success
//
bool GetVolumeInfo(const uint unitNum, VolumeInfo *infoOut) {
  const uint32_t VDHBLOCK = 2;
  uint8_t  __attribute__((aligned(4))) buffer[BLOCKSIZE];
  
  if (!IsValidUnitNum(unitNum)) {
    return false;
  }
  
  //
  //Check if the partition is empty
  //Assume it is empty if block 0 and block 1
  //are all zeros
  //
  bool isEmpty=true;  //Assume it is empty

  for (uint block=0;block<=1;++block) {
    uint error = ReadBlock(unitNum, block, buffer, NULL);
    if (error != MFERR_NONE) {
      return false;
    }  
    uint32_t *p = (uint32_t*)buffer;
    for(uint i=BLOCKSIZE/4;i!=0;--i) {   
      if (*p++ != 0) {
        isEmpty = false;
        break;
      }
    }
    if (!isEmpty) break;
  }

  
  //
  //Check if it is a ProDOS partition
  //
  
  //Read Volume Directory Header Block
  uint error = ReadBlock(unitNum, VDHBLOCK, buffer, NULL);
  if (error != MFERR_NONE) {
    return false;
  }
  //Is ProDOS partition?
  bool prodos = true;  //Assume ProDOS
  if (*(uint32_t*)buffer != 0x00030000) prodos = false;
  if ((buffer[4]&0xf0)!=0xf0) prodos = false; 


  //Zero the output struct
  memset(infoOut, 0, sizeof( VolumeInfo));
  
  if (isEmpty) {
    infoOut->type = TYPE_EMPTY; //Empty
    infoOut->blockCount = GetBlockCount(unitNum);
  }
  else if (prodos) {
    infoOut->type = TYPE_PRODOS; //ProDOS
    
    //Volume Name
    uint32_t volNameLen = buffer[4]&0x0f;
    infoOut->volNameLen = (uint8_t) volNameLen;
    memcpy(infoOut->volName, buffer+5, volNameLen);  //Volume Name,don't use strcpy because the src string is not null terminated
    infoOut->volName[volNameLen] ='\0';              //Null terminate the string
    
    //Block Count as defined in VDH
    infoOut->blockCount = buffer[0x2a]*256 + buffer[0x29];
  } 
  else {
    infoOut->type = TYPE_UNKNOWN; //Unknown
    infoOut->blockCount = GetBlockCount(unitNum);    
  }
  
  return true;
}

////////////////////////////////////////////////////////////////////
// Print a Device Information to a destination Buffer
//
// Input: dest - pointer to destination buffer
//
void GetDeviceInfoString(char* dest) {
  dest += sprintf(dest,"Device Information\n\r");
  dest += sprintf(dest,"==================\n\n\r");

  //
  // Pico Board
  //
  dest += sprintf(dest,"Pico Board = ");  
  #ifdef PICO_RP2040
  dest += sprintf(dest,"Pico RP2040\n\r");
  #else
  dest += sprintf(dest,"Pico 2 RP2350\n\r");
  #endif
  
  dest += sprintf(dest,"Wifi Supported = ");
  dest += sprintf(dest,CheckPicoW()?"Yes\n\r":"No\n\r");
  
  //CPU Speed and SPI Speed
  dest += sprintf(dest,"CPU Speed = %.0fMHz, SPI Speed = %.1fMHz\n\r",clock_get_hz(clk_sys)/1000000.0f,spi_get_baudrate(spi0)/1000000.0f);
  
  //
  // Firmware Version
  //
  dest += sprintf(dest,"MegaFlash Pico Firmware Version = %s",FIRMWAREVERSTR);
  #ifndef NDEBUG
  dest += sprintf(dest," (DEBUG)");
  #endif
  dest += sprintf(dest,"\n\rPico Firmware Build Timestamp = %s %s\n\r",__DATE__,__TIME__);
  dest += sprintf(dest,"Pico SDK Version = %s\n\r",  PICO_SDK_VERSION_STRING);
  
  //
  // Flash Information
  //
  const uint32_t flashSize = GetFlashSize();
  dest += sprintf(dest,"Total Flash Capacity = %dMB\n\r", flashSize);
  dest += sprintf(dest,"Flash Chip #0 JEDEC ID = %Xh\n\r", ReadJEDECID(0));
  dest += sprintf(dest,"Flash Chip #1 JEDEC ID = %Xh\n\r", ReadJEDECID(1));
}





