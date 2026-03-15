#ifndef _TFTPTXTASK_H
#define _TFTPTXTASK_H

#include "tftptask.h"





class CTFTPTXTask:public CTFTPTask {
public:
  CTFTPTXTask(const uint32_t unitNum,const char* hostname,const char* filename,const bool enable1kBlockSize,const uint32_t tftpTimeout,const uint32_t tftpMaxAttempt,
              const uint16_t tftpServerPort);
  virtual ~CTFTPTXTask();
  
  //Override Run()
  virtual void Run(const char* ssid, const char* wpakey);
  
protected:
  //Next Data Packet Buffer
  uint8_t *nextDataPacketBuf;   
  uint32_t nextDataPacketLen;

  bool OACKReceived;           //To indicate OACK Packet has been received
  uint16_t current_tftpBlock;  //The TFTP block number we have sent and the ACK we expected
  bool hasCompleted;           //To indicate the last Data packet has been sent. Waiting for last Ack
  uint32_t blockSent;          //Number of ProDOS block sent
  uint32_t blockCount;         //Total Number of ProDOS block of the unit
  uint32_t tftpBlockSize;      //TFTP block size (512 or 1024)
  bool serverTIDAccepted;      //Server TID (remote_port) accepted

  //Event Handlers
  void EvtDNSResult(const int dns_error, const ip_addr_t *ipaddr);
  void EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port);
  void EvtTimeout(uint32_t arg);
  
  void StartTransfer();  
  void SendDataPacket(); 
  void ProcessOACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port);
  void ProcessACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port);
  void HandleOACK_blksize(const char* value);
  
  uint32_t BuildDataPacket(uint8_t *destBuffer,uint16_t tftpBlock, uint32_t blockNum);
};



#endif
