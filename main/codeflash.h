/*
 * codeflash.h
 *
 *  Created on: Mar 10, 2019
 *      Author: root
 */

#ifndef MAIN_CODEFLASH_H_
#define MAIN_CODEFLASH_H_

#include <stdio.h>
#include "ibutton.h"

typedef unsigned int size_t;

typedef struct __attribute__((__packed__)) code_data{
	size_t cron_length;
	ib_code_t code;
	char *cron;
} codeflash_t;

esp_err_t codeflash_init();
esp_err_t codeflash_append_raw_data(void *data, size_t length, int into_inactive);
esp_err_t codeflash_get_by_code(ib_code_t code ,codeflash_t *dst);

#define TEST_COMMANDS

#ifdef TEST_COMMANDS
void test_codeflash_init();
void codeflash_erase_both();
void test_codeflash_write();
void test_codeflash_nvs_reset();
#endif

#endif /* MAIN_CODEFLASH_H_ */
