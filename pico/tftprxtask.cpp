#include "tftp.h"
#include "tftptask.h"
#include "tftprxtask.h"
#include "tftpstate.h"
#include "misc.h"
#include "mediaaccess.h"
#include "debug.h"

extern "C" volatile tftp_state_t tftp_state;


//Set to 1 to write received data to storage medium
#ifdef NDEBUG
#define WRITETOFLASH 1  /* Release Build*/
#else
#define WRITETOFLASH 1  /* Debug Build*/
#endif

//////////////////////////////////////////////////////////
// Constructor
// 
CTFTPRXTask::CTFTPRXTask(const uint32_t unit_num,const char* hostname,const char* filename,const bool enable_1k_block_size,const uint32_t tftp_timeout,
                         const uint32_t tftp_max_attempt,const uint16_t tftp_server_port)
                         :CTFTPTask(unit_num, hostname, filename,enable_1k_block_size,tftp_timeout,tftp_max_attempt,tftp_server_port)
                         ,image_writer_(filename) {
  
  oack_received_ = false;
  has_completed_ = false;
  server_tid_accepted_ = false;
  block_received_ = 0;
  tftp_block_size_ = 512;
  block_capacity_ = GetBlockCountActual(unit_num);
  DEBUG_PRINTF("block_capacity_ = %d\n",block_capacity_);
}

//////////////////////////////////////////////////////////
// Override Run()
//
void CTFTPRXTask::Run(const char* ssid, const char* wpakey){
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_WIFICONNECTING;
  tftp_critical_section_exit();
  INFO_PRINTF("tftp_state.status = TFTPSTATUS_WIFICONNECTING\n");
 
  CTFTPTask::Run(ssid,wpakey);
}

//////////////////////////////////////////////////////////
//
// DNS Lookup Result Handler
//
void CTFTPRXTask::EvtDNSResult(const int dns_error, const ip_addr_t *ipaddr) {
  //Call base class implementation
  //Throw exception if hostname cannot be resolved.
  CTFTPTask::EvtDNSResult(dns_error,ipaddr);  
  
  //Hostname resolved.
  StartTransfer();
}

//////////////////////////////////////////////
// Start File Transfer by sending RRQ packet
//
void CTFTPRXTask::StartTransfer() {
  this->server_port_ = tftp_server_port_; //Reset server_port TFTP Server listening port
  BuildReadRequestPacket();
  
  if (enable_1k_block_size_) {
    AddOption("blksize","1024");
    AddOption("tsize","0");   //Request server to report file size
  }
  
  SendPacket();
  SetTimer(tftp_timeout_);
  attempt_ = 1; //First Attempt
  expected_block_ = 1;  //Expecting to receive block #1
  
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_REQUEST;
  tftp_critical_section_exit();
  INFO_PRINTF("tftp_state.status = TFTPSTATUS_REQUEST\n");
}


//////////////////////////////////////////////////////////
//
// UDP Received Handler
//
// Discard all invalid packet and let the timeout mechanism
// to retry.
//
// Only OACK, Data Packet and ERROR Packet are processed
// Data Packet -> Process if valid and send ACK
//             -> Discard if invalid
//
// Error Packet -> Set Error Message and Stop except
//                 TFTPERROR_UNKNOWN_TID error
//                 In that case, the error message is ignored
//
//When an invalid packet is found, the packet is discarded.
//
void CTFTPRXTask::EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port){
  CTFTPTask::EvtUDPReceived(payload,payloadlen,remote_addr,remote_port);
  
  //validate the packet format
  if (payloadlen<4) {
    TRACE_PRINTF("Discard Packet: payloadlen<4\n");
    return;
  }

  //Check remote address
  if (!ip_addr_cmp(&server_addr,&remote_addr)) {
    TRACE_PRINTF("Discard Packet: remote_addr != server_addr\n");
    return;
  }
  
  //Check remote port
  //If server TID is not accepted, accept any remote port
  //Otherwise, accept remote_port == server_port_ only
  if (server_tid_accepted_ && remote_port!=server_port_) {
    TRACE_PRINTF("Discard Packet: Invalid remote port\n");
    return;
  }
  
  //check opcode should be 1-6
  const uint16_t opcode = payload[0]*256+payload[1];
  if (opcode==0 || opcode >=7) {
    TRACE_PRINTF("Discard Packet: Invalid TFTP OPCODE\n");
    return;
  }
  
  if (opcode == OP_OACK) ProcessOACKPacket(payload,payloadlen,remote_port);
  else if (opcode == OP_DATA) ProcessDataPacket(payload,payloadlen,remote_port);
  else if (opcode == OP_ERROR) ProcessErrorPacket(payload, payloadlen);
  //discard other opcode packets
}

/**
 *  \brief Process OACK Packet
 *  
 *  \param [in] payload Pointer to Packet payload
 *  \param [in] payloadlen Payload Len
 *  \param [in] remote_port Remote Port
 *  
 *  \details Parse the option-value pair from the payload.
 *  then call the corresponding handler.
 *  Restart the transfer if blksize option from server is invalid.
 */
void CTFTPRXTask::ProcessOACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port) {
  //Aceept OACKPacket once and before first data packet
  if (oack_received_ || block_received_!=0) return;
  oack_received_ = true;
  DEBUG_PRINTF("OACK Received\n");
  
  //Accept remote_port (TID) 
  if (!server_tid_accepted_){
    server_port_ = remote_port;
    server_tid_accepted_ = true;
    DEBUG_PRINTF("Setting server_port_ to %d\n",remote_port);    
  }
  
  //Parse and handle the options
  size_t current_pos=2;    //Option-Value pair starts at offset 2
  const char *option,*value;
  bool need_restart = false;
  while(ParseOptions(payload,payloadlen,&current_pos,&option,&value)) {
    DEBUG_PRINTF("option: %s=%s\n",option,value);
    if (0==strcmp(option,"tsize")) HandleOACK_tsize(value);
    else if (0==strcmp(option,"blksize")) {
      if (!HandleOACK_blksize(value)) {
        //Restart the transfer if HandleOACK_blksize() failed.
        need_restart = true; 
        break;
      }
    }
  }//while
  
  if (need_restart) {
    //
    // Restart the transfer
    //
    
    //Send Error Packet to terminate the previous connection
    BuildErrorPacket(TFTPERROR_DENIED_OPTIONS);   //error code #8
    SendPacket();
    
    WARN_PRINTF("Unrecongised blksize. Restarting transfer without blksize option\n");
    enable_1k_block_size_ = false;  //Turn off 1024 blksize option
    oack_received_ = false;   
    server_tid_accepted_ = false;
    block_received_ = 0;    
    tftp_block_size_ = 512;    
  
    //rebind UDP local interface so that local_port is changed and the server should recognise it as a new connection
    cyw43_arch_lwip_begin();      
    udp_bind(this->pcb, IP4_ADDR_ANY, 0 /*random port*/); //Bind local IF
    cyw43_arch_lwip_end();        

    //Start again
    this->CancelTimer();      //Cancel any pending timer event
    this->StartTransfer();    //Send RRQ to start the transfer
    return;
  }
  
  //Everything is ok.
  //Start data transfer by sending Ack with block# = 0
  DEBUG_PRINTF("Sending Ack of the OACK packet\n");
  BuildAckPacket(0); //Block Number 0
  SendPacket();
  SetTimer(tftp_timeout_);
  attempt_ = 1; //First Attempt    
}


//////////////////////////////////////////////////////////
// Handle tsize option acknowledgement from server
// Validate the value. Then, write it to tftp_state
// tsize is for info only. Ignore if the value is invalid.
//
void CTFTPRXTask::HandleOACK_tsize(const char* value) {
  //ignore if value is an empty string
  if (strlen(value)==0) return;
  
  //Convert it to unsigned integer
  const uint32_t tsize = strtoul(value,NULL,10);

  //ignore if it is 0.
  if (tsize == 0) return;
  
  //Write to tftpstate
  tftp_critical_section_enter_blocking();
  tftp_state.tsize = tsize;
  tftp_critical_section_exit();
  DEBUG_PRINTF("tftp_state.tsize = %d\n",tsize);
}

////////////////////////////////////////////////////////////////////
// Check if the block number exceeds the capacity of the unit
//
// If it is valid, return true;
// If it is not, set error and terminate the process.
//
bool CTFTPRXTask::ValidateBlockNumber(const uint32_t block_num) {
  //Check if number of blocks received exceeds the capacity of the unit
  if (block_num<block_capacity_) 
    return true;
  else { 
    //Set error and stop immedately
    //The server will timeout and quit
    this->Complete();      
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETED;
    tftp_state.error = TFTPERROR_OVERSIZE;
    tftp_critical_section_exit();
    ERROR_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");
    ERROR_PRINTF("tftp_state.error = TFTPERROR_OVERSIZE\n");
    return false;
  }  
}


//////////////////////////////////////////////////////////
// Process Data Packet
// Assume the packet is valid.
//
void CTFTPRXTask::ProcessDataPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port) {
  const uint16_t block = payload[2]*256+payload[3];
  const uint16_t data_size = payloadlen-4;

  //Validate Block Number
  if (block != expected_block_) {
    if (block==expected_block_-1) {
      //Last Data Block is received. It means Ack is lost. 
      //Retry without any delay.
      this->Retry(); 
      return;
    }
    else {
      TRACE_PRINTF("Discard Packet: Invalid Block Number\n");
      return;
    }
  }
  
  //
  //Valid Data Packet!
  //  

  //First data packet?
  if (block_received_==0) { 
    DEBUG_PRINTF("First data packet received\n");
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_TRANSFER;
    tftp_critical_section_exit();
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_TRANSFER\n");
    
    //Accept remote_port (TID)
    if (!server_tid_accepted_){
      server_port_ = remote_port;
      server_tid_accepted_ = true;
      DEBUG_PRINTF("Setting server_port_ to %d\n",remote_port);    
    }
  }

  assert(tftp_block_size_==512 || tftp_block_size_==1024); //Assume tftp_block_size_ is 512 or 1024 only  
  //TFTP protocol says if the length of data (data_size) is < tftp_block_size_, it is
  //the last data packet. Since the size of a disk image should be
  //multiple of 512, we expect the data_size of last data packet is 0 or 512(if tftp_block_size_==1024).
  
  //=true if this Data Packet signals end of transmission but 
  //it also carry 512 bytes of data
  const bool eof_with512payload = (tftp_block_size_==1024 && data_size==512);
  
  //If data_size == 0 or eof_with512payload, it means end of transmission without any issues
  if (data_size == 0 || eof_with512payload) {
    BuildAckPacket(block);  //Send Last Ack.
    SendPacket();
    SetTimer(tftp_timeout_laskack_);
    has_completed_ = true;
    
    if (eof_with512payload) {
      #if WRITETOFLASH
      if (!ValidateBlockNumber(block_received_)) return;
      const bool success = image_writer_.WriteBlock(unit_num_, block_received_, payload+4);  //Actual Data starts at offset 4
      if (!success) throw CTFTPTask::ERR_RWFAILED;
      #endif
      ++block_received_;    
    }
    
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETING;
    tftp_state.error = TFTPERROR_NOERR;
    tftp_state.blockTransferred = block_received_;
    tftp_critical_section_exit();
    INFO_PRINTF("\ntftp_state.status = TFTPSTATUS_COMPLETING\n");
    return;    
  }
  
  
  //
  //The Packet contains payload data.
  //

  //Check for oversize and odd filesize.
  //We want the oversize error has priorty
  //over odd filesize. So, we check for it first.
  
  //Check if number of blocks received exceeds the capacity of the unit
  //If it is not valid, ValidateBlockNumber() sets error and terminate the process.
  if (!ValidateBlockNumber(block_received_)) return;
  
  //If data_size is < tftp_block_size_, it signals end of transmission
  //But the filesize is not multiple of 512
  if (data_size < tftp_block_size_ && !eof_with512payload) {
    BuildAckPacket(block);  //Send Last Ack.
    SendPacket();
    SetTimer(tftp_timeout_laskack_);
    attempt_ = 1;  //Reset attempt to 1 since we have a good data block
    has_completed_ = true;  //To end the transmission after timer timeout
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETING;
    tftp_state.error = TFTPERROR_ODDSIZE;
    tftp_critical_section_exit();
    ERROR_PRINTF("\ntftp_state.status = TFTPSTATUS_COMPLETING\n");
    ERROR_PRINTF("tftp_state.error = TFTPERROR_ODDSIZE\n");
    return;
  }
  
  //If data_size = tftp_block_size_, it is a normal and good data block
  //write it to flash
  if (data_size == tftp_block_size_) {
    //Send ACK
    BuildAckPacket(block);
    SendPacket();
    SetTimer(tftp_timeout_);    
    attempt_=1;    //Reset attempt to 1 since we have a good data block
    ++expected_block_;
    
    #if WRITETOFLASH
    //block_received_ has been validated above
    const bool success = image_writer_.WriteBlock(unit_num_, block_received_, payload+4);  //Actual Data starts at offset 4
    if (!success) throw CTFTPTask::ERR_RWFAILED;
    #endif
    ++block_received_;    
    tftp_critical_section_enter_blocking();
    tftp_state.blockTransferred = block_received_;
    tftp_critical_section_exit();    
    
    if (data_size==1024) {
      #if WRITETOFLASH
      if (!ValidateBlockNumber(block_received_)) return;      
      const bool success = image_writer_.WriteBlock(unit_num_, block_received_, payload+4+512);  //Actual Data starts at offset 4
      if (!success) throw CTFTPTask::ERR_RWFAILED;
      #endif
      ++block_received_;
      tftp_critical_section_enter_blocking();
      tftp_state.blockTransferred = block_received_;
      tftp_critical_section_exit();    
    }
    
    return;
  } 

  TRACE_PRINTF("Discard Packet: Invalid block size\n");
}


//////////////////////////////////////////////////////////
// ProcessErrorPacket() method
// See CTFTP::ProcessErrorPacket
//

//////////////////////////////////////////////////////////
// Timer timeout Handler
//
// Send the last packet by calling retry
// If has_completed_ is set, end the process by calling Complete()
// and then return. See the note about Handling of Last Ack
//
void CTFTPRXTask::EvtTimeout(uint32_t arg){
  if (has_completed_) {
    this->Complete();
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETED;
    tftp_critical_section_exit();    
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");    
    INFO_PRINTF("RX Transfer Completed! Block Count = %d\n",block_received_);
    return;
  }
  
  this->Retry();
}

/////////////////////////////////////////////////////////////
// Retry() Method
//
// Send the last packet again.
// If has_completed_ is set, send the last ACK Packet one more
// time and then stop
//
void CTFTPRXTask::Retry() {
  if (has_completed_) {
    //Try Send last ACK Packet one more time and then stop
    INFO_PRINTF("Sending last ACK Packet again\n");
    SendPacket();
    this->Complete();
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETED;
    tftp_critical_section_exit();   
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");    
    INFO_PRINTF("RX Transfer Completed! Block Count = %d\n",block_received_);  
    return;    
  }
  
  //Call base class implemntation
  CTFTPTask::Retry();
}


/**************************************************************************************
Handling of last ACK

After the last Data packet is received, it is acknowledged by a ACK Packet. But this
last ACK Packet can be lost. We can do nothing and simply let the server to timeout.
But we can do it better.

When the last data packet is received, a flag has_completed_ is set and timer with slightly
longer Timeout period is started.

There are 2 possible situation. 

1) The last ACK is sent sucessfully. In this case, the server will not reply anything. 
Timer will timeout. EvtTimeout() is called and it ends the TFTP process.

2) The last ACK is lost. In this case, the server will resend last packet. EvtUDPReceived()
handler will be called and eventually, it reaches Retry() method. Retry() will send the 
ACK packet one more time and then complete the process

****************************************************************************************/

/////////////////////////////////////////////////////////////
// Complete Method
//
// The size of DOS 3.3 image file should be multiple of 8 blocks.
// If it is not, some block data remain in the buffer of image_writer_
// object. In this case, shows error message (TFTPERROR_DOWRONGSIZE)
//
void CTFTPRXTask::Complete() {
  if (image_writer_.getImageFormat()==ImageFormat::DO && !image_writer_.IsCompleted()) {
    //This error has a lower priority. If another error has occured, don't
    //overwrite tftp_state.error
    if (tftp_state.error == TFTPERROR_NOERR) {
      INFO_PRINTF("DOS Order Image File: Incorect Size\n");
      tftp_critical_section_enter_blocking();
      tftp_state.error = TFTPERROR_DOWRONGSIZE;
      tftp_critical_section_exit();
    }
  }
  
  //Call base class implementation
  CTFTPTask::Complete();
}
