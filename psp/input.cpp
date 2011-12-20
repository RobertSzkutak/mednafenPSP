/*
Copyright (C) 2011
Robert L Szkutak II (http://robertszkutak.com)

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.      The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.      Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.      This notice may not be removed or altered from any source
distribution.
*/

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <string>
#include <stdio.h>

#include "input.h"

#define MODE_NONE     0
#define MODE_NES      1
#define MODE_SMS      2
#define MODE_GB       3
#define MODE_GBA      4
#define MODE_PCE      5
#define MODE_LYNX     6
#define MODE_MD       7
#define MODE_PCFX     8
#define MODE_NGP      9
#define MODE_VB       10
#define MODE_WSWAN    11
#define MODE_GG       MODE_SMS //Gamegear and SMS have identical key bindings

#define NES_A       0x0001
#define NES_B       0x0002
#define NES_SELECT  0x0004
#define NES_START   0x0008
#define NES_UP      0x0010
#define NES_DOWN    0x0020
#define NES_LEFT    0x0040
#define NES_RIGHT   0x0080

#define SMS_UP        0x0001
#define SMS_DOWN      0x0002
#define SMS_LEFT      0x0004
#define SMS_RIGHT     0x0008
#define SMS_FIRE1     0x0010
#define SMS_FIRE2     0x0020
#define SMS_PAUSE     0x0040

#define GB_A      0x0001
#define GB_B      0x0002
#define GB_START  0x0008
#define GB_SELECT 0x0004
#define GB_LEFT   0x0020
#define GB_RIGHT  0x0010
#define GB_UP     0x0040
#define GB_DOWN   0x0080

#define GBA_A      0x0001
#define GBA_B      0x0002
#define GBA_START  0x0008
#define GBA_SELECT 0x0004
#define GBA_RIGHT  0x0010
#define GBA_LEFT   0x0020
#define GBA_UP     0x0040
#define GBA_DOWN   0x0080
#define GBA_R      0x0100
#define GBA_L      0x0200

#define PCE_I      0x0001
#define PCE_II     0x0002
#define PCE_SELECT 0x0004
#define PCE_RUN    0x0008
#define PCE_RIGHT  0x0020
#define PCE_LEFT   0x0080
#define PCE_UP     0x0010
#define PCE_DOWN   0x0040
#define PCE_III    0x0100
#define PCE_IV     0x0200
#define PCE_V      0x0400
#define PCE_VI     0x0800

#define LYNX_A        0x0001
#define LYNX_B        0x0002
#define LYNX_OPT2     0x0004
#define LYNX_OPT1     0x0008
#define LYNX_LEFT     0x0010
#define LYNX_RIGHT    0x0020
#define LYNX_UP       0x0040
#define LYNX_DOWN     0x0080
#define LYNX_PAUSE    0x0100

#define MD_UP     0x0001
#define MD_DOWN   0x0002
#define MD_LEFT   0x0004
#define MD_RIGHT  0x0008
#define MD_B      0x0010
#define MD_C      0x0020
#define MD_A      0x0040
#define MD_START  0x0080
#define MD_Z      0x0100
#define MD_Y      0x0200
#define MD_X      0x0400
#define MD_MODE   0x0800

#define PCFX_I      0x0001
#define PCFX_II     0x0002
#define PCFX_III    0x0004
#define PCFX_IV     0x0008
#define PCFX_V      0x0010
#define PCFX_VI     0x0020
#define PCFX_SELECT 0x0040
#define PCFX_RUN    0x0080
#define PCFX_UP     0x0100
#define PCFX_RIGHT  0x0200
#define PCFX_DOWN   0x0400
#define PCFX_LEFT   0x0800

#define NGP_UP      0x0001
#define NGP_DOWN    0x0002
#define NGP_LEFT    0x0004
#define NGP_RIGHT   0x0008
#define NGP_A       0x0010
#define NGP_B       0x0020
#define NGP_OPTION  0x0040

#define VB_A          0x0001
#define VB_B          0x0002
#define VB_R          0x0004
#define VB_L          0x0008
#define VB_R_UP       0x0010
#define VB_R_RIGHT    0x0020
#define VB_L_RIGHT    0x0040
#define VB_L_LEFT     0x0080
#define VB_L_DOWN     0x0100
#define VB_L_UP       0x0200
#define VB_START      0x0400
#define VB_SELECT     0x0800
#define VB_R_LEFT     0x1000
#define VB_R_DOWN     0x2000

#define WSWAN_X1     0x0001
#define WSWAN_X2     0x0002
#define WSWAN_X3     0x0004
#define WSWAN_X4     0x0008
#define WSWAN_Y1     0x0010
#define WSWAN_Y2     0x0020
#define WSWAN_Y3     0x0040
#define WSWAN_Y4     0x0080
#define WSWAN_START  0x0100
#define WSWAN_A      0x0200
#define WSWAN_B      0x0400

static unsigned short int MODE = MODE_NONE;
static SceCtrlData pad;

void psp_set_input_mode(const char *name)
{
	
	std::string buff(name);
	
	if(buff.compare("nes") == 0)
	    MODE = MODE_NES;
	if(buff.compare("sms") == 0)
	    MODE = MODE_SMS;
	if(buff.compare("gb") == 0)
	    MODE = MODE_GB;
	if(buff.compare("gba") == 0)
	    MODE = MODE_GBA;
	if(buff.compare("pce_fast") == 0)
	    MODE = MODE_PCE;
	if(buff.compare("lynx") == 0)
	    MODE = MODE_LYNX;
	if(buff.compare("md") == 0)
	    MODE = MODE_MD;
	if(buff.compare("pcfx") == 0)
	    MODE = MODE_PCFX;
	if(buff.compare("ngp") == 0)
	    MODE = MODE_NGP;
	if(buff.compare("vb") == 0)
	    MODE = MODE_VB;
	if(buff.compare("wswan") == 0)
	    MODE = MODE_WSWAN;
	if(buff.compare("gg") == 0)
	    MODE = MODE_GG;
	
	FILE *fp = new FILE();
    fp = fopen("output.txt", "a");
    fprintf(fp, "PSP INPUT MODE: %d\n", MODE);
    fclose(fp);
}

u16 psp_mednafen_input()
{
	sceCtrlReadBufferPositive(&pad, 1); 

	u16 result = 0;
	
	//Buttons

	if(pad.Buttons != 0)
	{
		if (pad.Buttons & PSP_CTRL_START)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_START;
					break;
				case MODE_SMS:
					result |= SMS_PAUSE;
					break;
				case MODE_GB:
				    result |= GB_START;
					break;
				case MODE_GBA:
				    result |= GBA_START;
					break;
				case MODE_PCE:
					result |= PCE_RUN;
					break;
				case MODE_LYNX:
					result |= LYNX_PAUSE;
					break;
				case MODE_MD:
					result |= MD_START;
					break;
				case MODE_PCFX:
					result |= PCFX_RUN;
					break;
				case MODE_NGP:
				    result |= NGP_OPTION;
					break;
				case MODE_VB:
					result |= VB_START;
					break;
				case MODE_WSWAN:
					result |= WSWAN_START;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_SELECT)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_SELECT;
					break;
				case MODE_GB:
				    result |= GB_SELECT;
					break;
				case MODE_GBA:
				    result |= GBA_SELECT;
					break;
				case MODE_PCE:
					result |= PCE_SELECT;
					break;
				case MODE_MD:
					result |= MD_MODE;
					break;
				case MODE_PCFX:
					result |= PCFX_SELECT;
					break;
				case MODE_VB:
					result |= VB_SELECT;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_UP)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_UP;
					break;
				case MODE_SMS:
					result |= SMS_UP;
					break;
				case MODE_GB:
				    result |= GB_UP;
					break;
				case MODE_GBA:
				    result |= GBA_UP;
					break;
				case MODE_PCE:
					result |= PCE_UP;
					break;
				case MODE_LYNX:
					result |= LYNX_UP;
					break;
				case MODE_MD:
					result |= MD_UP;
					break;
				case MODE_PCFX:
					result |= PCFX_UP;
					break;
				case MODE_NGP:
				    result |= NGP_UP;
					break;
				case MODE_VB:
					result |= VB_L_UP;
					break;
				case MODE_WSWAN:
					result |= WSWAN_Y1;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_DOWN)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_DOWN;
					break;
				case MODE_SMS:
					result |= SMS_DOWN;
					break;
				case MODE_GB:
				    result |= GB_DOWN;
					break;
				case MODE_GBA:
				    result |= GBA_DOWN;
					break;
				case MODE_LYNX:
					result |= LYNX_DOWN;
					break;
				case MODE_PCE:
					result |= PCE_DOWN;
					break;
				case MODE_MD:
					result |= MD_DOWN;
					break;
				case MODE_PCFX:
					result |= PCFX_DOWN;
					break;
			    case MODE_NGP:
				    result |= NGP_DOWN;
					break;
				case MODE_VB:
					result |= VB_L_DOWN;
					break;
				case MODE_WSWAN:
					result |= WSWAN_Y3;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_LEFT)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_LEFT;
					break;
				case MODE_SMS:
					result |= SMS_LEFT;
					break;
				case MODE_GB:
				    result |= GB_LEFT;
					break;
				case MODE_GBA:
				    result |= GBA_LEFT;
					break;
				case MODE_LYNX:
					result |= LYNX_LEFT;
					break;
				case MODE_PCE:
					result |= PCE_LEFT;
					break;
				case MODE_MD:
					result |= MD_LEFT;
					break;
				case MODE_PCFX:
					result |= PCFX_LEFT;
					break;
				case MODE_NGP:
				    result |= NGP_LEFT;
					break;
				case MODE_VB:
					result |= VB_L_LEFT;
					break;
				case MODE_WSWAN:
					result |= WSWAN_Y4;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_RIGHT)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_RIGHT;
					break;
				case MODE_SMS:
					result |= SMS_RIGHT;
					break;
				case MODE_GB:
				    result |= GB_RIGHT;
					break;
				case MODE_GBA:
				    result |= GBA_RIGHT;
					break;
				case MODE_LYNX:
					result |= LYNX_RIGHT;
					break;
				case MODE_PCE:
					result |= PCE_RIGHT;
					break;
				case MODE_PCFX:
					result |= PCFX_RIGHT;
					break;
				case MODE_MD:
					result |= MD_RIGHT;
					break;
				case MODE_NGP:
				    result |= NGP_RIGHT;
					break;
				case MODE_VB:
					result |= VB_L_RIGHT;
					break;
				case MODE_WSWAN:
					result |= WSWAN_Y2;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_SQUARE)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_B;
					break;
				case MODE_SMS:
					result |= SMS_FIRE1;
					break;
				case MODE_GB:
				    result |= GB_B;
					break;
				case MODE_GBA:
				    result |= GBA_B;
					break;
				case MODE_LYNX:
					result |= LYNX_B;
					break;
				case MODE_PCE:
					result |= PCE_III;
					break;
				case MODE_MD:
					result |= MD_A;
					break;
				case MODE_PCFX:
					result |= PCFX_III;
					break;
				case MODE_NGP:
				    result |= NGP_B;
					break;
				case MODE_VB:
					result |= VB_B;
					break;
				case MODE_WSWAN:
					result |= WSWAN_B;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_CROSS)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_A;
					break;
				case MODE_SMS:
					result |= SMS_FIRE2;
					break;
				case MODE_GB:
				    result |= GB_A;
					break;
				case MODE_GBA:
				    result |= GBA_A;
					break;
				case MODE_LYNX:
					result |= LYNX_A;
					break;
				case MODE_PCE:
					result |= PCE_II;
					break;
				case MODE_MD:
					result |= MD_B;
					break;
				case MODE_PCFX:
					result |= PCFX_II;
					break;
				case MODE_NGP:
				    result |= NGP_A;
					break;
				case MODE_VB:
					result |= VB_A;
					break;
				case MODE_WSWAN:
					result |= WSWAN_A;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_TRIANGLE)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_B;
					break;
				case MODE_SMS:
					result |= SMS_FIRE1;
					break;
				case MODE_GB:
				    result |= GB_B;
					break;
				case MODE_GBA:
				    result |= GBA_B;
					break;
				case MODE_LYNX:
					result |= LYNX_B;
					break;
				case MODE_PCE:
					result |= PCE_V;
					break;
				case MODE_MD:
					result |= MD_Y;
					break;
				case MODE_PCFX:
					result |= PCFX_V;
					break;
				case MODE_NGP:
				    result |= NGP_B;
					break;
				case MODE_VB:
					result |= VB_B;
					break;
				case MODE_WSWAN:
					result |= WSWAN_B;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_CIRCLE)
		{
			switch(MODE)
			{
				case MODE_NES:
					result |= NES_A;
					break;
				case MODE_SMS:
					result |= SMS_FIRE2;
					break;
				case MODE_GB:
				    result |= GB_A;
					break;
				case MODE_GBA:
				    result |= GBA_A;
					break;
				case MODE_LYNX:
					result |= LYNX_A;
					break;
				case MODE_PCE:
					result |= PCE_III;
					break;
				case MODE_MD:
					result |= MD_C;
					break;
				case MODE_PCFX:
					result |= PCFX_III;
					break;
				case MODE_NGP:
				    result |= NGP_A;
					break;
				case MODE_VB:
					result |= VB_A;
					break;
				case MODE_WSWAN:
					result |= WSWAN_A;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_LTRIGGER)
		{
			switch(MODE)
			{
				case MODE_GBA:
					result |= GBA_L;
					break;
				case MODE_LYNX:
					result |= LYNX_OPT1;
					break;
				case MODE_PCE:
					result |= PCE_IV;
					break;
				case MODE_MD:
					result |= MD_X;
					break;
				case MODE_PCFX:
					result |= PCFX_IV;
					break;
				case MODE_VB:
					result |= VB_L;
					break;
				default:
					break;
			}
		}
		if (pad.Buttons & PSP_CTRL_RTRIGGER)
		{
			switch(MODE)
			{
				case MODE_GBA:
					result |= GBA_R;
					break;
				case MODE_LYNX:
					result |= LYNX_OPT2;
					break;
				case MODE_PCE:
					result |= PCE_VI;
					break;
				case MODE_MD:
					result |= MD_Z;
					break;
				case MODE_PCFX:
					result |= PCFX_VI;
					break;
				case MODE_VB:
					result |= VB_R;
					break;
				default:
					break;
			}
		}
	}
	
	//Thumbstick
	
	if (pad.Ly-128 < -75)
	{
		switch(MODE)
		{
			case MODE_NES:
				result |= NES_UP;
				break;
			case MODE_SMS:
				result |= SMS_UP;
				break;
			case MODE_GB:
			    result |= GB_UP;
				break;
			case MODE_GBA:
				result |= GBA_UP;
			    break;
			case MODE_LYNX:
				result |= LYNX_UP;
				break;
			case MODE_PCE:
				result |= PCE_UP;
				break;
			case MODE_MD:
				result |= MD_UP;
				break;
			case MODE_PCFX:
				result |= PCFX_UP;
				break;
			case MODE_NGP:
			    result |= NGP_UP;
				break;
			case MODE_VB:
				result |= VB_R_UP;
				break;
			case MODE_WSWAN:
				result |= WSWAN_X1;
				break;
			default:
				break;
		}
	}
	else if (pad.Ly-128 > 75)
	{
		switch(MODE)
		{
			case MODE_NES:
				result |= NES_DOWN;
				break;
			case MODE_SMS:
				result |= SMS_DOWN;
				break;
			case MODE_GB:
				result |= GB_DOWN;
				break;
			case MODE_GBA:
				result |= GBA_DOWN;
			    break;
			case MODE_LYNX:
				result |= LYNX_DOWN;
				break;
			case MODE_PCE:
				result |= PCE_DOWN;
				break;
			case MODE_MD:
				result |= MD_DOWN;
				break;
			case MODE_PCFX:
				result |= PCFX_DOWN;
				break;
		    case MODE_NGP:
			    result |= NGP_DOWN;
				break;
			case MODE_VB:
				result |= VB_R_DOWN;
				break;
			case MODE_WSWAN:
				result |= WSWAN_X3;
				break;
			default:
				break;
		}
	}
	if (pad.Lx-128 < -75)
	{
		switch(MODE)
		{
			case MODE_NES:
				result |= NES_LEFT;
				break;
			case MODE_SMS:
				result |= SMS_LEFT;
				break;
			case MODE_GB:
			    result |= GB_LEFT;
				break;
		    case MODE_GBA:
			    result |= GBA_LEFT;
				break;
			case MODE_LYNX:
				result |= LYNX_LEFT;
				break;
			case MODE_PCE:
				result |= PCE_LEFT;
				break;
			case MODE_MD:
				result |= MD_LEFT;
				break;
			case MODE_PCFX:
				result |= PCFX_LEFT;
				break;
			case MODE_NGP:
			    result |= NGP_LEFT;
				break;
			case MODE_VB:
				result |= VB_R_LEFT;
				break;
			case MODE_WSWAN:
				result |= WSWAN_X4;
				break;
			default:
				break;
		}
	}
	else if (pad.Lx-128 > 75)
	{
		switch(MODE)
		{
			case MODE_NES:
				result |= NES_RIGHT;
				break;
			case MODE_SMS:
				result |= SMS_RIGHT;
				break;
			case MODE_GB:
			    result |= GB_RIGHT;
				break;
			case MODE_GBA:
				result |= GBA_RIGHT;
				break;
			case MODE_LYNX:
				result |= LYNX_RIGHT;
				break;
			case MODE_PCE:
				result |= PCE_RIGHT;
				break;
			case MODE_MD:
				result |= MD_RIGHT;
				break;
			case MODE_PCFX:
				result |= PCFX_RIGHT;
				break;
			case MODE_NGP:
			    result |= NGP_RIGHT;
				break;
			case MODE_VB:
				result |= VB_R_RIGHT;
				break;
			case MODE_WSWAN:
				result |= WSWAN_X2;
				break;
			default:
				break;
		}
	}

	return result;

}
