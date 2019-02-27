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
#include "ib_reader.h"

void app_main(){
	gpio_pad_select_gpio(4);
	gpio_set_direction(4, GPIO_MODE_OUTPUT);
	nvs_flash_init();
	start_console();
	wifi_get_data();
	start_ib_reader();
	while(1){
		gpio_set_level(4, 1);
		vTaskDelay(20 / portTICK_RATE_MS);
		gpio_set_level(4, 0);
		vTaskDelay(4980 / portTICK_RATE_MS);
	}
}
