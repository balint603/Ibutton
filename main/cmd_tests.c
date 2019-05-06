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
#include "esp_spiffs.h"

#include "ib_reader.h"


void erase_fs() {
	ESP_ERROR_CHECK(esp_spiffs_format(NULL));
}

void register_tests(){
	const esp_console_cmd_t cmd = {
			.command = "erasefs",
			.help = "codeflash_test",
			.func = &erase_fs,
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
