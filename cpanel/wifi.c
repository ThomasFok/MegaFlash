#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <stdint.h>
#include <stdbool.h>
#include "defines.h"
#include "ui-menu.h"
#include "ui-wnd.h"
#include "ui-misc.h"
#include "ui-textinput.h"
#include "textstrings.h"
#include "asm.h"

// Position and size of WIFI setting window
#define XPOS 1
#define YPOS 6
#define WIDTH 38
#define HEIGHT 15
#define PASSPHRASEWIDTH 36 //Width of Passphrase multiline input
static char wifiTitle[]    = "WIFI Setting (1/2)";

/////////////////////////////////////////////////////////////////////
// Draw WIFI Setting Window Frame
// 
// Input: page - '1' or '2' for page number display
//        isActive - The window is active
//        clearContentArea - To clear the content area
//
static void DrawWifiWindowFrame(char page,bool isActive,bool clearContentArea) {
  wifiTitle[14]=page;  
  wnd_DrawWindow(XPOS,YPOS,WIDTH,HEIGHT,wifiTitle,isActive,clearContentArea);  
}


static char wifiPrompt1[]  = "For security, current WIFI setting\n\rcannot be retrieved.";
static char wifiPrompt2[]  = "Enter new setting to proceed.";
static char wifiPrompt3[]  = "Network Name (SSID):";
static char wifiPrompt4[] = "(        . case sensitive)";

/////////////////////////////////////////////////////////////////////
// Draw WIFI Setting Window Page 1
// 
void DrawWifiWindowPage1() {
  DrawWifiWindowFrame('1',true,true);

  cputs(wifiPrompt1);
  newline2();
  cputs(wifiPrompt2);  
  
  newline2();
  cputs(wifiPrompt3);
  gotoxy(1,HEIGHT-1);
  cputs(strEditPrompt);  

  gotoxy(0,8);
  cputs(wifiPrompt4);
  gotox(1);
  cputs(strrequired);  
}

static char wifiPrompt6[] = "WPA2/WPA3 AES Personal (PSK)\n\rauthentication is used to connect\n\rto WIFI network.";
static char wifiPrompt7[] = "To connect to an open network,\n\rleave the Passphrase blank.";
static char wifiPrompt8[] = "WIFI Passphrase(WPA Pre-Shared Key):";

/////////////////////////////////////////////////////////////////////
// Draw WIFI Setting Window Page 2
// 
void DrawWifiWindowPage2() {
  DrawWifiWindowFrame('2',true,true);
  
  cputs(wifiPrompt6);
  newline2();
  cputs(wifiPrompt7);
  newline2();
  cputs(wifiPrompt8);
  
  gotoxy(1,HEIGHT-1);  
  cputs(strEditPrompt);  
  gotox(28);
  cputs(strSave);  
  
  gotoxy(0,11);
  cputs(wifiPrompt4);
  gotox(1);
  cputs(stroptional);
}

static char savedPrompt1[]="New WIFI Setting has been saved.";
static char savedPrompt2[]="Network Name:";
static char savedPrompt3[]="WIFI Passphrase:";


/////////////////////////////////////////////////////////////////////
// Draw the content WIFI Saved window
//
// Input ssid,password - New WIFI setting
//       success - Saving of setting is successful
// 
void DrawWifiSavedWindowContent(char* ssid,char* password,bool success) {
  if (success) {
    gotoxy00();    
    cputs(savedPrompt1);
    newline2();
    cputs(savedPrompt2);
    newline();
    cputs(ssid);
    newline2();
    cputs(savedPrompt3);    
    newline();
    
    PrintStringTwoLines((char*)password,PASSPHRASEWIDTH);
  } else {
      newline2();
      cputs(strError);
  }
  
  gotoxy(26,14);
  cputs(strOKAnyKey);
}

/////////////////////////////////////////////////////////////////////
// The routine to drive the WIFI setting process
//
void DoWifiSetting() {
  static_local bool success;  
  static_local WifiSetting_t setting;  
  
  ZeroMemory(sizeof(WifiSetting_t),&setting);
  setting.version = WIFISETTINGVER;
  setting.checkbyte = WIFISETTINGVER ^ WIFISETTING_CHKBYTECOMP;
  setting.authType = WIFIAUTHTYPE;
  #if 0 //Already set to 0 by ZeroMemory()
  setting.options = 0;  //Reserved for future use
  setting.ipaddr = 0;   //Reserved for future use
  setting.netmask = 0;  //Reserved for future use
  setting.gateway = 0;  //Reserved for future use
  setting.dns = 0;      //Reserved for future use
  #endif
  
  //
  //Display First Page
  //
  DrawWifiWindowPage1();    

again:
  //Ask user to enter SSID
  ti_textBuffer[0]='\0';
  if (!ti_EnterText(SSIDLEN,0,7,-1)) return;
  
  //SSID cannot be empty
  if (ti_textBuffer[0]=='\0') {
    beep();
    goto again;
  }
  strcpy(setting.ssid,ti_textBuffer);

  //
  //Display Second Page
  //
  DrawWifiWindowPage2();  

  //Ask user to enter WIFI password
  ti_textBuffer[0]='\0';
  if (!ti_EnterText(WPAKEYLEN,0,9,PASSPHRASEWIDTH)) return;     //Esc key pressed
  strcpy(setting.wpakey,ti_textBuffer);
  
  //Deactivate WIFI window  
  DrawWifiWindowFrame('2',false,false); 
  
  //
  //Show results
  //
  DrawWifiWindowFrame('2',true,true);
  cputs(strSaving);
  
  //Save setting to MegaFlash
  success = SaveSetting(CMD_SAVEWIFISETTINGS,(uint8_t)sizeof(WifiSetting_t),&setting);  

  //Show the result
  DrawWifiSavedWindowContent(setting.ssid,setting.wpakey,success);
  
  //Wait the user to acknowledge
  cgetc_showclock();
}
