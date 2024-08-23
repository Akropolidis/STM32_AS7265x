#include "AS7265X.h"


uint16_t maxWaitTime = 0; //Based on integration cycles

static uint8_t readRegister(uint8_t addr);
static void writeRegister(uint8_t addr, uint8_t val);

static uint8_t virtualReadRegister(uint8_t virtualAddr);
static void virtualWriteRegister(uint8_t virtualAddr, uint8_t dataToWrite);

static void selectDevice(uint8_t device); //Change between the x51, x52, or x53 for data and settings

static float convertBytesToFloat(uint32_t myLong);
static uint16_t getChannel(uint8_t channelRegister, uint8_t device);
static float getCalibratedValue(uint8_t calAddress, uint8_t device);


//Initializes the sensor with basic settings
//Returns false if sensor is not detected
void begin()
{
	uart2_rxtx_init();
	I2C1_init();
}

//Reads from a given location from the AS726x
static uint8_t readRegister(uint8_t addr)
{
	uint8_t data = 0;
	I2C1_byteRead(AS7265X_ADDR, addr, &data);
	return data;
}

//Write a value to a given location on the AS726x
static void writeRegister(uint8_t addr, uint8_t val)
{
	uint8_t bufferSize = 1;
	char data[bufferSize];

	data[0] = val;
	I2C1_burstWrite(AS7265X_ADDR, addr, bufferSize, data);
}

//Read from a virtual register on the AS7265x
static uint8_t virtualReadRegister(uint8_t virtualAddr)
{
	volatile uint8_t status, data;

	//Do a preliminary check of the read register
	status = readRegister(AS7265X_STATUS_REG);
	if((status & AS7265X_RX_VALID) != 0) //Data byte available in READ register
	{
		readRegister(AS7265X_READ_REG); //Read the byte but do nothing
	}

	//Wait for WRITE flag to clear
	while(1)
	{
		//Read slave I2C status to see if the read register is ready
		status = readRegister(AS7265X_STATUS_REG);
		if((status & AS7265X_TX_VALID) == 0) //New data may be written to WRITE register
		{
			break;
		}
		systickDelayMs(AS7265X_POLLING_DELAY); //Delay for 5 ms before checking for virtual register changes
	}

	//Send the virtual register address (disabling bit 7 to indicate a read).
	writeRegister(AS7265X_WRITE_REG, virtualAddr);

	while(1)
	{
		//Read slave I2C status to see if the read register is ready
		status = readRegister(AS7265X_STATUS_REG);
		if((status & AS7265X_RX_VALID) != 0) //New data may be written to WRITE register
		{
			break;
		}
		systickDelayMs(AS7265X_POLLING_DELAY); //Delay for 5 ms before checking for virtual register changes
	}

	data = readRegister(AS7265X_READ_REG);
	return data;
}

//Write to a virtual register in the AS7265x
static void virtualWriteRegister(uint8_t virtualAddr, uint8_t dataToWrite)
{
	volatile uint8_t status;

	//Wait for WRITE register to be empty
	while(1)
	{
		//Read slave I2C status to see if the write register is ready
		status = readRegister(AS7265X_STATUS_REG);
		if((status & AS7265X_TX_VALID) == 0) //New data may be written to WRITE register
		{
			break;
		}
		systickDelayMs(AS7265X_POLLING_DELAY); //Delay for 5 ms before checking for virtual register changes
	}

	//Send the virtual register address (enabling bit 7 to indicate a write).
	writeRegister(AS7265X_WRITE_REG, (virtualAddr | 1 << 7));

	//Wait for WRITE register to be empty
	while(1)
	{
		//Read slave I2C status to see if the write register is ready
		status = readRegister(AS7265X_STATUS_REG);
		if((status & AS7265X_TX_VALID) == 0) //New data may be written to WRITE register
		{
			break;
		}
		systickDelayMs(AS7265X_POLLING_DELAY); //Delay for 5 ms before checking for virtual register changes
	}

	//Send the data to complete the operation
	writeRegister(AS7265X_WRITE_REG, dataToWrite);
}

//As we read various registers we have to point at the master or first/second slave
static void selectDevice(uint8_t device)
{
	virtualWriteRegister(AS7265X_DEV_SELECT_CONTROL, device);
}

//Given 4 bytes (size of float and uint32_t) and returns the floating point value
float convertBytesToFloat(uint32_t myLong)
{
  float myFloat;
  memcpy(&myFloat, &myLong, 4); //Copy bytes into a float
  return myFloat;
}

//Get the 16-bit raw values stored in the high and low registers of each channel
static uint16_t getChannel(uint8_t channelRegister, uint8_t device)
{
	selectDevice(device);

	uint16_t colorData = virtualReadRegister(channelRegister) << 8; //XXXXXXXX-00000000 High uint8_t
	colorData |= virtualReadRegister(channelRegister + 1); //Low uint8_t
	return colorData;
}

//Given an address, read four consecutive bytes and return the floating point calibrated value
static float getCalibratedValue(uint8_t calAddress, uint8_t device)
{
	selectDevice(device);

	uint8_t chan0, chan1, chan2, chan3;
	chan0 = virtualReadRegister(calAddress + 0);
	chan1 = virtualReadRegister(calAddress + 1);
	chan2 = virtualReadRegister(calAddress + 2);
	chan3 = virtualReadRegister(calAddress + 3);

	//Channel calibrated values are stored big-endian
	uint32_t calBytes = 0;
	calBytes |= ((uint32_t)chan0 << (8 * 3)); //bits 24-31
	calBytes |= ((uint32_t)chan1 << (8 * 2)); //bits 16-23
	calBytes |= ((uint32_t)chan2 << (8 * 1)); //bits 8-15
	calBytes |= ((uint32_t)chan3 << (8 * 0)); //bits 0-7

	return (convertBytesToFloat(calBytes));

/*1. 00000000 00000000 00000000 WWWWWWWW
a. WWWWWWWW 00000000 00000000 00000000
b. calBytes = WWWWWWWW 00000000 00000000 00000000

2. 00000000 00000000 00000000 XXXXXXXX
a. 00000000 XXXXXXXX 00000000 00000000
b. calBytes = WWWWWWWW XXXXXXXX 00000000 00000000

3. 00000000 00000000 00000000 YYYYYYYY
a. 00000000 00000000 YYYYYYYY 00000000
b. calBytes = WWWWWWWW XXXXXXXX YYYYYYYY 00000000

4. 00000000 00000000 00000000 ZZZZZZZZ
a. 00000000 00000000 00000000 ZZZZZZZZ
b. calBytes = WWWWWWWW XXXXXXXX YYYYYYYY ZZZZZZZZ
*/
}








/* Obtaining various raw light readings */
//UV Readings
uint16_t getRawA()
{
	return (getChannel(AS7265X_RAW_R_G_A, AS72653_UV));
}
uint16_t getRawB()
{
	return (getChannel(AS7265X_RAW_S_H_B, AS72653_UV));
}
uint16_t getRawC()
{
	return (getChannel(AS7265X_RAW_T_I_C, AS72653_UV));
}
uint16_t getRawD()
{
	return (getChannel(AS7265X_RAW_U_J_D, AS72653_UV));
}
uint16_t getRawE()
{
	return (getChannel(AS7265X_RAW_V_K_E, AS72653_UV));
}
uint16_t getRawF()
{
	return (getChannel(AS7265X_RAW_W_L_F, AS72653_UV));
}

//VISIBLE Readings
uint16_t getRawG()
{
	return (getChannel(AS7265X_RAW_R_G_A, AS72652_VISIBLE));
}
uint16_t getRawH()
{
	return (getChannel(AS7265X_RAW_S_H_B, AS72652_VISIBLE));
}
uint16_t getRawI()
{
	return (getChannel(AS7265X_RAW_T_I_C, AS72652_VISIBLE));
}
uint16_t getRawJ()
{
	return (getChannel(AS7265X_RAW_U_J_D, AS72652_VISIBLE));
}
uint16_t getRawK()
{
	return (getChannel(AS7265X_RAW_V_K_E, AS72652_VISIBLE));
}
uint16_t getRawL()
{
	return (getChannel(AS7265X_RAW_W_L_F, AS72652_VISIBLE));
}

//NIR Readings
uint16_t getRawR()
{
	return (getChannel(AS7265X_RAW_R_G_A, AS72651_NIR));
}
uint16_t getRawS()
{
	return (getChannel(AS7265X_RAW_S_H_B, AS72651_NIR));
}
uint16_t getRawT()
{
	return (getChannel(AS7265X_RAW_T_I_C, AS72651_NIR));
}
uint16_t getRawU()
{
	return (getChannel(AS7265X_RAW_U_J_D, AS72651_NIR));
}
uint16_t getRawV()
{
	return (getChannel(AS7265X_RAW_V_K_E, AS72651_NIR));
}
uint16_t getRawW()
{
	return (getChannel(AS7265X_RAW_W_L_F, AS72651_NIR));
}

/* Obtaining the various calibrated light readings */
//UV Readings
float getCalibratedA()
{
	return (getCalibratedValue(AS7265X_CAL_R_G_A, AS72653_UV));
}
float getCalibratedB()
{
	return (getCalibratedValue(AS7265X_CAL_S_H_B, AS72653_UV));
}
float getCalibratedC()
{
	return (getCalibratedValue(AS7265X_CAL_T_I_C, AS72653_UV));
}
float getCalibratedD()
{
	return (getCalibratedValue(AS7265X_CAL_U_J_D, AS72653_UV));
}
float getCalibratedE()
{
	return (getCalibratedValue(AS7265X_CAL_V_K_E, AS72653_UV));
}
float getCalibratedF()
{
	return (getCalibratedValue(AS7265X_CAL_W_L_F, AS72653_UV));
}

//VISIBLE Readings
float getCalibratedG()
{
	return (getCalibratedValue(AS7265X_CAL_R_G_A, AS72652_VISIBLE));
}
float getCalibratedH()
{
	return (getCalibratedValue(AS7265X_CAL_S_H_B, AS72652_VISIBLE));
}
float getCalibratedI()
{
	return (getCalibratedValue(AS7265X_CAL_T_I_C, AS72652_VISIBLE));
}
float getCalibratedJ()
{
	return (getCalibratedValue(AS7265X_CAL_U_J_D, AS72652_VISIBLE));
}
float getCalibratedK()
{
	return (getCalibratedValue(AS7265X_CAL_V_K_E, AS72652_VISIBLE));
}
float getCalibratedL()
{
	return (getCalibratedValue(AS7265X_CAL_W_L_F, AS72652_VISIBLE));
}

//NIR Readings
float getCalibratedR()
{
	return (getCalibratedValue(AS7265X_CAL_R_G_A, AS72651_NIR));
}
float getCalibratedS()
{
	return (getCalibratedValue(AS7265X_CAL_S_H_B, AS72651_NIR));
}
float getCalibratedT()
{
	return (getCalibratedValue(AS7265X_CAL_T_I_C, AS72651_NIR));
}
float getCalibratedU()
{
	return (getCalibratedValue(AS7265X_CAL_U_J_D, AS72651_NIR));
}
float getCalibratedV()
{
	return (getCalibratedValue(AS7265X_CAL_V_K_E, AS72651_NIR));
}
float getCalibratedW()
{
	return (getCalibratedValue(AS7265X_CAL_W_L_F, AS72651_NIR));
}
