#include "psx.h"

namespace MDFN_IEN_PSX
{

static uint32 Asserted;
static uint32 Mask;
static uint32 Status;

static INLINE void Recalc(void)
{
 CPU->AssertIRQ(0, (bool)(Status & Mask));
}

void IRQ_Power(void)
{
 Asserted = 0;
 Status = 0;
 Mask = 0;

 Recalc();
}

void IRQ_Assert(int which, bool status)
{
 uint32 old_Asserted = Asserted;
 //PSX_WARNING("[IRQ] Assert: %d %d", which, status);

 Asserted &= ~(1 << which);

 if(status)
 {
  Asserted |= 1 << which;
  //Status |= 1 << which;
  Status |= (old_Asserted ^ Asserted) & Asserted;
 }

 Recalc();
}


void IRQ_Write(uint32 A, uint32 V)
{
 // FIXME if we ever have "accurate" bus emulation
 V <<= (A & 3) * 8;

 if(A & 4)
  Mask = V;
 else
 {
  Status &= V;
  //Status |= Asserted;
 }

 Recalc();
}


uint32 IRQ_Read(uint32 A)
{
 uint32 ret = 0;

 if(A & 4)
  ret = Mask;
 else
  ret = Status;

 // FIXME: Might want to move this out to psx.cpp eventually.
 ret |= 0x1F800000;
 ret >>= (A & 3) * 8;

 return(ret);
}


void IRQ_Reset(void)
{
 Asserted = 0;
 Status = 0; 
 Mask = 0;

 Recalc();
}


uint32 IRQ_GetRegister(unsigned int which, char *special, const uint32 special_len)
{
 uint32 ret = 0;

 switch(which)
 {
  case IRQ_GSREG_ASSERTED:
	ret = Asserted;
	break;

  case IRQ_GSREG_STATUS:
	ret = Status;
	break;

  case IRQ_GSREG_MASK:
	ret = Mask;
	break;
 }
 return(ret);
}

void IRQ_SetRegister(unsigned int which, uint32 value)
{
 switch(which)
 {
  case IRQ_GSREG_ASSERTED:
	Asserted = value;
	Recalc();
	break;

  case IRQ_GSREG_STATUS:
	Status = value;
	Recalc();
	break;

  case IRQ_GSREG_MASK:
	Mask = value;
	Recalc();
	break;
 }
}


}
