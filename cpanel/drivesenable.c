#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include "textstrings.h"
#include "defines.h"
#include "ui-menu.h"
#include "ui-wnd.h"
#include "ui-textinput.h"
#include "ui-misc.h"
#include "asm.h"


//
// Defined in main.c
//
extern UserSettings_t config;
extern bool clean;

// Position and size of window
#define XPOS 1
#define YPOS 6
#define WIDTH 38
#define HEIGHT 15

//
//static Global Variables 
static bool enableFlags[9]; //Enable Flag of each drive
static bool ramdiskEnableFlag;
static uint8_t i;           //Loop counter and array index


/////////////////////////////////////////////////////////////////////
// Draw Window Frame
//
static void DrawWindowFrame() {
  static const char windowTitle[]  = "Drives Enable";
  wnd_DrawWindow(XPOS,YPOS,WIDTH,HEIGHT,windowTitle,true,true);  
}

///////////////////////////////////////////////////////////////////////
// Unpack fd_enableflags in config variable to enableFlags array
// and copy RAMDISKFLAG bit from configbyte1 to ramdiskEnableFlag
//
static void UnpackFlags() {
  static_local uint8_t flags; 
  
  flags = config.fd_enableflags;
  for(i=8;i!=0;--i) {
    enableFlags[i] = (flags & 0x80);
    flags <<=1;
  }
  ramdiskEnableFlag = (config.configbyte1 & RAMDISKFLAG);
}

/////////////////////////////////////////////////////////////////////
// Pack enableFlags array to fd_enableFlags in config variable and
// copy ramdiskEnabelFlag to RAMDISKFLAG bit of configbyte1
//
static void PackFlags() { 
  config.fd_enableflags = enableFlags[8]?0x01:0x00;
  for(i=7;i!=0;--i) {
    config.fd_enableflags <<=1;
    if (enableFlags[i]) config.fd_enableflags |= 0x01;
  }  

  config.configbyte1 &= (~RAMDISKFLAG);
  if (ramdiskEnableFlag) config.configbyte1 |= RAMDISKFLAG;
}


////////////////////////////////////////////////////////////////////////
// Print or Clear the checkbox
//
// Input: index   - Index of checkbox (1-N)
//        checked - Checked or not
//
static void PrintCheckbox(uint8_t index,bool checked) {
  gotoxy(32,index);
  cputc(checked?'\304':' ');  //Tick or space
}


////////////////////////////////////////////////////////////////////
//  Main routine of Drives Enable function
//
void DoDrivesEnable() {
  static_local uint8_t unitCount;
  static_local unsigned char key;
  static_local bool newFlag;
  static_local bool dirty; //=true if user has pressed any number keys to toggle any drives
  
  dirty = false;
  unitCount = GetUnitCount();
  UnpackFlags();
  
  //Draw window and its content
  DrawWindowFrame();
  PrintDriveList(unitCount);
  
  //Print Checkboxes
  for(i=unitCount-1;i!=0;--i) PrintCheckbox(i,enableFlags[i]);
  PrintCheckbox(unitCount,ramdiskEnableFlag);
  
  //Prompt Messages
  newline2();
  cprintf(" Press key 1-%d to toggle",unitCount);
  gotoxy(1,14);
  cputs(strCancelesc);
  gotox(31);
  cputs(strOK_Enter);
  
  //Event Loop
  do {
    key = cgetc_showclock();
    i = key - '0'; //Use i as array index variable    
    
    //Toggle RAMDisk
    if (i == unitCount) {
      //Toggle ramdiskEnableFlag;
      ramdiskEnableFlag = !ramdiskEnableFlag;
      PrintCheckbox(unitCount,ramdiskEnableFlag);
      dirty = true;
      continue;
    }
    
    //Toggle Flash drives
    if (i>=1 && i<unitCount) {
      newFlag = !enableFlags[i];
      enableFlags[i] = newFlag;
      PrintCheckbox(i, newFlag);
      dirty = true;
      continue;
    }
    
    //Enter Key
    if (key==KEY_ENTER) {
      if (dirty) {
        clean = false;
        PackFlags();  //Pack the flags and save to config variable   
      }
      break;    //exit the loop
    }
  }while (key != KEY_ESC);
}
