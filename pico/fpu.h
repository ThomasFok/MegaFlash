#ifndef _FPU_H
#define _FPU_H

#include <stdint.h>

void fadd(uint8_t *dataBuffer);
void fsub(uint8_t *dataBuffer);
void fmul(uint8_t *dataBuffer);
void fdiv(uint8_t *dataBuffer);
void fsin(uint8_t *dataBuffer);
void fcos(uint8_t *dataBuffer);
void ftan(uint8_t *dataBuffer);
void fatn(uint8_t *dataBuffer);
void flog(uint8_t *dataBuffer);
void fexp(uint8_t *dataBuffer);
void fsqr(uint8_t *dataBuffer);
void fout(uint8_t *dataBuffer);

#endif