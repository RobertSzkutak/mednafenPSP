#include "psx.h"

namespace MDFN_IEN_PSX
{

static int32 lastts;

struct SIO_Port
{
 uint32 Data;

 uint16 Control;
 uint16 Mode;
 uint16 Baudrate;

 bool ReadAvail;

 uint32 ReadBuffer;
 uint32 WriteBuffer;

 bool ReadPending;
 bool WritePending;

 int32 ReadBitCounter;
 int32 WriteBitCounter;

 int32 ClockDivider;

 bool PrevRxD;

 bool PrevDSR;

 SIO_Device *Device;
};

#define CONTROL_TX_ENABLE 0x0001
#define CONTROL_RX_ENABLE 0x0004
#define CONTROL_RESET	  0x0040

static SIO_Port Ports[2] = { { 0 }, { 0 } };

SIO_Device::SIO_Device()
{

}

SIO_Device::~SIO_Device()
{

}

void SIO_Device::Power(void)
{

}

void SIO_Device::SetDTR(bool new_dtr, bool new_sel)
{

}

bool SIO_Device::GetDSR(void)
{
 return(false);
}

void SIO_Device::Clock(bool &RxD, bool TxD)
{

}


void SIO_SetDevice(unsigned int port, SIO_Device *device)
{
 Ports[port].Device = device;
}

void SIO_ResetTS(void)
{
 lastts = 0;
}

void SIO_Power(void)
{
 lastts = 0;

 for(int i = 0; i < 2; i++)
 {
  SIO_Port *port = &Ports[i];

  port->Data = 0;
  port->Mode = 0;
  port->Control = 0;
  port->Baudrate = 0;

  port->ReadAvail = 0;

  port->ReadBuffer = 0;
  port->WriteBuffer = 0;

  port->ReadPending = 0;
  port->WritePending = 0;

  port->ReadBitCounter = 0;
  port->WriteBitCounter= 0;

  port->ClockDivider = 0;

  port->PrevRxD = 0;

  port->PrevDSR = 0;

  if(port->Device)
   port->Device->Power();
 }
}

void SIO_Update(const pscpu_timestamp_t timestamp)
{
 int32 s_clocks = (timestamp - lastts) >> 0;
 lastts += s_clocks << 0;

 for(int i = 0; i < 2; i++)
 {
  SIO_Port *port = &Ports[i];

  port->ClockDivider -= s_clocks;

  while(port->ClockDivider <= 0)
  {
   bool RxD, TxD = 1;

   if(port->WritePending)
   {
    if(port->WriteBitCounter == -1)
    {
     PSX_WARNING("[SIO] TxD start bit");
     TxD = 0;
     port->WriteBitCounter++;
    }
    else
    {
     TxD = (port->WriteBuffer >> port->WriteBitCounter) & 1;

     PSX_WARNING("[SIO] TxD bit %d=%d", port->WriteBitCounter, TxD);

     port->WriteBitCounter++;
     if(port->WriteBitCounter >= 8) //((1 << ((port->Control >> 8) & 0x3)) * 8) )
     {
      port->WritePending = false;
      if(port->Control & 0x0400)
      {
       PSX_WARNING("[SIO] Tx IRQ");
       IRQ_Assert(IRQ_SIO, true);
      }
     }
    }
   }

   if(port->Device)
    port->Device->Clock(RxD, TxD);
   else
    RxD = 1;

   if(port->ReadPending)
   {
    if(port->ReadBitCounter == -1)
    {
     if(!RxD && port->PrevRxD)
     {
      port->ReadBitCounter = 0;
     }
    }
    else
    {
     port->ReadBuffer |= RxD << port->ReadBitCounter;

     port->ReadBitCounter++;
     if(port->ReadBitCounter >= 8) //((1 << ((port->Control >> 8) & 0x3)) * 8) )
     {
      port->ReadPending = false;
      port->ReadAvail = true;

      if(port->Control & 0x0800)
      {
       PSX_WARNING("[SIO] Rx IRQ");
       IRQ_Assert(IRQ_SIO, true);
      }

     }
    }
   }

   bool DSR = 0;

   if(port->Device)
    DSR = port->Device->GetDSR();

   //PSX_WARNING("[SIO] DSR status: %d", DSR);

   if(DSR && !port->PrevDSR)
   {
    if(port->Control & 0x1000)
    {
     PSX_WARNING("[SIO] DSR IRQ");
     IRQ_Assert(IRQ_SIO, true);
    }
   }

   port->PrevDSR = DSR;
   port->PrevRxD = RxD;
   port->ClockDivider += 0x88;
  }
 }
}

uint32 SIO_Read(const pscpu_timestamp_t timestamp, uint32 A)
{
 SIO_Port *port = &Ports[(A >> 4) & 1];
 uint32 ret = 0;

 //if(A == 0x1F801040)
 // DBG_Break();

 SIO_Update(timestamp);

 switch(A & 0xF)
 {
  case 0x0:
	ret = port->ReadBuffer;
	port->ReadAvail = false;

	if(!port->ReadPending)
	{
	 port->ReadBuffer = 0;
	 port->ReadPending = true;
	 port->ReadBitCounter = -1;
	}
	break;
 
  case 0x4:
	ret = 0;

	if(port->ReadAvail)
	 ret |= 0x02;
	if((port->Control & CONTROL_TX_ENABLE) && !port->ReadAvail)
	 ret |= 0x01;
	break;

  case 0x8:
	ret = port->Mode;
	break;

  case 0xA:
	ret = port->Control;
	break;

  case 0xE:
	ret = port->Baudrate;
	break;
 }

 if((A & 0xF) != 4)
 PSX_WARNING("[SIO] Read: 0x%08x 0x%08x @ %d", A, ret, timestamp);

 return(ret);
}

void SIO_Write(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 SIO_Port *port = &Ports[(A >> 4) & 1];

 PSX_WARNING("[SIO] Write: 0x%08x 0x%08x @ %d, scanline=%d", A, V, timestamp, GPU->GetScanlineNum());

 SIO_Update(timestamp);

 switch(A & 0xF)
 {
  case 0x0:
	if(port->Control & CONTROL_TX_ENABLE)
	{
	 port->WriteBuffer = V;

	 if(!port->WritePending)
	 {
	  port->WriteBitCounter = -1;
 	  port->WritePending = true;
	 }
	}
	break;

  case 0x8:
	port->Mode = V;
	break;

  case 0xA:
	if(V & 0x10)	// IRQ clear?
	{
	 V &= ~0x10;
	 IRQ_Assert(IRQ_SIO, false);
	}

	if(V & CONTROL_RESET)	// Reset?
	{
	 IRQ_Assert(IRQ_SIO, false);

	 V = 0;
	  port->Data = 0;
	  port->Mode = 0;
	  port->Control = 0;
	  port->Baudrate = 0;
	
	  port->ReadAvail = 0;

	  port->ReadBuffer = 0;
	  port->WriteBuffer = 0;

	  port->ReadPending = 0;
  	port->WritePending = 0;

	  port->ReadBitCounter = 0;
	  port->WriteBitCounter= 0;

	  port->ClockDivider = 0;

  	port->PrevRxD = 0;

	  port->PrevDSR = 0;
	}

	if(port->Device)
	 port->Device->SetDTR((bool)(V & 0x2), (bool)(V & 0x2000));

	port->Control = V;

	break;

  case 0xE:
	port->Baudrate = V;
	break;
 }
}



}



