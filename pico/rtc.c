#include <time.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/aon_timer.h"
#include "hardware/divider.h"
#include "debug.h"
#include "userconfig.h"

///////////////////////////////////////////////////////////////////
//ProDOS 8 Date Field
//
//The year field is 7 -bit wide. The range is 0-127
//According to ProDOS 8 Technical Note #28,
//Apple has defined the interpretation of the year field as follows.
//Year values from 40 to 99 represent 1940 through 1999
//Year values from 0 to 39 represent 2000 through 2039
//Year values from 100 to 127 are invalid.
//
//But it's year 2025 now. We definitely want MegaFlash
//can work after year 2039.
//
//So, we interpret year field as follows.
//Year values from 0 to 99 represent 2000 through 2099
//Year values from 100 to 127 represent 2000 through 2027


/////////////////////////////////////////////////////////////////
// Don't call aon_timer_is_running(). Use IsRTCRunning() instead.
// It has caused TFTP Task stop running before.
//

static int32_t prevOffset = 0;
static bool rtcRunning = false;

/////////////////////////////////////////////////////////////////
// Start RTC and initalize its time.
// input: current time in secondsSince1970
//        timezone offset in seconds
//
void InitRTC(const time_t secondsSince1970,const int32_t offset) {
  
  prevOffset = offset;                  //Save timezone offset to prevOffset
  struct timespec ts;
  ts.tv_sec = secondsSince1970+offset;  //adjust for the timezone
  ts.tv_nsec = 0;
  
  aon_timer_start(&ts);
  rtcRunning = true;
}

/////////////////////////////////////////////////////////////////
// Is the real time clock running?
//
// Output: bool - rtc is running
//
bool IsRTCRunning() {
  return rtcRunning;
}


//////////////////////////////////////////////////////
// Adjust the time based on new timezone offset
//
// Input: new timezone offset in seconds
//
void SetNewTimezoneOffset(const int32_t newOffset) {
  //No need to adjust if newOffset == prevOffset
  if (newOffset == prevOffset) return;
  
  //Do nothing if RTC is not running
  if (!IsRTCRunning()) return;
  
  struct timespec ts;
  aon_timer_get_time(&ts);
  
  //Set new time
  InitRTC(ts.tv_sec - prevOffset, newOffset);
}


//////////////////////////////////////////////////////
// Report current date and time in ProDOS format
// The date/time is formatted according to ProDOS standard
//
// Input: Pointer to a 4-bytes buffer to store the timestamp
//        timestamp is 0 if RTC is not initalized
//
void GetProdosTimestamp(uint8_t *timestamp) {
  if (IsRTCRunning()) {
    struct tm t;      
    aon_timer_get_time_calendar(&t);
    
    const uint32_t year = hw_divider_u32_remainder(t.tm_year,100); //ProDOS Year is 0-99 only.
    
    uint32_t date = year<<9 | (t.tm_mon+1)<<5 | t.tm_mday;
    timestamp[0] = date; date>>=8;  //$BF90
    timestamp[1] = date;            //$BF91
    timestamp[2] = t.tm_min;        //$BF92
    timestamp[3] = t.tm_hour;       //$BF93
  } else {
    //to indicate <No Date> in ProDOS
    timestamp[0] = 0;
    timestamp[1] = 0;
    timestamp[2] = 0;
    timestamp[3] = 0;    
  }
}

//////////////////////////////////////////////////////
// Debug Routine
// print struct tm as text
//
// Input: Pointer to struct tm
//
static void PrintDateTime(struct tm *t) {
  const size_t BUFFERLEN = 40;
  char buffer[BUFFERLEN];
  strftime(buffer,BUFFERLEN,"%F %T",t); 
  printf("%s",buffer);
}

//////////////////////////////////////////////////////
// Set RTC from timestamp in ProDOS format
// The date/time is formatted according to ProDOS standard
//
// Input: Pointer to a 4-bytes timestamp
//
void SetRTCFromProdosTimestamp(uint8_t *timestamp) {
  struct tm t;
  
  uint32_t date = timestamp[0] | (timestamp[1]<<8);  
  t.tm_sec  = 0;                      //Second
  t.tm_mday = date & 0b11111;         //Day   1-31
  t.tm_mon  = ((date>>5) & 0b1111)-1; //Month 0-11
  t.tm_hour = timestamp[3] & 0b11111; //Hour  0-23
  t.tm_min  = timestamp[2] & 0b111111;//Min   0-59
  
  uint32_t year = (date>>9) & 0b1111111;
  if (year>=100) year-=100;   //Make sure it is 0-99
  t.tm_year = 100+year;       //Year since 1900
  
  aon_timer_start_calendar(&t);
  #ifndef NDEBUG
  printf("Setting RTC to ");
  PrintDateTime(&t);
  printf("\n");
  #endif
}

//////////////////////////////////////////////////////
// Set RTC from timestamp in ProDOS format
// The date/time is formatted according to ProDOS 2.5 standard
//
// Input: Pointer to a 6-bytes timestamp
//
void SetRTCFromProdos25Timestamp(uint8_t *timestamp) {
  struct tm t;

  uint32_t time = timestamp[2] | (timestamp[3]<<8);  
  t.tm_sec  = timestamp[1];           //Second
  t.tm_mday = (time>>11) & 0b11111;   //Day   1-31
  t.tm_hour = (time>>6)  & 0b11111;   //Hour  0-23
  t.tm_min  = time       & 0b111111;  //Min   0-59
  //timestamp[0] is number of 4ms. It is ignored.

  uint32_t date = timestamp[4] | (timestamp[5]<<8);
  t.tm_mon  = ((date>>12) & 0b1111)-2;    //Month: ProDOS2.5 is 2-13. tm_mon is 0-11  
  uint32_t year = date & 0b111111111111;  //year from 0 CE
  t.tm_year = year - 1900;
  
  aon_timer_start_calendar(&t);
  #ifndef NDEBUG
  printf("Setting RTC to ");
  PrintDateTime(&t);
  printf("\n");
  #endif
}

//////////////////////////////////////////////////////
// Report current date and time in ProDOS 2.5 format
// The date/time is formatted according to ProDOS 2.5 standard
//
// Input: Pointer to a 6-bytes buffer to store the timestamp
//        timestamp is 0 if RTC is not initalized
//
uint32_t GetProdos25Timestamp(uint8_t *timestamp) {
  if (IsRTCRunning()) {
    struct tm t;     
    struct timespec ts;
    
    aon_timer_get_time(&ts);
    pico_localtime_r(&ts.tv_sec, &t);
    
    uint32_t t4ms   = hw_divider_u32_quotient(ts.tv_nsec,4000000); //Convert ns to number of 4ms
    timestamp[0] = t4ms;              //$BF8E
    timestamp[1] = t.tm_sec;          //$BF8F  
    
    uint32_t time = t.tm_mday<<11 | t.tm_hour<<6 | t.tm_min;
    timestamp[2] = time; time>>=8;    //$BF90
    timestamp[3] = time;              //$BF91
    
    //Month is 2..13. Year is 0..4095
    uint32_t date = (t.tm_mon+2)<<12 | ((t.tm_year+1900) & 0xfff);   
    timestamp[4] = date; date>>=8;    //$BF92
    timestamp[5] = date;              //$BF93
  } else {
    //to indicate <No Date> in ProDOS
    timestamp[0] = 0;
    timestamp[1] = 0;
    timestamp[2] = 0;
    timestamp[3] = 0;   
    timestamp[4] = 0;
    timestamp[5] = 0;  
  }
}


