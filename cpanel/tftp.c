#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include "defines.h"
#include "tftp.h"
#include "ui-menu.h"
#include "ui-wnd.h"
#include "ui-textinput.h"
#include "ui-misc.h"
#include "ui-progressbar.h"
#include "textstrings.h"
#include "asm.h"

//
//Function Prototypes
static void TFTPStatusLoop();
static void gotoStatus();

//
// Position and size of Image Transfer window
//
#define XPOS 1
#define YPOS 6
#define WIDTH 38
#define HEIGHT 15

//
// Position and size of Progress Bar
//
#define PB_XPOS 0
#define PB_YPOS 11
#define PB_WIDTH 36

//
// Position of Error Message
//
#define ERR_XPOS 0
#define ERR_YPOS 10

//
//Length and width of text input control
//
//Note: Pico can accept hostname and filename up to 80 chars long as defined in
//TFTP_HOSTNAME_MAXLEN and TFTP_FILENAME_MAXLEN constants. For aesthetic reason,
//the length is limited to 72 chars (two lines)
#define HOSTNAME_LEN  72
#define HOSTNAME_WDTH 36
#define FILENAME_LEN  72
#define FILENAME_WDTH 36

/////////////////////////////////////////////////////////////////////
// Draw Format Window 
// 
// Input: title - Title String
//
static void DrawTFTPWindowFrame(const char* title) {
  wnd_DrawWindow(XPOS,YPOS,WIDTH,HEIGHT,title,true,true);  
}


/////////////////////////////////////////////////////////////////////
// Draw Format Window Frame with Page Title
// 
// Input: page - '1'- '4' for page number display
//
static void DrawTFTPWindowFramePage(char page) {
  static char tftpWindowTitle[]  = "TFTP Disk Image Transfer (1/4)";
  tftpWindowTitle[26]=page;  
  DrawTFTPWindowFrame(tftpWindowTitle);
}

//
// Page 1 Text Strings
//
static char page1Prompt[] = "Supported Disk Image Formats\n\n\rDownload: ProDOS Order (.po/.hdv)\n\r          DOS Order    (.do/.dsk)\n\r  Upload: ProDOS Order (.po/.hdv)\n\n\rPlease select:";
static char selectPrompt[] ="Cancel:esc            Select: \312 \313 \315";
static const char* tftpMenuItems[] = {
  "Download from TFTP server",
  "Upload to TFTP server"
};
#define TFTPMENUITEMCOUNT 2

//
// Page 3 Text Strings
//
static char hostnamePrompt[] = "Enter IP Addr or hostname of server";
static char filenamePrompt[] = "Enter the filename of the disk image";
static char caseSensitivityPrompt[] = "(Case sensitivity depends on file\n\rsystem of the TFTP server.)";

////////////////////////////////////////////////////////////
// Print Download Confirmation Message
//
static void PrintDownloadConfirmation(uint8_t selectedUnit) {
  static char prompt[]= "Download and write disk image file\n\rto drive %u. All existing data will\n\rBE ERASED.";
  cprintf(prompt,selectedUnit);
  newline2();
  
  //Print selected Drive Info
  PrintDriveInfo(selectedUnit);
  newline();
}

///////////////////////////////////////////////////////////////
// Print Upload Confirmation Message
//
static void PrintUploadConfirmation(uint8_t selectedUnit,char *filename) {
  static char prompt[]="Upload the content of drive %u to\n\rTFTP Server. The file on server may\n\rbe overwritten without warning if\n\rthe file already exists.";
                         
  cprintf(prompt,selectedUnit);
  newline2();
  
  //Print Remote Filename:
  cputs(strRemote_);
  cputs(strFilename);
  newline();
  PrintStringTwoLines(filename,FILENAME_WDTH);
  newline2();
  newline();
}


///////////////////////////////////////////////////////////////////////////////////
// The routine to drive the TFTP Disk Image Transfer
//
void DoTFTPImageTransfer() {
  #define DIR_RX 0
  #define DIR_TX 1
  
  static_local uint8_t key,dir,unitCount,selectedUnit,error;
  static_local char filename[FILENAME_LEN+1];
  static_local char hostname[HOSTNAME_LEN+1];
  
  ///////////////////////////////////////////////////////////
  //
  //    Page 1 - Download or Upload
  //
  ///////////////////////////////////////////////////////////  
  DrawTFTPWindowFramePage('1');
  cputs(page1Prompt);
  gotoxy(0,14);
  cputs(selectPrompt);
  
  mnu_currentMenuItem = 0;
  do {
    key = DoMenu(tftpMenuItems,TFTPMENUITEMCOUNT,0,8);
    if (key==KEY_ESC) return;
  }while (key!=KEY_ENTER);
  dir = mnu_currentMenuItem;  //0=Download, 1=Upload

  ///////////////////////////////////////////////////////////
  //
  //    Page 2 - Drive Number
  //
  ///////////////////////////////////////////////////////////  
  unitCount = GetUnitCount();
  if (unitCount==0) FatalError(ERR_UINTCOUNT_ZERO);  
  
  DrawTFTPWindowFramePage('2');  
  gotoxy(1,HEIGHT-1);
  cputs(strEditPrompt);
  
  gotoxy00();
  PrintDriveInfoList(unitCount);
  newline();  
  
  //
  //Enter Drive Number
  //
  strDriveNumberPrompt[16]=unitCount+'0';
  cputs(strDriveNumberPrompt);
  if (!ti_EnterNumber(1,1,unitCount)) return;
  selectedUnit = (uint8_t) ti_enteredNumber;
  
  ///////////////////////////////////////////////////////////
  //
  //    Page 3 - Hostname and Filename
  //
  ///////////////////////////////////////////////////////////  
  DrawTFTPWindowFramePage('3');  
  gotoxy(1,HEIGHT-1);
  cputs(strEditPrompt);
  
  //
  //Enter hostname or IP Address
  //  
  gotoxy00();
  cputs(hostnamePrompt);
  
  //Get Last Used Server Hostname
#ifndef TESTBUILD  
  SendCommand(CMD_TFTPGETLASTSERVER);
  CopyStringFromDataBuffer(ti_textBuffer);
#else
  ti_textBuffer[0]='\0';
#endif  

again: 
  #if HOSTNAME_LEN>TFTP_HOSTNAME_MAXLEN
  assert(0);
  #endif
  if (!ti_EnterHostname(HOSTNAME_LEN,0,2,HOSTNAME_WDTH)) return;
  if (ti_textBuffer[0]=='\0') {
    beep();
    goto again;
  }
  strcpy(hostname,ti_textBuffer);
  ti_ClearUnderscore();
  
  //
  //Enter filename
  //
  gotoxy(0,6);
  cputs(filenamePrompt);
  gotoxy(0,10);
  cputs(caseSensitivityPrompt);
  ti_textBuffer[0]='\0';

again2:
  #if FILENAME_LEN>TFTP_FILENAME_MAXLEN
  assert(0);
  #endif

  if (!ti_EnterText(FILENAME_LEN,0,8,FILENAME_WDTH)) return;
  if (ti_textBuffer[0]=='\0') {
    beep();
    goto again2;
  }  
  strcpy(filename,ti_textBuffer);
  
  //Copy hostname and filename to data buffer
  SendCommand(CMD_RESETDATAPTR);
  CopyStringToDataBuffer(hostname);
  CopyStringToDataBuffer(filename);
    
    
  ///////////////////////////////////////////////////////////
  //
  //    Page 4 - Confirm
  //
  ///////////////////////////////////////////////////////////  
  DrawTFTPWindowFramePage('4');  
  gotoxy(1,HEIGHT-1);
  cputs(strEditPrompt);  
  
  gotoxy00();
  if (dir == DIR_RX) PrintDownloadConfirmation(selectedUnit);
  else PrintUploadConfirmation(selectedUnit,(char*)ti_textBuffer); //ti_textBuffer still stores the filename
  
  //Ask user to type CONFIRM
  if (!AskUserToConfirm()) return;
  
  ///////////////////////////////////////////////////////////
  //
  //    Page 4 - Transfer
  //
  ///////////////////////////////////////////////////////////  
  
  //
  //Draw all static texts
  DrawTFTPWindowFrame(dir==DIR_RX?strTFTP_Download:strTFTP_Upload);
 
  cputs(strServer);
  cputs_n(hostname,WIDTH-2-7);   //7 is the length of strServer
  
  newline2();
  cputs(strFilename);
  cputs_n(filename,WIDTH-2-9);   //9 is length of strFilename   
  
  newline2();
  cputs(strStatus);
  
  newline2();
  cputs(strBlocks);
  
  newline2();
  cputs(strRetransmit);
  
  gotox(19);
  cputs(strTime);

  //
  //Send TFTPRun command
  gotoStatus();
  cputs(strStarting); 
  error = StartTFTP(01/*flag*/,dir,selectedUnit);
  if (error!=MFERR_NONE) {
    if (error=MFERR_TIMEOUT) {
      gotoStatus();
      cputs(strError);
      gotoxy(ERR_XPOS,ERR_YPOS);
      cputs("Error:\n\rNo response from MegaFlash");
    }
    else FatalError(ERR_TFTPRUN_FAIL); 
  }
  
  //
  //Update status continuously until the transfer is completed.
  TFTPStatusLoop();

  //Show OK:Anykey
  gotoxy(26,HEIGHT-1);
  cputs(strOKAnyKey);
  cgetc_showclock();
}

////////////////////////////////////////
// Move cursor to Status Message position
//
static void gotoStatus() {
  gotoxy(7,4);  //status pos
}

//////////////////////////////////////////////////////////////
// Poll the TFTP status from megaflash and update the
// screen. Don't return until the TFTP has completed.
//
static void TFTPStatusLoop() {
  static_local int8_t completed;
  static_local uint8_t pbValue;
  static_local bool pbShown;
  static_local uint8_t count;
  pbShown = false;  //Progress Bar has been shown on screen
  
  do {
    //No need to update the screen continuously.
    //At 1MHz, the data refresh rates at different delay values are 
    //80=45Hz, 90=38Hz, 100=32Hz, 110=27Hz, 130=20Hz, 140=18Hz
    Delay(150); //Refresh Rate: 16Hz at 1MHz, 34Hz at 4MHz

    //Update the clock every 16 iteration
    ++count;
    if ((count&0x0f)==0) DisplayTime();

    GetTFTPStatus(PB_WIDTH);
    completed = GetParam8();  //1=Completed successfully, -1=Completed with error, 0=in progress
    pbValue = GetParam8();    //Progress Bar Value

    //Status
    gotoStatus();
    PrintStringFromDataBuffer();
    clreol();
    
    //Blocks
    gotoxy(7,6);
    PrintStringFromDataBuffer();
  
    //Retransmit
    gotoxy(11,8);
    PrintStringFromDataBuffer();
    
    //Elapsed Time
    gotox(24);
    PrintStringFromDataBuffer();
    
    //Progress Bar
    if (pbValue!=255) {
      if (!pbShown) {
      ShowProgressBar(PB_XPOS,PB_YPOS,PB_WIDTH);
      pbShown = true;
      }
      UpdateProgressBar(pbValue);
    }
    
    //Show Error
    if (completed == -1) {
      //Clear the progress bar
      HideProgressBar();
      
      //Printe Error Message
      gotoxy(ERR_XPOS,ERR_YPOS);
      PrintStringFromDataBuffer();
    }
  }while(!completed);
}




