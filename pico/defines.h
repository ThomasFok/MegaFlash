#ifndef _megaflash_defines_h
#define _megaflash_defines_h

#include "hardware/gpio.h"
#include "../common/defines.h"

/****************************************************

     Constants

****************************************************/

#define FIRMWAREVER     0x000E
#define FIRMWAREVERSTR  "V1.1.10"
// 0x0000 = V0.1
// 0x0001 = V0.2   18-Apr-2025
// 0x0002 = V0.3   05-May-2025
// 0x0003 = V1.0   14-Jul-2025
// 0x0004 = V1.1   30-Jul-2025
// 0x0005 = V1.1.1 04-Aug-2025
// 0x0006 = V1.1.2 11-Aug-2025
// 0x0007 = V1.1.3 15-Aug-2025
// 0x0008 = V1.1.4 18-Aug-2025
// 0x0009 = V1.1.5 25-Aug-2025
// 0x000A = V1.1.6 09-Sep-2025
// 0x000B = V1.1.7 20-Sep-2025
// 0x000C = V1.1.8 11-Mar-2026
// 0x000D = V1.1.9 17-Mar-2026
// 0x000E = V1.1.10 19-Mar-2026

//Deivce Signature Bytes
#define SIGNATURE1 0x88
#define SIGNATURE2 0x74

//IDREG Initial Value
#define IDREG_VAL 0x96

//
//Formatter
//
#define FMT_MINBLOCKCOUNT 32
#define FMT_MAXBLOCKCOUNT 0xffff
#define FMT_VOLNAMEMAXLEN 15
#define FMT_DEFAULTVOLNAME "MEGAFLASH"


//RAM Disk Size in Bytes
#ifdef PICO_RP2040
#define RAMDISK_SIZE (140*1024)
#else
#define RAMDISK_SIZE (400*1024)   /* Free heap is 36k when size =400k */
#endif

//Slinky Size in Bytes
#ifdef PICO_RP2040
#define SLINKY_SIZE (128*1024)
#else
#define SLINKY_SIZE (256*1024)
#endif

//Buffer Size
#define PARAMBUFFERSIZE  32 //Note: Smartport DIB requires 25 bytes
#define PARAMBUFFERINDEXMASK 0b11111
#define DATABUFFERSIZE   512
#define DATABUFFERINDEXMASK  0b111111111
#define BLOCKSIZE        512
#define PAGESIZE         256

//////////////////////////////////////////////////////////////////


//I/O Pins
#define PHI0_PIN 19
#define nRESET_PIN   21  /* Apple /Reset PIN, active low */
#define ACT_LED_PIN  26
#define PICO_LED_PIN 25  /* Pico Onboard LED */

static inline void TurnOnActLed() {
  gpio_clr_mask(1ul<<ACT_LED_PIN); //Turn on
}

static inline void TurnOffActLed() {
  gpio_set_mask(1ul<<ACT_LED_PIN); //Turn off
}



//Data Buffer Transfer Mode
typedef enum {
  MODE_LINEAR,
  MODE_INTERLEAVED
}transfermode_t;
#define DEFAULTTRANSFERMODE MODE_LINEAR

//MegaFlash Registers Address
#define CMDREG    0 /*$C0C0*/
#define STATUSREG 0 /*$C0C0*/
#define PARAMREG  1 /*$C0C1*/
#define DATAREG   2 /*$C0C2*/
#define IDREG     3 /*$C0C3*/



//ProDOS/SmartPort ReadBlock/WriteBlock Error Code
typedef enum {
  SP_NOERR      = 0,      //No Error
  SP_IOERR      = 0x27,   //I/O Error
  SP_NODRVERR   = 0x28,   //No Device Connected
  SP_NOWRITEERR = 0x2B,   //Write Protected Error
}rwerror_t;



#endif