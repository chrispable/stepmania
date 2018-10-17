#pragma once
#ifndef INPUT_HANDLER_PYTHON23IO_H
#define INPUT_HANDLER_PYTHON23IO_H

#include "ProductInfo.h" // Used to look for PRODUCT_ID_BARE which means STEPMANIA 5, NOT OITG
#include "InputHandler.h"
#include "RageThreads.h"
#include "io/Python23IO.h"
#include "arch/COM/serial.h"
#include "Preference.h"

#ifdef WIN32
#include "windows.h"
#endif

#ifdef PRODUCT_ID_BARE
#include "arch/Lights/LightsDriver_Export.h"
#ifdef STDSTRING_H
#define PSTRING RString
#else
#define PSTRING std::string
#endif
#else
#include "arch/Lights/LightsDriver_External.h"
#define PSTRING CString
#endif

class InputHandler_Python23IO : public InputHandler
{
public:
	InputHandler_Python23IO();
	~InputHandler_Python23IO();

	//sm5 compatability
#ifdef PRODUCT_ID_BARE
	void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);
#else
	void GetDevicesAndDescriptions(vector<InputDevice>& vDevicesOut, vector<PSTRING>& vDescriptionsOut);
#endif

private:
	/* Allow only one handler to control the board at a time. More than one
	* handler may be loaded due to startup and Static.ini interactions, so
	* we need this to prevent obscure I/O problems. */
	static bool s_bInitialized;

	Python23IO Board;
	RageThread USBThread;

	RageThread USBBulkThread;
	void USBBulkThreadMain();
	void NOP();
	void NOPWithSleep();
	void GetCurrentLightsState();
	static int USBBulkThread_Start(void *p);
	static int16_t upperCapAt(int16_t cap,int16_t var);
	static int16_t lowerCapAt(int16_t cap,int16_t var);


	bool m_bFoundUSBDevice;
	bool m_bShutdown;
	int16_t red_top_count;
	int16_t red_bottom_count;
	int16_t blue_top_count;
	int16_t blue_bottom_count;
	int16_t neon_count;
	static uint8_t usb_light_message[5];// = { 0, 0, 0, 0, 0 };

	static Preference<PSTRING> m_sP23IOMode;// ("Python23IO_Mode", "SDP3IO");
	static Preference<bool> m_bP2IOEXTIO;// ("Python23IO_P2IO_EXTIO", true);
	static bool hasHDXB;// = true;
	static bool isP3IO;// = true;
	static bool HDCabinetLayout;// = false;



#define PYTHON23IO_NUM_STATES 22
#ifndef PRODUCT_ID_BARE
	const static int button_list[PYTHON23IO_NUM_STATES];
#else
	#ifndef STDSTRING_H

	#else
	
	const static DeviceButton button_list[PYTHON23IO_NUM_STATES];
	#endif
#endif
	bool previousStates[PYTHON23IO_NUM_STATES];
	static uint8_t packetBuffer[3];
	bool myLights[16];// = { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false };
	bool previousLights[16];// = { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true };
	static uint8_t vextio_message[];// = { 0, 0, 0, 0 };

	void USBThreadMain();
	static int USBThread_Start(void *p);
	void ProcessDataFromDriver();



	//lights stuff
	void UpdateLightsUSBSD();
	void UpdateLightsUSBHD();
	void UpdateLightsP2IOSD();
	void UpdateLightsHDXB();
	void UpdateLightsVEXTIO();
	void ExtioSetPlayerPanel(int player, uint8_t panel, int state);
	

#define PYTHON23IO_USB_OUT_P1_PANEL_LR	0x01
#define PYTHON23IO_USB_OUT_P2_PANEL_LR	0x02
#define PYTHON23IO_USB_OUT_P1_PANEL_UD	0x04
#define PYTHON23IO_USB_OUT_P2_PANEL_UD	0x08
#define PYTHON23IO_USB_OUT_MARQUEE_LR	0x10
#define PYTHON23IO_USB_OUT_MARQUEE_UR	0x20
#define PYTHON23IO_USB_OUT_MARQUEE_LL	0x40
#define PYTHON23IO_USB_OUT_MARQUEE_UL	0x80


#define PYTHON23IO_INDEX_P1U 0
#define PYTHON23IO_INDEX_P1D 1
#define PYTHON23IO_INDEX_P2U 2
#define PYTHON23IO_INDEX_P2D 3
#define PYTHON23IO_INDEX_P1S 4
#define PYTHON23IO_INDEX_P2S 5
#define PYTHON23IO_INDEX_P1N 6
#define PYTHON23IO_INDEX_P2N 7
#define PYTHON23IO_INDEX_PAD1U 8
#define PYTHON23IO_INDEX_PAD1D 9
#define PYTHON23IO_INDEX_PAD1L 10
#define PYTHON23IO_INDEX_PAD1R 11
#define PYTHON23IO_INDEX_PAD2U 12
#define PYTHON23IO_INDEX_PAD2D 13
#define PYTHON23IO_INDEX_PAD2L 14
#define PYTHON23IO_INDEX_PAD2R 15

//extio bits
#define EXTIO_OUT_B1	0x80// 8
#define EXTIO_OUT_UP	0x40// 7
#define EXTIO_OUT_DOWN	0x20// 6
#define EXTIO_OUT_LEFT	0x10// 5
#define EXTIO_OUT_RIGHT	0x08// 4
#define EXTIO_OUT_NEON	0x40// 7


};

#endif // INPUT_HANDLER_PYTHON23IO_H

