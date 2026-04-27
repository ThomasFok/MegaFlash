#include "tftp.h"
#include "tftptask.h"
#include "misc.h"

extern "C" volatile tftp_state_t tftp_state;


CTFTPTask::CTFTPTask(const uint32_t unit_num,const char* hostname,const char* filename,const bool enable_1k_block_size,const uint32_t tftp_timeout,
                     const uint32_t tftp_max_attempt,const uint16_t tftp_server_port):CUDPTask() {
  assert(unit_num!=0);
  assert(hostname!=nullptr);
  assert(filename!=nullptr);
  assert(strlen(filename)<=255);
  InitTFTPState();
  this->unit_num_ = unit_num;
  this->hostname_ = hostname;
  this->filename_ = filename;
  this->enable_1k_block_size_ = enable_1k_block_size;
  
  this->tftp_timeout_ = CLAMP(tftp_timeout,TFTP_TIMEOUT_MIN,TFTP_TIMEOUT_MAX);
  this->tftp_max_attempt_ = CLAMP(tftp_max_attempt,TFTP_MAXATTEMPT_MIN,TFTP_MAXATTEMPT_MAX);
  
  this->tftp_server_port_ = tftp_server_port;
  this->tftp_timeout_laskack_ = MAX(tftp_timeout,TFTP_TIMEOUT_LASTACK_MIN);
  this->server_port_ = 0;
  
  this->attempt_ = 0;
  this->txbuffer_ = new uint8_t[TXBUFFERSIZE];
  this->txpacketlen_ = 0;
}

CTFTPTask::~CTFTPTask() {
  delete[] this->txbuffer_;
}

////////////////////////////////////////////////////////////////////
// Initialize TFTP State
//
void CTFTPTask::InitTFTPState() {
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
// Build an Ack Packet in txbuffer_
//
// Input: block - Block Number
//
void CTFTPTask::BuildAckPacket(const uint16_t block) {
  txbuffer_[0] =0;
  txbuffer_[1] = OP_ACK;
  txbuffer_[3] = (uint8_t) block;      //low byte
  txbuffer_[2] = (uint8_t)(block>>8);  //high byte
  txpacketlen_ = 4;
}


  
////////////////////////////////////////////////////////////////////
// Build an Error Packet in txbuffer_
//
// Input: errorcode - TFTP Error Code
//  
void CTFTPTask::BuildErrorPacket(const int errorcode) {
  //TFTP Error code is 0-8
  assert(errorcode>=0 && errorcode<=8);
  txbuffer_[0] =0;
  txbuffer_[1] = OP_ERROR;
  txbuffer_[3] = (uint8_t) errorcode;      //low byte
  txbuffer_[2] = 0;                        //high byte
  
  //error message
  const char* errormsg = GetErrorMessage(errorcode);
  strcpy((char*)(txbuffer_+4),errormsg);
  txpacketlen_ = 4 + strlen(errormsg) + 1;
}  



////////////////////////////////////////////////////////////////////
// Build a RRQ(Read Request) or WRQ(Write Request) Packet in txbuffer_
//
// Input: type - Packet Type (OP_RRQ or OPWRQ)
//
// The filename comes from data member filename
//
// mode is fixed to "octet" (binary)
//  
void CTFTPTask::BuildRQPacket(const uint8_t type) {
  assert(type==OP_RRQ || type==OP_WRQ);
  txbuffer_[0] = 0;
  txbuffer_[1] = type;
  txpacketlen_ = 2;
  
  //filename
  strcpy((char*)(txbuffer_+2),this->filename_);
  txpacketlen_ += strlen(this->filename_)+1; //+1 for NULL char
  
  //mode
  strcpy((char*)(txbuffer_+txpacketlen_),"octet");
  txpacketlen_ += 5+1; //len of "octet" and NULL char
}

////////////////////////////////////////////////////////////////////
// Add an option to packet for RRQ/WRQ
//
// Input: option - option name in ASCII
//        value  - value string in ASCII
//  
void CTFTPTask::AddOption(const char* option, const char* value) {
  strcpy((char*)(txbuffer_+txpacketlen_),option);
  txpacketlen_ += strlen(option)+1; //+1 for NULL char
  
  strcpy((char*)(txbuffer_+txpacketlen_),value);
  txpacketlen_ += strlen(value)+1; //+1 for NULL char
  assert(txpacketlen_<=TXBUFFERSIZE);
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
    memcpy(txbuffer_+txpacketlen_, data, len);
    txpacketlen_ += len;
  }
  assert(txpacketlen_<=TXBUFFERSIZE);
}

//
//Usage Example:
//
//  DEBUG_PRINTF("Adding options\n");
//  AddOption("blksize","1024");
//  AddOption("tsize","65536");
//  strcpy((char*)txbuffer_+txpacketlen_,"test");
//  txpacketlen_+=5;
//

////////////////////////////////////////////////////////////////////
// Parse Options for processing of OACK packet
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
//  while(ParseOption(txbuffer_,len,&currentPos,&option,&value)) {
//    DEBUG_PRINTF("Option: %s=%s\n",option,value);
//  }
//
bool CTFTPTask::ParseOptions(const uint8_t *buffer, const size_t len, size_t *currentPos,const char**option_out,const char**value_out) {
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
    *option_out = (char*)(buffer+*currentPos);
    *value_out = (char*)(buffer+firstNullPos+1);
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
  DEBUG_PRINTF("DNSLookup: hostname_=%s\n",this->hostname_);
  DNSLookup(this->hostname_); 
}


/////////////////////////////////////////////////////////////
// Retry Method
// This method is used by both CTFTPRXTask and CTFTPTXTask
//
void CTFTPTask::Retry() { 
  //Retry if number of attempt_ < tftp_max_attempt_
  if (attempt_<tftp_max_attempt_) {
    //Send Last Packet Again
    SendPacket();
    SetTimer(tftp_timeout_);    
    ++attempt_;
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
  const uint16_t errorcode = payload[2]*256+payload[3];

  //TFTP protocol defines errorcode 0-8
  //We found that TFTP64 server by Ph. Jounin sends errorcode 99
  //when user requests to stop the transfer. So, we need to handle it.
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
  
  const tftp_error_t tftp_error = static_cast<tftp_error_t>(errorcode);
  DEBUG_PRINTF("Error Packet Received. errorcode = %d\n",errorcode);
  if (tftp_error == TFTPERROR_UNKNOWN_TID) {
    //This error is not fatal. simply discard it
    ERROR_PRINTF("TFTPERROR_UNKNOWN_TID received. Discard it\n");
    return;
  }
  
  //Set error and terminate
  this->Complete();
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_COMPLETED;
  tftp_state.error = tftp_error;
  tftp_critical_section_exit();
  ERROR_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");
  ERROR_PRINTF("tftp_state.error = %d\n",tftp_error);  
  return;
}




/**
 *  \brief Handle blksize option acknowledgement from server
 *  
 *  \param [in] value blksize option string
 *  \return true if the blksize option is valid.
 *  
 *  \details This method is common to both CTFTPRXTask and CTFTPTXTask
 */
bool CTFTPTask::HandleOACK_blksize(const char* value) {
  //Only 512 and 1024 are acceptable
  if (0==strcmp(value,"1024")) {
    INFO_PRINTF("Switching TFTP blockSize to 1024\n");
    tftp_block_size_=1024;
    return true;
  } else if (0==strcmp(value,"512")){
    assert(tftp_block_size_==512);
    return true;
  } else {
    //Unrecognised blksize. 
    //We don't expect it would happen since 1024 is a perfectly good
    //block size. 
    //Anyway, we try to restart the transfer without 1024 blksize option
    return false; 
  }
}

