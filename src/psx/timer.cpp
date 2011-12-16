#include "psx.h"

/*
 Notes(some of it may be incomplete or wrong in subtle ways)

 Control bits:
	Lower 3 bits of mode, for timer1:
		0x1 = don't count during vblank
		0x3 = vblank going inactive triggers timer reset
		0x5 = vblank going inactive triggers timer reset, and only count within vblank.
		0x7 = Wait until vblank goes active then inactive, then start counting?
	For timer2:
		0x1 = timer stopped(TODO: confirm on real system)

	Target mode enabled		0x008
	IRQ enable			0x010
	--?Affects 0x400 status flag?-- 0x020
	IRQ evaluation auto-reset	0x040
	--unknown--			0x080
	Clock selection			0x100
	Divide by 8(timer 2 only?)	0x200

 Counter:
	Reset to 0 on writes to the mode/status register.

 Status flags:
	Unknown flag 		      0x0400
	Compare flag		      0x0800
		Cleared on mode/status read.
		Set when:	//ever Counter == 0(proooobably, need to investigate lower 3 bits in relation to this).
			

	Overflow/Carry flag	      0x1000
		Cleared on mode/status read.
		Set when counter overflows from 0xFFFF->0.

 Hidden flags:
	IRQ done
		Cleared on writes to the mode/status register, on writes to the count register, and apparently automatically when the counter
		increments if (Mode & 0x40) [Note: If target mode is enabled, and target is 0, IRQ done flag won't be automatically reset]

 There seems to be a brief period(edge condition?) where, if count to target is enabled, you can (sometimes?) read the target value in the count
 register before it's reset to 0.  I doubt any games rely on this, but who knows.  Maybe a PSX equivalent of the PC Engine "Battle Royale"? ;)

 When the counter == 0, the compare flag is set.  An IRQ will be generated if (Mode & 0x10), and the hidden IRQ done flag will be set.
*/

namespace MDFN_IEN_PSX
{

extern PS_GPU *GPU;

struct Timer
{
 uint32 Mode;
 int32 Counter;	// Only 16-bit, but 32-bit here for detecting counting past target.
 int32 Target;

 int32 Div8Counter;

 bool IRQDone;
};

static Timer Timers[3];
static pscpu_timestamp_t lastts;

void timer_moo(void)
{
// Timers[1].Counter = 0;
}

static void ClockTimer(int i, uint32 clocks)
{
 int32 before = Timers[i].Counter;
 int32 target = 0x10000;
 bool zero_tm = false;

 if(i == 0x2)
 {
  uint32 d8_clocks;

  Timers[i].Div8Counter += clocks;
  d8_clocks = Timers[i].Div8Counter >> 3;
  Timers[i].Div8Counter -= d8_clocks << 3;

  if(Timers[i].Mode & 0x200)	// Divide by 8, at least for timer 0x2
   clocks = d8_clocks;

  if(Timers[i].Mode & 1)
   clocks = 0;
 }

 if(Timers[i].Mode & 0x008)
  target = Timers[i].Target;

 if(target == 0 && Timers[i].Counter == 0)
  zero_tm = true;
 else
  Timers[i].Counter += clocks;

 if(clocks && (Timers[i].Mode & 0x40))
  Timers[i].IRQDone = false;

 if((before < target && Timers[i].Counter >= target) || zero_tm || Timers[i].Counter > 0xFFFF)
 {
  Timers[i].Mode |= 0x0800;

  if(Timers[i].Counter > 0xFFFF)
  {
   Timers[i].Counter -= 0x10000;
   Timers[i].Mode |= 0x1000;

   if(!target)
    Timers[i].Counter = 0;
  }

  if(target)
   Timers[i].Counter -= (Timers[i].Counter / target) * target;

  if((Timers[i].Mode & 0x10) && !Timers[i].IRQDone)
  {
   Timers[i].IRQDone = true;

   IRQ_Assert(IRQ_TIMER_0 + i, true);
   IRQ_Assert(IRQ_TIMER_0 + i, false);
  }

  if(Timers[i].Counter && (Timers[i].Mode & 0x40))
   Timers[i].IRQDone = false;
 }

}

void TIMER_AddDotClocks(uint32 count)
{
 if(Timers[0].Mode & 0x100)
  ClockTimer(0, count);
}

void TIMER_ClockHRetrace(void)
{
 if(Timers[1].Mode & 0x100)
  ClockTimer(1, 1);
}

void TIMER_Update(const pscpu_timestamp_t timestamp)
{
 int32 cpu_clocks = timestamp - lastts;

 for(int i = 0; i < 3; i++)
 {
  uint32 timer_clocks = cpu_clocks;

  if(Timers[i].Mode & 0x100)
   continue;

  ClockTimer(i, timer_clocks);
 }

 lastts = timestamp;
}

void TIMER_Write(const pscpu_timestamp_t timestamp, uint32 A, uint16 V)
{
 TIMER_Update(timestamp);

 int which = (A >> 4) & 0x3;

 assert(!(A & 3));

 PSX_DBGINFO("[TIMER] Write: %08x %04x", A, V);

 if(which >= 3)
  return;

 // TODO: See if the "Timers[which].Counter" part of the IRQ if() statements below is what a real PSX does.
 switch(A & 0xC)
 {
  case 0x0: Timers[which].IRQDone = false;
#if 1
	    if(Timers[which].Counter && (V & 0xFFFF) == 0)
	    {
	     Timers[which].Mode |= 0x0800;
	     if((Timers[which].Mode & 0x10) && !Timers[which].IRQDone)
	     {
	      Timers[which].IRQDone = true;
	      IRQ_Assert(IRQ_TIMER_0 + which, true);
	      IRQ_Assert(IRQ_TIMER_0 + which, false);
	     }
	    }
#endif
	    Timers[which].Counter = V & 0xFFFF;
	    break;

  case 0x4: Timers[which].Mode = (V & 0x3FF) | (Timers[which].Mode & 0x1C00);
	    Timers[which].IRQDone = false;
#if 1
	    if(Timers[which].Counter)
	    {
	     Timers[which].Mode |= 0x0800;
	     if((Timers[which].Mode & 0x10) && !Timers[which].IRQDone)
	     {
	      Timers[which].IRQDone = true;
	      IRQ_Assert(IRQ_TIMER_0 + which, true);
	      IRQ_Assert(IRQ_TIMER_0 + which, false);
	     }
	    }
	    Timers[which].Counter = 0;
#endif
	    break;

  case 0x8: Timers[which].Target = V & 0xFFFF;
	    break;

  default: assert(0);
 }

 // TIMER_Update(timestamp);
}

uint16 TIMER_Read(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint16 ret = 0;
 int which = (A >> 4) & 0x3;

 assert(!(A & 3));

 if(which >= 3)
  assert(0);

 TIMER_Update(timestamp);

 switch(A & 0xC)
 {
  case 0x0: ret = Timers[which].Counter;
	    break;

  case 0x4: ret = Timers[which].Mode;
	    Timers[which].Mode &= ~0x1800;
	    break;

  case 0x8: ret = Timers[which].Target;
	    break;

  default: assert(0);
 }

 return(ret);
}


void TIMER_ResetTS(void)
{
 lastts = 0;
}


void TIMER_Power(void)
{
 lastts = 0;

 memset(Timers, 0, sizeof(Timers));
}

uint32 TIMER_GetRegister(unsigned int which, char *special, const uint32 special_len)
{
 int tw = (which >> 4) & 0x3;
 uint32 ret = 0;

 switch(which & 0xF)
 {
  case TIMER_GSREG_COUNTER0:
	ret = Timers[tw].Counter;
	break;

  case TIMER_GSREG_MODE0:
	ret = Timers[tw].Mode;
	break;

  case TIMER_GSREG_TARGET0:
	ret = Timers[tw].Target;
	break;
 }

 return(ret);
}

void TIMER_SetRegister(unsigned int which, uint32 value)
{
 int tw = (which >> 4) & 0x3;

 switch(which & 0xF)
 {
  case TIMER_GSREG_COUNTER0:
	Timers[tw].Counter = value & 0xFFFF;
	break;

  case TIMER_GSREG_MODE0:
	Timers[tw].Mode = value & 0xFFFF;
	break;

  case TIMER_GSREG_TARGET0:
	Timers[tw].Target = value & 0xFFFF;
	break;
 }

}


}
