#include "global.h"
#include "StepMania.h" //to assist with debugging
#include "RageLog.h"
#include "RageUtil.h"	// for ssprintf, arraylen

#include "io/Python23IO.h"
#include "arch/USB/USBDriver_Impl.h"


serial::Serial Python23IO::com4;
uint8_t Python23IO::acio_request[256];
uint8_t Python23IO::acio_response[256];

// io correlated pid/vid
//first set is what a chimera uses, second set is a real ddr io (from an hd black cab), 
const uint16_t Python23IO::python3io_VENDOR_ID[2]={ 0x0000, 0x1CCF };
const uint16_t Python23IO::python3io_PRODUCT_ID[2]={ 0x5731, 0x8008 };
const uint16_t Python23IO::python2io_VENDOR_ID[1]= { 0x0000 };
const uint16_t Python23IO::python2io_PRODUCT_ID[1]= { 0x7305 };

uint8_t Python23IO::python23io_request[256];
uint8_t Python23IO::python23io_response[256];
uint8_t Python23IO::pchunk[3][4];

PSTRING Python23IO::m_sInputError;
int Python23IO::m_iInputErrorCount = 0;
uint8_t Python23IO::packet_sequence = 0;

//these get changed for p2io but they are correct for p3io
uint8_t Python23IO::hdxb_vcom_port = 0x00;//p3io setting
uint8_t Python23IO::hxdb_vbaud_rate = 0x03; //3 on p3io, 2 on p2io

/////////////////////////// below is p2io only
uint8_t Python23IO::extio_vcom_port = 0x00;//p2io only
uint8_t Python23IO::extio_vbaud_rate = 0x03;//p2io only

Preference<PSTRING> Python23IO::m_COM4PORT("Python23IO_HDXB_PORT", ""); //COM2 on unicorn tail
Preference<PSTRING> Python23IO::m_sP23IOMode("Python23IO_Mode", "SDP3IO");
Preference<bool> Python23IO::m_bP2IOEXTIO("Python23IO_P2IO_EXTIO", true);
Preference<int> Python23IO::m_iP2IO_HDXB_DEV_ID("Python23IO_HDXB_DEV_ID", 3);
uint8_t Python23IO::hdxb_dev_id=3; // if user connects ONLY HDXB this they can change this but really they shouldn't
int Python23IO::bulk_reply_size=0;




bool Python23IO::DeviceMatches(int iVID, int iPID)
{
	if (strcasecmp(m_sP23IOMode.Get().c_str(), "HDP2IO") == 0 || strcasecmp(m_sP23IOMode.Get().c_str(), "SDP2IO") == 0)
	{
		int NUM_python2io_CHECKS_IDS = ARRAYLEN(python2io_PRODUCT_ID);
		
		for (int i = 0; i < NUM_python2io_CHECKS_IDS; ++i)
			if (iVID == python2io_VENDOR_ID[i] && iPID == python2io_PRODUCT_ID[i])
				return true;
	}
	else
	{
		int NUM_python3io_CHECKS_IDS = ARRAYLEN(python3io_PRODUCT_ID);
		for (int i = 0; i < NUM_python3io_CHECKS_IDS; ++i)
			if (iVID == python3io_VENDOR_ID[i] && iPID == python3io_PRODUCT_ID[i])
				return true;
	}
	return false;
}

bool Python23IO::Open()
{
	hdxb_vcom_port = 0x00;
	hxdb_vbaud_rate = 0x03; //this is 2 on p2io
	
	extio_vcom_port = 0x00;//p2io only
	extio_vbaud_rate = 0x03;//p2io only
	//init
	m_bConnected = false;
	baud_pass  = false;
	hdxb_ready = false;
	board_isP2IO = false;
	board_hasHDXB = false;
	board_hasVEXTIO = false;

	//init previous lights
	int i=0;

	if (m_iP2IO_HDXB_DEV_ID.Get()>0 && m_iP2IO_HDXB_DEV_ID.Get() < 255)
	{
		hdxb_dev_id = m_iP2IO_HDXB_DEV_ID.Get();
	}
	if (m_COM4PORT.Get().length()>1)
	{
		COM4SET=true;
		com4.setPort(m_COM4PORT.Get().c_str());
	}

	packet_sequence = 0;
	
	board_isP2IO = false;
	if (strcasecmp(m_sP23IOMode.Get().c_str(), "HDP2IO") == 0)
	{
		board_isP2IO = true;
		board_hasHDXB = true;

	}
	if (strcasecmp(m_sP23IOMode.Get().c_str(), "SDP2IO") == 0)
	{
		board_isP2IO = true;
		board_hasHDXB = false;
	}
	if ((strcasecmp(m_sP23IOMode.Get().c_str(), "HDP3IO") == 0) ||  strcasecmp(m_sP23IOMode.Get().c_str(), "SDP3IOHDXB") == 0)
	{
		board_hasHDXB = true;
	}
	
	if (board_isP2IO)
	{
		hxdb_vbaud_rate = 0x02;
		hdxb_vcom_port = 0x01;
		int NUM_python2io_CHECKS_IDS = ARRAYLEN(python2io_PRODUCT_ID);
		for (int i = 0; i < NUM_python2io_CHECKS_IDS; ++i)
		{
			if (OpenInternal(python2io_VENDOR_ID[i], python2io_PRODUCT_ID[i]))
			{
				LOG->Info("Python23IO P2IO Driver:: Connected to index %d", i);
				m_bConnected = true;
				FlushBulkReadBuffer();
			}
		}
		if (!m_bConnected) return false;

	}
	else
	{
		int NUM_python3io_CHECKS_IDS = ARRAYLEN(python3io_PRODUCT_ID);
		for (int i = 0; i < NUM_python3io_CHECKS_IDS; ++i)
		{
			if (OpenInternal(python3io_VENDOR_ID[i], python3io_PRODUCT_ID[i]))
			{
				LOG->Info("Python23IO P3IO Driver:: Connected to index %d", i);
				m_bConnected = true;
				LOG->Info("init Python23IO P3IO watch dog ");
				InitHDAndWatchDog();
			}
		}
		if (!m_bConnected) return false;
	}

	if (board_isP2IO)
	{
		board_hasVEXTIO = m_bP2IOEXTIO.Get();
		if (board_hasVEXTIO && m_bConnected)
		{
			m_pDriver->SetConfiguration(1);
			openVEXTIO();
		}
	}

	
	if (board_hasHDXB)
	{
		openHDXB();
		usleep(906250); //capture waits this long
	}
	if (!board_isP2IO)
	{
		sendUnknownCommand();
	}
	FlushBulkReadBuffer();

	if (board_hasHDXB)
	{
		baud_pass = spamBaudCheck();

		// say the baud check passed anyway
		baud_pass = true;


		if (baud_pass)
		{
			nodeCount();
			getVersion();
			initHDXB2();
			initHDXB3();
			initHDXB4();
			pingHDXB();
			HDXBAllOnTest();
			hdxb_ready = true;
		}
	}

	LOG->Info("**************Python23IO board specs: %s isp2io:%d hashdxb:%d hasvextio:%d", m_sP23IOMode.Get().c_str(),board_isP2IO, board_hasHDXB, board_hasVEXTIO);
	return m_bConnected;
}

void Python23IO::HDXBAllOnTest()
{
	uint8_t all_on[0x0d]={
	0x00,
	0xff,
	0xff,
	0xff,
	0xff,
	0xff,
	0xff,
	0x7f,
	0x7f,
	0x7f,
	0x7f,
	0x7f,
	0x7f
	};
	LOG->Info("**************HDXB ALL ON TEST:");
	writeHDXB(all_on, 0xd, HDXB_SET_LIGHTS);
}



//ok seriously what was I drinking / smoking here
//i dont even know how or why this works but it does
//basically we need to get all 3 4-byte chunks read in for this to be COMPLETE
//but the maximum payload size is 16 bytes?  so I guess theres a circumstance when it could poop out 16?
//using libusb1.x I could only get 4x chunks, and 0.x seems to give 12 byte chunks.. I just dont even know anymore
//someone smarter than me make this better please
bool Python23IO::interruptRead(uint8_t* dataToQueue)
{
	m_iInputErrorCount = 0;
	int iExpected = 16; //end point size says max packet of 16, but with libusb1 I only ever got 12 in chunks of 4 every consequtive read
	uint8_t chunk[4][4] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t tchunk[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	bool interruptChunkFlags[3] = { false, false, false }; //i guess only 12 is needed? lets work with that
	bool allChunks = false;
	int currChunk = 0;
	while (!allChunks)
	{
		int iResult = m_pDriver->InterruptRead(interrupt_ep, (char*)tchunk, iExpected, REQ_TIMEOUT);

		//LOG->Warn( "Python23IO read, returned %i: %s\n", iResult, m_pDriver->GetError() );
		if (iResult>0)
		{
			m_iInputErrorCount = 0;
			//LOG->Info("Python23IO Got %d bytes of data: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",iResult, tchunk[0],tchunk[1],tchunk[2],tchunk[3],tchunk[4],tchunk[5],tchunk[6],tchunk[7],tchunk[8],tchunk[9],tchunk[10],tchunk[11]);
			if (iResult >= 12)
			{
				//we have EVERYTHING
				interruptChunkFlags[0] = true;
				interruptChunkFlags[1] = true;
				interruptChunkFlags[2] = true;
				for (int i = 0; i<iResult; i++)
				{
					chunk[i / 4][i % 4] = tchunk[i]; //array data is continuous
				}
			}
			else
			{
				if (iResult == 4)
				{
					//if we get the beginning, reset ourselves
					if (tchunk[0] == 0x80)
					{
						interruptChunkFlags[0] = false;
						interruptChunkFlags[1] = false;
						interruptChunkFlags[2] = false;
						currChunk = 0;
					}
					for (int i = 0; i<4; i++)
					{
						chunk[currChunk][i] = tchunk[i];
					}
					interruptChunkFlags[currChunk] = true;
					currChunk++;
				}
				else
				{
					//LOG->Info("Python23IO driver got weird number of bytes (%d) on an interrupt read!",iResult);
				}
			}


			//check all chunks are accounted for
			if (interruptChunkFlags[0] == true)
			{
				if (interruptChunkFlags[1] == true)
				{
					if (interruptChunkFlags[2] == true)
					{
						allChunks = true;
					}
				}
			}


		}
		else
		{
			//we got nothing, increment error and then spit out last state
			m_iInputErrorCount++;
		}
		if (m_iInputErrorCount>2)
		{
			break;
		}
	}

	//if we didnt get what we need, return the previous data
	if (allChunks == false)
	{
		for (int i = 0; i<3; i++)
		{
			for (int j = 0; j<4; j++)
			{
				chunk[i][j] = pchunk[i][j];
			}
		}
	}
	else //copy our current state to the previous state
	{
		for (int i = 0; i<3; i++)
		{
			for (int j = 0; j<4; j++)
			{
				pchunk[i][j] = chunk[i][j];
			}
		}
	}

	//format the data in the way we expect -- harkens back to the UDP Python23IO streamer
	uint8_t DDR_P1_PAD_UP = checkInput(pchunk[0][1], DDR_PAD_UP);
	uint8_t DDR_P1_PAD_DOWN = checkInput(pchunk[0][1], DDR_PAD_DOWN);
	uint8_t DDR_P1_PAD_LEFT = checkInput(pchunk[0][1], DDR_PAD_LEFT);
	uint8_t DDR_P1_PAD_RIGHT = checkInput(pchunk[0][1], DDR_PAD_RIGHT);

	uint8_t DDR_P1_CP_UP = checkInput(pchunk[0][3], DDR_CP_UP_P1);
	uint8_t DDR_P1_CP_DOWN = checkInput(pchunk[0][3], DDR_CP_DOWN_P1);
	uint8_t DDR_P1_CP_LEFT = checkInput(pchunk[0][1], DDR_CP_LEFT);
	uint8_t DDR_P1_CP_RIGHT = checkInput(pchunk[0][1], DDR_CP_RIGHT);
	uint8_t DDR_P1_CP_SELECT = checkInput(pchunk[0][1], DDR_CP_SELECT);

	uint8_t DDR_P2_PAD_UP = checkInput(pchunk[0][2], DDR_PAD_UP);
	uint8_t DDR_P2_PAD_DOWN = checkInput(pchunk[0][2], DDR_PAD_DOWN);
	uint8_t DDR_P2_PAD_LEFT = checkInput(pchunk[0][2], DDR_PAD_LEFT);
	uint8_t DDR_P2_PAD_RIGHT = checkInput(pchunk[0][2], DDR_PAD_RIGHT);

	uint8_t DDR_P2_CP_UP = checkInput(pchunk[0][3], DDR_CP_UP_P2);
	uint8_t DDR_P2_CP_DOWN = checkInput(pchunk[0][3], DDR_CP_DOWN_P2);
	uint8_t DDR_P2_CP_LEFT = checkInput(pchunk[0][2], DDR_CP_LEFT);
	uint8_t DDR_P2_CP_RIGHT = checkInput(pchunk[0][2], DDR_CP_RIGHT);
	uint8_t DDR_P2_CP_SELECT = checkInput(pchunk[0][2], DDR_CP_SELECT);

	uint8_t DDR_OP_TEST = checkInput(pchunk[0][3], DDR_TEST);
	uint8_t DDR_OP_SERVICE = checkInput(pchunk[0][3], DDR_SERVICE);
	uint8_t DDR_OP_COIN1 = checkInput(pchunk[0][3], DDR_COIN);
	//uint8_t DDR_OP_COIN2=checkInput(pchunk[0][3],DDR_COIN);

	//init the memory... yeah I'm lazy
	dataToQueue[0] = 0;
	dataToQueue[1] = 0;
	dataToQueue[2] = 0;
	if (DDR_P1_PAD_UP) dataToQueue[0] |= BYTE_BIT_M_TO_L(0);
	if (DDR_P1_PAD_DOWN) dataToQueue[0] |= BYTE_BIT_M_TO_L(1);
	if (DDR_P1_PAD_LEFT) dataToQueue[0] |= BYTE_BIT_M_TO_L(2);
	if (DDR_P1_PAD_RIGHT) dataToQueue[0] |= BYTE_BIT_M_TO_L(3);
	if (DDR_P2_PAD_UP) dataToQueue[0] |= BYTE_BIT_M_TO_L(4);
	if (DDR_P2_PAD_DOWN) dataToQueue[0] |= BYTE_BIT_M_TO_L(5);
	if (DDR_P2_PAD_LEFT) dataToQueue[0] |= BYTE_BIT_M_TO_L(6);
	if (DDR_P2_PAD_RIGHT) dataToQueue[0] |= BYTE_BIT_M_TO_L(7);

	if (DDR_P1_CP_UP) dataToQueue[1] |= BYTE_BIT_M_TO_L(0); //up and down buttons constantly pressed when reading with libusbk....
	if (DDR_P1_CP_DOWN) dataToQueue[1] |= BYTE_BIT_M_TO_L(1); //need to get captures of these inputs with oitg to figure out whats up
	if (DDR_P1_CP_LEFT) dataToQueue[1] |= BYTE_BIT_M_TO_L(2);
	if (DDR_P1_CP_RIGHT) dataToQueue[1] |= BYTE_BIT_M_TO_L(3);
	if (DDR_P2_CP_UP) dataToQueue[1] |= BYTE_BIT_M_TO_L(4);//so for now I commented them out
	if (DDR_P2_CP_DOWN) dataToQueue[1] |= BYTE_BIT_M_TO_L(5);//so the game is playable
	if (DDR_P2_CP_LEFT) dataToQueue[1] |= BYTE_BIT_M_TO_L(6);
	if (DDR_P2_CP_RIGHT) dataToQueue[1] |= BYTE_BIT_M_TO_L(7);

	if (DDR_OP_SERVICE) dataToQueue[2] |= BYTE_BIT_M_TO_L(0);
	if (DDR_OP_TEST) dataToQueue[2] |= BYTE_BIT_M_TO_L(1);
	if (DDR_OP_COIN1) dataToQueue[2] |= BYTE_BIT_M_TO_L(2);
	//if (DDR_OP_COIN2) dataToQueue[2] |= BYTE_BIT_M_TO_L(3); //my second coin switch is broken, I am just assuming this is the right constant...
	if (DDR_P1_CP_SELECT) dataToQueue[2] |= BYTE_BIT_M_TO_L(4);
	if (DDR_P2_CP_SELECT) dataToQueue[2] |= BYTE_BIT_M_TO_L(5);

	if (!allChunks){
		Close();
	}
	return allChunks;

}

bool Python23IO::sendUnknownCommand()
{
	//if (COM4SET) return true;
	uint8_t unknown[] = {
	0xaa,
	0x04,
	0x0a,
	0x32,
	0x01,
	0x00
	};
	LOG->Info("**************HDXB UNKNOWN COMMAND:");
	return WriteToBulkWithExpectedReply( unknown, false, true);
}


//sent every 78125us...
bool Python23IO::pingHDXB()
{
if (COM4SET)
	{
		//LOG->Info("**************HDXB Serial PING:");
		uint8_t hdxb_ping_command[] = {
			0xaa,//ACIO_PACKET_START
			hdxb_dev_id,//HDXB
			0x01,//type?
			0x10,//opcode
			0x00,
			0x00,
			0x11 + hdxb_dev_id//CHECKSUM 
		};
		com4.write(hdxb_ping_command,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
	}
	else
	{
		uint8_t hdxb_ping_command[] = {
			0xaa,
			0x0b,
			0x02,
			0x3a,
			hdxb_vcom_port,
			0x07,
			0xaa,//ACIO_PACKET_START
			hdxb_dev_id,//HDXB
			0x01,//type?
			0x10,//opcode
			0x00,
			0x00,
			0x11 + hdxb_dev_id//CHECKSUM 
		};
		//LOG->Info("**************HDXB Python23IO PING:");
		WriteToBulkWithExpectedReply(hdxb_ping_command, false, true);
		return readHDXB(0x7e); // now flush it according to captures
	}
	return true;

}

bool Python23IO::spamBaudCheck()
{
	bool found_baud=false;
	if (COM4SET)
	{
		LOG->Info("**************HDXB Serial BAUD Routine:");
		found_baud= ACIO::baudCheck(com4);
	}
	else
	{
		uint8_t com_baud_command[] = {
		0xaa, // packet start
		0x0f, // length
		0x05, // sequence
		0x3a, // com write
		hdxb_vcom_port, // virtual com port
		0x0b, // num bytes
		0xaa, // baud check, escaped 0xaa
		0xaa,	0xaa,	0xaa,	0xaa,	0xaa,	0xaa,	0xaa,	0xaa,	0xaa,	0xaa	};

		for (int i=0;i<50;i++)
		{
			LOG->Info("**************HDXB Python23IO BAUD:");
			WriteToBulkWithExpectedReply(com_baud_command, false, true);
			readHDXB(0x40);
			if (bulk_reply_size>=16 && python23io_response[1]==0x0F && python23io_response[4]==0x0B &&  python23io_response[5]==0xAA && python23io_response[6]==0xAA && python23io_response[7]==0xAA && python23io_response[8]==0xAA && python23io_response[9]==0xAA && python23io_response[10]==0xAA && python23io_response[11]==0xAA && python23io_response[12]==0xAA && python23io_response[13]==0xAA && python23io_response[14]==0xAA && python23io_response[15]==0xAA)
			{
			
				LOG->Info("**************HDXB Python23IO GOT BAUD RATE! Flushing...");
				readHDXB(0x40); // now flush it
			
				readHDXB(0x40); // now flush it
				found_baud=true;
				break;
			}
		}
	}

	return found_baud;
	

}

bool Python23IO::nodeCount()
{
	if (COM4SET)
	{
		uint8_t nodes[]={0xaa,0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02}; 
		com4.write(nodes,8);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);//flush
		return true;
	}
	else
	{
		// 00    00    01    00    PP    07    CC -- 01 is the command for enumeration, PP is 01 (payload length), 07 is the payload with a reply of 7 in it, CC is checksum
		uint8_t nodes[]={0xaa,0x0e,0x01,0x3a,hdxb_vcom_port,0x08,0xaa,0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02}; 
		LOG->Info("**************HDXB Python23IO Get Node Count:");
		WriteToBulkWithExpectedReply(nodes, false, true);
		readHDXB(0x40);
		return readHDXB(0x40); // now flush it
	}
}

bool Python23IO::getVersion()
{
	if(COM4SET)
	{
		uint8_t ICCB_1[]={
			0xaa,//ACIO START
			0x01,//ICCB1
			0x00,//TYPE?
			0x02,//OPCODE
			0x00,
			0x00,
			0x03,
		};
		uint8_t ICCB_2[]={
			0xaa,//ACIO START
			0x02,//ICCB2
			0x00,//TYPE?
			0x02,//OPCODE
			0x00,
			0x00,
			0x04,
		};
		uint8_t HDXB[]={
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x00,//TYPE?
			0x02,//OPCODE
			0x00,
			0x00,
			2 + hdxb_dev_id,
			//0x05,
		};
		LOG->Info("**************ICCB1 Serial Get Version:");

		com4.write(ICCB_1,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		LOG->Info("**************ICCB2 Serial Get Version:");
		com4.write(ICCB_2,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		LOG->Info("**************HDXB Serial Get Version:");
		com4.write(HDXB,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		return true;
	}
	else
	{
		//should build this but.. meh... lazy, manually define it
		uint8_t ICCB_1[] = {
			0xaa,
			0x0d,
			0x01,
			0x3a,
			hdxb_vcom_port,
			0x07,
			0xaa,//ACIO START
			0x01,//ICCB1
			0x00,//TYPE?
			0x02,//OPCODE
			0x00,
			0x00,
			0x03,
		};
		uint8_t ICCB_2[] = {
			0xaa,
			0x0d,
			0x01,
			0x3a,
			hdxb_vcom_port,
			0x07,
			0xaa,//ACIO START
			0x02,//ICCB2
			0x00,//TYPE?
			0x02,//OPCODE
			0x00,
			0x00,
			0x04,
		};
		uint8_t HDXB[] = {
			0xaa,
			0x0d,
			0x01,
			0x3a,
			hdxb_vcom_port,
			0x07,
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x00,//TYPE?
			0x02,//OPCODE
			0x00,
			0x00,
			0x02 + hdxb_dev_id,
		};
		if (hdxb_dev_id > 2)
		{
			LOG->Info("**************ICCB1 Python23IO Get Version:");
			WriteToBulkWithExpectedReply(ICCB_1, false, true);
			readHDXB(0x7e);
			readHDXB(0x7e);
			LOG->Info("**************ICCB2 Python23IO Get Version:");
			WriteToBulkWithExpectedReply(ICCB_2, false, true);
			readHDXB(0x7e);
			readHDXB(0x7e);
		}
		LOG->Info("**************HDXB Python23IO Get Version:");
		WriteToBulkWithExpectedReply(HDXB, false, true);
		readHDXB(0x7e);
		return readHDXB(0x7e);
	}
}

bool Python23IO::initHDXB2()
{
	if(COM4SET)
	{
		//should build this but.. meh... lazy, manually define it
		//init into opcode 3?
		uint8_t hdxb_op3[]={
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x00,//TYPE?
			0x03,//OPCODE
			0x00,
			0x00,
			3 + hdxb_dev_id,
			//0x06,
		};

		uint8_t ICCB1_op3[]={0xaa,0x01,0x00,0x03,0x00,0x00,0x04};
		uint8_t ICCB2_op3[]={0xaa,0x02,0x00,0x03,0x00,0x00,0x05};
	
		LOG->Info("**************ICCB1 Serial INIT mode 3:");
		com4.write(ICCB1_op3,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		LOG->Info("**************ICCB2 Serial INIT mode 3:");
		com4.write(ICCB2_op3,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		LOG->Info("**************HDXB Serial INIT mode 3:");
		com4.write(hdxb_op3,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);// now flush it according to captures
		return true; 
	}
	else
	{
		if (!m_bConnected) return false;
		//should build this but.. meh... lazy, manually define it
		//init into opcode 3?
		uint8_t hdxb_op3[]={
			0xaa,
			0x0d,
			0x01,
			0x3a,
			hdxb_vcom_port,
			0x07,
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x00,//TYPE?
			0x03,//OPCODE
			0x00,
			0x00,
			3 + hdxb_dev_id
			//0x06,
		};

		uint8_t ICCB1_op3[]={0xaa,0x0d,0x02,0x3a,hdxb_vcom_port,0x07,0xaa,0x01,0x00,0x03,0x00,0x00,0x04};
		uint8_t ICCB2_op3[]={0xaa,0x0d,0x03,0x3a,hdxb_vcom_port,0x07,0xaa,0x02,0x00,0x03,0x00,0x00,0x05};
	
		LOG->Info("**************ICCB1 Python23IO INIT mode 3:");
		WriteToBulkWithExpectedReply(ICCB1_op3, false, true);
		readHDXB(0x7e);
		LOG->Info("**************ICCB2 Python23IO INIT mode 3:");
		WriteToBulkWithExpectedReply(ICCB2_op3, false, true);
		readHDXB(0x7e);
		LOG->Info("**************HDXB Python23IO INIT mode 3:");
		WriteToBulkWithExpectedReply(hdxb_op3, false, true);
		readHDXB(0x7e);
		return readHDXB(0x7e); // now flush it according to captures
	}
}


bool Python23IO::initHDXB3()
{
	if(COM4SET)
	{
		//should build this but.. meh... lazy, manually define it
		uint8_t hdxb_type[]={
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x01,//TYPE?
			0x00,//OPCODE
			0x00,
			0x00,
			1+hdxb_dev_id,
			//0x04,
		};
		uint8_t iccb1_type[]={0xaa,0x01,0x01,0x00,0x00,0x00,0x02};
		uint8_t iccb2_type[]={0xaa,0x02,0x01,0x00,0x00,0x00,0x03};
	
		LOG->Info("**************ICCB1 Serial INIT type 0:");
		com4.write(iccb1_type,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		LOG->Info("**************ICCB2 Serial  INIT type 0:");
		com4.write(iccb2_type,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		LOG->Info("**************HDXB Serial  INIT type 0:");
		com4.write(hdxb_type,7);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		return true;
	}
	else
	{

		//should build this but.. meh... lazy, manually define it
		uint8_t hdxb_type[]={
			0xaa,
			0x0d,
			0x01,
			0x3a,
			0x00,
			0x07,
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x01,//TYPE?
			0x00,//OPCODE
			0x00,
			0x00,
			1 + hdxb_dev_id
			//0x04,
		};
		uint8_t iccb1_type[]={0xaa,0x0b,0x0a,0x3a,hdxb_vcom_port,0x07,0xaa,0x01,0x01,0x00,0x00,0x00,0x02};
		uint8_t iccb2_type[]={0xaa,0x0b,0x0e,0x3a,hdxb_vcom_port,0x07,0xaa,0x02,0x01,0x00,0x00,0x00,0x03};
	
		LOG->Info("**************ICCB1 Python23IO INIT type 0:");
		WriteToBulkWithExpectedReply(iccb1_type, false, true);
		readHDXB(0x7e);
		readHDXB(0x7e); // now flush it according to captures
		LOG->Info("**************ICCB2 Python23IO INIT type 0:");
		WriteToBulkWithExpectedReply(iccb2_type, false, true);
		readHDXB(0x7e);
		readHDXB(0x7e); // now flush it according to captures
		LOG->Info("**************HDXB Python23IO INIT type 0:");
		WriteToBulkWithExpectedReply(hdxb_type, false, true);
		readHDXB(0x7e);
		return readHDXB(0x7e); // now flush it according to captures
	}
}


bool Python23IO::initHDXB4()
{
	if(COM4SET)
	{
		//should build this but.. meh... lazy, manually define it
		uint8_t breath_of_life[]={
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x01,//TYPE?
			0x28,//OPCODE
			0x00,
			0x02,
			0x00,
			0x00,
			0x2b + hdxb_dev_id
		};
		LOG->Info("**************HDXB Serial INIT4:");
		com4.write(breath_of_life,9);
		usleep(72000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
		return true;
	}
	else
	{
		//should build this but.. meh... lazy, manually define it
		uint8_t breath_of_life[]={
			0xaa,
			0x0d,
			0x01,
			0x3a,
			hdxb_vcom_port,
			0x09,
			0xaa,//ACIO START
			hdxb_dev_id,//HDXB
			0x01,//TYPE?
			0x28,//OPCODE
			0x00,
			0x02,
			0x00,
			0x00,
			0x2b + hdxb_dev_id
		};
		LOG->Info("**************HDXB Python23IO INIT4:");
		return WriteToBulkWithExpectedReply(breath_of_life, false, true);
		//We are NOT reading because the capture says we don't at this point!
	}
}

bool Python23IO::openVEXTIO()
{
	uint8_t compacket22[] = {
		0xaa,
		0x02,
		0x06,
		0x22,
		0x13
	};
		uint8_t compacket2_f_3_8[] = {
		0xaa,
		0x02,
		0x06,
		0x23
	};
		WriteToBulkWithExpectedReply(compacket2_f_3_8, false, true);
		compacket2_f_3_8[3] = 0x2f;
		WriteToBulkWithExpectedReply(compacket2_f_3_8, false, true);
		compacket2_f_3_8[3] = 0x28;
		WriteToBulkWithExpectedReply(compacket2_f_3_8, false, true);
		uint8_t compacket2a[] = {
			0xaa,
			0x03,
			0x06,
			0x2a,
			0x05
		};
		WriteToBulkWithExpectedReply(compacket2a, false, true);
		compacket2_f_3_8[3] = 0x23;
		WriteToBulkWithExpectedReply(compacket2_f_3_8, false, true);
		uint8_t compacket32[] = {
			0xaa,
			0x04,
			0x06,
			0x32,
			0x00,
			0x00
		};
	uint8_t com_open_command[] = {
		0xaa, // packet start
		0x05, // length
		0x09, // sequence -- is overwritten
		0x38, // com open opcode
		extio_vcom_port, // virtual com port
		0x00, // @ baud
		extio_vbaud_rate  // choose speed: ?, ?, 19200, 38400, 57600
		};
	//LOG->Info("**************VEXTIO Python23IO OPEN: %02X %02X %02X %02X %02X %02X %02X", com_open_command[0], com_open_command[1], com_open_command[2], com_open_command[3], com_open_command[4], com_open_command[5], com_open_command[6] );
		return WriteToBulkWithExpectedReply(com_open_command, false, true);

}

bool Python23IO::openHDXB()
{
	if (COM4SET)
	{
		com4.open();
		com4.setBaudrate(38400);
		LOG->Info("**************HDXB SERIAL OPEN:");
		return true;
	}
	else
	{
		if (!m_bConnected) return false;
	
		uint8_t com_open_command[] = {
		0xaa, // packet start
		0x05, // length
		0x06, // sequence
		0x38, // com open opcode
		hdxb_vcom_port, // virtual com port
		0x00, // @ baud
		hxdb_vbaud_rate  // choose speed: ?, ?, 19200, 38400, 57600
		};
		LOG->Info("**************HDXB Python23IO OPEN:");
		return WriteToBulkWithExpectedReply(com_open_command, false, true);
	}
}

bool Python23IO::readHDXB(int len)
{
	usleep(36000); // 36 ms wait
	uint8_t com_read_command[] = {
		0xaa, // packet start
		0x04, // length
		0x0f, // sequence
		0x3b, // com read opcode
		hdxb_vcom_port, // virtual com port
		0x7e  // 7e is 126 bytes to read
	};
	com_read_command[5]=len&0xFF; // plug in passed in len
	//LOG->Info("**************HDXB Python23IO Read:");
	return WriteToBulkWithExpectedReply(com_read_command, false, true);
}

bool Python23IO::readVEXTIO(int len)
{
	usleep(36000); // 36 ms wait
	uint8_t com_read_command[] = {
		0xaa, // packet start
		0x04, // length
		0x0f, // sequence
		0x3b, // com read opcode
		extio_vcom_port, // virtual com port
		0x40  // 7e is 126 bytes to read
	};
	com_read_command[5] = len & 0xFF; // plug in passed in len
	//LOG->Info("**************VEXTIO Python23IO Read command: %02X %02X %02X %02X %02X %02X", com_read_command[0], com_read_command[1], com_read_command[2], com_read_command[3], com_read_command[4], com_read_command[5]);
	return WriteToBulkWithExpectedReply(com_read_command, false, true);
}


//pass in a raw payload only
bool Python23IO::writeHDXB(uint8_t* payload, int len, uint8_t opcode)
{
	if (!board_hasHDXB)
	{
		return false; //no board, we cant do it
	}

	if(COM4SET)
	{
	
		//frame: 03:01:12:00:0d
		//payload consists of 00:ff:ff:ff:ff:ff:ff:7f:7f:7f:7f:7f:7f
		//must also accomodate acio packet of: aa:03:01:28:00:02:00:00:2e
		acio_request[0]=hdxb_dev_id; // hdxb device id
		acio_request[1]=0x01; // type?
		acio_request[2]=opcode; // set lights?
		acio_request[3]=0x00; //
		acio_request[4]=len & 0xFF; // length
	
		//len should be 0x0d
		memcpy( &acio_request[5],  payload, len * sizeof( uint8_t ) );

		//03:01:12:00:0d:00:ff:ff:ff:ff:ff:ff:7f:7f:7f:7f:7f:7f
		len++; // add checksum to length of payload
		//len is now 0x0e

		//uint8_t unescaped_acio_length = len & 0xFF; // but we dont need this, we need number of bytes to write to acio bus

		//get escaped packet and length
		len+=5; //acio frame bytes added
		len = ACIO::prep_acio_packet_for_transmission(acio_request,len); 
		com4.write(acio_request,len);
		usleep(36000); // 36 ms wait
		ACIO::read_acio_packet(com4,acio_response);
	}
	else
	{
		if (!m_bConnected) return false;
		//frame: 03:01:12:00:0d
		//payload consists of 00:ff:ff:ff:ff:ff:ff:7f:7f:7f:7f:7f:7f
		//must also accomodate acio packet of: aa:03:01:28:00:02:00:00:2e
		python23io_request[0]=hdxb_dev_id; // hdxb device id
		python23io_request[1]=0x01; // type?
		python23io_request[2]=opcode; // set lights?
		python23io_request[3]=0x00; //
		python23io_request[4]=len & 0xFF; // length
	
		//len should be 0x0d
		memcpy( &python23io_request[5],  payload, len * sizeof( uint8_t ) );

		//03:01:12:00:0d:00:ff:ff:ff:ff:ff:ff:7f:7f:7f:7f:7f:7f
		len++; // add checksum to length of payload
		//len is now 0x0e

		//uint8_t unescaped_acio_length = len & 0xFF; // but we dont need this, we need number of bytes to write to acio bus

		//get escaped packet and length
		len+=5; //acio frame bytes added
		len = ACIO::prep_acio_packet_for_transmission(python23io_request,len); 

		//we are now ready to transmit over Python23IO channels...
		uint8_t python23io_acio_message[256];
		python23io_acio_message[0]=0xaa;
		python23io_acio_message[1]=(len+4) & 0xFF;
		python23io_acio_message[2]=0;
		python23io_acio_message[3]=0x3a; //com port write
		python23io_acio_message[4]=hdxb_vcom_port; // actual virtual com port number to use
		python23io_acio_message[5]=len&0xFF; //num bytes to write on the acio bus
		memcpy( &python23io_acio_message[6],  python23io_request, len * sizeof( uint8_t ) ); // stuff in a Python23IO wrapper
		//LOG->Info("**************HDXB Python23IO LIGHT Write:");
		WriteToBulkWithExpectedReply(python23io_acio_message, false, true);
		//LOG->Info("**************HDXB Python23IO Get Ping:");
	
	}
	return pingHDXB();
}


bool Python23IO::writeVEXTIO(uint8_t* payload, int len)
{
	if (!board_isP2IO && !board_hasVEXTIO)
	{
		return false; //no board, we cant do it
	}



	//we are now ready to transmit over Python2 IO channels...
	uint8_t python23io_extio_message[16];
	python23io_extio_message[0]=0xaa;
	python23io_extio_message[1]=(len+4) & 0xFF;
	python23io_extio_message[2]=0;
	python23io_extio_message[3]=0x3a; //com port write
	python23io_extio_message[4]=extio_vcom_port; // actual virtual com port number to use
	python23io_extio_message[5]=len&0xFF; //num bytes to write on the acio bus
	memcpy( &python23io_extio_message[6],  payload, len * sizeof( uint8_t ) ); // stuff in a Python23IO wrapper
	//LOG->Info("**************VEXTIO Python23IO LIGHT Write: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", python23io_extio_message[0], python23io_extio_message[1], python23io_extio_message[2], python23io_extio_message[3], python23io_extio_message[4], python23io_extio_message[5], python23io_extio_message[6], python23io_extio_message[7], python23io_extio_message[8], python23io_extio_message[9], python23io_extio_message[10]);
	WriteToBulkWithExpectedReply(python23io_extio_message, false, true);
	readVEXTIO(0x40);
	return true;
}

bool Python23IO::hasVEXTIO()
{
	return board_hasVEXTIO;
}

bool Python23IO::writeLightsP2IO(uint8_t* payload)
{
	
	//if not connected do nothing
	if (!m_bConnected)
	{
		LOG->Info("Python23IO io driver is in disconnected state! Not doing anything in write lights!");
		return false;
	}

	
	
	//make a light message
	uint8_t light_message[] = { 0xaa, 0x04, 0x00, 0x24, payload[0], ~payload[1] };
	//LOG->Info("Python23IO driver Sending P2IO light message: %02X %02X %02X %02X %02X %02X", light_message[0],light_message[1],light_message[2],light_message[3],light_message[4],light_message[5]);

	//send message
	return WriteToBulkWithExpectedReply(light_message, false);
}

bool Python23IO::writeLights(uint8_t* payload)
{
	//packets_since_keepalive++;

	//if not connected do nothing
	if (!m_bConnected)
	{
		LOG->Info("Python23IO io driver is in disconnected state! Not doing anything in write lights!");
		return false;
	}

	
	//make a light message
	uint8_t light_message[] = { 0xaa, 0x07, 0x00, 0x24, ~payload[4], payload[3], payload[2], payload[1], payload[0] };
	//LOG->Info("Python23IO driver Sending light message: %02X %02X %02X %02X %02X %02X %02X %02X %02X", light_message[0],light_message[1],light_message[2],light_message[3],light_message[4],light_message[5],light_message[6],light_message[7],light_message[8]);

	//send message
	return WriteToBulkWithExpectedReply(light_message, false);

}



bool Python23IO::WriteToBulkWithExpectedReply(uint8_t* message, bool init_packet, bool output_to_log)
{
	if (!m_bConnected) return false;

	//LOG->Info("Python23IO driver bulk write: Applying sequence");
	//first figure out what our message parameters should be based on what I see here and our current sequence
	packet_sequence++;
	if (board_isP2IO)
	{
		packet_sequence %= 15; //p2io loops after 0x0E
	}
	else
	{
		packet_sequence %= 16;
	}

	if (!init_packet)
	{
		message[2] = packet_sequence;
	}
	else
	{
		packet_sequence = message[2];
	}

	//max packet size is 64 bytes * 2 for escaped packets
	uint8_t response[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
						   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	int message_length = 5;
	int expected_response_size = 3; // handles packet start, length, sequence.. everything should be long than this though. Updates in the function

	message_length = message[1]+2;
	
	//LOG->Info("Expected response size is %d", expected_response_size);
	//actually send our message with the parameters we determined
	//but first we need to escape our shit


	//LOG->Info("Escaping our shit");
	int bytes_to_write=message_length;
	uint8_t message2[258];
	message2[0]=message[0];
	int old_stream_position=1;
	for (int new_stream_position=1;new_stream_position<bytes_to_write;new_stream_position++)
	{
		if (message[old_stream_position]==0xAA || message[old_stream_position]==0xFF)
		{
			message2[new_stream_position]=0xFF;
			new_stream_position++;
			bytes_to_write++;
			message2[new_stream_position]=~message[old_stream_position];
		}
		else
		{
			message2[new_stream_position]=message[old_stream_position];
		}
		old_stream_position++;
	}
	//END DATA EScAPING CODE


	//prepare debug line
	for (int i=0;i<bytes_to_write;i++)
	{
		sprintf (debug_message+(i*3), "%02X ", message2[i]);
	}
	//if(output_to_log) LOG->Info("Send %d bytes - %s", bytes_to_write, debug_message);

	int iResult = m_pDriver->BulkWrite(bulk_write_to_ep, (char*)message2, bytes_to_write, REQ_TIMEOUT);

	if (iResult != bytes_to_write)
	{
		LOG->Info("Python23IO message to send was truncated. Sent %d/%d bytes", iResult,bytes_to_write);
	}

	//get ready for the response
	//LOG->Info("Asking for reply...");
	iResult = GetResponseFromBulk(response, expected_response_size, output_to_log); //do a potentially fragmented read
	bulk_reply_size=iResult;


	//prepare debug line
	debug_message[0]=0;
	for (int i=0;i<bulk_reply_size;i++)
	{
		sprintf (debug_message+(i*3), "%02X ", response[i]);
	}
	//if(output_to_log) LOG->Info("Full response is %d/%d bytes - %s", bulk_reply_size,response[1]+2, debug_message);
	

	//if the unescaped data is too small according to the packet length byte...
	if (bulk_reply_size<(response[1]+2))
	{
		if(output_to_log) LOG->Info("Something is fishy, asking again...");
		bool overrider=false; //probably make this override more rubust but it is a giant hack anyway... hopefully not needed anymore
		if (iResult>1 && response[1]!=0xaa) overrider=true; // if we have something that looks like the start of the packet start vaguely
		//tell it to override looking for packet start, trimming off the leading 0xAAs and length.
		iResult += GetResponseFromBulk(response+iResult, response[1]-bulk_reply_size, output_to_log,overrider); //do a potentially fragmented read
		bulk_reply_size=iResult;

		//prepare debug line
		debug_message[0]=0;
		for (int i=0;i<bulk_reply_size;i++)
		{
			sprintf (debug_message+(i*3), "%02X ", response[i]);
		}
		if(output_to_log) LOG->Info("2nd attempt full response is %d/%d bytes - %s", bulk_reply_size,response[1]+2, debug_message);
	}
	for (int i=0;i<bulk_reply_size;i++)
	{
		python23io_response[i]=response[i];
	}
	return true;
}

//sometimes responses are split across 2 reads... could there be a THIRD read?
//this is a giant hack because data can be escaped which really fucks with length
int Python23IO::GetResponseFromBulk(uint8_t* response, int expected_response_length, bool output_to_log, bool force_override)
{
	int totalBytesRead=0;
	int num_reads=0;
	int num_failed_reads=0;
	int escaped_bytes=0;
	bool flag_has_start_of_packet=false;
	bool flag_has_response_length=false;
	if (force_override)
	{
			flag_has_start_of_packet=true;
			flag_has_response_length=true;
	}
	//int responseLength = m_pDriver->BulkRead(bulk_read_from_ep, (char*)response, 128, REQ_TIMEOUT);
	//LOG->Info("begin while loop");
	while (totalBytesRead < (expected_response_length+2)) //sometimes a response is fragmented over multiple requests, try a another time
	{
		//if(output_to_log) LOG->Info("GETTING BULK OF EXPECTED SIZE: %02X/%02x",totalBytesRead,expected_response_length);
		int i=0;
		num_reads++;

		uint8_t response2[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		int bytesReadThisRound = m_pDriver->BulkRead(bulk_read_from_ep, (char*)response2, 64, REQ_TIMEOUT);
		int escapedBytesThisRound=0;
		if (bytesReadThisRound <1) num_failed_reads++; //note that we got nothing from the read

		//DEBUG
		debug_message[0]=0;
		for (i=0;i<bytesReadThisRound;i++)
		{
			sprintf (debug_message+(i*3), "%02X ", response2[i]);
		}
		//if(output_to_log) LOG->Info("Bulk read round %d is %d bytes - %s", num_reads, bytesReadThisRound, debug_message);
		//END DEBUG


		if (bytesReadThisRound<1 && num_failed_reads>1)
		{
			LOG->Info("Python23IO must have given up sending data we need (%d/%d bytes)",totalBytesRead,expected_response_length);
			break;
		}

		//unescape the bytes
		i=0;


		//Try and find start of packet in the data we read
		if (!flag_has_start_of_packet)
		{
			//LOG->Info("Python23IO not escaping sop");
			while(response2[0]!=0xAA && bytesReadThisRound>0)
			{
				//LOG->Info("Response buffer does NOT start with 0xAA, it starts with %02X. Shifting buffer left by one.",response2[0]);
				//shift everything over until we do
				for (i=0;i<bytesReadThisRound-1;i++)
				{
					response2[i]=response2[i+1];
					response2[i+1]=0;
				}
				bytesReadThisRound--;
			}
			if (response2[0]==0xaa) flag_has_start_of_packet=true;

		}
		
		//if we dont have start of packet, lets try to read again
		if (!flag_has_start_of_packet)
		{
			usleep(3000); //sleep for 3ms
			continue;
		}

		//at this point we are guarunteed to have start of packet which means the first byte is 0xAA
		//So now the next non-0xAA byte is the length of the packet

		//EDGE CASE! We read a 0xAA in a previous round (reflected in total bytes read)
		i=0; //assume start of packet is in main read buffer
		if (totalBytesRead<1) i=1; // if we have nothing in the main buffer and the flag_has_start_of_packet is set, it means the start of packet is in the current read buffer

		for (i;i<bytesReadThisRound;i++) //lets look at EVERYTHING we read
		{
			//response length is not set...
			if (!flag_has_response_length)
			{
				//if we see ANY character that is not AA... we have a response length at some point
				if (response2[i]!=0xAA)
				{
					//if(output_to_log) LOG->Info("Expected response length found! Setting new length to %02X",response2[i]);
					flag_has_response_length=true;
					expected_response_length=response2[i];
					i--;
					continue;
				}

				//if we haven't seen any other data and AA is present in slot 2
				if (response2[i]==0xAA)
				{
					bytesReadThisRound--;
					//shift all bytes in the array after this char to the left
					for (int j=i;j<bytesReadThisRound;j++)
					{
						response2[j]=response2[j+1];
					}
					i--;
					continue;
				}
			}
			else //we have the first 2 bytes of a packet such that we know the response length
			{
				//if we hit an escaped character in the payload
				if (response2[i]==0xAA || response2[i]==0xFF)
				{
					//LOG->Info("Python23IO  escape char at %d",i);
					//decrease the actual number of bytes we read since it needs to be escaped and it doesn't count to expected reply length
					bytesReadThisRound--;
					escapedBytesThisRound++;

					//do the actual unescaping later AFTER we have the full payload
				}
			}

			
		}

		//todo: bounds checking
		//LOG->Info("Python23IO MEMCOP %d",num_reads);
		for(int j=0;j<bytesReadThisRound+escapedBytesThisRound;j++)
		{
			response[totalBytesRead+j]=response2[j];
		}
		//memcpy(response+totalBytesRead, response2, bytesReadThisRound);

		if ((bytesReadThisRound+escapedBytesThisRound)>0)
		{

			//LOG->Info("Python23IO adding %d bytes to %d",bytesReadThisRound,totalBytesRead);
			totalBytesRead+=bytesReadThisRound;
			escaped_bytes+=escapedBytesThisRound;

		}


	
	}

	//reading complete, time to unescape
	//shift all bytes in the array after this char to the left, overwriting the escaped character
	int bytesCountToEscape=totalBytesRead+escaped_bytes;
	for (int i=2;i<bytesCountToEscape;i++)
	{
		if (response[i]==0xAA || response[i]==0xFF)
		{
			bytesCountToEscape--;
			for (int j=i;j<bytesCountToEscape;j++)
			{
				response[j]=response[j+1];
			}

			//zero out the last character
			response[bytesCountToEscape]=0;

			//unescape the escaped character
			response[i]=~response[i];
		}
	}
	//LOG->Info("End Python23IO get bulk  message");
	return totalBytesRead;
}


void Python23IO::Reconnect()
{
	LOG->Info("Attempting to reconnect Python23IO.");
	//the actual input handler will ask for a reconnect, i'd rather it happen there for more control
	/* set a message that the game loop can catch and display */
	char temp[256];
	sprintf(temp, "I/O error: %s", m_pDriver->GetError());
	//m_sInputError = ssprintf( "I/O error: %s", m_pDriver->GetError() );
	m_sInputError = m_sInputError.assign(temp);
	Close();
	m_bConnected = false;
	baud_pass=false;
	hdxb_ready=false;
	Open();
	m_sInputError = "";
}


uint8_t Python23IO::checkInput(uint8_t x, uint8_t y)
{
	//there is a better way to do this but I'll optimize later, I wanted to make sure the logic was RIGHT
	uint8_t v = x;
	uint8_t w = y;

	//enable these 2 for Python23IO
	v = ~v;
	w = ~w;

	//printf("comparing %02X : %02X\n",v,w);
	return v & w;
}


static uint8_t python23io_init_hd_and_watchdog[21][45] = {
	{ 0xaa, 0x02, 0x00, 0x01 }, //0x01 is get version -- replies with 47 33 32 00 02 02 06 (G32)
	{ 0xaa, 0x02, 0x00, 0x2f },//?
	{ 0xaa, 0x03, 0x01, 0x27, 0x01 }, //0x27 is get cabinet type
	{ 0xaa, 0x02, 0x02, 0x31 },//coins?
	{ 0xaa, 0x02, 0x03, 0x01 }, //0x01 is get version -- replies with 47 33 32 00 02 02 06 (G32)
	{ 0xaa, 0x03, 0x04, 0x27, 0x00 }, //0x27 is get cabinet type

	//read sec plug
	{ 0xaa, 0x2b, 0x05, 0x25, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x06, 0x25, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x07, 0x25, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x08, 0x25, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x09, 0x25, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x0a, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x0b, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x0c, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x0d, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xaa, 0x2b, 0x0e, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	//end read sec plug
	
	{ 0xaa, 0x03, 0x0f, 0x05, 0x00 },//these next 4 clearly set some parameters. no idea what they are
	{ 0xaa, 0x03, 0x00, 0x2b, 0x01 },
	{ 0xaa, 0x03, 0x01, 0x29, 0x05 },
	{ 0xaa, 0x03, 0x02, 0x05, 0x30 }, //is this a 48 second watch dog?
	{ 0xaa, 0x03, 0x03, 0x27, 0x00 }, //0x27 is get cabinet type
};
const unsigned NUM_python23io_INIT_MESSAGES = ARRAYLEN(python23io_init_hd_and_watchdog);

//the watchdog application needs to be running on the pc I think.
//On a nerfed pcb this likely wont do anything but on an  real cabinet,
//if you don't send a keep alive... KABOOM!
//Also init HD controls. always do so. Harmless to SD cabinets.
void Python23IO::InitHDAndWatchDog()
{
	for (int i = 0; i<NUM_python23io_INIT_MESSAGES; i++)
	{
		WriteToBulkWithExpectedReply(python23io_init_hd_and_watchdog[i], true);
		usleep(16666);
	}
	

	// now lets flush any open reads:
	FlushBulkReadBuffer();
}

void Python23IO::FlushBulkReadBuffer()
{
	uint8_t response3[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
						   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	LOG->Info("Python23IO Flushing Read buffer. Expect it to give up");
	bulk_reply_size=GetResponseFromBulk(response3, 3, true); //do a potentially fragmented read
}