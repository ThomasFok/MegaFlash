#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <stdint.h>
#include <stdbool.h>
#include "defines.h"
#include "textstrings.h"
#include "ui-wnd.h"
#include "ui-misc.h"
#include "asm.h"
#include "config.h"
#include "mainmenu.h"

/******************************************************************
Build Types

Release: 
Output file is cpanel.bin. This file is included into Pico firmware.

Test Build:
Output file is cpanel.as (AppleSingle format). Then, this file is 
added to prodos19.dsk disk image for testing on emulator. Since 
Megaflash hardware does not exist on emulator, dummy test data are
used when the program tries to fetch information from Megaflash. 

MAME is used to test the program since it can emulate Apple IIc and
IIc+. It also support Slinky and serial port emulation.
******************************************************************/


//
//Global Variables
//
UserSettings_t config;
bool isAppleIIcplus;
bool isWifiSupported;
bool isFPUSupported;
uint8_t boardType;
bool clean;   //set to false if any setting is changed. Ask user to save when Esc key is pressed.


/////////////////////////////////////////////////////////////////////
// Reboot the machine
//
void Reboot() {
  ResetScreen();  //Reduce screen flickering
  ResetCPU();     //Reboot the machine
}


/////////////////////////////////////////////////////////////////////
// Fatal Error handler
//
void FatalError(uint8_t errorcode) {
  ResetScreen();
  cprintf("Unexpected Error:%d",errorcode);
  newline2();
  cputs(strPressanykeyto_);
  cputs(strreboot);
  beep();
  cgetc();
  Reboot();
}

//////////////////////////////////////////////////////////////////
// Show Device Info String in 80 column mode
//
void ShowDeviceInfoString() {
  set80(); 
  clrscr();
  //Request System Information
  if (!GetInfoString(INFOSTR_DEVICE)) FatalError(ERR_GETDEVINFOSTR_FAIL);  
  PrintStringFromDataBuffer();
  cprintf("Control Panel Build Timestamp = %s %s",__DATE__,__TIME__);
  newline2();
  cputs(strPressanykeyto_);
  cputs(strcontinue);
  cgetc();
  clrscr(); //Clearing the screen before switching to 40 reduces flickering
  set40();
}

void main() { 
  //Destory Reset Vector so that Ctrl-Reset reboot the machine.
  //It ensures the flash drive mapping are enabled after quitting from Control Panel.
  //When the machine reboots, CMD_COLDSTART command is sent. This command re-enables
  //the drive mapping.
  *(char*)0x3F3 = 0x00;
  *(char*)0x3F4 = 0x00;

#ifndef TESTBUILD
  //If the user reset the computer during data transfer, the transfer mode may be left at interleaved.
  //So, reset the mode to linear to avoid potential problem.
  SendCommand(CMD_MODELINEAR);

  //Make sure ROMDisk is disabled.
  SendCommand(CMD_DISABLEROMDISK);
  
  //Disable Drive Mapping so that we can acces to all drives including RAMDisk
  DriveMapping(false);    //false = disable
  
  //Read Pico Device Info
  SendCommand(CMD_GETDEVINFO);
  boardType = GetParam8Offset(5);                     //Read board type
  isWifiSupported = boardType & 0x80;                 //MSB set if Wifi is supported
  if (ReadOpenAppleButton()) ShowDeviceInfoString();  
#else
  boardType = BRD_PICO2W;
  isWifiSupported = true;
#endif  
  
  isAppleIIcplus = IsAppleIIcplus();
  isFPUSupported = HasFPUSupport();
  clean = true;
  LoadConfig();
  DoMainMenu();
	
  //Re-enable Drive mapping before quitting
  DriveMapping(true);   //true = enable
  
  ResetScreen();
	return;
}


