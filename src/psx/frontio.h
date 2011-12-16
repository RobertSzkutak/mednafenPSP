#ifndef __MDFN_PSX_FRONTIO_H
#define __MDFN_PSX_FRONTIO_H

namespace MDFN_IEN_PSX
{

class InputDevice
{
 public:

 InputDevice();
 virtual ~InputDevice();

 virtual void Power(void);
 virtual void Update(const void *data);

 //
 //
 //
 virtual void SetDTR(bool new_dtr);
 virtual bool GetDSR(void);

 virtual bool Clock(bool TxD);
};

class FrontIO
{
 public:

 FrontIO();
 ~FrontIO();

 void Power(void);
 void Write(pscpu_timestamp_t timestamp, uint32 A, uint32 V);
 uint32 Read(pscpu_timestamp_t timestamp, uint32 A);
 void Update(pscpu_timestamp_t timestamp);
 void ResetTS(void);

 void UpdateInput(void);
 void SetInput(unsigned int port, const char *type, void *ptr);

 void LoadMemcard(unsigned int which, const char *path);
 void SaveMemcard(unsigned int which, const char *path);

 private:

 InputDevice *Ports[2];
 void *PortData[2];

 InputDevice *MCPorts[2];

 bool DSR;

 //
 //
 //

 int32 ClockDivider;

 bool ReceivePending;
 bool TransmitPending;
 bool ReceiveBufferAvail;

 uint32 ReceiveBuffer;
 uint32 TransmitBuffer;

 int32 ReceiveBitCounter;
 int32 TransmitBitCounter;

 uint16 Mode;
 uint16 Control;
 uint16 Baudrate;

 //
 //
 int32 lastts;
};

extern InputInfoStruct FIO_InputInfo;

}
#endif
