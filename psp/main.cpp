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

#include "../src/drivers/main.h"
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <SDL/SDL.h>
#include <string.h>
#include <dirent.h>

#define printf pspDebugScreenPrintf

extern void MainRequestExit(void);

/* Define the module info section */
PSP_MODULE_INFO("mednafenPSP", 0, 1, 1);

/* Define the main thread's attribute value (optional) */
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* Exit callback */
int exit_callback(int arg1, int arg2, void *common) 
{
          MainRequestExit();
          sceKernelExitGame();
          return 0;
}

/* Callback thread */
int CallbackThread(SceSize args, void *argp) 
{
          int cbid;

          cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
          sceKernelRegisterExitCallback(cbid);

          sceKernelSleepThreadCB();

          return 0;
}

/* Sets up the callback thread and returns its thread id */
int SetupCallbacks(void) 
{
          int thid = 0;

          thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
          if(thid >= 0)
              sceKernelStartThread(thid, 0, 0);

          return thid;
}

using namespace std;

int getdir (string dir, vector<string> &files)
{
    DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(dir.c_str())) == NULL) 
    {
        return errno;
    }

    while ((dirp = readdir(dp)) != NULL) {
        files.push_back(string(dirp->d_name));
    }
    closedir(dp);
    return 0;
}

int main(int argc, char *argv[])
{
	pspDebugScreenInit();

	SetupCallbacks();

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    
	SceCtrlData pad;
	
    FILE *fp = new FILE();
    fp = fopen("output.txt", "w");
    fprintf(fp, "Hello World!\n");
    fclose(fp);

    bool launch = false;

	int placeholder = 2;

    string dir = string("ms0:/PSP/GAME/mednafenPSP/roms");
    vector<string> files = vector<string>();
    getdir(dir,files);

	bool printFlag = true, buttonFlag = false;

    while(!launch)
	{
		if(printFlag == true)
		{
		    pspDebugScreenClear();
		    sceDisplayWaitVblankStart();
		    for (unsigned int i = 0;i < files.size();i++) 
		    {
                if(placeholder == i)
				    pspDebugScreenPrintf("--> ");
        		pspDebugScreenPrintf("%s\n", files[i].c_str());
    		}
		printFlag = false;
		}

		while(!pad.Buttons)
		{
            buttonFlag = false;
            sceCtrlReadBufferPositive(&pad, 1); 
		}

		if(pad.Buttons != 0)
		{
			if(pad.Buttons & PSP_CTRL_CROSS)
				launch = true; 

			if(pad.Buttons & PSP_CTRL_UP)
			{
				if(buttonFlag == false)
				    placeholder--;
				printFlag = buttonFlag = true;
			}

			if(pad.Buttons & PSP_CTRL_DOWN)
			{
				if(buttonFlag == false)
				    placeholder++;
				printFlag = buttonFlag = true;
			}
			
			if(pad.Buttons & PSP_CTRL_RIGHT)
			{
				if(buttonFlag == false)
				    placeholder += 4;
				printFlag = buttonFlag = true;
			}
			
			if(pad.Buttons & PSP_CTRL_LEFT)
			{
				if(buttonFlag == false)
				    placeholder -= 4;
				printFlag = buttonFlag = true;
			}
			
			if(pad.Buttons & PSP_CTRL_RTRIGGER)
			{
				if(buttonFlag == false)
				    placeholder = files.size()-1;
				printFlag = buttonFlag = true;
			}
			
			if(pad.Buttons & PSP_CTRL_LTRIGGER)
			{
				if(buttonFlag == false)
				    placeholder = 2;
				printFlag = buttonFlag = true;
			}

			if(placeholder >= files.size())
				placeholder = 0;
			if(placeholder < 0)
				placeholder = files.size()-1; 
		}	

		sceDisplayWaitVblankStart();
		sceCtrlReadBufferPositive(&pad, 1); 
	}

    files[placeholder] = "ms0:/PSP/GAME/mednafenPSP/roms/" + files[placeholder];
	char*lol = (char*)files[placeholder].c_str();

	MEDNAFEN_main(argc, argv, lol);

	return 0;
}
