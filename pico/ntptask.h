#ifndef _NTPTASK_H
#define _NTPTASK_H

#include "udptask.h"


class CNTPTask:public CUDPTask {
public:  
  static const int ERR_NTPFAILED = CUDPTask::ERR_SUBCLASS_BEGIN;

  CNTPTask();
  time_t GetSecondsSince1970() {return secondsSince1970;}
  
protected:  
  uint32_t attempt;
  time_t secondsSince1970;  //result
  
  void AttemptNTP();
  const char* GetNTPServerHostname();
  void LookupNTPServer();
  void SendNTPRequest();

  //Override base class methods
  virtual void EvtStart() override;
  virtual void EvtDNSResult(const int dnserr, const ip_addr_t *ipaddr) override;
  virtual void EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port) override;
  virtual void EvtTimeout(uint32_t arg) override;
};


#endif