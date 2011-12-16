#include "psx.h"

#include "../cdrom/SimpleFIFO.h"

static uint32 Command;

static uint8 QMatrix[2][64];
static uint32 QMIndex;

static uint8 QScale;

static int16 Coeff[64];
static uint32 CoeffIndex;
static uint32 DecodeWB;

static SimpleFIFO<uint16> OutBuffer(524288); //460800; //384 * 2 * 8);

static uint32 InCounter;
static bool DecodeEnd;

static const uint8 ZigZag[64] =
{
 0x00, 0x01, 0x08, 0x10, 0x09, 0x02, 0x03, 0x0A,
 0x11, 0x18, 0x20, 0x19, 0x12, 0x0B, 0x04, 0x05,
 0x0C, 0x13, 0x1A, 0x21, 0x28, 0x30, 0x29, 0x22,
 0x1B, 0x14, 0x0D, 0x06, 0x07, 0x0E, 0x15, 0x1C,
 0x23, 0x2A, 0x31, 0x38, 0x39, 0x32, 0x2B, 0x24,
 0x1D, 0x16, 0x0F, 0x17, 0x1E, 0x25, 0x2C, 0x33,
 0x3A, 0x3B, 0x34, 0x2D, 0x26, 0x1F, 0x27, 0x2E,
 0x35, 0x3C, 0x3D, 0x36, 0x2F, 0x37, 0x3E, 0x3F
};

static INLINE void WriteQTab(uint8 V)
{
 QMatrix[QMIndex >> 6][QMIndex & 0x3F] = V;
 QMIndex = (QMIndex + 1) & 0x7F;
}

static INLINE void WriteImageData(uint16 V)
{
 const uint32 qmw = (bool)(DecodeWB < 2);

  //printf("MDEC DMA SubWrite: %04x\n", V);

  if(!CoeffIndex)
  {
   if(V == 0xFE00)
   {
    QScale = 0;
    while(CoeffIndex < 64)
     Coeff[CoeffIndex++] = 0;
    //DecodeWB = 5;	// FIXME: Zero out blocks
    //DecodeEnd = true;
   }
   else
   {
    QScale = V >> 10;
    Coeff[0] = sign_10_to_s16(V & 0x3FF) * QMatrix[qmw][0];
    CoeffIndex++;
   }
  }
  else
  {
   if(V == 0xFE00)
   {
    while(CoeffIndex < 64)
     Coeff[CoeffIndex++] = 0;
   }
   else
   {
    uint32 rlcount = V >> 10;

    for(uint32 i = 0; i < rlcount && CoeffIndex < 64; i++)
    {
     Coeff[CoeffIndex] = 0;
     CoeffIndex++;
    }

    if(CoeffIndex < 64)
    {
     Coeff[CoeffIndex] = (sign_10_to_s16(V & 0x3FF) * QScale * QMatrix[qmw][CoeffIndex]) >> 4;	// Arithmetic right shift or division(negative differs)?
     CoeffIndex++;
    }
   }
  }

  if(CoeffIndex == 64)
  {
   CoeffIndex = 0;

   //printf("Block %d finished\n", DecodeWB);

   DecodeWB++;
   if(DecodeWB == 6)
   {
    //printf(" 16x16 IMAGE CHUNK FINISHED\n");
    DecodeWB = 0;
     uint16 tmp = rand();

    if(1)
    //if(!(Command & 0x8000000))
    {
     for(int i = 0; i < 384; i++)
     {
      if(OutBuffer.CanWrite())
       OutBuffer.WriteUnit(tmp);
     }
    }
    else
    {
     for(int i = 0; i < 256; i++)
     {
      OutBuffer.WriteUnit(tmp);
     }
    }
   }
  }
}

void MDEC_DMAWrite(uint32 V)
{
 if(Command == 0x60000000)
 {
  printf("MYSTERY0: %08x\n", V);
 }
 else if(Command == 0x40000001)
 {
  for(int i = 0; i < 4; i++)
  {
   WriteQTab((uint8)V);
   V >>= 8;
  }
 }
 else if((Command & 0xF5FF0000) == 0x30000000)
 {
  for(int vi = 0; vi < 2; vi++)
  {
   WriteImageData(V & 0xFFFF);
   V >>= 16;
  }
  if(InCounter > 0)
   InCounter--;
  else
   puts("InCounter trying to less than 0?");

 }
 else
 {
  printf("MYSTERY1: %08x\n", V);
 }
}

void MDEC_DMARead(uint32 &V)
{
 V = 0;

 if((Command & 0xF5FF0000) == 0x30000000 && OutBuffer.CanRead() >= 2)
 {
  V = OutBuffer.ReadUnit() | (OutBuffer.ReadUnit() << 16);
 }
 else
  V = rand();
}

void MDEC_Write(uint32 A, uint32 V)
{
 PSX_WARNING("[MDEC] Write: 0x%08x 0x%08x", A, V);
 if(A & 4)
 {
  if(V & 0x80000000) // Reset?
  {
   memset(QMatrix, 0, sizeof(QMatrix));
   QMIndex = 0;

   QScale = 0;

   memset(Coeff, 0, sizeof(Coeff));
   CoeffIndex = 0;
   DecodeWB = 0;

   OutBuffer.Flush();

   InCounter = 0;
   DecodeEnd = false;
  }
 }
 else
 {
  Command = V;

  if((Command & 0xF5FF0000) == 0x30000000)
   InCounter = V & 0xFFFF;
 }
}

uint32 MDEC_Read(uint32 A)
{
 uint32 ret = 0;

 if(A & 4)
 {
  ret = 0;
//  if(InCounter > 0)
//   ret |= 0x20000000;
 }
 else
  ret = Command;

 //PSX_WARNING("[MDEC] Read: 0x%08x 0x%08x", A, ret);

 return(ret);
}

