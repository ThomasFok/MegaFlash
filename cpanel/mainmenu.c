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
#include "mainmenu.h"
#include "timezone.h"
#include "wifi.h"
#include "testwifi.h"
#include "format.h"
#include "tftp.h"
#include "drivesenable.h"


//
// Defined in main.c
//
extern UserSettings_t config;
extern bool isAppleIIcplus;
extern bool isWifiSupported;
extern bool isFPUSupported;
extern bool clean;

//
// Constants
//
static const char strChecked[]    ="(\304)";
static const char strNotChecked[] ="( )";

static char mmTitle[] = "MegaFlash Control Panel";
static char mmPrompt[] ="Exit:esc     Select:\310 \325 \312 \313 \315";

enum COMMANDS {
  ID_POWERONSPEED,
  ID_BOOTMF,
  ID_FPU,
  ID_NTPCLIENT,
  ID_TIMEZONE,
  ID_DRIVESENABLE,
  ID_WIFISETTINGS,
  ID_TESTWIFI,
  ID_TFTP,
  ID_FORMAT,
  ID_ERASESETTINGS,
  ID_SAVEANDREBOOT
};

static char* mainMenuItems[] = {
  "Power on CPU Speed",
  "Boot MegaFlash",
  "Applesoft BASIC FPU",  
  "Network Time Sync",
  "Time Zone",
  "Drives Enable >\0",//When Wifi Settings is removed, we need to move the /n char to this item. \0 is the placeholder of the /n char
  "Wifi Settings >\n",
  
  "Test Wifi/NTP >",
  "Disk Image Transfer via WIFI >",
  "Format >",
  "Erase All Settings\n",
  
  "Save and Reboot"
};
static uint8_t mainMenuIDs[] = {
  ID_POWERONSPEED,
  ID_BOOTMF,
  ID_FPU,
  ID_NTPCLIENT,
  ID_TIMEZONE,
  ID_DRIVESENABLE,
  ID_WIFISETTINGS,
  ID_TESTWIFI,
  ID_TFTP,
  ID_FORMAT,
  ID_ERASESETTINGS,
  ID_SAVEANDREBOOT
};
#define MMITEMCOUNT (sizeof(mainMenuItems)/sizeof((mainMenuItems)[0]))
static uint8_t mainMenuItemCount = MMITEMCOUNT;


//Window Position and Size
#define XPOS 4
#define YPOS 3
#define WIDTH 32
#define HEIGHT 18

//Menu Position
#define MENU_XPOS 0
#define MENU_YPOS 0

static void ShowCPUSpeed() {
  gotox(24);
  if (config.configbyte1 & CPUSPEEDFLAG) cputs("Normal");
  else cputs("  Fast");
}

static void ToggleCPUSpeed() {
  clean = false;
  config.configbyte1 = config.configbyte1 ^ CPUSPEEDFLAG;
  ShowCPUSpeed();
}

static void ShowAutoBoot() {
  gotox(27);
  if (config.configbyte1 & AUTOBOOTFLAG) cputs(strChecked);
  else cputs(strNotChecked);
}

static void ToggleAutoBoot() {  
  clean = false;
  config.configbyte1 = config.configbyte1 ^ AUTOBOOTFLAG;
  ShowAutoBoot();
}

static void ShowFPU() {
  gotox(27);
  if (config.configbyte1 & FPUFLAG) cputs(strChecked);
  else cputs(strNotChecked); 
}

static void ToggleFPU() {
  clean = false;    
  config.configbyte1 = config.configbyte1 ^ FPUFLAG;
  ShowFPU();
}

static void ShowNTPClient() {
  gotox(27);
  if (config.configbyte1 & NTPCLIENTFLAG) cputs(strChecked);
  else cputs(strNotChecked);  
}  
  
static void ToggleNTPClient() {
  clean = false;  
  config.configbyte1 = config.configbyte1 ^ NTPCLIENTFLAG;
  ShowNTPClient();  
}
  
static void ToggleTimezone(uint8_t key) {
  gotox(21);
  if (key!=0) clean = false;
  DoToggleTimezone(key);  
}

static void ShowTimezone() {
  ToggleTimezone(0);  //0 means refresh the screen without changing the time zone
}

static void ShowAllOptions() {
  //The order must be the same as mainMenuItems
  gotoxy(0,MENU_YPOS);
  if (isAppleIIcplus) {
    ShowCPUSpeed(); cursordown();
  }
  ShowAutoBoot(); cursordown();
  if (isFPUSupported) {
    ShowFPU(); cursordown();
  }
  if (isWifiSupported) {
    ShowNTPClient(); cursordown();
    ShowTimezone();
  }
}

static void DrawMainMenuWindowFrame(bool isActive) {
  wnd_DrawWindow(XPOS,YPOS,WIDTH,HEIGHT,mmTitle,isActive,false);
}

///////////////////////////////////////////////////////////
// To remove an item from menu
//
// Input: uint8_t item - The array index of the item to be removed.
//
// Note: Remove menu items in reverse order. ie. from the highest 
// array index to 0. Then, we can use the ID_XXXX constants to identify
// the item.
static void RemoveMenuItem(uint8_t item) {
  uint8_t i;  //change to static_local does not reduce code size
  
  //Use MMITEMCOUNT constant instead of mainMenuItemCount
  //to reduce code size
  for(i=item;i<MMITEMCOUNT-1;++i) {
    mainMenuItems[i] = mainMenuItems[i+1];
    mainMenuIDs[i] = mainMenuIDs[i+1];
  }
  --mainMenuItemCount;
}

void DoMainMenu() {
  static_local bool redrawAll = true;
  static_local uint8_t key; 
  static_local uint8_t selectedItem;
  static_local int8_t reply;

  //
  //Remove unwanted menu items. 
  //The order is from highest ID number to 0
  if (!isWifiSupported) {
    RemoveMenuItem(ID_TFTP);
    RemoveMenuItem(ID_TESTWIFI);
    mainMenuItems[ID_DRIVESENABLE][strlen(mainMenuItems[ID_DRIVESENABLE])]='\n'; //move the new line char from WIFI Settings to FPU
    RemoveMenuItem(ID_WIFISETTINGS);
    RemoveMenuItem(ID_TIMEZONE);
    RemoveMenuItem(ID_NTPCLIENT);
  }
  if (!isFPUSupported) {
    RemoveMenuItem(ID_FPU);
  }
  if (!isAppleIIcplus) {
    RemoveMenuItem(ID_POWERONSPEED);
  }
  
  selectedItem = 0;
  do{
    if (redrawAll) {
      redrawAll=false;
      wnd_ResetScrollWindow();
      clrscr();
      DrawMainMenuWindowFrame(true);
      gotoxy(0,17);
      cputs(mmPrompt);
      ShowAllOptions();
      DisplayTime();
    }

    do {
      //Restore current item
      mnu_currentMenuItem = selectedItem;

      key=DoMenu((const char**)mainMenuItems,mainMenuItemCount,MENU_XPOS,MENU_YPOS);
      
      //Escape Key
      //Exit directly if no settings are changed.
      //Otherwise, Ask user to save the settings
      if (key==KEY_ESC) {
        if (clean) return;  //Exit to boot menu if no setting is changed
        
        //Ask user to save
        DrawMainMenuWindowFrame(false);  //Deactivate Main Menu Window     
        reply=ShowSaveBeforeExitDialog();
        if (reply<0) { //-1:Cancel Exit
          redrawAll = true;    
          continue;        
        }
        else if (reply==0){ //0:Don't Save
          return;   //Exit Control Panel and return to boot menu
        } 
        else{ //+1:Save
          SaveConfigReboot();   //No return. Reboot after saving
        }
      }

      //Save current Item since mnu_currentMenuItem may be changed by
      //command handler such as ShowEraseSettingsDialog()
      selectedItem = mnu_currentMenuItem;

      //Move the cursor to selected item
      gotoy(selectedItem+MENU_YPOS);

      //Convert selected menu item to ID
      switch (mainMenuIDs[selectedItem]) {
        case ID_POWERONSPEED:
          ToggleCPUSpeed();
          break;
        case ID_BOOTMF:
          ToggleAutoBoot();
          break;
        case ID_FPU:
          ToggleFPU();
          break;
        case ID_NTPCLIENT:
          ToggleNTPClient();
          break;
        case ID_TIMEZONE:
          ToggleTimezone(key);
          break;
        case ID_DRIVESENABLE:
          if (key!=KEY_ENTER) break;     
          DrawMainMenuWindowFrame(false);  //Deactivate Main Menu Window 
          redrawAll = true;
          DoDrivesEnable();
          break;          
        case ID_WIFISETTINGS:
          if (key!=KEY_ENTER) break;
          DrawMainMenuWindowFrame(false);  //Deactivate Main Menu Window 
          redrawAll = true;        
          DoWifiSetting();
          break;
        case ID_TESTWIFI:
          //The Test window covers the main menu window completely.
          //No need to Deactivate main menu window 
          if (key!=KEY_ENTER) break;          
          DrawMainMenuWindowFrame(false);  //Deactivate Main Menu Window                    
          redrawAll = true;    
          DoTestWifi();
          break;
        case ID_TFTP:  
          if (key!=KEY_ENTER) break;             
          DrawMainMenuWindowFrame(false);  //Deactivate Main Menu Window 
          DoTFTPImageTransfer();
          redrawAll = true;   
          break;
        case ID_FORMAT:
          if (key!=KEY_ENTER) break;       
          DrawMainMenuWindowFrame(false);  //Deactivate Main Menu Window           
          redrawAll = true;       
          DoFormat();
          break;
        case ID_ERASESETTINGS:
          if (key!=KEY_ENTER) break;     
          DrawMainMenuWindowFrame(false);  //Deactivate Main Menu Window 
          redrawAll = true;
          ShowEraseSettingsDialog();
          break;
        case ID_SAVEANDREBOOT:
          if (key!=KEY_ENTER) break;          
          
          if (clean) Reboot();    //No return.
          SaveConfigReboot();     //No return. Save then reboot      
                                  //Save 5 bytes if 'else' is omitted.
          break;
     }  
    }while(redrawAll==false);
  }while(1);
}
