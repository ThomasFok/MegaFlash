#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "usbserial.h"
#include "filetransfer.h"
#include "formatter.h"
#include "misc.h"
#include "flash.h"
#include "mediaaccess.h"
#include "romdisk.h"
#include "ramdisk.h"
#include "flashunitmapper.h"

//--------------------------------------------------------------
//The definitions below must be the same as the ones in busloop.c

extern uint8_t dataBuffer[];
//--------------------------------------------------------------

static void PrintBanner() {
 printf("\n\n");
 printf("#     #                          #######\n");                                 
 printf("##   ##  ######   ####     ##    #        #         ##     ####   #    #\n");
 printf("# # # #  #       #    #   #  #   #        #        #  #   #       #    #\n");
 printf("#  #  #  #####   #       #    #  #####    #       #    #   ####   ######\n");
 printf("#     #  #       #  ###  ######  #        #       ######       #  #    #\n");
 printf("#     #  #       #    #  #    #  #        #       #    #  #    #  #    #\n");
 printf("#     #  ######   ####   #    #  #        ######  #    #   ####   #    #\n");
}

static void WaitForAnyKey() {
  printf("\nPress any key to continue...");
  usb_getkey();
  printf("\n");
}


static bool Confirm() {
  char buffer[8];
  bool cancelled;
  
  do {
    printf("Type CONFIRM to proceed, Ctrl-C to abort: ");

    buffer[0] = '\0';
    usb_getstring(buffer, 8, &cancelled); 
    printf("\n");
    if (cancelled || buffer[0]=='\0') {
      return false;
    }
  }while(stricmp(buffer,"confirm"));
  return true;
}


static uint32_t AskUnitNum() {
  const int CTRLC = 3;
  uint unitCount = GetTotalUnitCount();
  char unitCountChar = '0' + unitCount;
  
  if (unitCount==0) {
    printf("Error: no flash\n");
    return 0;
  }
  
  int key;
  do {
    printf("Please enter drive number (1-%c), Ctrl-C to abort:",unitCountChar);
    key=usb_getkey();
    if (key == CTRLC) {
      putchar('\n');
      return 0;
    }
    if (key>=32) putchar(key);
    putchar('\n');
  }while (key<'1' || key>unitCountChar);
  
  return key-'0';
}


static void PrintVolInfoList(const uint unitNum) {
  VolumeInfo info;

  printf("%3d   ",unitNum);

  bool success = GetVolumeInfo(unitNum, &info);
  if (!success) {
    printf("Error\n");
    return;
  }
  
  if (info.type == TYPE_PRODOS)     printf("ProDOS  ");
  else if (info.type == TYPE_EMPTY) printf("Empty   ");
  else                              printf("Unknown ");

  if (info.type == TYPE_PRODOS) {
    //Volume Name
    printf("%-16s",info.volName);
    
    //Block Count
    printf("%5u",info.blockCount);
  }
  putchar('\n');
}

static void PrintAllPartitions() {
  uint unitCount = GetTotalUnitCount();
  if (unitCount !=0) {
    printf("\nPartition Information:\nDrive Type    Volume Name     Size (Blocks)\n");
                    
    for (uint i=1;i<=unitCount;++i) {
      PrintVolInfoList(i);
    }
  }
}


static void DeviceInfo() {
  GetDeviceInfoString(dataBuffer);
  assert(strlen(dataBuffer)<DATABUFFERSIZE);
  printf("%s",dataBuffer);

  //
  //Heap Usage
  //
  printf("Free/Total Heap = %d/%d\n",GetFreeHeap(),GetTotalHeap());

  //
  // Parition Information
  //
  PrintAllPartitions();
  WaitForAnyKey();
}  



static void EraseFlash() {
  printf("Erase Flash Content\n");
  printf("===================\n\n");
  
  printf("!!!! WARNING !!!!\n");
  printf("All data stored in MegaFlash will be destroyed.\n");
  printf("It takes at least 200 seconds to complete.\n\n");
 
  if (Confirm()) {
    printf("\nErasing... Please wait.");
    TurnOnActLed();
    TurnOnPicoLed();
    EraseEverything();
    TurnOffActLed();
    TurnOffPicoLed();
    printf("\nDone!\n");
    WaitForAnyKey();
  }
}


static void PrintVolInfo(const uint unitNum) {
  VolumeInfo info;
  GetVolumeInfo(unitNum, &info);
  printf("Drive  = %d\n", unitNum);
  printf("Type   = ");
  if (info.type==TYPE_PRODOS)       printf("ProDOS\n");
  else if (info.type == TYPE_EMPTY) printf("Empty\n");
  else                              printf("Unknown\n");
  
  if (info.type==TYPE_PRODOS) {
    printf("Volume = %s\n",info.volName);
    printf("Size   = %u blocks\n",info.blockCount);
  } 
  putchar('\n');
}

static void UploadImage() {
  printf("Upload ProDOS Image\n");
  printf("===================\n\n");
  
  printf("A ProDOS ordered disk image (.po/.hdv) can be uploaded to MegaFlash and\n");
  printf("written to a drive with XMODEM-CRC or XMODEM-1k protocol.\n");

  
  PrintAllPartitions();
  putchar('\n'); 
  
  uint32_t unitNum = AskUnitNum();
  if (unitNum==0) return;   //User entered Ctrl-C
  
  printf("\nCurrent:\n");  
  PrintVolInfo(unitNum);

  printf("WARNING: All data stored in the drive will be destroyed.\n");
  if (Confirm()) {
    Upload(unitNum);
    WaitForAnyKey();
  }
}

static void DownloadImage() {
  printf("Download ProDOS Image\n");
  printf("=====================\n\n");
  
  printf("The content of a drive in MegaFlash can be downloaded to PC and be saved\n");
  printf("as a ProDOS ordered disk image file (.po/.hdv).\n");
  
  PrintAllPartitions();
  putchar('\n');
  
  uint32_t unitNum = AskUnitNum();
  if (unitNum==0) return;  //User entered Ctrl-C
  
  printf("\nDrive Info:\n");
  PrintVolInfo(unitNum);

  //Ask for Protocol
  uint32_t key;
  enum TxProtocol protocol;
  do {
    printf("\nFile Transfer Protocol:\n");
    printf("1:XModem/CRC (128 Bytes Packet)\n");
    printf("2:XModem-1k (1024 Bytes Packet) (Default)\n");
    printf("\nPlease select the protocol:");

    key=usb_getkey();
    if (key=='1') {putchar('1'); break;}
    else if (key=='2' || key=='\r') {putchar('2'); break;}
    else putchar('\n');
  } while (1);
  putchar('\n');

  protocol = (key=='1')?XMODEM128:XMODEM1K;
  Download(unitNum,protocol); 
  WaitForAnyKey();
}

void UserTerminal() {
  int key;
  
  //Disable stdout buffer. Otherwise, printf doesnt send out
  //until a newline character is printed.
  setbuf(stdout, NULL);
  
  //Make sure all flash drives are accessible
  DisableFlashUnitMapping();
  
  //Make sure ROM Disk and RAM Disk are disabled
  DisableRomdisk();
  DisableRamdisk();
  
  while(1) { 
    do{
      PrintBanner();
      printf("\nMain Menu\n");
      printf(  "=========\n\n");
      printf("1) Device Information\n");
      printf("2) Upload ProDOS Image file to MegaFlash\n");
      printf("3) Download ProDOS Image file from MegaFlash\n");
      printf("4) Erase Flash Content\n");
      printf("\nPlease Select:");
      key = usb_getkey();
      printf("%c\n\n",key);
      
    }while(key<'1' || key>'4');
    
    if (key=='1')      DeviceInfo();
    else if (key=='3') DownloadImage();
    else if (key=='2') UploadImage();
    else if (key=='4') EraseFlash();
  }
}
