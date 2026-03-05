#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "dmamemops.h"
#include "defines.h"
#include "debug.h"

////////////////////////////////////////////////////////////////////
// Hardware CRC
//
// The DMA hardware can transfer data and calculate CRC at the same time
// To calculate CRC32, call SetCRC32Seed() with the required seed value.
// Then, transfer the data by CopyMemory() or CopyMemoryAligned().
// The CRC value can be retrieved by GetCRC().
//
// If data transfer is not required, CRC16() and CRC32() functions 
// can be used. 


//Global Variables
static int channel; 
static dma_channel_config_t copymem_config_size32;
static dma_channel_config_t copymem_config_size8;
static dma_channel_config_t zeromem_config_size32;
static dma_channel_config_t zeromem_config_size8;
static dma_channel_config_t crc_config_size32;
static dma_channel_config_t crc_config_size8;


int GetMemoryDMAChannel() {
  return channel;
}

//////////////////////////////////////////////////////
// Initialize DMA Channel Data Structure.
// It must be called before using any DMA routines
// 
void InitDMAChannel() {
  channel = dma_claim_unused_channel(true);

  //
  //Generate DMA config for various routines
  //

  //CopyMemoryAligned
  copymem_config_size32 = dma_channel_get_default_config(channel);
  channel_config_set_transfer_data_size(&copymem_config_size32, DMA_SIZE_32);
  channel_config_set_read_increment(&copymem_config_size32, true);
  channel_config_set_write_increment(&copymem_config_size32, true);
  channel_config_set_sniff_enable(&copymem_config_size32, true);     //Enable CRC sniffer
  
  //CopyMemory
  copymem_config_size8 = copymem_config_size32;
  channel_config_set_transfer_data_size(&copymem_config_size8, DMA_SIZE_8);
  
  //ZeroMemoryAligned
  zeromem_config_size32 = copymem_config_size32;
  channel_config_set_read_increment(&zeromem_config_size32, false);  
  
  //ZeroMemory
  zeromem_config_size8 = copymem_config_size32;
  channel_config_set_read_increment(&zeromem_config_size8, false);
  channel_config_set_transfer_data_size(&zeromem_config_size8, DMA_SIZE_8);
  
  //CRC16Aligned, CRC32Aligned
  crc_config_size32 = copymem_config_size32;
  channel_config_set_write_increment(&crc_config_size32, false); 
  
  //CRC16, CRC32
  crc_config_size8 = copymem_config_size32;
  channel_config_set_write_increment(&crc_config_size8, false); 
  channel_config_set_transfer_data_size(&crc_config_size8, DMA_SIZE_8);  
}

//////////////////////////////////////////////////////
// Copy Memory by DMA with 8-bit transfer size
// 
// Input: dest - destination pointer
//        src  - source pointer
//        len  - Number of bytes to copy
// 
void CopyMemory(uint8_t* dest,const uint8_t *src,const uint32_t len) {
  assert(!dma_channel_is_busy(channel));
 
  dma_channel_configure(
      channel,                // Channel to be configured
      &copymem_config_size8,  // The DMA configuration
      dest,                   // The initial write address
      src,                    // The initial read address
      len,                    // Number of transfers
      true                    // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(channel);  
}

//////////////////////////////////////////////////////
// Copy Memory by DMA with 32-bit transfer size
// The src,dest pointers and len must be 32-bit aligned
// 
// Input: dest - destination pointer
//        src  - source pointer
//        len  - Number of bytes to copy
// 
void __no_inline_not_in_flash_func(CopyMemoryAligned)(uint8_t* dest,const uint8_t *src,const uint32_t len) {
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert((uint32_t)src%4==0);   //must be 32-bit aligned  
  assert(!dma_channel_is_busy(channel));  
  
  dma_channel_configure(
      channel,                // Channel to be configured
      &copymem_config_size32, // The DMA configuration
      dest,                   // The initial write address
      src,                    // The initial read address
      len/4,                  // Number of transfers
      true                    // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(channel);  
}

//////////////////////////////////////////////////////
// Copy Memory by DMA with 32-bit transfer size in background
// The src, dest pointers and len must be 32-bit aligned.
// 
// Input: dest - destination pointer
//        src  - source pointer
//        len  - Number of bytes to copy
// 
// Usage: Start DMA transfer with this routine.
// DMA will execute in background. 
// Then, call DMAWaitFinish() to make sure the transfer is complete.
void __no_inline_not_in_flash_func(CopyMemoryAlignedBG)(uint8_t* dest,const uint8_t *src,const uint32_t len) {
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert((uint32_t)src%4==0);   //must be 32-bit aligned  
  assert(!dma_channel_is_busy(channel));  
  
  dma_channel_configure(
      channel,                // Channel to be configured
      &copymem_config_size32, // The DMA configuration
      dest,                   // The initial write address
      src,                    // The initial read address
      len/4,                  // Number of transfers
      true                    // Start immediately.
  );
}

//////////////////////////////////////////////////////
// Fill Memory with zeros
//
// Input: dest - destination pointer
//        len  - Number of bytes to fill
//
void ZeroMemory(uint8_t *dest,const uint32_t len) {
  assert(!dma_channel_is_busy(channel));    
  const uint32_t src[] = {0}; 
  
  dma_channel_configure(
      channel,                // Channel to be configured
      &zeromem_config_size8,  // The DMA configuration
      dest,                   // The initial write address
      src,                    // The initial read address
      len,                    // Number of transfers
      true                    // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(channel);    
}

//////////////////////////////////////////////////////
// Fill Memory with zeros with 32-bit transfer size
// The dest pointer and len must be 32-bit aligned
//
// Input: dest - destination pointer
//        len  - Number of bytes to fill
//
void __no_inline_not_in_flash_func(ZeroMemoryAligned)(uint8_t *dest,const uint32_t len) {
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert(!dma_channel_is_busy(channel));    
  const uint32_t src[] = {0};
  
  dma_channel_configure(
      channel,                // Channel to be configured
      &zeromem_config_size32, // The DMA configuration
      dest,                   // The initial write address
      src,                    // The initial read address
      len/4,                  // Number of transfers
      true                    // Start immediately.
  );

  dma_channel_wait_for_finish_blocking(channel);    
}

//////////////////////////////////////////////////////
// Fill Memory with zeros with 32-bit transfer size in background
// The dest pointer and len must be 32-bit aligned
//
// Input: dest - destination pointer
//        len  - Number of bytes to fill
//
void __no_inline_not_in_flash_func(ZeroMemoryAlignedBG)(uint8_t *dest,const uint32_t len) {
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)dest%4==0);  //must be 32-bit aligned
  assert(!dma_channel_is_busy(channel));    
  const uint32_t src[] = {0};
  
  dma_channel_configure(
      channel,                // Channel to be configured
      &zeromem_config_size32, // The DMA configuration
      dest,                   // The initial write address
      src,                    // The initial read address
      len/4,                  // Number of transfers
      true                    // Start immediately.
  );
}


//////////////////////////////////////////////////////
// Calculate CRC16-XMODEM 
//
// Input: src  - data pointer
//        len  - Number of bytes
//
// Output: CRC16
//
uint32_t CRC16(const uint8_t *src,const uint32_t len) {
  assert(!dma_channel_is_busy(channel));    
  uint32_t dest[1];   //dummy dest
  
  SetCRC16Seed(channel,DEFAULT_CRC16_SEED);
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &crc_config_size8,    // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len,                  // Number of transfers
      true                  // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(channel);    
  
  return GetCRC();
}

//////////////////////////////////////////////////////
// Calculate CRC16-XMODEM with 32-bit transfer size
// The src pointer and len must be 32-bit aligned
//
// Input: src  - data pointer
//        len  - Number of bytes
//
// Output: CRC16
//
uint32_t CRC16Aligned(const uint8_t *src,const uint32_t len) {
  assert(!dma_channel_is_busy(channel));    
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)src%4==0);   //must be 32-bit aligned  
  uint32_t dest[1];             //dummy dest
  
  SetCRC16Seed(channel,DEFAULT_CRC16_SEED);
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &crc_config_size32,   // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(channel);    
  
  return GetCRC();
}

//////////////////////////////////////////////////////
// Calculate CRC32
//
// Input: src  - data pointer
//        len  - Number of bytes
//
// Output: CRC32
//
// Speed: 3us for 128 Bytes. 
//        4us for 512 Bytes. 
//        35us for 4096 Bytes
uint32_t CRC32(const uint8_t *src,const uint32_t len) {
  assert(!dma_channel_is_busy(channel));    
  uint32_t dest[1];   //dummy dest
  
  SetCRC32Seed(channel,DEFAULT_CRC32_SEED);
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &crc_config_size8,    // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len,                  // Number of transfers
      true                  // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(channel);    
  
  return GetCRC();
}


//////////////////////////////////////////////////////
// Calculate CRC32 with 32-bit transfer size
// The src pointer and len must be 32-bit aligned
//
// Input: src  - data pointer
//        len  - Number of bytes
//
// Output: CRC32
//
// Speed: 2us for 128 Bytes. 
//        3us for 512 Bytes. 
//        10us for 4096 Bytes
uint32_t CRC32Aligned(const uint8_t *src,const uint32_t len) {
  assert(!dma_channel_is_busy(channel));    
  assert(len%4==0);             //must be multiple of 4
  assert((uint32_t)src%4==0);   //must be 32-bit aligned  
  uint32_t dest[1];             //dummy dest
  
  SetCRC32Seed(channel,DEFAULT_CRC32_SEED);
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &crc_config_size32,   // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );
  
  dma_channel_wait_for_finish_blocking(channel);    
  
  return GetCRC();
}