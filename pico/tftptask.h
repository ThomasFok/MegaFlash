#ifndef _TFTPTASK_H
#define _TFTPTASK_H

#include "udptask.h"
#include "tftpstate.h"

//Buffer Allocation
#define TFTP_HEADER_LEN 4
#define TFTP_BLOCKSIZE 512
#define TXBUFFERSIZE (TFTP_HEADER_LEN+PRODOS_BLOCKSIZE*2)


class CTFTPTask:public CUDPTask {
public:
  static const int ERR_RWFAILED = CUDPTask::ERR_SUBCLASS_BEGIN;

  const uint32_t OP_RRQ   = 1;
  const uint32_t OP_WRQ   = 2;
  const uint32_t OP_DATA  = 3;
  const uint32_t OP_ACK   = 4;
  const uint32_t OP_ERROR = 5;
  const uint32_t OP_OACK  = 6; //Option Acknowledgment

  CTFTPTask(const uint32_t unit_num,const char* hostname,const char* filename,const bool enable_1k_block_size,const uint32_t tftp_timeout,
            const uint32_t tftp_max_attempt,const uint16_t tftp_server_port);
  virtual ~CTFTPTask();
  
protected:
  //
  // Transfer Parameters
  //
  const char* hostname_;
  const char* filename_;
  uint32_t unit_num_;             //Source/Destination Drive unit number
  uint32_t tftp_timeout_;         //TFTP Timeout in ms
  uint32_t tftp_max_attempt_;     //Maximum Number of retries
  uint32_t tftp_timeout_laskack_; //TFTP Timeout of Last ACK in ms
  uint16_t tftp_server_port_;     //TFTP Server Listening Port
  bool enable_1k_block_size_;     //Enable blksize and tsize TFTP Option Negotiation

  //Variables common to both TFTPTXTask and TFTPRXTask
  uint32_t attempt_;           //To track the retry count
  uint32_t tftp_block_size_;   //TFTP block size (512 or 1024)
  bool oack_received_;         //To indicate OACK Packet has been received
  bool server_tid_accepted_;   //Server TID (remote_port) accepted
  bool has_completed_;         //To indicate the data transfer has completed 

  //
  // Server and TX Buffer
  //
  uint16_t server_port_;      //The server port for data transfer
  uint8_t *txbuffer_;
  uint32_t txpacketlen_;

  //
  // Methods
  //
  void InitTFTPState();
  const char* GetErrorMessage(const int errorcode);
  
  void BuildAckPacket(const uint16_t block);
  void BuildErrorPacket(const int errorcode);
  void BuildReadRequestPacket()  {BuildRQPacket(OP_RRQ);}
  void BuildWriteRequestPacket() {BuildRQPacket(OP_WRQ);}
  void AddOption(const char* option, const char* value);
  void AddOption(const char* option, const uint32_t value);
  void AddBinaryData(const uint8_t *data,const uint32_t len);
  bool ParseOptions(const uint8_t *buffer, const size_t len, size_t *currentPos,const char**option_out,const char**value_out);

  ////////////////////////////////////////////////////////////////////
  // Send the packet in txbuffer_ to server
  // 
  void SendPacket() {
    assert(this->server_port_ != 0);
    SendUDP(txbuffer_,txpacketlen_,this->server_port_);
  }
  
  void SendPacket(const uint8_t *srcBuffer,const uint32_t len) {
    assert(this->server_port_ != 0);
    SendUDP(srcBuffer,len,this->server_port_);
  }
  
  //
  //Methods common to both CTFTPRXTask and CTFTPTXTask
  //
  virtual void EvtStart() override;
  virtual void Retry();
  void ProcessErrorPacket(const uint8_t* payload,uint16_t payloadlen);
  bool HandleOACK_blksize(const char* value);

private:  
  void BuildRQPacket(const uint8_t type);
};

#endif