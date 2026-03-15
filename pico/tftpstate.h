#ifndef _TFTPSTATE_H
#define _TFTPSTATE_H
#ifdef __cplusplus
extern "C" {
#endif

#include "../common/defines.h"

typedef enum {
  TFTPSTATUS_IDLE=0,
  TFTPSTATUS_STARTING,
  TFTPSTATUS_WIFICONNECTING,
  TFTPSTATUS_REQUEST,         //Requesting from server
  TFTPSTATUS_TRANSFER,        //Data is being transferred   
  TFTPSTATUS_COMPLETING,      //Handling last ACK
  TFTPSTATUS_COMPLETED,       //Complete Successfully

}tftp_status_t;

//Error Code Range:
//There are 3 types of error. 
//1) Error defined in TFTP Protocol
//2) Our own Error
//3) Exception from CUDPTask.
//
//Since Error code defined in TFTP Protocol is a 16-bit unsigned integer.
//So, the Error Code 
//  0-0xffff     : TFTP Protocol Errorcode (16-bit unsigned integer
//  >0x10000     : Our own Errorcodes
//  <0 (negative): ErrorCode from CUDPTask
//



// Error Code:
// >=0: Error Defined in TFTP Protocol
// <0 : Our own Errorcode.
//
// The CTFTPRXTask and CTFTPTXTask object does not throw any exception. All errorcode
// is written to tftp_state.error directly.
//
// CUDPTask::Run() method may throw exception. The caller should catch those exception
// and translate it to error code defined here and save the error code to tftp_state
//
typedef enum {
  //0-8 defined by TFTP protocol RFC1350 and RFC1782
  TFTPERROR_UNKNOWN = 0,
  TFTPERROR_FILENOTFOUND = 1,
  TFTPERROR_ACCESSVIOLATION = 2,
  TFTPERROR_DISKFULL = 3,
  TFTPERROR_ILLEGAL_OP = 4,
  TFTPERROR_UNKNOWN_TID = 5,  //Not fatal
  TFTPERROR_FILE_ALREADY_EXIST = 6,
  TFTPERROR_NO_SUCH_USER = 7,
  TFTPERROR_DENIED_OPTIONS = 8,
  
  //Our erorr code
  TFTPERROR_NOERR    = -1,           
  TFTPERROR_ARG      = -2,           //Invalid Parameters e.g. filename is empty.
  TFTPERROR_TIMEOUT  = -3,           //No valid response from server after retries
  TFTPERROR_ABORTED  = -4,           //Aborted
  TFTPERROR_ODDSIZE  = -5,           //File size is not multiple of 512
  TFTPERROR_OVERSIZE = -6,           //File size > Capacity of the drive
  TFTPERROR_WIFINOTCONNECTED = -7,   //Unable to connect to WIFI Network
  TFTPERROR_WIFICONNECTIONLOST = -8, //WIFI Connection lost during File Transfer
  TFTPERROR_DNS = -9,                //Failed to resolve server hostname to IP Address
  TFTPERROR_WATCHDOG = -10,          //WatchDog timer timeout
  TFTPERROR_RWFAILED = -11,          //Read/Write to storage medium failed
  TFTPERROR_DOWRONGSIZE = -12        //DOS Order Image: Size not multiple of 8 blocks
}tftp_error_t;

#define TFTPSTATE_INVALIDBLOCKCOUNT (-1)
#define TFTPSTATE_INVALIDTSIZE (-1)
typedef struct {
  absolute_time_t startTime;
  uint32_t taskid; 
  uint32_t dir;               //0=Download from server, 1=Upload to server
  uint32_t unitNum;           //unitNum of source/destination drive
  uint32_t blockTransferred;  //Number of block sent/received
  uint32_t tsize;             //size of the file being received in bytes
  uint32_t retries;           //Number of Retries
  tftp_error_t error;
  tftp_status_t status;  
  char server_hostname[TFTP_HOSTNAME_MAXLEN+1];
  char filename[TFTP_FILENAME_MAXLEN+1];
} tftp_state_t;


void InitTFTPState();
void TFTPCopyHostname(const char* hostname);
void TFTPCopyFilename(const char* filename);
void tftp_critical_section_enter_blocking();
void tftp_critical_section_exit();

//Helper methods for DoTFTPStatus()
uint8_t TFTPCalcProgressBarValue(uint32_t pbValueMax,const uint32_t blockTransferred,const uint32_t tsize);
char* TFTPFormatStatusMessage(char* dest,const tftp_status_t status,const tftp_error_t error);
char* TFTPFormatBlocksMessage(char *dest,const uint32_t blockTransferred,const uint32_t tsize);
char* TFTPFormatRetransmit(char *dest,const uint32_t retries);
char* TFTPFormatElapsedTime(char *dest,const uint32_t elapsedTime);
char* TFTPFormatErrorMessage(char* dest,const tftp_error_t error);

#ifdef __cplusplus
}
#endif

#endif