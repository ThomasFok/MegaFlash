#ifndef _common_defines_h
#define _common_defines_h

#include <stdint.h>

//This file is shared with Pico and CPanel project

//************** MegaFlash ********************/
//MegaFlash Commands
#define CMD_RESETBOTHPTRS       0x00
#define CMD_RESETDATAPTR        0x01
#define CMD_RESETPARAMPTR       0x02
#define CMD_MODELINEAR          0x03
#define CMD_MODEINTERLEAVED     0x04
#define CMD_COLDSTART           0x05
#define CMD_ENABLEROMDISK       0x06
#define CMD_DISABLEROMDISK      0x07
#define CMD_LOAD_CPANEL         0x08
#define CMD_TESTWIFI            0x09
#define CMD_GETINFOSTR          0x0a

#define CMD_GETDEVINFO          0x10
#define CMD_GETDEVSTATUS        0x11
#define CMD_GETUNITSTATUS       0x12
#define CMD_GETDIB              0x13
#define CMD_GETVOLINFO          0x14
#define CMD_READBLOCK           0x15
#define CMD_WRITEBLOCK          0x16
#define CMD_GETPRODOSTIME       0x17
#define CMD_GETPRODS25TIME      0x18  
#define CMD_GETTIMESTR          0x19
#define CMD_SETRTC_PRODOS       0x1a
#define CMD_SETRTC_PRODOS25     0x1b
#define CMD_WRITEBLOCKSIZETOVDH 0x1c
#define CMD_FORMATDISK          0x1d
#define CMD_ERASEDISK           0x1e

#define CMD_SAVEUSERSETTINGS    0x20
#define CMD_GETUSERSETTINGS     0x21
#define CMD_SAVEWIFISETTINGS    0x22
#define CMD_GETCONFIGBYTES      0x23
#define CMD_ERASEUSERSETTINGS   0x24
#define CMD_ERASEWIFISETTINGS   0x25
#define CMD_ERASEADVSETTINGS    0x26
#define CMD_ERASEALLSETTINGS    0x27
#define CMD_DRIVEMAPPING        0x28

#define CMD_FADD                0x30
#define CMD_FMUL                0x31
#define CMD_FDIV                0x32
#define CMD_FSIN                0x33
#define CMD_FCOS                0x34
#define CMD_FTAN                0x35
#define CMD_FATN                0x36
#define CMD_FLOG                0x37
#define CMD_FEXP                0x38
#define CMD_FSQR                0x39
#define CMD_FOUT                0x3a
#define CMD_FSUB                0x3b

#define CMD_RESETSTOPWATCH_US   0x40
#define CMD_GETSTOPWATCH_US     0x41
#define CMD_RESETSTOPWATCH_MS   0x42
#define CMD_GETSTOPWATCH_MS     0x43
#define CMD_RESETSTOPWATCH_S    0x44
#define CMD_GETSTOPWATCH_S      0x45

#define CMD_TFTPRUN             0x50
#define CMD_TFTPSTATUS          0x51
#define CMD_TFTPGETLASTSERVER   0x52
#define CMD_TFTPSAVELASTSERVER  0x53

//MegaFlash Error Code
#define MFERR_NONE         0x00  /* No Error*/
#define MFERR_NOFLASH      0x01  /* Supported Flash Chip is not found  */
#define MFERR_NOTPICOW     0x02  /* Not running on PicoW */
#define MFERR_UNKNOWNCMD   0x03  /* Unknown Command Code */
#define MFERR_INVALIDWEKEY 0x04  /* Invalid Write Enable KEY */
#define MFERR_INVALIDUNIT  0x05  /* Invalid Unit Number */
#define MFERR_INVALIDBLK   0x06  /* Invalid Block Number */
#define MFERR_RWERROR      0x07  /* ReadBlock/WriteBlock Error */
#define MFERR_INVALIDPAGE  0x08  /* LoadCPanel Invalid Page */
#define MFERR_USERCONFIG   0x09  /* Error in accessing User Configuration */
#define MFERR_INVALIDARG   0x0A  /* Invalid Argument   */
#define MFERR_TIMEOUT      0x0B  /* Timeout Error.  */

//Write Enable Key
#define WRITEENABLEKEY  0x71

//Info String Type
#define INFOSTR_DEVICE  0x00

//Status Registers Fields
#define BUSYFLAG       0b10000000
#define ERRORFLAG      0b01000000
#define ERRORCODEFIELD 0b00011111 /*Last five bits are error code field*/

//Board Type
//MSB is set if Wifi is supported
typedef enum {
  BRD_PICO   = 0,
  BRD_PICOW  = BRD_PICO  | 0x80,
  BRD_PICO2  = 1,
  BRD_PICO2W = BRD_PICO2 | 0x80
} BoardType;

//Volume Types
typedef enum {
  TYPE_PRODOS = 0,
  TYPE_EMPTY = 1,
  TYPE_UNKNOWN = 2
} VolumeType;

//Storage Media Types
typedef enum {
  TYPE_FLASH,
  TYPE_ROMDISK,
  TYPE_RAMDISK
} MediaType;


/*********************************************************

The UserSetting_t and WifiSetting_t structure is used to
pass data between Apple and MegaFlash.

***********************************************************/

//*****************************************************
//
//               User Settings
//
//*****************************************************
//
#define USERSETTINGSVER              2       //Version starts at 1
#define TIMEZONEIDVER                1
#define USERSETTINGS_CHKBYTECOMP     0x5A

//Bits definition of configbyte1
#define CPUSPEEDFLAG   0b10000000 //1=Normal(1MHz), 0=Fast
#define AUTOBOOTFLAG   0b01000000
#define RAMDISKFLAG    0b00100000
#define NTPCLIENTFLAG  0b00010000
#define FPUFLAG        0b00001000
#define DEFCFGBYTE1    0b01000000  //Default configbyte1 value
#define DEFCFGBYTE2    0           //Default configbyte2 value
#define DEFFDENFLAGS   0xff        //Default value of fd_enableflags   

typedef struct  {
  uint8_t  version;
  uint8_t  checkbyte;   //=version^USERSETTINGS_CHKBYTECOMP to indicate the structure data is valid    
  uint8_t  configbyte1;
  uint8_t  configbyte2; //=0, reserved for future
  uint8_t  timezoneidver;  
  uint8_t  timezoneid;
  uint8_t  fd_enableflags; //Added: version 2
} UserSettings_t;


//*****************************************************
//
//              WIFI Setting
//
//*****************************************************
#define WIFISETTINGVER           1
#define WIFISETTING_CHKBYTECOMP  0x46
#define WIFIAUTHTYPE             0
#define WIFIOPTIONS              0
#define SSIDLEN                  32
#define WPAKEYLEN                63

typedef struct {
  uint8_t version;          //Version ID of the structure
  uint8_t checkbyte;        //=version^WIFISETTING_CHKBYTECOMP to indicate the structure data is valid    
  uint8_t authType;         //WIFI Authentication Type. Currently unused and should be 0
  uint8_t options;          //Reserved for manual IP Address configuration. should be 0
  uint32_t ipaddr;          //Reserved for manual IP Address configuration
  uint32_t netmask;         //Reserved for manual IP Address configuration
  uint32_t gateway;         //Reserved for manual IP Address configuration
  uint32_t dns;             //Reserved for manual IP Address configuration
  char wpakey[WPAKEYLEN+1]; //Wifi Password. +1 for null character  
  char ssid[SSIDLEN+1];     //SSID. +1 for null character
} WifiSetting_t; //len=117 bytes



//*****************************************************
//
//              Time  Zone Info
//
//*****************************************************
//These Time Zone Info may be changed in the future. timezoneidver is to identify the version of this Time Zone Information
#define TZHOUR {-12,-11,-10,-9,-9,-8,-7,-6,-5,-4,-3,-3,-2,-1,0,1,2,3, 3,4, 4,5, 5, 5,6, 6,7,8, 8,9, 9,10,10,11,12,12,13,14}
#define TZMIN  {  0,  0,  0,30, 0, 0, 0, 0, 0, 0,30, 0, 0, 0,0,0,0,0,30,0,30,0,30,45,0,30,0,0,45,0,30, 0,30, 0, 0,45, 0, 0}
#define DEFAULTTIMEZONE 14  //15th timezone. ie. UTC+0:00


/******************************* Test Wifi *********************************/
typedef enum {
  NETERR_UNKNOWN,
  NETERR_NOTPICOW,
  NETERR_TIMEOUT,
  NETERR_SSIDNOTSET,
  NETERR_NONET,    //No matching SSID
  NETERR_WIFINOTCONNECTED,
  NETERR_BADAUTH,  
  NETERR_ABORTED,  //Aborted by user or system reset
  NETERR_NOIP,     //Wifi OK, DHCP problem
  NETERR_DNSFAILED,//Wifi OK, DHCP OK, DNS problem
  NETERR_NTPFAILED,//Wifi OK, DHCP OK, DNS OK, NTP problem
  NETERR_NONE      //Everything ok
} NetworkError_t;


/********************************* TFTP ***********************************/
#define TFTP_HOSTNAME_MAXLEN 80     /* Not including the NULL characters */
#define TFTP_FILENAME_MAXLEN 80     /* Not including the NULL characters */





#endif