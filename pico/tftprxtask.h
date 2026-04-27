#ifndef _TFTPRXTASK_H
#define _TFTPRXTASK_H

#include "tftptask.h"
#include "imagewriter.h"


class CTFTPRXTask:public CTFTPTask {
public:
  CTFTPRXTask(const uint32_t unit_num,const char* hostname,const char* filename,const bool enable_1k_block_size,const uint32_t tftp_timeout,
              const uint32_t tftp_max_attempt,const uint16_t tftp_server_port);

  //Override Run()
  virtual void Run(const char* ssid, const char* wpakey) override;
  
protected:
  uint32_t block_received_;   //Number of ProDOS block received
  uint32_t block_capacity_;   //The capacity of the unit in number of ProDOS blocks.
  uint16_t expected_block_;   //The TFTP block number we are expecting

  //Event Handlers
  virtual void EvtDNSResult(const int dns_error, const ip_addr_t *ipaddr) override;
  virtual void EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port) override;
  virtual void EvtTimeout(uint32_t arg) override;

  //overridden virtual methods
  virtual void Retry() override; 
  virtual void Complete() override;

  void StartTransfer();  
  void ProcessOACKPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port);
  void ProcessDataPacket(const uint8_t* payload,uint16_t payloadlen,uint16_t remote_port);
  void HandleOACK_tsize(const char* value);
    
private:
  //Helper method
  bool ValidateBlockNumber(const uint32_t block_num);
  
  //Helper object
  CImageWriter image_writer_;
};



#endif
