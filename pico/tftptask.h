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

  CTFTPTask(const uint32_t unitNum,const char* hostname,const char* filename,const bool enable1kBlockSize,const uint32_t tftpTimeout,
            const uint32_t tftpMaxAttempt,const uint16_t tftpServerPort);
  ~CTFTPTask();
  
protected:
  //
  // Transfer Parameters
  //
  uint32_t unitNum;
  const char* hostname;
  const char* filename;
  bool enable1kBlockSize;   //Enable blksize and tsize TFTP Option Negotiation
  uint32_t tftpTimeout;     //TFTP Timeout in ms
  uint32_t tftpMaxAttempt;  //Maximum Number of retries
  uint16_t tftpServerPort;  //TFTP Server Listening Port
  uint32_t tftpTimeoutLastACK; //TFTP Timeout of Last ACK in ms

  //Variables
  uint32_t attempt;       //To track the retry count

  //
  // Server and TX Buffer
  //
  uint16_t server_port;
  uint8_t *txbuffer;
  uint32_t txpacketlen;

  //
  // Methods
  //
  void UpdateTFTPState();
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
  // Send the packet in txbuffer to server
  // 
  void SendPacket() {
    assert(this->server_port != 0);
    SendUDP(txbuffer,txpacketlen,this->server_port);
  }
  
  void SendPacket(const uint8_t *srcBuffer,const uint32_t len) {
      assert(this->server_port != 0);
    SendUDP(srcBuffer,len,this->server_port);
  }
  
  //
  //Method common to both CTFTPRXTask and CTFTPTXTask
  //
  void EvtStart();
  virtual void Retry();
  void ProcessErrorPacket(const uint8_t* payload,uint16_t payloadlen);

private:  
  void BuildRQPacket(const uint8_t type);
};

#endif