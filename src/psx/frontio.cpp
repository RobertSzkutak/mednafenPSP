#include "psx.h"

#include "input/gamepad.h"

#define PSX_FIODBGINFO(format, ...) { /*printf(format "\n", ## __VA_ARGS__); */ }

namespace MDFN_IEN_PSX
{

InputDevice::InputDevice()
{
}

InputDevice::~InputDevice()
{
}

void InputDevice::Power(void)
{
}

void InputDevice::Update(const void *data)
{
}


void InputDevice::SetDTR(bool new_dtr)
{

}

bool InputDevice::GetDSR(void)
{
 return(0);
}

bool InputDevice::Clock(bool TxD)
{
 return(0);
}


FrontIO::FrontIO()
{
 Ports[0] = Ports[1] = NULL;
}

FrontIO::~FrontIO()
{

}

void FrontIO::Write(pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 assert(!(A & 0x1));

 PSX_FIODBGINFO("[FIO] Write: %08x %08x", A, V);

 Update(timestamp);

 switch(A & 0xF)
 {
  case 0x0:
	TransmitBuffer = V;
	TransmitPending = true;
	break;

  case 0x8:
	Mode = V;
	break;

  case 0xa:
	Control = V & ~(0x40 | 0x10);

	if(V & 0x10)
	 IRQ_Assert(IRQ_SIO, false);

	if(V & 0x40)	// Reset
	{
	 IRQ_Assert(IRQ_SIO, false);

	 ReceivePending = false;
	 TransmitPending = false;
	 ReceiveBufferAvail = false;

	 TransmitBuffer = 0;
	 ReceiveBuffer = 0;

	 ReceiveBitCounter = 0;
	 TransmitBitCounter = 0;

	 Mode = 0;
	 Control = 0;
	 Baudrate = 0;
	}

	Ports[0]->SetDTR((Control & 0x2) && !(Control & 0x2000));
	Ports[1]->SetDTR((Control & 0x2) && (Control & 0x2000));

	break;

  case 0xe:
	Baudrate = V;
	break;
 }
}

uint32 FrontIO::Read(pscpu_timestamp_t timestamp, uint32 A)
{
 uint32 ret = 0;

 assert(!(A & 0x1));

 Update(timestamp);

 switch(A & 0xF)
 {
  case 0x0:
	ret = ReceiveBuffer;
	ReceiveBufferAvail = false;
	ReceivePending = true;
	break;

  case 0x4:
	ret = 0;

	if(!TransmitPending)
	 ret |= 0x1;

	if(ReceiveBufferAvail)
	 ret |= 0x2;

	break;

  case 0x8:
	ret = Mode;
	break;

  case 0xa:
	ret = Control;
	break;

  case 0xe:
	ret = Baudrate;
	break;
 }

 if((A & 0xF) != 0x4)
  PSX_FIODBGINFO("[FIO] Read: %08x %08x", A, ret);

 return(ret);
}

void FrontIO::Update(pscpu_timestamp_t timestamp)
{
 int32 clocks = timestamp - lastts;

 ClockDivider -= clocks;
 while(ClockDivider <= 0)
 {
  if((ReceivePending && (Control & 0x4)) || (TransmitPending && (Control & 0x1)))
  {
   bool rxd = 0, txd = 0;
   const uint32 BCMask = ((((Control >> 8) & 0x3) + 1) * 8) - 1;

   //PSX_WARNING("[FIO] Clock %08x", BCMask);

   if(TransmitPending)
   {
    txd = (TransmitBuffer >> TransmitBitCounter) & 1;
    TransmitBitCounter = (TransmitBitCounter + 1) & BCMask;
    if(!TransmitBitCounter)
    {
     PSX_FIODBGINFO("[FIO] Data transmitted: %08x", TransmitBuffer);
     TransmitPending = false;

     if(Control & 0x400)
      IRQ_Assert(IRQ_SIO, true);
    }
   }

   rxd = Ports[0]->Clock(txd) | Ports[1]->Clock(txd);

   if(ReceivePending)
   {
    ReceiveBuffer &= ~(1 << ReceiveBitCounter);
    ReceiveBuffer |= rxd << ReceiveBitCounter;

    ReceiveBitCounter = (ReceiveBitCounter + 1) & BCMask;

    if(!ReceiveBitCounter)
    {
     PSX_FIODBGINFO("[FIO] Data received: %08x", ReceiveBuffer);

     ReceivePending = false;
     ReceiveBufferAvail = true;

     if(Control & 0x800)
      IRQ_Assert(IRQ_SIO, true);
    }
   }
  }

  bool new_DSR = Ports[0]->GetDSR() | Ports[1]->GetDSR();

  if((DSR ^ new_DSR) & new_DSR)
  {
   if(Control & 0x1000)
   {
    PSX_FIODBGINFO("[DSR] IRQ");
    IRQ_Assert(IRQ_SIO, true);
   }
  }
  DSR = new_DSR;

  ClockDivider += 0x88;
 }

  bool new_DSR = Ports[0]->GetDSR() | Ports[1]->GetDSR();

  if((DSR ^ new_DSR) & new_DSR)
  {
   if(Control & 0x1000)
   {
    PSX_FIODBGINFO("[DSR] IRQ");
    IRQ_Assert(IRQ_SIO, true);
   }
  }
  DSR = new_DSR;



 lastts = timestamp;
}

void FrontIO::ResetTS(void)
{
 lastts = 0;
}


void FrontIO::Power(void)
{
 DSR = 0;
 lastts = 0;

 //
 //

 ClockDivider = 0;

 ReceivePending = false;
 TransmitPending = false;
 ReceiveBufferAvail = false;

 TransmitBuffer = 0;
 ReceiveBuffer = 0;

 ReceiveBitCounter = 0;
 TransmitBitCounter = 0;

 Mode = 0;
 Control = 0;
 Baudrate = 0;
}

void FrontIO::UpdateInput(void)
{
 for(int i = 0; i < 2; i++)
 {
  if(Ports[i])
   Ports[i]->Update(PortData[i]);
 }
}

void FrontIO::SetInput(unsigned int port, const char *type, void *ptr)
{
 if(Ports[port])
 {
  delete Ports[port];
  Ports[port] = NULL;
 }

 Ports[port] = Device_Gamepad_Create();
 PortData[port] = ptr;
}

void FrontIO::LoadMemcard(unsigned int which, const char *path)
{

}

void FrontIO::SaveMemcard(unsigned int which, const char *path)
{

}



static InputDeviceInfoStruct InputDeviceInfoPSXPort[] =
{
 // None
 {
  "none",
  "none",
  NULL,
  0,
  NULL 
 },

 // Gamepad
 {
  "gamepad",
  "Gamepad",
  NULL,
  sizeof(Device_Gamepad_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_Gamepad_IDII,
 },
};

static const InputPortInfoStruct PortInfo[] =
{
 { 0, "port1", "Port 1", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { 0, "port2", "Port 2", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
};

InputInfoStruct FIO_InputInfo =
{
 sizeof(PortInfo) / sizeof(InputPortInfoStruct),
 PortInfo
};


}
