#include "debug.h"
#include "ntptask.h"
#include "userconfig.h"

#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800ull // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_MAX_RETRY 5         // retry up to 5 times
#define NTP_TIMEOUT 5000

//If the timestamp from NTP is smaller than this value, assume it has rolled-over. (will happen in 2036)
#define ROLLOVER_LIMIT 3955046400    //1st May,2025 12:00am, 3955046400 = 2208988800 + 1746057600(unix epoch of 1,May,2025)

CNTPTask::CNTPTask():CUDPTask() {
  static_assert(sizeof(secondsSince1970)==8);
  attempt = 0;
  secondsSince1970 = 0;
}

/////////////////////////////////////////////////////////////////////////
// Attempt to query NTP Server
//
// The operation flow is:
//
// LookupNTPServer() -> EvtDNSResult()  -> SendNTPRequest() -> EvtUDPReceived
//                                                          -> EvtTimeout
//
// We are sending request to a server pool. The DNS Request returns different
// server on each query.
// We may start the NTP request by calling LookupNTPServer() if we want to get
// another NTP Server IP from DNS or calling SendNTPRequest() if we want to use
// the same server again.  
//
void CNTPTask::AttemptNTP() {
  if (attempt==0) {
    //Initial Attempt
    LookupNTPServer();
  }  
  else if (attempt==1) {
    //First Retry, use the same NTP server
    SendNTPRequest();
  } 
  else if (attempt>=2 && attempt<NTP_MAX_RETRY) {
    //Subsequent Retry, try another NTP server
    LookupNTPServer();
  } 
  else {
    //Giveup
    throw CNTPTask::ERR_NTPFAILED;
  }
}

void CNTPTask::EvtStart() {
  CUDPTask::EvtStart();
  AttemptNTP();
}


void CNTPTask::EvtDNSResult(const int dnserr, const ip_addr_t *ipaddr){
  CUDPTask::EvtDNSResult(dnserr, ipaddr);
  assert(GetServerIpResolved());
  SendNTPRequest();
}

void CNTPTask::EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port){
  CUDPTask::EvtUDPReceived(payload, payloadlen, remote_addr, remote_port);
  
  //Cancel Timeout timer  
  CancelTimer();  
  
  //Validate the packet
  const uint8_t mode = payload[0] & 0x7;
  const uint8_t stratum = payload[1];
  
  if (ip_addr_cmp(&remote_addr,&server_addr) &&
      remote_port == NTP_PORT &&
      payloadlen  == NTP_MSG_LEN &&
      mode == 0x04 &&
      stratum !=0) {
    
    DEBUG_PRINTF("Valid Response received from NTP Server\n");    
    this->Complete();
    
    //Get the time from payload
    const uint32_t secondsSince1900 = payload[40] << 24 | payload[41] << 16 | payload[42] << 8 | payload[43];
       
    //Convert to time_t
    if (secondsSince1900 < ROLLOVER_LIMIT) secondsSince1970 = secondsSince1900 + 0x100000000ull - NTP_DELTA;
    else secondsSince1970 = secondsSince1900 - NTP_DELTA;    
    INFO_PRINTF("unix epoch = %llu\n",secondsSince1970);
    
  } else {
    ERROR_PRINTF("Invalid response from NTP Server\n");
    AttemptNTP(); //Try again
  }
}


void CNTPTask::EvtTimeout(uint32_t arg){
  CUDPTask::EvtTimeout(arg);
  AttemptNTP(); //Try again
}


/////////////////////////////////////////////////
// Get NTP Server Hostname
//
// The following hostname are returned in round-robin fashion 
// unless the user has override the default setting.
//
// 0.pool.ntp.org
// 1.pool.ntp.org
// 2.pool.ntp.org
// 3.pool.ntp.org
//
const char* CNTPTask::GetNTPServerHostname() {
  const char* serverOverride = GetNTPServerOverride();
  if (serverOverride[0]!='\0') return serverOverride;
  
  static char hostname[] = "3.pool.ntp.org";
  
  //Advance to next server
  char c = hostname[0];
  hostname[0] = c=='3'?'0':c+1;
  
  return (const char*)hostname;
}


void CNTPTask::LookupNTPServer() {
  DNSLookup(GetNTPServerHostname());
}

void CNTPTask::SendNTPRequest() {
  assert(GetServerIpResolved());
  DEBUG_PRINTF("Sending NTP Request Attempt: #%d\n",attempt+1);
  uint8_t payload[NTP_MSG_LEN];
  memset(payload, 0, NTP_MSG_LEN);
  payload[0] = 0x23;  //0x1b for version 3, 0x23 for version 4
      
  SendUDP(payload, NTP_MSG_LEN, NTP_PORT);   
  ++attempt;
  
  SetTimer(NTP_TIMEOUT);
}