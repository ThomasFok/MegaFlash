#include <string.h>
#include "pico/stdlib.h"
#include <stdio.h>
#include "defines.h"
#include "debug.h"
#include "math.h"

////////////////////////////////////////////////////////////////////
// Applesoft FPU
//
// The firmware intercepts the floating point routines of Applesoft
// BASIC. It sends the operands called FAC and ARG to parameter buffer.
// The operation is performed by Pico. Then, result is stored in parameter
// buffer. The firmware copies the result back to FAC.
//
// FAC and ARG are stored in the following address.
//
// $9D FACEXP           $A5   ARGEXP
// $9E FACMANTISSA1     $A6   ARGMANTISSA1
// $9F FACMANTISSA2     $A7   ARGMANTISSA2
// $A0 FACMANTISSA2     $A8   ARGMANTISSA3
// $A1 FACMANTISSA4     $A9   ARGMANTISSA4
// $A2 FACSIGN          $AA   ARGSIGN
// $AC FAC EXTENSION
//
// To optimize the speed of 6502 firmware code, 
// the following code is used to send FAC and ARG
// to parameter buffer.
//
//    ldx #5          ;6 bytes for fac and arg
// :  lda fac,x       ;lda $9d,x
//    sta paramreg
//    lda arg,x       ;lda $a5,x
//    sta paramreg
//    dex
//    bpl :-
//    lda facext      ;lda $ac
//    sta paramreg
//
// The order of the data in parameter buffer would be
//
// $A2,$AA,$A1,$A9,$A0,$A8,$9F,$A7,$9E,$A6,$9D,$A5,$AC
//
// Constants like FACEXP, ARGSIGN are defined to access
// those data.
//
// For the result, the first byte is error code. The possible
// values are OVERFLOW, DIV0ERROR, IQERROR or 0 (No Error).
// 
// They correspond to Overflow Error, Division by Zero Error
// and Illegal Quantity Error in Applesoft BASIC.
//
// Then, the following 7 bytes are result. The firmware
// should copy them to FAC.
//

///////////////////////////////////////////////////////////////////
// Benchmark test shows that putting the routines in RAM has
// no performance gain at all.
//

//To access double as 2 uint32_t or 1 uint64_t
typedef union {
  double   d;
  uint32_t i32[2];
  uint64_t i64;
} double_ui64;

//Global Variables
double_ui64 fac, arg, result;

//FAC and ARG Offsets in Parameter Buffer
#define FACEXP       (10)
#define FACSIGN      (0)
#define FACMANTISSA1 (8)
#define FACMANTISSA2 (6)
#define FACMANTISSA3 (4)
#define FACMANTISSA4 (2)
#define FACEXT       (12)

#define ARGEXP       (11)
#define ARGSIGN      (1)
#define ARGMANTISSA1 (9)
#define ARGMANTISSA2 (7)
#define ARGMANTISSA3 (5)
#define ARGMANTISSA4 (3)


#define RESERROR     (0)
#define RESSIGN      (1)
#define RESMANTISSA4 (2)
#define RESMANTISSA3 (3)
#define RESMANTISSA2 (4)
#define RESMANTISSA1 (5)
#define RESEXP       (6)
#define RESEXT       (7)

//Error Flags
#define OVERFLOWERROR (0b10000000)
#define DIV0ERROR     (0b01000000)
#define IQERROR       (0b00100000)

//Macro to show debug message
//Print fac, arg and result
#define DEBUG_PRINT_ALL(prompt) \
          DEBUG_PRINTF("%s\n",prompt);     \
          DEBUG_PRINTF(" fac=%f\n",fac.d); \
          DEBUG_PRINTF(" arg=%f\n",arg.d); \
          DEBUG_PRINTF(" result=%f\n",result.d);

//Print fac only
#define DEBUG_PRINT_FAC(prompt) \
          DEBUG_PRINTF("%s\n",prompt);     \
          DEBUG_PRINTF(" fac=%f\n",fac.d); \

//Print fac and result
#define DEBUG_PRINT_FAC_RES(prompt) \
          DEBUG_PRINTF("%s\n",prompt);     \
          DEBUG_PRINTF(" fac=%f\n",fac.d); \
          DEBUG_PRINTF(" result=%f\n",result.d);


/////////////////////////////////////////////////////////////
// Convert FAC from source data buffer to double and store it
// to fac global variable
//
// Input: Pointer to source data buffer
//
#if 1
//
//New implementation. endian-dependent, 30% faster, using 32-bit integer only
//
static void LoadFAC(const uint8_t *src) {
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  const uint32_t LOW =  0; 
  const uint32_t HIGH = 1;
  #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  const uint32_t LOW =  1;
  const uint32_t HIGH = 0;  
  #else 
  #error "unknown __BYTE_ORDER__"
  #endif
  
  //
  //Convert FAC
  //
  fac.i32[LOW]  = 0; //Set fac to 0
  fac.i32[HIGH] = 0;
  uint16_t exp = (uint16_t)src[FACEXP];

  //No further processing needed if exp==0 (It means fac is zero.)
  if (exp == 0) return;
  
  //
  // Upper 32-bit of double (fac.i32[HIGH])
  //
  
  //Sign bit
  if (src[FACSIGN] & 0x80) fac.i32[HIGH] |= 1ull << 11; //Set sign bit to 1, leave 11-bit for storing exponent

  //Exponent adjustment
  // e =  e -0x80 + 1023 -1
  // -0x80 to remove MBF bias
  // +1023 to add double bias
  // -1 to adjust the position of decimal point of double
  // So, overall adjusment is e = e + 894
  exp += 894;

  //Store Exponent
  fac.i32[HIGH] |= exp;      //1 sign bit + 11-bit exponent in fac.i32[HIGH]               

  //Shift 7-bit mantissa byte 1
  fac.i32[HIGH]<<= 7;
  fac.i32[HIGH] |= (src[FACMANTISSA1] & 0x7f);  //Remove the most significant bit, which is implied in double format

  //Mantissa byte 2
  fac.i32[HIGH]<<= 8;  
  fac.i32[HIGH] |= src[FACMANTISSA2];  //27-bit in fac.i32[HIGH];
  
  //Mantissa byte 3, upper 5 bit
  fac.i32[HIGH]<<=5;
  fac.i32[HIGH] |= src[FACMANTISSA3]>>3;   
  
  //
  // Lower 32-bit of double (fac.i32[LOW])
  //

  //Mantissa byte 3, lower 3 bit
  fac.i32[LOW] = src[FACMANTISSA3]&0b111;

  //Mantissa byte 4
  fac.i32[LOW]<<= 8;  
  fac.i32[LOW] |= src[FACMANTISSA4];

  //FAC Extension
  fac.i32[LOW]<<= 8;  
  fac.i32[LOW] |= src[FACEXT];

  //Shift-in 13 zero bits
  fac.i32[LOW] <<= 13;
}
#else
//
//Old implementation. endian-neutral, slower, using 64-bit integer
//
static void LoadFAC(const uint8_t *src) {
  //
  //Convert FAC
  //
  fac.i64 = 0; //Set fac to 0
  uint16_t exp = (uint16_t)src[FACEXP];

  //No further processing needed if exp==0 (It means fac is zero.)
  if (exp != 0) {
    //Sign bit
    if (src[FACSIGN] & 0x80) fac.i64 |= 1ull << 11; //Set sign bit to 1, leave 11-bit for storing exponent

    //Exponent adjustment
    // e =  e -0x80 + 1023 -1
    // -0x80 to remove MBF bias
    // +1023 to add double bias
    // -1 to adjust the position of decimal point of double
    // So, overall adjusment is e = e + 894
    exp += 894;

    //Shift in Exponent
    fac.i64 |= exp;      //1 sign bit + 11-bit exponent in fac.i64                
  
    //Shift 7-bit mantissa byte 1
    fac.i64<<= 7;
    fac.i64 |= (src[FACMANTISSA1] & 0x7f);  //Remove the most significant bit, which is implied in double format

    //Mantissa byte 2
    fac.i64<<= 8;  
    fac.i64 |= src[FACMANTISSA2];

    //Mantissa byte 3
    fac.i64<<= 8;  
    fac.i64 |= src[FACMANTISSA3];

    //Mantissa byte 4
    fac.i64<<= 8;  
    fac.i64 |= src[FACMANTISSA4];

    //FAC Extension
    fac.i64<<= 8;  
    fac.i64 |= src[FACEXT];

    //Shift-in 13 zero bits
    fac.i64 <<= 13;
  }
}
#endif

/////////////////////////////////////////////////////////////
// Convert ARG from source data buffer to double and store it
// to arg global variable
//
// Input: Pointer to source data buffer
//
#if 1
//
//New implementation. endian-dependent, 30% faster, using 32-bit integer only
//
static void LoadARG(const uint8_t *src) {
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  const uint32_t LOW =  0; 
  const uint32_t HIGH = 1;
  #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  const uint32_t LOW =  1;
  const uint32_t HIGH = 0;  
  #else 
  #error "unknown __BYTE_ORDER__"
  #endif
  
  //
  //Convert ARG
  //
  arg.i32[LOW]  = 0; //Set arg to 0
  arg.i32[HIGH] = 0;
  uint16_t exp = (uint16_t)src[ARGEXP];

  //No further processing needed if exp==0 (It means arg is zero.)
  if (exp == 0) return;
  
  //
  // Upper 32-bit of double (arg.i32[HIGH])
  //
  
  //Sign bit
  if (src[ARGSIGN] & 0x80) arg.i32[HIGH] |= 1ull << 11; //Set sign bit to 1, leave 11-bit for storing exponent

  //Exponent adjustment
  // e =  e -0x80 + 1023 -1
  // -0x80 to remove MBF bias
  // +1023 to add double bias
  // -1 to adjust the position of decimal point of double
  // So, overall adjusment is e = e + 894
  exp += 894;

  //Store Exponent
  arg.i32[HIGH] |= exp;      //1 sign bit + 11-bit exponent in fac.i32[HIGH]               

  //Shift 7-bit mantissa byte 1
  arg.i32[HIGH]<<= 7;
  arg.i32[HIGH] |= (src[ARGMANTISSA1] & 0x7f);  //Remove the most significant bit, which is implied in double format

  //Mantissa byte 2
  arg.i32[HIGH]<<= 8;  
  arg.i32[HIGH] |= src[ARGMANTISSA2];  //27-bit in fac.i32[HIGH];
  
  //Mantissa byte 3, upper 5 bit
  arg.i32[HIGH]<<=5;
  arg.i32[HIGH] |= src[ARGMANTISSA3]>>3;   
  
  //
  // Lower 32-bit of double (fac.i32[LOW])
  //

  //Mantissa byte 3, lower 3 bit
  arg.i32[LOW] = src[ARGMANTISSA3]&0b111;

  //Mantissa byte 4
  arg.i32[LOW]<<= 8;  
  arg.i32[LOW] |= src[ARGMANTISSA4];

  //Shift-in 13+8 zero bits
  arg.i32[LOW] <<= 13+8;
}
#else
//
//Old implementation. endian-neutral, slower, using 64-bit integer  
//
static void LoadARG(const uint8_t *src) {
  //
  //Convert ARG
  //
  arg.i64 = 0;  
  uint16_t exp = (uint16_t)src[ARGEXP];

  //No further processing needed if exp==0
  if (exp != 0) {
    //Sign bit
    if (src[ARGSIGN] & 0x80) arg.i64 |= 1ull << 11; //Set sign bit to 1, leave 11-bit for storing exponent

    //Shift in Exponent
    exp += 894;
    arg.i64 |= exp;        //1 sign bit + 11-bit exponent in arg.i64              

    //Shift in 7-bit mantissa byte 1
    arg.i64<<= 7;
    arg.i64 |= (src[ARGMANTISSA1] & 0x7f);  //Remove the most significant bit, which is implied in double format

    //Mantissa byte 2
    arg.i64<<= 8;  
    arg.i64 |= src[ARGMANTISSA2];

    //Mantissa byte 3
    arg.i64<<= 8;  
    arg.i64 |= src[ARGMANTISSA3];

    //Mantissa byte 4
    arg.i64<<= 8;  
    arg.i64 |= src[ARGMANTISSA4];

    //Shift-in 13+8 zero bits
    arg.i64<<= 13+8;
  }
}
#endif

/////////////////////////////////////////////////////////////
// Convert Both FAC and ARG from source data buffer to double 
// and store them to fac and arg global variables
//
// Input: Pointer to source data buffer
//
static void LoadFAC_ARG(const uint8_t *src) {
  LoadFAC(src);
  LoadARG(src);
}

/////////////////////////////////////////////////////////////
// Convert result in double format to MBF and store
// the result in dest buffer. The first byte is error code.
//
// Input: Pointer to dest buffer
//
static void StoreResult(uint8_t *dest) {
  //Clear Error Flag
  dest[RESERROR] = 0;

  //Infinity?
  if (fpclassify(result.d)== FP_INFINITE) {
    dest[RESERROR] = OVERFLOWERROR;
    memset(dest+1, 0, 7);
    return;    
  } 
  //NAN?
  else if (isnan(result.d)) {
    dest[RESERROR] = IQERROR;
    memset(dest+1, 0, 7);
    return;    
  }

  //Get 11-bit exponent
  uint16_t exponent = (uint16_t)(result.i64 >> 52) & 0x7ff;
  //if exponent<=894, underflow occurs. return 0.0
  if (exponent <= 894) {
    memset(dest+1, 0, 7);
    return;
  }

  //adjust exponent
  exponent -= 894;
  if (exponent > 255) {
    //Overflow error!
    dest[RESERROR] = OVERFLOWERROR;
    memset(dest+1, 0, 7);
    return;
  }
  //Store Exponent
  dest[RESEXP] = (uint8_t)exponent; 

  //Get sign bit
  dest[RESSIGN] = (result.d < 0.0) ? 0x80 : 0;

  //Remove unused bits
  result.i64 >>= 13;

  //Get extension bit
  dest[RESEXT] = (uint8_t)result.i64;

  //Mantissa Byte 4
  result.i64 >>= 8;
  dest[RESMANTISSA4] = (uint8_t)result.i64;

  //Mantissa Byte 3
  result.i64 >>= 8;
  dest[RESMANTISSA3] = (uint8_t)result.i64;

  //Mantissa Byte 2
  result.i64 >>= 8;
  dest[RESMANTISSA2] = (uint8_t)result.i64;

  //Mantissa Byte 1, MSB + 7 bit mantissa
  result.i64 >>= 8;
  dest[RESMANTISSA1] = (uint8_t)result.i64 | 0x80 ; //7-bit mantissa, MSB is always set
}

/////////////////////////////////////////////////////////////
// Round FAC using FAC Ext
//
// The original implementation of these operations round the FAC
// using FAC Extension before the actual calculation. We try
// to match its behaviour here.
//
// Input: Pointer to buffer storing FAC and ARG
//
// Return: OVERFLOWERROR or 0 (No Error)
//
// The Rouding result is stored to the buffer directly.
//
// The algorithm is:
// 1) If FAC.EXP = 0, do nothing
// 2) If MSB of FAC.EXT = 0, do nothing
// 3) Add one to mantissa
// 4) If there is carry, add one to FAC.EXP and shift mantissa right
// 5) If FAC.EXP overflow, show OVERFLOW ERROR
//
static uint8_t RoundFAC(uint8_t *fac) {
  //Do nothing if FAC Ext MSB is 0
  if ((fac[FACEXT]&0x80)==0) {
    return 0; //No Error
  }
  
  //Do nothing if FAC Exp = 0
  uint8_t exp = fac[FACEXP];
  if (exp==0) {
    return 0; //No Error
  }
  
  //Assemble Mantissa
  uint32_t mantissa = fac[FACMANTISSA1]<<24 |
                      fac[FACMANTISSA2]<<16 |
                      fac[FACMANTISSA3]<<8  |
                      fac[FACMANTISSA4];
  
  //Add 1 to mantissa
  ++mantissa;

  //If mantissa=0, there is carry. i.e. mantissa=0x100000000
  //Increase exponent and shift right
  if (mantissa==0) {
    ++exp; 
    mantissa=0x80000000; // 0x100000000>>1 = 0x8000000
  }
  
  //If exp=0, overflow error!
  if (exp==0) {
    fac[RESERROR] = OVERFLOWERROR;
    memset(fac+1, 0, 7);
    return OVERFLOWERROR;
  }
  
  //Write mantissa and exp back
  fac[FACEXT] = 0;   //The original implementation sets FAC Extension to 0
  fac[FACMANTISSA4] = mantissa; mantissa>>=8;
  fac[FACMANTISSA3] = mantissa; mantissa>>=8;  
  fac[FACMANTISSA2] = mantissa; mantissa>>=8;
  fac[FACMANTISSA1] = mantissa;
  fac[FACEXP] = exp;
                      
  return 0; //No Error
}

/////////////////////////////////////////////////////////////
// FADD - ARG + FAC
//
// Test program shows that the performance gain is very minor.
//
// Input: Pointer to parameter buffer
//
void fadd(uint8_t *paramBuffer) {
  LoadFAC_ARG(paramBuffer);
  result.d = arg.d + fac.d;
  DEBUG_PRINT_ALL("fadd"); 
  StoreResult(paramBuffer);  
}

/////////////////////////////////////////////////////////////
// FSUB - ARG - FAC
// Added on 27-Feb-2026 V1.1.8
//
// Test program shows that the performance gain is very minor.
//
// Input: Pointer to parameter buffer
//
void fsub(uint8_t *paramBuffer) {
  LoadFAC_ARG(paramBuffer);
  result.d = arg.d - fac.d;
  DEBUG_PRINT_ALL("fsub"); 
  StoreResult(paramBuffer);  
}

/////////////////////////////////////////////////////////////
// FMUL - ARG * FAC
//
// Input: Pointer to parameter buffer
//
void fmul(uint8_t *paramBuffer) {
  LoadFAC_ARG(paramBuffer);
  result.d = arg.d * fac.d;
  DEBUG_PRINT_ALL("fmul");
  StoreResult(paramBuffer);
}

/////////////////////////////////////////////////////////////
// FDIV - ARG / FAC
//
// The original implementation round FAC before calculation.
//
// Input: Pointer to parameter buffer
//
void fdiv(uint8_t *paramBuffer) {
  if (RoundFAC(paramBuffer)!=0) {
    DEBUG_PRINTF("fdiv: RoundFAC Overflow Error\n");
    return;
  }  
  LoadFAC_ARG(paramBuffer);
  if (fac.d != 0.0) { 
    result.d = arg.d / fac.d;
    DEBUG_PRINT_ALL("fdiv"); 
    StoreResult(paramBuffer);
    paramBuffer[RESEXT] &= 0b11000000;
    //Note: From Applesoft Source Code, the lowest 6-bits of
    //FAC Extension are always zero. So, we clear those bits
    //so that we have the same result as Applesoft.
  } else {
    DEBUG_PRINTF("fdiv:Division by Zero error\n");
    paramBuffer[RESERROR] = DIV0ERROR;
    memset(paramBuffer+1, 0, 7);
  }
}

/////////////////////////////////////////////////////////////
// FSIN - sin(fac)
//
// The original implementation round FAC before calculation.
//
// Input: Pointer to parameter buffer
//
void fsin(uint8_t *paramBuffer) {
  if (RoundFAC(paramBuffer)!=0) {
    DEBUG_PRINTF("fsin: RoundFAC Overflow Error\n");
    return;
  }  
  LoadFAC(paramBuffer);
  result.d = sin(fac.d);
  DEBUG_PRINT_FAC_RES("sin");
  StoreResult(paramBuffer);
}

/////////////////////////////////////////////////////////////
// FCOS - cos(fac)
//
// The original implementation round FAC before calculation.
//
// Input: Pointer to parameter buffer
//
void fcos(uint8_t *paramBuffer) {
  if (RoundFAC(paramBuffer)!=0) {
    DEBUG_PRINTF("fcos: RoundFAC Overflow Error\n");
    return;
  }    
  LoadFAC(paramBuffer);
  result.d = cos(fac.d);
  DEBUG_PRINT_FAC_RES("cos");
  StoreResult(paramBuffer);
}

/////////////////////////////////////////////////////////////
// FTAN - tan(fac)
//
// Input: Pointer to parameter buffer
//
/*******************************************************
All MBF shown is in this format
FACEXP FACMANTISSA1..4 FACEXT

tan(pi/2) is undefined. If TAN(ATN(1)*2) is executed on Apple II,
error occurs. But on Pico, there is no error. The result is 

1.645522e+10. It can be represented in MBF.

If ATN(1)*2 is executed, the result should be pi/2.

This number in MBF is

81 C9 0F DA A2 00

And Division by Zero error occurs if this number is put into TAN().

If 81 C9 0F DA A1 00 is used, Division by zero error also occurs.
But if this number is put into tan() on Pico, the result is

1.899580e+09.

If 81 C9 0F DA A0 00 is used, there is no error.

So, in order to have the same behaviour as real Apple II, this
function returns DIV0ERROR when the absolute value of the result
is greater than 1.8995e+09
********************************************************/
void ftan(uint8_t *paramBuffer) {
  static const double limit = 1.8995e+09;
  
  LoadFAC(paramBuffer);
  result.d = tan(fac.d);
  
  //If result is greater than a certain limit, set it to infinity
  //So that StoreResult() will generate error.
  if (fabs(result.d)>limit) result.d = infinity();
  
  DEBUG_PRINT_FAC_RES("tan");
  StoreResult(paramBuffer);

  //tan() is undefined at pi/2. Applesoft reports it as Division by Zero
  //error, not Illegal Quantity Error. So, if error code is not 0,
  //set it to Division by Zero error.
  if (paramBuffer[RESERROR]!=0) {
    DEBUG_PRINTF("tan: Division by Zero error\n");
    paramBuffer[RESERROR] = DIV0ERROR;
  }
}

/////////////////////////////////////////////////////////////
// FATN - atan(fac)
//
// Input: Pointer to parameter buffer
//
void fatn(uint8_t *paramBuffer) {
  LoadFAC(paramBuffer);
  result.d = atan(fac.d);
  DEBUG_PRINT_FAC_RES("atn");
  StoreResult(paramBuffer);
}

/////////////////////////////////////////////////////////////
// FLOG - log(fac)
//
// Input: Pointer to parameter buffer
//
void flog(uint8_t *paramBuffer) {
  LoadFAC(paramBuffer);
  result.d = log(fac.d);
  DEBUG_PRINT_FAC_RES("log");
  StoreResult(paramBuffer);
}

/////////////////////////////////////////////////////////////
// FEXP - exp(fac)
//
// Input: Pointer to parameter buffer
//
void fexp(uint8_t *paramBuffer) {
  LoadFAC(paramBuffer);
  result.d = exp(fac.d);
  DEBUG_PRINT_FAC_RES("exp");
  StoreResult(paramBuffer);
}

/////////////////////////////////////////////////////////////
// FSQR - sqrt(fac)
//
// The original implementation round FAC before calculation.
//
// Input: Pointer to parameter buffer
//
void fsqr(uint8_t *paramBuffer) {
  if (RoundFAC(paramBuffer)!=0) {
    DEBUG_PRINTF("fsqr: RoundFAC Overflow Error\n");
    return;
  }    
  LoadFAC(paramBuffer);
  result.d = sqrt(fac.d);
  DEBUG_PRINT_FAC_RES("sqr");
  StoreResult(paramBuffer);
}


/////////////////////////////////////////////////////////////
// Format a double number as a String with the format matching
// Applesoft BASIC
//
// Input: double d  - the floating number
//        char* buf - Pointer to buffer to receive the output
//
// Output: int      - Number of characters written to buf
//
// No need to consider underflow/overflow problem.
// Since the range of double covers the entire range of MBF.
// So, the number must be valid and printable.
static int formatApplesoftString(double d, char* buf) {
  //Limit the output of snprintf to a reasonable length
  const size_t BUFFERSIZE = 20; 
  
  int len = 0;  //Number of char generated
  
  //Fast track if d==0
  if (d==0.0) {
    buf[1]='0';
    buf[2]='\0';
    return 1; //Length = 1
  }else if (d < 0.0) {  //negative?
    *buf++ = '-';
    d = fabs(d);
    ++len;
  }
  
  //To match the formatting of Applesoft
  if (d < 1e-4) len+=snprintf(buf, BUFFERSIZE,"%.9G", d);
  else if (d < 1e-3) len+=snprintf(buf, BUFFERSIZE, "%.9GE-04", d * 1e4); //Force n.nnnnnnnnE-04
  else if (d < 1e-2) len+=snprintf(buf, BUFFERSIZE, "%.9GE-03", d * 1e3); //Force n.nnnnnnnnE-03
  else len+=snprintf(buf, BUFFERSIZE, "%.9G", d);

  //Remove Leading Zero.
  //No need to consider the case of "0" since we have handled it above.
  //Don't use memcpy since src and dest overlap
  if (buf[0] == '0') {
    memmove(buf,buf+1,len); //Remove first char. copy null char at the end as well
    --len;
  }    
  return len;
}

/////////////////////////////////////////////////////////////
// FOUT - Format FAC as a string
//
// Input: Pointer to parameter buffer
//
// Parameter Output: 
//   First byte is the length of the string
//   The following is a NULL-terminated string
//
void fout(uint8_t *paramBuffer) {
  LoadFAC(paramBuffer);  
  DEBUG_PRINT_FAC("fout");
  paramBuffer[0] = formatApplesoftString(fac.d, paramBuffer+1); //return the length of output string
  DEBUG_PRINTF("buf=%s\n",paramBuffer+1);
  assert(paramBuffer[0]==strlen(paramBuffer+1));  //Make sure length is correct
}

