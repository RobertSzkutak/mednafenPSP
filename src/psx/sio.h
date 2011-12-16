#ifndef __MDFN_PSX_SIO_H
#define __MDFN_PSX_SIO_H

namespace MDFN_IEN_PSX
{

class SIO_Device
{
 public:

 SIO_Device();
 virtual ~SIO_Device();

 virtual void Power(void);

 virtual void SetDTR(bool new_dtr, bool new_sel);
 virtual bool GetDSR(void);

 virtual void Clock(bool &RxD, bool TxD);
};

void SIO_Power(void);
uint32 SIO_Read(const pscpu_timestamp_t timestamp, uint32 A);
void SIO_Write(const pscpu_timestamp_t timestamp, uint32 A, uint32 V);

void SIO_SetDevice(unsigned int port, SIO_Device *device);

void SIO_Update(const pscpu_timestamp_t timestamp);
void SIO_ResetTS(void);

}

#endif
