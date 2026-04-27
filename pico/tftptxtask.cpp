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
CTFTPTXTask::CTFTPTXTask(const uint32_t unit_num,const char* hostname,const char* filename,const bool enable_1k_blocksize,const uint32_t tftp_timeout,
                         const uint32_t tftp_max_attempt,const uint16_t tftp_server_port):
                         CTFTPTask(unit_num, hostname, filename,enable_1k_blocksize,tftp_timeout,tftp_max_attempt,tftp_server_port) {
  
  next_data_packet_buf_ = new uint8_t[TXBUFFERSIZE];
  next_data_packet_len_ = 0;
  
  oack_received_ = false;
  has_completed_ = false; 
  server_tid_accepted_ = false;
  block_sent_ = 0;
  tftp_block_size_ = 512;
  block_count_ = GetBlockCountForImageTransfer(unit_num); //Number of ProDOS blocks to be sent
  DEBUG_PRINTF("Total block_count_=%d\n",block_count_);
}

//Destructor
//
CTFTPTXTask::~CTFTPTXTask() {
  delete []next_data_packet_buf_;
}


//Override Run()
//
void CTFTPTXTask::Run(const char* ssid, const char* wpakey){
  tftp_critical_section_enter_blocking();
  tftp_state.status = TFTPSTATUS_WIFICONNECTING;
  tftp_state.tsize = block_count_ * PRODOS_BLOCKSIZE;    //Size of file being sent in bytes.
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
  this->server_port_ = tftp_server_port_; //Reset server_port TFTP Server listening port
  BuildWriteRequestPacket();
  if (enable_1k_block_size_) {
    AddOption("blksize","1024");    
    AddOption("tsize", block_count_*PRODOS_BLOCKSIZE); //Tell server the file size
  }
  
  SendPacket();
  SetTimer(tftp_timeout_);
  attempt_ = 1; //First Attempt
  current_tftp_block_ = 0;  //Expecting to receive Ack Block #0 or OACK
  
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
  if (oack_received_ || block_sent_!=0) return;
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
    if (0==strcmp(option,"blksize")) {
      if (!HandleOACK_blksize(value)) {
        //Restart the transfer if HandleOACK_blksize() failed.
        need_restart = true; 
        break;
      }
    }
  } //while

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
    block_sent_ = 0;
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
  //Start data transfer by sending Data Packet
  SendDataPacket();
}

//////////////////////////////////////////////////////////
// Build and send Data packet
//
void CTFTPTXTask::SendDataPacket() {
  //First Data Packet?
  if (block_sent_==0) {
    //Prepare first Data packet
    assert(current_tftp_block_==0);
    next_data_packet_len_ = BuildDataPacket(next_data_packet_buf_,current_tftp_block_+1,block_sent_);
    
    tftp_critical_section_enter_blocking();
    tftp_state.status = TFTPSTATUS_TRANSFER;
    tftp_critical_section_exit();
    INFO_PRINTF("tftp_state.status = TFTPSTATUS_TRANSFER\n");    
  }
  
  //Send Data Packet already in nextPacketBuf
  ++current_tftp_block_; //Advance to next TFTP block
  TRACE_PRINTF("Sending TFTP Data Packet Block=%d, len=%d\n",current_tftp_block_,next_data_packet_len_);
  SendPacket(next_data_packet_buf_,next_data_packet_len_);
  SetTimer(tftp_timeout_);
  attempt_ = 1; //First Attempt  
  
  /**** Prepare next Data Packet while waiting Ack from server ****/
  
  //Copy nextPacket to txbuffer_ for Retry()
  //Also, update block_sent_ and has_completed_
  memcpy(txbuffer_,next_data_packet_buf_,next_data_packet_len_);
  txpacketlen_ = next_data_packet_len_;  
  const uint32_t payloadlen = txpacketlen_ - 4; //4 bytes for Data Packet header
  assert(payloadlen==0 || payloadlen==512 || payloadlen==1024);
  block_sent_ += payloadlen/PRODOS_BLOCKSIZE;
  
  //Transfer Completed?
  //Not fully loaded payload means this is the last packet.
  //If tftp_block_size_ ==  512, payloadlen = 0 signals end of transfer
  //If tftp_block_size_ == 1024, payloadlen = 0 or 512 signals end of transfer
  if(payloadlen<tftp_block_size_) {
    has_completed_ = true;  
  } else {
    //No, Build Next Data Packet
    next_data_packet_len_ = BuildDataPacket(next_data_packet_buf_,current_tftp_block_+1,block_sent_);
  }
  
  tftp_critical_section_enter_blocking();
  if (has_completed_) tftp_state.status = TFTPSTATUS_COMPLETING;
  tftp_state.error = TFTPERROR_NOERR;
  tftp_state.blockTransferred = block_sent_;
  tftp_critical_section_exit();
  if (has_completed_) INFO_PRINTF("\ntftp_state.status = TFTPSTATUS_COMPLETING\n");
}

//////////////////////////////////////////////////////////
// Build Data Packet
// up to 2 ProDOS blocks are put into Data Packet.
//
// Input: dest_buffer    - Pointer to destination buffer
//        tftp_block_num - TFTP Block number of this packet
//        block_num      - ProDOS block number of payload data
//
// Output: uint32_t - Length of Data Packet
//
uint32_t CTFTPTXTask::BuildDataPacket(uint8_t *dest_buffer,uint16_t tftp_block_num, uint32_t block_num) {
  uint32_t packet_len = 4; //Length of header
  dest_buffer[0] = 0;
  dest_buffer[1] = OP_DATA;
  dest_buffer[3] = (uint8_t) tftp_block_num;      //low byte
  dest_buffer[2] = (uint8_t)(tftp_block_num>>8);  //high byte
  
  //Special case block_num == block_count_
  //All data have been sent. Send a Data packet without any payload
  //to end the transfer.
  assert(block_num <= block_count_);
  if (block_num == block_count_) {
    //Do nothing
  } else {
    //Put first block to payload
    assert(block_num<=0xffff);
    uint error = ReadBlock(unit_num_, block_num++, dest_buffer+4, NULL /*spErrorOut*/); //Read ProDOS block
    if (error!=MFERR_NONE) throw CTFTPTask::ERR_RWFAILED;   
    packet_len += PRODOS_BLOCKSIZE;
    
    //Put Second block to payload
    if (tftp_block_size_==1024) {
      if (block_num<block_count_) {
        assert(block_num<=0xffff);
        error = ReadBlock(unit_num_, block_num,dest_buffer+4+512, NULL /*spErrorOut*/); //Read ProDOS block
        if (error!=MFERR_NONE) throw CTFTPTask::ERR_RWFAILED;           
        packet_len += PRODOS_BLOCKSIZE;
      }     
    }
  }
  
  assert(packet_len==4 || packet_len==516 || packet_len==1028);
  return packet_len;
}




//////////////////////////////////////////////////////////
// Process ACK
// Accept remote_port if this is the first ACK Packet
//
void CTFTPTXTask::ProcessACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port){
  const uint16_t block = payload[2]*256+payload[3];
  TRACE_PRINTF("ACK Received block=%d\n",block);
  
  //First ACK Packet
  if (block==0 && block_sent_==0) {
    //Accept remote_port (TID)
    if (!server_tid_accepted_){
      server_port_ = remote_port;
      server_tid_accepted_ = true;
      DEBUG_PRINTF("Setting server_port_ to %d\n",remote_port);    
    }      
  }
  
  //Is it the ACK we are waiting for?
  if (block==current_tftp_block_) {
    //Proper Ack Received. Send next Data Packet unless has_completed_ is set
    if (has_completed_) {
      this->Complete();
      tftp_critical_section_enter_blocking();
      tftp_state.status = TFTPSTATUS_COMPLETED;
      tftp_critical_section_exit();    
      INFO_PRINTF("tftp_state.status = TFTPSTATUS_COMPLETED\n");    
      INFO_PRINTF("TX Transfer Completed! Block Count = %d\n",block_sent_);
    }
    else {
      SendDataPacket();
      return;
    }
  } else {
    //block number is not expected one.
    if (block==current_tftp_block_-1) {
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
