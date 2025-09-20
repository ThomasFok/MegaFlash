#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "usbserial.h"
#include "dmamemops.h"
#include "flash.h"
#include "filetransfer.h"
#include "defines.h"
#include "debug.h"
#include "misc.h"
#include "mediaaccess.h"

//--------------------------------------------------------------
//The definitions below must be the same as the ones in busloop.c

extern uint8_t dataBuffer[];
//--------------------------------------------------------------


//ASCII control code
#define SOH 01
#define STX 02
#define EOT 04
#define ACK 06
#define NAK 21
#define CAN 24
#define CTRLC 03

#define onesecond 1000000
#define STARTCHAR 'C'



#if 0  //software CRC routines
static int crc16(const uint8_t *addr, int num)
{
int crc=0;
const int poly=0x1021;

for (; num>0; num--)               /* Step through bytes in memory */
  {
  crc = crc ^ (*addr++ << 8);      /* Fetch byte from memory, XOR into CRC top byte*/
  for (int i=0; i<8; i++)          /* Prepare to rotate 8 bits */
    {
    crc = crc << 1;                /* rotate */
    if (crc & 0x10000)             /* bit 15 was set (now bit 16)... */
      crc = (crc ^ poly) & 0xFFFF; /* XOR with XMODEM polynomic */
                                   /* and ensure CRC remains 16-bit value */
    }                              /* Loop for 8 bits */
  }                                /* Loop until num=0 */
  return crc;                     /* Return updated CRC */
}

static uint32_t crc32(uint8_t *bytp, uint32_t length) {
  uint32_t crc = 0xffffffff;
  
    while(length--) {
        uint32_t byte32 = (uint32_t)*bytp++;

        for (uint8_t bit = 8; bit; bit--, byte32 >>= 1) {
            crc = (crc >> 1) ^ (((crc ^ byte32) & 1ul) ? 0xEDB88320ul : 0ul);
        }
    }
    return ~crc;
}


#endif

uint32_t verificationErrorCount;

//////////////////////////////////////////////////////////
// To process a packet received from PC
//
// The payload can be 128 Bytes or 1024 Bytes. The sender
// can switch at any time according to XModem-1k protocol.
//
// 128 Bytes of data is called a part. So, 1024 Bytes payload
// has 8 parts.
//
// The packet order byte and CRC checksum are validated.
// Then, payload data is copied to dataBuffer. Once we got
// 512 Bytes of data, the data is written to flash.
bool  PacketReceived(const uint8_t *packetData,const uint32_t packetNumber,const uint32_t payloadLength,
                     const uint32_t unitNum,uint8_t packetOrderByte){
  static uint32_t blockNum;
  static uint32_t partsAlreadyInBuffer;
  
  //First Packet==0 Initialize static variables
  if (packetNumber==0) {
    blockNum = 0;
    partsAlreadyInBuffer = 0;
  }

  //Validate packetOrderByte
  if (packetOrderByte!=packetData[0]) return false;
  if (packetOrderByte!=255-packetData[1]) return false;

  //Pointer to payload
  const uint8_t* src = packetData+2; 

  //Validate CRC  
  const uint32_t crc1 = CRC16Aligned(src,payloadLength);
  const uint32_t crc2 = packetData[payloadLength+2]*256+packetData[payloadLength+3];
  if (crc1 != crc2) return false;

  //Valid Packet!
  //Send ACK immediately so that host can send next packet
  //while we are processing the previous one.
  usb_putchar(ACK);

  uint32_t partsRemaining = payloadLength/128;  //Number of 128 Bytes chunk to be processed
  do {
    //
    //Copy data to dataBuffer
    //
    uint32_t partsToCopy = MIN(4-partsAlreadyInBuffer,partsRemaining);  //Number of 128 Bytes chunk to be copied
    CopyMemoryAligned(dataBuffer+partsAlreadyInBuffer*128,src,partsToCopy*128);
    src += partsToCopy*128;
    partsAlreadyInBuffer += partsToCopy;
    partsRemaining -= partsToCopy;
    assert(partsAlreadyInBuffer<=4);
    assert(partsRemaining<=8);
    
    //Do we have 512 Bytes in dataBuffer?
    if (partsAlreadyInBuffer==4) {
      //Write to flash if blockNum<0x10000
      if (blockNum < 0x10000) {
        TurnOnActLed();
        TurnOnPicoLed();
        bool success = WriteBlockForImageTransfer(unitNum,blockNum,dataBuffer);
        if (!success) ++verificationErrorCount;
        TurnOffActLed();
        TurnOffPicoLed();
      }
      
      ++blockNum;
      partsAlreadyInBuffer=0;
    }
  }while(partsRemaining>0);
  
  return true;  //valid packet
}

//////////////////////////////////////////////////////
// XModem Receive (PC to MegaFlash)
// Receive Data from PC using XModem Protcol and 
// write to flash.
//
// Input: unitNum - ProDOS Drive to be written.
//
// Output: int - Number of packets received
//
int xmodemrx(const uint32_t unitNum) {
  const uint MAXSTARTRETRY = 30; //Max retries when initiating the transfer
                                 //Retry every 3 seconds. 90 seconds for user to start transfer
  const uint MAXERROR = 10;

  const uint PADDING = 2;  //To make sure packet payload is 32-bit aligned
  const uint OVERHEAD = 4; //2 Packet Order Bytes and 2-Bytes CRC
  const uint PACKETSIZE128 = 128 + OVERHEAD;
  const uint PACKETSIZE1K  = 1024 + OVERHEAD;
  const uint RXBUFFERSIZE = PADDING + PACKETSIZE1K;
  
  enum {
    STATE_BEGIN,    //Sending 'C' to start file transfer
    STATE_RECEVIE_PACKET,
    STATE_ACK,
    STATE_NAK,
    STATE_EOF_REACHED,
    STATE_COMPLETE,
    STATE_ABORT
  } state = STATE_BEGIN;
  
  int ch, byteCount;
  int errorCount = 0;
  uint32_t packetNumber = 0;  //To count how many 128-bytes packet are recevied.
                              //1k payload is counted as 8 packets
  uint32_t packetSize; 
  uint8_t packetOrderByte = 0;
  verificationErrorCount = 0;

  //allocate packet buffer
  uint8_t* packetBuffer = malloc(RXBUFFERSIZE);
  assert(packetBuffer);
  
  do {
    switch (state) {
      case STATE_BEGIN:
        //Send 'C' every 3 seconds until SOH is received or too many retries
        usb_putchar(STARTCHAR);
        ch=usb_getchar_timeout_us(3*onesecond);

        if (ch==SOH) {
          state=STATE_RECEVIE_PACKET;
          ++packetOrderByte;
          packetSize = PACKETSIZE128;
        } else if (ch==STX) {
          state=STATE_RECEVIE_PACKET;
          ++packetOrderByte;
          packetSize = PACKETSIZE1K;          
        }
        else if (ch==CTRLC || ch==CAN) {
          state=STATE_ABORT;
        }else ++errorCount;
        
        if (errorCount>MAXSTARTRETRY) state=STATE_ABORT;
        break;
        
      case STATE_RECEVIE_PACKET:
        byteCount = usb_getraw_timeout(packetBuffer+PADDING,packetSize,onesecond);
        if (byteCount!=packetSize) {
          if (++errorCount>MAXERROR) state=STATE_ABORT;
          else state=STATE_NAK;
          continue;
        }
        
        const uint32_t payloadLength = packetSize - OVERHEAD;
        assert(payloadLength%128==0); //should be multiple of 128
        bool isValid=PacketReceived(packetBuffer+PADDING,packetNumber,payloadLength,unitNum,packetOrderByte);
        if (isValid) {
          state=STATE_ACK;
          errorCount = 0;
          packetNumber += payloadLength/128;
        } else {
          if (++errorCount<=MAXERROR) state=STATE_NAK;
          else state=STATE_ABORT;
        }
        break;
        
      case STATE_ACK:
        //ACK already sent in PacketReceived()
        //Just wait for SOH or EOT
        ch=usb_getchar_timeout_us(onesecond);
        if (ch==SOH) {
          state=STATE_RECEVIE_PACKET;
          ++packetOrderByte;
          packetSize = PACKETSIZE128;
        }
        else if (ch==STX) {
          state=STATE_RECEVIE_PACKET;
          ++packetOrderByte;
          packetSize = PACKETSIZE1K;          
        }
        else if (ch==EOT) state=STATE_EOF_REACHED;
        else state=STATE_ABORT;
        break;
        
      case STATE_NAK:
        usb_putchar(NAK);
        //wait for SOH or STX
        ch=usb_getchar_timeout_us(onesecond);
        if (ch==SOH) {
          state=STATE_RECEVIE_PACKET;  
          packetSize = PACKETSIZE128;
        } else if (ch==STX) {
          state=STATE_RECEVIE_PACKET;  
          packetSize = PACKETSIZE1K;
        } 
        else state=STATE_ABORT;
        break;
        
      case STATE_EOF_REACHED:
        usb_putchar(ACK); //Final ACK
        state = STATE_COMPLETE;
        break;
    }
  }while (state!=STATE_ABORT && state!=STATE_COMPLETE);
  
  if (state==STATE_ABORT) {
    //Send NAK 10 times in 10ms interval
    //and discard all input
    for(int i=0;i<10;++i) {
      usb_putchar(NAK);
      sleep_ms(10);
      usb_discard();
    }
  }
  
  //free packet buffer
  free(packetBuffer); 

  return (state==STATE_COMPLETE)?packetNumber:-1;
}

//PC -> MegaFlash
void Upload(const uint32_t unitNum) {
  printf("\nYou are ready to upload ProDOS ordered image file (.po/.hdv) to drive %d.\n",unitNum);
  printf("Please start upload using XModem-1k or XModem/CRC protocol\n");
  printf("within 90 seconds. Type Ctrl-C to abort.\n");
  
  int packetCount = xmodemrx(unitNum);
  if (packetCount<0) printf("\nAborted.\n");
  else {
    printf("\n\n");
    if (packetCount%4!=0) printf("Warning:Last block is incomplete.\n");
    if (packetCount>0x40000) printf("Warning:The file is larger than 32MB\n");
    printf("%d blocks received.\n",packetCount/4);
    printf("Verification Error:%d\n",verificationErrorCount);
  }
}

////////////////////////////////////////////////////////////////////////////////////////
//
// XMODEM - Trasmit
//
////////////////////////////////////////////////////////////////////////////////////////

//
// Constants
//
const uint TXPADDING = 1;  //To make sure packet payload is 32-bit aligned
const uint TXOVERHEAD = 5; //1 SOH/STX + 2 Packet Order Bytes and 2-Bytes CRC
const uint TXPACKETSIZE128 = 128 + TXOVERHEAD;
const uint TXPACKETSIZE1K  = 1024 + TXOVERHEAD;
const uint TXBUFFERSIZE = TXPADDING + TXPACKETSIZE1K;

//
// The static variable blockInBuffer needs to be reset before each download session
// The protocol is if packetBuffer is NULL, reset the variable then return
static void SendPacket128(uint8_t* packetBuffer,uint32_t unitNum, uint32_t packetNumber,uint8_t packetOrderByte,bool crcMode) {
  const uint32_t UNKNOWNBLOCK=0xffffffff;
  
  //To store the block number of the block already stored in dataBuffer
  static uint32_t blockInBuffer = UNKNOWNBLOCK;
  
  //Initialize static variable if packetBuffer == NULL and then return
  if (NULL==packetBuffer) {
    blockInBuffer = UNKNOWNBLOCK;
    return; 
  }
  
  uint32_t block = packetNumber /4;
  if (block!=blockInBuffer) {
    TurnOnActLed();
    TurnOnPicoLed();
    ReadBlock(unitNum,block,dataBuffer, NULL);
    blockInBuffer = block;
    TurnOffActLed();
    TurnOffPicoLed();
  }
  
  //Build Packet
  packetBuffer[TXPADDING+0] = SOH;
  packetBuffer[TXPADDING+1] = packetOrderByte;
  packetBuffer[TXPADDING+2] = 255- packetOrderByte;
  
  //copy data to dataBuffer and calculate CRC
  uint8_t *dest = packetBuffer + (TXPADDING+3); //Skip 1 SOH + 2 Packet Order Bytes
  const uint8_t *src = dataBuffer + (packetNumber & 0b11)*128;
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert((uint32_t)src%4==0);   //must be 32-bit aligned  
  
  if (crcMode) {
    SetCRC16Seed(GetMemoryDMAChannel(),DEFAULT_CRC16_SEED);
    CopyMemoryAligned(dest, src, 128);
  } else {
    SetChecksumSeed(GetMemoryDMAChannel(),0);
    CopyMemory(dest, src, 128); //Must use CopyMemory() instead of CopyMemoryAligned()
                                //to calculate 8-bit checksum
  }
  
  if (crcMode) {
    uint32_t crc = GetCRC();  //CRC16 from packet data
    packetBuffer[TXPADDING+132] = (uint8_t) crc;  crc>>=8; //Low Byte 
    packetBuffer[TXPADDING+131] = (uint8_t) crc;           //High Byte
    usb_putraw(packetBuffer+TXPADDING,TXPACKETSIZE128);
  } else {
    uint32_t checksum = GetChecksum();
    packetBuffer[TXPADDING+131] = (uint8_t)checksum;  
    usb_putraw(packetBuffer+TXPADDING,TXPACKETSIZE128-1);  
  }
}

static void SendPacket1k(uint8_t* packetBuffer,uint32_t unitNum, uint32_t packetNumber,uint8_t packetOrderByte) {
  assert(packetNumber%8==0); //should be multiple of 8
  
  uint32_t blockNum = packetNumber/4;
  
  //Build Packet
  packetBuffer[TXPADDING+0] = STX;
  packetBuffer[TXPADDING+1] = packetOrderByte;
  packetBuffer[TXPADDING+2] = 255-packetOrderByte;
  
  //Read payload from flash
  //Two blocks are being sent. 
  TurnOnActLed();
  TurnOnPicoLed();
  uint8_t *dest = packetBuffer+ (TXPADDING+3);  //Skip 1 STX + 2 Packet Order Bytes
  ReadBlock(unitNum,blockNum,dest,NULL);               //Read First Block
  ReadBlock(unitNum,blockNum+1,dest+BLOCKSIZE,NULL);   //Read Second Block
  TurnOffActLed();
  TurnOffPicoLed();  
  
  //Calculate CRC16
  uint32_t crc=CRC16Aligned(dest, BLOCKSIZE*2);
  packetBuffer[TXPADDING+1024+4] = (uint8_t) crc; crc>>=8; //Low Byte
  packetBuffer[TXPADDING+1024+3] = (uint8_t) crc;          //High Byte
  
  //Send the packet
  usb_putraw(packetBuffer+TXPADDING,TXPACKETSIZE1K);
}

int xmodemtx(const uint32_t unitNum, const uint32_t blockCount,enum TxProtocol protocol) {
  assert(blockCount != 0);
  const uint32_t MAXUNKNOWN = 10;
  const uint32_t MAXNAK     = 10;
 
  int ch;
  uint32_t nakCount = 0;
  uint32_t unknownCount = 0;
  uint32_t packetNumber=0;                  //To count number of 128 bytes chunk sent    
  uint32_t packetRemaining = blockCount*4;  //Number of 128 bytes chunk to be sent
  uint8_t  packetOrderByte = 1;
  bool crcMode = false; 
    
  enum {
    STATE_BEGIN,
    STATE_SENDING,
    STATE_EOF_REACHED,
    STATE_COMPLETE,
    STATE_ABORT
  } state = STATE_BEGIN;
 
  //allocate packet buffer
  uint8_t* packetBuffer = malloc(TXBUFFERSIZE);
  assert(packetBuffer);
 
  //Reset static variable
  SendPacket128(NULL,0,0,0,false); //NULL means reset static variable
 
  do {
    switch (state) {
      case STATE_BEGIN:
        ch = usb_getchar_timeout_us(90*onesecond);
        if (ch == STARTCHAR) {
          crcMode = true;
          state = STATE_SENDING;
        }
        else if (ch == NAK) {
          crcMode = false;  //Checksum mode
          protocol = XMODEM128; //Fall back to XMODEM128 since XMODEM1K is CRC only
          state = STATE_SENDING;
        }
        else if (ch == CTRLC) {
          state = STATE_ABORT;
        }
        break;
      case STATE_SENDING:
        if (protocol == XMODEM128) {
          SendPacket128(packetBuffer,unitNum,packetNumber,packetOrderByte,crcMode);
        } else {
          SendPacket1k(packetBuffer,unitNum,packetNumber,packetOrderByte);
        }
        
        ch = usb_getchar_timeout_us(3*onesecond);
        if (ch==-1) { //Timeout
          state=STATE_ABORT;
        }
        else if (ch==ACK) {
          unknownCount = 0;          
          nakCount = 0;
          ++packetOrderByte;
          
          if (protocol == XMODEM128) {
            packetNumber += 1; 
            packetRemaining -= 1;
          } else {
            packetNumber += 8; 
            packetRemaining -= 8;           
          }
          
          //if packetRemaining < 8, switch to XMODEM128
          if (packetRemaining<8) protocol = XMODEM128;
          
          //End of File?
          if (packetRemaining==0) state=STATE_EOF_REACHED;
        }
        else if (ch==NAK) {
          unknownCount = 0;
          ++nakCount;
          if (nakCount > MAXNAK) state = STATE_ABORT;
          else continue;  //Resend Packet
        }
        else if (ch==CAN) state=STATE_ABORT;
        else {
          ++unknownCount;
          if (unknownCount>MAXUNKNOWN) state=STATE_ABORT;
        }
        break;
      case STATE_EOF_REACHED:
        //Send EOT, wait for ACK
        usb_putchar(EOT);
        ch = usb_getchar_timeout_us(3*onesecond);
        if (ch==-1) state=STATE_ABORT;
        else if (ch==ACK) state=STATE_COMPLETE;
        else if (ch==NAK) {
          ++nakCount;
          if (nakCount > MAXNAK) state = STATE_ABORT;
          else continue;   //Resend EOT
        }
        else {
          ++unknownCount;
          if (unknownCount>MAXUNKNOWN) state=STATE_ABORT;
        }
        break;
    }
  }while (state!=STATE_COMPLETE && state!=STATE_ABORT);

  if (state == STATE_ABORT) {
    usb_discard();
  }
  
  //free packet buffer
  free(packetBuffer); 

  return (state==STATE_COMPLETE)?packetNumber:-1;
}



void Download(const uint32_t unitNum,enum TxProtocol protocol) { 
  printf("\nYou are ready to download ProDOS image file (.po/.hdv) from drive %d.\n",unitNum);
  printf("Please start download using %s protocol within 90 seconds.\n",protocol==XMODEM128?"XModem/CRC":"XModem-1k");
  printf("Type Ctrl-C to abort.\n");
  
  uint32_t blockCount = GetBlockCountForImageTransfer(unitNum);

  int packetCount = xmodemtx(unitNum, blockCount, protocol);
  if (packetCount<0) printf("\nAborted.\n");
  else {
    printf("\n\nDownload Completed.\n");
    printf("%d blocks sent.\n",packetCount/4);
  }
}





