#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "lwip/dns.h"
#include "udptask.h"
#include "debug.h"
#include "misc.h"



//Function Prototype
void udp_callback(void *arg, struct udp_pcb *pcb, struct pbuf *pub, const ip_addr_t *remote_addr, u16_t remot_port);

//--------------------------------------------------------------------
// This function is modified from cyw43_arch_wifi_connect_timeout_ms()
// The original function fails to report CYW43_LINK_NONET
//
static int wifi_connect_timeout_ms(const char *ssid, const char *pw, uint32_t auth, uint32_t timeout_ms) {
  absolute_time_t timeout = make_timeout_time_ms(timeout_ms);

  int err = cyw43_arch_wifi_connect_bssid_async(ssid, NULL, pw, auth);
  if (err) return err;

  int status = CYW43_LINK_UP + 1;
  while(status >= 0 && status != CYW43_LINK_UP) {
      int new_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
      if (new_status != status) {
          status = new_status;
          DEBUG_PRINTF("connect status: %d\n", status);
      }
      
      if (status <0) {
        break; //error occured. stop waiting
      }        
      
      if (time_reached(timeout)) {
          return PICO_ERROR_TIMEOUT;  //-2
      }
      // Do polling
      cyw43_arch_poll();
      cyw43_arch_wait_for_work_until(timeout);
  }
  
  if (status == CYW43_LINK_UP) {
      return PICO_OK; // success
  } else if (status == CYW43_LINK_BADAUTH) {
      DEBUG_PRINTF("BADAUTH !!!!!!!!!!!\n");
      return PICO_ERROR_BADAUTH;    //-7
  } else {
      return PICO_ERROR_CONNECT_FAILED;
  }
}

///////////////////////////////////////////////////////////////////
//
// Init static members
volatile bool CUDPTask::isRunning = false;
volatile bool CUDPTask::abortRequested = false;
CUDPTask *CUDPTask::runningObject = NULL;

///////////////////////////////////////////////////////////////////
// Constructor
//
CUDPTask::CUDPTask() {
  server_addr = IPADDR4_INIT(0); 
  wifiConnected = false;
  serverIpResolved = false;
  dnsTimeout   = TIMEOUT_NEVER;
  timerTimeout = TIMEOUT_NEVER;
  timerArg = 0;
  pcb = NULL;
  completed = false;
  udpCallbackInvoked=false;
  rxbuffer = new uint8_t[UDP_BUFFERSIZE];
  rxdatalen = 0;
  rxremoteipaddr=IPADDR4_INIT(0);
  rxremoteport=0;
  watchdogTimeout = TIMEOUT_NEVER;
  hasInitedCyw43 = false;

}

///////////////////////////////////////////////////////////////////
// Destructor
//
CUDPTask::~CUDPTask() {
  cyw43_arch_lwip_begin();  
  if (this->pcb) udp_remove(this->pcb);
  if (rxbuffer) delete[] rxbuffer;
  cyw43_arch_lwip_end();   
  
  if (hasInitedCyw43) {
    //Disconnect WIFI
    TRACE_PRINTF("Disconnecting WIFI\n");
    cyw43_arch_disable_sta_mode();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN,0); //Turn off LED
    
    //CYW43 Library will go crazy if we keep on connecting and disconnecting WIFI
    //deinit then re-init the library seems to fix the problem.
    cyw43_arch_deinit();    
  }
}

///////////////////////////////////////////////////////////////////
// Init CYW43 
//
void CUDPTask::InitCyw43() {
  if (!CheckPicoW()) throw CUDPTask::ERR_NOTPICOW;
  
  TRACE_PRINTF("InitCyw43()\n");
  hasInitedCyw43 = true;
  cyw43_arch_init_with_country(WIFI_COUNTRY);
  cyw43_arch_enable_sta_mode();
  
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN,1); //Turn on LED
}


///////////////////////////////////////////////////////////////////
// Call this function to execute the task
//
void CUDPTask::Run(const char* ssid, const char* wpakey) {
  CUDPTask::isRunning = true;
  CUDPTask::abortRequested = false;
  CUDPTask::runningObject = this;
  TRACE_PRINTF("CUDPTask::Run()\n");

  try {
    InitCyw43();
    ConnectWifi(ssid,wpakey);
    wifiConnected = true;
    
    //Create udp_pcb
    pcb = udp_new();
    DEBUG_PRINTF("udp_new() pcb = 0x%x\n",(uint32_t)pcb);
    udp_bind(pcb, IP4_ADDR_ANY, 0 /*random port*/); //Bind local IF
    udp_recv(pcb,udp_callback,this);  //Set callback function
    
    //Start Event
    WatchdogUpdate();
    this->EvtStart();

    //Event Loop
    do {
      //Run the loop every HEARTBEAT_PERIOD to check the abortRequested flag
      absolute_time_t nextRun = make_timeout_time_ms(HEARTBEAT_PERIOD);
      
      cyw43_arch_poll();
      
      //DNS Callback
      if (dnsCallbackInvoked) {
        dnsCallbackInvoked = false;
        dnsTimeout = TIMEOUT_NEVER;
        WatchdogUpdate();
        this->EvtDNSResult(dns_error, &dns_result_ipaddr);
      }
        
      //DNS Query Timeout
      if (time_reached(dnsTimeout)) {
        dnsTimeout = TIMEOUT_NEVER;
        WatchdogUpdate();      
        this->EvtDNSResult(DNSERR_TIMEOUT, NULL);
      }
      
      //UDP Callback
      if (udpCallbackInvoked) {
        udpCallbackInvoked = false;
        WatchdogUpdate();      
        EvtUDPReceived(rxbuffer,rxdatalen,rxremoteipaddr,rxremoteport);
      }
      
      //Timer Timeout
      if (time_reached(timerTimeout)) {
        timerTimeout = TIMEOUT_NEVER;
        WatchdogUpdate();      
        this->EvtTimeout(timerArg);
      }
      
      //WIFI Connection Lost, suppress the event if completed has been set
      if (!completed && CYW43_LINK_UP != cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
        WatchdogUpdate();      
        this->EvtConnectionLost();
      }
      
      //Watchdog Timer
      if (!completed && time_reached(watchdogTimeout)) {
        watchdogTimeout = TIMEOUT_NEVER;
        this->EvtWatchdogTimeout();
        throw CUDPTask::ERR_WATCHDOG;
      }
      
      //Abort Request
      if (CUDPTask::abortRequested) CUDPTask::Abort(this);
      
      if (!completed) {
        cyw43_arch_wait_for_work_until(MIN3(nextRun,dnsTimeout,timerTimeout));
      }
    }while(!completed);

    DEBUG_PRINTF("Event Loop completed normally\n");
    CUDPTask::isRunning = false;
    CUDPTask::runningObject = NULL;        
    
  }catch(...) {
    //Make sure isRunning and runningObject is set to false and NULL if any exception occurs
    CUDPTask::isRunning = false;
    CUDPTask::runningObject = NULL;    
    throw; 
  }
}




///////////////////////////////////////////////////////////////////
//
// Connect WIFI
//
// Possible Outcome: 
//   WIFI Connected (Function returned)
//   Error (Exception thrown)
void CUDPTask::ConnectWifi(const char* ssid, const char* wpakey) {
  int errorcode, link_status;
  uint32_t delay;
  int attempt=0;  
    
  //Already connected?
  if (cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA )==CYW43_LINK_UP) return;

  //If SSID is empty, don't proceed
  if (ssid[0]=='\0') {
    DEBUG_PRINTF("SSID not set\n");
    throw CUDPTask::ERR_SSIDNOTSET;
  }      
  DEBUG_PRINTF("SSID=%s\n",ssid);  
  
  //If wpakey is empty, use CYW43_AUTH_OPEN
  uint32_t authType;
  if (wpakey[0]=='\0') {
    authType = CYW43_AUTH_OPEN;
    DEBUG_PRINTF("WPA Key is empty. Using CYW43_AUTH_OPEN\n");
  } else{
    authType = CYW43_AUTH_WPA3_WPA2_AES_PSK;
  }  
  
  //Try to connect
  do {
    DEBUG_PRINTF("\nConnecting to Wifi Attempt:#%d\n",attempt+1);
    errorcode = wifi_connect_timeout_ms(ssid, wpakey, authType, 15000);
    
    link_status = cyw43_tcpip_link_status (&cyw43_state, CYW43_ITF_STA);
    DEBUG_PRINTF("link_status=%d\n",link_status);
    
    if (errorcode ==0 && link_status==CYW43_LINK_UP) {
      DEBUG_PRINTF("WIFI connected\n");
      DEBUG_PRINTF("IP address: %s\n", ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));
      DEBUG_PRINTF("Gateway: %s\n", ip4addr_ntoa(netif_ip4_gw(&cyw43_state.netif[CYW43_ITF_STA])));      
      DEBUG_PRINTF("Netmask: %s\n", ip4addr_ntoa(netif_ip4_netmask(&cyw43_state.netif[CYW43_ITF_STA])));      
      DEBUG_PRINTF("DNS Server: %s\n", ip4addr_ntoa(dns_getserver(0)));
           
      return;  //success
    } else {
      DEBUG_PRINTF("wifi_connect errorcode = %d\n",errorcode);
    }
    
    //Delay between retries varies. 
    if (attempt ==0) delay = 1000;
    else delay = 15000;
    ++attempt;
    
    if (attempt<WIFI_MAX_ATTEMPT) {
      DEBUG_PRINTF("Delaying %d\n",delay);
      absolute_time_t until = make_timeout_time_ms(delay);
      
      do {
        sleep_ms(1);
        if (CUDPTask::abortRequested) CUDPTask::Abort(this);
      }while(!time_reached(until));
    }

  }while(attempt<WIFI_MAX_ATTEMPT);


  //Test Result:
  //On TP-Link TL-WR741 Router
  //If AuthType is incorrect, CYW43_LINK_FAIL is returned. 
  //If WPA Key is incorrect, CYW43_LINK_BADAUTH is returned.
  //If DHCP is disabled, CYW43_LINK_NOIP is returned.
  //
  //On Asus RT-AC59U V2
  //If AuthType is incorrect, CYW43_LINK_FAIL is returned. 
  //If WPA Key is incorrect, it does not constantly return CYW43_LINK_BADAUTH.
  //In most cases, it returns CYW43_LINK_JOIN, occasionally CYW43_LINK_BADAUTH.
  //If DHCP is disabled, CYW43_LINK_NOIP is returned.  
  //
  //So, we map CYW43_LINK_BADAUTH, CYW43_LINK_JOIN and CYW43_LINK_FAIL to ERRBADAUTH

  if (link_status == CYW43_LINK_NONET) {
    throw CUDPTask::ERR_NONET;
  } else if (link_status == CYW43_LINK_BADAUTH || link_status == CYW43_LINK_JOIN || link_status == CYW43_LINK_FAIL) {
    throw CUDPTask::ERR_BADAUTH;
  } else if (link_status == CYW43_LINK_NOIP) {
    throw CUDPTask::ERR_NOIP;
  } 

  //WIFI Not connected due to other reasons
  throw CUDPTask::ERR_WIFINOTCONNECTED;
}

/* For reference
#define CYW43_LINK_DOWN         (0)     ///< link is down
#define CYW43_LINK_JOIN         (1)     ///< Connected to wifi
#define CYW43_LINK_NOIP         (2)     ///< Connected to wifi, but no IP address
#define CYW43_LINK_UP           (3)     ///< Connect to wifi with an IP address
#define CYW43_LINK_FAIL         (-1)    ///< Connection failed
#define CYW43_LINK_NONET        (-2)    ///< No matching SSID found (could be out of range, or down)
#define CYW43_LINK_BADAUTH      (-3)    ///< Authenticatation failure
*/

///////////////////////////////////////////////////////////////////
//
// Return an error message for Debug purpose
//
const char* CUDPTask::GetErrorCodeMessage(const int error){
  if (error == CUDPTask::ERR_NONE) return "No Error";
  else if (error == CUDPTask::ERR_NOTPICOW) return "Not running on Pico W";
  else if (error == CUDPTask::ERR_SSIDNOTSET) return "SSID is not set";
  else if (error == CUDPTask::ERR_NONET) return "No matching SSID found";
  else if (error == CUDPTask::ERR_BADAUTH) return "Authentication failed";
  else if (error == CUDPTask::ERR_NOIP)    return "DHCP problem";
  else if (error == CUDPTask::ERR_WIFINOTCONNECTED) return "WIFI not connected";
  else if (error == CUDPTask::ERR_CONNECTIONLOST) return "WIFI Connection lost";
  else if (error == CUDPTask::ERR_DNSINVALIDHOST) return "DNS: Invalid Hostname";
  else if (error == CUDPTask::ERR_DNSTIMEOUT) return "DNS: Timeout";
  else if (error == CUDPTask::ERR_WATCHDOG) return "Watchdog: Timeout";
  else if (error == CUDPTask::ERR_ABORTED) return "Aborted";
  else return "Unknown";
}

///////////////////////////////////////////////////////////////////////////////////////////
// ######  #     #  #####     #       ####### ####### #    # #     # ###### 
// #     # ##    # #     #    #       #     # #     # #   #  #     # #     #
// #     # # #   # #          #       #     # #     # #  #   #     # #     #
// #     # #  #  #  #####     #       #     # #     # ###    #     # ###### 
// #     # #   # #       #    #       #     # #     # #  #   #     # #      
// #     # #    ## #     #    #       #     # #     # #   #  #     # #      
// ######  #     #  #####     ####### ####### ####### #    #  #####  #      
///////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
//DNS Query callback function
//ipaddr is the reply from DNS server
//Copy it to addr_out member of arg structure
void dns_callback(const char *hostname, const ip_addr_t *ipaddr, void *arg){
  TRACE_PRINTF("dns_callback() invoked\n");
  CUDPTask* pTask = (CUDPTask*) arg;
  
  //Read note below.
  if (CUDPTask::GetRunningObject()!=pTask || pTask==NULL) {
    WARN_PRINTF("dns_callback() arg NOT POINTING to current UDPTask object. Ignore it!\n");
    return;
  }

  pTask->dnsCallbackInvoked = true;
  if (ipaddr!=NULL) {
    pTask->dns_error = DNSERR_NONE;
    pTask->dns_result_ipaddr = *ipaddr;
  } else {
    pTask->dns_error = DNSERR_INVALIDHOST;
  }
}
//If we don't check pTask is point to Running Task, it causes problem with the following sequence.
//
//1) To test DNS Failed scenario, the internet link of router is disconnected. 
//2) Enable Network Time Sync
//3) Power up Apple
//4) Pico tries to do Network Time Sync
//5) While the Time sync is in progress, start Test Wifi
//6) pcb member variable is corrupted and it crashes when udp_remove() is called in the destructor.
//
//The cause is that dns_callback() is invoked when Test Wifi is running. But arg is not
//pointing to CTestWifiTask object. 
//Since CNTPTask is aborted, the dns_callback() belongs to the aborted CNTPTask, not
//CTestWifiTask. 
//So, we need to check if arg is actually pointing to running task. Otherwise, it causes
//memory corruption when we write ipaddr and DNSERR_NONE with pTask pointer.



////////////////////////////////////////////////////////////
// To resolve IP Address from hostname
//
// EvtDNSResult() handler will be called to receive the result
//
// Input: hostname 
//        timeout in msec
//
void CUDPTask::DNSLookup(const char* hostname,const uint32_t timeout) {
  TRACE_PRINTF("CUDPTask::DNSLooup(): hostname = %s\n",hostname);
  this->dnsCallbackInvoked = false;
  this->dns_result_ipaddr = IPADDR4_INIT(0); 

  ip_addr_t ipaddr;
  cyw43_arch_lwip_begin();
  int err = dns_gethostbyname(hostname, &ipaddr, dns_callback, this);
  cyw43_arch_lwip_end();
  if (err == ERR_OK) {  //dns_gethostbyname() returns IP address immediately
    DEBUG_PRINTF("dns_gethostbyname() returns ERR_OK. resolved IP Address=%s\n",ipaddr_ntoa(&ipaddr));
    this->EvtDNSResult(DNSERR_NONE,&ipaddr);
    return;
  }
  else if (err == ERR_ARG) { 
    //Invalid host
    this->EvtDNSResult(DNSERR_INVALIDHOST,NULL);
    return;
  } 
  else if (err != ERR_INPROGRESS) {
    //Other unexpected error, report as timeout
    this->EvtDNSResult(DNSERR_TIMEOUT,NULL);
    return;
  }
  
  //Waiting for callback
  DEBUG_PRINTF("DNSLooup(): waiting for callback\n");
  dnsTimeout = make_timeout_time_ms(timeout);
}



/////////////////////////////////////////////////////////////////////////////
// Send UDP Packet
//
//
void CUDPTask::SendUDP(const uint8_t *payload,const uint16_t payloadlen, const uint16_t destPort) {
  cyw43_arch_lwip_begin();
    struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, payloadlen, PBUF_RAM);
    pbuf_take(pbuf,payload,payloadlen);  //Copy payload into pbuf
    udp_sendto(this->pcb, pbuf, &this->server_addr, destPort);
    pbuf_free(pbuf);
  cyw43_arch_lwip_end();  
}

/////////////////////////////////////////////////////////////////////////////
// UDP Received callback function
//
// UDP Payload is copied to rxbuffer
// remote IP address and port is copied to rxremoteipaddr and rxremoteport
// udpCallbackInvoked is set to true
//
void udp_callback(void *arg, struct udp_pcb *pcb, struct pbuf *pbuf, const ip_addr_t *remote_addr, u16_t remote_port) {
  TRACE_PRINTF("udp_callback invoked\n");
  CUDPTask* pTask = (CUDPTask*) arg;  
  
  //make sure the callback is for this object
  //see note at dns_callback()
  if (CUDPTask::GetRunningObject()==pTask && pTask!=NULL) {
    if (pbuf->tot_len <= UDP_BUFFERSIZE) {
      //Copy recevied data to CUDPTask object
      pTask->udpCallbackInvoked=true;
      pTask->rxdatalen = pbuf->tot_len;
      pbuf_copy_partial(pbuf,pTask->rxbuffer,pbuf->tot_len,0);
      pTask->rxremoteipaddr=*remote_addr;
      pTask->rxremoteport=remote_port;
    } else {
      assert(0);
    }
  } else {
    WARN_PRINTF("udp_callback() arg NOT POINTING to current UDPTask object. Ignore it!\n");
  }
  
  if (pbuf) pbuf_free(pbuf);
}



////////////////////////////////////////////////////////////////////////////////
// To Start a Timer
//
//
void CUDPTask::SetTimer(const uint32_t timeout, const uint32_t arg) {
  timerTimeout = make_timeout_time_ms(timeout);
  timerArg = arg;
}

////////////////////////////////////////////////////////////////////////////////
// To end event loop
//
//
void CUDPTask::Complete() {
  TRACE_PRINTF("CUDPTask:Complete()\n");
  this->completed = true;
}
//**************************************************************************************


////////////////////////////////////////////////////////////////////////////////
// Start Event Handler
//
// Default Behaviour:
//   Do Nothing (Debug Message Only)
//
void CUDPTask::EvtStart() {
  TRACE_PRINTF("EvtStart() called\n");
}


////////////////////////////////////////////////////////////////////
// DNS Result Event Handler
//
// Default Behaviour:
//
// If DNS Lookup is successful,
// Copy resolved IP Address to server_addr member variable
// Set serverIPResolved to true
//
// If Error occured, throw ERR_DNSINVALIDHOST or ERR_DNSTIMEOUT
// exception.
//
void CUDPTask::EvtDNSResult(const int dns_error, const ip_addr_t *ipaddr){
  switch(dns_error) {
    case DNSERR_NONE:
      assert(ipaddr != NULL);
      DEBUG_PRINTF("EvtDNSResult(): IP Addr = %s\n",ipaddr_ntoa(ipaddr));
      this->serverIpResolved = true;
      this->server_addr = *ipaddr;
      break;
    case DNSERR_INVALIDHOST:
      DEBUG_PRINTF("EvtDNSResult(): Invalid Host\n");
      throw CUDPTask::ERR_DNSINVALIDHOST;
      break;
    default:
      DEBUG_PRINTF("EvtDNSResult(): Timeout or Other error\n");
      throw CUDPTask::ERR_DNSTIMEOUT;
      break;
  }
}

////////////////////////////////////////////////////////////////////
// UDP Received Handler
//
// Default Behaviour:
//    Nothing (Debug Message Only)
//
void CUDPTask::EvtUDPReceived(const uint8_t* payload,uint16_t payloadlen,ip_addr_t remote_addr,uint16_t remote_port) {
  TRACE_PRINTF("EvtUDPReceived() Remote IP=%s port=%d len=%d\n",ipaddr_ntoa(&remote_addr),remote_port,payloadlen);
}

////////////////////////////////////////////////////////////////////
// Timer Timeout Handler
//
// Default Behaviour:
//    Nothing (Debug Message Only)
//
void CUDPTask::EvtTimeout(uint32_t arg) {
  TRACE_PRINTF("EvtTimeout() called\n");
}

////////////////////////////////////////////////////////////////////
// WIFI Connection Lost Handler
//
// Default Behaviour:
//    throw ERR_CONNECTIONLOST exception
//
void CUDPTask::EvtConnectionLost(){
  TRACE_PRINTF("EvtConnectionLost() called\n");
  throw CUDPTask::ERR_CONNECTIONLOST;
}


////////////////////////////////////////////////////////////////////
// This event handler is called when an abort request is received.
//
// return true to abort
//        false to ignore the abort request
//
bool CUDPTask::EvtAbortRequested(){
  TRACE_PRINTF("EvtAbortRequested() called\n");
  return true;
}

////////////////////////////////////////////////////////////////////
// This event handler is called when the process is aborted.
//
void CUDPTask::EvtAborted(){
  TRACE_PRINTF("EvtAborted() called\n");
}

////////////////////////////////////////////////////////////////////
// This event handler is called when the watchdog timer timeout
// and the process is being terminated.
//
void CUDPTask::EvtWatchdogTimeout() {
  TRACE_PRINTF("EvtWatchdogTimeout()\n");
}

