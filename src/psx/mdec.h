#ifndef __MDFN_PSX_MDEC_H
#define __MDFN_PSX_MDEC_H

void MDEC_DMAWrite(uint32 V);

void MDEC_DMARead(uint32 &V);

void MDEC_Write(uint32 A, uint32 V);
uint32 MDEC_Read(uint32 A);


#endif
