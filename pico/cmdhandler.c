#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/aon_timer.h"
#include "defines.h"
#include "busloop.h"
#include "mediaaccess.h"
#include "flash.h"
#include "flashunitmapper.h"
#include "romdisk.h"
#include "ramdisk.h"
#include "rtc.h"
#include "userconfig.h"
#include "debug.h"
#include "misc.h"
#include "formatter.h"
#include "fpu.h"
#include "ipc.h"
#include "network.h"
#include "tftpstate.h"

//--------------------------------------------------------------
//The definitions below must be the same as the ones in a2bus.c
extern union {
  uint8_t  r[4];      //Individual 8-bit registers
  uint32_t chunk;     //A chunk of 4 registers
} registers;
//--------------------------------------------------------------

//--------------------------------------------------------------
//The definitions below must be the same as the ones in busloop.c
extern uint8_t parameterBuffer[]; 
extern uint8_t dataBuffer[];
extern uint parameterBufferIndex;
extern uint dataBufferIndex;
extern transfermode_t dataBufferTransferMode;
//--------------------------------------------------------------


//Set to true to activate NTP Client now
extern volatile bool updateNTPNow;


//////////////////////////////////////////////////////
// Initalize Command Handler 
// Currently, nothing to do
//
void CommandHandlerInit() {
}

/////////////////////////////////////////////////////////////
// Clear Error Flag and Error Code in status register
//
static inline void ClearError() {
  registers.r[STATUSREG] &= ~(ERRORFLAG|ERRORCODEFIELD);
}

/////////////////////////////////////////////////////////////
// Set Error Flag and Error Code in status register
//
// Input: error code
//
static inline void SetError(const uint8_t errorCode) {
  assert(errorCode < 32);
  ClearError();
  if (errorCode) registers.r[STATUSREG] |= ERRORFLAG | errorCode;
}

/////////////////////////////////////////////////////////////
// Reset Data Pointer to zero
//
static inline void ResetDataPointer() {
  dataBufferIndex = 0;
  registers.r[DATAREG]=dataBuffer[0];
}

/////////////////////////////////////////////////////////////
// Reset Parameter Pointer to zero
//
static inline void ResetParamPointer() {
  parameterBufferIndex = 0;
  registers.r[PARAMREG]=parameterBuffer[0];
}

/////////////////////////////////////////////////////////////
// Check if Write Enable Key is present in parameter buffer
// If the key is present, it would be destroyed.
// If not, MFERR_INVALIDWEKEY error is set.
//
// Input: Index - Location of the key in parameter buffer
// 
// Output: bool - key is present.
//
static bool __no_inline_not_in_flash_func(CheckWriteEnableKey)(const uint32_t index) {
  if (parameterBuffer[index] == WRITEENABLEKEY) {
    parameterBuffer[index] = 0; //Destroy the key
    return true;
  }
  
  SetError(MFERR_INVALIDWEKEY);
  return false;
}


/////////////////////////////////////////////////////////////////
// Enable/Disable Ramdisk according to RAMDISKFLAG
//
static void ReconfigRamdisk() {
  if (GetConfigByte1()&RAMDISKFLAG) EnableRamdisk();
  else DisableRamdisk();  
}

/////////////////////////////////////////////////////////////////
// When user config is changed, call this function to make the
// changes effective.
//
static void Reconfig() {
  //RAMDisk
  ReconfigRamdisk();
  
  //Flash Drive Mapping
  SetupFlashUnitMapping();
  
  //Timezone
  SetNewTimezoneOffset(GetTimezoneOffset());
  
  //NTP Client may be enabled.
  if (GetNTPClientEnabled() && !IsRTCRunning()) updateNTPNow = true;
}


/////////////////////////////////////////////////////////////
// Report Device Information
// For isonline driver routine
//
// Parameter Output:
//   Device Signature Byte 1
//   Device Signature Byte 2
//   DeviceInfoVer
//   Firmware Version Low Byte
//   Firmware Version High Byte
//   Pico Board Type
//   Hardware Type
//   Number of Flash Drive Enabled
//   Number of Flash Drive that actually exist
//   Flash Capacity in MB Low Byte
//   Flash Capacity in MB High Byte
//   Reserved = 0
//
static void DoGetDeviceInfo() {
  //Don't want random data in parameter buffer
  memset(parameterBuffer,0,PARAMBUFFERSIZE);
  
  parameterBuffer[0] = SIGNATURE1;              //First Signature Byte
  parameterBuffer[1] = SIGNATURE2;              //Second Signature Byte
  parameterBuffer[2] = 0x00;                    //DeviceInfoVer
  parameterBuffer[3] = FIRMWAREVER & 0xff;      //firmware version low byte
  parameterBuffer[4] = (FIRMWAREVER>>8) & 0xff; //firmware version high byte
  
  //Board Type
#ifdef PICO_RP2040
  parameterBuffer[5] = CheckPicoW()?BRD_PICOW:BRD_PICO;
#else
  parameterBuffer[5] = CheckPicoW()?BRD_PICO2W:BRD_PICO2;    
#endif

  //Hardware Type
  parameterBuffer[6] = 0; //0=MegaFlash
  
  //Number of Flash Drive Enabled
  parameterBuffer[7] = GetUnitCountFlashEnabled();
  
  //Number of Flash Drive that Acutally Exist
  parameterBuffer[8] = GetUnitCountFlashActual();
  
  //Total Capacity of Flash Memory in MB
  uint16_t flashSize = GetFlashSize();
  parameterBuffer[9]  = flashSize & 0xff;         //Low Byte
  parameterBuffer[10] = (flashSize>>8) & 0xff;    //High Byte
  
  //Reserved for future use
  parameterBuffer[11] = 0;
    
  ResetParamPointer();
  ResetDataPointer();
  ClearError();
}

/////////////////////////////////////////////////////////////
// Return Infomation Text String
// Transfer mode is set to linear
//
// Parameter Input:
//   Info String Type
//
static void DoGetInfoString(){
  //Copy Device Info string to Data Buffer
  uint32_t type = parameterBuffer[0];
  
  if (type==INFOSTR_DEVICE) {
    GetDeviceInfoString((char*)dataBuffer);
    assert(strlen(dataBuffer)<DATABUFFERSIZE);
  } else {
    SetError(MFERR_INVALIDARG);
    return;
  }

  dataBufferTransferMode = MODE_LINEAR; 
  ResetDataPointer();
  ClearError();
}

/////////////////////////////////////////////////////////////
// Report Device Status
// For getdevstatus driver routine
//
// Parameter Output:
//   unit count
//
// Possible Errors:
//   MFERR_NOFLASH
//
static void DoGetDeviceStatus() {
  uint numOfUnit = GetTotalUnitCount();
  
  parameterBuffer[0] = numOfUnit; 
  if (numOfUnit == 0) {
    SetError(MFERR_NOFLASH);
  } else {
    ClearError();
  }
  
  ResetParamPointer();
}

/////////////////////////////////////////////////////////////
// Report ProDOS Unit Status
// For getunitstatus driver routine
//
// Parameter Input:
//   unit number (1-N)
//
// Parameter Output:
//   block count (low byte)
//   block count (mid byte)
//   block count (high byte)
// 
// Possible Errors:
//   MFERR_INVALIDUNIT
// 
static void DoGetUnitStatus() {
  uint unitNum = parameterBuffer[0];
  
  //validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    parameterBuffer[0]=0; //Block Count = 0
    parameterBuffer[1]=0;
    parameterBuffer[2]=0;    
    SetError(MFERR_INVALIDUNIT);
    goto exit;
  }
  
  uint32_t blockCount = GetBlockCount(unitNum);
  parameterBuffer[0]= (uint8_t)blockCount;  blockCount>>=8;
  parameterBuffer[1]= (uint8_t)blockCount;  blockCount>>=8;
  parameterBuffer[2]= (uint8_t)blockCount;
  ClearError();

exit:  
  ResetParamPointer();
}
     
/////////////////////////////////////////////////////////////
// Get DIB (Device Information Block) of a unit
//
// Parameter Input:
//   unit number (1-N)
//
// Parameter Output:
//   Device Infomration Block (25 bytes)
// 
// Possible Errors:
//   MFERR_INVALIDUNIT
//      
static void DoGetDIB(){
  assert(sizeof(struct dib_t)<=PARAMBUFFERSIZE);
  uint unitNum = parameterBuffer[0];
  
  //validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    SetError(MFERR_INVALIDUNIT);
    goto exit;
  }

  GetDIB(unitNum,parameterBuffer);

exit:
  ResetParamPointer();
}


/////////////////////////////////////////////////////////////
// Read a ProDOS block from flash to dataBuffer
// For readblock driver routine
//
// Parameter Input:
//   unit number (1-N)
//   block number (low byte)
//   block number (mid byte)
//   block number (high byte) (ignored)
//
// Parameter Output:
//   Smartport/ProDOS error code (only when MFERR_RWERROR occurs)
//
// Possible Errors:
//   MFERR_INVALIDUNIT
//   MFERR_RWERROR
// 
// Note: To simplify the process of Apple Driver, the parameter 
// output of ReadBlock and WriteBlock calls is ProDOS/Smartport 
// error code. The driver may return this code back to ProDOS/Smartport.
// If it is zero, it means no error.
static void __no_inline_not_in_flash_func(DoReadBlock)() {
  uint unitNum = parameterBuffer[0];
  const uint blockNum = parameterBuffer[1] | parameterBuffer[2]<<8 | parameterBuffer[3]<<16;
  
  //Assume no error
  ClearError();   

  uint error = ReadBlock(unitNum, blockNum, dataBuffer,&parameterBuffer[0]);
  SetError(error);
  
exit:
  ResetDataPointer();
  ResetParamPointer();  
}

/////////////////////////////////////////////////////////////
// Write ProDOS block from dataBuffer
// For writeblock driver routine
//
// Parameter Input:
//   unit number (1-N)
//   block number (low byte)
//   block number (mid byte)
//   block number (high byte)
//   Write Enable Key
//
// Parameter Output:
//   Smartport/ProDOS error code (SP_NOERR, SP_IOERR, SP_NODRVERR, SP_NOWRITEERR)
//
// Possible Errors:
//   MFERR_INVALIDUNIT
//   MFERR_INVALIDWEKEY
//   MFERR_RWERROR
// 
static void __no_inline_not_in_flash_func(DoWriteBlock)() {
  uint unitNum = parameterBuffer[0];
  const uint blockNum = parameterBuffer[1] | parameterBuffer[2]<<8 | parameterBuffer[3]<<16;
  
  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(4)) {
    parameterBuffer[0] = SP_IOERR;  //Return I/O Error code
    SetError(MFERR_INVALIDWEKEY);
    goto exit;
  }  

  uint error = WriteBlock(unitNum, blockNum, dataBuffer, &parameterBuffer[0]);
  SetError(error);

exit: 
  ResetDataPointer();
  ResetParamPointer();    
}

//////////////////////////////////////////////////////
// Report current date and time to ProDOS
// The date/time is formatted according to ProDOS standard
// So, the clock driver just needs to copy the bytes
// to $BF90-$BF93
//
// Parameter Output:
//   Date Byte 1 for copying to $BF90
//   Date Byte 2 for copying to $BF91
//   Minute Byte for copying to $BF92
//   Hour Byte   for copying to $BF93
//
static void DoGetProdosTime(){
  GetProdosTimestamp(parameterBuffer);
  
  ResetParamPointer();
  ClearError(); 
}

//////////////////////////////////////////////////////
// Report current date and time to ProDOS 2.5 or above
// The date/time is formatted according to ProDOS 2.5 standard
// So, the clock driver just needs to copy the bytes
// to $BF8E-$BF93
//
// Parameter Output:
//   millisecond for copying to $BF8E
//   Second Byte for copying to $BF8F
//   Time Byte 1 for copying to $BF90
//   Time Byte 2 for copying to $BF91
//   Date Byte 1 for copying to $BF92
//   Date Byte 2 for copying to $BF93
//
static void DoGetProdos25Time(){
  GetProdos25Timestamp(parameterBuffer);
  
  ResetParamPointer();
  ClearError();
}

//////////////////////////////////////////////////////
// Generate a time string for Apple to display on screen
// The string is always 8 characters long in the format of
// 12:59 AM
// 
// The string is preformatted for Apple with High bit set.
// Apple Program code just needs to copy the string to screen
// memory directly
//
// If RTC is not set, 8 space characters are returned.
// 
static void DoGetTimeString(){
  //The buffer is slightly bigger in case something
  //goes wrong and sprintf() generates a longer
  //string. (e.g. h=140, m=210)
  static char formattedTextBuffer[16];
  static uint8_t lasth = 0xff;
  static uint8_t lastm = 0xff;
  const uint32_t STRLEN = 8;  //Length of formatted string
  struct tm t;

  //Note: Don't use aon_timer_is_running(). It causes TFTP Task to stop running
  //when Apple calls this method to show the time.
  if (IsRTCRunning()){
    aon_timer_get_time_calendar(&t);
  
    uint8_t h = t.tm_hour;
    uint8_t m = t.tm_min;

    //reformat if time changes
    if (h!=lasth || m!=lastm) {
      lasth = h; lastm = m;
      if (h==0) h=12;
      else if (h>=13) h-=12;
      
      sprintf(formattedTextBuffer,"%2d:%02d AM",h,m);       
       
      //Change AM to PM if hour>=12
      if (t.tm_hour>=12) formattedTextBuffer[6] = 'P';
    }
    
     //Copy from formattedTextBuffer to parameterBuffer and set high bit
    for(uint32_t i=0;i<STRLEN;++i) {
      parameterBuffer[i] = formattedTextBuffer[i]|0x80;
    }     
  } else {
    //RTC not running.
    //Write 8 space characters with high bit set to parameterBuffer directly
    for(uint32_t i=0;i<STRLEN;++i){
      parameterBuffer[i] = ' '|0x80;
    }
  }
  
  //Null-terminate the string
  parameterBuffer[STRLEN] ='\0';
  
  ClearError();
  ResetParamPointer();
}

//////////////////////////////////////////////////////
// Set RTC from timestamp in ProDOS format
// The date/time is formatted according to ProDOS standard
// which is located at $BF90-$BF93
//
// Parameter Input:
//   Date Byte 1 ($BF90)
//   Date Byte 2 ($BF91)
//   Minute Byte ($BF92)
//   Hour Byte   ($BF93)
//   Write Enable Key
//
// Note: Currently, this command is not used by Apple firmware.
// But it makes a clock setting application program possible.
static void DoSetRTC_Prodos(){
  //Assume no error
  ClearError();   

  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(4)) {
    goto exit;
  }

  //Set the RTC!
  SetRTCFromProdosTimestamp(parameterBuffer);

exit:
  return;  
}

//////////////////////////////////////////////////////
// Set RTC from timestamp in ProDOS format
// The date/time is formatted according to ProDOS standard
// which is located at $BF8E-$BF93
//
// Parameter Input:
//   millisecond 
//   Second Byte 
//   Time Byte 1
//   Time Byte 2
//   Date Byte 1
//   Date Byte 2
//   Write Enable Key
//
// Note: Currently, this command is not used by Apple firmware.
// But it makes a clock setting application program possible.
static void DoSetRTC_Prodos25(){
  //Assume no error
  ClearError();   

  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(6)) {
    goto exit;
  }

  //Set the RTC!
  SetRTCFromProdos25Timestamp(parameterBuffer);

exit:
  return;  
}

/////////////////////////////////////////////////////////////
// Write Block Size to Block 2 (Volume Diretory Header)
// Write Block size to offset $29-$2A of block 2 of the selected unit
// to work around the format bug of Copy II+ 8.x/9.x
//
// Parameter Input:
//   unit number (1-N)
//   Write Enable Key
//
// Parameter Output:
//   Smartport/ProDOS error code (SP_NOERR, SP_IOERR, SP_NODRVERR, SP_NOWRITEERR)
//
// Possible Errors:
//   MFERR_INVALIDUNIT
//   MFERR_INVALIDWEKEY
//   MFERR_RWERROR
// 
static void DoWriteBlockSizeToVDH() {
  const uint VDHBLOCK = 2;  
  uint8_t buffer[BLOCKSIZE];
  uint unitNum = parameterBuffer[0];
  uint error;
  
  //Assume no error
  ClearError();    
  
  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(1)) {
    parameterBuffer[0] = SP_IOERR;  //Return I/O Error code
    goto exit;
  }
  
  //Read Volume Directory Header (Block 2)
  error = ReadBlock(unitNum, VDHBLOCK, buffer, &parameterBuffer[0]);
  if (error != MFERR_NONE) {
    SetError(error);
    goto exit;
  }

  //Write Block Count to offset 0x29-2a
  uint32_t blockCount = GetBlockCount(unitNum);
  
  //Limit it to 16-bit
  if (blockCount >0xffff) blockCount = 0xffff; 
  
  buffer[0x29] = (uint8_t)blockCount; blockCount>>=8;
  buffer[0x2a] = (uint8_t)blockCount;

  //Write it back
  error = WriteBlock(unitNum, VDHBLOCK, buffer, &parameterBuffer[0]);  
  if (error != MFERR_NONE) {
    SetError(error);
  }

exit: 
  ResetParamPointer();     
}


/////////////////////////////////////////////////////////////
// Load Control Panel Program code to Apple II
// One page (256 bytes) of Control Panel Program is copied
// to Data Buffer.
// Transfer Mode is set to linear
//
// Parameter Input:
//   page requested
//
// Possible Errors:
//   MFERR_INVALIDPAGE
// 
// Note: There is no command to retrieve the length of Control
// Panel program code. Apple II firmware should keep requesting 
// pages until MFERR_INVALIDPAGE error occurs.
static void DoLoadCPanel() {
  extern const uint8_t cpanelData[];
  extern const uint32_t cpanelDataLen[];  
  
  const uint pageCount = (cpanelDataLen[0]+255)/256;   //Round-up Division
  
  uint page = parameterBuffer[0]; //Page Requested
  if (page<pageCount) {
    //Copy Control Panel Program code to Data Buffer
    //No need to use DMA since the page size is 256 bytes only
    memcpy(dataBuffer, cpanelData+page*256, 256);
    ClearError();
  } else {
    //invalid page number
    SetError(MFERR_INVALIDPAGE);
  }

  dataBufferTransferMode = MODE_LINEAR; 
  ResetDataPointer();
  ResetParamPointer();
}

/////////////////////////////////////////////////////////////
// Save Wifi Setting to non-volatile memory
// The data as defined in WifiSetting_t should have uploaded
// to Data Buffer by Apple.
// Note: WifiSetting_t is too big to fit in Parameter Buffer
// So, it is uploaded to Data Buffer.
//
// Parameter Input:
//   Write Enable Key
//
// Possible Errors:
//   MFERR_INVALIDWEKEY
static void DoSaveWifiSettings() { 
  //Assume no error
  ClearError();   
  
  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(0)) {
    goto exit;
  }

  bool success = SaveWifiSettings(dataBuffer);
  if (success) {
    Reconfig();  //User Config changed.
  } else {
    SetError(MFERR_USERCONFIG);
  }

exit:
  return;
}

/////////////////////////////////////////////////////////////
// Save User Settings to non-volatile memory
// The data as defined in UserSettings_t should have uploaded
// to Data Buffer by Apple.
//
// Parameter Input:
//   Write Enable Key
//
// Possible Errors:
//   MFERR_INVALIDWEKEY
//   MFERR_USERCONFIG
static void DoSaveUserSettings() {
  //Assume no error
  ClearError();   
  
  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(0)) {
    goto exit;
  }

  bool success = SaveUserSettings(dataBuffer);
  if (success) {
    Reconfig();  //User Config changed.
  } else {
    SetError(MFERR_USERCONFIG);
  }

exit:
  return;   
}


//////////////////////////////////////////////////////
// Get User Settings (UserSettings_t structure)
// The data structure is copied to dataBuffer
// Transfer mode is set to linear
//
static void DoGetUserSettings(){
  GetUserSettings(dataBuffer);
  ClearError();
  dataBufferTransferMode = MODE_LINEAR;   
  ResetDataPointer();
} 

/////////////////////////////////////////////////////////////
// Get configbyte1 and configbyte2
//
// Parameter Output:
//   configbyte1
//   configbyte2
//
static void DoGetConfigBytes() {
  parameterBuffer[0]=GetConfigByte1();
  parameterBuffer[1]=GetConfigByte2();
  
  ClearError();
  ResetParamPointer();
}

/////////////////////////////////////////////////////////////
// Erase User Settings from Flash and
// Initialize the config in memory to default
//
// Parameter Input:
//   Write Enable Key
//
static void DoEraseUserSettings() {
  //Validate Write Enable Key
  if (!CheckWriteEnableKey(0)) {
    return;
  }
  
  EraseUserSettingsFromFlash();
  Reconfig();  //User Config changed.
  
  ClearError();
}

/////////////////////////////////////////////////////////////
// Erase Wifi Settings from Flash and
// Initialize the wifi settings in memory to default
//
// Parameter Input:
//   Write Enable Key
//
static void DoEraseWifiSettings() {
  //Validate Write Enable Key
  if (!CheckWriteEnableKey(0)) {
    return;
  }
  
  EraseWifiSettingsFromFlash();
  Reconfig();  //User Config changed.  
  
  ClearError();
}

/////////////////////////////////////////////////////////////
// Erase Advanced Settings from Flash and
// Initialize the advanced settings in memory to default
//
// Parameter Input:
//   Write Enable Key
//
static void DoEraseAdvancedSettings() {
  //Validate Write Enable Key
  if (!CheckWriteEnableKey(0)) {
    return;
  }
  
  EraseAdvancedSettingsFromFlash();
  Reconfig();  //User Config changed.  
  
  ClearError();  
}

/////////////////////////////////////////////////////////////
// Erase Both User Config and Wifi Setting from Flash
// Initialize them to default
//
// Parameter Input:
//   Write Enable Key
//
static void DoEraseAllSettings() {
  //Validate Write Enable Key
  if (!CheckWriteEnableKey(0)) {
    return;
  }
  
  EraseAllSettingsFromFlash();
  Reconfig();  //User Config changed.
  
  ClearError();
}

///////////////////////////////////////////////////////////////
// Enable/Disable Drive Mapping
// This command is for Control Panel to disable drive mapping
// so that it can access to all drives including RAMDisk.
//
// To re-enable the drive mapping, call this command or
// CMD_COLDSTART command
//
// Parameter Input:
//  Enable Flag !=0 to enable, =0 to disable
//  Write Enable Key
//
static void DoDriveMapping() {
  //Validate Write Enable Key
  if (!CheckWriteEnableKey(1)) {
    return;
  }
  
  if (parameterBuffer[0]) {
    //Enable Flash Drive Mapping
    EnableFlashUnitMapping();
    ReconfigRamdisk();
  } else {
    //Disable Flash Drive Mapping and Enable RAMDisk
    DisableFlashUnitMapping();
    EnableRamdisk();
  }
}


/////////////////////////////////////////////////////////////
// This command has two purposes.
// 1) Inform Pico that Apple has cold-started (Powerup or Ctrl-OA-Apple)
// 2) Retreieve the config bytes for Apple Firmware to initalize 
//    the machine
// Since this command changes the state of Pico, Write Enable Key is needed.
//
// Parameter Input:
//   Write Enable Key
//
// Parameter Output:
//   configbyte1
//   configbyte2
//
static void DoAppleColdStart() {
  //Validate Write Enable Key
  if (!CheckWriteEnableKey(0)) {
    return;
  }
  
  //Reset data transfer mode in case the computer is reset during data transfer
  dataBufferTransferMode = MODE_LINEAR;   
  
  //ROM Disk is disabled by default
  DisableRomdisk();
  
  //Make sure all config changes are effective.
  Reconfig();
  
  //Enable Flash Unit Mapping in case the user Reset the machine from Control Panel
  EnableFlashUnitMapping();
  
  //Same implementation as DoGetConfigBytes()
  DoGetConfigBytes();
}

//////////////////////////////////////////////////////
// Sub-routine for DoTestWifi()
// Format an IP Address to string
// and copy it to destination buffer
//
// Input: dest   - Destination Buffer
//        ipaddr - IP Address
//
// Output: char* - Point to the byte after the null character
//                 of the result
//
static char* FormatIPAddr(char* dest, ip_addr_t ipaddr) {
  //format the IP Address for Apple to display
  char* ipaddrstr;
  
  //Convert ipaddr to string
  ipaddrstr = ip4addr_ntoa(&ipaddr);
  
  //Copy to dest
  strcpy(dest,ipaddrstr);
  
  return dest+strlen(dest)+1;
}


/////////////////////////////////////////////////////////////
// Test Wifi Connection (for Apple Control Panel)
// The IP addresses are formatted for Apple Display (high-bit set)
// and stored in data buffer.
// Since this command stalls Pico for 30seconds to 1 minute, 
// Write Enable Key is required to avoid unintended trigger
// of the command
// Transfer mode is set to linear
//
// Parameter Input:
//   Write Enable Key
//
// Parameter Output:
//   offset 0    : errorcode as defined by NetworkError_t in ntp.h file
//   offset 1-4  : IP Address in Big-Endian Order
//   offset 5-8  : Netmask
//   offset 9-12 : Gatway Address
//   offset 13-16: DNS Sever Address
//
// Data Buffer Output:
//  IP Address String (Null-Terminated)
//  Netmask String
//  Gateway Address String
//  DNS Server Address String
//
static void DoTestWifi() {
  const uint32_t TIMEOUT_MS = 90*1000; //90 Seconds. Also update control panel text message if this value is changed.

  //Running on Pico W?
  if (!CheckPicoW()) {
    parameterBuffer[0] = NETERR_NOTPICOW;
    goto exit;
  }
  
  //Validate Write Enable Key
  if (!CheckWriteEnableKey(0)) {
    goto exit;
  }
  
  //testResult and msg need to be static because
  //their addresses are passed to another core.
  //They need to be always present in memory. If they are
  //stack variables, the other core may corrupt the stack 
  //when this function timeouts and returns but another core
  //is still processing.
  static TestResult_t testResult;
  memset(&testResult, 0 ,sizeof(TestResult_t));
  testResult.testCompleted = false;
  
  static struct IpcMsg msg;
  msg.command = IPCCMD_WIFITEST;
  msg.data = (uint32_t)&testResult;
  
  //Ask another core to do the test
  UDPTask_RequestAbortIfRunning();      //Abort if another network is running
  multicore_fifo_push_timeout_us((uint32_t)&msg,10);

  //wait until test completed or timeout
  absolute_time_t until = make_timeout_time_ms(TIMEOUT_MS);
  do {
    sleep_ms(1);
  } while(!testResult.testCompleted && !time_reached(until));
  
  if (testResult.testCompleted) {
    DEBUG_PRINTF("Test Completed: errorcode = %d (%d=NO_ERR)\n",testResult.error,NETERR_NONE);
  } else {
    DEBUG_PRINTF("Timeout\n");
    UDPTask_RequestAbortIfRunning(); //try to stop the task
    parameterBuffer[0] = NETERR_TIMEOUT;
    goto exit;
  }

  ClearError();
  parameterBuffer[0] = (uint8_t)testResult.error;
  memcpy(parameterBuffer+1, &testResult, sizeof(ip4_addr_t)*4);
  
  char* dest = (char*)dataBuffer;
  dest = FormatIPAddr(dest, testResult.ipaddr);
  dest = FormatIPAddr(dest, testResult.netmask);
  dest = FormatIPAddr(dest, testResult.gateway);
  dest = FormatIPAddr(dest, testResult.dnsserver);

exit:
  dataBufferTransferMode = MODE_LINEAR;   
  ResetDataPointer();
  ResetParamPointer();
}

/////////////////////////////////////////////////////////////
// Format a unit
//
// Parameter Input: 
//   Unit Number
//   block count (low byte)
//   block count (mid byte)
//   block count (high byte)
//   WE Key
//   Volume Name String,Null-terminated
//
// Parameter Output:
//   Smartport/ProDOS error code (only when MFERR_RWERROR occurs)
//
// Possible Errors:
//   MFERR_INVALIDUNIT
//   MFERR_INVALIDBLK
//   MFERR_RWERROR
// 
static void DoFormatDisk() {
  uint unitNum = parameterBuffer[0];
  const uint32_t blockCount = parameterBuffer[1] | parameterBuffer[2]<<8 | parameterBuffer[3]<<16;
  char* volName = parameterBuffer + 5;
  
  //Assume no error
  ClearError();   
  
  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(4)) {
    parameterBuffer[0] = SP_IOERR;  //Return I/O Error code
    goto exit;
  }

  //Validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    parameterBuffer[0] = SP_IOERR;  //Return I/O Error code    
    SetError(MFERR_INVALIDUNIT);
    goto exit;
  }  

  //Check if the unit is writable
  if (!IsUnitWritable(unitNum)) {
    parameterBuffer[0] = SP_NOWRITEERR;   //Write Protected Error
    SetError(MFERR_RWERROR);
    goto exit; 
  }
  
  //Validate blockNum
  if (blockCount <  FMT_MINBLOCKCOUNT || blockCount>FMT_MAXBLOCKCOUNT || blockCount>GetBlockCount(unitNum)) {
    parameterBuffer[0] = SP_IOERR;
    SetError(MFERR_INVALIDBLK);
    goto exit;
  }
  
  //Sanitize Volume Name
  uint32_t volNameLen = SanitizeVolumeName(volName);
  
  DEBUG_PRINTF("Format: unit=%d, blockCount=%d\n",unitNum,blockCount);
  DEBUG_PRINTF("        len =%d, name=%s\n",volNameLen,volName);
  
  //Format!
  bool success = FormatUnit(unitNum, blockCount, volName, volNameLen);
  if (success) parameterBuffer[0] = SP_NOERR;
  else {
    parameterBuffer[0] = SP_IOERR;
    SetError(MFERR_RWERROR);
  }
  
exit: 
  ResetDataPointer();
  ResetParamPointer();  
}

/////////////////////////////////////////////////////////////
// Erase a unit
//
// Parameter Input: 
//   Unit Number
//   WE Key
//
// Parameter Output:
//   Smartport/ProDOS error code (only when MFERR_RWERROR occurs)
//
// Possible Errors:
//   MFERR_INVALIDUNIT
//   MFERR_RWERROR
// 
static void DoEraseDisk() {
  uint unitNum = parameterBuffer[0];
  
  //Assume no error
  ClearError();   
  
  //Validate Write Enable Key, to avoid unintented Write
  if (!CheckWriteEnableKey(1)) {
    parameterBuffer[0] = SP_IOERR;  //Return I/O Error code
    goto exit;
  }

  //Validate unitNum
  if (!IsValidUnitNum(unitNum)) {
    parameterBuffer[0] = SP_IOERR;  //Return I/O Error code    
    SetError(MFERR_INVALIDUNIT);
    goto exit;
  }  

  //Check if the unit is writable
  if (!IsUnitWritable(unitNum)) {
    parameterBuffer[0] = SP_NOWRITEERR;   //Write Protected Error
    SetError(MFERR_RWERROR);
    goto exit; 
  }
  
  //Erase the unit
  bool success = EraseEntireUnit(unitNum);
  if (success) parameterBuffer[0] = SP_NOERR;
  else {
    parameterBuffer[0] = SP_IOERR;
    SetError(MFERR_RWERROR);
  }
  
exit: 
  ResetDataPointer();
  ResetParamPointer();  
}


/////////////////////////////////////////////////////////////
// Get ProDOS Volume Info
//
// Parameter Input: 
//   Unit Number
//
// Parameter Output:
//   Type = 0 if ProDOS, !=0 if unknown
//   Block Count (Low)
//   Block Count (High)
//   MediumType
//   Volume Name Length
//   Volume Name String, null terminated.
//
static void DoGetVolumeInfo() {
  VolumeInfo info;
  uint unitNum = parameterBuffer[0];

  //Assume no error
  ClearError();   
  
  if (!IsValidUnitNum(unitNum)) {
    SetError(MFERR_INVALIDUNIT);
    goto exit;
  }  

  bool success = GetVolumeInfo(unitNum,&info);
  if (!success) {
    SetError(MFERR_RWERROR);    
    goto exit;
  }
  
  //Don't want random data in parameter buffer
  memset(parameterBuffer,0,PARAMBUFFERSIZE);
  
  //Type
  parameterBuffer[0] = info.type;
  
  //Block Count
  parameterBuffer[1] = (uint8_t) info.blockCount;
  parameterBuffer[2] = (uint8_t) (info.blockCount>>8);  
  parameterBuffer[3] = (uint8_t) GetMediumType(unitNum);
  
  parameterBuffer[4] = info.volNameLen;         //Volume Name Length
  strncpy(parameterBuffer+5, info.volName, VOLNAMELENMAX+1); //Volume Name


exit: 
  ResetDataPointer();
  ResetParamPointer();   
}

/********************************************************************

        Timer
        
********************************************************************/
/////////////////////////////////////////////////////////////
// Reset microseconds timer to 0
// 
static uint32_t startTime_us=0ul;
static void DoResetTimer_us() {
  startTime_us = time_us_32();  //current timestamp value in microseconds
}

/////////////////////////////////////////////////////////////
// Get the microseconds timer
// and put it into parameter buffer as a 32-bit integer
//
static void DoGetTimer_us() {
  uint32_t elapsed = time_us_32() - startTime_us;
  *(uint32_t*)parameterBuffer = elapsed;
  ResetParamPointer();
}

////////////////////////////////////////////////////////////
// Reset milliseconds timer to 0
// 
static uint64_t startTime_ms=0ull;
static void DoResetTimer_ms() {
  startTime_ms = time_us_64();  //current timestamp value in microseconds
}

/////////////////////////////////////////////////////////////
// Get the milliseconds timer
// and put it into parameter buffer as a 32-bit integer
//
static void DoGetTimer_ms() {
  uint64_t elapsed = (time_us_64() - startTime_ms)/1000ull;
  *(uint32_t*)parameterBuffer = (uint32_t)elapsed;
  ResetParamPointer();
}

/////////////////////////////////////////////////////////////
// Reset seconds timer to 0
// 
static uint64_t startTime_s=0ull;
static void DoResetTimer_s() {
  startTime_s = time_us_64(); //current timestamp value in microseconds
}

/////////////////////////////////////////////////////////////
// Get the milliseconds timer
// and put it into parameter buffer as a 32-bit integer
//
static void DoGetTimer_s() {
  uint64_t elapsed = (time_us_64() - startTime_s)/1000000ull;
  *(uint32_t*)parameterBuffer = (uint32_t)elapsed;
  ResetParamPointer();
}

/********************************************************************

        TFTP
        
********************************************************************/
static uint32_t tftpCurrentTaskID = 0;
extern volatile tftp_state_t tftp_state;


/////////////////////////////////////////////////////////////
// Execute TFTP Transfer
//
// Parameter Input: 
//   Unit Number
//   Direction  - 0=Download from server, 1=Upload to server
//   Flag       - Bit0:To save hostname to userConfig
//   WE Key
//
// Data Buffer Input:
//   hostname   - hostname string
//   filename   - filename string
//
static void DoTFTPRun() {
  const uint32_t TIMEOUT_MS = 30*1000; //30 Seconds. 
  
  //Assume no error
  ClearError();     
  
  //Running on Pico W?
  if (!CheckPicoW()) {
    parameterBuffer[0] = NETERR_NOTPICOW;
    goto exit;
  }  
  
  //
  //Step 1: Validate Write Enable Key, to avoid unintended command
  if (!CheckWriteEnableKey(3)) {
    goto exit;
  }
  
  //
  //Step 2: Validate hostname and filename len
  char *hostname = (char*)dataBuffer;
  char *filename = (char*)(dataBuffer+strlen(hostname)+1);  
  hostname = strtrim(hostname); 
  //Note: strtrim may change the length of hostname
  //trim hostname after getting filename                               

  if (strlen(hostname)>TFTP_HOSTNAME_MAXLEN) {
    SetError(MFERR_INVALIDARG);
    goto exit;
  }
  if (strlen(filename)>TFTP_FILENAME_MAXLEN) {
    SetError(MFERR_INVALIDARG);    
    goto exit;
  }  
  
  //Save hostname
  uint32_t flag = parameterBuffer[2];
  if (flag&0x01) {
    SaveTFTPLastServer(hostname);  
  }
  
  //
  //Step 3: Abort running task
  if (!UDPTask_AbortTimeout_ms(TIMEOUT_MS)) {
    SetError(MFERR_TIMEOUT);
    goto exit;
  }
  
  //
  //Step 4: Copy Parameters to tftp_state
  tftp_state.unitNum = parameterBuffer[0];
  tftp_state.dir = parameterBuffer[1];    //0=Download from server, 1=Upload to server
  TFTPCopyHostname(hostname); 
  TFTPCopyFilename(filename);
  
  DEBUG_PRINTF("CMD_TFTPRUN Parameters:\n");
  DEBUG_PRINTF("dir = %d\n",tftp_state.dir);
  DEBUG_PRINTF("unitNum = %d\n",tftp_state.unitNum);
  DEBUG_PRINTF("Hostname = %s\n",hostname);
  DEBUG_PRINTF("filename = %s\n",filename);
  
  //Step 5: Send IPC command to another core
  ++tftpCurrentTaskID;    //Get a new Task ID
  static struct IpcMsg msg; //must be static since &msg is passed to another core
  msg.command = IPCCMD_TFTP;
  msg.data = tftpCurrentTaskID;
  multicore_fifo_push_timeout_us((uint32_t)&msg,10);
  
  //Step 6: Wait until TFTP Task is running
  absolute_time_t until = make_timeout_time_ms(TIMEOUT_MS);
  bool taskStarted = false;
  do {
    if (tftp_state.taskid==tftpCurrentTaskID) {
      taskStarted = true;
      TRACE_PRINTF("Task has started. DoTFTPRun() returning\n");
      break;
    }
  }while (!time_reached(until));
  if (!taskStarted) SetError(MFERR_TIMEOUT);
  
exit:
  return;
}


/////////////////////////////////////////////////////////////
// Return TFTP Transfer Status
// This function returns binary status data in parameter buffer.
// To reduce the code size of Control Panel, it also returns
// pre-formatted text strings in data buffer.
// Transfer mode is set to linear
//
// Parameter Input:
//   Version    = 0   version number of the requested data structure
//   Reserved   = 0   reserved for future use
//   Progress Bar Max Value
//
// Parameter Output:
//   Completed Flag =0 running, =1 completed successfully, =-1 completed with error
//   ProgressBar Value ==255 if the value cannot be determined and the progress bar should not be shown.
//   TFTP Status as defined in tftp_status_t
//   TFTP ErrorCode as defined in tftp_error_t
//   Block Transferred (Low Byte)
//   Block Transferred (Middle Byte 1)
//   Block Transferred (Middle Byte 2)
//   Block Transferred (High Byte)
//   Number of Retries (Low Byte)
//   Number of Retries (High Byte)
//   Elapsed Time in s (Low Byte)
//   Elapsed Time in s (High Byte)
//
// Data Buffer Output: Five Text Strings are returned
//    Status Text Message
//    Blocks Message
//    Retransmit
//    Elapsed Time
//    Error Message
//
// Note: Block Transferred can be 65536. So, it is a 32-bit integer
static void DoTFTPStatus() {
  ClearError(); //Assume no error
  
  //Only PicoW has network function
  if (!CheckPicoW()) {
    SetError(MFERR_NOTPICOW);
    goto exit;
  }
  
  //Check Version Byte ==0
  if (parameterBuffer[0]!=0) {
    SetError(MFERR_INVALIDARG);
    goto exit;
  }

  uint32_t pbMaxValue = parameterBuffer[2];
  
  //Clear DataBuffer in background
  //DONT use DMA. It causes conflict with flash.c
  memset(dataBuffer,0,DATABUFFERSIZE);
  
  //Variables to store TFTP State
  uint32_t elapsedTime;       //elapsed time in s
  uint32_t blockTransferred;  //Number of block sent/received
  uint32_t tsize;             //size of the file being received in bytes
  uint32_t retries;           //Number of Retries
  int32_t error;              //error code
  uint8_t status;             //status code

  //Take a snapshot of current TFTP State  
  tftp_critical_section_enter_blocking();
  blockTransferred = tftp_state.blockTransferred;
  tsize = tftp_state.tsize;
  retries = tftp_state.retries;
  error = tftp_state.error;
  status = tftp_state.status;
  tftp_critical_section_exit();
  elapsedTime = (get_absolute_time()-tftp_state.startTime)/1000000ull;
  
  //Write binary status to parameter buffer
  if (status==TFTPSTATUS_COMPLETED) parameterBuffer[0]=error==TFTPERROR_NOERR?1:-1;
  else parameterBuffer[0] = 0; //running
  parameterBuffer[1] = TFTPCalcProgressBarValue(pbMaxValue,blockTransferred,tsize); //Progress Bar Value
  parameterBuffer[2] = (uint8_t) status;
  parameterBuffer[3] = (int8_t) error;
  uint32_t blocks = blockTransferred;
  parameterBuffer[4] = (uint8_t)blocks; blocks>>=8;
  parameterBuffer[5] = (uint8_t)blocks; blocks>>=8;
  parameterBuffer[6] = (uint8_t)blocks; blocks>>=8;
  parameterBuffer[7] = (uint8_t)blocks;
  uint16_t retries16 = MIN(retries,0xffff);         //limit it to 16-bit integer  
  parameterBuffer[8] = (uint8_t) retries16;
  parameterBuffer[9] = (uint8_t) (retries16>>8);
  uint16_t elapsedTime16 = MIN(elapsedTime,0xffff); //limit it to 16-bit integer  
  parameterBuffer[10] = (uint8_t) elapsedTime16;
  parameterBuffer[11] = (uint8_t) (elapsedTime16>>8);
  
  //Write text string message to data buffer
  char* dest = dataBuffer;
  dest = TFTPFormatStatusMessage(dest,status,error); 
  dest = TFTPFormatBlocksMessage(dest,blockTransferred,tsize);
  dest = TFTPFormatRetransmit(dest,retries);
  dest = TFTPFormatElapsedTime(dest,elapsedTime);
  dest = TFTPFormatErrorMessage(dest,error);
  
exit:  
  dataBufferTransferMode = MODE_LINEAR; 
  ResetDataPointer();
  ResetParamPointer();  
}


/////////////////////////////////////////////////////////////
// Get last used server hostname from user config and copy
// the result to dataBuffer
//
// Data Buffer Output:
//    char* hostname - Last used server hostname
//
static void DoTFTPGetLastServer() {
  strcpy((char*)dataBuffer,GetTFTPLastServer());  
  dataBufferTransferMode = MODE_LINEAR; 
  ResetDataPointer();
}

/////////////////////////////////////////////////////////////
// Save last used server hostname from data buffer to user config
//
// Data Buffer Input:
//    char* hostname - Last used server hostname
//
static void DoTFTPSaveLastServer() {
  char* hostname = dataBuffer;
  SaveTFTPLastServer(strtrim(hostname));  
  ClearError();
}


/********************************************************************/



//////////////////////////////////////////////////////
// Execute command from Apple
//
// Input: command code
//
void __no_inline_not_in_flash_func(DoCommand)(const uint32_t command) {
  TurnOnActLed();
  switch(command) {
    case CMD_RESETBOTHPTRS:
      ResetDataPointer();
      ResetParamPointer();
      ClearError();
      break;
    case CMD_RESETDATAPTR:
      ResetDataPointer();
      ClearError();
      break;
    case CMD_RESETPARAMPTR:
      ResetDataPointer();
      ClearError();
      break;
    case CMD_MODELINEAR:
      dataBufferTransferMode = MODE_LINEAR;
      ResetDataPointer();
      ClearError();
      break;
    case CMD_MODEINTERLEAVED:
      dataBufferTransferMode = MODE_INTERLEAVED;
      ResetDataPointer();
      ClearError();
      break;    
    case CMD_COLDSTART:
      DoAppleColdStart();
      break;
    case CMD_ENABLEROMDISK:
      EnableRomdisk();
      ClearError();
      break;
    case CMD_DISABLEROMDISK:
      DisableRomdisk();
      ClearError();
      break;
    case CMD_LOAD_CPANEL:
      DoLoadCPanel();
      break;
    case CMD_TESTWIFI:
      DoTestWifi();
      break;      
    case CMD_GETINFOSTR:
      DoGetInfoString();
      break;
    case CMD_GETDEVINFO:
      DoGetDeviceInfo();
      break;
    case CMD_GETDEVSTATUS:
      DoGetDeviceStatus();
      break;
    case CMD_GETUNITSTATUS:
      DoGetUnitStatus();
      break;
    case CMD_GETDIB:
      DoGetDIB();
      break;
    case CMD_GETVOLINFO:
      DoGetVolumeInfo();
      break;
    case CMD_READBLOCK:
      DoReadBlock();
      break;
    case CMD_WRITEBLOCK:
      DoWriteBlock();
      break;
    case CMD_GETPRODOSTIME:
      DoGetProdosTime();
      break;
    case CMD_GETPRODS25TIME:
      DoGetProdos25Time();
      break;
    case CMD_GETTIMESTR:
      DoGetTimeString();
      break;
    case CMD_SETRTC_PRODOS:
      DoSetRTC_Prodos();
      break;
    case CMD_SETRTC_PRODOS25:  
      DoSetRTC_Prodos25();
      break;
    case CMD_WRITEBLOCKSIZETOVDH:
      DoWriteBlockSizeToVDH();
      break;
    case CMD_FORMATDISK:
      DoFormatDisk();
      break;
    case CMD_ERASEDISK:
      DoEraseDisk();
      break;
    case CMD_SAVEUSERSETTINGS:  
      DoSaveUserSettings();    
      break;
    case CMD_GETUSERSETTINGS:
      DoGetUserSettings();
      break;
    case CMD_SAVEWIFISETTINGS:
      DoSaveWifiSettings();
      break;
    case CMD_GETCONFIGBYTES:
      DoGetConfigBytes();
      break;
    case CMD_ERASEUSERSETTINGS:
      DoEraseUserSettings();
      break;
    case CMD_ERASEWIFISETTINGS:
      DoEraseWifiSettings();
      break;
    case CMD_ERASEADVSETTINGS:  
      DoEraseAdvancedSettings();
      break;
    case CMD_ERASEALLSETTINGS:
      DoEraseAllSettings();
      break;
    case CMD_DRIVEMAPPING:
      DoDriveMapping();
      break;
    case CMD_FADD:
      fadd(parameterBuffer);
      ResetParamPointer();
      ClearError();      
      break;
    case CMD_FMUL:
      fmul(parameterBuffer);
      ResetParamPointer();
      ClearError();
      break; 
    case CMD_FDIV:
      fdiv(parameterBuffer);
      ResetParamPointer();
      ClearError();
      break;
    case CMD_FSIN:
      fsin(parameterBuffer);
      ResetParamPointer();
      ClearError();
      break;
    case CMD_FCOS:
      fcos(parameterBuffer);
      ResetParamPointer();
      ClearError();
      break;   
    case CMD_FTAN:
      ftan(parameterBuffer);
      ResetParamPointer();
      ClearError();
      break;    
    case CMD_FATN:
      fatn(parameterBuffer);
      ResetParamPointer();
      ClearError();
      break;
    case CMD_FLOG:
      flog(parameterBuffer);
      ResetParamPointer();
      ClearError();    
      break;
    case CMD_FEXP:
      fexp(parameterBuffer);
      ResetParamPointer();
      ClearError();    
      break;
    case CMD_FSQR:
      fsqr(parameterBuffer);
      ResetParamPointer();
      ClearError();    
      break;
    case CMD_FOUT:
      fout(parameterBuffer);
      ResetParamPointer();
      ClearError();    
      break;      
    case CMD_RESETTIMER_US:
      DoResetTimer_us();
      break;
    case CMD_GETTIMER_US:
      DoGetTimer_us();
      break;
    case CMD_RESETTIMER_MS:
      DoResetTimer_ms();
      break;
    case CMD_GETTIMER_MS:
      DoGetTimer_ms();
      break;    
    case CMD_RESETTIMER_S:
      DoResetTimer_s();
      break;
    case CMD_GETTIMER_S:
      DoGetTimer_s();
      break;            
    case CMD_TFTPRUN:
      DoTFTPRun();
      break;
    case CMD_TFTPSTATUS:
      DoTFTPStatus();
      break;
    case CMD_TFTPGETLASTSERVER:
      DoTFTPGetLastServer();
      break;
    case CMD_TFTPSAVELASTSERVER:
      DoTFTPSaveLastServer();
      break;
    default:
      SetError(MFERR_UNKNOWNCMD);
  }
  TurnOffActLed();
}


