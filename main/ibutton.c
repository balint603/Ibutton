/*
 * ibutton.c
 *
 *  Created on: Feb 23, 2019
 *      Author: root
 */

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "ibutton.h"

gpio_num_t DATA_GPIO_NUM;

#define GET_LEVEL (gpio_get_level(DATA_GPIO_NUM))
#define PULL (gpio_set_direction(DATA_GPIO_NUM, GPIO_MODE_OUTPUT))
#define RELEASE (gpio_set_direction(DATA_GPIO_NUM, GPIO_MODE_INPUT))
#define FAMILY_CODE 0x01


static const uint8_t COMMAND_BYTE = 0x33;


static const uint8_t CRC8_TABLE[256] =
{
	  0, 94,188,226, 97, 63,221,131,194,156,126, 32,163,253, 31, 65,
	157,195, 33,127,252,162, 64, 30, 95,  1,227,189, 62, 96,130,220,
	 35,125,159,193, 66, 28,254,160,225,191, 93,  3,128,222, 60, 98,
	190,224,  2, 92,223,129, 99, 61,124, 34,192,158, 29, 67,161,255,
	 70, 24,250,164, 39,121,155,197,132,218, 56,102,229,187, 89,  7,
	219,133,103, 57,186,228,  6, 88, 25, 71,165,251,120, 38,196,154,
	101, 59,217,135,  4, 90,184,230,167,249, 27, 69,198,152,122, 36,
	248,166, 68, 26,153,199, 37,123, 58,100,134,216, 91,  5,231,185,
	140,210, 48,110,237,179, 81, 15, 78, 16,242,172, 47,113,147,205,
	 17, 79,173,243,112, 46,204,146,211,141,111, 49,178,236, 14, 80,
	175,241, 19, 77,206,144,114, 44,109, 51,209,143, 12, 82,176,238,
	 50,108,142,208, 83, 13,239,177,240,174, 76, 18,145,207, 45,115,
	202,148,118, 40,171,245, 23, 73,  8, 86,180,234,105, 55,213,139,
	 87,  9,235,181, 54,104,138,212,149,203, 41,119,244,170, 72, 22,
	233,183, 85, 11,136,214, 52,106, 43,117,151,201, 74, 20,246,168,
	116, 42,200,150, 21, 75,169,247,182,232, 10, 84,215,137,107, 53
};

static uint8_t crc8_check(uint8_t *data, uint8_t length){
	uint8_t crc = 0;
	for(int i = 0; i < length; i++)
		crc = CRC8_TABLE[crc ^ data[i]];
	return crc;
}
/** \brief Send a byte out.
 *
 * */
static void send_byte(uint8_t data){
	for(int i = 8; i > 0; i--){
		if(data & 0x01){
			PULL;
			ets_delay_us(5);
			RELEASE;
			ets_delay_us(80);
		}
		else{
			PULL;
			ets_delay_us(80);
			RELEASE;
			ets_delay_us(5);
		}
		data >>= 1;
	}
}

/** \brief Reads a byte.
 *  Must called just after the command byte has been sent out.
 *  \ret long ibutton code.
 * */
static uint8_t read_byte(){
	uint8_t byte = 0;
	for(int i = 8; i > 0; i--){
		PULL;
		ets_delay_us(5);
		RELEASE;
		ets_delay_us(10);
		byte >>= 1;
		if(GET_LEVEL)
			byte |= 0b10000000;
		ets_delay_us(30);
	}
	return byte;
}


/**
 *  Test if an iButton device is connected to the reader or not. It filters the short circuit.
 *
 *  \ret 0 when the input level was low before reset pulse send,
 *  or no answer after reset pulse. (See one-wire protocol).
 *  \ret 1 when the input had been pulled down only after the reset pulse,
 *  in this case this function assumes that an iButton device had given a presence pulse.
 * */
int ib_presence(){
	int line_level_tmp = GET_LEVEL;
	ets_delay_us(100);
	if(!(line_level_tmp && GET_LEVEL))			// Input is still pulled down to GDN: reader is shorted.
		return 0;
	PULL;
	ets_delay_us(480);
	RELEASE;
	ets_delay_us(70);
	line_level_tmp = GET_LEVEL;
	ets_delay_us(410);
	return line_level_tmp ? 0 : 1;
}

/** \brief uint8_t array to long conversion. * */
static long bytes_to_code(uint8_t *data){
	long code_val = 0;
	if(!data)
		return code_val;
	for(int i = 6; i > 0; i--){
		code_val <<= 8;
		code_val |= (long)*(data+i);
	}
	return code_val;
}

/**
 * Read the iButton ROM.
 * This function must be called, when an iButton has just been connected to the reader.
 * \param ib_code serial number of iButton device.
 * \ret IB_OK when the computed CRC equals the MSB from the ROM data and LSB equals 01h (iButton family code).
 * \ret IB_CRC_ERR when CRC error.
 * \ret IB_FAM_ERR when the family code does not match.
 * */
ib_ret_t ib_read_code(long *ib_code){
	uint8_t crc = 0;
	uint8_t data[8];

	portDISABLE_INTERRUPTS();
	send_byte(COMMAND_BYTE);
	ets_delay_us(480);
	for(uint8_t curr_data = 0; curr_data < 8; curr_data++){
		data[curr_data] = read_byte();
	}
	portENABLE_INTERRUPTS();

	if(data[0] != FAMILY_CODE)
		return IB_FAM_ERR;
	if(crc8_check(data,8))
		return IB_CRC_ERR;

	*ib_code = bytes_to_code(data);
	return IB_OK;
}

void onewire_init(gpio_num_t data_pin){
	gpio_pad_select_gpio(data_pin);
	gpio_set_direction(data_pin, GPIO_MODE_INPUT);
	gpio_set_level(data_pin, 0);
	DATA_GPIO_NUM = data_pin;
}
