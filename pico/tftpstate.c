#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "debug.h"
#include "tftp.h"
#include "tftpstate.h"

/****************************************************************************************
tftp_state is used to passing information between cores.

To Start TFTP Task:
Apple -> Param/Data Buffer -> CommandHandler -> tftp_state -> IPC Msg to another core
->  ExecuteTFTP() pickup call parameters from tftp_state

CommandHandler also gets the current TFTP Status from tftp_state.

taskid & runningTaskid
CMD_TFTPRUN Handler does not return until the TFTP Task has started running. Calling 
IsTFTPTaskRunning() is not reliable because the task may terminate immediately due to
error. So, CMD_TFTPRUN Handler will generate a value to taskid. When the task
starts running, it copies taskid to runningTaskid. By checking these two values, 
CMD_TFTPRUN Handler can reliably determine if TFTP Task has been started.
/****************************************************************************************/

//Global Variable
critical_section_t tftp_cs;
volatile tftp_state_t tftp_state;

void __no_inline_not_in_flash_func(tftp_critical_section_enter_blocking)() {
  critical_section_enter_blocking(&tftp_cs);
}

void __no_inline_not_in_flash_func(tftp_critical_section_exit)() {
  critical_section_exit(&tftp_cs);
}

void InitTFTPState() {
  critical_section_init(&tftp_cs);
  tftp_state.taskid = 0;
  tftp_state.dir = 0;
  tftp_state.server_hostname[0] = '\0';
  tftp_state.filename[0] = '\0';
  tftp_state.unitNum = 0;
  tftp_state.blockTransferred = TFTPSTATE_INVALIDBLOCKCOUNT;
  tftp_state.tsize = TFTPSTATE_INVALIDTSIZE;
  tftp_state.error = TFTPERROR_NOERR;
  tftp_state.status = TFTPSTATUS_IDLE;
  tftp_state.retries = 0;
}


void TFTPCopyHostname(const char* hostname) {
  strncpy((char*)tftp_state.server_hostname,hostname,TFTP_HOSTNAME_MAXLEN);
  tftp_state.server_hostname[TFTP_HOSTNAME_MAXLEN] = '\0';
}

void TFTPCopyFilename(const char* filename) {
  strncpy((char*)tftp_state.filename,filename,TFTP_FILENAME_MAXLEN);
  tftp_state.filename[TFTP_FILENAME_MAXLEN] = '\0';
}

////////////////////////////////////////////////////////////////
// Calculate current Progress Bar Value
//
// Input: pbValueMax       - Maximum Value of Progress Bar
//        blockTransferred - Number of Transferred Blocks
//        tsize            - Transfer Size in Bytes
//
//Note: If the tsize (Transfer Size) is unknown, the progress bar value 
//      cannot be calculated. Return INVALID_PBVALUE to indicate 
//      this situation.
//
uint8_t TFTPCalcProgressBarValue(uint32_t pbValueMax,const uint32_t blockTransferred,const uint32_t tsize) {
  const uint8_t INVALID_PBVALUE = 0xff;
  uint8_t result = INVALID_PBVALUE;
  if (blockTransferred == TFTPSTATE_INVALIDBLOCKCOUNT || tsize == TFTPSTATE_INVALIDTSIZE) goto exit;
  
  //Calculate Total Number of blocks
  uint32_t totalBlocks = tsize/PRODOS_BLOCKSIZE;
  if (totalBlocks==0) goto exit;    //Avoid division by 0
  
  if (blockTransferred>=totalBlocks) result = pbValueMax; //completed
  else {
    float fresult = (float)blockTransferred/totalBlocks*pbValueMax;
    result = (uint8_t)fresult;    
  }
  
exit:
    return result;
}


/////////////////////////////////////////////////////////////////////
// Write a string to dest buffer
//
// Input:  dest - point to destination buffer
//         s    - String to be stored.

// Output: char* - Point to the byte after the null character
//                 of the result
//
static char* PutString(char* dest,const char* s) {
  strcpy(dest,s);
  return dest+strlen(s)+1;
}

/////////////////////////////////////////////////////////////////////
// Write an empty string (NULL char) to dest buffer
//
// Input:  dest - point to destination buffer

// Output: char* - Point to the byte after the null character
//
static char *PutEmptyString(char* dest) {
  return PutString(dest,"");
}

/////////////////////////////////////////////////////////////////////
// Format a status message for Apple to display
//
// Input: dest   - point to destination buffer
//        status - status code
//        error  - error code
//
// Output: char* - Point to the byte after the null character of
//                 the generated message
//
char* TFTPFormatStatusMessage(char* dest,const tftp_status_t status,const tftp_error_t error){
  static const char *STATUSMSG[] = { "Idle",
                                     "Starting",
                                     "Connecting to WIFI",
                                     "Requesting Server",
                                     "Transferring",
                                     "Completing",
                                     "Completed"};
                          
  //check the range of status
  if (status>=count_of(STATUSMSG)) return PutEmptyString(dest);         
                            
  dest += sprintf(dest,"%s",STATUSMSG[status]);

  //Add " Successfully" or " with error" to "Completed"
  if (status ==TFTPSTATUS_COMPLETED){
    return PutString(dest,error==TFTPERROR_NOERR?" Successfully":" with error");
  }    
  else return PutEmptyString(dest);
}

/////////////////////////////////////////////////////////////////////
// Format a Block message for Apple to display
//
// Input: dest   - point to destination buffer
//        blockTransferred - Number of blocks has been transferred
//        tsize            - Transfer Size (Total Size of the file) in bytes
//
// Output: char* - Point to the byte after the null character of
//                 the generated message
//
// Message format: 12345/65535 (18.8%)
//
char* TFTPFormatBlocksMessage(char *dest,const uint32_t blockTransferred,const uint32_t tsize) {
    if (blockTransferred == TFTPSTATE_INVALIDBLOCKCOUNT) goto exit; 

    //Output number of blocks transferred
    dest += sprintf(dest,"%u",MIN(blockTransferred,65536));

    //Calculate Total Number of blocks
    if (tsize == TFTPSTATE_INVALIDTSIZE) goto exit;
    uint32_t totalBlocks = tsize/PRODOS_BLOCKSIZE;
    //make sure blockTransferred<=totalBlocks and totalBlocks!=0
    if (blockTransferred>totalBlocks || totalBlocks==0) goto exit;    
    
    //Output Total Number of blocks
    dest += sprintf(dest,"/%u",MIN(totalBlocks,65536));

    //Output Completed Percentage
    float percent = (float)blockTransferred/totalBlocks*100.0f;
    dest += sprintf(dest," (%.1f%%)",percent);

exit:
    //Null terminate the string
    return PutEmptyString(dest);
}

/////////////////////////////////////////////////////////////////////
// Format Retransmit count for Apple to display
//
// Input: dest    - point to destination buffer
//        retries - Number of retransmit
//
// Output: char* - Point to the byte after the null character of
//                 the generated message
//
char* TFTPFormatRetransmit(char *dest,const uint32_t retries) {
  dest += sprintf(dest,"%u",MIN(retries,99999));  //Limit the value to 99999
  return dest+1;
}

/////////////////////////////////////////////////////////////////////
// Format Elapsed Time for Apple to display
//
// Input: dest        - point to destination buffer
//        elaspedTime - Time in s
//
// Output: char* - Point to the byte after the null character of
//                 the generated message
//
char* TFTPFormatElapsedTime(char *dest,const uint32_t elapsedTime) {
  dest += sprintf(dest,"%us",MIN(elapsedTime,99999)); //Limit the value to 99999
  return dest+1;
}

/////////////////////////////////////////////////////////////////////
// Write "Error:\n\r" and error message to dest buffer
//
// Input:  dest     - point to destination buffer
//         errorMsg - Error Message string
//
static char* PutErrorString(char* dest,char* errorMsg) {
  dest += sprintf(dest,"Error:\n\r%s",errorMsg);
  return dest+1;
}



/////////////////////////////////////////////////////////////////////
// Write "Warning:\n\r" and error message to dest buffer
//
// Input:  dest - point to destination buffer
//
static char* PutWarningString(char* dest,char* errorMsg) {
  dest += sprintf(dest,"Warning:\n\r%s",errorMsg);
  return dest+1;  
}

/////////////////////////////////////////////////////////////////////
// Format Error Message for Apple to display
//
// Input: dest    - point to destination buffer
//        error   - TFTP Error code
//
// Output: char* - Point to the byte after the null character of
//                 the generated message
//
char* TFTPFormatErrorMessage(char* dest,const tftp_error_t error) {
  //Shortcut for No Error
  if (error==TFTPERROR_NOERR) return PutEmptyString(dest);

  switch(error) {
    //
    // TFTP Protocol Errors
    //
    case TFTPERROR_UNKNOWN:
      dest = PutErrorString(dest,"Unknown TFTP error");
      break;
    case TFTPERROR_FILENOTFOUND:
      dest = PutErrorString(dest,"File not found");
      break;
    case TFTPERROR_ACCESSVIOLATION:
      dest = PutErrorString(dest,"Access violation");
      break;
    case TFTPERROR_DISKFULL:
      dest = PutErrorString(dest,"Disk full");
      break;
    case TFTPERROR_ILLEGAL_OP:
      dest = PutErrorString(dest,"TFTP Protocol:Illegal opcode");
      break;
    case TFTPERROR_UNKNOWN_TID:
      //should not happend since it is not a fatal error
      assert(0);
      dest = PutErrorString(dest,"TFTP Protocol:Unknown TID");
      break;
    case TFTPERROR_FILE_ALREADY_EXIST:
      dest = PutErrorString(dest,"File already exist");
      break;
    case TFTPERROR_NO_SUCH_USER:
      dest = PutErrorString(dest,"No such user");
      break;
    case TFTPERROR_DENIED_OPTIONS:
      dest = PutErrorString(dest,"TFTP Protocol:Options denied by\n\rserver");
      break;
    //
    // TFTP Task Errors
    //
    case TFTPERROR_NOERR:
      dest = PutEmptyString(dest);
      break;
    case TFTPERROR_ARG:
      dest = PutErrorString(dest,"Invalid Parameters");
      break;
    case TFTPERROR_TIMEOUT:
      dest = PutErrorString(dest,"Timeout Error. No valid responses\n\rfrom server");
      break;
    case TFTPERROR_ABORTED:
      dest = PutErrorString(dest,"Aborted");
      break;
    case TFTPERROR_ODDSIZE:
      dest = PutWarningString(dest,"The file size is not mulitple of 512");
      break;
    case TFTPERROR_OVERSIZE:
      dest = PutWarningString(dest,"The file size exceeds the capacity\n\rof the drive");
      break;
    case TFTPERROR_WIFINOTCONNECTED:
      dest = PutErrorString(dest,"Failed to connect to WIFI");
      break;
    case TFTPERROR_WIFICONNECTIONLOST:
      dest = PutErrorString(dest,"WIFI disconnected");
      break;
    case TFTPERROR_DNS:
      dest = PutErrorString(dest,"DNS:Unable to resolve the hostname\n\rof the server");
      break;
    case TFTPERROR_WATCHDOG:
      dest = PutErrorString(dest,"No response from MegaFlash. Please\n\rpower cycle your computer");
      break;
    case TFTPERROR_RWFAILED:
      dest = PutErrorString(dest,"Storage medium I/O error");
      break;
    case TFTPERROR_DOWRONGSIZE:
      dest = PutWarningString(dest,"DOS Order Image:Incorrect file size");
      break;
    default:
      dest = PutErrorString(dest,"")-1;   //-1 To exclude the null character
      if (error>=0) dest += sprintf(dest,"TFTP Protocol Error=%d",error);
      else dest += sprintf(dest,"Error code=%d",error);
      ++dest; //To include the null character
  }
  return dest;
}




