/*
 * ib_reader.c
 *
 *  Created on: Feb 26, 2019
 *      Author: root
 */

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "ibutton.h"
#include "ib_reader.h"

TaskFunction_t ibutton_read_task(){
	ib_code_t ibutton_code;
	while(1){
		if(ib_presence()){
			switch (ib_read_code(&ibutton_code)) {
				case 0:
					printf("Code: %lu\n",ibutton_code);
					break;
				case 1:
					printf("Family code err\n");
					break;
				case 2:
					printf("CRC err\n");
					break;
				default:
					break;
			}
			vTaskDelay(100 / portTICK_RATE_MS );
		}
		vTaskDelay(10 / portTICK_RATE_MS );
	}
}

void start_ib_reader(){
	TaskHandle_t ib_read_handler;
	const char task_name[] = "ibutton_reader task";
	if(xTaskCreate(ibutton_read_task, task_name, 4096,
			0, 5, &ib_read_handler) != pdPASS){
		printf("%s cannot be created.\n", task_name);
	}
}
