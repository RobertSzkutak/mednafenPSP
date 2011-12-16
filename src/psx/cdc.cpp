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
	Implement missing commands.

	Allow reading in the leadout area.

	Determine size of results and IRQ FIFOs/queues, and handle overflow conditions to prevent an assert()

	What happens when CD-DA streaming to the SPU is enabled, but the SPU is turned off?

*/

namespace MDFN_IEN_PSX
{

// 00 - 2048
// 01 - 2328
// 02 - 2060
// 03 - 2340


PS_CDC::PS_CDC() : Results(64), IRQQueue(256), CDDABuffer(8192), DMABuffer(2340)
{
 uint8 buf[2048];

 if(!CDIF_ReadTOC(&toc))
  throw(1);	// FIXME

 IsPSXDisc = false;

 if(CDIF_ReadSector(buf, 4, 1) == 0x2 && !strncmp((char *)buf + 10, "Licensed  by", strlen("Licensed  by")))
  IsPSXDisc = true;
}

PS_CDC::~PS_CDC()
{

}

void PS_CDC::Power(void)
{
 Control = 0;
 DriveStatus = DS_STOPPED;
 PSRCounter = 0;

 Muted = false;
 Forward = false;
 Backward = false;

 Mode = 0;

 memset(ArgsBuf, 0, sizeof(ArgsBuf));
 ArgsIn = 0;

 Results.Flush();

 IRQQueue.Flush();
 RecalcIRQ();

 PendingCommand = 0;
 PendingCommandCounter = 0;

 CDDABuffer.Flush();
 DMABuffer.Flush();
 SB_In = false;

 memset(SubQBuf, 0, sizeof(SubQBuf));
 memset(SubQBuf_Safe, 0, sizeof(SubQBuf_Safe));
 SubQChecksumOK = false;

 lastts = 0;
}

void PS_CDC::ResetTS(void)
{
 lastts = 0;
}

void PS_CDC::RecalcIRQ(void)
{
 IRQ_Assert(IRQ_CD, (bool)IRQQueue.CanRead());
}

void PS_CDC::WriteIRQ(uint8 V)
{
 if(!IRQQueue.CanWrite())
 {
  PSX_WARNING("[CDC] IRQ FIFO is full!");
 }
 else
 {
  IRQQueue.WriteByte(V);
  RecalcIRQ();
 }
}

void PS_CDC::WriteResult(uint8 V)
{
 if(!Results.CanWrite())
 {
  PSX_WARNING("[CDC] Result FIFO is full!");
 }
 else
  Results.WriteByte(V);
}


uint8 PS_CDC::MakeStatus(bool cmd_error)
{
 uint8 ret = 0;

 // Are these bit positions right?

 if(DriveStatus == DS_PLAYING || DriveStatus == DS_PLAY_SEEKING)
  ret |= 0x80;

 if(DriveStatus == DS_SEEKING || DriveStatus == DS_PLAY_SEEKING) // Not sure about DS_PLAY_SEEKING
  ret |= 0x40;

 if(DriveStatus == DS_READING)
  ret |= 0x20;

 // TODO: shell open and seek error

 if(DriveStatus != DS_STOPPED)
  ret |= 0x02;

 if(cmd_error)
  ret |= 0x01;

 return(ret);
}

void PS_CDC::DecodeSubQ(uint8 *subpw)
{
 uint8 tmp_q[0xC];

 memset(tmp_q, 0, 0xC);

 for(int i = 0; i < 96; i++)
  tmp_q[i >> 3] |= ((subpw[i] & 0x40) >> 6) << (7 - (i & 7));

 if((tmp_q[0] & 0xF) == 1)
 {
  memcpy(SubQBuf, tmp_q, 0xC);
  SubQChecksumOK = CDIF_CheckSubQChecksum(tmp_q);

  if(SubQChecksumOK)
   memcpy(SubQBuf_Safe, tmp_q, 0xC);
 }
}


void PS_CDC::Update(const pscpu_timestamp_t timestamp)
{
 int32 clocks = timestamp - lastts;

 while(clocks > 0)
 {
  int32 chunk_clocks = clocks;

  if(PSRCounter > 0 && chunk_clocks > PSRCounter)
   chunk_clocks = PSRCounter;

  if(PendingCommandCounter > 0 && chunk_clocks > PendingCommandCounter)
   chunk_clocks = PendingCommandCounter;

  if(PSRCounter > 0)
  {
   PSRCounter -= chunk_clocks;

   if(PSRCounter <= 0) 
   {
    if(DriveStatus == DS_SEEKING)
    {
     CurSector = SeekTarget;
     DriveStatus = DS_PAUSED;

     WriteResult(MakeStatus());
     WriteIRQ(CDCIRQ_COMPLETE);
    }
    else if(DriveStatus == DS_PLAY_SEEKING)
    {
     CurSector = SeekTarget;

     DriveStatus = DS_PLAYING;
     PSRCounter = 33868800 / 75;
    }
    else if(DriveStatus == DS_READING)
    {
     if(CurSector >= toc.tracks[100].lba)
     {
	PSX_WARNING("[CDC] Beyond end!");
      DriveStatus = DS_STOPPED;

      //WriteResult(MakeStatus());
      WriteIRQ(CDCIRQ_DATA_END);
     }
     else
     {
      if(SB_In && 0)
      {
       PSRCounter += 33868800 / (75 * ((Mode & MODE_SPEED) ? 2 : 1));
      }
      else
      {
       uint8 buf[2352 + 96];
       bool xa_sector = (bool)(Mode & MODE_STRSND);


       CDIF_ReadRawSector(buf, CurSector);	// FIXME: error out on error.
       DecodeSubQ(buf + 2352);

       if(!(Mode & 0x10))
	if(!CDIF_ValidateRawSector(buf))	// FIXME: error out on error
	{
	 //exit(1);
	}

       //if(buf[12 + 3] != 0x2)
//	xa_sector = false;

       if(!(buf[12 + 6] & 0x4))
	xa_sector = false;

       if((Mode & MODE_SF) && (buf[12 + 4] != FilterFile || buf[12 + 5] != FilterChan))
	xa_sector = false;

       if(buf[12 + 6] & 0x4)
	PSX_DBGINFO("[CDC] MEOW: %02x -- %02x", buf[12 + 3], Mode);

       PSX_DBGINFO("[CDC] Reading sector: %d %s", CurSector, xa_sector ? "(XA audio)" : "");

       if(xa_sector)
       {
	if(!(Mode & MODE_SF) || (buf[12 + 4] == FilterFile && buf[12 + 5] == FilterChan))
	{
	 PSX_DBGINFO("[CDC] XA Sector: %d\n", CurSector);
	}
       }
       else
       {
        memcpy(SB, buf + 12, 2340);
        SB_In = true;

        //if(Mode & MODE_REPORT)
        {
 	 WriteResult(MakeStatus());
         WriteIRQ(CDCIRQ_DATA_READY);
        }
       }

       if((Mode & MODE_AUTOPAUSE) && (buf[12 + 6] & 0x80) && 0)
       {
#if 1
	 DriveStatus = DS_PAUSED;
	 PSRCounter = 0;

//	 WriteResult(MakeStatus());
//	 WriteIRQ(CDCIRQ_ACKNOWLEDGE);
	 WriteResult(MakeStatus());
	 WriteIRQ(CDCIRQ_COMPLETE);
#endif
	PSRCounter = 0;
	//exit(1);
       }
       else
       {
	PSRCounter += 33868800 / (75 * ((Mode & MODE_SPEED) ? 2 : 1));
        CurSector++;
       }
      }
     }
    }
    else if(DriveStatus == DS_PLAYING)
    {
     if(CurSector >= toc.tracks[100].lba)
     {
      DriveStatus = DS_STOPPED;
      //WriteResult(MakeStatus());
      WriteIRQ(CDCIRQ_DATA_END);
     }
     else
     {
      if(CDDABuffer.CanWrite() >= 2352)
      {
       uint8 buf[2352 + 96];

       CDIF_ReadRawSector(buf, CurSector);	// FIXME: error out on error.
       DecodeSubQ(buf + 2352);

       if(PlayTrackMatch == -1)
	PlayTrackMatch = SubQBuf_Safe[0x1];

       if(Mode & 0x01)
       {
	if(Muted || (SubQBuf_Safe[0] & 0x40))
	{
	 for(int i = 0; i < 2352; i++)
	  CDDABuffer.WriteByte(0);
	}
	else
         CDDABuffer.Write(buf, 2352);
       }

       PSRCounter += 33868800 / 75;

       if((Mode & MODE_AUTOPAUSE) && PlayTrackMatch != -1 && SubQBuf_Safe[0x1] != PlayTrackMatch)
       {
	MDFN_DispMessage("Autopause");
	DriveStatus = DS_PAUSED;
	PSRCounter = 0;
	 //WriteResult(MakeStatus());
	 //WriteIRQ(CDCIRQ_ACKNOWLEDGE);
	 WriteResult(MakeStatus());
         WriteIRQ(CDCIRQ_DATA_END);
	// WriteIRQ(CDCIRQ_COMPLETE);
	PSRCounter = 0;
       }
       else if((Mode & MODE_REPORT) && (!(SubQBuf_Safe[0x9] & 0xF) || Forward || Backward))
       {
	//PSX_WARNING("[CDC] CD-DA DATA READY");

        WriteResult(MakeStatus());
	WriteResult(SubQBuf_Safe[0x1]);	// Track
	WriteResult(SubQBuf_Safe[0x2]);	// Index

	if(SubQBuf_Safe[0x9] & 0x10)
	{
	 WriteResult(SubQBuf_Safe[0x3]);	// R M
	 WriteResult(SubQBuf_Safe[0x4] | 0x80);	// R S
 	 WriteResult(SubQBuf_Safe[0x5]);	// R F
	}
	else	
	{
	 WriteResult(SubQBuf_Safe[0x7]);	// A M
	 WriteResult(SubQBuf_Safe[0x8]);	// A S
	 WriteResult(SubQBuf_Safe[0x9]);	// A F
	}

	WriteResult(0);	// ???
	WriteResult(0);	// ???

        WriteIRQ(CDCIRQ_DATA_READY);
       }

	// FIXME: What's the real fast-forward and backward speed?
       if(Forward)
        CurSector += 12;
       else if(Backward)
       {
        CurSector -= 12;

        if(CurSector < 0)	// FIXME: How does a real PS handle this condition?
         CurSector = 0;
       }
       else
        CurSector++;
      }
      else
       PSX_WARNING("[CDC] BUG CDDA buffer full");

     }
    } // end if playing
   }
  }

  if(PendingCommandCounter > 0)
  {
   PendingCommandCounter -= chunk_clocks;

   if(PendingCommandCounter <= 0)
   {
    const CDC_CTEntry *command = Commands;
    int32 next_time = 0;

    while(command->name)
    {
     if(command->command == PendingCommand)
      break;
     command++;
    }

    if(command->func)
    {
     if(!PendingCommandPhase)
     {
	Results.Flush();
	assert(!Results.CanRead());
      //PSX_WARNING("[CDC] Command: %s --- %d", command->name, Results.CanRead());
      printf("[CDC] Command: %s --- ", command->name);
      for(int i = 0; i < ArgsIn; i++)
       printf(" 0x%02x", ArgsBuf[i]);
      printf("\n");
      next_time = (this->*(command->func))(ArgsIn, ArgsBuf);
      ArgsIn = 0; // Maybe?  Can args for more than one command be queued up?
      PendingCommandPhase = 1;
     }
     else
     {
      //PSX_WARNING("[CDC] Command PartN: %s", command->name);
      next_time = (this->*(command->func2))();
     }
    }
    else
    {
     PSX_WARNING("[CDC] Unknown command: 0x%02x", PendingCommand);
    }

    if(!next_time)
     PendingCommandCounter = 0;
    else
     PendingCommandCounter += next_time;
   }
  }

  clocks -= chunk_clocks;
 } // end while(clocks > 0)

 lastts = timestamp;
}

void PS_CDC::Write(const pscpu_timestamp_t timestamp, uint32 A, uint8 V)
{
 //PSX_WARNING("[CDC] Write %d 0x%02x @ %d", A, V, timestamp);
 switch(A)
 {
  case 0:
	// D0 - 0 = command send, 1 = result receive?
	Control = V & 0x3;
	break;

  case 1:
	if(Control & 0x2)
	 break;

	if(!(Control & 0x1))
	{
         PendingCommandCounter = 1024;
	 PendingCommand = V & 0x1F;
         PendingCommandPhase = 0;
	}
	break;

  case 2:
	if(Control & 0x2)
	 break;

	if(Control & 0x1)
	{
	 if(V == 0x07)
	 {
	  ArgsIn = 0;
	  Results.Flush();
	 }
	}
	else
	{
	 if(ArgsIn < 16)
	  ArgsBuf[ArgsIn++] = V;
	}
	break;

  case 3:
	if(Control & 0x1)
	{
	 if(V == 0x07)
	 {
	  //PSX_WARNING("[CDC] IRQ Queue Flush");
	  //IRQQueue.Flush();
	  if(IRQQueue.CanRead())
	   IRQQueue.ReadByte();

	  RecalcIRQ();
	 }
	}
	else
	{
	 if(V == 0x80)
	 {
	  int32 offs = (Mode & 0x20) ? 0 : 12;

	  PSX_DBGINFO("[CDC] DMA start bit?");

	  if(SB_In)
	  {
	   SB_In = false; 
	   DMABuffer.Flush();
	   DMABuffer.Write(SB + offs, 2340 - offs);
	  }

	 }
	 // Unknown, probably D7 has something to do with DMA.
	}	
	break;
 }
}

uint8 PS_CDC::Read(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint8 ret = 0;


 switch(A)
 {
  case 0:
	// D5 - 1 = Result ready?
	// D6 - 1 = DMA ready?
	// D7 - 1 = command being processed
	//ret |= 0x18;	// ???

	ret = Control & 0x3;

	ret |= 0x18;

	if(Results.CanRead())
	 ret |= 0x20;

	if(DMABuffer.CanRead()) //SB_In)
	 ret |= 0x40;

        if(PendingCommandCounter > 0)
	 ret |= 0x80;

	break;

  case 1:
	if(Control & 0x01)
	{
	 if(Results.CanRead())
	  ret = Results.ReadByte();
	}
	else
	 assert(0);
	break;

  case 2:
	PSX_DBGINFO("[CDC] Read 2");
	if(DMABuffer.CanRead())
	 ret = DMABuffer.ReadByte();
	break;

  case 3:
	if(Control & 0x1)
	{
	 if(IRQQueue.CanRead())
	  ret = IRQQueue.ReadByte(true);
	 else
	  ret = CDCIRQ_NONE;
	}
	else
	{
	 PSX_WARNING("[CDC] Read from reg 3 when D0 of Control is zero?");
	 ret = 0; //0xFF;
	}
	// assert(0);

	RecalcIRQ();
	break;
 }

 //PSX_WARNING("[CDC] Read %d 0x%02x", A, ret);
 return(ret);
}


bool PS_CDC::DMACanRead(void)
{
 return(DMABuffer.CanRead());
}

//  if(SB_RP >= ((Mode & 0x10) ? 2340 : 2060) )
bool PS_CDC::DMARead(uint32 &data)
{
 data = 0;

 for(int i = 0; i < 4; i++)
 {
  if(DMABuffer.CanRead())
   data |= DMABuffer.ReadByte() << (i * 8);
 }

 return(DMABuffer.CanRead());
}


int32 PS_CDC::Command_Sync(const int arg_count, const uint8 *args)
{
 PSX_WARNING("[CDC] Unimplemented command: 0x%02x", PendingCommand);
 return(0);
}

int32 PS_CDC::Command_Nop(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_Setloc(const int arg_count, const uint8 *args)
{
 uint8 m, s, f;

 m = BCD_TO_INT(args[0] & 0x7F);
 s = BCD_TO_INT(args[1]);
 f = BCD_TO_INT(args[2]);

 CommandLoc = f + 75 * s + 75 * 60 * m - 150;

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_Play(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 Forward = Backward = false;

 if(arg_count && args[0])
 {
  int track = BCD_TO_INT(args[0]);

  if(track < toc.first_track || track > toc.last_track)
  {
   // FIXME: error condition here
   assert(0);
  }
  else
  {
   PlayTrackMatch = track;

   printf("[CDC] Play track: %d\n", track);
   SeekTarget = toc.tracks[track].lba;
   PSRCounter = 33868;
   DriveStatus = DS_PLAY_SEEKING;
  }
 }
 else
 {
  //if(DriveStatus != DS_PLAYING)
  //{
   CurSector = CommandLoc;
   PlayTrackMatch = -1;
  //}

  DriveStatus = DS_PLAYING;
  PSRCounter = 33868800 / 75;
 }

 return(0);
}

int32 PS_CDC::Command_Forward(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 Backward = false;
 Forward = true;

 return(0);
}

int32 PS_CDC::Command_Backward(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 Backward = true;
 Forward = false;

 return(0);
}

int32 PS_CDC::Command_ReadN(const int arg_count, const uint8 *args)
{
 DMABuffer.Flush();
 SB_In = false;

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 CurSector = CommandLoc;

 DriveStatus = DS_READING;
 PSRCounter = 33868800 / (75 * ((Mode & MODE_SPEED) ? 2 : 1));

 return(0);
}

int32 PS_CDC::Command_Standby(const int arg_count, const uint8 *args)
{
 return(0);
}

int32 PS_CDC::Command_Stop(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(33868);
}

int32 PS_CDC::Command_Stop_Part2(void)
{
 DriveStatus = DS_STOPPED;
 PSRCounter = 0;

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_COMPLETE);

 return(0);
}


int32 PS_CDC::Command_Pause(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(33868);
}

int32 PS_CDC::Command_Pause_Part2(void)
{
 DriveStatus = DS_PAUSED;
 PSRCounter = 0;

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_COMPLETE);

 return(0);
}

int32 PS_CDC::Command_Reset(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(33868);
}

int32 PS_CDC::Command_Reset_Part2(void)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_COMPLETE);

 Muted = false; // Does it get reset here?
 CDDABuffer.Flush();

 DMABuffer.Flush();
 SB_In = false;

 Mode = 0;
 CurSector = 0;
 CommandLoc = 0;
 PSRCounter = 0;
 DriveStatus = DS_PAUSED;

 return(0);
}

int32 PS_CDC::Command_Mute(const int arg_count, const uint8 *args)
{
 Muted = true;

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_Demute(const int arg_count, const uint8 *args)
{
 Muted = false;

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_Setfilter(const int arg_count, const uint8 *args)
{
 FilterFile = args[0];
 FilterChan = args[1];

 //PSX_WARNING("[CDC] Setfilter: %02x %02x", args[0], args[1]);

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_Setmode(const int arg_count, const uint8 *args)
{
 PSX_DBGINFO("[CDC] Set mode 0x%02x", args[0]);
 Mode = args[0];

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_Getparam(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteResult(Mode);
 WriteResult(0x00);
 WriteResult(FilterFile);
 WriteResult(FilterChan);

 WriteIRQ(CDCIRQ_ACKNOWLEDGE);


 return(0);
}

int32 PS_CDC::Command_GetlocL(const int arg_count, const uint8 *args)
{
 PSX_WARNING("[CDC] Unimplemented command: 0x%02x", PendingCommand);
 return(0);
}

int32 PS_CDC::Command_GetlocP(const int arg_count, const uint8 *args)
{
 WriteResult(SubQBuf_Safe[0x1]);	// Track
 WriteResult(SubQBuf_Safe[0x2]);	// Index
 WriteResult(SubQBuf_Safe[0x3]);	// R M
 WriteResult(SubQBuf_Safe[0x4]);	// R S
 WriteResult(SubQBuf_Safe[0x5]);	// R F
 WriteResult(SubQBuf_Safe[0x7]);	// A M
 WriteResult(SubQBuf_Safe[0x8]);	// A S
 WriteResult(SubQBuf_Safe[0x9]);	// A F

 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_ReadT(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(44100 * 768 / 1000);
}

int32 PS_CDC::Command_ReadT_Part2(void)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_COMPLETE);

 return(0);
}


// NOTE: For example, converts an LBA of 0 to an MSF of 00:02:00. (a +150 offset to LBA)
static void lba2msf(uint32 lba, uint8 * m, uint8 * s, uint8 * f)
{
 lba += 150;

 *m = lba / 75 / 60;
 *s = (lba - *m * 75 * 60) / 75;
 *f = lba - (*m * 75 * 60) - (*s * 75);
}

int32 PS_CDC::Command_GetTN(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteResult(INT_TO_BCD(toc.first_track));
 WriteResult(INT_TO_BCD(toc.last_track - toc.first_track + 1));

 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_GetTD(const int arg_count, const uint8 *args)
{
 int track;
 uint8 m, s, f;

 if(!args[0] || args[0] == 0xAA)
  track = 100;
 else
 {
  track= BCD_TO_INT(args[0]);

  if(track < toc.first_track || track > toc.last_track)	// Error
  {
   WriteResult(MakeStatus(true));
   WriteIRQ(CDCIRQ_ACKNOWLEDGE);
   return(0);
  }
 }

 //PSX_WARNING("[CDC] GetTD %d", track);

 lba2msf(toc.tracks[track].lba, &m, &s, &f);

 WriteResult(MakeStatus());
 WriteResult(INT_TO_BCD(m));
 WriteResult(INT_TO_BCD(s));
 //WriteResult(INT_TO_BCD(f));

 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 return(0);
}

int32 PS_CDC::Command_SeekL(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 SeekTarget = CommandLoc;

 PSRCounter = 33868;
 DriveStatus = DS_SEEKING;

 return(0);
}

int32 PS_CDC::Command_SeekP(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 SeekTarget = CommandLoc;

 PSRCounter = 33868;
 DriveStatus = DS_SEEKING;

 return(0);
}

int32 PS_CDC::Command_Test(const int arg_count, const uint8 *args)
{
 //PSX_WARNING("[CDC] Test command sub-operation: 0x%02x", args[0]);

 switch(args[0])
 {
  default:
	PSX_WARNING("[CDC] Unknown Test command sub-operation: 0x%02x", args[0]);
	assert(0);
	break;

  //case 0x04:	// Read SCEx counter
  //	break;

  //case 0x05:	// Reset SCEx counter
  //	break;

  case 0x20:
	{
	 uint8 rd[4] = { 0, 0, 0, 0 };

	 Results.Write(rd, 4);
	 WriteIRQ(CDCIRQ_ACKNOWLEDGE);
	}
	break;
 }
 return(0);
}

int32 PS_CDC::Command_ID(const int arg_count, const uint8 *args)
{
 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 PSRCounter = 0;
 DriveStatus = DS_PAUSED;

 return(33868);
}

int32 PS_CDC::Command_ID_Part2(void)
{
 if(IsPSXDisc)
 {
  WriteResult(0);
  WriteResult(0);
 }
 else
 {
  WriteResult(0x08);
  WriteResult(0x90);
 }

 WriteResult(0);
 WriteResult(0);

 if(IsPSXDisc)
 {
  WriteResult('S');
  WriteResult('C');
  WriteResult('E');
  WriteResult('A');
 }
 else
 {
  WriteResult(0);
  WriteResult(0);
  WriteResult(0);
  WriteResult(0);
 }

 WriteIRQ(CDCIRQ_COMPLETE);

 return(0);
}


int32 PS_CDC::Command_ReadS(const int arg_count, const uint8 *args)
{
 DMABuffer.Flush();
 SB_In = false;

 WriteResult(MakeStatus());
 WriteIRQ(CDCIRQ_ACKNOWLEDGE);

 //assert(DriveStatus != DS_READING);

 CurSector = CommandLoc;

 DriveStatus = DS_READING;
 PSRCounter = 33868800 / (75 * ((Mode & MODE_SPEED) ? 2 : 1));

 return(0);
}

int32 PS_CDC::Command_Init(const int arg_count, const uint8 *args)
{
 return(0);
}

int32 PS_CDC::Command_ReadTOC(const int arg_count, const uint8 *args)
{
 return(0);
}


PS_CDC::CDC_CTEntry PS_CDC::Commands[] =
{
 { 0x00, 0, "Sync", &PS_CDC::Command_Sync, NULL },
 { 0x01, 0, "Nop", &PS_CDC::Command_Nop, NULL },
 { 0x02, 3, "Setloc", &PS_CDC::Command_Setloc, NULL },
 { 0x03, 0, "Play", &PS_CDC::Command_Play, NULL },
 { 0x04, 0, "Forward", &PS_CDC::Command_Forward, NULL },
 { 0x05, 0, "Backward", &PS_CDC::Command_Backward, NULL },
 { 0x06, 0, "ReadN", &PS_CDC::Command_ReadN, NULL },
 //{ 0x07, 0, "Standby", &PS_CDC::Command_Standby, NULL },
 { 0x07, 0, "Standby", &PS_CDC::Command_Pause, &PS_CDC::Command_Pause_Part2 },
 { 0x08, 0, "Stop", &PS_CDC::Command_Stop, &PS_CDC::Command_Stop_Part2 },
 { 0x09, 0, "Pause", &PS_CDC::Command_Pause, &PS_CDC::Command_Pause_Part2 },
 { 0x0A, 0, "Reset", &PS_CDC::Command_Reset, &PS_CDC::Command_Reset_Part2 },
 { 0x0B, 0, "Mute", &PS_CDC::Command_Mute, NULL },
 { 0x0C, 0, "Demute", &PS_CDC::Command_Demute, NULL },
 { 0x0D, 2, "Setfilter", &PS_CDC::Command_Setfilter, NULL },
 { 0x0E, 1, "Setmode", &PS_CDC::Command_Setmode, NULL },
 { 0x0F, 0, "Getparam", &PS_CDC::Command_Getparam, NULL },
 { 0x10, 0, "GetlocL", &PS_CDC::Command_GetlocL, NULL },
 { 0x11, 0, "GetlocP", &PS_CDC::Command_GetlocP, NULL },
 { 0x12, 1, "ReadT", &PS_CDC::Command_ReadT, &PS_CDC::Command_ReadT_Part2 },
 { 0x13, 0, "GetTN", &PS_CDC::Command_GetTN, NULL },
 { 0x14, 1, "GetTD", &PS_CDC::Command_GetTD, NULL },
 { 0x15, 0, "SeekL", &PS_CDC::Command_SeekL, NULL },
 { 0x16, 0, "SeekP", &PS_CDC::Command_SeekP, NULL },
 { 0x19, 1 /*?*/, "Test", &PS_CDC::Command_Test, NULL },

 { 0x1A, 0, "ID", &PS_CDC::Command_ID, &PS_CDC::Command_ID_Part2 },
 { 0x1B, 0, "ReadS", &PS_CDC::Command_ReadS, NULL },
 { 0x1C, 0, "Init", &PS_CDC::Command_Init, NULL },

 { 0x1E, 0, "ReadTOC", &PS_CDC::Command_ReadTOC, NULL },

 { 0, NULL },
};


}
