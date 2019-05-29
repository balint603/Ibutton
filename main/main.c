/** \mainpage iButton Access Control project
 *
 *  \section intro_sec About this project
 *  This software is developed to run on ESP32 (Espressif) device. \n
 *  The task of this project is to create an online access control device.
 *  The authentication is realized with iButton (Maxim Integrated) keys and can be restrict in time.
 *  It means that the device only allows access when the given person's key can be found in the local database
 *  and the time restriction setting - belongs to each person - matches the current time.
 *  \n\n
 *  Time descriptor are based on (UNIX) crontab and used to store time settings related to each keys.
 *  This device works with a local database stored in flash memory. It consists of key-cron entries.
 *  \section sw_modules Software modules
 *  This software consists of the following main modules:
 *   - ib_database module: \n
 *
 *   - cron module:
 *   - ib_http_client module:
 *   - ib_log module:
 *   - ibutton module:
 * */

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/uart.h"

#include "console.h"
#include "ib_sntp.h"
#include "ib_reader.h"
#include "ib_database.h"
#include "esp_spiffs.h"
#include "ib_http_client.h"
#include "ib_log.h"

void spiffs_init() {
	ESP_LOGI("SPIFF","Initializing...");
	esp_err_t ret;
	esp_vfs_spiffs_conf_t conf = {
			.base_path = "/spiffs",
			.partition_label = NULL,
			.max_files = 5,
			.format_if_mount_failed = 1	// todo FORMAT FLASH?
	};

	ret = esp_vfs_spiffs_register(&conf);
	if (ret != ESP_OK) {
		if (ret == ESP_FAIL){
			ESP_LOGE(__func__, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(__func__, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(__func__, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		return;
	}
	ESP_LOGI("SPIFF","Initialized");
}

void initials() {
	esp_err_t ret = ibd_init();
	if ( IBD_OK != ret ) {
		ESP_LOGE(__func__,"Cannot init ib_database:%x",ret);
	}
}

static void nvs_init() {
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK( nvs_flash_erase() );
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
}

void app_main(){
	gpio_pad_select_gpio(4);
	gpio_set_direction(4, GPIO_MODE_OUTPUT);
	nvs_init();
	spiffs_init();
	start_console();
	wifi_get_data();
	initials();
	start_ib_reader();
	ib_client_init();
	ib_log_init();

	while(1){
		gpio_set_level(4, 1);
		vTaskDelay(20 / portTICK_RATE_MS);
		gpio_set_level(4, 0);
		vTaskDelay(4980 / portTICK_RATE_MS);
	}
}
