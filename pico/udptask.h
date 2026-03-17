#ifndef _UDPTASK_H
#define _UDPTASK_H

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "debug.h"
#include <queue>

#define DEFAULT_DNSTIMEOUT 5000       //DNS timeout in msec
#define UDP_BUFFERSIZE 1500           //UDP Packet Buffer Size
#define HEARTBEAT_PERIOD   50         //Execute event loop every HEARTBEAT_PERIOD in msec
#define WATCHDOG_TIMEOUT   (15*1000)  //Watchdog Timer timeout in msec



#define TIMEOUT_NEVER at_the_end_of_time
enum {
  DNSERR_TIMEOUT     = -1,
  DNSERR_NONE        =  0,
  DNSERR_INVALIDHOST =  1
};

//Encapsulate a received UDP packet
//Note: This object owns pbuf buffer and its destructor frees the buffer.
class CRxPacket {
public:  
    struct pbuf* rxpbuf;
    ip_addr_t rxremoteipaddr;
    uint16_t  rxremoteport;

    CRxPacket(struct pbuf* const pbuf, const ip_addr_t remoteipaddr,const uint16_t remoteport) {
        rxpbuf = pbuf;
        rxremoteipaddr = remoteipaddr;
        rxremoteport = remoteport;
    }
    
    ~CRxPacket() {
      if (rxpbuf) pbuf_free(rxpbuf);
    }   
};

class CUDPTask {
  public:
    //Constant
    const uint32_t WIFI_COUNTRY = CYW43_COUNTRY_WORLDWIDE;
    const int WIFI_MAX_ATTEMPT = 3;
  
    //Error Code 0-15 is reserved for CUDPTask
    static const int ERR_NONE      = 0;
    static const int ERR_NOTPICOW   = 1;
    static const int ERR_SSIDNOTSET = 2;
    static const int ERR_NONET      = 3;        //No matching SSID
    static const int ERR_BADAUTH    = 4;        //Authentication Problem
    static const int ERR_NOIP       = 5;        //DHCP problem
    static const int ERR_WIFINOTCONNECTED = 6;  //Other Problems
    static const int ERR_CONNECTIONLOST   = 7;  //WIFI is disconnect during the process
    static const int ERR_DNSINVALIDHOST   = 8;  //Hostname is invalid or not exist
    static const int ERR_DNSTIMEOUT       = 9;  //Timeout during DNS Lookup
    static const int ERR_WATCHDOG         = 10; //Watchdog Timer timeout
    static const int ERR_ABORTED          = 11; //Aborted by request
    
    //Error Code 16-31 is for subclass
    static const int ERR_SUBCLASS_BEGIN  = 16;

    //Public Methods
    CUDPTask();
    virtual ~CUDPTask();  /* must be virtual since this is a base class */
    virtual void Run(const char* ssid, const char* wpakey);

    //Getter methods
    bool GetWifiConnected() const {return this->wifiConnected;}
    bool GetServerIpResolved() const {return this->serverIpResolved;}
    ip_addr_t GetServerAddr() const {return this->server_addr;}
    bool GetCompleted() const {return this->completed;}

    
    //Declare callback functions as friend
    friend void dns_callback(const char *hostname, const ip_addr_t *ipaddr, void *arg);
    friend void udp_callback(void *arg, struct udp_pcb *pcb, struct pbuf *pbuf, const ip_addr_t *remote_addr, u16_t remote_port);
    
  protected:
    void InitCyw43();
    bool hasInitedCyw43;
    
    //
    // Wifi Connection
    //
    void ConnectWifi(const char* ssid, const char* wpakey);
    bool wifiConnected;     //To indicate WIFI is connected

    //
    // DNS
    //
    void DNSLookup(const char* hostname,const uint32_t timeout=DEFAULT_DNSTIMEOUT);
    absolute_time_t dnsTimeout;
    ip_addr_t server_addr;  //Sever IP Address
    bool serverIpResolved;  //To indicate Server IP Addr is resolved
    //To recevie result from DNS Callback
    bool dnsCallbackInvoked;
    int dns_error;
    ip_addr_t dns_result_ipaddr;


    //
    //UDP Send/Receive
    //
    void SendUDP(const uint8_t *payload,const uint16_t len, const uint16_t port);
    struct udp_pcb *pcb;
    //To receive result from UDP Callback
    std::queue<CRxPacket> packetQueue; //Queue to store received UDP packets
    uint8_t *rxbuffer;
    uint16_t rxdatalen;
    ip_addr_t rxremoteipaddr;
    uint16_t  rxremoteport;


    //
    //Timer
    //
    void SetTimer(const uint32_t timeout, const uint32_t arg=0);
    void CancelTimer() {timerTimeout = TIMEOUT_NEVER;}
    absolute_time_t timerTimeout;
    uint32_t timerArg;

    //
    //Completion
    //
    virtual void Complete();
    bool completed;

    //Event Handlers
    virtual void EvtStart();
    virtual void EvtDNSResult(const int dnserr, const ip_addr_t *ipaddr);
    virtual void EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port);
    virtual void EvtTimeout(uint32_t arg);
    virtual void EvtConnectionLost();
    virtual bool EvtAbortRequested();
    virtual void EvtAborted();
    virtual void EvtWatchdogTimeout();
    
private:
      //
      // Watchdog
      //
      absolute_time_t watchdogTimeout;
      void WatchdogUpdate() {
        watchdogTimeout = make_timeout_time_ms(WATCHDOG_TIMEOUT);
      }
      
  //
  // static members
  //
public:  
    static const char* GetErrorCodeMessage(const int error);  //Translate error code to error message for debug
    static CUDPTask* GetRunningObject() {return runningObject;}
    static bool IsRunning() {return CUDPTask::isRunning;}     //Is a CUDPTask running
    static void RequestAbortIfRunning() {
      if (!IsRunning()) return;
      else {
        INFO_PRINTF("Abort Requested\n");
        abortRequested = true;
      }
    }
    
    //Abort running UDPTask with timeout
    //return true if no task is running or the task has been aborted
    //return false if timeout
    static bool AbortTimeout_ms(const uint32_t timeout_ms) {
      if (!IsRunning()) return true;
      else {
        INFO_PRINTF("Abort Requested\n");
        abortRequested = true;
      }      
      absolute_time_t until = make_timeout_time_ms(timeout_ms);
      do {
        if (!IsRunning()) return true;
        sleep_ms(1);
      }while(!time_reached(until));
      return false; //timeout
    }
    
protected:
    static CUDPTask *runningObject;
    static volatile bool isRunning;       //To indicate a CUDPTask is running  
    static volatile bool abortRequested;
    
    //This method is called when abortRequested is set to true.
    //It calls EvtAbortedRequested() method of the running task
    //If it returns true, it aborts the task by throwing NETERR_ABORTED.
    static void Abort(CUDPTask* p) {                 //Try to abort
      bool proceed = p->EvtAbortRequested();         //Ask if to proceed the abort request
      CUDPTask::abortRequested = false;
      if (proceed) {
        p->EvtAborted();
        throw CUDPTask::ERR_ABORTED;
      }
    }

};


#endif