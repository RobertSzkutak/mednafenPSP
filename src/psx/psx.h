#ifndef __MDFN_PSX_PSX_H
#define __MDFN_PSX_PSX_H

#include <mednafen/mednafen.h>
#include <trio/trio.h>

#include "../cdrom/cdromif.h"
#include "../general.h"
#include "../FileWrapper.h"

#define PSX_WARNING(format, ...) { printf(format "\n", ## __VA_ARGS__); }
#define PSX_DBGINFO(format, ...) { /*printf(format "\n", ## __VA_ARGS__);*/ }

namespace MDFN_IEN_PSX
{
 typedef int32 pscpu_timestamp_t;

 pscpu_timestamp_t PSX_EventHandler(const pscpu_timestamp_t timestamp);

 void PSX_MemWrite8(const pscpu_timestamp_t timestamp, uint32 A, uint32 V);
 void PSX_MemWrite16(const pscpu_timestamp_t timestamp, uint32 A, uint32 V);
 void PSX_MemWrite24(const pscpu_timestamp_t timestamp, uint32 A, uint32 V);
 void PSX_MemWrite32(const pscpu_timestamp_t timestamp, uint32 A, uint32 V);

 uint8 PSX_MemRead8(const pscpu_timestamp_t timestamp, uint32 A);
 uint16 PSX_MemRead16(const pscpu_timestamp_t timestamp, uint32 A);
 uint32 PSX_MemRead24(const pscpu_timestamp_t timestamp, uint32 A);
 uint32 PSX_MemRead32(const pscpu_timestamp_t timestamp, uint32 A);

 uint8 PSX_MemPeek8(uint32 A);
 uint16 PSX_MemPeek16(uint32 A);
 uint32 PSX_MemPeek32(uint32 A);

 // Should write to WO-locations if possible
 #if 0
 void PSX_MemPoke8(uint32 A, uint8 V);
 void PSX_MemPoke16(uint32 A, uint16 V);
 void PSX_MemPoke32(uint32 A, uint32 V);
 #endif

 void PSX_RequestMLExit(void);
};
#include "dis.h"
#include "cpu.h"
#include "irq.h"
#include "timer.h"
#include "gpu.h"
#include "cdc.h"
#include "spu.h"
#include "dma.h"
//#include "sio.h"
#include "frontio.h"
#include "mdec.h"
#include "debug.h"

namespace MDFN_IEN_PSX
{
 extern PS_CPU *CPU;
 extern PS_GPU *GPU;
 extern PS_CDC *CDC;
 extern PS_SPU *SPU;
 extern uint8 *MainRAM;
};


#endif
