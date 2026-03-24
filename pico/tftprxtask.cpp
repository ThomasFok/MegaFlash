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
CTFTPRXTask::CTFTPRXTask(const uint32_t unitNum,const char* hostname,const char* filename,const bool enable1kBlockSize,const uint32_t tftpTimeout,
                         const uint32_t tftpMaxAttempt,const uint16_t tftpServerPort)
                         :CTFTPTask(unitNum, hostname, filename,enable1kBlockSize,tftpTimeout,tftpMaxAttempt,tftpServerPort)
                         ,imageWriter(filename) {
  
  OACKReceived = false;
  hasCompleted = false;
  serverTIDAccepted = false;
  blockReceived = 0;
  tftpBlockSize = 512;
  blockCapacity = GetBlockCountActual(unitNum);
  DEBUG_PRINTF("blockCapacity = %d\n",blockCapacity);
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
  this->server_port = tftpServerPort; //Reset server_port TFTP Server listening port
  BuildReadRequestPacket();
  
  if (enable1kBlockSize) {
    AddOption("blksize","1024");
    AddOption("tsize","0");   //Request server to report file size
  }
  
  SendPacket();
  SetTimer(tftpTimeout);
  attempt = 1; //First Attempt
  expectedBlock = 1;  //Expecting to receive block #1
  
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
  //Otherwise, accept remote_port == server_port only
  if (serverTIDAccepted && remote_port!=server_port) {
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

////////////////////////////////////////////////////////////
// Process OACK Packet
//
// Parse the option-value pair from the payload
// then call the corresponding handler.
// Handler may throw E_NEEDRESTART. In this case,
// the data transfer is restarted.
#define E_NEEDRESTART (0)
void CTFTPRXTask::ProcessOACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port) {
  //Aceept OACKPacket once and before first data packet
  if (OACKReceived || blockReceived!=0) return;
  OACKReceived = true;
  DEBUG_PRINTF("OACK Received\n");
  
  //Accept remote_port (TID) 
  if (!serverTIDAccepted){
    server_port = remote_port;
    serverTIDAccepted = true;
    DEBUG_PRINTF("Setting server_port to %d\n",remote_port);    
  }
  
  //Parse and handle the options
  size_t currentPos=2;    //Option-Value pair starts at offset 2
  const char *option,*value;
  try {
    while(ParseOptions(payload,payloadlen,&currentPos,&option,&value)) {
      DEBUG_PRINTF("option: %s=%s\n",option,value);
      if (0==strcmp(option,"blksize")) HandleOACK_blksize(value);
      else if (0==strcmp(option,"tsize")) HandleOACK_tsize(value);
    }
  } catch(int e) {
    if (e==E_NEEDRESTART) {
      //
      // Restart the transfer
      //
      
      //Send Error Packet to terminate the previous connection
      BuildErrorPacket(TFTPERROR_DENIED_OPTIONS);   //error code #8
      SendPacket();
    
      //rebind UDP local interface so that local_port is changed and the server should recognise it as a new connection
      cyw43_arch_lwip_begin();      
      udp_bind(this->pcb, IP4_ADDR_ANY, 0 /*random port*/); //Bind local IF
      cyw43_arch_lwip_end();        
      OACKReceived = false; 

      //Start again
      this->CancelTimer();      //Cancel any pending timer event
      this->StartTransfer();    //Send RRQ to start the transfer
      return;
    }
    assert(0); //unknown exception. should not happen
    return;
  }
  
  //Everything is ok.
  //Start data transfer by sending Ack with block# = 0
  DEBUG_PRINTF("Sending Ack of the OACK packet\n");
  BuildAckPacket(0); //Block Number 0
  SendPacket();
  SetTimer(tftpTimeout);
  attempt = 1; //First Attempt    
}

//////////////////////////////////////////////////////////
// Handle blksize option acknowledgement from server
//
// throw E_NEEDRESTART if the blksize is not 512 or 1024
//
void CTFTPRXTask::HandleOACK_blksize(const char* value) {
  const bool is1024 = (0==strcmp(value,"1024"));
  const bool is512 = (0==strcmp(value,"512"));
  
  //Only 512 and 1024 are acceptable
  if (is512 || is1024) {
    if (is1024) {
      INFO_PRINTF("Switching TFTP blockSize to 1024\n");
      tftpBlockSize=1024;
    }
  } else {
    //Unrecognised blksize. 
    //We don't expect it would happen since 1024 is a perfectly good
    //block size. 
    //Anyway, we try to restart the transfer without 1024 blksize option
    WARN_PRINTF("Unrecongised blksize. Restarting transfer without blksize option\n");
    enable1kBlockSize = false;  //Turn off 1024 blksize option
    throw E_NEEDRESTART;
  }
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
bool CTFTPRXTask::ValidateBlockNumber(const uint32_t blockNum) {
  //Check if number of blocks received exceeds the capacity of the unit
  if (blockNum<blockCapacity) 
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
  const uint16_t dataSize = payloadlen-4;

  //Validate Block Number
  if (block != expectedBlock) {
    if (block==expectedBlock-1) {
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
  if (blockReceived==0) { 
    DEBUG_PRINTF("First data packet received\n");
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_TRANSFER;
    tftp_critical_section_exit();
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_TRANSFER\n");
    
    //Accept remote_port (TID)
    if (!serverTIDAccepted){
      server_port = remote_port;
      serverTIDAccepted = true;
      DEBUG_PRINTF("Setting server_port to %d\n",remote_port);    
    }
  }

  assert(tftpBlockSize==512 || tftpBlockSize==1024); //Assume tftpBlockSize is 512 or 1024 only  
  //TFTP protocol says if the length of data (dataSize) is < tftpBlockSize, it is
  //the last data packet. Since the size of a disk image should be
  //multiple of 512, we expect the dataSize of last data packet is 0 or 512(if tftpBlockSize==1024).
  
  //=true if this Data Packet signals end of transmission but 
  //it also carry 512 bytes of data
  const bool eof_with512payload = (tftpBlockSize==1024 && dataSize==512);
  
  
  //If dataSize == 0 or eof_with512payload, it means end of transmission without any issues
  if (dataSize == 0 || eof_with512payload) {
    BuildAckPacket(block);  //Send Last Ack.
    SendPacket();
    SetTimer(tftpTimeoutLastACK);
    hasCompleted = true;
    
    if (eof_with512payload) {
      #if WRITETOFLASH
      if (!ValidateBlockNumber(blockReceived)) return;
      const bool success = imageWriter.WriteBlock(unitNum, blockReceived, payload+4);  //Actual Data starts at offset 4
      if (!success) throw CTFTPTask::ERR_RWFAILED;
      #endif
      ++blockReceived;    
    }
    
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETING;
    tftp_state.error = TFTPERROR_NOERR;
    tftp_state.blockTransferred = blockReceived;
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
  if (!ValidateBlockNumber(blockReceived)) return;
  
  //If dataSize is < tftpBlockSize, it signals end of transmission
  //But the filesize is not multiple of 512
  if (dataSize < tftpBlockSize && !eof_with512payload) {
    BuildAckPacket(block);  //Send Last Ack.
    SendPacket();
    SetTimer(tftpTimeoutLastACK);
    attempt = 1;  //Reset attempt to 1 since we have a good data block
    hasCompleted = true;  //To end the transmission after timer timeout
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETING;
    tftp_state.error = TFTPERROR_ODDSIZE;
    tftp_critical_section_exit();
    ERROR_PRINTF("\ntftp_state.status = TFTPSTATUS_COMPLETING\n");
    ERROR_PRINTF("tftp_state.error = TFTPERROR_ODDSIZE\n");
    return;
  }
  
  //If dataSize = tftpBlockSize, it is a normal and good data block
  //write it to flash
  if (dataSize == tftpBlockSize) {
    //Send ACK
    BuildAckPacket(block);
    SendPacket();
    SetTimer(tftpTimeout);    
    attempt=1;    //Reset attempt to 1 since we have a good data block
    ++expectedBlock;
    
    #if WRITETOFLASH
    //blockReceived has been validated above
    const bool success = imageWriter.WriteBlock(unitNum, blockReceived, payload+4);  //Actual Data starts at offset 4
    if (!success) throw CTFTPTask::ERR_RWFAILED;
    #endif
    ++blockReceived;    
    tftp_critical_section_enter_blocking();
    tftp_state.blockTransferred = blockReceived;
    tftp_critical_section_exit();    
    
    if (dataSize==1024) {
      #if WRITETOFLASH
      if (!ValidateBlockNumber(blockReceived)) return;      
      const bool success = imageWriter.WriteBlock(unitNum, blockReceived, payload+4+512);  //Actual Data starts at offset 4
      if (!success) throw CTFTPTask::ERR_RWFAILED;
      #endif
      ++blockReceived;
      tftp_critical_section_enter_blocking();
      tftp_state.blockTransferred = blockReceived;
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
// If hasCompleted is set, end the process by calling Complete()
// and then return. See the note about Handling of Last Ack
//
void CTFTPRXTask::EvtTimeout(uint32_t arg){
  if (hasCompleted) {
    this->Complete();
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETED;
    tftp_critical_section_exit();    
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");    
    INFO_PRINTF("RX Transfer Completed! Block Count = %d\n",blockReceived);
    return;
  }
  
  this->Retry();
}

/////////////////////////////////////////////////////////////
// Retry() Method
//
// Send the last packet again.
// If hasCompleted is set, send the last ACK Packet one more
// time and then stop
//
void CTFTPRXTask::Retry() {
  if (hasCompleted) {
    //Try Send last ACK Packet one more time and then stop
    INFO_PRINTF("Sending last ACK Packet again\n");
    SendPacket();
    this->Complete();
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_COMPLETED;
    tftp_critical_section_exit();   
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");    
    INFO_PRINTF("RX Transfer Completed! Block Count = %d\n",blockReceived);  
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

When the last data packet is received, a flag hasCompleted is set and timer with slightly
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
// If it is not, some block data remain in the buffer of imageWriter
// object. In this case, shows error message (TFTPERROR_DOWRONGSIZE)
//
void CTFTPRXTask::Complete() {
  if (imageWriter.getImageFormat()==ImageFormat::DO && !imageWriter.IsCompleted()) {
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
