#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <conio.h>
#include "defines.h"
#include "asm.h"
#include "ui-misc.h"

//From main.c
extern UserSettings_t config;
extern bool isAppleIIe;

/////////////////////////////////////////////////////////////////////
// Validate User Config
//
// Output: true if config is valid.
//
static bool ValidateConfig() {
  if (config.version!=USERSETTINGSVER || (config.version^USERSETTINGS_CHKBYTECOMP)!=config.checkbyte || config.timezoneidver!=TIMEZONEIDVER) {
    return false;
  }
  return true;
}

#ifdef TESTBUILD
void InitConfig() {
  config.version     = USERSETTINGSVER;
  config.checkbyte   = USERSETTINGSVER ^ USERSETTINGS_CHKBYTECOMP;
  config.configbyte1 = DEFCFGBYTE1;  
  config.configbyte2 = DEFCFGBYTE2;
  config.timezoneidver = TIMEZONEIDVER;
  config.timezoneid  = DEFAULTTIMEZONE;
  config.fd_enableflags = DEFFDENFLAGS;
}
#endif  

/////////////////////////////////////////////////////////////////////
// Load User Config from MegaFlash
//
void LoadConfig() {
#ifdef TESTBUILD
  InitConfig();
  return;
#else  

  //Load User Config
  if (!LoadSetting(CMD_GETUSERSETTINGS, sizeof(UserSettings_t), &config)) FatalError(ERR_LOADSETTING_FAIL);

  //Validate it
  if (!ValidateConfig()) FatalError(ERR_CONFIG_INVALID);

#endif    
}


/////////////////////////////////////////////////////////////////////
// Save User Config to MegaFlash and then reboot
//
void SaveConfigReboot() {
  static_local bool success;
  
  //Check version, checkbyte and zoneverid
  if (!ValidateConfig()) {
    FatalError(ERR_CONFIG_INVALID);
  }
 
  //Save to MegaFlash
  success = SaveSetting(CMD_SAVEUSERSETTINGS,(uint8_t)sizeof(UserSettings_t),&config);  
  
  //We don't expect the saving would fail. So, no need to provide a fancy UI.
  if (!success) {
    FatalError(ERR_SAVECONFIGREBOOT_FAIL);
  }
  
  Reboot();
}