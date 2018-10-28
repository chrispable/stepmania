#include "global.h"
#include "LightsDriver_EXTIO.h"
#include "RageLog.h"
#include "GameState.h"
#include "RageThreads.h"
#include "PrefsManager.h"

#ifdef PRODUCT_ID_BARE
REGISTER_LIGHTS_DRIVER_CLASS(EXTIO);
#else
REGISTER_LIGHTS_DRIVER(EXTIO);
#endif
#if defined(WINDOWS) || defined(WIN32) || defined(_WIN32) || defined(_WINDOWS)
static Preference<PSTRING> m_EXTIO_COMPORT("EXTIOComPort", "COM1"); //default on windows
#else
static Preference<PSTRING> m_EXTIO_COMPORT("EXTIOComPort", "ttys0"); //default on *nix?
#endif

/*
Note: This driver is ONLY for EXTIOs that are connected directly to the com port
of the computer. In the case of a Python2io, there is a port on the IO itself
that is abstracted from the OS. You will configure that in the Python23IO driver
if you have connected to that port OR you will connect the EXTIO directly to the
computer, in the case of a Python3IO.

*/

//stepmania constructs
bool LightsDriver_EXTIO::s_bInitialized = false;
bool LightsDriver_EXTIO::lightsUpdated = true;
bool LightsDriver_EXTIO::m_bShutdown = false;

//extio constructs
serial::Serial LightsDriver_EXTIO::extio;
serial::Timeout LightsDriver_EXTIO::serial_timeout = serial::Timeout::simpleTimeout(1000);
uint8_t LightsDriver_EXTIO::extio_message[] = { 0, 0, 0, 0 };


void LightsDriver_EXTIO::Set(const LightsState *ls)
{
	//blank this out
}


LightsDriver_EXTIO::LightsDriver_EXTIO()
{
	if (s_bInitialized) return;
	s_bInitialized = true; //only one instance should do this
	m_bShutdown = false;


	LOG->Info("EXTIO: Initializing EXTIO Light drivers...");
	LOG->Info("EXTIO: Configured EXTIO serial port: %s", m_EXTIO_COMPORT.Get().c_str());

	//initialize light defaults
	for (int i = 0; i < 10; i++)
	{
		myLights[i] = false;
		previousLights[i] = true;
	}
	//check if we have a valid com port name for EXTIO
	if (m_EXTIO_COMPORT.Get().length()>1)
	{
		//this is nice because EXTIO code will now only run after this SINGLE check
		EXTIOThread.SetName("EXTIO thread");
		EXTIOThread.Create(EXTIOThread_Start, this);
	}


}

LightsDriver_EXTIO::~LightsDriver_EXTIO()
{
	m_bShutdown = true;
	if (EXTIOThread.IsCreated())
	{
		LOG->Info("EXTIO: Shutting down EXTIO serial thread...");
		EXTIOThread.Wait();
		LOG->Info("EXTIO: EXTIO serial thread shut down.");
	}
}

void LightsDriver_EXTIO::UpdateLightsEXTIO()
{
	//itg lights are DUMB AS FUCK, you can fix this by setting BlinkGameplayButtonLightsOnNote=0 in static.ini

	//we may have gotten an update packet but pad lights may not have changed
	if (!memcmp(myLights, previousLights, sizeof(myLights))) {
		return;
	}

	memcpy(previousLights, myLights, sizeof(myLights)); //save current lights as previous lights

	memset(extio_message, 0, 4 * sizeof(uint8_t));// zero out the message

	
	//stuff for the extio
	if (myLights[0]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_LEFT, 1);
	if (myLights[1]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_RIGHT, 1);
	if (myLights[2]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_UP, 1);
	if (myLights[3]) ExtioSetPlayerPanel(PLAYER_1, EXTIO_OUT_DOWN, 1);
	if (myLights[4]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_LEFT, 1);
	if (myLights[5]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_RIGHT, 1);
	if (myLights[6]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_UP, 1);
	if (myLights[7]) ExtioSetPlayerPanel(PLAYER_2, EXTIO_OUT_DOWN, 1);
	if (myLights[8] || myLights[9]) extio_message[2] |= EXTIO_OUT_NEON;
	//LOG->Info("EXTIO: write EXTIO packets");
	WriteExtioPacket();
}

void LightsDriver_EXTIO::EXTIOThreadMain()
{
	extio.setPort(m_EXTIO_COMPORT.Get().c_str()); //on a ddr pcb, extio must reside on COM1
	extio.setBaudrate(38400);
	extio.setTimeout(serial_timeout);
	extio.open();
	LOG->Info("EXTIO: Opened EXTIO");
	//all P2/P3IO have the extio as pad lights so lets make sure that is good

	while (!m_bShutdown)
	{
		//LOG->Info("EXTIO: EXTIO POLL");
		UpdateLightsPoll();
		//LOG->Info("EXTIO: EXTIO UPDATE");
		UpdateLightsEXTIO();
		usleep(16666); //60 times per sec

	}

	//turn off pad lights
	memset(extio_message, 0, 4 * sizeof(uint8_t));// zero out the message
	WriteExtioPacket();

}

void LightsDriver_EXTIO::ExtioSetPlayerPanel(int player, uint8_t panel, int state)
{
	//LOG->Info( "EXTIO: EXTIO setting player %d, panel %d",player, panel );
	player %= 2;

	if (state >0)
	{
		extio_message[player] |= panel;
	}
	else
	{
		extio_message[player] &= (~panel);
	}
	//LOG->Info( "EXTIO: EXTIO message is now %02X%02X%02X%02X",extio_message[0],extio_message[1],extio_message[2],extio_message[3] );


}

//encapsulates the extio set bit and checksum and reconnect logic to make our job simpler
void LightsDriver_EXTIO::WriteExtioPacket()
{
	int bytesread = -1;
	extio_message[0] |= EXTIO_OUT_B1;
	uint8_t crc = (extio_message[0] + extio_message[1] + extio_message[2]) & 0xFF;
	crc &= 0x7F;
	extio_message[3] = crc;
	//LOG->Info( " after crc EXTIO %02X%02X%02X%02X",extio_message[0],extio_message[1],extio_message[2],extio_message[3] );



	if (!extio.isOpen()) //Not open? Try to open it before we continue;
	{
		//LOG->Info( "EXTIO is NOT open" );
		extio.open();
	}

	if (extio.isOpen())
	{
		int byteswritten = extio.write(extio_message, 4);
		if (byteswritten == 4)
		{
			//LOG->Info( "Wrote to EXTIO %02X%02X%02X%02X",extio_message[0],extio_message[1],extio_message[2],extio_message[3] );
			uint8_t response[] = { 0, 0 };
			bytesread = extio.read(response, 1);
			if (bytesread >= 0)
			{
				//LOG->Info( "Read %d bytes from EXTIO %02X%02X",bytesread,response[0],response[1] );
			}
			else
			{
				//LOG->Info( "Read nothing from EXTIO." );
			}
		}
		else
		{
			//LOG->Info( "Could not write full amount (only %d/4) to EXTIO.",byteswritten );

		}
	}
}



//No need to look at these, they are trampolines and stubs
int LightsDriver_EXTIO::EXTIOThread_Start(void *p)
{
	((LightsDriver_EXTIO *)p)->EXTIOThreadMain();
	return 0;
}

void LightsDriver_EXTIO::UpdateLightsPoll()
{
	memset(myLights, false, sizeof(myLights));// zero out the message

	//stuff for the extio
#ifdef PRODUCT_ID_BARE
	LightsState l = LightsDriver_Export::GetState();
	if (l.m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT])		myLights[0] = true;
	if (l.m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT])	myLights[1] = true;
	if (l.m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP])		myLights[2] = true;
	if (l.m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN])		myLights[3] = true;
	if (l.m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT])		myLights[4] = true;
	if (l.m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT])	myLights[5] = true;
	if (l.m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP])		myLights[6] = true;
	if (l.m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN])		myLights[7] = true;
#else
	const LightsState *ls = LightsDriver_External::Get();
	if (l.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_LEFT])	myLights[0] = true;
	if (l.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_RIGHT])	myLights[1] = true;
	if (l.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_UP])		myLights[2] = true;
	if (l.m_bGameButtonLights[GAME_CONTROLLER_1][DANCE_BUTTON_DOWN])	myLights[3] = true;
	if (l.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_LEFT])	myLights[4] = true;
	if (l.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_RIGHT])	myLights[5] = true;
	if (l.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_UP])		myLights[6] = true;
	if (l.m_bGameButtonLights[GAME_CONTROLLER_2][DANCE_BUTTON_DOWN])	myLights[7] = true;
#endif
	if (l.m_bCabinetLights[LIGHT_BASS_LEFT])							myLights[8] = true;
	if (l.m_bCabinetLights[LIGHT_BASS_RIGHT])							myLights[9] = true;
}

