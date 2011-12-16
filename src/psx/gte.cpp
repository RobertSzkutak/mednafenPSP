
#ifndef PSXDEV_GTE_TESTING
#include "psx.h"
#include "gte.h"
#endif

/* Notes:

 AVSZ3/AVSZ4:
	OTZ is MAC0 >> 12
	OTZ overflow/underflow flag is set in an overflow condition even if MAC0 == 0.
	sf field bit has no effect?

 FLAG register:
	Bits present mask: 0xfffff000

	Checksum bit can't be directly set, it's apparently calculated like (bool)(FLAGS & 0x7f87e000)

	Instructions effectively clear it 0 at start. (todo: test "invalid" instructions)

 X/Y FIFO [3] register write pushes a copy down to [2]

*/

#ifndef PSXDEV_GTE_TESTING
namespace MDFN_IEN_PSX
{
#endif

// FIXME: Add alternate structure definitions for big-endian platforms.

typedef struct
{
 int16 MX[3][3];
 int16 dummy;
}  __attribute__((__packed__)) gtematrix;

typedef struct
{
 uint8 R;
 uint8 G;
 uint8 B;
 uint8 CD;
} gtergb;

typedef struct
{
 int16 X;
 int16 Y;
} gtexy;

int32 A(unsigned int which, int64 value);
int16 Lm_B(unsigned int which, int32 value, int lm);
uint8 Lm_C(unsigned int which, int32 value);

int32 Lm_D(int32 value);

int32 F(int64 value);

int32 Lm_G(unsigned int which, int32 value);
int32 Lm_H(int32 value);

void MAC_to_RGB_FIFO(void);
void MAC_to_IR(int lm);

void MultiplyMatrixByVector(const gtematrix *matrix, const int16 *v, const int32 *crv, uint32 sf, int lm);

static uint32 CR[32];
static uint32 FLAGS;	// Temporary for instruction execution, copied into CR[31] at end of instruction execution.

typedef union
{
 gtematrix All[4];
 int32 Raw[4][5];
 struct
 {
  gtematrix Rot;
  gtematrix Light;
  gtematrix Color;
  gtematrix Null;	// Might not be correct.
 };
} Matrices_t;

static Matrices_t Matrices;

static union
{
 int32 All[4][4];	// Really only [4][3], but [4] to ease address calculation.
  
 struct
 {
  int32 T[4];
  int32 B[4];
  int32 FC[4];
  int32 Null[4];
 };
} CRVectors;

static int32 OFX;
static int32 OFY;
static uint16 H;
static int16 DQA;
static int32 DQB;
 
static int16 ZSF3;
static int16 ZSF4;


// Begin DR
static int16 Vectors[3][4];
static gtergb RGB;
static uint16 OTZ;
static int16 IR0;
static int16 IR1;
static int16 IR2;
static int16 IR3;
static gtexy XY_FIFO[4];
static uint16 Z_FIFO[4];
static gtergb RGB_FIFO[3];
static int32 MAC[4];
static uint32 IRGB;
static uint32 LZCS;
static uint32 LZCR;

static uint32 Reg23;
// end DR

void PTransform(uint32 sf, int lm, unsigned int v);

int32 RTPS(uint32 instr);
int32 RTPT(uint32 instr);

int32 NCLIP(uint32 instr);

void NormColor(uint32 sf, int lm, uint32 v);
int32 NCS(uint32 instr);
int32 NCT(uint32 instr);


void NormColorColor(uint32 v, uint32 sf, int lm);
int32 NCCS(uint32 instr);
int32 NCCT(uint32 instr);

void NormColorDepthCue(uint32 v, uint32 sf, int lm);
int32 NCDS(uint32 instr);
int32 NCDT(uint32 instr);

int32 AVSZ3(uint32 instr);
int32 AVSZ4(uint32 instr);

int32 OP(uint32 instr);

int32 GPF(uint32 instr);
int32 GPL(uint32 instr);

void DepthCue(int mult_IR123, int RGB_from_FIFO, uint32 sf, int lm);
int32 DCPL(uint32 instr);
int32 DPCS(uint32 instr);
int32 DPCT(uint32 instr);
int32 INTPL(uint32 instr);

int32 SQR(uint32 instr);
int32 MVMVA(uint32 instr);

static INLINE uint8 Sat5(int16 cc)
{
 if(cc < 0)
  cc = 0;
 if(cc > 0x1F)
  cc = 0x1F;
 return(cc);
}



void GTE_Power(void)
{
 memset(CR, 0, sizeof(CR));
 //memset(DR, 0, sizeof(DR));

 memset(Matrices.All, 0, sizeof(Matrices.All));
 memset(CRVectors.All, 0, sizeof(CRVectors.All));
 OFX = 0;
 OFY = 0;
 H = 0;
 DQA = 0;
 DQB = 0;
 ZSF3 = 0;
 ZSF4 = 0;


 memset(Vectors, 0, sizeof(Vectors));
 memset(&RGB, 0, sizeof(RGB));
 OTZ = 0;
 IR0 = 0;
 IR1 = 0;
 IR2 = 0;
 IR3 = 0;

 memset(XY_FIFO, 0, sizeof(XY_FIFO));
 memset(Z_FIFO, 0, sizeof(Z_FIFO));
 memset(RGB_FIFO, 0, sizeof(RGB_FIFO));
 memset(MAC, 0, sizeof(MAC));
 IRGB = 0;
 LZCS = 0;
 LZCR = 0;

 Reg23 = 0;
}

void GTE_WriteCR(unsigned int which, uint32 value)
{
 static const uint32 mask_table[32] = {
	/* 0x00 */
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0000FFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,

	/* 0x08 */
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0000FFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,

	/* 0x10 */
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0000FFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,

	/* 0x18 */
	0xFFFFFFFF, 0xFFFFFFFF, 0x0000FFFF, 0x0000FFFF, 0xFFFFFFFF, 0x0000FFFF, 0x0000FFFF, 0xFFFFFFFF
 };

 //PSX_WARNING("[GTE] Write CR %d, 0x%08x", which, value);

 value &= mask_table[which];

 CR[which] = value | (CR[which] & ~mask_table[which]);

 if(which < 24)
 {
  int we = which >> 3;
  which &= 0x7;

  if(which >= 5)
   CRVectors.All[we][which - 5] = value;
  else
   Matrices.Raw[we][which] = value;
  return;
 }

 switch(which)
 {
  case 24:
	OFX = value;
	break;

  case 25:
	OFY = value;
	break;

  case 26:
	H = value;
	break;

  case 27:
	DQA = value;
	break;

  case 28:
	DQB = value;
	break;

  case 29:
	ZSF3 = value;
	break;

  case 30:
	ZSF4 = value;
	break;

  case 31:
	CR[31] = (value & 0x7ffff000) | ((value & 0x7f87e000) ? (1 << 31) : 0);
	break;
 }
}

uint32 GTE_ReadCR(unsigned int which)
{
 uint32 ret = 0;

 switch(which)
 {
  default:
	ret = CR[which];
	if(which == 4 || which == 12 || which == 20)
	 ret = (int16)ret;
	break;

  case 24:
	ret = OFX;
	break;

  case 25:
	ret = OFY;
	break;

  case 26:
	ret = (int16)H;
	break;

  case 27:
	ret = (int16)DQA;
	break;

  case 28:
	ret = DQB;
	break;

  case 29:
	ret = (int16)ZSF3;
	break;

  case 30:
	ret = (int16)ZSF4;
	break;

  case 31:
	ret = CR[31];
	break;
 }

 return(ret);
}

void GTE_WriteDR(unsigned int which, uint32 value)
{
 switch(which & 0x1F)
 {
  case 0:
	Vectors[0][0] = value;
	Vectors[0][1] = value >> 16;
	break;

  case 1:
	Vectors[0][2] = value;
	break;

  case 2:
	Vectors[1][0] = value;
	Vectors[1][1] = value >> 16;
	break;

  case 3:
	Vectors[1][2] = value;
	break;

  case 4:
	Vectors[2][0] = value;
	Vectors[2][1] = value >> 16;
	break;

  case 5:
	Vectors[2][2] = value;
	break;

  case 6:
	RGB.R = value >> 0;
	RGB.G = value >> 8;
	RGB.B = value >> 16;
	RGB.CD = value >> 24;
	break;

  case 7:
	OTZ = value;
	break;

  case 8:
	IR0 = value;
	break;

  case 9:
	IR1 = value;
	break;

  case 10:
	IR2 = value;
	break;

  case 11:
	IR3 = value;
	break;

  case 12:
	XY_FIFO[0].X = value;
	XY_FIFO[0].Y = value >> 16;
	break;

  case 13:
	XY_FIFO[1].X = value;
	XY_FIFO[1].Y = value >> 16;
	break;

  case 14:
	XY_FIFO[2].X = value;
	XY_FIFO[2].Y = value >> 16;
	break;

  case 15:
	XY_FIFO[3].X = value;
	XY_FIFO[3].Y = value >> 16;

	XY_FIFO[0] = XY_FIFO[1];
	XY_FIFO[1] = XY_FIFO[2];
	XY_FIFO[2] = XY_FIFO[3];
	break;

  case 16:
	Z_FIFO[0] = value;
	break;

  case 17:
	Z_FIFO[1] = value;
	break;

  case 18:
	Z_FIFO[2] = value;
	break;

  case 19:
	Z_FIFO[3] = value;
	break;

  case 20:
	RGB_FIFO[0].R = value;
	RGB_FIFO[0].G = value >> 8;
	RGB_FIFO[0].B = value >> 16;
	RGB_FIFO[0].CD = value >> 24;
	break;

  case 21:
	RGB_FIFO[1].R = value;
	RGB_FIFO[1].G = value >> 8;
	RGB_FIFO[1].B = value >> 16;
	RGB_FIFO[1].CD = value >> 24;
	break;

  case 22:
	RGB_FIFO[2].R = value;
	RGB_FIFO[2].G = value >> 8;
	RGB_FIFO[2].B = value >> 16;
	RGB_FIFO[2].CD = value >> 24;
	break;

  case 23:
	Reg23 = value;
	break;

  case 24:
	MAC[0] = value;
	break;

  case 25:
	MAC[1] = value;
	break;

  case 26:
	MAC[2] = value;
	break;

  case 27:
	MAC[3] = value;
	break;

  case 28:
	IRGB = value & 0x7FFF;
	IR1 = ((value >> 0) & 0x1F) << 7;
	IR2 = ((value >> 5) & 0x1F) << 7;
	IR3 = ((value >> 10) & 0x1F) << 7;
	break;

  case 29:	// Read-only
	break;

  case 30:
	// FIXME
	LZCS = value;
	{
	 uint32 test = value & 0x80000000;
	 LZCR = 0;

	 while((value & 0x80000000) == test && LZCR < 32)
	 {
	  LZCR++;
	  value <<= 1;
	 }
	}
	break;

  case 31:	// Read-only
	break;
 }
}

uint32 GTE_ReadDR(unsigned int which)
{
 uint32 ret = 0;

 switch(which & 0x1F)
 {
  case 0:
	ret = (uint16)Vectors[0][0] | ((uint16)Vectors[0][1] << 16);
	break;

  case 1:
	ret = (int16)Vectors[0][2];
	break;

  case 2:
	ret = (uint16)Vectors[1][0] | ((uint16)Vectors[1][1] << 16);
	break;

  case 3:
	ret = (int16)Vectors[1][2];
	break;

  case 4:
	ret = (uint16)Vectors[2][0] | ((uint16)Vectors[2][1] << 16);
	break;

  case 5:
	ret = (int16)Vectors[2][2];
	break;

  case 6:
	ret = RGB.R | (RGB.G << 8) | (RGB.B << 16) | (RGB.CD << 24);
	break;

  case 7:
	ret = (uint16)OTZ;
	break;

  case 8:
	ret = (int16)IR0;
	break;

  case 9:
	ret = (int16)IR1;
	break;

  case 10:
	ret = (int16)IR2;
	break;

  case 11:
	ret = (int16)IR3;
	break;

  case 12:
	ret = (uint16)XY_FIFO[0].X | ((uint16)XY_FIFO[0].Y << 16);
	break;

  case 13:
	ret = (uint16)XY_FIFO[1].X | ((uint16)XY_FIFO[1].Y << 16);
	break;

  case 14:
	ret = (uint16)XY_FIFO[2].X | ((uint16)XY_FIFO[2].Y << 16);
	break;

  case 15:
	ret = (uint16)XY_FIFO[3].X | ((uint16)XY_FIFO[3].Y << 16);
	break;

  case 16:
	ret = (uint16)Z_FIFO[0];
	break;

  case 17:
	ret = (uint16)Z_FIFO[1];
	break;

  case 18:
	ret = (uint16)Z_FIFO[2];
	break;

  case 19:
	ret = (uint16)Z_FIFO[3];
	break;

  case 20:
	ret = RGB_FIFO[0].R | (RGB_FIFO[0].G << 8) | (RGB_FIFO[0].B << 16) | (RGB_FIFO[0].CD << 24);
	break;

  case 21:
	ret = RGB_FIFO[1].R | (RGB_FIFO[1].G << 8) | (RGB_FIFO[1].B << 16) | (RGB_FIFO[1].CD << 24);
	break;

  case 22:
	ret = RGB_FIFO[2].R | (RGB_FIFO[2].G << 8) | (RGB_FIFO[2].B << 16) | (RGB_FIFO[2].CD << 24);
	break;

  case 23:
	ret = Reg23;
	break;

  case 24:
	ret = MAC[0];
	break;

  case 25:
	ret = MAC[1];
	break;

  case 26:
	ret = MAC[2];
	break;

  case 27:
	ret = MAC[3];
	break;

  case 28:
	ret = IRGB;
	break;

  case 29:
	ret = Sat5(IR1 >> 7) | (Sat5(IR2 >> 7) << 5) | (Sat5(IR3 >> 7) << 10);
	break;

  case 30:
	ret = LZCS;
	break;

  case 31:
	ret = LZCR;
	break;
 }
 return(ret);
}

INLINE int32 A(unsigned int which, int64 value)
{
 // Done for an issue with NCCS, at least.  See if this masking-out is applicable in all cases, and for other registers.
 //
 FLAGS &= ~(1 << (27 - which));
 FLAGS &= ~(1 << (30 - which));
 //
 //

 if(value < -2147483648LL)
 {
  // flag set here
  FLAGS |= 1 << (27 - which);
 }

 if(value > 2147483647LL)
 {
  // flag set here
  FLAGS |= 1 << (30 - which);
 }
 return(value);
}

INLINE int32 F(int64 value)
{
 if(value < -2147483648LL)
 {
  // flag set here
  FLAGS |= 1 << 15;
 }

 if(value > 2147483647LL)
 {
  // flag set here
  FLAGS |= 1 << 16;
 }
 return(value);
}


INLINE int16 Lm_B(unsigned int which, int32 value, int lm)
{
 int32 tmp = lm << 15;

 if(value < (-32768 + tmp))
 {
  // set flag here
  FLAGS |= 1 << (24 - which);
  value = -32768 + tmp;
 }

 if(value > 32767)
 {
  // Set flag here
  FLAGS |= 1 << (24 - which);
  value = 32767;
 }

 return(value);
}

INLINE uint8 Lm_C(unsigned int which, int32 value)
{
 if(value & ~0xFF)
 {
  // Set flag here
  FLAGS |= 1 << (21 - which);	// Tested with GPF

  if(value < 0)
   value = 0;

  if(value > 255)
   value = 255;
 }

 return(value);
}

INLINE int32 Lm_D(int32 value)
{
 // Not sure if we should have it as int64, or just chain on to and special case when the F flags are set.
 if(FLAGS & (1 << 15))
 {
  FLAGS |= 1 << 18;
  return(0);
 }

 if(FLAGS & (1 << 16))
 {
  FLAGS |= 1 << 18;
  return(0xFFFF);
 }

 if(value < 0)
 {
  // Set flag here
  value = 0;
  FLAGS |= 1 << 18;	// Tested with AVSZ3
 }
 else if(value > 65535)
 {
  // Set flag here.
  value = 65535;
  FLAGS |= 1 << 18;	// Tested with AVSZ3
 }

 return(value);
}

INLINE int32 Lm_G(unsigned int which, int32 value)
{
 if(value < -1024)
 {
  // Set flag here
  value = -1024;
  FLAGS |= 1 << (14 - which);
 }

 if(value > 1023)
 {
  // Set flag here.
  value = 1023;
  FLAGS |= 1 << (14 - which);
 }

 return(value);
}

INLINE int32 Lm_H(int32 value)
{
 if(value < 0)
 {
  // Set flag here
  value = 0;
  FLAGS |= 1 << 12;
 }

 if(value > 4096)	// Yes, six.
 {
  // Set flag here
  value = 4096;
  FLAGS |= 1 << 12;
 }

 return(value);
}

INLINE void MAC_to_RGB_FIFO(void)
{
 RGB_FIFO[0] = RGB_FIFO[1];
 RGB_FIFO[1] = RGB_FIFO[2];
 RGB_FIFO[2].R = Lm_C(0, MAC[1] >> 4);
 RGB_FIFO[2].G = Lm_C(1, MAC[2] >> 4);
 RGB_FIFO[2].B = Lm_C(2, MAC[3] >> 4);
 RGB_FIFO[2].CD = RGB.CD;
}


INLINE void MAC_to_IR(int lm)
{
 IR1 = Lm_B(0, MAC[1], lm);
 IR2 = Lm_B(1, MAC[2], lm);
 IR3 = Lm_B(2, MAC[3], lm);
}

// NOTES:
// << 12 for translation vector with MVMA when sf is 0.
// FIXME: far color vector is borked
INLINE void MultiplyMatrixByVector(const gtematrix *matrix, const int16 *v, const int32 *crv, uint32 sf, int lm)
{
#if 0
 MAC[1] = A(0, ((int64)crv[0] << 12) + (((int64)(matrix.MX[0][0] * v[0]) + (matrix.MX[0][1] * v[1]) + (matrix.MX[0][2] * v[2])) >> sf));
 MAC[2] = A(1, ((int64)crv[1] << 12) + (((int64)(matrix.MX[1][0] * v[0]) + (matrix.MX[1][1] * v[1]) + (matrix.MX[1][2] * v[2])) >> sf));
 MAC[3] = A(2, ((int64)crv[2] << 12) + (((int64)(matrix.MX[2][0] * v[0]) + (matrix.MX[2][1] * v[1]) + (matrix.MX[2][2] * v[2])) >> sf));
#endif

 assert(crv != CRVectors.FC);

#if 0
 if(0 && crv == CRVectors.FC)
 {
  MAC[1] = A(0, ((((int64)crv[0] << 8) + (int64)(matrix->MX[0][0] * v[0]) + (matrix->MX[0][1] * v[1]) + (matrix->MX[0][2] * v[2])) >> sf));
  MAC[2] = A(1, ((((int64)crv[1] << 8) + (int64)(matrix->MX[1][0] * v[0]) + (matrix->MX[1][1] * v[1]) + (matrix->MX[1][2] * v[2])) >> sf));
  MAC[3] = A(2, ((((int64)crv[2] << 8) + (int64)(matrix->MX[2][0] * v[0]) + (matrix->MX[2][1] * v[1]) + (matrix->MX[2][2] * v[2])) >> sf));
 }
 else
#endif
 {
  MAC[1] = A(0, ((((int64)crv[0] << 12) + (int64)(matrix->MX[0][0] * v[0]) + (matrix->MX[0][1] * v[1]) + (matrix->MX[0][2] * v[2])) >> sf));
  MAC[2] = A(1, ((((int64)crv[1] << 12) + (int64)(matrix->MX[1][0] * v[0]) + (matrix->MX[1][1] * v[1]) + (matrix->MX[1][2] * v[2])) >> sf));
  MAC[3] = A(2, ((((int64)crv[2] << 12) + (int64)(matrix->MX[2][0] * v[0]) + (matrix->MX[2][1] * v[1]) + (matrix->MX[2][2] * v[2])) >> sf));
 }

 MAC_to_IR(lm);
}


#define VAR_UNUSED __attribute__((unused))

#define DECODE_FIELDS							\
 const uint32 sf VAR_UNUSED = (instr & (1 << 19)) ? 12 : 0;		\
 const uint32 mx VAR_UNUSED = (instr >> 17) & 0x3;			\
 const uint32 v_i = (instr >> 15) & 0x3;				\
 const int32* cv VAR_UNUSED = CRVectors.All[(instr >> 13) & 0x3];	\
 const int lm VAR_UNUSED = (instr >> 10) & 1;			\
 int16 v[3];							\
 if(v_i == 3)							\
 {								\
  v[0] = IR1;							\
  v[1] = IR2;							\
  v[2] = IR3;							\
 }								\
 else								\
 {								\
  v[0] = Vectors[v_i][0];					\
  v[1] = Vectors[v_i][1];					\
  v[2] = Vectors[v_i][2];					\
 }
								

int32 SQR(uint32 instr)
{
 DECODE_FIELDS;

 // Typecast to int64 shouldn't be necessary here (might be able to remove call to A()...
 MAC[1] = A(0, ((IR1 * IR1) >> sf));
 MAC[2] = A(1, ((IR2 * IR2) >> sf));
 MAC[3] = A(2, ((IR3 * IR3) >> sf));

 MAC_to_IR(lm);

 return(5);
}


int32 MVMVA(uint32 instr)
{
 DECODE_FIELDS;

 MultiplyMatrixByVector(&Matrices.All[mx], v, cv, sf, lm);

 return(8);
}


INLINE void PTransform(uint32 sf, int lm, unsigned int v)
{
 int64 h_div_sz;

 MultiplyMatrixByVector(&Matrices.Rot, Vectors[v], CRVectors.T, sf, lm);

 Z_FIFO[0] = Z_FIFO[1];
 Z_FIFO[1] = Z_FIFO[2];
 Z_FIFO[2] = Z_FIFO[3];
 Z_FIFO[3] = Lm_D(MAC[3]);

 // FIXME: division
 if(H < (Z_FIFO[3] * 2))
  h_div_sz = (((int64)H << 16)) / Z_FIFO[3];
 else
 {
  h_div_sz = 0x1FFFF;
  FLAGS |= 1 << 17;
 }

 MAC[0] = F(((int64)OFX + IR1 * h_div_sz + 32768) >> 16);
 XY_FIFO[3].X = Lm_G(0, MAC[0]);

 MAC[0] = F(((int64)OFY + IR2 * h_div_sz + 32768) >> 16);
 XY_FIFO[3].Y = Lm_G(1, MAC[0]);

 XY_FIFO[0] = XY_FIFO[1];
 XY_FIFO[1] = XY_FIFO[2];
 XY_FIFO[2] = XY_FIFO[3];

// MAC[0] = F((DQB + ((DQA * h_div_sz + 32768) >> 16)) >> 8 );
// IR0 = Lm_H(MAC[0]);

// printf("MOO: %d %d %16lld\n", DQB, DQA, (long long)h_div_sz);

 MAC[0] = F((int64)DQB + DQA * h_div_sz);
 IR0 = Lm_H(MAC[0] >> sf);
}

int32 RTPS(uint32 instr)
{
 DECODE_FIELDS;

 PTransform(sf, lm, 0);

 return(15);
}

int32 RTPT(uint32 instr)
{
 DECODE_FIELDS;
 int i;

 for(i = 0; i < 3; i++)
  PTransform(sf, lm, i);

 return(23);
}

INLINE void NormColor(uint32 sf, int lm, uint32 v)
{
 int16 tmp_vector[3];

 MultiplyMatrixByVector(&Matrices.Light, Vectors[v], CRVectors.Null, sf, lm);

 tmp_vector[0] = IR1; tmp_vector[1] = IR2; tmp_vector[2] = IR3;
 MultiplyMatrixByVector(&Matrices.Color, tmp_vector, CRVectors.B, sf, lm);

 MAC_to_RGB_FIFO();
}

int32 NCS(uint32 instr)
{
 DECODE_FIELDS;

 NormColor(sf, lm, 0);

 return(14);
}

int32 NCT(uint32 instr)
{
 DECODE_FIELDS;
 int i;

 for(i = 0; i < 3; i++)
  NormColor(sf, lm, i);

 return(30);
}

INLINE void NormColorColor(uint32 v, uint32 sf, int lm)
{
 int16 tmp_vector[3];

 MultiplyMatrixByVector(&Matrices.Light, Vectors[v], CRVectors.Null, sf, lm);

 tmp_vector[0] = IR1; tmp_vector[1] = IR2; tmp_vector[2] = IR3;
 MultiplyMatrixByVector(&Matrices.Color, tmp_vector, CRVectors.B, sf, lm);

 MAC[1] = A(0, ((RGB.R << 4) * IR1) >> sf);
 MAC[2] = A(1, ((RGB.G << 4) * IR2) >> sf);
 MAC[3] = A(2, ((RGB.B << 4) * IR3) >> sf);

 MAC_to_IR(lm);

 MAC_to_RGB_FIFO();
}

int32 NCCS(uint32 instr)
{
 DECODE_FIELDS;

 NormColorColor(0, sf, lm);
 return(17);
}


int32 NCCT(uint32 instr)
{
 int i;
 DECODE_FIELDS;

 for(i = 0; i < 3; i++)
  NormColorColor(i, sf, lm);

 return(39);
}

INLINE void DepthCue(int mult_IR123, int RGB_from_FIFO, uint32 sf, int lm)
{
 int32 R_temp, G_temp, B_temp;

 //assert(sf);

 if(RGB_from_FIFO)
 {
  R_temp = RGB_FIFO[0].R << 4;
  G_temp = RGB_FIFO[0].G << 4;
  B_temp = RGB_FIFO[0].B << 4;
 }
 else
 {
  R_temp = RGB.R << 4;
  G_temp = RGB.G << 4;
  B_temp = RGB.B << 4;
 }

 if(mult_IR123)
 {
  // Note: Do not put A() here!  We might just want to change this to local temporaries.
  MAC[1] = (((int64)CRVectors.FC[0] << 12) - R_temp * IR1) >> sf;
  MAC[2] = (((int64)CRVectors.FC[1] << 12) - G_temp * IR2) >> sf;
  MAC[3] = (((int64)CRVectors.FC[2] << 12) - B_temp * IR3) >> sf;

  MAC[1] = A(0, (R_temp * IR1 + IR0 * Lm_B(0, MAC[1], FALSE)) >> sf);
  MAC[2] = A(1, (G_temp * IR2 + IR0 * Lm_B(1, MAC[2], FALSE)) >> sf);
  MAC[3] = A(2, (B_temp * IR3 + IR0 * Lm_B(2, MAC[3], FALSE)) >> sf);

/*
  MAC[1] = A(0, (int64)((R_temp * IR1) >> sf) + IR0 * Lm_B(0, (int64)CRVectors.FC[0] - ((R_temp * IR1) >> sf), FALSE) );
  MAC[2] = A(1, (int64)((G_temp * IR2) >> sf) + IR0 * Lm_B(1, (int64)CRVectors.FC[1] - ((G_temp * IR2) >> sf), FALSE) );
  MAC[3] = A(2, (int64)((B_temp * IR3) >> sf) + IR0 * Lm_B(2, (int64)CRVectors.FC[2] - ((B_temp * IR3) >> sf), FALSE) );
*/
 }
 else
 {
  // Note: Do not put A() here!  We might just want to change this to local temporaries.
  MAC[1] = (((int64)CRVectors.FC[0] << 12) - (R_temp << 12)) >> sf;
  MAC[2] = (((int64)CRVectors.FC[1] << 12) - (G_temp << 12)) >> sf;
  MAC[3] = (((int64)CRVectors.FC[2] << 12) - (B_temp << 12)) >> sf;

  MAC[1] = A(0, (((int64)R_temp << 12) + IR0 * Lm_B(0, MAC[1], FALSE)) >> sf);
  MAC[2] = A(1, (((int64)G_temp << 12) + IR0 * Lm_B(1, MAC[2], FALSE)) >> sf);
  MAC[3] = A(2, (((int64)B_temp << 12) + IR0 * Lm_B(2, MAC[3], FALSE)) >> sf);

/*
  MAC[1] = A(0, (int64)R_temp + ((IR0 * Lm_B(0, (int64)CRVectors.FC[0] - R_temp, FALSE)) >> sf) );
  MAC[2] = A(1, (int64)G_temp + ((IR0 * Lm_B(1, (int64)CRVectors.FC[1] - G_temp, FALSE)) >> sf) );
  MAC[3] = A(2, (int64)B_temp + ((IR0 * Lm_B(2, (int64)CRVectors.FC[2] - B_temp, FALSE)) >> sf) );
*/
 }

 MAC_to_IR(lm);

 MAC_to_RGB_FIFO();
}


int32 DCPL(uint32 instr)
{
 DECODE_FIELDS;

 DepthCue(TRUE, FALSE, sf, lm);

 return(8);
}


int32 DPCS(uint32 instr)
{
 DECODE_FIELDS;

 DepthCue(FALSE, FALSE, sf, lm);

 return(8);
}

int32 DPCT(uint32 instr)
{
 int i;
 DECODE_FIELDS;

 for(i = 0; i < 3; i++)
 {
  DepthCue(FALSE, TRUE, sf, lm);
 }

 return(17);
}

// SF field *is* used(tested), but it's a bit...weird.
int32 INTPL(uint32 instr)
{
 DECODE_FIELDS;

 //if(sf)
 //{
 // MAC[1] = A(0, (int64)IR1 + ((IR0 * Lm_B(0, (int64)CRVectors.FC[0] - IR1, FALSE)) >> 12) );
 // MAC[2] = A(1, (int64)IR2 + ((IR0 * Lm_B(1, (int64)CRVectors.FC[1] - IR2, FALSE)) >> 12) );
 // MAC[3] = A(2, (int64)IR3 + ((IR0 * Lm_B(2, (int64)CRVectors.FC[2] - IR3, FALSE)) >> 12) );
 //}
 //else
 //{

 // Note: Do not put A() here!  We might just want to change this to local temporaries.
 MAC[1] = (((int64)CRVectors.FC[0] << 12) - (IR1 << 12)) >> sf;
 MAC[2] = (((int64)CRVectors.FC[1] << 12) - (IR2 << 12)) >> sf;
 MAC[3] = (((int64)CRVectors.FC[2] << 12) - (IR3 << 12)) >> sf;

 MAC[1] = A(0, (((int64)IR1 << 12) + IR0 * Lm_B(0, MAC[1], FALSE)) >> sf);
 MAC[2] = A(1, (((int64)IR2 << 12) + IR0 * Lm_B(1, MAC[2], FALSE)) >> sf);
 MAC[3] = A(2, (((int64)IR3 << 12) + IR0 * Lm_B(2, MAC[3], FALSE)) >> sf);
// }

 MAC_to_IR(lm);

 MAC_to_RGB_FIFO();

 return(8);
}


INLINE void NormColorDepthCue(uint32 v, uint32 sf, int lm)
{
 int16 tmp_vector[3];

 MultiplyMatrixByVector(&Matrices.Light, Vectors[v], CRVectors.Null, sf, lm);

 tmp_vector[0] = IR1; tmp_vector[1] = IR2; tmp_vector[2] = IR3;
 MultiplyMatrixByVector(&Matrices.Color, tmp_vector, CRVectors.B, sf, lm);

 DepthCue(TRUE, FALSE, sf, lm);
}

int32 NCDS(uint32 instr)
{
 DECODE_FIELDS;

 NormColorDepthCue(0, sf, lm);

 return(19);
}

int32 NCDT(uint32 instr)
{
 int i;
 DECODE_FIELDS;

 for(i = 0; i < 3; i++)
  NormColorDepthCue(i, sf, lm);

 return(44);
}

int32 NCLIP(uint32 instr)
{
 DECODE_FIELDS;

 MAC[0] = F( (int64)(XY_FIFO[0].X * (XY_FIFO[1].Y - XY_FIFO[2].Y)) + (XY_FIFO[1].X * (XY_FIFO[2].Y - XY_FIFO[0].Y)) + (XY_FIFO[2].X * (XY_FIFO[0].Y - XY_FIFO[1].Y))
	  );

 return(8);
}

// tested, SF field doesn't matter?
int32 AVSZ3(uint32 instr)
{
 DECODE_FIELDS;

 MAC[0] = F(((int64)ZSF3 * (Z_FIFO[1] + Z_FIFO[2] + Z_FIFO[3])));

 OTZ = Lm_D(MAC[0] >> 12);

 return(5);
}

int32 AVSZ4(uint32 instr)
{
 DECODE_FIELDS;

 MAC[0] = F(((int64)ZSF4 * (Z_FIFO[0] + Z_FIFO[1] + Z_FIFO[2] + Z_FIFO[3])));

 OTZ = Lm_D(MAC[0] >> 12);

 return(5);
}


int32 OP(uint32 instr)
{
 DECODE_FIELDS;

 MAC[1] = A(0, ((int64)(Matrices.Rot.MX[1][1] * IR3) - (Matrices.Rot.MX[2][2] * IR2)) >> sf);
 MAC[2] = A(1, ((int64)(Matrices.Rot.MX[2][2] * IR1) - (Matrices.Rot.MX[0][0] * IR3)) >> sf);
 MAC[3] = A(2, ((int64)(Matrices.Rot.MX[0][0] * IR2) - (Matrices.Rot.MX[1][1] * IR1)) >> sf);

 MAC_to_IR(lm);

 return(6);
}

int32 GPF(uint32 instr)
{
 DECODE_FIELDS;

 MAC[1] = A(0, ((IR0 * IR1) >> sf));
 MAC[2] = A(1, ((IR0 * IR2) >> sf));
 MAC[3] = A(2, ((IR0 * IR3) >> sf));

 MAC_to_IR(lm);

 MAC_to_RGB_FIFO();

 return(5);
}

int32 GPL(uint32 instr)
{
 DECODE_FIELDS;


 MAC[1] = A(0, ((int64)MAC[1] + ((IR0 * IR1) >> sf)));
 MAC[2] = A(1, ((int64)MAC[2] + ((IR0 * IR2) >> sf)));
 MAC[3] = A(2, ((int64)MAC[3] + ((IR0 * IR3) >> sf)));

 MAC_to_IR(lm);

 MAC_to_RGB_FIFO();

 return(5);
}


/*
 

24 23 22 21 20|19|18 17|16 15|14 13|12 11|10| 9  8| 7  6| 5  4  3  2  1  0
              |sf| mx  |  v  |  cv |-----|lm|-----------|


 sf = shift 12

 mx = matrix selection

 v = source vector

 cv = add vector(translation/back/far color(bugged)/none)

 lm = limit negative results to 0

*/

int32 GTE_Instruction(uint32 instr)
{
 int32 ret = 1;

 //PSX_WARNING("[GTE] Instruction 0x%08x", instr);

 FLAGS = 0;

 switch(instr & ((0x1F << 20) | 0x3F))
 {
  default: //PSX_WARNING("[GTE] Unknown instruction: 0x%08x, 0x%08x", instr & ~(0x7F << 25), instr & ((0x1F << 20) | 0x3F));
	   break;

  case 0x0100001:
	ret = RTPS(instr);
	break;

  case 0x0200030:
	ret = RTPT(instr);
	break;

  case 0x0400012:
	ret = MVMVA(instr);
	break;

  case 0x0600029:
	ret = DCPL(instr);
	break;

  case 0x0700010:	// RR
	ret = DPCS(instr);
	break;

  case 0x0900011:	// RR
	ret = INTPL(instr);
	break;

  case 0x0A00028:
	ret = SQR(instr);
	break;

  case 0x0c0001e:
	ret = NCS(instr);
	break;

  case 0x0d00020:
	ret = NCT(instr);
	break;

  case 0x0e00013:
	ret = NCDS(instr);
	break;

  case 0x0f00016:
	ret = NCDT(instr);
	break;

  case 0x0F0002A:
	ret = DPCT(instr);
	break;

  case 0x100001b:
	ret = NCCS(instr);
	break;

  case 0x110003f:
	ret = NCCT(instr);
	break;

  case 0x1400006:
	ret = NCLIP(instr);
	break;

  case 0x150002d:
	ret = AVSZ3(instr);
	break;

  case 0x160002E:
	ret = AVSZ4(instr);
	break;

  case 0x170000C:
	ret = OP(instr);
	break;

  case 0x190003D:
	ret = GPF(instr);
	break;

  case 0x1A0003E:
	ret = GPL(instr);
	break;
	
 }

 if(FLAGS & 0x7f87e000)
  FLAGS |= 1 << 31;

 CR[31] = FLAGS;

 //if(timestamp < ts_done)
 // ret = ts_done - timestamp;

 // Execute instruction here, and set ts_done
 // ts_done = timestamp + InstrMap[instr]();

 return(ret - 1);
}

#ifndef PSXDEV_GTE_TESTING
}
#endif
