/*
 * ib_log.c
 *
 *  Created on: Apr 9, 2019
 *      Author: root
 */


#include  "ib_log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "ib_reader.h"
#include "ib_database.h"
#include "cJSON.h"

#define TESTMODE


int ib_send_json_logm(ib_log_t *logmsg) {
	cJSON *time_j = NULL;
	char code_buf[17];

	cJSON *logm = cJSON_CreateObject();
	if ( !logm ) {
		goto end;
	}

	if ( !cJSON_AddStringToObject(logm, "device", ib_get_device_name()) ) {
		goto end;
	}

	snprintf(code_buf, sizeof(code_buf), "%llX", logmsg->code);
	if ( !cJSON_AddStringToObject(logm, "key code", code_buf) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(logm, "type", logmsg->log_type) ) {
		goto end;
	}

	time_j = cJSON_CreateObject();
	if ( !time_j ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "sec", logmsg->timestamp.tm_sec) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "min", logmsg->timestamp.tm_min) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "hour", logmsg->timestamp.tm_hour) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "day", logmsg->timestamp.tm_mday) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "month", logmsg->timestamp.tm_mon) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "year", logmsg->timestamp.tm_year) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "weekday", logmsg->timestamp.tm_wday) ) {
		goto end;
	}
	cJSON_AddItemToObject(logm, "time stamp", time_j );


#ifdef TESTMODE
	char *string = cJSON_Print(logm);
	if ( string )
		ESP_LOGD(__func__,"%s",string);
	else
		ESP_LOGD(__func__,"Failed to print log message");
#endif
end:
	cJSON_Delete(logm);
	return 0;
}





















