#include "psx.h"

// FIXME: 0-length block count?


/* Notes:

 Channel 6:
	DMA hangs if D28 is 0?
	D1 did not have an apparent effect.

*/

namespace MDFN_IEN_PSX
{


static uint8 LoopHistory[1024 * 2048 / 8 / 4];
static uint32 LoopHistoryClear[1024 * 2048 / 4];
static uint32 LoopHistoryCI;

static uint32 DMAControl;
static uint32 DMAIntControl;
static uint8 DMAIntStatus;
static bool IRQOut;

struct Channel
{
 uint32 BaseAddr;
 uint32 BlockControl;
 uint32 ChanControl;

 uint32 CurAddr;
 uint32 Counter;

 uint32 NextAddr; // Channel 2 linked list only

 int32 SimuFinishDelay;
};

static Channel DMACH[7];
static pscpu_timestamp_t lastts;


static const char *PrettyChannelNames[7] = { "MDEC IN", "MDEC OUT", "GPU", "CDC", "SPU", "PIO", "OTC" };

static INLINE void RecalcIRQOut(void)
{
 bool irqo;

 irqo = (bool)(DMAIntStatus & ((DMAIntControl >> 16) & 0x7F));
 irqo &= (DMAIntControl >> 23) & 1;

 // I think it's logical OR, not XOR/invert.  Still kind of weird, maybe it actually does something more complicated?
 //irqo ^= (DMAIntControl >> 15) & 1;
 irqo |= (DMAIntControl >> 15) & 1;

 IRQOut = irqo;
 IRQ_Assert(IRQ_DMA, irqo);
}

void DMA_ResetTS(void)
{
 lastts = 0;
}

void DMA_Power(void)
{
 // These memset()'s would be better in a DMA_Init() instead.
 memset(LoopHistory, 0, sizeof(LoopHistory));
 memset(LoopHistoryClear, 0, sizeof(LoopHistoryClear));
 LoopHistoryCI = 0;

 lastts = 0;

 memset(DMACH, 0, sizeof(DMACH));

 DMAControl = 0;
 DMAIntControl = 0;
 DMAIntStatus = 0;
 RecalcIRQOut();
}

bool DMA_GPUWriteActive(void)
{
 const int ch = 2;
 uint32 dc = (DMAControl >> (ch * 4)) & 0xF;

 if((dc & 0x8) && (DMACH[ch].ChanControl & (1 << 24)))
  if(DMACH[ch].ChanControl & 0x1)
   return(true);

 return(false);
}

static INLINE void RunChannel(const pscpu_timestamp_t timestamp, int ch)
{
 int32 simu_time = 0;

   switch(ch)
   {
    default: assert(0);
		break;

    case 0x0:	// MDEC in
	simu_time = DMACH[ch].Counter;
	while(DMACH[ch].Counter)
	{
	 MDEC_DMAWrite(*(uint32 *)&MainRAM[DMACH[ch].CurAddr]);

	 DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	 DMACH[ch].Counter--;
	}
	break;

  
    case 0x1:	// MDEC out
	simu_time = DMACH[ch].Counter;
	while(DMACH[ch].Counter)
	{
	 uint32 data;

	 MDEC_DMARead(data);

	 *(uint32 *)&MainRAM[DMACH[ch].CurAddr] = data;

	 DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	 DMACH[ch].Counter--;
	}
	break;

    case 0x3: // CDC
	simu_time = DMACH[ch].Counter;
	while(DMACH[ch].Counter)
	{
	 uint32 data;

	 CDC->DMARead(data);

	 *(uint32 *)&MainRAM[DMACH[ch].CurAddr] = data;
	 DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	 DMACH[ch].Counter--;
	}
	break;


    case 0x2:
	if(DMACH[ch].ChanControl & (1 << 10))	// Linked list
	{
	 bool Finished = false;

	 assert(DMACH[ch].ChanControl & 0x1);

	 LoopHistoryCI = 0;

	 while(!Finished)
	 {
	  if(!DMACH[ch].Counter)
	  {
	   if(DMACH[ch].NextAddr & 0x800000)
	   {
	    Finished = true;
	    break;
	   }

	   DMACH[ch].CurAddr = DMACH[ch].NextAddr & 0x1FFFFC;

	   if( LoopHistory[DMACH[ch].CurAddr >> 5] & (1 << ((DMACH[ch].CurAddr >> 2) & 0x7)) )
           {
            Finished = true;
            break;
           }

	   assert(LoopHistoryCI < (1024 * 2048 / 4));

	   LoopHistory[DMACH[ch].CurAddr >> 5] |= (1 << ((DMACH[ch].CurAddr >> 2) & 0x7));
	   LoopHistoryClear[LoopHistoryCI] = DMACH[ch].CurAddr >> 5;
	   LoopHistoryCI++;

	   simu_time++;
	   uint32 header = *(uint32 *)&MainRAM[DMACH[ch].CurAddr];

	   DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;

	   DMACH[ch].Counter = header >> 24;
	   DMACH[ch].NextAddr = header & 0xFFFFFF;

	   continue;
	  }

	  simu_time++;
	  GPU->WriteDMA(*(uint32 *)&MainRAM[DMACH[ch].CurAddr]);
          DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	  DMACH[ch].Counter--;
	 }

	 for(int i = 0; i < LoopHistoryCI; i++)
         {
	  LoopHistory[LoopHistoryClear[i]] = 0;
         }
	}
	else
	{
	 simu_time = DMACH[ch].Counter;
	 if(DMACH[ch].ChanControl & 0x1) // Write to GPU
	 {
	  while(DMACH[ch].Counter)
	  {
	   GPU->WriteDMA(*(uint32 *)&MainRAM[DMACH[ch].CurAddr]);
	   DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	   DMACH[ch].Counter--;
	  }
	 }
	 else // Read from GPU
	 {
	  while(DMACH[ch].Counter)
	  {
	   *(uint32 *)&MainRAM[DMACH[ch].CurAddr] = GPU->Read(timestamp, 0);
	   DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	   DMACH[ch].Counter--;
	  }
	 }
	}
	break;

    case 0x4:
	 simu_time = DMACH[ch].Counter;
	 if(DMACH[ch].ChanControl & 0x1) // Write to SPU
	 {
	  while(DMACH[ch].Counter)
	  {
	   SPU->WriteDMA(*(uint32 *)&MainRAM[DMACH[ch].CurAddr]);
	   DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	   DMACH[ch].Counter--;
	  }
	 }
	 else // Read from SPU
	 {
	  while(DMACH[ch].Counter)
	  {
	   *(uint32 *)&MainRAM[DMACH[ch].CurAddr] = SPU->ReadDMA();

	   DMACH[ch].CurAddr = (DMACH[ch].CurAddr + 4) & 0x1FFFFF;
	   DMACH[ch].Counter--;
	  }
	 }
	 break;

    case 0x6:
	simu_time = DMACH[ch].Counter;
	while(DMACH[ch].Counter > 1)
	{
	 //printf("OT: %08x %08x\n", DMACH[ch].CurAddr, (DMACH[ch].CurAddr - 4) & 0x1FFFFF);
	 *(uint32 *)&MainRAM[DMACH[ch].CurAddr] = (DMACH[ch].CurAddr - 4) & 0x1FFFFF;

	 DMACH[ch].CurAddr = (DMACH[ch].CurAddr - 4) & 0x1FFFFF;
	 DMACH[ch].Counter--;
	}

	if(DMACH[ch].Counter)
	{
	 *(uint32 *)&MainRAM[DMACH[ch].CurAddr] = 0xFFFFFF;

	 DMACH[ch].CurAddr = (DMACH[ch].CurAddr - 4) & 0x1FFFFF;
	 DMACH[ch].Counter--;
	}
	break;
   }

#if 1
   if(!simu_time)	// For 0-length DMA.
   {
    puts("HMM");
    simu_time = 1;
   }

   DMACH[ch].SimuFinishDelay = simu_time;
#else
   DMACH[ch].ChanControl &= ~(1 << 24);

   if(DMAIntControl & (1 << (16 + ch)))
   {
    DMAIntStatus |= 1 << ch;
    RecalcIRQOut();
   }
#endif

   //if(!(DMACH[ch].ChanControl & (1 << 24)))
   //{
   // PSX_DBGINFO("[DMA] DMA End for Channel %d(%s) --- scanline=%d", ch, PrettyChannelNames[ch], GPU->GetScanlineNum());
   //}
}

void DMA_Update(const pscpu_timestamp_t timestamp)
{
 int32 clocks = timestamp - lastts;
 lastts = timestamp;

 for(int ch = 0; ch < 7; ch++)
 {
  if(DMACH[ch].SimuFinishDelay > 0)
  {
   DMACH[ch].SimuFinishDelay -= clocks;

   if(DMACH[ch].SimuFinishDelay <= 0)
   {
    DMACH[ch].SimuFinishDelay = 0;
    DMACH[ch].ChanControl &= ~(1 << 24);

    if(DMAIntControl & (1 << (16 + ch)))
    {
     DMAIntStatus |= 1 << ch;
     RecalcIRQOut();
    }
   }
  }

 }

 //MDFN_DispMessage("%d %d", DMACH[0].Counter, DMACH[1].Counter);
}

void DMA_Write(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 int ch = (A & 0x7F) >> 4;

 PSX_WARNING("[DMA] Write: %08x %08x, DMAIntStatus=%08x", A, V, DMAIntStatus);

 // FIXME if we ever have "accurate" bus emulation
 V <<= (A & 3) * 8;

 DMA_Update(timestamp);

 if(ch == 7)
 {
  switch(A & 0xC)
  {
   case 0x0: DMAControl = V;
	     break;

   case 0x4: DMAIntControl = V & 0x00ff803f;
	     DMAIntStatus &= ~(V >> 24);
	     //DMAIntStatus &= (V >> 16);
     	     RecalcIRQOut();
	     break;

   default: PSX_WARNING("[DMA] Unknown write: %08x %08x", A, V);
		assert(0);
	    break;
  }
  return;
 }
 switch(A & 0xC)
 {
  case 0x0: DMACH[ch].BaseAddr = V & 0xFFFFFF;
	    break;

  case 0x4: DMACH[ch].BlockControl = V;
	    break;

  case 0xC:
  case 0x8: 
	   {
	    uint32 OldCC = DMACH[ch].ChanControl;

	    if(ch == 6)
	     DMACH[ch].ChanControl = V & 0x51000002; 	// Not 100% sure, but close.
	    else
	     DMACH[ch].ChanControl = V & 0x71770703;

	    if((OldCC & (1 << 24)) && !(V & (1 << 24)))
	    {
	     PSX_WARNING("[DMA] Forced stop for channel %d", ch);
	     DMACH[ch].SimuFinishDelay = 0;
	    }

	    if(!(OldCC & (1 << 24)) && (V & (1 << 24)))
	    {
	     DMACH[ch].CurAddr = DMACH[ch].NextAddr = DMACH[ch].BaseAddr & 0x1FFFFC;

 	     PSX_DBGINFO("[DMA] Start DMA for Channel %d(%s) --- MADR=0x%08x, BCR=0x%08x, CHCR=0x%08x --- scanline=%d", ch, PrettyChannelNames[ch], DMACH[ch].BaseAddr, DMACH[ch].BlockControl, V, GPU->GetScanlineNum());

	     if(ch == 6)
	      DMACH[ch].Counter = DMACH[ch].BlockControl;
	     else if(ch == 3)
	     {
	      DMACH[ch].Counter = (DMACH[ch].BlockControl & 0xFFFF);
	     }
	     else if(ch == 2 && (V & (1 << 10)))
	     {
	      DMACH[ch].CurAddr = 0xFFFFFF;
	      DMACH[ch].Counter = 0;
	     }
	     else
	      DMACH[ch].Counter = (DMACH[ch].BlockControl >> 16) * (DMACH[ch].BlockControl & 0xFFFF);

	     RunChannel(timestamp, ch);
	    }
	   }
	   break;
 }

}

uint32 DMA_Read(const pscpu_timestamp_t timestamp, uint32 A)
{
 int ch = (A & 0x7F) >> 4;
 uint32 ret = 0;

 //printf("DMA Read: %08x\n", A);
 
 //assert(!(A & 3));

 if(ch == 7)
 {
  switch(A & 0xC)
  {
   default: PSX_WARNING("[DMA] Unknown read: %08x", A);
		assert(0);
	    break;

   case 0x0: ret = DMAControl;
	     break;

   case 0x4: ret = DMAIntControl | (DMAIntStatus << 24) | (IRQOut << 31);
	     break;
  }
 }
 else switch(A & 0xC)
 {
  case 0x0: ret = DMACH[ch].BaseAddr;
  	    break;

  case 0x4: ret = DMACH[ch].BlockControl;
	    break;

  case 0xC:
  case 0x8: ret = DMACH[ch].ChanControl;
            break;

 }

 ret >>= (A & 3) * 8;

 //PSX_WARNING("[DMA] Read: %08x %08x", A, ret);

 return(ret);
}



}
