/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* This resampler has only been designed with NES CPU frequencies(NTSC and PAL) as the input rate, and output rates of
   22050-48000 in mind, up to 1024 coefficient multiply-accumulates per output sample.
*/

// Don't set these higher than 3, the accumulation variables will overflow if you do.
// If you want to use this resampler as a more general-purpose downsampler(with input and output rates closer together), you will probably want
// to set these to 0.
#define FIR_TABLE_EXTRA_BITS  	3
#define FIR_TABLE_EXTRA_BITS_S	"3"

#include "../mednafen.h"
#include <math.h>
#include "filter.h"
#include "../cputest/cputest.h"

#if defined(ARCH_POWERPC_ALTIVEC) && defined(HAVE_ALTIVEC_H)
 #include <altivec.h>
#endif

static void kaiser_window( double* io, int count, double beta )
{
        int const accuracy = 16; //12;

        double* end = io + count;

        double beta2    = beta * beta * (double) -0.25;
        double to_fract = beta2 / ((double) count * count);
        double i        = 0;
        double rescale = 0; // Doesn't need an initializer, to shut up gcc

        for ( ; io < end; ++io, i += 1 )
        {
                double x = i * i * to_fract - beta2;
                double u = x;
                double k = x + 1;

                double n = 2;
                do
                {
                        u *= x / (n * n);
                        n += 1;
                        k += u;
                }
                while ( k <= u * (1 << accuracy) );

                if ( !i )
                        rescale = 1 / k; // otherwise values get large

                *io *= k * rescale;
        }
}

static void gen_sinc( double* out, int size, double cutoff, double kaiser )
{
	assert( size % 2 == 0 ); // size must be enev
 
	int const half_size = size / 2;
	double* const mid = &out [half_size];
 
	// Generate right half of sinc
	for ( int i = 0; i < half_size; i++ )
	{
		double angle = (i * 2 + 1) * (M_PI / 2);
		mid [i] = sin( angle * cutoff ) / angle;
	}
 
	kaiser_window( mid, half_size, kaiser );
 
	// Mirror for left half
	for ( int i = 0; i < half_size; i++ )
		out [i] = mid [half_size - 1 - i];
}
 
static void normalize( double* io, int size, double gain = 1.0 )
{
	double sum = 0;
	for ( int i = 0; i < size; i++ )
		sum += io [i];

	double scale = gain / sum;
	for ( int i = 0; i < size; i++ )
		io [i] *= scale;
}


static INLINE void DoMAC(int16 *wave, int16 *coeffs, int32 count, int32 *accum_output)
{
 int32 acc0[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
 int32 acc1[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
 int c;

 for(c = 0; c < (count >> 1); c += 8)
 {
  acc0[0] += ((int32)wave[c + 0] * coeffs[c + 0]);
  acc0[1] += ((int32)wave[c + 1] * coeffs[c + 1]);
  acc0[2] += ((int32)wave[c + 2] * coeffs[c + 2]);
  acc0[3] += ((int32)wave[c + 3] * coeffs[c + 3]);
  acc0[4] += ((int32)wave[c + 4] * coeffs[c + 4]);
  acc0[5] += ((int32)wave[c + 5] * coeffs[c + 5]);
  acc0[6] += ((int32)wave[c + 6] * coeffs[c + 6]);
  acc0[7] += ((int32)wave[c + 7] * coeffs[c + 7]);
 }

 acc0[0] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc0[1] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc0[2] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc0[3] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc0[4] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc0[5] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc0[6] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc0[7] >>= FIR_TABLE_EXTRA_BITS + 1;


 for(; c < count; c += 8)
 {
  acc1[0] += ((int32)wave[c + 0] * coeffs[c + 0]);
  acc1[1] += ((int32)wave[c + 1] * coeffs[c + 1]);
  acc1[2] += ((int32)wave[c + 2] * coeffs[c + 2]);
  acc1[3] += ((int32)wave[c + 3] * coeffs[c + 3]);
  acc1[4] += ((int32)wave[c + 4] * coeffs[c + 4]);
  acc1[5] += ((int32)wave[c + 5] * coeffs[c + 5]);
  acc1[6] += ((int32)wave[c + 6] * coeffs[c + 6]);
  acc1[7] += ((int32)wave[c + 7] * coeffs[c + 7]);
 }

 acc1[0] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc1[1] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc1[2] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc1[3] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc1[4] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc1[5] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc1[6] >>= FIR_TABLE_EXTRA_BITS + 1;
 acc1[7] >>= FIR_TABLE_EXTRA_BITS + 1;

 *accum_output = (acc0[0] + acc0[1] + acc0[2] + acc0[3] + acc0[4] + acc0[5] + acc0[6] + acc0[7] +
		  acc1[0] + acc1[1] + acc1[2] + acc1[3] + acc1[4] + acc1[5] + acc1[6] + acc1[7]) >> 15;
}

#ifdef ARCH_X86

#ifdef __x86_64__
#define X86_REGC "r"
#define X86_REGAT ""
#else
#define X86_REGC "e"
#define X86_REGAT "l"
#endif

static INLINE void DoMAC_MMX(int16 *wave, int16 *coeffs, int32 count, int32 *accum_output)
{
 // Multiplies 16 coefficients at a time.
 int dummy;

/*
 MMX register usage:
	mm0: Temporary sample load and multiply register
	mm2: Temporary sample load and multiply register

	mm1: accumulator, 2 32-bit signed values
	mm3: accumulator, 2 32-bit signed values

	mm4: accumulator, 2 32-bit signed values
	mm5: accumulator, 2 32-bit signed values

	mm6: Temporary sample load and multiply register, temporary summation register
	mm7: Temporary sample load and multiply register
	
*/
 asm volatile(
"pxor %%mm1, %%mm1\n\t"
"pxor %%mm3, %%mm3\n\t"
"pxor %%mm4, %%mm4\n\t"
"pxor %%mm5, %%mm5\n\t"
"MMX_Loop:\n\t"

"movq (%%"X86_REGC"di), %%mm0\n\t"
"pmaddwd (%%"X86_REGC"si), %%mm0\n\t"

"movq 8(%%"X86_REGC"di), %%mm2\n\t"
"psrad $1, %%mm0\n\t"
"pmaddwd 8(%%"X86_REGC"si), %%mm2\n\t"

"movq 16(%%"X86_REGC"di), %%mm6\n\t"
"psrad $1, %%mm2\n\t"
"pmaddwd 16(%%"X86_REGC"si), %%mm6\n\t"

"movq 24(%%"X86_REGC"di), %%mm7\n\t"
"psrad $1, %%mm6\n\t"
"pmaddwd 24(%%"X86_REGC"si), %%mm7\n\t"

"paddd %%mm0, %%mm1\n\t"
"paddd %%mm2, %%mm3\n\t"
"psrad $1, %%mm7\n\t"
"paddd %%mm6, %%mm4\n\t"
"paddd %%mm7, %%mm5\n\t"

"add"X86_REGAT" $32, %%"X86_REGC"si\n\t"
"add"X86_REGAT" $32, %%"X86_REGC"di\n\t"
"subl $1, %%ecx\n\t"
"jnz MMX_Loop\n\t"

//
#if FIR_TABLE_EXTRA_BITS != 0
"psrad $"FIR_TABLE_EXTRA_BITS_S", %%mm1\n\t"
"psrad $"FIR_TABLE_EXTRA_BITS_S", %%mm3\n\t"
"psrad $"FIR_TABLE_EXTRA_BITS_S", %%mm4\n\t"
"psrad $"FIR_TABLE_EXTRA_BITS_S", %%mm5\n\t"
#endif

// Now, mm1, mm3, mm4, mm5 contain 8 32-bit sums that need to be added together.

"paddd %%mm5, %%mm3\n\t"
"paddd %%mm4, %%mm1\n\t"
"paddd %%mm3, %%mm1\n\t"
"movq %%mm1, %%mm6\n\t"
"psrlq $32, %%mm6\n\t"
"paddd %%mm6, %%mm1\n\t"

"psrad $15, %%mm1\n\t"
//"psrad $16, %%mm1\n\t"
"movd %%mm1, (%%"X86_REGC"dx)\n\t"
 : "=D" (dummy), "=S" (dummy), "=c" (dummy)
 : "D" (wave), "S" (coeffs), "c" ((count + 0xF) >> 4), "d" (accum_output)
#ifdef __x86_64__
 : "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7", "cc"
#else
 // FIXME.  Do we need to compile with -mmmx or something?
 // st(0), st(1), st(2), st(3), st(4), st(5), st(6), st(7)
 : "cc"
#endif
);
}
#endif


#ifdef ARCH_POWERPC_ALTIVEC
static INLINE void DoMAC_AltiVec(int16 *wave, int16 *coeffs, int32 count, int32 *accum_output)
{
          vector signed int acc0, acc1, acc2, acc3;
	  vector unsigned int vecsrEBp1 = vec_splats((unsigned int)FIR_TABLE_EXTRA_BITS + 1);
	  vector unsigned int vecsr16m1 = vec_splats((unsigned int)16 - 1);
          vector signed int zerosi = vec_splat_s32(0);
          vector signed short wd0, fd0;
          vector signed short wd1, fd1;
	  vector signed int tmp;
	  int32 ino;

#if 0
	  {
	   const uint8 blocksize = 1 * sizeof(int16);
	   const uint8 blockcount = (count + 15) / 16;
	   const int16 blockstride = blocksize;

	   vec_dstt(coeffs, (blocksize << 3) | (blockcount << 8) | (blockstride << 16), 0);
	  }
#endif

          acc0 = zerosi;
	  acc1 = zerosi;
	  acc2 = zerosi;
	  acc3 = zerosi;

          for(ino = 0; ino < count; ino += (16 << 1))
          {
           wd0 = vec_ld(ino, wave);
           fd0 = vec_ld(ino, coeffs);
           acc0 = vec_msums(wd0, fd0, acc0);

           wd1 = vec_ld(ino + (8 << 1), wave);
           fd1 = vec_ld(ino + (8 << 1), coeffs);
           acc1 = vec_msums(wd1, fd1, acc1);
          }

          acc0 = vec_sra(acc0, vecsrEBp1);
          acc1 = vec_sra(acc1, vecsrEBp1);

          for(; ino < (count << 1); ino += (16 << 1))
          {
           wd0 = vec_ld(ino, wave);
           fd0 = vec_ld(ino, coeffs);
           acc2 = vec_msums(wd0, fd0, acc2);

           wd1 = vec_ld(ino + (8 << 1), wave);
           fd1 = vec_ld(ino + (8 << 1), coeffs);
           acc3 = vec_msums(wd1, fd1, acc3);
          }

	  acc2 = vec_sra(acc2, vecsrEBp1);
	  acc3 = vec_sra(acc3, vecsrEBp1);

	  //
	  //
	  tmp = vec_add(vec_add(acc0, acc2), vec_add(acc1, acc3));
	  tmp = vec_splat(vec_sums(tmp, zerosi), 3);
	  tmp = vec_sra(tmp, vecsr16m1);

	  vec_ste(tmp, 0, accum_output);
}
#endif

/* Returns number of samples written to out. */
/* leftover is set to the number of samples that need to be copied
   from the end of in to the beginning of in.
*/

int32 NES_Resampler::Do(int16 *in, int16 *out, uint32 maxoutlen, uint32 inlen, int32 *leftover)
{
	uint32 max;
	uint32 count=0;
	int32 *I32Out = boobuf;

	{
	 int64 max_temp = inlen;

	 max_temp -= NumCoeffs;

	 if(max_temp < 0) 
	 {
  	  puts("Eep");
  	  max_temp = 0;
	 }
	 max = max_temp;
	}
	//printf("%d %d\n", inlen, max);

	if(0)
	{

	}
        #ifdef ARCH_X86
        else if(cpuext & CPUTEST_FLAG_MMX)
        {
 	 while(InputIndex < max)
         {
          const unsigned int align_index = InputIndex & 0x3; //((int)(unsigned long long)wave & 0x6) >> 1;
          int16 *wave = &in[InputIndex &~ 0x3];
          int16 *coeffs = &FIR_ENTRY(align_index, PhaseWhich[InputPhase], 0);
	  int32 coeff_count = FIR_CoCounts[align_index];

 	  DoMAC_MMX(wave, coeffs, coeff_count, I32Out);

	  I32Out++;
	  count++;

	  InputPhase = PhaseNext[InputPhase];
          InputIndex += PhaseStep[InputPhase];
	 }
	 asm volatile("emms\n\t");
	}
	#endif
	#ifdef ARCH_POWERPC_ALTIVEC
        else if(cpuext & CPUTEST_FLAG_ALTIVEC)
	{
         while(InputIndex < max)
         {
          const unsigned int align_index = InputIndex & 0x7;
          int16 *wave = &in[InputIndex &~ 0x7];
          int16 *coeffs = &FIR_ENTRY(align_index, PhaseWhich[InputPhase], 0);
          int32 coeff_count = FIR_CoCounts[align_index];

          DoMAC_AltiVec(wave, coeffs, coeff_count, I32Out);

          I32Out++;
          count++;

          InputPhase = PhaseNext[InputPhase];
          InputIndex += PhaseStep[InputPhase];
         }
	}
	#endif
	else
	{
         while(InputIndex < max)
         {
          int16 *wave = &in[InputIndex];
          int16 *coeffs = &FIR_ENTRY(0, PhaseWhich[InputPhase], 0);
          int32 coeff_count = FIR_CoCounts[0];

          DoMAC(wave, coeffs, coeff_count, I32Out);

          I32Out++;
          count++;

          InputPhase = PhaseNext[InputPhase];
          InputIndex += PhaseStep[InputPhase];
         }
	}


        *leftover = inlen - InputIndex;

	InputIndex = 0;

	if(*leftover < 0) 
	{
	 //printf("Oops: %d\n", *leftover);
         InputIndex = (0 - *leftover);
	 *leftover = 0;
	}

	if(!debias_multiplier)
	{
         if(SoundVolume == 256)
 	 {
          for(uint32 x = 0; x < count; x++)
           out[x] = boobuf[x];
	 }
	 else
	 {
          for(uint32 x = 0; x < count; x++)
	   out[x] = (boobuf[x] * SoundVolume) >> 8;
	 }
	}
	else
	{
	 for(uint32 x = 0; x < count; x++)
	 {
	  int32 sample = boobuf[x];
          debias += ((int64)((sample << 16) - debias) * debias_multiplier) >> 32;
	  out[x] = ((sample - (debias >> 16)) * SoundVolume) >> 8;
	 }
	}
	return(count);
}

NES_Resampler::~NES_Resampler()
{
 if(PhaseWhich)
  free(PhaseWhich);

 if(PhaseNext)
  free(PhaseNext);

 if(PhaseStep)
  free(PhaseStep);

 if(PhaseStepSave)
  free(PhaseStepSave);

 if(FIR_Coeffs_Real)
 {
  for(unsigned int i = 0; i < NumAlignments * NumPhases; i++)
   if(FIR_Coeffs_Real[i])
    free(FIR_Coeffs_Real[i]);

  free(FIR_Coeffs_Real);
 }

 if(FIR_Coeffs)
  free(FIR_Coeffs);

 if(FIR_CoCounts)
  free(FIR_CoCounts);
}

void NES_Resampler::SetVolume(double newvolume)
{
 SoundVolume = (int32)(newvolume * 256);
}

// Copy constructor
#if 0
// Bah, it's obviously screwed up, make a base initialization function to call or just don't implement a copy constructor!
NES_Resampler::NES_Resampler(const NES_Resampler &resamp)
{
 NES_Resampler(resamp.InputRate, resamp.OutputRate, resamp.RateError, resamp.DebiasCorner, resamp.Quality);

 SoundVolume = resamp.SoundVolume;
 for(unsigned int i = 0; i < NumPhases; i++)
 {
  PhaseStep[i] = resamp.PhaseStep[i];
 }
}
#endif

NES_Resampler::NES_Resampler(double input_rate, double output_rate, double rate_error, double debias_corner, int quality)
{
 double *FilterBuf = NULL;
 double ratio = (double)output_rate / input_rate;
 double cutoff;
 double required_bandwidth;
 double k_beta;
 double k_d;

 InputRate = input_rate;
 OutputRate = output_rate;
 RateError = rate_error;
 DebiasCorner = debias_corner;
 Quality = quality;

 //cpuext = cputest_get_flags();

 MDFN_printf("filter.cpp debug info:\n");
 MDFN_indent(1);

 #ifdef ARCH_X86
 if(cpuext & CPUTEST_FLAG_MMX)
 {
  MDFN_printf("MMX\n");
  NumAlignments = 4;
 } else
 #elif ARCH_POWERPC_ALTIVEC
 if(cpuext & CPUTEST_FLAG_ALTIVEC)
 {
  puts("AltiVec");
  NumAlignments = 8;
 } else
 #endif
 {
  NumAlignments = 1;
  puts("None");
 }


 if(quality == -2)
 {
  k_beta = 3.384;
  k_d = 2.23;
  NumCoeffs = 128;
 }
 else if(quality == -1)
 {
  k_beta = 4.538;
  k_d = 2.93;
  NumCoeffs = 256;
 }
 else if(quality == 0)
 {
  k_beta = 5.658;
  k_d = 3.62;
  NumCoeffs = 352;
 }
 else if(quality == 1)
 {
  k_beta = 7.865;
  k_d = 5.0;
  NumCoeffs = 512;
 }
 else if(quality == 2)
 {
  k_beta = 8.960;
  k_d = 5.7;
  NumCoeffs = 768;
 }
 else if(quality == 3)
 {
  k_beta = 10.056;
  k_d = 6.4;
  NumCoeffs = 1024;
 }
 else
 {
  MDFN_indent(-1);
  throw(-1);
 }

 assert((NumCoeffs % 32) == 0);
 assert(NumAlignments <= 8); 

 NumCoeffs_Padded = NumCoeffs + 4 + 16;	// FIXME: set differently based on SIMD path in use.

 required_bandwidth = k_d / NumCoeffs;

 MDFN_printf("%f\n", required_bandwidth);	

 // Get the number of phases required, and adjust ratio.
 {
  double s_ratio = (double)input_rate / output_rate;
  double findo = 0;
  uint32 count = 0;
  uint32 findo_i;

  do
  {
   count++;
   findo += s_ratio;
  } while( fabs(1.0 - ((round(findo) / count) / s_ratio)) > rate_error);

  s_ratio = round(findo) / count;
  findo_i = (uint32) round(findo);
  ratio = 1 / s_ratio;
  NumPhases = count;

  PhaseWhich = (uint32 *)malloc(sizeof(uint32) * NumPhases);
  PhaseNext = (uint32 *)malloc(sizeof(uint32) * NumPhases);
  PhaseStep = (uint32 *)malloc(sizeof(uint32) * NumPhases);
  PhaseStepSave = (uint32 *)malloc(sizeof(uint32) * NumPhases);

  uint32 last_indoo = 0;
  for(unsigned int i = 0; i < NumPhases; i++)
  {
   uint32 index_pos = i * findo_i / NumPhases;

   PhaseWhich[i] = (i * findo_i) % NumPhases;
   PhaseNext[i] = (i + 1) % (NumPhases);
   PhaseStepSave[i] = PhaseStep[i] = index_pos - last_indoo;
   last_indoo = index_pos;
  }
  PhaseStepSave[0] = PhaseStep[0] = findo_i - last_indoo;

  Ratio_Dividend = findo_i;
  Ratio_Divisor = NumPhases;

  MDFN_printf("Phases: %d, Output rate: %f, %d %d\n", NumPhases, input_rate * ratio, Ratio_Dividend, Ratio_Divisor);

  MDFN_printf("Desired rate error: %.10f, Actual rate error: %.10f\n", rate_error, fabs((double)input_rate / output_rate * ratio - 1));
 }

 cutoff = (ratio - required_bandwidth) / NumPhases;

 MDFN_printf("Cutoff: %f, %f\n", cutoff, required_bandwidth);

 if(cutoff <= 0)
 {
  MDFN_printf("Cutoff frequency is <= 0: %f\n", cutoff);
 }

 FIR_Coeffs = (int16 **)malloc(sizeof(int16 **) * NumAlignments * NumPhases);
 FIR_Coeffs_Real = (int16 **)malloc(sizeof(int16 **) * NumAlignments * NumPhases);

 for(unsigned int i = 0; i < NumAlignments * NumPhases; i++)
 {
  uint8 *tmp_ptr = (uint8 *)calloc(sizeof(int16) * NumCoeffs_Padded + 16, 1);

  FIR_Coeffs_Real[i] = (int16 *)tmp_ptr;
  tmp_ptr += 0xF;
  tmp_ptr -= ((unsigned long long)tmp_ptr & 0xF);
  FIR_Coeffs[i] = (int16 *)tmp_ptr;
 }

 MDFN_printf("FIR table memory usage: %d bytes\n", (int)((sizeof(int16) * NumCoeffs_Padded + 16) * NumAlignments * NumPhases));


 FilterBuf = (double *)malloc(sizeof(double) * NumCoeffs * NumPhases);
 gen_sinc(FilterBuf, NumCoeffs * NumPhases, cutoff, k_beta);
 normalize(FilterBuf, NumCoeffs * NumPhases); 

 #if 0
 for(int i = 0; i < NumCoeffs * NumPhases; i++)
  fprintf(stderr, "%.20f\n", FilterBuf[i]);

 #endif


 FIR_CoCounts = (uint32 *)calloc(NumAlignments, sizeof(uint32));
 FIR_CoCounts[0] = NumCoeffs;

 for(unsigned int phase = 0; phase < NumPhases; phase++)
 {
  int32 neg_sum = 0;
  int32 pos_sum = 0;
  int32 sum = 0;
  int32 sum_absv = 0;
  int32 max = 0, min = 0;
  double amp_mult = 65536 * NumPhases * (1 << FIR_TABLE_EXTRA_BITS);

  for(unsigned int i = 0; i < NumCoeffs; i++)
  {
   int32 tmpco = (int32)(FilterBuf[i * NumPhases + phase] * amp_mult);

   FIR_ENTRY(0, NumPhases - 1 - phase, i) = (int16)tmpco;

   sum += tmpco;
   sum_absv += abs(tmpco);

   if(tmpco > max)
    max = tmpco;

   if(tmpco < min)
    min = tmpco;

   if(tmpco > 0)
    pos_sum += tmpco;
   else
    neg_sum += tmpco;
  }

  assert(min >= -32768);
  assert(max <= 32767);

  double ira = ((double)(65536 << FIR_TABLE_EXTRA_BITS) / sum_absv) * 2; // * 2 since we shift right by one in loops now
								         // to prevent distortions when input samples are near
								         // maximum absolute magnitude.

  MDFN_printf("Phase %d: min=%d max=%d, neg_sum=%d, pos_sum=%d, sum=%d, sum_absv=%d -- worst-case ira=%.2f%%\n", phase, min, max, neg_sum, pos_sum, sum, sum_absv, ira * 100); //65536 * count / amp_mult * NumPhases);
 }

 for(unsigned int ali = 1; ali < NumAlignments; ali++)
 {
  for(unsigned int phase = 0; phase < NumPhases; phase++)
  {
   FIR_CoCounts[ali] = NumCoeffs + ali;
   for(unsigned int i = 0; i < NumCoeffs; i++)
   {
    FIR_ENTRY(ali, phase, i + ali) = FIR_ENTRY(0, phase, i);
   }
  }
 }

 free(FilterBuf);
 FilterBuf = NULL;

 InputIndex = 0;
 InputPhase = 0;

 debias = 0;
 debias_multiplier = (int32)(((int64)1 << 32) * debias_corner / output_rate);

 MDFN_indent(-1);
}
