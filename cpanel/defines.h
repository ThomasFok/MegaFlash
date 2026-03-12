#ifndef _DEFINES_H
#define _DEFINES_H

#include "../common/defines.h"

//For CC65, global or static variables are usually faster than
//stack variables. So, some local variables are declared as
//static_local, which is defined as static to reduce code size.
#define static_local static

//
//MegaFlash Registers
#define CMDREG    0xc0c0  //Command Register
#define STATUSREG 0xc0c1  //Status Register
#define PARAMREG  0xc0c2  //Parameter Register
#define IDREG     0xc0c3  //ID Register


//
//Key Codes
#define KEY_UP    11
#define KEY_DOWN  10
#define KEY_LEFT  8
#define KEY_RIGHT 21
#define KEY_DEL   127
#define KEY_ESC   27
#define KEY_ENTER 13

//Mousetext Char
#define LEFT_BAR 223
#define RIGHT_BAR 218
#define TOP_BAR 204     //'\314'
#define BOTTOM_BAR '_'
#define DUAL_BAR 220
#define WHITE_BLOCK 160
#define TICK     196    //'\304'
#define CHECKER1 214
#define CHECKER2 215
#define ENTER    205    //'\315'
#define UPARROW  203    //'\313'
#define DNARROW  202    //'\312'
#define LTARROW  200    //'\310'
#define RTARROW  213    //'\325'
#define CDASH    211    //'\323'
#define ELLIPSIS 201    //'\311'


//Scroll Window Zero Page
#define WNDLFT  0x20   //Range: 0-39, Default 0
#define WNDWDTH 0x21   //Range: 1-40, Default 40
#define WNDTOP  0x22   //Range: 0-22, Default 0
#define WNDBTM  0x23   //Scroll Window Bottom Line + 1.
                       //Range WNDTOP+1 - 24, Default 24
#define WNDLFT_DEFAULT  0
#define WNDWDTH_DEFAULT 40
#define WNDTOP_DEFAULT  0
#define WNDBTM_DEFAULT  24

/////////////////////////////////////////////////////////
// Data structure of CMD_GETVOLINFO command result
//
// type = 0 if ProDOS,1 = Empty, 2 = Unknown as defined in VolumeType enum
// Block Count (Low)
// Block Count (High)
// mediumType
// Volume Name Length
// Volume Name, null terminated.
//
#define VOLNAMELEN 15
typedef struct {
  uint8_t type;
  uint16_t blockCount;
  uint8_t mediumType;
  uint8_t volNameLen;
  char volName[16];
} VolInfo_t;

/////////////////////////////////////////
// Fatal Errors
//
enum ERROR {
  ERR_CONFIG_INVALID,
  ERR_GETDEVINFOSTR_FAIL,
  ERR_LOADSETTING_FAIL,
  ERR_SAVECONFIGREBOOT_FAIL,
  ERR_UINTCOUNT_ZERO,
  ERR_GETVOLINFO_FAIL,
  ERR_GETBLOCKCOUNT_FAIL,
  ERR_GETBLOCKCOUNT_INVALID,
  ERR_TFTPRUN_FAIL
};

//Function Prototypes
void FatalError(uint8_t errorcode);
void Reboot();

#endif