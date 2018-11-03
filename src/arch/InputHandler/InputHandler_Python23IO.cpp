/*
Holy shit this bs is using libusb0. WTF is that bullshit about. Not only does the API differ from libusb1, the actual data returned differs a bit!!
This is based on my libusb1 program i use to funnel data over UDP to a remote controller.

Some notes:
If using on a real ddr pc, the xp version of zadig has absolute SHIT libusb0 drivers. Libusb1 can use the winusb backend (potential reason for inconsistencies?).
You'll get a reaping error with that version of the driver. Instead, and I have NO idea why this even works, but it does, install the libusbk driver
for your python io. Yup... somehow the winusb approximation library actually works with this... go figure

Secondly, the ibutton library has some com port shit in it. Half (66% of an HD cabinet) of DDR's lights are controlled over serial.
But SOME of them are controlled over USB. Dear god konami get your act together and commit! I guess they did... everything is serial over usb with modern shit... lmao
Anyway, this ibutton library com port driver does NOT work! Theres notes alluding to that. So, I found this awesome MIT licensed
multiplatform library I threw in arch/COM. Should let people actually throw ddr hardware in a mac or raspberry pi and use it or something haha.

Like hell do I understand cmake or anything like that so I'm keeping my changes self contained. Should be a simple addition to whatever fork that way.
Finally, I do not understand the button system in this at all, so I am throwing keyboard keys out there. Would someone be so kind as to convert this
to a joystick or special DDR input device so I can disable the keyboard buttons entirely so asshats cant press f1 for credits
this depends on io/python23io.ccp/h -- see there for additional details. 

*****IMPORTANT!*****
usb bulk write and read commands are in their own thread to not bog interrupt reading. I do not know the consequences of doing this
libusb says the file descriptors are thread safe but you may not get the notification data arrived? Thing is, are the descriptors device or endpoint specific?
My questions on the ML went unanswered so I leave this as an experiment to test on my lab rats... But it seems to be fine
*/
#include "global.h"
#include "RageLog.h"
#include "GameState.h"
#include "PrefsManager.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/COM/serial.h"

#if defined(_MSC_VER)
#define strcasecmp _stricmp
#endif

// required I/O routines
#include "LightsManager.h"
#include "InputHandler_Python23IO.h"


#ifdef PRODUCT_ID_BARE
#ifndef STDSTRING_H
#include <array>
#endif
REGISTER_INPUT_HANDLER_CLASS(Python23IO);
#else
REGISTER_INPUT_HANDLER(Python23IO);
#endif

bool InputHandler_Python23IO::s_bInitialized = false;
uint8_t InputHandler_Python23IO::usb_light_message[5];
uint8_t InputHandler_Python23IO::vextio_message[4] = { 0, 0, 0, 0 };
uint8_t InputHandler_Python23IO::packetBuffer[3] = { 0, 0, 0 };

Preference<PSTRING>InputHandler_Python23IO:: m_sP23IOMode("Python23IO_Mode", "SDP3IO");
Preference<bool> InputHandler_Python23IO::m_bP2IOEXTIO("Python23IO_P2IO_EXTIO", true);
bool InputHandler_Python23IO::hasHDXB = true;
bool InputHandler_Python23IO::isP3IO = true;
bool InputHandler_Python23IO::HDCabinetLayout = false;





//This is the old p3io -> UDP network packet definition... nothing special to see here folks... probably leave this alone...if it aint broke dont fix it!
//model keys after network packet structure:
// Read this as MOST SIGNIFICANT BIT to LEAST SIGNIFICANT BIT per byte
// udlrudlr  -- p1 and p2 pad
// UDLRUDLR  -- p1 and p2 control panel
// stcC12XX  -- service test coin1 coin2 start1 start2 ? ?
#ifdef PRODUCT_ID_BARE
void InputHandler_Python23IO::GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut)
{
	if (m_bFoundUSBDevice)
	{
		vDevicesOut.push_back(InputDeviceInfo(DEVICE_KEYBOARD, "Python23IO"));
	}
}

#ifndef STDSTRING_H //rstring doesnt exist...
std::array<DeviceButton, PYTHON23IO_NUM_STATES> button_list =
{
#else //  STDSTRING_H
const DeviceButton InputHandler_Python23IO::button_list[PYTHON23IO_NUM_STATES] =
#endif

#else
void InputHandler_Python23IO::GetDevicesAndDescriptions(vector<InputDevice>& vDevicesOut, vector<PSTRING>& vDescriptionsOut)
{
	if (m_bFoundUSBDevice)
	{
		vDevicesOut.push_back(InputDevice(DEVICE_KEYBOARD));
		vDescriptionsOut.push_back("Python23IO");
	}
}
const int InputHandler_Python23IO::button_list[PYTHON23IO_NUM_STATES] =
#endif
{
	//player 1 pad

	KEY_UP,					// DANCE_BUTTON_UP,
	KEY_DOWN,				// DANCE_BUTTON_DOWN,
	KEY_LEFT,				// DANCE_BUTTON_LEFT,
	KEY_RIGHT,				// DANCE_BUTTON_RIGHT,

	//player 2 pad
	KEY_KP_C8,				// DANCE_BUTTON_UP,
	KEY_KP_C2,				// DANCE_BUTTON_DOWN,
	KEY_KP_C4,				// DANCE_BUTTON_LEFT,
	KEY_KP_C6,				// DANCE_BUTTON_RIGHT,

	//player 1 CP
	KEY_HOME,				// DANCE_BUTTON_MENUUP
	KEY_END,				// DANCE_BUTTON_MENUDOWN
	KEY_DEL,				// DANCE_BUTTON_MENULEFT
	KEY_PGDN,				// DANCE_BUTTON_MENURIGHT

	//player 2 CP
	KEY_KP_HYPHEN,			// DANCE_BUTTON_MENUUP
	KEY_KP_PLUS,			// DANCE_BUTTON_MENUDOWN
	KEY_KP_SLASH,			// DANCE_BUTTON_MENULEFT
	KEY_KP_ASTERISK,		// DANCE_BUTTON_MENURIGHT

	//buttons for admin functions
	KEY_F1,					// DANCE_BUTTON_COIN -- mimic service on a ddr machine
	KEY_SCRLLOCK,			// OPERATOR / TEST BUTTON
	KEY_F1,					// DANCE_BUTTON_COIN
	KEY_F1,					// DANCE_BUTTON_COIN

	//p1 and p2 start buttons
	KEY_ENTER,				// DANCE_BUTTON_START p1
	KEY_KP_ENTER,			// DANCE_BUTTON_START p2

#ifdef PRODUCT_ID_BARE
#ifndef STDSTRING_H //stepmania 5 master
}
};
#else //stepmania 5.1-new
};
#endif 
#else//oitg
};

#endif





InputHandler_Python23IO::InputHandler_Python23IO()
{
	m_bFoundUSBDevice = false;

	//initialize light defaults
	for (int i = 0; i < 16; i++)
	{
		myLights[i] = false;
		previousLights[i] = true;
	}

	LOG->Info("Checking Python23IO mode...");

	if (strcasecmp(m_sP23IOMode.Get().c_str(), "HDP3IO") == 0)
	{
		HDCabinetLayout = true;
		hasHDXB = true;
		isP3IO = true;
		LOG->Info("Python23IO mode Type set to HD with HDXB and HD style cabinet buttons.");
	}
	else if (strcasecmp(m_sP23IOMode.Get().c_str(), "HDP2IO") == 0)
	{
		HDCabinetLayout = false;
		hasHDXB = true;
		isP3IO = true;
		LOG->Info("Python23IO mode Type set to P2IO (Standard cabinet layout only) with HDXB device.");
	}
	else if (strcasecmp(m_sP23IOMode.Get().c_str(), "SDP2IO") == 0)
	{
		HDCabinetLayout = true;
		hasHDXB = true;
		isP3IO = false;
		LOG->Info("Python23IO mode Type set to P2IO without HDXB device.");
	}
	else if (strcasecmp(m_sP23IOMode.Get().c_str(), "SDP3IOHDXB") == 0)
	{
		HDCabinetLayout = false;
		hasHDXB = true;
		isP3IO = true;
		LOG->Info("Python23IO mode Type set to P3IO WITH HDXB and standard cabinet buttons.");
	}
	else
	{
		LOG->Info("Python23IO mode Type set to P3IO without HDXB and standard cabinet buttons.");
		HDCabinetLayout = false;
		hasHDXB = false;
		isP3IO = true;
	}


	for (int i = 0; i<PYTHON23IO_NUM_STATES; i++)
	{
		previousStates[i] = 0;
	}

	/* if a handler has already been created (e.g. by ScreenArcadeStart)
	* and it has claimed the board, don't try to claim it again. */

	if (s_bInitialized)
	{
		LOG->Warn("InputHandler_Python23IO: Redundant driver loaded. Disabling...");
		return;
	}

	// attempt to open the I/O device
	if (!Board.Open())
	{
		LOG->Warn("InputHandler_Python23IO: could not establish a connection with the I/O device.");
		return;
	}

	LOG->Info("Opened Python2/3 IO USB board.");


	s_bInitialized = true;
	m_bFoundUSBDevice = true;
	m_bShutdown = false;


	
	USBThread.SetName("Python23IO USB Interrupt thread");
	USBThread.Create(USBThread_Start, this);

	USBBulkThread.SetName("Python23IO USB Bulk thread");
	
	LOG->Info("detect Python23IO bulk thread.");
	//update lights appropriatly for each cabinet type -- this prevents doing a compare operation every loop -- do it once to make the thread
	
	USBBulkThread.Create(USBBulkThread_Start, this);

}

InputHandler_Python23IO::~InputHandler_Python23IO()
{


	m_bShutdown = true;

	if (USBThread.IsCreated())
	{
		LOG->Trace("Shutting down Python23IO USB Interrupt thread...");
		USBThread.Wait();
		LOG->Trace("Python23IO USB interrupt  thread shut down.");
	}

	if (USBBulkThread.IsCreated())
	{
		LOG->Trace("Shutting down Python23IO USB Bulk thread...");
		USBBulkThread.Wait();
		LOG->Trace("Python23IO USB Bulk thread shut down.");
	}


	/* Reset all USB lights to off and close it */
	if (m_bFoundUSBDevice)
	{
		memset(usb_light_message, 0, 5 * sizeof(uint8_t));// zero out the message
		Board.writeLights(usb_light_message);
		Board.Close();

		s_bInitialized = false;
	}
}



//TRAMPOLINES FOR SD/HD CABINET BULK THREAD SELECTION
int InputHandler_Python23IO::USBBulkThread_Start(void *p)
{
	((InputHandler_Python23IO *)p)->USBBulkThreadMain();
	return 0;
}

void InputHandler_Python23IO::NOP()
{
	return;
}

void InputHandler_Python23IO::NOPWithSleep()
{
	usleep(16666); 
	return;
}

void InputHandler_Python23IO::USBBulkThreadMain()
{
	void (InputHandler_Python23IO::*cabinetFunction)();
	void (InputHandler_Python23IO::*EXTIOFunction)();
	void (InputHandler_Python23IO::*HDXBFunction)();
	int lightsFuncs = 1;
	if (isP3IO)
	{
		if (HDCabinetLayout)
		{
			cabinetFunction = &InputHandler_Python23IO::UpdateLightsUSBHD;
		}
		else
		{
			cabinetFunction = &InputHandler_Python23IO::UpdateLightsUSBSD;
		}
	}
	else
	{
		cabinetFunction = &InputHandler_Python23IO::UpdateLightsP2IOSD;
	}

	if (hasHDXB)
	{
		HDXBFunction = &InputHandler_Python23IO::UpdateLightsHDXB;
		lightsFuncs++;
	}
	else
	{
		HDXBFunction = &InputHandler_Python23IO::NOP;
	}

	if (!isP3IO && Board.hasVEXTIO()) // if p2io and has vextio
	{

		EXTIOFunction = &InputHandler_Python23IO::UpdateLightsVEXTIO;
		lightsFuncs ++ ;
	}
	else
	{
		EXTIOFunction = &InputHandler_Python23IO::NOP;
	}

	red_top_count=0;
	red_bottom_count=0;
	blue_top_count=0;
	blue_bottom_count=0;
	neon_count=0;
	bool cabinetLightsUpdated = false;
	bool hdxbUpdated = true;
	bool extioLightsUpdated = false;
	int i = 0;
	uint8_t packets_since_keepalive = 0;
	while (!m_bShutdown)
	{
		packets_since_keepalive++;
		packets_since_keepalive %= 6;
		cabinetLightsUpdated = false;
		extioLightsUpdated = false;
		if (packets_since_keepalive == 0) cabinetLightsUpdated = true;//force an update

		GetCurrentLightsState();
		if (!cabinetLightsUpdated)
		{

			for (i = 0; i < 8; i++)
			{
				if (myLights[i] != previousLights[i]){
					cabinetLightsUpdated = true; break;
				}
			}
		}
		for (i = 6; i < 16; i++)
		{
			if (myLights[i] != previousLights[i]){
				extioLightsUpdated = true; break;
			}
		}

		
		if (cabinetLightsUpdated)
		{
			(*this.*cabinetFunction)();
			
		}
		if (hdxbUpdated)(*this.*HDXBFunction)();//always update hdxb
		if (extioLightsUpdated)(*this.*EXTIOFunction)();

		usleep(((16666)*1) / (1*(lightsFuncs))); //hack until I thread this better? trying to limit lights to 60fps
	}
}

//END TRAMPOLINES FOR SD/HD CABINET BULK THREAD SELECTION



//INTERRUPT THREAD SETUP
int InputHandler_Python23IO::USBThread_Start(void *p)
{
	((InputHandler_Python23IO *)p)->USBThreadMain();
	return 0;
}
void InputHandler_Python23IO::USBThreadMain()
{
	// boost this thread priority past the priority of the binary;
	// if we don't, we might lose input data (e.g. coins) during loads.
#ifdef WIN32
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
	{
		LOG->Warn("Failed to set Python23IO thread priority: %d", GetLastError());
	}
		

	/* Enable priority boosting. */
	SetThreadPriorityBoost(GetCurrentThread(), FALSE);
#else
#ifndef PRODUCT_ID_BARE
	HOOKS->BoostThreadPriority();
#endif
#endif


	while (!m_bShutdown)
	{


		// read our input data (and handle I/O errors)
		//LOG->Info( "Python23IO::about to process" );
		if (!Board.interruptRead(packetBuffer))
		{
			LOG->Warn("Python23IO disconnected? Trying to reconnect");
			Board.Reconnect();
			usleep(100000);
			continue;
		}

		// update the I/O state with the data we've read
		ProcessDataFromDriver();
	}
	//thread will exit and priority wont matter
}
//END INTERRUPT THREAD SETUP


void InputHandler_Python23IO::ProcessDataFromDriver()
{
	//LOG->Info( "Python23IO::Processing data from driver" );
	int i = 0;
	for (i = 0; i<PYTHON23IO_NUM_STATES; i++)
	{
		int byte = i / 8;
		int bit = i % 8;
		if (BYTE_BIT_IS_SET_M_TO_L(packetBuffer[byte], bit) != previousStates[i])
		{
			previousStates[i] = !previousStates[i];
			//LOG->Info( "Python23IO input state of button %d changed to %d",i,previousStates[i] );
#ifdef PRODUCT_ID_BARE
#ifndef STDSTRING_H
			ButtonPressed(DeviceInput(DEVICE_KEYBOARD, button_list[i], previousStates[i]));
#else
			ButtonPressed(DeviceInput(DEVICE_KEYBOARD, button_list[i], previousStates[i]));

#endif

#else
			ButtonPressed(DeviceInput(DEVICE_KEYBOARD, button_list[i]), previousStates[i]);
#endif
		}
	}
	InputHandler::UpdateTimer();
}

void InputHandler_Python23IO::GetCurrentLightsState()
{
	memcpy( &previousLights,  myLights, 16 * sizeof( bool ) ); // stuff in a Python23IO wrapper
	memset( myLights, false, 16 * sizeof(bool));// zero out the message


#ifdef PRODUCT_ID_BARE
	LightsState ls = LightsDriver_Export::GetState();

	if (ls.m_bGameButtonLights[GameController_1][GAME_BUTTON_START]) myLights[PYTHON23IO_INDEX_P1S] = true;
	if (ls.m_bGameButtonLights[GameController_2][GAME_BUTTON_START]) myLights[PYTHON23IO_INDEX_P2S] = true;
	if (ls.m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT]) myLights[PYTHON23IO_INDEX_P1U] = true;
	if (ls.m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT])  myLights[PYTHON23IO_INDEX_P1D] = true;
	if (ls.m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT]) myLights[PYTHON23IO_INDEX_P2U] = true;
	if (ls.m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT])  myLights[PYTHON23IO_INDEX_P2D] = true;
	if (ls.m_bCabinetLights[LIGHT_BASS_LEFT]) myLights[PYTHON23IO_INDEX_P1N] = true;
	if (ls.m_bCabinetLights[LIGHT_BASS_RIGHT]) myLights[PYTHON23IO_INDEX_P2N] = true;
	if (ls.m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT])	myLights[PYTHON23IO_INDEX_PAD1L] = true;
	if (ls.m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT])	myLights[PYTHON23IO_INDEX_PAD1R] = true;
	if (ls.m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP])		myLights[PYTHON23IO_INDEX_PAD1U] = true;
	if (ls.m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN])	myLights[PYTHON23IO_INDEX_PAD1D] = true;
	if (ls.m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT])	myLights[PYTHON23IO_INDEX_PAD2L] = true;
	if (ls.m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT])	myLights[PYTHON23IO_INDEX_PAD2R] = true;
	if (ls.m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP])		myLights[PYTHON23IO_INDEX_PAD2U] = true;
	if (ls.m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN])	myLights[PYTHON23IO_INDEX_PAD2D] = true;
	
	
	
#else
	
	const LightsState *ls = LightsDriver_External::Get();
	//i am convinced the actual button lights are controlled over serial on HD cabinets
	if (ls->m_bCabinetLights[LIGHT_BUTTONS_LEFT]) myLights[PYTHON23IO_INDEX_P1S] = true;
	if (ls->m_bCabinetLights[LIGHT_BUTTONS_RIGHT]) myLights[PYTHON23IO_INDEX_P2S] = true;

	//the following do not control the spotlights in HD mode... see above.. other lights are over serial COM2, see extio lights driver
	if (ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT])  myLights[PYTHON23IO_INDEX_P2D] = true;
	if (ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT]) myLights[PYTHON23IO_INDEX_P2U] = true;
	if (ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT]) myLights[PYTHON23IO_INDEX_P1D] = true;
	if (ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT])  myLights[PYTHON23IO_INDEX_P1U] = true;
	if (ls->m_bCabinetLights[LIGHT_BASS_LEFT]) myLights[PYTHON23IO_INDEX_P1N] = true;
	if (ls->m_bCabinetLights[LIGHT_BASS_RIGHT]) myLights[PYTHON23IO_INDEX_P2N] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_LEFT])	myLights[PYTHON23IO_INDEX_PAD1L] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_RIGHT])	myLights[PYTHON23IO_INDEX_PAD1R] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_UP])		myLights[PYTHON23IO_INDEX_PAD1U] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_DOWN])	myLights[PYTHON23IO_INDEX_PAD1D] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_LEFT])	myLights[PYTHON23IO_INDEX_PAD2L] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_RIGHT])	myLights[PYTHON23IO_INDEX_PAD2R] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_UP])		myLights[PYTHON23IO_INDEX_PAD2U] = true;
	if (ls.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_DOWN])	myLights[PYTHON23IO_INDEX_PAD2D] = true;

#endif
	
	return;
}

//BULK LIGHT UPDATES FOR SD, HD and P2IO CABINETS
void InputHandler_Python23IO::UpdateLightsP2IOSD()
{
	//simulate lights not updated here...
	memset(usb_light_message, 0, 2 * sizeof(uint8_t));// zero out the message

	
	if ( myLights[PYTHON23IO_INDEX_P2D]) usb_light_message[1] |= PYTHON23IO_USB_OUT_MARQUEE_LR; 
	if ( myLights[PYTHON23IO_INDEX_P2U]) usb_light_message[1] |= PYTHON23IO_USB_OUT_MARQUEE_UR; 
	if ( myLights[PYTHON23IO_INDEX_P1D]) usb_light_message[1] |= PYTHON23IO_USB_OUT_MARQUEE_LL; 
	if ( myLights[PYTHON23IO_INDEX_P1U]) usb_light_message[1] |= PYTHON23IO_USB_OUT_MARQUEE_UL;
	usb_light_message[1] |= PYTHON23IO_USB_OUT_P2_PANEL_UD; // always on (bridge mode I assume), p1/p2 swapped INTENTIONALLY for p2io
	usb_light_message[1] |= PYTHON23IO_USB_OUT_P1_PANEL_UD; // always on (bridge mode I assume), p1/p2 swapped INTENTIONALLY for p2io
	if ( myLights[PYTHON23IO_INDEX_P1S]) usb_light_message[1] |= PYTHON23IO_USB_OUT_P2_PANEL_LR; // p1/p2 swapped INTENTIONALLY for p2io
	if ( myLights[PYTHON23IO_INDEX_P2S]) usb_light_message[1] |= PYTHON23IO_USB_OUT_P1_PANEL_LR; // p1/p2 swapped INTENTIONALLY for p2io

	
	//to do better, i need to access the same device as input if I make this its own driver, but i cant have a static driver member...
	

	Board.writeLightsP2IO(usb_light_message); 
}

void InputHandler_Python23IO::UpdateLightsUSBSD()
{
	//simulate lights not updated here...
	memset(usb_light_message, 0, 5 * sizeof(uint8_t));// zero out the message
	

	//i am convinced the actual button lights are controlled over serial on HD cabinets
	if ( myLights[PYTHON23IO_INDEX_P1S]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P1_PANEL_LR;  //on hd cabinet buttons left LR is red spotlights satellite bottom
	if ( myLights[PYTHON23IO_INDEX_P1S]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P1_PANEL_UD; //on hd cabinet buttons left UD is red spotlights marquee top
	if ( myLights[PYTHON23IO_INDEX_P2S]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P2_PANEL_LR;//on hd cabinet buttons right LR is blue spotlights satellite bottom
	if ( myLights[PYTHON23IO_INDEX_P2S]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P2_PANEL_UD;//on hd cabinet buttons right UD is blue spotlights marquee top

	//the following do not control the spotlights in HD mode... see above.. other lights are over serial COM2, see extio lights driver
	if ( myLights[PYTHON23IO_INDEX_P2D]) usb_light_message[0] |= PYTHON23IO_USB_OUT_MARQUEE_LR; //in hd mode this is different
	if ( myLights[PYTHON23IO_INDEX_P2U]) usb_light_message[0] |= PYTHON23IO_USB_OUT_MARQUEE_UR; //in hd mode this is different
	if ( myLights[PYTHON23IO_INDEX_P1D]) usb_light_message[0] |= PYTHON23IO_USB_OUT_MARQUEE_LL; //in hd mode this is different
	if ( myLights[PYTHON23IO_INDEX_P1U]) usb_light_message[0] |= PYTHON23IO_USB_OUT_MARQUEE_UL; //in hd mode this is different

	
	//to do better, i need to access the same device as input if I make this its own driver, but i cant have a static driver member...
	

	Board.writeLights(usb_light_message); 
}




void InputHandler_Python23IO::UpdateLightsUSBHD()
{
	//simulate lights not updated here...
	memset(usb_light_message, 0, 5 * sizeof(uint8_t));// zero out the message
	
	if (myLights[PYTHON23IO_INDEX_P2D]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P1_PANEL_LR; //in hd mode this is different -- see above for why I do this
	if (myLights[PYTHON23IO_INDEX_P2U]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P2_PANEL_LR; //in hd mode this is different
	if (myLights[PYTHON23IO_INDEX_P1D]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P1_PANEL_UD; //in hd mode this is different
	if (myLights[PYTHON23IO_INDEX_P1U]) usb_light_message[0] |= PYTHON23IO_USB_OUT_P2_PANEL_UD; //in hd mode this is different


	//decide later of I want to hard reset a channel to zero or fade it out...

	//to do better, i need to access the same device as input if I make this its own driver, but i cant have a static driver member...
	Board.writeLights(usb_light_message);

	

}
//END BULK LIGHT UPDATES FOR SD AND HD CABINETS

void InputHandler_Python23IO::UpdateLightsVEXTIO()
{
	memset(vextio_message, 0, 4 * sizeof(uint8_t));// zero out the message
	
	//stuff for the extio
	if (myLights[PYTHON23IO_INDEX_PAD1L]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_LEFT, 1);
	if (myLights[PYTHON23IO_INDEX_PAD1R]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_RIGHT, 1);
	if (myLights[PYTHON23IO_INDEX_PAD1U]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_UP, 1);
	if (myLights[PYTHON23IO_INDEX_PAD1D]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_DOWN, 1);
	if (myLights[PYTHON23IO_INDEX_PAD2L]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_LEFT, 1);
	if (myLights[PYTHON23IO_INDEX_PAD2R]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_RIGHT, 1);
	if (myLights[PYTHON23IO_INDEX_PAD2U]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_UP, 1);
	if (myLights[PYTHON23IO_INDEX_PAD2D]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_DOWN, 1);
	if (myLights[PYTHON23IO_INDEX_P1N] || myLights[PYTHON23IO_INDEX_P2N]) vextio_message[2] |= EXTIO_OUT_NEON;

	vextio_message[0] |= 0x80;
	vextio_message[3] = ((vextio_message[0] + vextio_message[1] + vextio_message[2]) & 0xFF) & 0x7F;
	

	Board.writeVEXTIO(vextio_message, 4);
}

void InputHandler_Python23IO::ExtioSetPlayerPanel(int player, uint8_t panel, int state)
{
	//LOG->Info( "EXTIO: EXTIO setting player %d, panel %d",player, panel );
	player %= 2;

	if (state >0)
	{
		vextio_message[player] |= panel;
	}
	else
	{
		vextio_message[player] &= (~panel);
	}
	//LOG->Info( "Python23IO VEXTIO: EXTIO message is now %02X%02X%02X%02X",extio_message[0],extio_message[1],extio_message[2],extio_message[3] );


}

//TODO: migrate the RGB pattern stuff to lightsman 
//This is basically the logic to translate active lights into RGB patterns
void InputHandler_Python23IO::UpdateLightsHDXB()
{

	bool buttons_left=false;
	bool buttons_right=false;
	bool neons=false;
	uint8_t hdxb[]={0,0,0,0,0,0,0,0,0,0,0,0,0};

	
	red_bottom_count=myLights[PYTHON23IO_INDEX_P2D] ? upperCapAt(0x7F, red_bottom_count+2) : lowerCapAt(0x00, red_bottom_count - 1);
	blue_bottom_count=myLights[PYTHON23IO_INDEX_P2U] ? upperCapAt(0x7F, blue_bottom_count+2) : lowerCapAt(0x00, blue_bottom_count - 1);
	red_top_count=myLights[PYTHON23IO_INDEX_P1D] ? upperCapAt(0x7F, red_top_count+2) : lowerCapAt(0x00, red_top_count - 1);
	blue_top_count=myLights[PYTHON23IO_INDEX_P1U] ? upperCapAt(0x7F, blue_top_count+2) : lowerCapAt(0x00, blue_top_count - 1);
	neon_count=(myLights[PYTHON23IO_INDEX_P1N] || myLights[PYTHON23IO_INDEX_P2N]) ? upperCapAt(0x7F, neon_count + 2) : lowerCapAt(0x00, neon_count - 4);
	
	//order of speaker lights is GRB:: P1 Upper, p1 lower, P2 upper, P2 lower
	//actually may be rgb....

	//what is the mysterious byte 0? el byto mysteriouso~~
	hdxb[0]=0;//?
	//p1 up:
	hdxb[1]=neon_count&0xFF;//G
	hdxb[2]=red_top_count&0xFF;//R
	hdxb[3]=blue_top_count&0xFF;//B
	//p2 up:
	hdxb[4]=neon_count&0xFF;//G
	hdxb[5]=red_top_count&0xFF;//R
	hdxb[6]=blue_top_count&0xFF;//B
	//p1 down
	hdxb[7]=neon_count&0xFF;//G
	hdxb[8]=red_bottom_count&0xFF;//R
	hdxb[9]=blue_bottom_count&0xFF;//B
	//p2 down
	hdxb[10]=neon_count&0xFF;//G
	hdxb[11]=red_bottom_count&0xFF;//R
	hdxb[12]=blue_bottom_count&0xFF;//B

	//light button panels if needed
	if (myLights[PYTHON23IO_INDEX_P1S])
	{
		hdxb[1] |= 0x80;
		hdxb[2] |= 0x80;
		hdxb[3] |= 0x80;
	}
	if (myLights[PYTHON23IO_INDEX_P2S])
	{
		hdxb[4] |= 0x80; 
		hdxb[5] |= 0x80;
		hdxb[6] |= 0x80;
	}

	Board.writeHDXB(hdxb,0xd);
}

int16_t InputHandler_Python23IO::upperCapAt(int16_t cap,int16_t var)
{
	if (var > cap) return cap;
	return var;
}

int16_t InputHandler_Python23IO::lowerCapAt(int16_t cap,int16_t var)
{
	if (var < cap) return cap;
	return var;
}
