#include "../psx.h"

namespace MDFN_IEN_PSX
{

class InputDevice_Gamepad : public InputDevice
{
 public:

 InputDevice_Gamepad();
 virtual ~InputDevice_Gamepad();

 virtual void Power(void);
 virtual void Update(const void *data);

 //
 //
 //
 virtual void SetDTR(bool new_dtr);
 virtual bool GetDSR(void);
 virtual bool Clock(bool TxD);

 private:

 bool dtr;

 uint8 buttons[2];

 int32 command_phase;
 uint32 bitpos;
 uint8 receive_buffer;

 uint8 command;

 uint8 transmit_buffer[3];
 uint32 transmit_pos;
 uint32 transmit_count;
};

InputDevice_Gamepad::InputDevice_Gamepad()
{

}

InputDevice_Gamepad::~InputDevice_Gamepad()
{

}

void InputDevice_Gamepad::Power(void)
{
 dtr = 0;

 buttons[0] = buttons[1] = 0;

 command_phase = 0;

 bitpos = 0;

 receive_buffer = 0;

 command = 0;

 memset(transmit_buffer, 0, sizeof(transmit_buffer));

 transmit_pos = 0;
 transmit_count = 0;
}

void InputDevice_Gamepad::Update(const void *data)
{
 uint8 *d8 = (uint8 *)data;

 buttons[0] = d8[0];
 buttons[1] = d8[1];
}


void InputDevice_Gamepad::SetDTR(bool new_dtr)
{
 if(!dtr && new_dtr)
 {
  command_phase = 0;
  bitpos = 0;
  transmit_pos = 0;
  transmit_count = 0;
 }

 dtr = new_dtr;
}

bool InputDevice_Gamepad::GetDSR(void)
{
 if(!dtr)
  return(0);

 if(!bitpos && transmit_count)
  return(1);

 return(0);
}

bool InputDevice_Gamepad::Clock(bool TxD)
{
 bool ret = 0;

 if(!dtr)
  return(0);

 if(transmit_count)
  ret = (transmit_buffer[transmit_pos] >> bitpos) & 1;

 receive_buffer &= ~(1 << bitpos);
 receive_buffer |= TxD << bitpos;
 bitpos = (bitpos + 1) & 0x7;

 if(!bitpos)
 {
  //PSX_WARNING("[FIO] Gamepad: %02x", receive_buffer);

  if(transmit_count)
  {
   transmit_pos++;
   transmit_count--;
  }


  switch(command_phase)
  {
   case 0:
 	  if((receive_buffer & 0x81) != 0x01)
	    command_phase = -1;
	  else
	  {
	   transmit_buffer[0] = 0x41;
	   transmit_pos = 0;
	   transmit_count = 1;
	   command_phase++;
	  }
	  break;

   case 1:
	command = receive_buffer;
	command_phase++;

	transmit_buffer[0] = 0x5A;
	transmit_buffer[1] = 0xFF ^ buttons[0];
	transmit_buffer[2] = 0xFF ^ buttons[1];
	transmit_pos = 0;
	transmit_count = 3;
	break;

  }
 }

 return(ret);
}


#if 0
void InputDevice_Gamepad::StartRead(void)
{
 latch_readpos = 0;

 latch[0] = 0x41;
 latch[1] = 0x5A;
 latch[2] = 0xFF ^ buttons[0];
 latch[3] = 0xFF ^ buttons[1];
}

uint8 InputDevice_Gamepad::Read(void)
{
 if(latch_readpos >= 4)
  return(0xFF);
 else
  return(latch[latch_readpos++]);
}
#endif

InputDevice *Device_Gamepad_Create(void)
{
 return new InputDevice_Gamepad();
}


InputDeviceInputInfoStruct Device_Gamepad_IDII[16] =
{
 { "select", "SELECT", 4, IDIT_BUTTON, NULL },
 { NULL, "empty", 0, IDIT_BUTTON },
 { NULL, "empty", 0, IDIT_BUTTON },
 { "start", "START", 5, IDIT_BUTTON, NULL },
 { "up", "UP ↑", 0, IDIT_BUTTON, "down" },
 { "right", "RIGHT →", 3, IDIT_BUTTON, "left" },
 { "down", "DOWN ↓", 1, IDIT_BUTTON, "up" },
 { "left", "LEFT ←", 2, IDIT_BUTTON, "right" },

 { "l2", "L2 (rear left shoulder)", 11, IDIT_BUTTON, NULL },
 { "r2", "R2 (rear right shoulder)", 13, IDIT_BUTTON, NULL },
 { "l1", "L1 (front left shoulder)", 10, IDIT_BUTTON, NULL },
 { "r1", "R1 (front right shoulder)", 12, IDIT_BUTTON, NULL },

 { "triangle", "△ (upper)", 6, IDIT_BUTTON_CAN_RAPID, NULL },
 { "circle", "○ (right)", 9, IDIT_BUTTON_CAN_RAPID, NULL },
 { "cross", "x (lower)", 7, IDIT_BUTTON_CAN_RAPID, NULL },
 { "square", "□ (left)", 8, IDIT_BUTTON_CAN_RAPID, NULL },
};



}
