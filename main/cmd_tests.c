/*
 * cmd_tests.c
 *
 *  Created on: Mar 14, 2019
 *      Author: root
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "rom/uart.h"
#include "cmd_system.h"
#include "sdkconfig.h"
#include "cmd_tests.h"

#include "ib_reader.h"
#include "codeflash.h"

static struct {
    struct arg_int *min;
    struct arg_int *hour;
    struct arg_int *day;
    struct arg_int *month;
    struct arg_int *wday;
    struct arg_end *end;
} time_args;

#ifdef TEST_COMMANDS
void register_tests(){
	const esp_console_cmd_t cmd = {
			.command = "test_codeflash_init",
			.help = "codeflash_test",
			.func = &test_codeflash_init,
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

	const esp_console_cmd_t cmd2 = {
			.command = "test_codeflash_erase_both",
			.help = "codeflash_erase_both",
			.func = &codeflash_erase_both,
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&cmd2));

	const esp_console_cmd_t cmd3 = {
				.command = "test_codeflash_write-read",
				.help = "codeflash_write-read",
				.func = &test_codeflash_write,
		};
		ESP_ERROR_CHECK(esp_console_cmd_register(&cmd3));

	const esp_console_cmd_t cmd4 = {
				.command = "test_codeflash_nvs_data_reset",
				.help = "codeflash data reset",
				.func = &test_codeflash_nvs_reset,
		};
		ESP_ERROR_CHECK(esp_console_cmd_register(&cmd4));
}
#endif
