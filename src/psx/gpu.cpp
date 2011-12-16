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

#include "psx.h"

/*
 TODO:
	Test and clean up line, particularly polyline, drawing.

	"abe" transparency testing might be not correct.

	Mask bit emulation seems to be broken.  See "Silent Hill".

	Not everything is returned in the status port read yet(double check).

	"dfe" bit of drawing mode isn't handled properly yet.

	Correct the number of scanlines per frame in interlace, and non-interlace modes, and in NTSC and PAL modes. (It might be right already, but I doubt it)

	Correct the number of clocks per scanline.

	Initialize more stuff in the Power() function.

	See if the FB rect fill command has a bit that can enable dithering.

	Fix dithering of all vertex-colors-the-same polygons with goraud shading bit set to 1(but confirm that it really happens on the real thing).  Right now
	dithering is disabled due to goraud shading being disabled in that case as an optimization.

	Fix "ODE" bit of GPU status in non-interlaced mode, and return the interlaced field(odd/even) bit correctly in GPU status reads.

	Fix triangle edges.

	Fix triangle span rendering order(it's bottom-to-up sometimes on the real thing, to avoid negative x step/increment values).
*/

/*
 GPU display timing master clock is nominally 53.693182 MHz for NTSC PlayStations, and 53.203425 MHz for PAL PlayStations.

 Non-interlaced NTSC mode line timing notes:

	3412.5 master display clocks per scanline on average(probably oscillates between 3413 and 3412)

	Multiplying the results of counter 0 in pixel clock mode by the clock divider of the current dot clock mode/width gives a result that's slightly less
	than 3412.5(such as 3408 for "320" mode, 3410 for "256" and "512" modes, ); the dot clock divider is probably being reset each scanline.

 Non-interlaced PAL mode:

	3404 master display clocks per scanline(I thiiiiink, might not be exactly right - maybe 3405?).

*/
namespace MDFN_IEN_PSX
{
//FILE *fp;

static const int32 dither_table[4][4] =
{
 { -4,  0, -3,  1 },
 {  2, -2,  3, -1 },
 { -3,  1, -4,  0 },
 {  3, -1,  2, -2 },
};

PS_GPU::PS_GPU()
{
 for(int y = 0; y < 4; y++)
  for(int x = 0; x < 4; x++)
   for(int v = 0; v < 512; v++)
   {
    int value = v + dither_table[y][x];

    value >>= 3;
 
    if(value < 0)
     value = 0;

    if(value > 0x1F)
     value = 0x1F;

    DitherLUT[y][x][v] = value;
   }

 // 65536 * 53693181.818 / (44100 * 768)
 GPUClockRatio = 103896;


 memset(RGB8SAT_Under, 0, sizeof(RGB8SAT_Under));

 for(int i = 0; i < 256; i++)
  RGB8SAT[i] = i;

 memset(RGB8SAT_Over, 0xFF, sizeof(RGB8SAT_Over));
}

PS_GPU::~PS_GPU()
{

}

void PS_GPU::SoftReset(void) // Control command 0x00
{
 DMAControl = 0;

 DisplayMode = 0;
 DisplayOff = 1;
 DisplayFB_XStart = 0;
 DisplayFB_YStart = 0;

 HorizStart = 0;
 HorizEnd = 0;

 VertStart = 0;
 VertEnd = 0;
}

void PS_GPU::Power(void)
{
 dtd = false;
 memset(GPURAM, 0, sizeof(GPURAM));

 DMAControl = 0;

 tww = 0;
 twh = 0;
 twx = 0;
 twy = 0;

 RecalcTexWindowLUT();

 OffsX = 0;
 OffsY = 0;

 ClipX0 = 0;
 ClipY0 = 0;
 ClipX1 = 1023;
 ClipY1 = 511;

 MaskSetOR = 0;
 MaskEvalAND = 0;

 abr = 0;

 memset(CB, 0, sizeof(CB));
 CBIn = 0;

 InFBRead = false;
 InFBWrite = false;

 DisplayMode = 0;
 DisplayOff = 1;
 DisplayFB_XStart = 0;
 DisplayFB_YStart = 0;

 HorizStart = 0;
 HorizEnd = 0;

 VertStart = 0;
 VertEnd = 0;

 //
 //
 //
 DisplayFB_CurY = 0;
 DisplayHeightCounter = 0;


 //
 //
 //
 scanline = 0;

 //
 //
 //
 DotClockCounter = 0;
 GPUClockCounter = 0;
 LineClockCounter = 3412;

 field = 0;
 PhaseChange = 0;

 lastts = 0;


 // TODO: factor out in a separate function.
 LinesPerField = 263;
 VisibleStartLine = 22;

 DrawTimeAvail = 0;
}

void PS_GPU::ResetTS(void)
{
 lastts = 0;
}

#define COORD_FBS 20

static INLINE int32 COORD_GET_INT_EX(int64 n)
{
 int32 ret;

 ret = (n + ((n >> 63) & ((1 << COORD_FBS) - 1))) >> COORD_FBS;

 return(ret);
}

static INLINE int32 COORD_GET_INT(int32 n)
{
 return(n >> COORD_FBS);
}


#define COORD_MF_INT(n) ((n) << COORD_FBS)

typedef int v8si __attribute__ ((vector_size(32)));


struct tri_vertex
{
 int32 x, y;
 int32 u, v;
 int32 r, g, b;

 bool tex_unbound;
};

struct point_group
{
#if 0
 int64 x, y;
 int64 u, v;
 int64 r, g, b;
#else
 int32 x, y;
 int32 u, v;
 int32 r, g, b;
#endif

 bool tex_unbound;
};

struct point_group_step
{
#if 0
 int64 dx_dk, dy_dk;
 int64 du_dk, dv_dk;
 int64 dr_dk, dg_dk, db_dk;
#else
 int32 dx_dk, dy_dk;
 int32 du_dk, dv_dk;
 int32 dr_dk, dg_dk, db_dk;
#endif
};

static INLINE void ZeroStep(point_group_step &step)
{
 memset(&step, 0, sizeof(point_group_step));
}

static INLINE int32 ROUND_HELPER(int32 delta, int32 dk)
{
 int32 ret;

#if 1
 if(delta < 0)
  delta -= dk - 1;

 if(delta > 0)
  delta += dk - 1;
#endif
 ret = delta / dk;


 return(ret);
}


template<bool goraud, bool textured>
static INLINE void CalcStep(point_group_step &dest, const point_group &point0, const point_group &point1, int32 dk)
{
 dest.dx_dk = ROUND_HELPER(point1.x - point0.x, dk);
 dest.dy_dk = ROUND_HELPER(point1.y - point0.y, dk);

 if(textured)
 {
  dest.du_dk = ROUND_HELPER(point1.u - point0.u, dk);
  dest.dv_dk = ROUND_HELPER(point1.v - point0.v, dk);
 }

 if(goraud)
 {
  dest.dr_dk = ROUND_HELPER(point1.r - point0.r, dk);
  dest.dg_dk = ROUND_HELPER(point1.g - point0.g, dk);
  dest.db_dk = ROUND_HELPER(point1.b - point0.b, dk);
 }
}


template<bool goraud, bool textured>
static INLINE void AddStep(point_group &point, const point_group_step &step, int32 count = 1)
{
 //if(count == 1)
 // point.point_vec += step.delta_vec;
 //else
 {
  point.x += step.dx_dk * count;
  point.y += step.dy_dk * count;

  if(textured)
  {
   point.u += step.du_dk * count;
   point.v += step.dv_dk * count;
  }
 
  if(goraud)
  {
   point.r += step.dr_dk * count;
   point.g += step.dg_dk * count;
   point.b += step.db_dk * count;
  }

 }
}

struct i_group
{
 int32 u, v;
 int32 r, g, b;
 int32 dummy0[3];
};

struct i_deltas
{
 int32 du_dx, dv_dx;
 int32 dr_dx, dg_dx, db_dx;
 int32 dummy0[3];

 int32 du_dy, dv_dy;
 int32 dr_dy, dg_dy, db_dy;
 int32 dummy1[3];
};

//#define CALCIS(x,y) ( A.x * (B.y - C.y) + B.x * (C.y - A.y) + C.x * (A.y - B.y) )
#define CALCIS(x,y) (((B.x - A.x) * (C.y - B.y)) - ((C.x - B.x) * (B.y - A.y)))
static INLINE bool CalcIDeltas(i_deltas &idl, const tri_vertex &A, const tri_vertex &B, const tri_vertex &C)
{
 int64 num = ((int64)COORD_MF_INT(1)) << 32;
 int64 denom = CALCIS(x, y);
 int64 one_div;

 if(!denom)
  return(false);

//num -= abs(denom) - 1;
// num += abs(denom) >> 1;

 one_div = num / denom;

 idl.dr_dx = ((one_div * CALCIS(r, y)) + 0x00000000) >> 32;
 idl.dr_dy = ((one_div * CALCIS(x, r)) + 0x00000000) >> 32;

 idl.dg_dx = ((one_div * CALCIS(g, y)) + 0x00000000) >> 32;
 idl.dg_dy = ((one_div * CALCIS(x, g)) + 0x00000000) >> 32;

 idl.db_dx = ((one_div * CALCIS(b, y)) + 0x00000000) >> 32;
 idl.db_dy = ((one_div * CALCIS(x, b)) + 0x00000000) >> 32;

 idl.du_dx = ((one_div * CALCIS(u, y)) + 0x00000000) >> 32;
 idl.du_dy = ((one_div * CALCIS(x, u)) + 0x00000000) >> 32;

 idl.dv_dx = ((one_div * CALCIS(v, y)) + 0x00000000) >> 32;
 idl.dv_dy = ((one_div * CALCIS(x, v)) + 0x00000000) >> 32;

// printf("  du_dx=%08x, du_dy=%08x\n", idl.du_dx, idl.du_dy);

 return(true);
}
#undef CALCIS

template<bool goraud, bool textured>
static INLINE void AddIDeltas_DX(i_group &ig, const i_deltas &idl)
{
 if(textured)
 {
  ig.u += idl.du_dx;
  ig.v += idl.dv_dx;
 }

 if(goraud)
 {
  ig.r += idl.dr_dx;
  ig.g += idl.dg_dx;
  ig.b += idl.db_dx;
 }
}


template<bool goraud, bool textured>
static INLINE void AddIDeltas_DY(i_group &ig, const i_deltas &idl, int32 count = 1)
{
 if(textured)
 {
  ig.u += idl.du_dy * count;
  ig.v += idl.dv_dy * count;
 }

 if(goraud)
 {
  ig.r += idl.dr_dy * count;
  ig.g += idl.dg_dy * count;
  ig.b += idl.db_dy * count;
 }
}

template<int BlendMode, bool MaskEval_TA, bool textured>
INLINE void PS_GPU::PlotPixel(int32 x, int32 y, uint16 fore_pix)
{
 if(BlendMode >= 0 && (fore_pix & 0x8000))
 {
  uint16 bg_pix = GPURAM[y][x];	// Don't use bg_pix for mask evaluation, it's modified in blending code paths.
  uint16 pix; // = fore_pix & 0x8000;

/*
 static const int32 tab[4][2] =
 {
  { 2,  2 },
  { 4,  4 },
  { 4, -4 },
  { 4,  1 }
 };
*/
  // Efficient 15bpp pixel math algorithms from blargg
  switch(BlendMode)
  {
   case 0:
	bg_pix |= 0x8000;
	pix = ((fore_pix + bg_pix) - ((fore_pix ^ bg_pix) & 0x0421)) >> 1;
	break;
	  
   case 1:
       {
	bg_pix &= ~0x8000;

	uint32 sum = fore_pix + bg_pix;
	uint32 carry = (sum - ((fore_pix ^ bg_pix) & 0x8421)) & 0x8420;

	pix = (sum - carry) | (carry - (carry >> 5));
       }
       break;

   case 2:
       {
	bg_pix |= 0x8000;
        fore_pix &= ~0x8000;

	uint32 diff = bg_pix - fore_pix + 0x108420;
	uint32 borrow = (diff - ((bg_pix ^ fore_pix) & 0x108420)) & 0x108420;

	pix = (diff - borrow) & (borrow - (borrow >> 5));
       }
       break;

   case 3:
       {
	bg_pix &= ~0x8000;
	fore_pix = ((fore_pix >> 2) & 0x1CE7) | 0x8000;

	uint32 sum = fore_pix + bg_pix;
	uint32 carry = (sum - ((fore_pix ^ bg_pix) & 0x8421)) & 0x8420;

	pix = (sum - carry) | (carry - (carry >> 5));
       }
       break;
  }


  if(!MaskEval_TA || !(GPURAM[y][x] & 0x8000))
   GPURAM[y][x] = textured ? pix : ((pix & 0x7FFF) | MaskSetOR);
 }
 else
 {
  if(!MaskEval_TA || !(GPURAM[y][x] & 0x8000))
   GPURAM[y][x] = textured ? fore_pix : ((fore_pix & 0x7FFF) | MaskSetOR);
 }
}

INLINE uint16 PS_GPU::ModTexel(uint16 texel, int32 r, int32 g, int32 b, const int32 dither_x, const int32 dither_y)
{
 uint16 ret = texel & 0x8000;

 ret |= DitherLUT[dither_y][dither_x][(((texel & 0x1F) * r) >> (5 - 1))] << 0;
 ret |= DitherLUT[dither_y][dither_x][(((texel & 0x3E0) * g) >> (10 - 1))] << 5;
 ret |= DitherLUT[dither_y][dither_x][(((texel & 0x7C00) * b) >> (15 - 1))] << 10;

 return(ret);
}

template<uint32 TexMode_TA>
INLINE uint16 PS_GPU::GetTexel(uint32 clut_offset, int32 u_arg, int32 v_arg)
{
     uint32 u = TexWindowXLUT[u_arg];
     uint32 v = TexWindowYLUT[v_arg];
     uint32 fbtex_x = TexPageX + (u >> (2 - TexMode_TA));
     uint32 fbtex_y = TexPageY + v;
     uint16 fbw = GPURAM[fbtex_y][fbtex_x & 1023];

     if(TexMode_TA != 2)
     {
      if(TexMode_TA == 0)
       fbw = (fbw >> ((u & 3) * 4)) & 0xF;
      else
       fbw = (fbw >> ((u & 1) * 8)) & 0xFF;
 
      fbw = *(&GPURAM[0][0] + ((clut_offset + fbw) & ((512 * 1024) - 1)));
      //fbw = GPURAM[clut_offset >> 10][(clut_offset + fbw) & 1023];
     }

     return(fbw);
}

template<bool goraud, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
INLINE void PS_GPU::DrawSpan(int y, uint32 clut_offset, const int32 x_start, const int32 x_bound, const int32 bv_x, i_group ig, const i_deltas &idl, bool unbound)
{
  int32 xs = COORD_GET_INT(x_start /*+ (1 << (COORD_FBS - 1))*/), xb = COORD_GET_INT(x_bound/* + (1 << (COORD_FBS - 1))*/);

//printf("[GPU]        Drawspawn y=%d, xs=%d, xb=%d\n", y, xs, xb);
//  if(((dfe ^ 1) & FrameInterlaced) & (y ^ 1 ^ field ^ DisplayFB_YStart))
//   return;

  //if(!dfe && FrameInterlaced && (y & 1) == field)
  // return;

  if(xs < xb)	// (xs != xb)
  {
//   bool skip_first_inc = false;
   uint32 u_adjust, v_adjust;

   if(xs < ClipX0)
    xs = ClipX0;

   if(xb > (ClipX1 + 1))
    xb = ClipX1 + 1;

   if(xs < xb)
    DrawTimeAvail -= (xb - xs) * ((BlendMode >= 0) ? 2 : 1);

   if(textured)
   {
    ig.u += (xs - bv_x) * idl.du_dx;
    ig.v += (xs - bv_x) * idl.dv_dx;

    if(unbound)
    {
     u_adjust = 0;
     v_adjust = 0;
    }
    else
    {
     u_adjust = ((int64)idl.du_dx * (x_start & ((1 << COORD_FBS) - 1))) >> COORD_FBS;
     v_adjust = ((int64)idl.dv_dx * (x_start & ((1 << COORD_FBS) - 1))) >> COORD_FBS;
    }
   }

   if(goraud)
   {
    ig.r += (xs - bv_x) * idl.dr_dx;
    ig.g += (xs - bv_x) * idl.dg_dx;
    ig.b += (xs - bv_x) * idl.db_dx;
   }
   //if(!perp_coord.tex_unbound && x_start.x & )

   for(int32 x = xs; x < xb; x++)
   {
    uint32 r, g, b;

    if(goraud)
    {
     r = RGB8SAT[COORD_GET_INT(ig.r)];
     g = RGB8SAT[COORD_GET_INT(ig.g)];
     b = RGB8SAT[COORD_GET_INT(ig.b)];
    }
    else
    {
     r = COORD_GET_INT(ig.r);
     g = COORD_GET_INT(ig.g);
     b = COORD_GET_INT(ig.b);
    }

    if(textured)
    {
     uint16 fbw = GetTexel<TexMode_TA>(clut_offset, COORD_GET_INT(ig.u + u_adjust), COORD_GET_INT(ig.v + v_adjust));

     if(fbw)
     {
      if(TexMult)
      {
       if(dtd)
        fbw = ModTexel(fbw, r, g, b, x & 3, y & 3);
       else
        fbw = ModTexel(fbw, r, g, b, 3, 2); //x & 3, y & 3);
      }
      PlotPixel<BlendMode, MaskEval_TA, true>(x, y, fbw);
     }
     u_adjust = 0;
     v_adjust = 0;
    }
    else
    {
     uint16 pix = 0x8000;

     if(goraud && dtd)
     {
      pix |= DitherLUT[y & 3][x & 3][r] << 0;
      pix |= DitherLUT[y & 3][x & 3][g] << 5;
      pix |= DitherLUT[y & 3][x & 3][b] << 10;
     }
     else
     {
      pix |= (r >> 3) << 0;
      pix |= (g >> 3) << 5;
      pix |= (b >> 3) << 10;
     }
    
     PlotPixel<BlendMode, MaskEval_TA, false>(x, y, pix);
    }

    AddIDeltas_DX<goraud, textured>(ig, idl);
    //AddStep<goraud, textured>(perp_coord, perp_step);
   }
  }
}

#if 0
template<typename T>
INLINE void SWAP(T &a, T &b)
{
 T tmp;

 tmp = a;
 a = b;
 b = tmp;
}
#endif

template<bool goraud, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
void PS_GPU::DrawTriangle(tri_vertex *vertices, uint32 clut)
{
 i_deltas idl;

#if 0
 vertices[0].y = COORD_MF_INT(rand());
 vertices[1].y = COORD_MF_INT(rand());
 vertices[2].y = COORD_MF_INT(rand());

 vertices[0].x = COORD_MF_INT(rand());
 vertices[1].x = COORD_MF_INT(rand());
 vertices[2].x = COORD_MF_INT(rand());
#endif

 if(vertices[2].y < vertices[1].y)
 {
  tri_vertex tmp = vertices[1];
  vertices[1] = vertices[2];
  vertices[2] = tmp;
 }

 if(vertices[1].y < vertices[0].y)
 {
  tri_vertex tmp = vertices[0];
  vertices[0] = vertices[1];
  vertices[1] = tmp;
 }

 if(vertices[2].y < vertices[1].y)
 {
  tri_vertex tmp = vertices[1];
  vertices[1] = vertices[2];
  vertices[2] = tmp;
 }

 if(vertices[0].y == vertices[2].y)
  return;

 if((vertices[2].y - vertices[0].y) >= 1024)
 {
  PSX_WARNING("[GPU] Triangle height too large: %d", (vertices[2].y - vertices[0].y));
  return;
 }

 if(abs(vertices[2].x - vertices[0].x) >= 1024 ||
    abs(vertices[2].x - vertices[1].x) >= 1024 ||
    abs(vertices[1].x - vertices[0].x) >= 1024)
 {
  PSX_WARNING("[GPU] Triangle width too large: %d %d %d", abs(vertices[2].x - vertices[0].x), abs(vertices[2].x - vertices[1].x), abs(vertices[1].x - vertices[0].x));
  return;
 }

 if(!CalcIDeltas(idl, vertices[0], vertices[1], vertices[2]))
  return;

 // [0] should be top vertex, [2] should be bottom vertex, [1] should be off to the side vertex.
 //
 //
 int32 y_start = vertices[0].y;
 int32 y_middle = vertices[1].y;
 int32 y_bound = vertices[2].y;

 int32 base_coord;
 int32 base_step;

 int32 bound_coord_ul;
 int32 bound_coord_us;

 int32 bound_coord_ll;
 int32 bound_coord_ls;

 bool right_facing;
 //bool bottom_up;
 //bool unbound = false;
 i_group ig;

 ig.u = COORD_MF_INT(vertices[0].u) + (1 << (COORD_FBS - 1));
 ig.v = COORD_MF_INT(vertices[0].v) + (1 << (COORD_FBS - 1));
 ig.r = COORD_MF_INT(vertices[0].r);
 ig.g = COORD_MF_INT(vertices[0].g);
 ig.b = COORD_MF_INT(vertices[0].b);

 base_coord = COORD_MF_INT(vertices[0].x);
 base_step = ROUND_HELPER(COORD_MF_INT(vertices[2].x - vertices[0].x), (vertices[2].y - vertices[0].y));

 bound_coord_ul = COORD_MF_INT(vertices[0].x);
 bound_coord_ll = COORD_MF_INT(vertices[1].x);

 //
 //
 //

 bool base_coord_tex_unbound = false;
 if(vertices[0].tex_unbound && vertices[2].tex_unbound)
  base_coord_tex_unbound = true;

 bool bound_coord_ul_tex_unbound = false;
 if(vertices[0].tex_unbound && vertices[1].tex_unbound)
  bound_coord_ul_tex_unbound = true;

 bool bound_coord_ll_tex_unbound = false;
 if(vertices[1].tex_unbound && vertices[2].tex_unbound)
  bound_coord_ll_tex_unbound = true;


 //
 //
 //

 if(vertices[1].y == vertices[0].y)
 {
  bound_coord_us = 0;
  right_facing = (bool)(vertices[1].x > vertices[0].x);
 }
 else
 {
  bound_coord_us = ROUND_HELPER(COORD_MF_INT(vertices[1].x - vertices[0].x), (vertices[1].y - vertices[0].y));
  right_facing = (bool)(bound_coord_us > base_step);
 }

 if(vertices[2].y == vertices[1].y)
  bound_coord_ls = 0;
 else
  bound_coord_ls = ROUND_HELPER(COORD_MF_INT(vertices[2].x - vertices[1].x), (vertices[2].y - vertices[1].y));

 if(y_start < ClipY0)
 {
  int32 count = ClipY0 - y_start;

  y_start = ClipY0;
  base_coord += base_step * count;
  bound_coord_ul += bound_coord_us * count;

  AddIDeltas_DY<goraud, textured>(ig, idl, count);

  if(y_middle < ClipY0)
  {
   int32 count_ls = ClipY0 - y_middle;

   y_middle = ClipY0;
   bound_coord_ll += bound_coord_ls * count_ls;
  }
 }

 if(y_bound > (ClipY1 + 1))
 {
  y_bound = ClipY1 + 1;

  if(y_middle > y_bound)
   y_middle = y_bound;
 }

 if(right_facing)
 {
  for(int32 y = y_start; y < y_middle; y++)
  {
   DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, base_coord, bound_coord_ul, vertices[0].x, ig, idl, base_coord_tex_unbound);
   base_coord += base_step;
   bound_coord_ul += bound_coord_us;
   AddIDeltas_DY<goraud, textured>(ig, idl);
  }

  for(int32 y = y_middle; y < y_bound; y++)
  {
   DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, base_coord, bound_coord_ll, vertices[0].x, ig, idl, base_coord_tex_unbound);

   base_coord += base_step;
   bound_coord_ll += bound_coord_ls;
   AddIDeltas_DY<goraud, textured>(ig, idl);
  }
 }
 else
 {
  for(int32 y = y_start; y < y_middle; y++)
  {
   DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, bound_coord_ul, base_coord, vertices[0].x, ig, idl, bound_coord_ul_tex_unbound);
   base_coord += base_step;
   bound_coord_ul += bound_coord_us;
   AddIDeltas_DY<goraud, textured>(ig, idl);
  }

  for(int32 y = y_middle; y < y_bound; y++)
  {
   DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, bound_coord_ll, base_coord, vertices[0].x, ig, idl, bound_coord_ll_tex_unbound);
   base_coord += base_step;
   bound_coord_ll += bound_coord_ls;
   AddIDeltas_DY<goraud, textured>(ig, idl);
  }
 }

#if 0
 printf("[GPU] Vertices: %d:%d(r=%d, g=%d, b=%d) -> %d:%d(r=%d, g=%d, b=%d) -> %d:%d(r=%d, g=%d, b=%d)\n\n\n", vertices[0].x, vertices[0].y,
	vertices[0].r, vertices[0].g, vertices[0].b,
	vertices[1].x, vertices[1].y,
	vertices[1].r, vertices[1].g, vertices[1].b,
	vertices[2].x, vertices[2].y,
	vertices[2].r, vertices[2].g, vertices[2].b);
#endif
}


template<int numvertices, bool goraud, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
void PS_GPU::Command_DrawPolygon(const uint32 *cb)
{
 tri_vertex vertices[4];
 uint32 raw_colors[4];
 uint32 clut = 0;
 //uint32 tpage = 0;

 memset(vertices, 0, sizeof(vertices));

 raw_colors[0] = (*cb & 0xFFFFFF);
 cb++;

 vertices[0].x = (int16)(*cb & 0xFFFF);
 vertices[0].y = (int16)(*cb >> 16);
 cb++;


 if(textured)
 {
  vertices[0].u = (*cb & 0xFF);
  vertices[0].v = (*cb >> 8) & 0xFF;
  clut = ((*cb >> 16) & 0x7FFF) << 4;
  cb++;
 }

 if(goraud)
 {
  raw_colors[1] = (*cb & 0xFFFFFF);
  cb++;
 }

 vertices[1].x = (int16)(*cb & 0xFFFF);
 vertices[1].y = (int16)(*cb >> 16);
 cb++;

 if(textured)
 {
  vertices[1].u = (*cb & 0xFF);
  vertices[1].v = (*cb >> 8) & 0xFF;
  cb++;
 }

 if(goraud)
 {
  raw_colors[2] = (*cb & 0xFFFFFF);
  cb++;
 }

 vertices[2].x = (int16)(*cb & 0xFFFF);
 vertices[2].y = (int16)(*cb >> 16);
 cb++;

 if(textured)
 {
  vertices[2].u = (*cb & 0xFF);
  vertices[2].v = (*cb >> 8) & 0xFF;
  cb++;
 }

 if(numvertices == 4)
 {
  if(goraud)
  {
   raw_colors[3] = (*cb & 0xFFFFFF);
   cb++;
  }

  vertices[3].x = (int16)(*cb & 0xFFFF);
  vertices[3].y = (int16)(*cb >> 16);
  cb++;

  if(textured)
  {
   vertices[3].u = (*cb & 0xFF);
   vertices[3].v = (*cb >> 8) & 0xFF;
   cb++;
  }

  if(textured)
  {
   vertices[1].tex_unbound = true;
   vertices[2].tex_unbound = true;
  }
 }
 else
 {
  raw_colors[3] = raw_colors[0];
  if(textured)
  {
   vertices[0].tex_unbound = false;
   vertices[1].tex_unbound = false;
   vertices[2].tex_unbound = false;
  }
 }

 if(!goraud)
  raw_colors[1] = raw_colors[2] = raw_colors[3] = raw_colors[0];

 for(int i = 0; i < numvertices; i++)
 {
  //raw_colors[i] = rand() & 0xFFFFFF;

  vertices[i].r = raw_colors[i] & 0xFF;
  vertices[i].g = (raw_colors[i] >> 8) & 0xFF;
  vertices[i].b = (raw_colors[i] >> 16) & 0xFF;
 }

 for(int i = 0; i < numvertices; i++)
 {
  vertices[i].x += OffsX;
  vertices[i].y += OffsY;
 }

 if(textured && !dtd && (raw_colors[0] == raw_colors[1]) && (raw_colors[1] == raw_colors[2]) && (raw_colors[2] == raw_colors[3]) && (raw_colors[0] == 0x808080))
 {
  if(numvertices == 3)
   DrawTriangle<false, textured, BlendMode, false, TexMode_TA, MaskEval_TA>(vertices, clut);
  else
  {
   tri_vertex vertices_tmp[3];

   memcpy(&vertices_tmp[0], &vertices[1], 3 * sizeof(tri_vertex));

   DrawTriangle<false, textured, BlendMode, false, TexMode_TA, MaskEval_TA>(vertices, clut);
   DrawTriangle<false, textured, BlendMode, false, TexMode_TA, MaskEval_TA>(vertices_tmp, clut);
  }
  return;
 }

 if(numvertices == 3)
  DrawTriangle<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(vertices, clut);
 else
 {
  tri_vertex vertices_tmp[3];

  memcpy(&vertices_tmp[0], &vertices[1], 3 * sizeof(tri_vertex));

  DrawTriangle<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(vertices, clut);
  DrawTriangle<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(vertices_tmp, clut);
 }
}

template<bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA, bool FlipX, bool FlipY>
void PS_GPU::DrawSprite(int32 x_arg, int32 y_arg, int32 w, int32 h, uint8 u_arg, uint8 v_arg, uint32 color, uint32 clut_offset)
{
 const int32 r = color & 0xFF;
 const int32 g = (color >> 8) & 0xFF;
 const int32 b = (color >> 16) & 0xFF;
 const uint16 fill_color = 0x8000 | ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);

 int32 x_start, x_bound;
 int32 y_start, y_bound;
 uint8 u, v;
 int v_inc = 1, u_inc = 1;

 //printf("[GPU] Sprite: x=%d, y=%d, w=%d, h=%d\n", x_arg, y_arg, w, h);

 x_start = x_arg;
 x_bound = x_arg + w;

 y_start = y_arg;
 y_bound = y_arg + h;

 if(textured)
 {
  u = u_arg;
  v = v_arg;

  if(FlipX)
  {
   u_inc = -1;
   u += x_bound - x_start - 1;
  }

  if(FlipY)
  {
   v_inc = -1;
   v += y_bound - y_start - 1;
  }
 }

 if(x_start < ClipX0)
 {
  if(textured)
   u += (ClipX0 - x_start) * u_inc;

  x_start = ClipX0;
 }

 if(y_start < ClipY0)
 {
  if(textured)
   v += (ClipY0 - y_start) * v_inc;

  y_start = ClipY0;
 }

 if(x_bound > (ClipX1 + 1))
  x_bound = ClipX1 + 1;

 if(y_bound > (ClipY1 + 1))
  y_bound = ClipY1 + 1;

 if(y_bound > y_start && x_bound > x_start)
 {
  int32 suck_time = (x_bound - x_start) * (y_bound - y_start);

  if(BlendMode >= 0)
   suck_time <<= 1;

  DrawTimeAvail -= suck_time;
 }

 for(int32 y = y_start; y < y_bound; y++)
 {
  uint8 u_r;

  if(textured)
   u_r = u;

  for(int32 x = x_start; x < x_bound; x++)
  {
   if(textured)
   {
    uint16 fbw = GetTexel<TexMode_TA>(clut_offset, u_r, v);

    if(fbw)
    {
     if(TexMult)
     {
      if(dtd)
       fbw = ModTexel(fbw, r, g, b, x & 3, y & 3);
      else
       fbw = ModTexel(fbw, r, g, b, 3, 2); //x & 3, y & 3);
     }
     PlotPixel<BlendMode, MaskEval_TA, true>(x, y, fbw);
    }
   }
   else
    PlotPixel<BlendMode, MaskEval_TA, false>(x, y, fill_color);

   if(textured)
    u_r += u_inc;
  }
  if(textured)
   v += v_inc;
 }
}

template<uint8 raw_size, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
void PS_GPU::Command_DrawSprite(const uint32 *cb)
{
 int32 x, y;
 int32 w, h;
 uint8 u = 0, v = 0;
 uint32 color = 0;
 uint32 clut = 0;

 color = *cb & 0x00FFFFFF;
 cb++;

 x = (int16)(*cb & 0xFFFF);
 y = (int16)(*cb >> 16);
 cb++;

 //printf("SPRITE: %d %d %d\n", raw_size, x, y);

 if(textured)
 {
  u = *cb & 0xFF;
  v = (*cb >> 8) & 0xFF;
  clut = ((*cb >> 16) & 0x7FFF) << 4;
  cb++;
 }

 switch(raw_size)
 {
  default:
  case 0:
	w = *cb & 0xFFFF;
	h = *cb >> 16;
	cb++;
	break;

  case 1:
	w = 1;
	h = 1;
	break;

  case 2:
	w = 8;
	h = 8;
	break;

  case 3:
	w = 16;
	h = 16;
	break;
 }

 x += OffsX;
 y += OffsY;

 switch(SpriteFlip & 0x3000)
 {
  case 0x0000:
	if(!TexMult || (color == 0x808080 && !dtd))
  	 DrawSprite<textured, BlendMode, false, TexMode_TA, MaskEval_TA, false, false>(x, y, w, h, u, v, color, clut);
	else
	 DrawSprite<textured, BlendMode, true, TexMode_TA, MaskEval_TA, false, false>(x, y, w, h, u, v, color, clut);
	break;

  case 0x1000:
	if(!TexMult || (color == 0x808080 && !dtd))
  	 DrawSprite<textured, BlendMode, false, TexMode_TA, MaskEval_TA, true, false>(x, y, w, h, u, v, color, clut);
	else
	 DrawSprite<textured, BlendMode, true, TexMode_TA, MaskEval_TA, true, false>(x, y, w, h, u, v, color, clut);
	break;

  case 0x2000:
	if(!TexMult || (color == 0x808080 && !dtd))
  	 DrawSprite<textured, BlendMode, false, TexMode_TA, MaskEval_TA, false, true>(x, y, w, h, u, v, color, clut);
	else
	 DrawSprite<textured, BlendMode, true, TexMode_TA, MaskEval_TA, false, true>(x, y, w, h, u, v, color, clut);
	break;

  case 0x3000:
	if(!TexMult || (color == 0x808080 && !dtd))
  	 DrawSprite<textured, BlendMode, false, TexMode_TA, MaskEval_TA, true, true>(x, y, w, h, u, v, color, clut);
	else
	 DrawSprite<textured, BlendMode, true, TexMode_TA, MaskEval_TA, true, true>(x, y, w, h, u, v, color, clut);
	break;
 }
}

template<bool polyline, bool goraud, int BlendMode, bool MaskEval_TA>
void PS_GPU::Command_DrawLine(const uint32 *cb)
{
 if(polyline)
 {
//  assert(0);
  InPLine = true;
  return;
 }
 point_group vertices[2];
 point_group cur_point;
 point_group_step step;

 //printf("Line %08x\n", *cb);

 vertices[0].r = COORD_MF_INT((*cb >> 0) & 0xFF);
 vertices[0].g = COORD_MF_INT((*cb >> 8) & 0xFF);
 vertices[0].b = COORD_MF_INT((*cb >> 16) & 0xFF);
 cb++;

 vertices[0].x = COORD_MF_INT(((*cb >> 0) & 0xFFFF) + OffsX);
 vertices[0].y = COORD_MF_INT(((*cb >> 16) & 0xFFFF) + OffsY);
 cb++;

 if(goraud)
 {
  vertices[1].r = COORD_MF_INT((*cb >> 0) & 0xFF);
  vertices[1].g = COORD_MF_INT((*cb >> 8) & 0xFF);
  vertices[1].b = COORD_MF_INT((*cb >> 16) & 0xFF);
  cb++;
 }
 else
 {
  vertices[1].r = vertices[0].r;
  vertices[1].g = vertices[0].g;
  vertices[1].b = vertices[0].b;
 }


 vertices[1].x = COORD_MF_INT(((*cb >> 0) & 0xFFFF) + OffsX);
 vertices[1].y = COORD_MF_INT(((*cb >> 16) & 0xFFFF) + OffsY);
 cb++;

 if(vertices[0].y == vertices[1].y && vertices[0].x == vertices[1].x)
  return;

#if 0
 if(vertices[1].y < vertices[0].y)
 {
  point_group tmp = vertices[0];
  vertices[0] = vertices[1];
  vertices[1] = tmp;
 }
#endif
 int32 i_dx = abs(COORD_GET_INT(vertices[1].x) - COORD_GET_INT(vertices[0].x));
 int32 i_dy = abs(COORD_GET_INT(vertices[1].y) - COORD_GET_INT(vertices[0].y));
 int32 k = (i_dx > i_dy) ? i_dx : i_dy;

 CalcStep<goraud, false>(step, vertices[0], vertices[1], k);

 cur_point = vertices[0];
 
 for(int32 i = 0; /*i < k*/ i <= k; i++)
 {
  int32 x, y;
  uint16 pix = 0x8000;

  x = COORD_GET_INT(cur_point.x);
  y = COORD_GET_INT(cur_point.y);

  if(goraud && dtd)
  {
   pix |= DitherLUT[y & 3][x & 3][COORD_GET_INT(cur_point.r)] << 0;
   pix |= DitherLUT[y & 3][x & 3][COORD_GET_INT(cur_point.g)] << 5;
   pix |= DitherLUT[y & 3][x & 3][COORD_GET_INT(cur_point.b)] << 10;
  }
  else
  {
   pix |= (COORD_GET_INT(cur_point.r) >> 3) << 0;
   pix |= (COORD_GET_INT(cur_point.g) >> 3) << 5;
   pix |= (COORD_GET_INT(cur_point.b) >> 3) << 10;
  }

  // FIXME: There has to be a faster way than checking for being inside the drawing area for each pixel.
  if(x >= ClipX0 && x <= ClipX1 && y >= ClipY0 && y <= ClipY1)
   PlotPixel<BlendMode, MaskEval_TA, false>(x, y, pix);

  AddStep<goraud, false>(cur_point, step);
 }

}

void PS_GPU::Command_FBRect(const uint32 *cb)
{
 int32 r = cb[0] & 0xFF;
 int32 g = (cb[0] >> 8) & 0xFF;
 int32 b = (cb[0] >> 16) & 0xFF;

 int32 destX = cb[1] & 0xFFFF;
 int32 destY = cb[1] >> 16;
 int32 width = cb[2] & 0xFFFF;
 int32 height = (cb[2] >> 16) & 0xFFFF;


 const uint16 fill_value = ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);
// destX &= 1023;
// destY &= 511;

 //printf("[GPU] FB Rect %d:%d w=%d, h=%d\n", destX, destY, width, height);

 if(destX &~1023)
  PSX_WARNING("[GPU] FB Rect dest X coordinate out of range?");

 if(destY &~511)
  PSX_WARNING("[GPU] FB Rect dest Y coordinate out of range?");

 if(width &~1023)
  PSX_WARNING("[GPU] FB Rect width out of range?");

 if(height &~511)
  PSX_WARNING("[GPU] FB Rect height out of range? %d", height);

 if((destX + width) > 1024)
  width = 1024 - destX;

 if((destY + height) > 512)
  height = 512 - destY;

 if(height > 0 && width > 0)
  DrawTimeAvail -= width * height;

 for(int32 y = 0; y < height; y++)
 {
  for(int32 x = 0; x < width; x++)
  {
   int32 d_y = (y + destY);
   int32 d_x = (x + destX);

   assert(!(d_y & ~ 511));
   assert(!(d_x & ~ 1023));

   GPURAM[d_y][d_x] = fill_value;
  }
 }

}

void PS_GPU::Command_FBCopy(const uint32 *cb)
{
 int32 sourceX = (cb[1] >> 0) & 0xFFFF;
 int32 sourceY = (cb[1] >> 16) & 0xFFFF;
 int32 destX = (cb[2] >> 0) & 0xFFFF;
 int32 destY = (cb[2] >> 16) & 0xFFFF;
 int32 width = (cb[3] >> 0) & 0xFFFF;
 int32 height = (cb[3] >> 16) & 0xFFFF;

 if(sourceX &~1023)
  PSX_WARNING("[GPU] FB Copy source X coordinate out of range?");

 if(sourceY &~511)
  PSX_WARNING("[GPU] FB Copy source Y coordinate out of range?");

 if(destX &~1023)
  PSX_WARNING("[GPU] FB Copy dest X coordinate out of range?");

 if(destY &~511)
  PSX_WARNING("[GPU] FB Copy dest Y coordinate out of range?");

 if(width &~1023)
  PSX_WARNING("[GPU] FB Copy width out of range?");

 if(height &~511)
  PSX_WARNING("[GPU] FB Copy height out of range?");

 sourceX &= 1023;
 sourceY &= 511;

 destX &= 1023;
 destY &= 511;

 width &= 1023;
 height &= 511;

 if((sourceX + width) > 1024 || (destX + width) > 1024)
  PSX_WARNING("[GPU] FB copy width out of range?");

 if((sourceY + height) > 512 || (destY + height) > 512)
  PSX_WARNING("[GPU] FB copy height out of range?");

 if(height > 0 && width > 0)
  DrawTimeAvail -= (width * height) * 2;

 for(int32 y = 0; y < height; y++)
 {
  for(int32 x = 0; x < width; x++)
  {
   int32 s_y = (y + sourceY) & 511;
   int32 s_x = (x + sourceX) & 1023;
   int32 d_y = (y + destY) & 511;
   int32 d_x = (x + destX) & 1023;

   GPURAM[d_y][d_x] = GPURAM[s_y][s_x];
  }
 }

}

void PS_GPU::Command_FBWrite(const uint32 *cb)
{
 FBRW_X = CB[1] & 0xFFFF;
 FBRW_Y = CB[1] >> 16;
 FBRW_W = CB[2] & 0xFFFF;
 FBRW_H = CB[2] >> 16;

 if(FBRW_X & ~1023)
 {
  PSX_WARNING("[GPU] FB write X out of range?");
 }

 if(FBRW_Y & ~511)
 {
  PSX_WARNING("[GPU] FB write Y out of range?");
 }

 if((FBRW_X + FBRW_W) > 1024)
 {
  PSX_WARNING("[GPU] FB write width out of range?");
 }

 if((FBRW_Y + FBRW_H) > 512)
 {
  PSX_WARNING("[GPU] FB write height out of range?");
 }


 FBRW_CurX = FBRW_X;
 FBRW_CurY = FBRW_Y;

 InFBWrite = true;

 if(FBRW_W == 0 || FBRW_H == 0)
  InFBWrite = false;
}

void PS_GPU::Command_FBRead(const uint32 *cb)
{
 FBRW_X = CB[1] & 0xFFFF;
 FBRW_Y = CB[1] >> 16;
 FBRW_W = CB[2] & 0xFFFF;
 FBRW_H = CB[2] >> 16;


 if(FBRW_X & ~1023)
 {
  PSX_WARNING("[GPU] FB read X out of range?");
 }

 if(FBRW_Y & ~511)
 {
  PSX_WARNING("[GPU] FB read Y out of range?");
 }

 if((FBRW_X + FBRW_W) > 1024)
 {
  PSX_WARNING("[GPU] FB read width out of range?");
 }

 if((FBRW_Y + FBRW_H) > 512)
 {
  PSX_WARNING("[GPU] FB read height out of range?");
 }


 FBRW_CurX = FBRW_X;
 FBRW_CurY = FBRW_Y;

 InFBRead = true;

 if(FBRW_W == 0 || FBRW_H == 0)
  InFBRead = false;
}


void PS_GPU::RecalcTexWindowLUT(void)
{
 {
  uint8 targ_x = 0;
  uint8 targ_wc = 0;
  uint8 x = twx * 8;

  do
  {
   if(!targ_wc)
   {
    targ_x = twx * 8;
    targ_wc = tww * 8;
   }
   TexWindowXLUT[x] = targ_x;

   x++;
   targ_x++;
   targ_wc++;
  } while(x != (twx * 8));
 }

 {
  uint8 targ_y = 0;
  uint8 targ_hc = 0;
  uint8 y = twy * 8;

  do
  {
   if(!targ_hc)
   {
    targ_y = twy * 8;
    targ_hc = twh * 8;
   }
   //printf("Texture window y: %d %d\n", y, targ_y);
   TexWindowYLUT[y] = targ_y;

   y++;
   targ_y++;
   targ_hc++;
  } while(y != (twy * 8));
 }

 memset(TexWindowXLUT_Pre, TexWindowXLUT[0], sizeof(TexWindowXLUT_Pre));
 memset(TexWindowXLUT_Post, TexWindowXLUT[255], sizeof(TexWindowXLUT_Post));

 memset(TexWindowYLUT_Pre, TexWindowYLUT[0], sizeof(TexWindowYLUT_Pre));
 memset(TexWindowYLUT_Post, TexWindowYLUT[255], sizeof(TexWindowYLUT_Post));
}

void PS_GPU::Command_DrawMode(const uint32 *cb)
{
 TexPageX = (*cb & 0xF) * 64;
 TexPageY = (*cb & 0x10) * 16;

 SpriteFlip = *cb & 0x3000;

 abr = (*cb >> 5) & 0x3;
 TexMode = (*cb >> 7) & 0x3;

 dtd = (*cb >> 9) & 1;
 dfe = (*cb >> 10) & 1;
}

void PS_GPU::Command_TexWindow(const uint32 *cb)
{
 tww = (*cb & 0x1F);
 twh = ((*cb >> 5) & 0x1F);
 twx = ((*cb >> 10) & 0x1F);
 twy = ((*cb >> 15) & 0x1F);

 RecalcTexWindowLUT();
}

void PS_GPU::Command_Clip0(const uint32 *cb)
{
 ClipX0 = *cb & 1023;
 ClipY0 = (*cb >> 10) & 511;
}

void PS_GPU::Command_Clip1(const uint32 *cb)
{
 ClipX1 = *cb & 1023;
 ClipY1 = (*cb >> 10) & 511;
}

void PS_GPU::Command_DrawingOffset(const uint32 *cb)
{
 OffsX = sign_x_to_s32(11, (*cb & 2047));
 OffsY = sign_x_to_s32(10, ((*cb >> 11) & 1023));

 //PSX_WARNING("[GPU] Drawing offset: %d %d -- %d", OffsX, OffsY, scanline);
}

void PS_GPU::Command_MaskSetting(const uint32 *cb)
{
 //printf("Mask setting: %08x\n", *cb);
 MaskSetOR = (*cb & 1) ? 0x8000 : 0x0000;
 MaskEvalAND = (*cb & 2) ? 0x8000 : 0x0000;
}

void PS_GPU::Command_ClearCache(const uint32 *cb)
{

}

CTEntry PS_GPU::Commands[4][256] =
{
 #define BLENDMODE_MAC 0
 {
  #include "gpu_command_table.inc"
 },
 #undef BLENDMODE_MAC

 #define BLENDMODE_MAC 1
 {
  #include "gpu_command_table.inc"
 },
 #undef BLENDMODE_MAC

 #define BLENDMODE_MAC 2
 {
  #include "gpu_command_table.inc"
 },
 #undef BLENDMODE_MAC

 #define BLENDMODE_MAC 3
 {
  #include "gpu_command_table.inc"
 },
 #undef BLENDMODE_MAC
};

void PS_GPU::WriteCB(uint32 InData)
{
 //fputc(0, fp);
 //fwrite(&InData, 1, 4, fp);

 if(InFBWrite)
 {
  for(int i = 0; i < 2; i++)
  {
   if(FBRW_CurX < 1024 && FBRW_CurY < 512)
    GPURAM[FBRW_CurY][FBRW_CurX] = InData;

   FBRW_CurX++;
   if(FBRW_CurX == (FBRW_X + FBRW_W))
   {
    FBRW_CurX = FBRW_X;
    FBRW_CurY++;
    if(FBRW_CurY == (FBRW_Y + FBRW_H))
    {
     InFBWrite = false;
     break;
    }
   }
   InData >>= 16;
  }
  return;
 }

 if(InPLine && InData == 0x55555555)
 {
  InPLine = false;
  CBIn = 0;
  return;
 }

 CB[CBIn] = InData;
 //printf("%d, %02x -- %d -- %d\n", CBIn, CB[0] >> 24, Commands[CB[0] >> 24].len, InPLine);
 CBIn++;

 const uint32 cc = CB[0] >> 24;
 const CTEntry *command = &Commands[0][cc];

 if(CBIn >= command->len)
 {
  DrawTimeAvail -= 16;

  if(!command->func[TexMode])
  {
   if(CB[0]) 
    PSX_WARNING("[GPU] Unknown command: %08x, %d", CB[0], scanline);
   //printf("COMMAND: %s, ", command->name);
   //for(int i = 0; i < command->len; i++)
   // printf(" %08x ", CB[i]);
   //printf("\n");
  }
  else
  {
#if 0
   PSX_WARNING("[GPU] Command: %08x %s %d %d %d", CB[0], command->name, command->len, scanline, DrawTimeAvail);
   {
    int i;
    printf("[GPU]    ");
    for(i = 0; i < command->len; i++)
     printf("0x%08x ", CB[i]);
    printf("\n");
   }
#endif
   // A very very ugly kludge to support texture mode specialization. fixme/cleanup/SOMETHING in the future.
   if(cc >= 0x20 && cc <= 0x3F && (cc & 0x4))
   {
    uint32 tpage;

    tpage = CB[4 + ((cc >> 4) & 0x1)] >> 16;

    TexPageX = (tpage & 0xF) * 64;
    TexPageY = (tpage & 0x10) * 16;

    SpriteFlip = tpage & 0x3000;

    abr = (tpage >> 5) & 0x3;
    TexMode = (tpage >> 7) & 0x3;
   }

   command = &Commands[abr][cc];

   ((this)->*(command->func[TexMode | (MaskEvalAND ? 0x4 : 0x0)]))(CB);
  }

  if(InPLine)
  {
   //puts("Pew pew");
   if(CB[0] & 0x08000000)
    CB[0] &= ~0x08000000;
   else
   {
    CBIn -= 1 + ((cc & 0x10) >> 4);

    if(cc & 0x10)
    {
     CB[0] = (CB[0] & ~0xFFFFFF) | (CB[2] & 0xFFFFFF);
     CB[1] = CB[3];
    }
    else
     CB[1] = CB[2];
   }
  }
  else
   CBIn -= command->len;
 }
}

void PS_GPU::Write(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 if(A & 4)	// GP1 ("Control")
 {
  uint32 command = V >> 24;

  //fputc(1, fp);
  //fwrite(&V, 1, 4, fp);


  V &= 0x00FFFFFF;

  PSX_DBGINFO("[GPU] Control command: %02x %06x %d", command, V, scanline);

  switch(command)
  {
   default: PSX_WARNING("[GPU] Unknown control command %02x - %06x", command, V);
	    break;

   case 0x00:	// Reset GPU
	SoftReset();
	break;

   case 0x01:	// Reset command buffer
	CBIn = 0;
	InFBWrite = false;
	break;

   case 0x02: 	// Reset IRQ ???
   	break;

   case 0x03:	// Display enable
	DisplayOff = V & 1;
	break;

   case 0x04:	// DMA Setup
	DMAControl = V & 0x3;
	break;

   case 0x05:	// Start of display area in framebuffer
	DisplayFB_XStart = V & 0x3FF;
	DisplayFB_YStart = (V >> 10) & 0x1FF;
	break;

   case 0x06:	// Horizontal display range
	HorizStart = V & 0xFFF;
	HorizEnd = (V >> 12) & 0xFFF;
	break;

   case 0x07:
	VertStart = V & 0x3FF;
	VertEnd = (V >> 10) & 0x3FF;	// & 0x3FF or & 0x7FF?
	break;

   case 0x08:
	DisplayMode = V & 0xFF;
	break;

   case 0x10:	// GPU info(?)
	//printf("%08x\n", V);
	//assert(0);
	switch(V)
	{
	 default: PSX_WARNING("[GPU] Unknown control command GPU info param - %06x", V);
		  ControlQueryResponse = 0;
		  assert(0);
		  break;
#if 1
	 //case 0x0:
	 //case 0x1: PSX_WARNING("[GPU] Unknown control command GPU info param - %06x", V);

	 case 0x2: ControlQueryResponse = (tww << 0) | (twh << 5) | (twx << 10) | (twy << 15);
		   break;

	 case 0x3: ControlQueryResponse = (ClipY0 << 10) | ClipX0;
		   break;

 	 case 0x4: ControlQueryResponse = (ClipY1 << 10) | ClipX1;
		   break;

	 case 0x6: PSX_WARNING("[GPU] Unknown control command GPU info param - %06x", V);
	 case 0x5: ControlQueryResponse = (OffsX & 2047) | ((OffsY << 11) & 1023);
		   break;
#endif
	 case 0x7: ControlQueryResponse = 2;
		   break;
	}
	break;

  }
 }
 else		// GP0 ("Data")
 {
  //uint32 command = V >> 24;
  //printf("Meow command: %02x\n", command);
  WriteCB(V);
 }
}


void PS_GPU::WriteDMA(uint32 V)
{
 WriteCB(V);
}


uint32 PS_GPU::Read(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint32 ret = 0;

 if(A & 4)	// Status
 {
  ret = (((DisplayMode << 1) & 0x7F) | ((DisplayMode >> 6) & 1)) << 16;

  ret |= DMAControl << 29;

  ret |= field << 31; //FIXME: (DisplayFB_CurY & 1) << 31;

  if(DMAControl & 0x02)
   ret |= 1 << 25;

  ret |= DisplayOff << 23;

  if(!InFBRead && !InFBWrite && !DMA_GPUWriteActive() && DrawTimeAvail >= 0)
   ret |= 1 << 26;

  if(InFBRead)
   ret |= (1 << 27);

  // FIXME: GPU is not always ready to receive commands.
  if(!InFBRead && !InFBWrite && !DMA_GPUWriteActive() && DrawTimeAvail >= 0)
   ret |= (1 << 28);

  //
  //
  ret |= TexPageX >> 6;
  ret |= TexPageY >> 4;
  ret |= abr << 5;
  ret |= TexMode << 7;

  ret |= dtd << 9;
  ret |= dfe << 10;

  if(MaskSetOR)
   ret |= 1 << 11;

  if(MaskEvalAND)
   ret |= 1 << 12;


  //ret ^= rand() << 31;
  //ret ^= rand() << 26;
  //ret ^= rand() << 28;
  //ret &= ~(1 << 26);
  //if(scanline < 120)
  // ret |= 1 << 26;
 }
 else		// "Data"
 {
  if(InFBRead)
  {
   for(int i = 0; i < 2; i++)
   {
    if(FBRW_CurX < 1024 && FBRW_CurY < 512)
     ret |= GPURAM[FBRW_CurY][FBRW_CurX] << (i * 16);

    FBRW_CurX++;
    if(FBRW_CurX == (FBRW_X + FBRW_W))
    {
     FBRW_CurX = FBRW_X;
     FBRW_CurY++;
     if(FBRW_CurY == (FBRW_Y + FBRW_H))
     {
      InFBRead = false;
      break;
     }
    }
   }
  }
  else
  {
   ret = ControlQueryResponse;
   ControlQueryResponse = 0;	// TODO: confirm on real system
  }
 }

 return(ret);
}

INLINE void PS_GPU::ReorderRGB_Var(uint32 out_Rshift, uint32 out_Gshift, uint32 out_Bshift, bool bpp24, const uint16 *src, uint32 *dest, const int32 dx_start, const int32 dx_end, int32 fb_x)
{
     if(bpp24)	// 24bpp
     {
      for(int32 x = dx_start; x < dx_end; x++)
      {
       uint32 srcpix;

       srcpix = src[(fb_x >> 1) + 0] | (src[((fb_x >> 1) + 1) & 0x7FF] << 16);
       srcpix >>= (fb_x & 1) * 8;

       dest[x] = (((srcpix >> 0) << out_Rshift) & (0xFF << out_Rshift)) | (((srcpix >> 8) << out_Gshift) & (0xFF << out_Gshift)) |
       		 (((srcpix >> 16) << out_Bshift) & (0xFF << out_Bshift));

       fb_x = (fb_x + 3) & 0x7FF;
      }
     }				// 15bpp
     else
     {
      for(int32 x = dx_start; x < dx_end; x++)
      {
       uint32 srcpix = src[fb_x >> 1];

       //dest[x] = (((srcpix >> 0) << out_Rshift) & (0xFF << out_Rshift)) | (((srcpix >> 8) << out_Gshift) & (0xFF << out_Gshift)) |
       //		 (((srcpix >> 16) << out_Bshift) & (0xFF << out_Bshift));
       dest[x] = OutputLUT[srcpix & 0x7FFF];

       fb_x = (fb_x + 2) & 0x7FF;
      }
     }

}


template<uint32 out_Rshift, uint32 out_Gshift, uint32 out_Bshift>
void PS_GPU::ReorderRGB(bool bpp24, const uint16 *src, uint32 *dest, const int32 dx_start, const int32 dx_end, int32 fb_x)
{
 ReorderRGB_Var(out_Rshift, out_Gshift, out_Bshift, bpp24, src, dest, dx_start, dx_end, fb_x);
}

void PS_GPU::Update(const pscpu_timestamp_t sys_timestamp)
{
 static const uint32 DotClockRatios[5] = { 10, 8, 5, 4, 7 };
 const uint32 dmc = (DisplayMode & 0x40) ? 4 : (DisplayMode & 0x3);
 const uint32 dmw = 2720 / DotClockRatios[dmc];

 int32 sys_clocks = sys_timestamp - lastts;
 int32 gpu_clocks;
 int32 dot_clocks;

 assert(sys_clocks >= 0);

 if(!sys_clocks)
  return;

 DrawTimeAvail += sys_clocks << 1;

 //if(DrawTimeAvail > 8192)
 // DrawTimeAvail = 8192;

 if(DrawTimeAvail > 1024) //2048)
  DrawTimeAvail = 1024; //2048;


 if(DrawTimeAvail < -131072)
  DrawTimeAvail = -131072;

 // DrawTimeAvail = 65536; // Debug, remove

 //puts("GPU Update Start");

 GPUClockCounter += (uint64)sys_clocks * GPUClockRatio;

 gpu_clocks = GPUClockCounter >> 16;
 GPUClockCounter -= gpu_clocks << 16;

 DotClockCounter += gpu_clocks;
 dot_clocks = DotClockCounter / DotClockRatios[DisplayMode & 0x3];
 DotClockCounter -= dot_clocks * DotClockRatios[DisplayMode & 0x3];

 TIMER_AddDotClocks(dot_clocks);

 while(gpu_clocks > 0)
 {
  int32 chunk_clocks = gpu_clocks;

  if(chunk_clocks > LineClockCounter)
   chunk_clocks = LineClockCounter;

  gpu_clocks -= chunk_clocks;
  LineClockCounter -= chunk_clocks;

  if(!LineClockCounter)
  {
   if(PALMode)
    LineClockCounter = 3405;
   else
    LineClockCounter = 3412 + PhaseChange;

   scanline = (scanline + 1) % LinesPerField;
   PhaseChange = !PhaseChange;

   TIMER_ClockHRetrace();

   //if(scanline == 100)
	//DBG_Break();

   if(scanline == (LinesPerField - 1))
   {
    PSX_RequestMLExit();
   }


   if(scanline == 0)
   {
    IRQ_Assert(IRQ_VSYNC, true);
    IRQ_Assert(IRQ_VSYNC, false);
   }

   if(scanline == 0)
   {
    const bool OldFrameInterlaced = FrameInterlaced;

    DisplayHeightCounter = 0;

    PALMode = (bool)(DisplayMode & 0x08);

    if(PALMode)	// PAL
     VisibleStartLine = 72;
    else	// NTSC
     VisibleStartLine = 22;

    FrameInterlaced = (bool)(DisplayMode & 0x20);

    if(FrameInterlaced)
    {
     skip = false;

     if(OldFrameInterlaced)
     {
      field = !field;
     }

     if(PALMode)	// PAL
      LinesPerField = 313 - field;
     else			// NTSC
      LinesPerField = 263 - field;
    }
    else
    {
     field = 0;

     if(PALMode)	// PAL
      LinesPerField = 314;
     else			// NTSC
      LinesPerField = 263;
    }
    espec->InterlaceOn = FrameInterlaced; 
    espec->InterlaceField = field;
    DisplayRect->h = 240 << FrameInterlaced;
   }


   const int32 VS_Adjust = PALMode ? (34 - 7) : 6;

   if(scanline == (VertStart + VS_Adjust))
   {
    HeightMode = (bool)(DisplayMode & 0x04);
    DisplayFB_CurY = DisplayFB_YStart;
    if(FrameInterlaced && HeightMode)
     DisplayFB_CurY += field;

    DisplayHeightCounter = (VertEnd - VertStart);
   }
   //GPURAM[rand() & 511][rand() & 1023] = rand();
   if(scanline >= VisibleStartLine && scanline < (VisibleStartLine + 240) && !skip)
   {
    uint32 *dest;	// = surface->pixels + (scanline - VisibleStartLine) * surface->pitch32;
    int32 dest_line;
    int32 fb_x = DisplayFB_XStart * 2;
    int32 dx_start = HorizStart, dx_end = HorizEnd;

    dest_line = ((scanline - VisibleStartLine) << FrameInterlaced) + field;
    dest = surface->pixels + dest_line * surface->pitch32;

    if(dx_end < dx_start)
     dx_end = dx_start;

    dx_start = dx_start / DotClockRatios[dmc];
    dx_end = dx_end / DotClockRatios[dmc];

    dx_start -= 528 / DotClockRatios[dmc];
    dx_end -= 528 / DotClockRatios[dmc];

    if(dx_start < 0)
    {
     fb_x -= dx_start * ((DisplayMode & 0x10) ? 3 : 2);
     fb_x &= 0x7FF; //0x3FF;
     dx_start = 0;
    }

    if(dx_end > dmw)
     dx_end = dmw;

    if(!DisplayHeightCounter || DisplayOff)
     dx_start = dx_end = 0;


    LineWidths[dest_line].x = 0;
    LineWidths[dest_line].w = dmw;

    {
     const uint16 *src = GPURAM[DisplayFB_CurY & 0x1FF];
     const uint32 black = surface->MakeColor(0, 0, 0);

     for(int32 x = 0; x < dx_start; x++)
      dest[x] = black;

     //printf("%d %d %d - %d %d\n", scanline, dx_start, dx_end, HorizStart, HorizEnd);
     if(surface->format.Rshift == 0 && surface->format.Gshift == 8 && surface->format.Bshift == 16)
      ReorderRGB<0, 8, 16>(DisplayMode & 0x10, src, dest, dx_start, dx_end, fb_x);
     else if(surface->format.Rshift == 8 && surface->format.Gshift == 16 && surface->format.Bshift == 24)
      ReorderRGB<8, 16, 24>(DisplayMode & 0x10, src, dest, dx_start, dx_end, fb_x);
     else if(surface->format.Rshift == 16 && surface->format.Gshift == 8 && surface->format.Bshift == 0)
      ReorderRGB<16, 8, 0>(DisplayMode & 0x10, src, dest, dx_start, dx_end, fb_x);
     else if(surface->format.Rshift == 24 && surface->format.Gshift == 16 && surface->format.Bshift == 8)
      ReorderRGB<24, 16, 8>(DisplayMode & 0x10, src, dest, dx_start, dx_end, fb_x);
     else
      ReorderRGB_Var(surface->format.Rshift, surface->format.Gshift, surface->format.Bshift, DisplayMode & 0x10, src, dest, dx_start, dx_end, fb_x);

     for(int32 x = dx_end; x < dmw; x++)
      dest[x] = black;
    }
   }

   if(DisplayHeightCounter)
   {
    if(FrameInterlaced && HeightMode)
     DisplayFB_CurY = (DisplayFB_CurY + 2) & 0x1FF;
    else
     DisplayFB_CurY = (DisplayFB_CurY + 1) & 0x1FF;

    DisplayHeightCounter--;
   }

  }
 }

 //puts("GPU Update End");

 lastts = sys_timestamp;
}

void PS_GPU::StartFrame(EmulateSpecStruct *espec_arg)
{
 espec = espec_arg;

 surface = espec->surface;
 DisplayRect = &espec->DisplayRect;
 LineWidths = espec->LineWidths;
 skip = espec->skip;

 DisplayRect->x = 0;
 DisplayRect->y = 0;
 DisplayRect->w = 256;
 DisplayRect->h = 240;

 for(int i = 0; i < 240; i++)
 {
  LineWidths[i].x = 0;
  LineWidths[i].w = 0;
 }

 if(espec->VideoFormatChanged)
 {
  for(int rc = 0; rc < 0x8000; rc++)
  {
   uint32 r, g, b;

   r = ((rc >> 0) & 0x1F) * 255 / 31;
   g = ((rc >> 5) & 0x1F) * 255 / 31;
   b = ((rc >> 10) & 0x1F) * 255 / 31;
   OutputLUT[rc] = espec->surface->format.MakeColor(r, g, b, 0);
  }
 }
}

}
