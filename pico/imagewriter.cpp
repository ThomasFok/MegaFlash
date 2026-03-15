#include "string.h"
#include "imagewriter.h"
#include "mediaaccess.h"
#include "debug.h"
#include "defines.h"
#include "misc.h"

static const uint32_t SECTORSIZE       = 256; //DOS 3.3 Sector Size
static const uint32_t BLOCKSBUFFERSIZE = 8*BLOCKSIZE;
static const uint32_t UNKNOWNBLOCKNUM  = -1;

//////////////////////////////////////////////////////////////////////////
//Sector mapping between DO and PO disk images
//
//Sectors Order on floppy disk:
// DOS 3.3: 00 07 14 06 13 05 12 04 11 03 10 02 09 01 08 15
// ProDOS : 00 08 01 09 02 10 03 11 04 12 05 13 06 14 07 15
//
static const uint8_t block_map[][2] {
  { 0,14},  //DOS Sector {0,1} = ProDOS Sector { 0,14}
  {13,12},  //DOS Sector {2,3} = ProDOS Sector {13,12}
  {11,10},  //and so on
  { 9, 8},
  { 7, 6},
  { 5, 4},
  { 3, 2},
  { 1,15}
};

//
// Constructor
//
CImageWriter::CImageWriter(const char* filename){
  //Initialize data members
  blocksBuffer = nullptr;
  format = ImageFormat::PO;  //Default image format is PO  
  blockCount = 0;
  firstBlockNum = UNKNOWNBLOCKNUM;
  #ifndef NDEBUG
  expectedBlockNum = 0;  //Debug only
  #endif

  //
  //Detect image format based on filename extension
  //
  if (filename==nullptr) filename = "";

  //Find filename extension 
  const char* extension = strrchr(filename,'.'); //Seach last occurrence of '.'
  if (extension==nullptr) extension = ""; // '.' not found
  else ++extension; //point to the character after '.'
  
  //Check filename extension
  if (0==stricmp(extension,"dsk") || 0==stricmp(extension,"do")) {
    format = ImageFormat::DO;
    INFO_PRINTF("CImagerWriter: ImageFormat set to DO\n");
  } 
  
  //Allocate buffer for 8 blocks if ImageFormat is DO
  if (format == ImageFormat::DO) {
    blocksBuffer = new uint8_t[BLOCKSBUFFERSIZE]; 
    assert(blocksBuffer!=nullptr);
  }
}

//
// Destructor
//
CImageWriter::~CImageWriter() {
  delete[] blocksBuffer;
}


////////////////////////////////////////////////////////////////
// WriteBlock - Substitute the WriteBlockForImageTransfer() function
// Assume block is received in successive order.
//
// If the disk image format is ProDOS, simply pass to WriteBlockForImageTransfer
//
// If the image format is DOS 3.3, buffer the payload until 8 blocks are received.
// Then, all 8 blocks are written to storage medium all together.
//
// Note that we must have got all 8 DOS 3.3 'blocks' before we can reassemble 
// a complete ProDOS block.
//
// Input: unitNum    - Unit Number (1-N)
//        blockNum   - Block Number
//        srcBuffer  - Source Buffer
//
// Output: bool - success
//
bool CImageWriter::WriteBlock(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer){ 
  //
  // ProDOS Order - Pass to WriteBlockForImageTransfer()
  //
  if (format == ImageFormat::PO) {
    return WriteBlockForImageTransfer(unitNum,blockNum,srcBuffer);
  }
  
  //
  // DOS 3.3 Order
  //
  else if (format == ImageFormat::DO) {
    //Make sure the blockNum is in successive order
    assert(blockNum == expectedBlockNum++);
    
    //Record the block number of first block in buffer
    if (blockCount==0) {
      firstBlockNum = blockNum;
    }

    //Copy data to blocks buffer according to block_map
    //to convert the sector order
    assert(blocksBuffer!=nullptr);
    memcpy(blocksBuffer+block_map[blockCount][0]*SECTORSIZE,srcBuffer,SECTORSIZE);
    memcpy(blocksBuffer+block_map[blockCount][1]*SECTORSIZE,srcBuffer+SECTORSIZE,SECTORSIZE);
    ++blockCount;

    bool success = true; //Assume success    
    if (blockCount==8) {
      //Write all blocks to storage
      success = success && Flush(unitNum);
      
      //Reset variables
      blockCount = 0;
      firstBlockNum = UNKNOWNBLOCKNUM;
    }
    
    return success;
  }
 
  //unknown ImageFormat
  assert(0);
  return false; //Failed
}


////////////////////////////////////////////////////
//Write all 8 blocks in blocksBuffer to storage
//
// Input: unitNum - Destination Unit Number
//
// Output: bool - success
//
bool CImageWriter::Flush(const uint unitNum) {
  if (format == ImageFormat::DO) {
    assert(firstBlockNum != UNKNOWNBLOCKNUM);
    assert(blocksBuffer!=nullptr);
    for(uint32_t i=0;i<8;++i) {
      if (!WriteBlockForImageTransfer(unitNum,firstBlockNum+i,blocksBuffer+BLOCKSIZE*i)) return false;
    }
    return true; //success
  }
  
  else if (format == ImageFormat::PO) {
    assert(blockCount == 0); //blockCount should be 0 
    return true;  //Do nothing 
  }  
  
  //Unknown ImageFormat
  assert(0);
  return false; 
}

//////////////////////////////////////////////////////
// Return true if there is no data left in buffer
// i.e. blockCount == 0
// 
// Output: true if no data in buffer
//
bool CImageWriter::IsCompleted() const{
  if (format == ImageFormat::PO) return true;
  else if (format == ImageFormat::DO) return blockCount==0;

  //unknown ImageFormat    
  assert(0);  
  return false; 
}


