#ifndef _FPU_H
#define _FPU_H

#include <stdint.h>

void fadd(uint8_t *paramBuffer);
void fsub(uint8_t *paramBuffer);
void fmul(uint8_t *paramBuffer);
void fdiv(uint8_t *paramBuffer);
void fsin(uint8_t *paramBuffer);
void fcos(uint8_t *paramBuffer);
void ftan(uint8_t *paramBuffer);
void fatn(uint8_t *paramBuffer);
void flog(uint8_t *paramBuffer);
void fexp(uint8_t *paramBuffer);
void fsqr(uint8_t *paramBuffer);
void fout(uint8_t *paramBuffer);

#endif