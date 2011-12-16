#ifndef __MDFN_PSX_CDC_H
#define __MDFN_PSX_CDC_H

#include "../cdrom/cdromif.h"
#include "../cdrom/SimpleFIFO.h"

namespace MDFN_IEN_PSX
{

class PS_CDC
{
 public:

 PS_CDC();
 ~PS_CDC();

 void Power(void);
 void ResetTS(void);
 void Update(const pscpu_timestamp_t timestamp);

 void Write(const pscpu_timestamp_t timestamp, uint32 A, uint8 V);
 uint8 Read(const pscpu_timestamp_t timestamp, uint32 A);

 bool DMACanRead(void);
 bool DMARead(uint32 &data);

 INLINE bool GetCDDA(int32 &l, int32 &r)
 {
  if(CDDABuffer.CanRead())
  {
   l = (int16)(CDDABuffer.ReadByte() | (CDDABuffer.ReadByte() << 8));
   r = (int16)(CDDABuffer.ReadByte() | (CDDABuffer.ReadByte() << 8));
   return(true);
  }

  return(false);
 }

 private:

 uint8 Control;
 uint8 ArgsBuf[16];
 uint32 ArgsIn;

 SimpleFIFO<uint8> Results;

 uint8 SB[2340];
 bool SB_In;
 uint32 SB_RP;

 SimpleFIFO<uint8> DMABuffer;

 SimpleFIFO<uint8> CDDABuffer;

 uint8 SubQBuf[0xC];
 uint8 SubQBuf_Safe[0xC];
 bool SubQChecksumOK;

 void RecalcIRQ(void);
 enum
 {
  CDCIRQ_NONE = 0,
  CDCIRQ_DATA_READY = 1,
  CDCIRQ_COMPLETE = 2,
  CDCIRQ_ACKNOWLEDGE = 3,
  CDCIRQ_DATA_END = 4,
  CDCIRQ_DISC_ERROR = 5
 };
 SimpleFIFO<uint8> IRQQueue;

 void WriteIRQ(uint8);
 void WriteResult(uint8);

 uint8 FilterFile;
 uint8 FilterChan;


 uint8 PendingCommand;
 bool PendingCommandPhase;
 int32 PendingCommandCounter;

 enum { MODE_SPEED = 0x80 };
 enum { MODE_STRSND = 0x40 };
 enum { MODE_SIZE = 0x20 };
 enum { MODE_SIZE2 = 0x10 };
 enum { MODE_SF = 0x08 };
 enum { MODE_REPORT = 0x04 };
 enum { MODE_AUTOPAUSE = 0x02 };
 enum { MODE_CDDA = 0x01 };
 uint8 Mode;

 enum
 {
  DS_PAUSED = -1,
  DS_STOPPED = 0,
  DS_SEEKING,
  DS_PLAY_SEEKING,
  DS_PLAYING,
  DS_READING,
 };
 int DriveStatus;
 bool Forward;
 bool Backward;
 bool Muted;

 int32 PlayTrackMatch;

 int32 PSRCounter;

 int32 CurSector;

 int32 SeekTarget;

 pscpu_timestamp_t lastts;

 CD_TOC toc;
 bool IsPSXDisc;

 int32 CommandLoc;

 uint8 MakeStatus(bool cmd_error = false);
 void DecodeSubQ(uint8 *subpw);

 struct CDC_CTEntry
 {
  uint8 command;
  uint8 args_length;
  const char *name;
  int32 (PS_CDC::*func)(const int arg_count, const uint8 *args);
  int32 (PS_CDC::*func2)(void);
 };

 static CDC_CTEntry Commands[];

 int32 Command_Sync(const int arg_count, const uint8 *args);
 int32 Command_Nop(const int arg_count, const uint8 *args);
 int32 Command_Setloc(const int arg_count, const uint8 *args);
 int32 Command_Play(const int arg_count, const uint8 *args);
 int32 Command_Forward(const int arg_count, const uint8 *args);
 int32 Command_Backward(const int arg_count, const uint8 *args);
 int32 Command_ReadN(const int arg_count, const uint8 *args);
 int32 Command_Standby(const int arg_count, const uint8 *args);
 int32 Command_Stop(const int arg_count, const uint8 *args);
 int32 Command_Stop_Part2(void); 
 int32 Command_Pause(const int arg_count, const uint8 *args);
 int32 Command_Pause_Part2(void);
 int32 Command_Reset(const int arg_count, const uint8 *args);
 int32 Command_Reset_Part2(void);
 int32 Command_Mute(const int arg_count, const uint8 *args);
 int32 Command_Demute(const int arg_count, const uint8 *args);
 int32 Command_Setfilter(const int arg_count, const uint8 *args);
 int32 Command_Setmode(const int arg_count, const uint8 *args);
 int32 Command_Getparam(const int arg_count, const uint8 *args);
 int32 Command_GetlocL(const int arg_count, const uint8 *args);
 int32 Command_GetlocP(const int arg_count, const uint8 *args);

 int32 Command_ReadT(const int arg_count, const uint8 *args);
 int32 Command_ReadT_Part2(void);

 int32 Command_GetTN(const int arg_count, const uint8 *args);
 int32 Command_GetTD(const int arg_count, const uint8 *args);
 int32 Command_SeekL(const int arg_count, const uint8 *args);

 int32 Command_SeekP(const int arg_count, const uint8 *args);
 int32 Command_Test(const int arg_count, const uint8 *args);

 int32 Command_ID(const int arg_count, const uint8 *args);
 int32 Command_ID_Part2(void);

 int32 Command_ReadS(const int arg_count, const uint8 *args);
 int32 Command_Init(const int arg_count, const uint8 *args);

 int32 Command_ReadTOC(const int arg_count, const uint8 *args);
};

}

#endif
