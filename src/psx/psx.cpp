#include "psx.h"
#include "../mempatcher.h"
#include "../PSFLoader.h"
#include "../player.h"
#include "../cputest/cputest.h"

namespace MDFN_IEN_PSX
{

class PSF1Loader : public PSFLoader
{
 public:

 PSF1Loader(MDFNFILE *fp);
 virtual ~PSF1Loader();

 virtual void HandleEXE(const uint8 *data, uint32 len, bool ignore_pcsp = false);

 PSFTags tags;
};

static PSF1Loader *psf_loader = NULL;

static pscpu_timestamp_t next_timestamp;

PS_CPU *CPU = NULL;
PS_SPU *SPU = NULL;
PS_GPU *GPU = NULL;
PS_CDC *CDC = NULL;
FrontIO *FIO = NULL;

void *MainRAM_Alloc = NULL;
void *BIOSROM_Alloc = NULL;
void *PIOMem_Alloc = NULL;

uint8 *MainRAM;
uint8 *BIOSROM;
uint8 *PIOMem;

static uint32 TextMem_Start;
static std::vector<uint8> TextMem;

static uint8 ScratchRAM[1024];


template<typename T, bool IsWrite, bool Access24, bool Peek> static INLINE void MemRW(const pscpu_timestamp_t timestamp, uint32 A, uint32 &V)
{
 static const uint32 mask[8] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
				 0x7FFFFFFF, 0x1FFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

 //if(IsWrite)
 // V = (T)V;

 if(!Peek)
 {
  #if 0
  if(IsWrite)
   printf("Write%d: %08x(orig=%08x), %08x\n", (int)(sizeof(T) * 8), A & mask[A >> 29], A, V);
  else
   printf("Read%d: %08x(orig=%08x)\n", (int)(sizeof(T) * 8), A & mask[A >> 29], A);
  #endif
 }

 A &= mask[A >> 29];

 //if(A == 0xa0 && IsWrite)
 // DBG_Break();

 if(A < 0x00800000)
 //if(A <= 0x1FFFFF)
 {
  //assert(A <= 0x1FFFFF);
  if(Access24)
  {
   if(IsWrite)
    MDFN_en24lsb(&MainRAM[A & 0x1FFFFF], V);
   else
    V = MDFN_de24lsb(&MainRAM[A & 0x1FFFFF]);
  }
  else
  {
   if(IsWrite)
    *(T*)&MainRAM[A & 0x1FFFFF] = V;
   else
    V = *(T*)&MainRAM[A & 0x1FFFFF];
  }

  return;
 }

 if(A >= 0x1F800000 && A <= 0x1F8003FF)
 {
  if(Access24)
  {
   if(IsWrite)
    MDFN_en24lsb(&ScratchRAM[A & 0x3FF], V);
   else
    V = MDFN_de24lsb(&ScratchRAM[A & 0x3FF]);
  }
  else
  {
   if(IsWrite)
    *(T*)&ScratchRAM[A & 0x3FF] = V;
   else
    V = *(T*)&ScratchRAM[A & 0x3FF];
  }
  return;
 }

 if(A >= 0x1FC00000 && A <= 0x1FC7FFFF)
 {
  if(!IsWrite)
  {
   if(Access24)
    V = MDFN_de24lsb(&BIOSROM[A & 0x7FFFF]);
   else
    V = *(T*)&BIOSROM[A & 0x7FFFF];
  }

  return;
 }

 if(A >= 0x1F801000 && A <= 0x1F802FFF && !Peek)	// Hardware register region. (TODO: Implement proper peek suppor)
 {
  //if(IsWrite)
  // printf("HW Write%d: %08x %08x\n", (unsigned int)(sizeof(T)*8), (unsigned int)A, (unsigned int)V);
  //else
  // printf("HW Read%d: %08x\n", (unsigned int)(sizeof(T)*8), (unsigned int)A);

  if(A >= 0x1F801C00 && A <= 0x1F801DFF)	// SPU
  {
   if(sizeof(T) == 4 && !Access24)
   {
    PSX_WARNING("[SPU] 32-bit access to %08x at time %d", A, timestamp);

    if(IsWrite)
    {
     SPU->Write(timestamp, A | 0, V);
     SPU->Write(timestamp, A | 2, V >> 16);
    }
    else
    {
     V = SPU->Read(timestamp, A) | (SPU->Read(timestamp, A | 2) << 16);
    }
   }
   else
   {
    if(IsWrite)
     SPU->Write(timestamp, A & ~1, V);
    else
     V = SPU->Read(timestamp, A & ~1);
   }
   return;
  }		// End SPU

  if(A >= 0x1f801800 && A <= 0x1f80180F)
  {
   if(IsWrite)
    CDC->Write(timestamp, A & 0x3, V);
   else
    V = CDC->Read(timestamp, A & 0x3);

   return;
  }

  if(A >= 0x1F801810 && A <= 0x1F801817)
  {
   if(IsWrite)
    GPU->Write(timestamp, A, V);
   else
    V = GPU->Read(timestamp, A);

   return;
  }

  if(A >= 0x1F801820 && A <= 0x1F801827)
  {
   if(IsWrite)
    MDEC_Write(A, V);
   else
    V = MDEC_Read(A);

   return;
  }

  if(A >= 0x1F801070 && A <= 0x1F801077)	// IRQ
  {
   if(IsWrite)
    IRQ_Write(A, V);
   else
    V = IRQ_Read(A);
   return;
  }

  if(A >= 0x1F801080 && A <= 0x1F8010FF) 	// DMA
  {
   if(IsWrite)
    DMA_Write(timestamp, A, V);
   else
    V = DMA_Read(timestamp, A);

   return;
  }

  if(A >= 0x1F801100 && A <= 0x1F80112F)	// Root counters
  {
   if(IsWrite)
    TIMER_Write(timestamp, A, V);
   else
    V = TIMER_Read(timestamp, A);

   return;
  }

  if(A >= 0x1F801040 && A <= 0x1F80104F)
  {
   if(IsWrite)
    FIO->Write(timestamp, A, V);
   else
    V = FIO->Read(timestamp, A);
   return;
  }

 }


 if(A >= 0x1F000000 && A <= 0x1F7FFFFF)
 {
  if(!IsWrite)
  {
   //if((A & 0x7FFFFF) <= 0x84)
   // PSX_WARNING("[PIO] Read%d from %08x at time %d", (int)(sizeof(T) * 8), A, timestamp);

   V = 0;

   if((A & 0x7FFFFF) < 65536)
    V = *(T*)&PIOMem[A & 0x7FFFFF];
   else if((A & 0x7FFFFF) < (65536 + TextMem.size()))
    V = *(T*)&TextMem[(A & 0x7FFFFF) - 65536];
  }
  return;
 }

 if(!Peek)
 {
  if(IsWrite)
  {
   if(A != 0x1f801018 && A != 0x1f801020)
   PSX_WARNING("[MEM] Unknown write%d to %08x at time %d, =%08x(%d)", (int)(sizeof(T) * 8), A, timestamp, V, V);
  }
  else
  {
   V = 0;
   PSX_WARNING("[MEM] Unknown read%d from %08x at time %d", (int)(sizeof(T) * 8), A, timestamp);
  }
 }
 else
  V = 0;

}

void PSX_MemWrite8(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 MemRW<uint8, true, false, false>(timestamp, A, V);
}

void PSX_MemWrite16(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 MemRW<uint16, true, false, false>(timestamp, A, V);
}

void PSX_MemWrite24(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 //assert(0);
 MemRW<uint32, true, true, false>(timestamp, A, V);
}

void PSX_MemWrite32(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 MemRW<uint32, true, false, false>(timestamp, A, V);
}

uint8 PSX_MemRead8(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint32 V;

 MemRW<uint8, false, false, false>(timestamp, A, V);

 return(V);
}

uint16 PSX_MemRead16(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint32 V;

 MemRW<uint16, false, false, false>(timestamp, A, V);

 return(V);
}

uint32 PSX_MemRead24(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint32 V;

 //assert(0);
 MemRW<uint32, false, true, false>(timestamp, A, V);

 return(V);
}

uint32 PSX_MemRead32(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint32 V;

 MemRW<uint32, false, false, false>(timestamp, A, V);

 return(V);
}


uint8 PSX_MemPeek8(uint32 A)
{
 uint32 V;

 MemRW<uint8, false, false, true>(0, A, V);

 return(V);
}

uint16 PSX_MemPeek16(uint32 A)
{
 uint32 V;

 MemRW<uint16, false, false, true>(0, A, V);

 return(V);
}

uint32 PSX_MemPeek32(uint32 A)
{
 uint32 V;

 MemRW<uint32, false, false, true>(0, A, V);

 return(V);
}

static INLINE void UpdateAll(const pscpu_timestamp_t timestamp)
{
 GPU->Update(timestamp);
 CDC->Update(timestamp);
 SPU->Update(timestamp);

 TIMER_Update(timestamp);

 DMA_Update(timestamp);

 FIO->Update(timestamp);
}

pscpu_timestamp_t PSX_EventHandler(const pscpu_timestamp_t timestamp)
{
 UpdateAll(timestamp);

 while(next_timestamp <= timestamp)
  next_timestamp += 256;

 return(next_timestamp);
}

void PSX_Power(void)
{
 memset(MainRAM, 0, 2048 * 1024);
 memset(ScratchRAM, 0, 1024);

 TIMER_Power();

 DMA_Power();
 IRQ_Power();

 FIO->Power();

 CDC->Power();
 GPU->Power();
 SPU->Power();

 CPU->Power();

 next_timestamp = 256;
 CPU->SetEventNT(256);
}

void PSX_RequestMLExit(void)
{
 CPU->Exit();
}

}

using namespace MDFN_IEN_PSX;


static void Emulate(EmulateSpecStruct *espec)
{
 const bool skip_orig = espec->skip;
 pscpu_timestamp_t timestamp = 0;

 if(psf_loader)
  espec->skip = true;

 MDFNMP_ApplyPeriodicCheats();


 espec->MasterCycles = 0;
 espec->SoundBufSize = 0;


 FIO->UpdateInput();
 GPU->StartFrame(espec);
 SPU->StartFrame(espec->SoundRate, MDFN_GetSettingUI("psx.spu.resamp_quality"));

 next_timestamp = 256;
 CPU->SetEventNT(256);

 timestamp = CPU->Run(timestamp);

 //printf("Timestamp: %d\n", timestamp);

 assert(timestamp);

 UpdateAll(timestamp);

 espec->SoundBufSize = SPU->EndFrame(espec->SoundBuf);

 CDC->ResetTS();
 TIMER_ResetTS();
 DMA_ResetTS();
 GPU->ResetTS();
 FIO->ResetTS();

 espec->MasterCycles = timestamp;

 if(psf_loader)
 {
  espec->skip = skip_orig;

  if(!espec->skip)
  {
   espec->LineWidths[0].w = ~0;
   Player_Draw(espec->surface, &espec->DisplayRect, 0, espec->SoundBuf, espec->SoundBufSize);
  }
 }
}

static bool TestMagic(const char *name, MDFNFILE *fp)
{
 if(PSFLoader::TestMagic(0x01, fp))
  return(true);

 if(fp->size < 0x800)
  return(false);

 if(memcmp(fp->data, "PS-X EXE", 8))
  return(false);

 return(true);
}

static bool TestMagicCD(void)
{
 uint8 buf[2048];

 if(CDIF_ReadSector(buf, 4, 1) != 0x2)
  return(false);

 if(strncmp((char *)buf + 10, "Licensed  by", strlen("Licensed  by")))
  return(false);

 //if(strncmp((char *)buf + 32, "Sony", 4))
 // return(false);

 //for(int i = 0; i < 2048; i++)
 // printf("%d, %02x %c\n", i, buf[i], buf[i]);
 //exit(1);

 return(true);
}

static bool InitCommon(void)
{
 // TODO, maybe.
 //#ifdef ARCH_X86
 //if(!(cputest_
 //#endif

 CPU = new PS_CPU();
 SPU = new PS_SPU();
 GPU = new PS_GPU();
 CDC = new PS_CDC();
 FIO = new FrontIO();

 MainRAM_Alloc = calloc(1, CPU->GetFastMapAllocSize(2048 * 1024));
 BIOSROM_Alloc = calloc(1, CPU->GetFastMapAllocSize(512 * 1024));
 PIOMem_Alloc = calloc(1, CPU->GetFastMapAllocSize(65536));

 MainRAM = (uint8 *)MainRAM_Alloc + CPU->SetupFastMapTrampoline(MainRAM_Alloc, 2048 * 1024);
 BIOSROM = (uint8 *)BIOSROM_Alloc + CPU->SetupFastMapTrampoline(BIOSROM_Alloc, 512 * 1024);
 PIOMem = (uint8 *)PIOMem_Alloc + CPU->SetupFastMapTrampoline(PIOMem_Alloc, 65536);

 CPU->SetFastMap(MainRAM, 0x00000000, 2048 * 1024);
 CPU->SetFastMap(MainRAM, 0x80000000, 2048 * 1024);
 CPU->SetFastMap(MainRAM, 0xA0000000, 2048 * 1024);

 CPU->SetFastMap(BIOSROM, 0x1FC00000, 512 * 1024);
 CPU->SetFastMap(BIOSROM, 0x9FC00000, 512 * 1024);
 CPU->SetFastMap(BIOSROM, 0xBFC00000, 512 * 1024);

 CPU->SetFastMap(PIOMem, 0x1F000000, 65536);
 CPU->SetFastMap(PIOMem, 0x9F000000, 65536);
 CPU->SetFastMap(PIOMem, 0xBF000000, 65536);


 MDFNMP_Init(1024, ((uint64)1 << 29) / 1024);
 MDFNMP_AddRAM(2048 * 1024, 0x00000000, MainRAM);
 MDFNMP_AddRAM(1024, 0x1F800000, ScratchRAM);

 try
 {
  std::string biospath = MDFN_MakeFName(MDFNMKF_FIRMWARE, 0, MDFN_GetSettingS("psx.bios").c_str());

  FileWrapper BIOSFile(biospath.c_str(), FileWrapper::MODE_READ);

  BIOSFile.read(BIOSROM, 512 * 1024);
 }
 catch(std::exception &e)
 {
  MDFN_PrintError("%s", e.what());
  return(false);
 }

 #ifdef WANT_DEBUGGER
 DBG_Init();
 #endif

 PSX_Power();

 return(true);
}

static void LoadEXE(const uint8 *data, const uint32 size, bool ignore_pcsp = false)
{
 uint32 PC;
 uint32 SP;
 uint32 TextStart;
 uint32 TextSize;

 if(size < 0x800)
  throw(MDFN_Error(0, "PS-EXE is too small."));

 PC = MDFN_de32lsb(&data[0x10]);
 SP = MDFN_de32lsb(&data[0x30]);
 TextStart = MDFN_de32lsb(&data[0x18]);
 TextSize = MDFN_de32lsb(&data[0x1C]);

 printf("PC=0x%08x\nTextStart=0x%08x\nTextSize=0x%08x\nSP=0x%08x\n", PC, TextStart, TextSize, SP);

 TextStart &= 0x1FFFFF;

 if(TextSize > 2048 * 1024)
 {
  throw(MDFN_Error(0, "Text section too large"));
 }

 if(TextSize > (size - 0x800))
  throw(MDFN_Error(0, "Text section recorded size is larger than data available in file.  Needed=0x%08x, Available=0x%08x", TextSize, size - 0x800));

 if(!TextMem.size())
 {
  TextMem_Start = TextStart;
  TextMem.resize(TextSize);
 }

 if(TextStart < TextMem_Start)
 {
  uint32 old_size = TextMem.size();

  printf("RESIZE: 0x%08x\n", TextMem_Start - TextStart);

  TextMem.resize(old_size + TextMem_Start - TextStart);
  memmove(&TextMem[TextMem_Start - TextStart], &TextMem[0], old_size);

  TextMem_Start = TextStart;
 }

 if(TextMem.size() < (TextStart - TextMem_Start + TextSize))
  TextMem.resize(TextStart - TextMem_Start + TextSize);

 memcpy(&TextMem[TextStart - TextMem_Start], data + 0x800, TextSize);


 //
 //
 //

 // BIOS patch
 MDFN_en32lsb(BIOSROM + 0x6990, (3 << 26) | ((0xBF001000 >> 2) & ((1 << 26) - 1)));

 uint8 *po;

 po = &PIOMem[0x0800];

 MDFN_en32lsb(po, (0x0 << 26) | (31 << 21) | (0x8 << 0));	// JR
 po += 4;
 MDFN_en32lsb(po, 0);	// NOP(kinda)
 po += 4;

 po = &PIOMem[0x1000];
 // Load source address into r8
 uint32 sa = 0x9F000000 + 65536;
 MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16) | (sa >> 16));	// LUI
 po += 4;
 MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (8 << 16) | (sa & 0xFFFF)); 	// ORI
 po += 4;

 // Load dest address into r9
 MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem_Start >> 16));	// LUI
 po += 4;
 MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (9 << 16) | (TextMem_Start & 0xFFFF)); 	// ORI
 po += 4;

 // Load size into r10
 MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem.size() >> 16));	// LUI
 po += 4;
 MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (10 << 16) | (TextMem.size() & 0xFFFF)); 	// ORI
 po += 4;

 //
 // Loop begin
 //
 
 MDFN_en32lsb(po, (0x24 << 26) | (8 << 21) | (1 << 16));	// LBU to r1
 po += 4;
 MDFN_en32lsb(po, 0); po += 4;			      	        // NOP

 MDFN_en32lsb(po, (0x28 << 26) | (9 << 21) | (1 << 16));	// SB from r1
 po += 4;
 MDFN_en32lsb(po, 0); po += 4;			      	        // NOP

 MDFN_en32lsb(po, (0x08 << 26) | (10 << 21) | (10 << 16) | 0xFFFF);	// Decrement size
 po += 4;

 MDFN_en32lsb(po, (0x08 << 26) | (8 << 21) | (8 << 16) | 0x0001);	// Increment source addr
 po += 4;

 MDFN_en32lsb(po, (0x08 << 26) | (9 << 21) | (9 << 16) | 0x0001);	// Increment dest addr
 po += 4;

 MDFN_en32lsb(po, (0x05 << 26) | (0 << 21) | (10 << 16) | (-8 & 0xFFFF));
 po += 4;
 MDFN_en32lsb(po, 0); po += 4;			      	        // NOP

 //
 // Loop end
 //

 // Load SP into r29
 if(ignore_pcsp)
 {
  po += 16;
 }
 else
 {
  printf("MEOWPC: %08x\n", PC);
  MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (SP >> 16));	// LUI
  po += 4;
  MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (29 << 16) | (SP & 0xFFFF)); 	// ORI
  po += 4;

  // Load PC into r2
  MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | ((PC >> 16) | 0x8000));      // LUI
  po += 4;
  MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (2 << 16) | (PC & 0xFFFF));   // ORI
  po += 4;
 }

 // Jump to r2
 MDFN_en32lsb(po, (0x0 << 26) | (2 << 21) | (0x8 << 0));	// JR
 po += 4;
 MDFN_en32lsb(po, 0);	// NOP(kinda)
 po += 4;

}

PSF1Loader::PSF1Loader(MDFNFILE *fp)
{
 tags = Load(0x01, 2033664, fp);
}

PSF1Loader::~PSF1Loader()
{

}

void PSF1Loader::HandleEXE(const uint8 *data, uint32 size, bool ignore_pcsp)
{
 LoadEXE(data, size, ignore_pcsp);
}

static int Load(const char *name, MDFNFILE *fp)
{
 if(!TestMagic(name, fp))
  return(0);

 if(!InitCommon())
  return(0);


 TextMem.resize(0);

 if(PSFLoader::TestMagic(0x01, fp))
 {
  psf_loader = new PSF1Loader(fp);

  std::vector<std::string> SongNames;

  SongNames.push_back(psf_loader->tags.GetTag("title"));

  Player_Init(1, psf_loader->tags.GetTag("game"), psf_loader->tags.GetTag("artist"), psf_loader->tags.GetTag("copyright"), SongNames);
 }
 else
  LoadEXE(fp->data, fp->size);

 return(1);
}

static int LoadCD(void)
{
 int ret = InitCommon();

 // TODO: fastboot setting
 //MDFN_en32lsb(BIOSROM + 0x6990, 0);

 return(ret);
}

static void CloseGame(void)
{
 TextMem.resize(0);

 if(psf_loader)
 {
  delete psf_loader;
  psf_loader = NULL;
 }

 if(CDC)
 {
  delete CDC;
  CDC = NULL;
 }

 if(SPU)
 {
  delete SPU;
  SPU = NULL;
 }

 if(GPU)
 {
  delete GPU;
  GPU = NULL;
 }

 if(CPU)
 {
  delete CPU;
  CPU = NULL;
 }

 if(FIO)
 {
  delete FIO;
  FIO = NULL;
 }

 if(MainRAM_Alloc)
 {
  free(MainRAM_Alloc);
  MainRAM_Alloc = NULL;
 }

 if(BIOSROM_Alloc)
 {
  free(BIOSROM_Alloc);
  BIOSROM_Alloc = NULL;
 }

 if(PIOMem_Alloc)
 {
  free(PIOMem_Alloc);
  PIOMem_Alloc = NULL;
 }
}

static void SetInput(int port, const char *type, void *ptr)
{
 FIO->SetInput(port, type, ptr);
}

static int StateAction(StateMem *sm, int load, int data_only)
{
 //SFORMAT StateRegs[] =
 //{
 return(0);
}


static void DoSimpleCommand(int cmd)
{
 switch(cmd)
 {
  case MDFN_MSC_RESET: PSX_Power(); break;
  case MDFN_MSC_POWER: PSX_Power(); break;
 }
}


static const FileExtensionSpecStruct KnownExtensions[] =
{
 { ".psx", gettext_noop("PS-X Executable") },
 { ".exe", gettext_noop("PS-X Executable") },
 { NULL, NULL }
};

static MDFNSetting PSXSettings[] =
{
 { "psx.bios", MDFNSF_EMU_STATE, gettext_noop("Path to the ROM BIOS"), NULL, MDFNST_STRING, "scph5501.bin" },
 { "psx.spu.resamp_quality", MDFNSF_NOFLAGS, gettext_noop("SPU output resampler quality."),
	gettext_noop("0 is lowest quality and CPU usage, 10 is highest quality and CPU usage.  The resampler that this setting refers to is used for converting from 44.1KHz to the sampling rate of the host audio device Mednafen is using.  Changing Mednafen's output rate, via the \"sound.rate\" setting, to \"44100\" will bypass the resampler, which will decrease CPU usage by Mednafen, and can increase or decrease audio quality, depending on various operating system and hardware factors."), MDFNST_UINT, "5", "0", "10" },
 { NULL },
};


MDFNGI EmulatedPSX =
{
 "psx",
 "Sony PlayStation",
 KnownExtensions,
 MODPRIO_INTERNAL_HIGH,
 #ifdef WANT_DEBUGGER
 &PSX_DBGInfo,
 #else
 NULL,
 #endif
 &FIO_InputInfo,
 Load,
 TestMagic,
 LoadCD,
 TestMagicCD,
 CloseGame,
 NULL,	//ToggleLayer,
 NULL,	//"Background Scroll\0Foreground Scroll\0Sprites\0",
 NULL,
 NULL,
 NULL,
 StateAction,
 Emulate,
 SetInput,
 DoSimpleCommand,
 PSXSettings,
 MDFN_MASTERCLOCK_FIXED(33868800),
 0,

 true, // Multires possible?

 2720, //2560,  // lcm_width
 480,   // lcm_height
 NULL,  // Dummy

 306, //288,   // Nominal width
 240,   // Nominal height

 1024,   // Framebuffer width
 480,   // Framebuffer height

 2,     // Number of output sound channels

};
