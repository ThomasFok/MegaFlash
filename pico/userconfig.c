#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "userconfig.h"
#include "flash.h"
#include "encryption.h"
#include "misc.h"
#include "debug.h"
#include "tftp.h"


//
// Magic Word
//
//This integer is written to flash to indicate the setting in flash
//is the one we recognize. When settings are loaded, this magic value
//in flash is checked against this value. If they are not matched,
//the settings in flash are invalid. So, we initalize all settings
//to default.
#define MAGICVALUE         0x5e97724c

#define CONFIGBUFFERSIZE 512
uint8_t configBuffer[CONFIGBUFFERSIZE]; 

//pConfig is a pointer to config_t struct and the data are stored in configBuffer
#define pConfig ((Config_t*)configBuffer)


///////////////////////////////////////////////////////////////////////////////  
// 
//   Global Variables
//
///////////////////////////////////////////////////////////////////////////////  

//=true means the setting has not been written to flash
static bool settingsNotInFlash = false;  
  
  
///////////////////////////////////////////////
// Read and Decrypt configuration from Flash
//  
static void ReadDecryptConfigFromFlash() {
  ReadUserConfigBlock(configBuffer);
  Decrypt(configBuffer,configBuffer,CONFIGBUFFERSIZE);
}  
  
///////////////////////////////////////////////
// Encrypt and Write configuration from Flash
//    
static void EncryptWriteConfigToFlash() {
  TRACE_PRINTF("EncryptWriteConfigToFlash()\n");
  #ifdef PICO_RP2040  
  /* To reduce stack memory usage on RP2040, no extra buffer is used */
  Encrypt(configBuffer,configBuffer,CONFIGBUFFERSIZE);
  WriteUserConfigBlock(configBuffer);
  Decrypt(configBuffer,configBuffer,CONFIGBUFFERSIZE); //Restore the content of configBuffer
  #else
  uint8_t encryptBuffer[CONFIGBUFFERSIZE];  
  Encrypt(encryptBuffer,configBuffer,CONFIGBUFFERSIZE);
  WriteUserConfigBlock(encryptBuffer);  
  #endif
}
  
  

/////////////////////////////////////////////////////
// Init User Settings in user config
//
static void InitUserSettings() {
  pConfig->user_configbyte1 = DEFCFGBYTE1;  
  pConfig->user_configbyte2 = DEFCFGBYTE2;
  pConfig->user_timezoneidver = TIMEZONEIDVER;
  pConfig->user_timezoneid  = DEFAULTTIMEZONE;
  pConfig->user2_fd_enableflags = DEFFDENFLAGS;
}

/////////////////////////////////////////////////////
// Init Wifi Settings in user config 
//
static void InitWifiSettings() {
  pConfig->wifi_ipaddr = 0;
  pConfig->wifi_netmask = 0;
  pConfig->wifi_gateway = 0;
  pConfig->wifi_dns = 0;
  pConfig->wifi_options = WIFIOPTIONS;  
  pConfig->wifi_authType = WIFIAUTHTYPE;
  memset(pConfig->wifi_ssid,0,SSIDLEN+1);       //Clear old setting for security
  memset(pConfig->wifi_wpakey,0,SSIDLEN+1);     //Clear old setting for security  
}

/////////////////////////////////////////////////////
// Init Advanced Settings in user config
//
static void InitAdvancedSettings() {
  pConfig->tftp_maxattempt = TFTP_MAXATTEMPT_DEFAULT;
  pConfig->tftp_serverport = TFTP_SERVERPORT_DEFAULT;
  pConfig->tftp_timeout = TFTP_TIMEOUT_DEFAULT;
  pConfig->tftp_enable1kblock = TFTP_ENABLE1KBLOCK_DEFAULT;
  pConfig->tftp_lastserver[0]='\0';
  pConfig->ntpserver_override[0] ='\0';
}


//////////////////////////////////////////////
// If settingsNotInFlash is set, the setting in memory is just initialized
// to default value and has not been written to flash. 
// Call this function to write them to flash.
//
void SaveConfigs() {
  if (settingsNotInFlash) {
    settingsNotInFlash = false;
    EncryptWriteConfigToFlash();
  }
}

/////////////////////////////////////////////////////
// Load all configs from flash to memory
// If the data loaded from flash is invalid, initalize it
//
// Note: This function is called when the program starts up.
// We want this function to complete quickly so that we are
// ready to serve bus request from Apple. So, we DO NOT write
// anything to flash memory. If the setting in flash is invalid,
// we just initalize the setting in memory only. 
//
// After the bus loop has been running in another core, 
// SaveConfigs() function should be called to save 
// the settings from memory to flash. Since the flash routine
// is now thread-safe, we can call the function at any time from
// any core.
// 
void LoadAllConfigs() {
  //
  //Read Config from flash
  static_assert(sizeof(Config_t)<=CONFIGBUFFERSIZE);
  ReadDecryptConfigFromFlash();
  
  //
  //If the structure of Config_t is modified, we can upgrade the
  //old structure to the new one here
  
  //
  
  //Magic is valid?
  if (pConfig->magic != MAGICVALUE) {
    TRACE_PRINTF("LoadAllConfigs() Magic Not correct. Init all configs to to defaults\n");
    settingsNotInFlash = true;
    memset(configBuffer,0,CONFIGBUFFERSIZE);
    pConfig->magic = MAGICVALUE;
    InitUserSettings(); 
    InitWifiSettings();
    InitAdvancedSettings();    
  } else {
    settingsNotInFlash = false;
  }  
}


/////////////////////////////////////////////////////
// The function is called when Apple uploads new 
// wifi setting. The setting is copied into wifiSetting
// variable. Then, it is written to flash
//
// Input: settingPtr - Pointer to WifiSetting_t
//
// Output: success
//
bool SaveWifiSettings(void *settingPtr) {
  //
  //Check source data version
  WifiSetting_t *src = (WifiSetting_t*)settingPtr;
  if (src->version!=WIFISETTINGVER) {
    ERROR_PRINTF("SaveWifiSettings() version not recognized\n");
    return false;
  }  
  
  //
  //Check source data checkbyte
  if ((src->version^WIFISETTING_CHKBYTECOMP)!=src->checkbyte) {
    TRACE_PRINTF("SaveWifiSettings(): checkbyte not match\n");
    return false;
  }  
  
  //Clear old setting for security
  memset(pConfig->wifi_ssid,0,SSIDLEN+1);       
  memset(pConfig->wifi_wpakey,0,WPAKEYLEN+1);  
  
  //
  //Copy source setting to memory
  pConfig->wifi_authType = src->authType;
  pConfig->wifi_options = src->options;
  strncpy(pConfig->wifi_ssid,src->ssid,SSIDLEN);
  pConfig->wifi_ssid[SSIDLEN] = '\0';
  strncpy(pConfig->wifi_wpakey,src->wpakey,WPAKEYLEN);
  pConfig->wifi_wpakey[WPAKEYLEN] = '\0'; 
  
  //Current implementation is to ignore the value from Apple and
  //set them to 0.
  pConfig->wifi_ipaddr = 0;
  pConfig->wifi_netmask = 0;
  pConfig->wifi_gateway = 0;
  pConfig->wifi_dns = 0;
  
  //
  //Write to Flash
  EncryptWriteConfigToFlash();
  
  return true;
}



/////////////////////////////////////////////////////
// The function is called when Apple uploads new 
// user userConfig. The setting is copied into ConfigPico
// variable. Then, it is written to flash
//
// Input: configPtr - Pointer to UserSettings_t
//
// Output: success
//
bool SaveUserSettings(void *settingPtr) {
  //
  //Check source data version
  UserSettings_t *src = (UserSettings_t*)settingPtr;
  if (src->version!=USERSETTINGSVER || src->timezoneidver!=TIMEZONEIDVER) {
    ERROR_PRINTF("SaveUserSettings() version or timezoneidver not recognized\n");
    return false;  
  }
  
  //
  //Check source data checkbyte
  if ((src->version^USERSETTINGS_CHKBYTECOMP)!=src->checkbyte) {
    TRACE_PRINTF("SaveUserSettings(): checkbyte not match\n");
    return false;
  }
  
  //
  //Copy data to userConfig
  pConfig->user_configbyte1     = src->configbyte1;
  pConfig->user_configbyte2     = src->configbyte2;
  pConfig->user_timezoneidver   = src->timezoneidver;
  pConfig->user_timezoneid      = src->timezoneid;
  pConfig->user2_fd_enableflags = src->fd_enableflags;

  //Write to flash
  EncryptWriteConfigToFlash();
  
  return true;
}



/////////////////////////////////////////////////////
// Erase User Settings from Flash
//
void EraseUserSettingsFromFlash() {
  //Reset config in memory to default
  InitUserSettings();
  
  //Write to Flash
  EncryptWriteConfigToFlash();
}

/////////////////////////////////////////////////////
// Erase Wifi Settings from Flash
//
void EraseWifiSettingsFromFlash() {
  //Reset config in memory to default
  InitWifiSettings();
  
  //Write to Flash
  EncryptWriteConfigToFlash();
}


/////////////////////////////////////////////////////
// Erase Advanced Settings from Flash
//
void EraseAdvancedSettingsFromFlash() {
  //Reset config in memory to default
  InitAdvancedSettings();
  
  //Write to Flash
  EncryptWriteConfigToFlash();
}

/////////////////////////////////////////////////////
// Erase All Settings from Flash
//
void EraseAllSettingsFromFlash() {
  InitUserSettings(); 
  InitWifiSettings();
  InitAdvancedSettings();  
  
  //Write to Flash
  EncryptWriteConfigToFlash();  
}


//*****************************************
// Getter/Setter functions
//
const char* GetSSID() {
  return pConfig->wifi_ssid;
}

const char* GetWPAKey() {
  return pConfig->wifi_wpakey;
}

uint8_t GetConfigByte1() {
  //Clear NTP Client Flag if not running on PicoW
  //so that ProDOS clock driver is not installed.
  if (!CheckPicoW()) {
    return pConfig->user_configbyte1 & (~NTPCLIENTFLAG);
  }
  return pConfig->user_configbyte1;
}

uint8_t GetConfigByte2() {
  return pConfig->user_configbyte2;
}

bool GetNTPClientEnabled() {
  return ((pConfig->user_configbyte1 & NTPCLIENTFLAG)!=0);
}

////////////////////////////////////////////////
// Timezone Data
//
const int32_t tzhour[] = TZHOUR;
const int32_t tzmin[]  = TZMIN;


/////////////////////////////////////////////////////
// return Timezone Offset in seconds
//
// Output - Timezone Offset (e.g. UTC-1:00 = -3600)
//
int32_t GetTimezoneOffset() {
  int32_t hour = tzhour[pConfig->user_timezoneid];
  int32_t min  = tzmin [pConfig->user_timezoneid];
  
  int32_t offset = hour*60;     //offset in minutes

  if (hour<0) offset -= min;
  else        offset += min;
  
  return offset*60;             //offset in seconds
}


/////////////////////////////////////////////////////
// Copy user setting to a buffer
//
// Input: dest - Pointer to buffer to receive the data
//
void GetUserSettings(uint8_t* destBuffer) {
  UserSettings_t* dest = (UserSettings_t*)destBuffer;
  dest->version        = USERSETTINGSVER;
  dest->checkbyte      = USERSETTINGSVER ^ USERSETTINGS_CHKBYTECOMP;
  dest->configbyte1    = pConfig->user_configbyte1;
  dest->configbyte2    = pConfig->user_configbyte2;
  dest->timezoneidver  = pConfig->user_timezoneidver;
  dest->timezoneid     = pConfig->user_timezoneid;
  dest->fd_enableflags = pConfig->user2_fd_enableflags;
}

/////////////////////////////////////////////////////
// Get TFTP Server Port
//
// Output: uint16_t - TFTP Server port
//
uint16_t GetTFTPServerPort() {
  return pConfig->tftp_serverport;
}

/////////////////////////////////////////////////////
// Save TFTP Server Port setting
//
// Input: port - TFTP Server port
//
void SaveTFTPServerPort(const uint16_t port) {
  pConfig->tftp_serverport = port;
  EncryptWriteConfigToFlash();
}

/////////////////////////////////////////////////////
// Get TFTP timeout
//
// Output: uint16_t - TFTP Timeout
//
uint16_t GetTFTPTimeout() {
  return pConfig->tftp_timeout;
}

/////////////////////////////////////////////////////
// Save TFTP timeout setting
//
// Input: timeout - TFTP Timeout
//
void SaveTFTPTimeout(const uint16_t timeout) {
  pConfig->tftp_timeout = timeout;
  EncryptWriteConfigToFlash();
}

/////////////////////////////////////////////////////
// Get TFTP maxattempt
//
// Output: uint8_t - TFTP maxattempt
//
uint8_t GetTFTPMaxAttempt() {
  return pConfig->tftp_maxattempt;
}

/////////////////////////////////////////////////////
// Save TFTP maxattempt setting
//
// Input: maxattempt - TFTP maxattempt
//
void SaveTFTPMaxAttempt(const uint8_t maxattempt) {
  pConfig->tftp_maxattempt = maxattempt;
  EncryptWriteConfigToFlash();
}
 
/////////////////////////////////////////////////////
// Get TFTP Enable1kBlockSize
//
// Output: bool - TFTP enable1kblock
// 
bool GetTFTPEnable1kBlockSize() {
  return pConfig->tftp_enable1kblock;
}
 
 
/////////////////////////////////////////////////////
// Save TFTP Enable1kBlockSize
//
// Input: enable - TFTP enable1kblock
//
void SaveTFTPEnable1kBlockSize(const bool enable) {
  pConfig->tftp_enable1kblock = enable;
  EncryptWriteConfigToFlash();
}

/////////////////////////////////////////////////////
// Get TFTP Last Server Hostname/IPAddr
//
// Output: const char* - Last Server Hostname
// 
const char* GetTFTPLastServer() {
  return pConfig->tftp_lastserver;
}


/////////////////////////////////////////////////////
// Save Last TFTP Server Hostname/IPAddr
//
// Input: hostname 
//
void SaveTFTPLastServer(const char* hostname) {
  //If the length of hostname is too long, simply reject it
  //since this setting is for user convenience and not essential.
  if (strlen(hostname)>TFTP_LASTSERVERLEN) {
    WARN_PRINTF("SaveTFTPLastServer() hostname is too long to store into flash\n");
    return;
  }
  
  //Do nothing if the new hostname is same as the old one
  if (0==strcmp(hostname,pConfig->tftp_lastserver)) return;
  
  strncpy(pConfig->tftp_lastserver,hostname,TFTP_LASTSERVERLEN);
  pConfig->tftp_lastserver[TFTP_LASTSERVERLEN] = '\0';
  EncryptWriteConfigToFlash();
}


/////////////////////////////////////////////////////
// Get NTP Server Override
//
// Output: const char* - NTP Server
// 
const char* GetNTPServerOverride() {
  return pConfig->ntpserver_override;
}

/////////////////////////////////////////////////////
// SaveNTP Server Override
//
// Input: server - NTP Server
//
// Output: bool  - success
//
bool SaveNTPServerOverride(const char* ntpserver) {
  if (strlen(ntpserver)>NTPSERVERORLEN) {
    ERROR_PRINTF("SaveNTPServerOverride() NTP server hostname is too long to store into flash\n");
    return false;
  }  
  
  //Do nothing if the new ntpserver is same as the old one
  if (0==strcmp(ntpserver,pConfig->ntpserver_override)) return true;  
  
  strncpy(pConfig->ntpserver_override,ntpserver,NTPSERVERORLEN);
  pConfig->ntpserver_override[NTPSERVERORLEN]='\0';
  EncryptWriteConfigToFlash();
  return true;
}

/////////////////////////////////////////////////////
// Get Flash Drives Enable Flags
//
// Output: uint8_t - Flash Drives Enable Flags
// 
uint8_t GetFlashdriveEnableFlag(){
  return pConfig->user2_fd_enableflags;
}



