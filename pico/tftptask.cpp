#include "tftp.h"
#include "tftptask.h"

extern "C" volatile tftp_state_t tftp_state;


CTFTPTask::CTFTPTask(const uint32_t unitNum,const char* hostname,const char* filename,const bool enable1kBlockSize,const uint32_t tftpTimeout,
                     const uint32_t tftpMaxAttempt,const uint16_t tftpServerPort):CUDPTask() {
  assert(unitNum!=0);
  assert(hostname!=NULL);
  assert(filename!=NULL);
  assert(strlen(filename)<=255);
  UpdateTFTPState();
  this->unitNum = unitNum;
  this->hostname = hostname;
  this->filename = filename;
  this->enable1kBlockSize = enable1kBlockSize;
  
  if (tftpTimeout<TFTP_TIMEOUT_MIN)     this->tftpTimeout = TFTP_TIMEOUT_MIN;
  else if(tftpTimeout>TFTP_TIMEOUT_MAX) this->tftpTimeout = TFTP_TIMEOUT_MAX;
  else                                  this->tftpTimeout = tftpTimeout;
  
  if (tftpMaxAttempt<TFTP_MAXATTEMPT_MIN)      this->tftpMaxAttempt = TFTP_MAXATTEMPT_MIN;
  else if (tftpMaxAttempt>TFTP_MAXATTEMPT_MAX) this->tftpMaxAttempt = TFTP_MAXATTEMPT_MAX;
  else                                         this->tftpMaxAttempt = tftpMaxAttempt;
  
  this->tftpServerPort = tftpServerPort;
  this->tftpTimeoutLastACK = MAX(tftpTimeout,TFTP_TIMEOUT_LASTACK_MIN);
  this->server_port = 0;
  
  this->attempt = 0;
  this->txbuffer = new uint8_t[TXBUFFERSIZE];
  this->txpacketlen = 0;
}

CTFTPTask::~CTFTPTask() {
  if (this->txbuffer) delete[] this->txbuffer;
}

////////////////////////////////////////////////////////////////////
// Initalize TFTP State
//
void CTFTPTask::UpdateTFTPState() {
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_IDLE;
  tftp_state.blockTransferred  = TFTPSTATE_INVALIDBLOCKCOUNT;
  tftp_state.tsize = TFTPSTATE_INVALIDTSIZE;
  tftp_state.error = TFTPERROR_NOERR;
  tftp_state.retries = 0;
  tftp_critical_section_exit();  
}

////////////////////////////////////////////////////////////////////
// Convert TFTP Error Code to text message
//
// Input: errorcode - TFTP Error Code
//
// Output: const char* - Text Message String
// 
const char* CTFTPTask::GetErrorMessage(const int errorcode) {
  if (errorcode == TFTPERROR_UNKNOWN) return "TFTP Unknown Error";
  else if (errorcode == TFTPERROR_FILENOTFOUND) return "File not found on server";
  else if (errorcode == TFTPERROR_ACCESSVIOLATION) return "Access violation on server";
  else if (errorcode == TFTPERROR_DISKFULL) return "Disk full on server";
  else if (errorcode == TFTPERROR_ILLEGAL_OP) return "Illegal TFTP Operation";
  else if (errorcode == TFTPERROR_UNKNOWN_TID) return "Unknown TID";
  else if (errorcode == TFTPERROR_FILE_ALREADY_EXIST) return "File already exist on server";
  else if (errorcode == TFTPERROR_NO_SUCH_USER) return "No such user";
  else if (errorcode == TFTPERROR_DENIED_OPTIONS) return "Options negotiation denied.";
  else return "Unexpected Error";
}

////////////////////////////////////////////////////////////////////
// Build an Ack Packet in txbuffer
//
// Input: block - Block Number
//
void CTFTPTask::BuildAckPacket(const uint16_t block) {
  txbuffer[0] =0;
  txbuffer[1] = OP_ACK;
  txbuffer[3] = (uint8_t) block;      //low byte
  txbuffer[2] = (uint8_t)(block>>8);  //high byte
  txpacketlen = 4;
}


  
////////////////////////////////////////////////////////////////////
// Build an Error Packet in txbuffer
//
// Input: errorcode - TFTP Error Code
//  
void CTFTPTask::BuildErrorPacket(const int errorcode) {
  //TFTP Error code is 0-8
  assert(errorcode>=0 && errorcode<=8);
  txbuffer[0] =0;
  txbuffer[1] = OP_ERROR;
  txbuffer[3] = (uint8_t) errorcode;      //low byte
  txbuffer[2] = 0;                        //high byte
  
  //error message
  const char* errormsg = GetErrorMessage(errorcode);
  strcpy((char*)(txbuffer+4),errormsg);
  txpacketlen = 4 + strlen(errormsg) + 1;
}  



////////////////////////////////////////////////////////////////////
// Build a RRQ(Read Request) or WRQ(Write Request) Packet in txbuffer
//
// Input: type - Packet Type (OP_RRQ or OPWRQ)
//
// The filename comes from data member filename
//
// mode is fixed to "octet" (binary)
//  
void CTFTPTask::BuildRQPacket(const uint8_t type) {
  assert(type==OP_RRQ || type==OP_WRQ);
  txbuffer[0] = 0;
  txbuffer[1] = type;
  txpacketlen = 2;
  
  //filename
  strcpy((char*)(txbuffer+2),this->filename);
  txpacketlen += strlen(this->filename)+1; //+1 for NULL tftp_state.blockTransferred
  
  //mode
  strcpy((char*)(txbuffer+txpacketlen),"octet");
  txpacketlen += 5+1; //len of "octet"  and NULL tftp_state.blockTransferred
}

////////////////////////////////////////////////////////////////////
// Add an option to packet for RRQ/WRQ
//
// Input: option - option name in ASCII
//        value  - value string in ASCII
//  
void CTFTPTask::AddOption(const char* option, const char* value) {
  strcpy((char*)(txbuffer+txpacketlen),option);
  txpacketlen += strlen(option)+1; //+1 for NULL tftp_state.blockTransferred
  
  strcpy((char*)(txbuffer+txpacketlen),value);
  txpacketlen += strlen(value)+1; //+1 for NULL tftp_state.blockTransferred
  assert(txpacketlen<=TXBUFFERSIZE);
}


////////////////////////////////////////////////////////////////////
// Add an option to packet for RRQ/WRQ
//
// Input: option - option name in ASCII
//        value  - uint32_t value
//  
void CTFTPTask::AddOption(const char* option, const uint32_t value) {
  //Convert value to string
  const size_t BUFSIZE = 16;
  char buf[BUFSIZE];
  snprintf(buf,BUFSIZE,"%u",value);
  AddOption(option,buf);
}


////////////////////////////////////////////////////////////////////
// Add binary data to packet
//
// Input: data - pointer to binary data
//        len - number of bytes
// 
void CTFTPTask::AddBinaryData(const uint8_t *data,const uint32_t len) {
  if (len!=0) {
    memcpy(txbuffer+txpacketlen, data, len);
    txpacketlen += len;
  }
  assert(txpacketlen<=TXBUFFERSIZE);
}

//
//Usage Example:
//
//  DEBUG_PRINTF("Adding options\n");
//  AddOption("blksize","1024");
//  AddOption("tsize","65536");
//  strcpy((char*)txbuffer+txpacketlen,"test");
//  txpacketlen+=5;
//

////////////////////////////////////////////////////////////////////
// Parse Option for processing of OACK packet
//
// Input: buffer - Pointer to data buffer
//        len    - Length of data
//        *currentPos - Pointer to start position of scanning
//        **pOption - Pointer to char* to receive option
//        **pValue - Pointer to char* to receive value
//  
// return: true - An option-value pair is found
//
//Example:
//  size_t currentPos=0;  //Start scanning at offset 0
//  const char* option,*value;
//  while(ParseOption(txbuffer,len,&currentPos,&option,&value)) {
//    DEBUG_PRINTF("Option: %s=%s\n",option,value);
//  }
//
bool CTFTPTask::ParseOption(const uint8_t *buffer, const size_t len, size_t *currentPos,const char**pOption,const char**pValue) {
  size_t firstNullPos  = -1;
  size_t secondNullPos = -1;

  //End of data?
  if (*currentPos >= len) return false;

  //Searching from currentPos for two null characters
  for (size_t i=*currentPos;i<len;++i) {
    if (buffer[i]=='\0') {
      if (firstNullPos==-1) firstNullPos=i;
      else {
        secondNullPos = i;
        break;
      }
    }
  }
  
  if (firstNullPos!=-1 && secondNullPos!=-1) {
    *pOption = (char*)(buffer+*currentPos);
    *pValue = (char*)(buffer+firstNullPos+1);
    *currentPos = secondNullPos+1;
    return true;  //Option-Value found
  } else {
    return false;
  }
}


//////////////////////////////////////////////////////////
//
// Event Start Handler
// This method is common to both CTFTPRXTask and CTFTPTXTask
//
void CTFTPTask::EvtStart() {
  CUDPTask::EvtStart();
  
  //Start the process by looking up server IP
  DEBUG_PRINTF("DNSLookup: hostname=%s\n",this->hostname);
  DNSLookup(this->hostname); 
}


/////////////////////////////////////////////////////////////
// Retry Method
// This method is used by both CTFTPRXTask and CTFTPTXTask
//
void CTFTPTask::Retry() { 
  //Retry if number of attempt < tftpMaxAttempt
  if (attempt<tftpMaxAttempt) {
    //Send Last Packet Again
    SendPacket();
    SetTimer(tftpTimeout);    
    ++attempt;
    INFO_PRINTF("#"); 
    tftp_critical_section_enter_blocking();
    ++tftp_state.retries;
    tftp_critical_section_exit();   
    return;
  } else {
    //Too many retries. Giveup
    this->Complete();
    tftp_critical_section_enter_blocking();
    tftp_state.error = TFTPERROR_TIMEOUT;
    tftp_state.status = TFTPSTATUS_COMPLETED;
    tftp_critical_section_exit();    
    ERROR_PRINTF("tftp_state.error = TFTPERROR_TIMEOUT\n");
    ERROR_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");    
    ERROR_PRINTF("Too many retries. Give up\n");
    return;
  }
}



//////////////////////////////////////////////////////////
// Process Error Packet
// Any Errorcode is fatal except TFTPERROR_UNKNOWN_TID
// This method is common to both CTFTPRXTask and CTFTPTXTask
//
void CTFTPTask::ProcessErrorPacket(const uint8_t* payload,uint16_t payloadlen) {
  uint16_t errorcode = payload[2]*256+payload[3];

  //TFTP protocol defines errorcode 0-8
  //We found that TFTP64 by Ph. Jounin server send errorcode 99
  //when user request to stop the transfer. So, we need to handle it.
  if (errorcode == 99) {
    this->Complete();    
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETED;
    tftp_state.error = TFTPERROR_ABORTED;
    tftp_critical_section_exit();    
    ERROR_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");
    ERROR_PRINTF("tftp_state.error = TFTPERROR_ABORTED\n");
    return;
  }
   
  DEBUG_PRINTF("Error Packet Received. errorcode = %d\n",errorcode);
  if (errorcode == TFTPERROR_UNKNOWN_TID) {
    //This error is not fatal. simply discard it
    ERROR_PRINTF("TFTPERROR_UNKNOWN_TID received. Discard it\n");
    return;
  }
  
  //Set error and terminate
  this->Complete();
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_COMPLETED;
  tftp_state.error = errorcode;
  tftp_critical_section_exit();
  ERROR_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");
  ERROR_PRINTF("tftp_state.error = %d\n",errorcode);  
  return;
}
