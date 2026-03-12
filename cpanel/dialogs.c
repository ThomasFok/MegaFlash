#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "defines.h"
#include "dialogs.h"
#include "ui-menu.h"
#include "ui-wnd.h"
#include "config.h"
#include "asm.h"
#include "textstrings.h"


//Working Variables
static uint8_t key;


/////////////////////////////////////////////////////////////
//
// Save before Exit Dialog
//
/////////////////////////////////////////////////////////////
#define SE_XPOS 9
#define SE_YPOS 9
#define SE_WIDTH 22
#define SE_HEIGHT 7
const char* strSaveBeforeExitTitle = "Exit";
static void DrawSaveBeforeExitWindow() {
  wnd_DrawWindow(SE_XPOS,SE_YPOS,SE_WIDTH,SE_HEIGHT,strSaveBeforeExitTitle,true,true);
}

const char* exitMenuItem[] = {
  "Save and Reboot",
  "Don't Save",
  "Cancel"  
};

//////////////////////////////////////////////////////////
// Ask user to save before exit
// return 1  if user selected Save
// return 0  if user selected Don't Save
// return -1 if user selected Cancel
int8_t ShowSaveBeforeExitDialog() {
  DrawSaveBeforeExitWindow();
  cputs("Save changes?");
  
  mnu_currentMenuItem=0;  //Default is Yes  
  do {
    key=DoMenu(exitMenuItem,3,2,2);
    if (key==KEY_ESC) return -1;  //cancel
  }while (key!=KEY_ENTER);  
  
  return 1-mnu_currentMenuItem;
}

#if 0
/////////////////////////////////////////////////////////////
//
// Save Confirmation Dialog
//
/////////////////////////////////////////////////////////////
#define SC_XPOS 10
#define SC_YPOS 10
#define SC_WIDTH 20
#define SC_HEIGHT 6
static void DrawSaveConfirmWindow() {
  wnd_DrawWindow(SC_XPOS,SC_YPOS,SC_WIDTH,SC_HEIGHT,strConfirm,true,true);
}


/////////////////////////////////////////////
// Confirm to save the setting and reboot
// Return true if user select Yes
//
bool ShowSaveConfirmDialog() {
  DrawSaveConfirmWindow();
  cputs("Save and Reboot?");

  mnu_currentMenuItem=0;  //Default is Yes  
  do {
    key=DoMenu(yesnoMenuItem,2,2,2);
    if (key==KEY_ESC) return false;
  }while (key!=KEY_ENTER);
  
  if (mnu_currentMenuItem == 0) {
    DrawSaveConfirmWindow();
    cputs(strSaving);
  }

  //true if mnu_currentMenuItem==0
  //mnu_currentMenuItem is either 0 or 1
  //Inverting bit 0 is equivalent to mnu_currentMenuItem==0
  return mnu_currentMenuItem^0x01;
}
#endif


/////////////////////////////////////////////////////////////
//
// Erase All Settings Dialog
//
/////////////////////////////////////////////////////////////
#define ES_XPOS 5
#define ES_YPOS 9
#define ES_WIDTH 30
#define ES_HEIGHT 7

static void DrawEraseSettingsWindow() {
  wnd_DrawWindow(ES_XPOS,ES_YPOS,ES_WIDTH,ES_HEIGHT,strConfirm,true,true);
}

//Erase all settings from Pico Flash Memory
void ShowEraseSettingsDialog() {
  DrawEraseSettingsWindow();
  cputs("Reset all options to default\n\rand erase WIFI settings?");

  mnu_currentMenuItem=1;  //Default is No
  do {
    key=DoMenu(yesnoMenuItem,2,2,3);
    if (key==KEY_ESC) return;
  }while (key!=KEY_ENTER);
  
  if (mnu_currentMenuItem == 0) {
    DrawEraseSettingsWindow();    
    cputs(strErasing);
    
    EraseAllSettings(); 
    DriveMapping(false);  //EraseAllSettings() disable RAMDisk. Disable drive mapping to make sure we can access
                          //to all drives
    LoadConfig();   //Reload Config
    DisplayTime();  //Timezone may have changed. Update the clock
    
    cputs(strDone);  
    gotoxy(18,6);
    cputs(strOKAnyKey);
    cgetc();
  }
}


#if 0
/////////////////////////////////////////////////////////////
//
// PicoW Needed Dialog
//
/////////////////////////////////////////////////////////////
void ShowPicoWNeededDialog() {
  wnd_DrawWindow(5,9,30,5,strError,true,true);
  cputs("Wifi requires PicoW Board.");
  gotoxy(18,4);
  cputs(strOKAnyKey);
  cgetc();
}
#endif

