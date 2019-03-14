/*
 * codeflash.h
 *
 *  Created on: Mar 10, 2019
 *      Author: root
 *
 * Flash module to store iButton codes and associated cron strings.
 *
 * Addr: x  x+1              x+9                     x+9+(value of cron_length)
 *
 * Prev.|__ ________________ _____________       ___| Next data
 * 		|  |                |                       |
 * 		|  |      code [8B] |cron[0-254]B  o o o    |
 * 		|__|________________|_____________       ___|
 *       ^                           ^
 	 	 |							 |
 *    cron_length [1B]				 |
 * 	  -value range: 0 - 254			 |
 * 	  								 |
 * 	  code                           |
 * 	  -iButton serial number         |
 * 	  								 |
 * 	  cron string format_____________|
 * 	  - ';' seperated list
 * 	  - example string: "* 7-17 * * 1-5;* 12-20 * * 0,6"
 * 	  - 'or' relation between crons
 * 	  - includes null terminator
 */

#ifndef MAIN_CODEFLASH_H_
#define MAIN_CODEFLASH_H_

#include <stdio.h>
#include "ibutton.h"

typedef unsigned int size_t;

#define MAX_N_OF_CRONS 25

/** \brief It is a data formula stored in flash. */
typedef struct __attribute__((__packed__)) code_data{
	const uint8_t crons_length;
	const unsigned long code;
	const char *crons;
} codeflash_t;

esp_err_t codeflash_init();
esp_err_t codeflash_check_data(void *data, size_t length);
esp_err_t codeflash_append_raw_data(void *data, size_t length, int into_inactive);
esp_err_t codeflash_get_by_code(unsigned long code, codeflash_t *data_f);

#define TEST_COMMANDS

#ifdef TEST_COMMANDS
void test_codeflash_init();
void codeflash_erase_both();
void test_codeflash_write();
void test_codeflash_nvs_reset();
#endif

#endif /* MAIN_CODEFLASH_H_ */
