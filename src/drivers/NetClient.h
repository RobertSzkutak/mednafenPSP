#ifndef __MDFN_DRIVERS_NETCLIENT_H
#define __MDFN_DRIVERS_NETCLIENT_H

class NetClient
{
 public:

 NetClient();	//const char *host);
 virtual ~NetClient();

 virtual void Connect(const char *host, unsigned int port)

 virtual void Disconnect(void);

 virtual bool IsConnected(void);

 virtual uint32 Send(const void *data, uint32 len, uint32 timeout = 0);

 virtual uint32 Receive(void *data, uint32 len, uint32 timeout = 0);
}

#endif
