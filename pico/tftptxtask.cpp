#include "tftp.h"
#include "tftptask.h"
#include "tftptxtask.h"
#include "tftpstate.h"
#include "misc.h"
#include "mediaaccess.h"
#include "debug.h"
#include "defines.h"

extern "C" volatile tftp_state_t tftp_state;



// Constructor
// 
CTFTPTXTask::CTFTPTXTask(const uint32_t unitNum,const char* hostname,const char* filename,const bool enable1kBlockSize,const uint32_t tftpTimeout,
                         const uint32_t tftpMaxAttempt,const uint16_t tftpServerPort):
                         CTFTPTask(unitNum, hostname, filename,enable1kBlockSize,tftpTimeout,tftpMaxAttempt,tftpServerPort) {
  
  nextDataPacketBuf = new uint8_t[TXBUFFERSIZE];
  nextDataPacketLen = 0;
  
  OACKReceived = false;
  hasCompleted = false; 
  serverTIDAccepted = false;
  blockSent = 0;
  tftpBlockSize = 512;
  blockCount = GetBlockCountForImageTransfer(unitNum); //Number of ProDOS blocks to be sent
  DEBUG_PRINTF("Total blockCount=%d\n",blockCount);
}

//Destructor
//
CTFTPTXTask::~CTFTPTXTask() {
  delete []nextDataPacketBuf;
}


//Override Run()
//
void CTFTPTXTask::Run(const char* ssid, const char* wpakey){
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_WIFICONNECTING;
  tftp_state.tsize = blockCount * PRODOS_BLOCKSIZE;    //Size of file being sent in bytes.
  tftp_critical_section_exit();
  INFO_PRINTF("tftp_state.status = TFTPSTATUS_WIFICONNECTING\n");
  
  CTFTPTask::Run(ssid,wpakey);
}


//////////////////////////////////////////////////////////
//
// DNS Lookup Result Handler
//
void CTFTPTXTask::EvtDNSResult(const int dns_error, const ip_addr_t *ipaddr) {
  //Call base class implementation
  //Throw exception if hostname cannot be resolved.
  CTFTPTask::EvtDNSResult(dns_error,ipaddr);  
  
  //Hostname resolved.
  StartTransfer();
}

//////////////////////////////////////////////
// Start File Transfer by sending RRQ packet
//
void CTFTPTXTask::StartTransfer() {
  this->server_port = tftpServerPort; //Reset server_port TFTP Server listening port
  BuildWriteRequestPacket();
  if (enable1kBlockSize) {
    AddOption("blksize","1024");    
    AddOption("tsize", blockCount*PRODOS_BLOCKSIZE); //Tell server the file size
  }
  
  SendPacket();
  SetTimer(tftpTimeout);
  attempt = 1; //First Attempt
  current_tftpBlock = 0;  //Expecting to receive Ack Block #0 or OACK
  
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_REQUEST;
  tftp_critical_section_exit();
  INFO_PRINTF("tftp_state.status = TFTPSTATUS_REQUEST\n");
  //The server will response with OACK if option negotiaion is supported or
  //it will response with ACK#0.
}

//////////////////////////////////////////////////////////
//
// UDP Received Handler
//
// Discard all invalid packet and let the timeout mechanism
// to retry.
//
void CTFTPTXTask::EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port){
  CTFTPTask::EvtUDPReceived(payload,payloadlen,remote_addr,remote_port);
  
  //validate the packet format
  if (payloadlen<4) {
    TRACE_PRINTF("Discard Packet: payloadlen<4\n");
    return;
  }

  //Check remote address
  if (!ip_addr_cmp(&server_addr,&remote_addr))  {
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
  else if (opcode == OP_ACK) ProcessACKPacket(payload,payloadlen,remote_port);
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
void CTFTPTXTask::ProcessOACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port) {
  //Aceept OACKPacket once and before first data packet
  if (OACKReceived || blockSent!=0) return;
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
  bool needRestart = false;
  while(ParseOptions(payload,payloadlen,&currentPos,&option,&value)) {
    DEBUG_PRINTF("option: %s=%s\n",option,value);
    if (0==strcmp(option,"blksize")) {
      if (!HandleOACK_blksize(value)) {
        //Restart the transfer if HandleOACK_blksize() failed.
        needRestart = true; 
        break;
      }
    }
  } //while

  if (needRestart) {
    //
    // Restart the transfer
    //
    
    //Send Error Packet to terminate the previous connection
    BuildErrorPacket(TFTPERROR_DENIED_OPTIONS);   //error code #8
    SendPacket();

    WARN_PRINTF("Unrecongised blksize. Restarting transfer without blksize option\n");
    enable1kBlockSize = false;  //Turn off 1024 blksize option
    OACKReceived = false; 
    serverTIDAccepted = false;  
    blockSent = 0;
  
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
  //Start data transfer by sending Data Packet
  SendDataPacket();
}


/**
 *  \brief Handle blksize option acknowledgement from server
 *  
 *  \param [in] value blksize option string
 *  \return true if the blksize option is valid.
 */
bool CTFTPTXTask::HandleOACK_blksize(const char* value) {
  //Only 512 and 1024 are acceptable
  if (0==strcmp(value,"1024")) {
    INFO_PRINTF("Switching TFTP blockSize to 1024\n");
    tftpBlockSize=1024;
    return true;
  } else if (0==strcmp(value,"512")){
    return true;
  } else {
     //Unrecognised blksize. 
    //We don't expect it would happen since 1024 is a perfectly good
    //block size. 
    //Anyway, we try to restart the transfer without 1024 blksize option
    return false; 
  }
}


//////////////////////////////////////////////////////////
// Build and send Data packet
//
void CTFTPTXTask::SendDataPacket() {
  //First Data Packet?
  if (blockSent==0) {
    //Prepare first Data packet
    assert(current_tftpBlock==0);
    nextDataPacketLen = BuildDataPacket(nextDataPacketBuf,current_tftpBlock+1,blockSent);
    
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_TRANSFER;
    tftp_critical_section_exit();
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_TRANSFER\n");    
  }
  
  //Send Data Packet already in nextPacketBuf
  ++current_tftpBlock; //Advance to next TFTP block
  TRACE_PRINTF("Sending TFTP Data Packet Block=%d, len=%d\n",current_tftpBlock,nextDataPacketLen);
  SendPacket(nextDataPacketBuf,nextDataPacketLen);
  SetTimer(tftpTimeout);
  attempt = 1; //First Attempt  
  
  /**** Prepare next Data Packet while waiting Ack from server ****/
  
  //Copy nextPacket to txbuffer for Retry()
  //Also, update blockSent and hasCompleted
  memcpy(txbuffer,nextDataPacketBuf,nextDataPacketLen);
  txpacketlen = nextDataPacketLen;  
  const uint32_t payloadlen = txpacketlen - 4; //4 bytes for Data Packet header
  assert(payloadlen==0 || payloadlen==512 || payloadlen==1024);
  blockSent += payloadlen/PRODOS_BLOCKSIZE;
  
  //Transfer Completed?
  //Not fully loaded payload means this is the last packet.
  //If tftpBlockSize ==  512, payloadlen = 0 signals end of transfer
  //If tftpBlockSize == 1024, payloadlen = 0 or 512 signals end of transfer
  if(payloadlen<tftpBlockSize) {
    hasCompleted = true;  
  } else {
    //No, Build Next Data Packet
    nextDataPacketLen = BuildDataPacket(nextDataPacketBuf,current_tftpBlock+1,blockSent);
  }
  
  tftp_critical_section_enter_blocking();
  if (hasCompleted) tftp_state.status = TFTPSTATUS_COMPLETING;
  tftp_state.error = TFTPERROR_NOERR;
  tftp_state.blockTransferred = blockSent;
  tftp_critical_section_exit();
  if (hasCompleted) INFO_PRINTF("\ntftp_state.status = TFTPSTATUS_COMPLETING\n");
}

//////////////////////////////////////////////////////////
// Build Data Packet
// If tftpBlockSize, up to 2 ProDOS blocks is put into Data Packet.
//
// Input: destBuffer - Pointer to destination buffer
//        tftpBlock  - TFTP Block number of this packet
//        blockNum   - ProDOS block number of payload data
//
// Output: uint32_t - Length of Data Packet
//
uint32_t CTFTPTXTask::BuildDataPacket(uint8_t *destBuffer,uint16_t tftpBlockNum, uint32_t blockNum) {
  uint32_t packetLen = 4; //Length of header
  destBuffer[0] =0;
  destBuffer[1] = OP_DATA;
  destBuffer[3] = (uint8_t) tftpBlockNum;      //low byte
  destBuffer[2] = (uint8_t)(tftpBlockNum>>8);  //high byte
  
  //Special case blockNum == blockCount
  //All data has been sent. Send a Data packet without any payload
  //to end the transfer.
  assert(blockNum <= blockCount);
  if (blockNum == blockCount) {
    //Do nothing
  } else {
    //Put first block to payload
    assert(blockNum<=0xffff);
    uint error = ReadBlock(unitNum, blockNum++, destBuffer+4, NULL /*spErrorOut*/); //Read ProDOS block
    if (error!=MFERR_NONE) throw CTFTPTask::ERR_RWFAILED;   
    packetLen += PRODOS_BLOCKSIZE;
    
    //Put Second block to payload
    if (tftpBlockSize==1024) {
      if (blockNum<blockCount) {
        assert(blockNum<=0xffff);
        error = ReadBlock(unitNum, blockNum,destBuffer+4+512, NULL /*spErrorOut*/); //Read ProDOS block
        if (error!=MFERR_NONE) throw CTFTPTask::ERR_RWFAILED;           
        packetLen += PRODOS_BLOCKSIZE;
      }     
    }
  }
  
  assert(packetLen==4 || packetLen==516 || packetLen==1028);
  return packetLen;
}




//////////////////////////////////////////////////////////
// Process ACK
// Accept remote_port if this is the first ACK Packet
//
void CTFTPTXTask::ProcessACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port){
  const uint16_t block = payload[2]*256+payload[3];
  TRACE_PRINTF("ACK Received block=%d\n",block);
  
  //First ACK Packet
  if (block==0 && blockSent==0) {
    //Accept remote_port (TID)
    if (!serverTIDAccepted){
      server_port = remote_port;
      serverTIDAccepted = true;
      DEBUG_PRINTF("Setting server_port to %d\n",remote_port);    
    }      
  }
  
  //Is it the ACK we are waiting for?
  if (block==current_tftpBlock) {
    //Proper Ack Received. Send next Data Packet unless hasCompleted is set
    if (hasCompleted) {
      this->Complete();
      tftp_critical_section_enter_blocking();
      tftp_state.status = TFTPSTATUS_COMPLETED;
      tftp_critical_section_exit();    
      INFO_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");    
      INFO_PRINTF("TX Transfer Completed! Block Count = %d\n",blockSent);
    }
    else {
      SendDataPacket();
      return;
    }
  } else {
    //block number is not expected one.
    if (block==current_tftpBlock-1) {
      //If Ack of last Data Packet is received, it means the Data Block
      //is lost. 
      //Don't call Retry() immediately. It may cause a race condition
      //with server and causes a series of retransmission.
      //Do nothing and let the timeout handler to resend the packet. 
      INFO_PRINTF("Discard Packet: Last Data Packet Lost\n");
      return;
    } else {
      INFO_PRINTF("Discard Packet: Invalid Block Number\n");
      return;
    }
  }
}

//////////////////////////////////////////////////////////
// Timer timeout Handler
//
void CTFTPTXTask::EvtTimeout(uint32_t arg){
  this->Retry();
}

/////////////////////////////////////////////////////////////
// Retry() Method
// See CTFTPTask::Retry()


//////////////////////////////////////////////////////////
// ProcessErrorPacket() Method
// See CTFTP::ProcessErrorPacket()
