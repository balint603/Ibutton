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

typedef struct code_data{
	size_t next_offset;
	ib_code_t code;
	char *cron_start;
} code_flash_t;

esp_err_t codeflash_init();
esp_err_t codeflash_append_data(char *data, size_t length);
esp_err_t codeflash_get_by_code(code_flash_t *dst);

#define TEST_COMMANDS

#ifdef TEST_COMMANDS
void test_codeflash_init();
#endif

#endif /* MAIN_CODEFLASH_H_ */
