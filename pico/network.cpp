
//////////////////////////////////////////////////////////////
//Entry point of network functions
//It is also the bridge between C and C++

#include "udptask.h"
#include "ntptask.h"
#include "testwifitask.h"
#include "tftprxtask.h"
#include "tftptxtask.h"
#include "userconfig.h"
#include "rtc.h"
#include "network.h"
#include "debug.h"
#include "tftp.h"
#include <typeinfo>

#ifdef __cplusplus
extern "C" {
#endif

//
//Export CUDPTask static method as C function
//
void UDPTask_RequestAbortIfRunning() {
  CUDPTask::RequestAbortIfRunning();
}

bool UDPTask_AbortTimeout_ms(const uint32_t timeout_ms) {
  return CUDPTask::AbortTimeout_ms(timeout_ms);
}

bool IsUDPTaskRunning() {
  return CUDPTask::IsRunning();
}

bool IsNTPTaskRunning() {
  CUDPTask *runningTask=CUDPTask::GetRunningObject();  
  if (runningTask==NULL) return false; //To avoid exception when dereference a NULL pointer below
  return typeid(CNTPTask)==typeid(*runningTask);
}

bool IsTestWifiTaskRunning() {
  CUDPTask *runningTask=CUDPTask::GetRunningObject();  
  if (runningTask==NULL) return false; //To avoid exception when dereference a NULL pointer below 
  return typeid(CTestWifiTask)==typeid(*runningTask);
}


bool IsTFTPTaskRunning() {
  CUDPTask *runningTask=CUDPTask::GetRunningObject();  
  return dynamic_cast<CTFTPTask*>(runningTask)!=nullptr;
}

//tftp_state defined in tftpstate.c
extern "C" volatile tftp_state_t tftp_state;



////////////////////////////////////////////////////////////
// Convert CUDPTask exception to TFTP ERROR
//
static int ConvertExceptionToTFTPError(int e) {
  switch(e) {
    case CUDPTask::ERR_NONE:
      return TFTPERROR_NOERR;
    case CUDPTask::ERR_NOTPICOW:
    case CUDPTask::ERR_SSIDNOTSET:
    case CUDPTask::ERR_NONET:
    case CUDPTask::ERR_BADAUTH:
    case CUDPTask::ERR_NOIP:
    case CUDPTask::ERR_WIFINOTCONNECTED:
      return TFTPERROR_WIFINOTCONNECTED;
    case CUDPTask::ERR_CONNECTIONLOST:
      return TFTPERROR_WIFICONNECTIONLOST;
    case CUDPTask::ERR_DNSINVALIDHOST:
    case CUDPTask::ERR_DNSTIMEOUT:
      return TFTPERROR_DNS;
    case CUDPTask::ERR_WATCHDOG:
      return TFTPERROR_WATCHDOG;
    case CUDPTask::ERR_ABORTED:
      return TFTPERROR_ABORTED;
    case CTFTPTask::ERR_RWFAILED:
      return TFTPERROR_RWFAILED;
    default:
      return TFTPERROR_UNKNOWN;
  }
}


void ExecuteTFTP(const uint32_t taskid) {
  uint32_t dir = tftp_state.dir;
  uint unitNum = tftp_state.unitNum;
  const char* hostname = (const char*)tftp_state.server_hostname;
  const char* filename = (const char*)tftp_state.filename;

  TRACE_PRINTF("ExecuteTFTP: taskid=%d\n",taskid);  
  TRACE_PRINTF("dir = %d\n",dir);
  TRACE_PRINTF("unitNum = %d\n",unitNum);
  TRACE_PRINTF("Hostname = %s\n",hostname);
  TRACE_PRINTF("filename = %s\n",filename);
  
  tftp_state.status = TFTPSTATUS_STARTING;  //make sure it is not COMPLETED. Otherwise, Apple may terminate the process immediately.
  tftp_state.startTime = get_absolute_time();
  tftp_state.taskid = taskid;
  TRACE_PRINTF("tftp_state.status = TFTPSTATUS_STARTING");
  
  //
  // Start the task
  //
  int errorcode = TFTPERROR_NOERR;
  const char* ssid = GetSSID();   
  const char* wpakey = GetWPAKey(); 
  CTFTPTask *task = NULL;

  try {
    if (dir==0)      task = new CTFTPRXTask(unitNum,hostname,filename,GetTFTPEnable1kBlockSize(), GetTFTPTimeout(),GetTFTPMaxAttempt(),GetTFTPServerPort());
    else if (dir==1) task = new CTFTPTXTask(unitNum,hostname,filename,GetTFTPEnable1kBlockSize(), GetTFTPTimeout(),GetTFTPMaxAttempt(),GetTFTPServerPort());
    else {
      assert(0); //should not happen
    }  
      
    task->Run(ssid,wpakey);
    
  } catch(int e) {
    ERROR_PRINTF("CUDPTask Execption caught:%d (%s)\n", e,CUDPTask::GetErrorCodeMessage(e));
    
    //Map CUDPTask exception to tftp_error_t error code
    errorcode = ConvertExceptionToTFTPError(e);
  } catch(...) {
    //make sure no exception goto C code    
    errorcode = TFTPERROR_UNKNOWN;
  }    
  
  //Setup error code
  //Make sure status is set to COMPLETED
  tftp_critical_section_enter_blocking();
  if (errorcode!=TFTPERROR_NOERR) {
    tftp_state.error = errorcode;
    ERROR_PRINTF("tftp_state.error = %d\n",errorcode);
  }
  tftp_state.status = TFTPSTATUS_COMPLETED;
  tftp_critical_section_exit();
  
  //Free CTFTPTask object
  if (task) delete task;
}


////////////////////////////////////////////////////////////
//
//
NetworkError_t GetNetworkTime() {
  DEBUG_PRINTF("GetNetworkTime()\n");
  if (!GetNTPClientEnabled()) {
    return NETERR_NONE;
  }
  
  try {
    CNTPTask task;
    task.Run(GetSSID(),GetWPAKey());
  
    if (task.GetCompleted()) {
      //Successful! Pass to RTC
      InitRTC(task.GetSecondsSince1970(),GetTimezoneOffset());
    } else {
      return NETERR_NTPFAILED;
    }
  } catch(int e) {
    ERROR_PRINTF("CUDPTask Execption caught:%d (%s)\n", e,CUDPTask::GetErrorCodeMessage(e));
    return (e==CUDPTask::ERR_NOTPICOW)?NETERR_NOTPICOW:NETERR_NTPFAILED;
  } catch(...) {
    //make sure no exception goto C code
    DEBUG_PRINTF("Execption caught: unknown\n");
    return NETERR_NTPFAILED;
  }
  
  return NETERR_NONE;
}



void TestWifi(TestResult_t *testResultPtr) {
  DEBUG_PRINTF("TestWifi()\n");
  try {
    CTestWifiTask task(testResultPtr);
    task.Run(GetSSID(),GetWPAKey());
    
    if (task.GetCompleted()) {
      testResultPtr->error = NETERR_NONE;
      testResultPtr->testCompleted = true;
      return;
    }
  } catch(int e) {
      NetworkError_t netError =  NETERR_NONE;
      //Map Exception to NetworkError_t    
      switch(e) {
        case CUDPTask::ERR_NOTPICOW: netError = NETERR_NOTPICOW; break;
        case CUDPTask::ERR_SSIDNOTSET: netError = NETERR_SSIDNOTSET; break;
        case CUDPTask::ERR_NONET: netError = NETERR_NONET; break;
        case CUDPTask::ERR_BADAUTH: netError= NETERR_BADAUTH; break;
        case CUDPTask::ERR_NOIP: netError = NETERR_NOIP; break;
        case CUDPTask::ERR_WIFINOTCONNECTED : netError = NETERR_WIFINOTCONNECTED; break;
        case CUDPTask::ERR_CONNECTIONLOST: netError = NETERR_WIFINOTCONNECTED; break;
        case CUDPTask::ERR_DNSINVALIDHOST: netError = NETERR_DNSFAILED; break;
        case CUDPTask::ERR_DNSTIMEOUT: netError = NETERR_DNSFAILED; break;
        case CUDPTask::ERR_WATCHDOG: netError = NETERR_TIMEOUT; break;
        case CUDPTask::ERR_ABORTED: netError = NETERR_ABORTED; break;
        case CNTPTask::ERR_NTPFAILED: netError = NETERR_NTPFAILED; break;
        default: netError = NETERR_UNKNOWN;
      }
    
    testResultPtr->error = netError;
    testResultPtr->testCompleted = true;    
    return;
  } catch(...) {
    //make sure no exception goto C code
    testResultPtr->error = NETERR_UNKNOWN;
    testResultPtr->testCompleted = true;    
    return;
  }
  
  //Unknown Error
  testResultPtr->error = NETERR_UNKNOWN;
  testResultPtr->testCompleted = true;
}



#ifdef __cplusplus
}
#endif
