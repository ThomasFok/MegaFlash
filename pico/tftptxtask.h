#ifndef _TFTPTXTASK_H
#define _TFTPTXTASK_H

#include "tftptask.h"


class CTFTPTXTask:public CTFTPTask {
public:
  CTFTPTXTask(const uint32_t unit_num,const char* hostname,const char* filename,const bool enable_1k_blocksize,const uint32_t tftp_timeout,
              const uint32_t tftp_max_attempt,const uint16_t tftp_server_port);
  virtual ~CTFTPTXTask();
  
  //Override Run()
  virtual void Run(const char* ssid, const char* wpakey) override;
  
protected:
  //Next Data Packet Buffer
  uint8_t *next_data_packet_buf_;   
  uint32_t next_data_packet_len_;

  bool oack_received_;         //To indicate OACK Packet has been received
  uint16_t current_tftp_block_;//The TFTP block number we have sent and the ACK we expected
  bool has_completed_;         //To indicate the last Data packet has been sent. Waiting for last Ack
  uint32_t block_sent_;        //Number of ProDOS block sent
  uint32_t block_count_;       //Total Number of ProDOS block of the unit
  uint32_t tftp_block_size_;   //TFTP block size (512 or 1024)
  bool server_tid_accepted_;   //Server TID (remote_port) accepted

  //Event Handlers
  virtual void EvtDNSResult(const int dns_error, const ip_addr_t *ipaddr) override;
  virtual void EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port) override;
  virtual void EvtTimeout(uint32_t arg) override;
  
  void StartTransfer();  
  void SendDataPacket(); 
  void ProcessOACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port);
  void ProcessACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port);
  bool HandleOACK_blksize(const char* value);
  
  uint32_t BuildDataPacket(uint8_t *dest_buffer,uint16_t tftp_block_num, uint32_t block_num);
};

#endif
