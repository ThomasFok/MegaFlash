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
static dma_channel_config_t dma_config;
static int channel;

int GetMemoryDMAChannel() {
  return channel;
}

//////////////////////////////////////////////////////
// Initialize DMA Channel Data Structure.
// It must be called before using any DMA routines
// 
void InitDMAChannel() {
  channel = dma_claim_unused_channel(true);
  
  dma_config = dma_channel_get_default_config(channel);
  channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
  channel_config_set_read_increment(&dma_config, true);
  channel_config_set_write_increment(&dma_config, true);
  
  //Enable CRC sniffer
  channel_config_set_sniff_enable(&dma_config, true);
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
 
  const dma_channel_config orgConfig = dma_config; //Save Original Config 
  channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len,                  // Number of transfers
      true                  // Start immediately.
  );
  
  dma_config = orgConfig;   //restore original DMA Config
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
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
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
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
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
  
  const dma_channel_config orgConfig = dma_config; //Save Original Config
  channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
  channel_config_set_read_increment(&dma_config, false);  //Set read increment to false
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len,                  // Number of transfers
      true                  // Start immediately.
  );
  
  dma_config = orgConfig;   //restore original DMA Config 
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
  
  const dma_channel_config orgConfig = dma_config; //Save Original Config
  channel_config_set_read_increment(&dma_config, false);  //Set read increment to false
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );

  dma_config = orgConfig;   //restore original DMA Config 
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
  
  const dma_channel_config orgConfig = dma_config; //Save Original Config
  channel_config_set_read_increment(&dma_config, false);  //Set read increment to false
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );

  dma_config = orgConfig;   //restore original DMA Config 
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
  
  const dma_channel_config orgConfig = dma_config; //Save Original Config 
  SetCRC16Seed(channel,DEFAULT_CRC16_SEED);
  channel_config_set_write_increment(&dma_config, false); //Set write increment to false
  channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len,                  // Number of transfers
      true                  // Start immediately.
  );
  
  dma_config = orgConfig;   //restore original DMA Config 
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
  
  const dma_channel_config orgConfig = dma_config; //Save Original Config   
  SetCRC16Seed(channel,DEFAULT_CRC16_SEED);
  channel_config_set_write_increment(&dma_config, false); //Set write increment to false
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );
  
  dma_config = orgConfig;   //restore original DMA Config 
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
  
  const dma_channel_config orgConfig = dma_config; //Save Original Config     
  SetCRC32Seed(channel,DEFAULT_CRC32_SEED);
  channel_config_set_write_increment(&dma_config, false); //Set write increment to false
  channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len,                  // Number of transfers
      true                  // Start immediately.
  );
  
  dma_config = orgConfig;   //restore original DMA Config 
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
  
  const dma_channel_config orgConfig = dma_config; //Save Original Config   
  SetCRC32Seed(channel,DEFAULT_CRC32_SEED);
  channel_config_set_write_increment(&dma_config, false); //Set write increment to false
  
  dma_channel_configure(
      channel,              // Channel to be configured
      &dma_config,          // The DMA configuration
      dest,                 // The initial write address
      src,                  // The initial read address
      len/4,                // Number of transfers
      true                  // Start immediately.
  );
  
  dma_config = orgConfig;   //restore original DMA Config 
  dma_channel_wait_for_finish_blocking(channel);    
  
  return GetCRC();
}