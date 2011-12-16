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
#define MODE_PCE_FAST 5
#define MODE_LYNX     6
#define MODE_MD       7
#define MODE_PCFX     8
#define MODE_NGP      9
#define MODE_VB       10
#define MODE_WSWAN    11
#define MODE_GG       12

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

static unsigned short int MODE = MODE_NONE;

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
	    MODE = MODE_PCE_FAST;
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

	SceCtrlData pad;

	sceCtrlReadBufferPositive(&pad, 1); 

	u16 result = 0;

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
				default:
					break;
			}
		}
	}

	return result;

}
