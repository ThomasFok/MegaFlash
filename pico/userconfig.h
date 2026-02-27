#ifndef _USERPREF_U
#define _USERPREF_U

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "../common/defines.h"

//To save security register memory, the length of these settings are limited to 47
#define TFTP_LASTSERVERLEN         47
#define NTPSERVERORLEN             47

//Default Values of some settings
#define FLASHDRIVENABLEFLAGDEFAULT       255


/************************************************************************
This data structure is written to Flash security registers as user configuration
The structure is divided to four parts
1) Magic Word - To identify the data in Flash is a valid structure. It
is also the version identifier of the structure

2) User Settings - For saving Control Panel Setting. 

3) Wifi Settings - For Wifi Settings (ssid, wpakey...)

4) Advanced Settings - Currently, no command gives access to these settings.

User Settings and Wifi Settings can be individually erased by commands from Apple
************************************************************************/
typedef struct {
  uint32_t magic;
  
  //User Settings
  uint8_t user_configbyte1;
  uint8_t user_configbyte2;
  uint8_t user_timezoneidver;
  uint8_t user_timezoneid;
  
  //Wifi Settings
  uint32_t wifi_ipaddr;   //For Manual IP Setting
  uint32_t wifi_netmask;  //For Manual IP Setting
  uint32_t wifi_gateway;  //For Manual IP Setting
  uint32_t wifi_dns;      //For Manual IP Setting
  uint8_t wifi_options;   //For Manual IP Setting, currently=0
  uint8_t wifi_authType;  //currently = 0
  char wifi_wpakey[WPAKEYLEN+1];
  char wifi_ssid[SSIDLEN+1];
  uint8_t wifi_padding[1];  //for 32-bit alignment
  
  //Advanced Settings
  uint16_t tftp_serverport;   //TFTP server port
  uint16_t tftp_timeout;      //TFTP timeout in ms
  uint8_t tftp_maxattempt;    //TFTP maxattempt
  bool    tftp_enable1kblock; //TFTP enable1kblock
  char    tftp_lastserver[TFTP_LASTSERVERLEN+1]; //+1 for NULL char. TFTP Last Server Hostname/IP Addr
  char    ntpserver_override[NTPSERVERORLEN+1];  //+1 for NULL char. NTP Server Override  
  
  //User Settings (V2) (UserSettings_t version=2)
  uint8_t user2_fd_enableflags;     //Enable Flags of each individual flash drive
  
  
} Config_t;

//Note:
//In version 1.0, this variable is called maxflashdrive. It is the last member
//of Advanced Settings. It stores the maximum number of flash drive reported to 
//users. The default value is 0xff. But no user interface is developed to 
//utilize this variable.
//In version 1.1, this variable is repurposed as fd_enableflags.
//It now belongs to User Setting (V2) Group. But the actual position within
//Config_t is not changed. The default value is also 0xff. So, we don't need
//any change to the structure of Config_t



const char* GetSSID();
const char* GetWPAKey();
uint8_t GetConfigByte1();
uint8_t GetConfigByte2();
int32_t GetTimezoneOffset();
bool GetNTPClientEnabled();
uint16_t GetTFTPServerPort();
void SaveTFTPServerPort(const uint16_t port);
uint16_t GetTFTPTimeout();
void SaveTFTPTimeout(const uint16_t timeout);
uint8_t GetTFTPMaxAttempt();
void SaveTFTPMaxAttempt(const uint8_t maxattempt);
bool GetTFTPEnable1kBlockSize();
void SaveTFTPEnable1kBlockSize(const bool enable);
const char* GetTFTPLastServer();
void SaveTFTPLastServer(const char* hostname);
const char* GetNTPServerOverride();
bool SaveNTPServerOverride(const char* ntpserver);
uint8_t GetFlashdriveEnableFlag();

void LoadAllConfigs();
void SaveConfigs();
bool SaveUserSettings(const uint8_t *settingPtr);
bool SaveWifiSettings(const uint8_t *settingPtr);
void GetUserSettings(uint8_t* dest);
void EraseUserSettingsFromFlash();
void EraseWifiSettingsFromFlash();
void EraseAdvancedSettingsFromFlash();
void EraseAllSettingsFromFlash();

#ifdef __cplusplus
}
#endif

#endif

