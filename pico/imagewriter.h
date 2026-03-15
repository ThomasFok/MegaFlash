#ifndef _IMAGEWRITER_H
#define _IMAGEWRITER_H

#include "pico/stdlib.h"

//Disk Image Format
enum class ImageFormat{PO, DO}; //ProDOS Order, DOS 3.3 Order

////////////////////////////////////////////////////////
// CImageWrite 
//
// This class is for TFTP Download to convert DOS Order
// image to ProDOS order.
//
class CImageWriter {
public:
  CImageWriter(const char* filename);
  ~CImageWriter();

  bool WriteBlock(const uint unitNum, const uint blockNum, const uint8_t* srcBuffer);
  ImageFormat getImageFormat() const {return format;}
  bool IsCompleted() const;

protected:  
  ImageFormat format;
  uint8_t* blocksBuffer;  
  uint32_t blockCount;      //Number of blocks in blocksBuffer
  uint32_t firstBlockNum;   //Block Number of first block in blocksBuffer
  #ifndef NDEBUG
  uint32_t expectedBlockNum;  //Debug Build Only: To make sure the order of blocks is in successive order
  #endif
  
  bool Flush(const uint unitNum); 
};



#endif