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
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "ib_reader.h"
#include "ib_http_client.h"
#include "ib_database.h"
#include "cmd_wifi.h"
#include  "ib_sntp.h"

#define TAG 			"iB_logger"

#define TESTMODE
#define QUEUE_DEPTH		10

extern EventGroupHandle_t wifi_event_group;
extern const int CONNECTED_BIT;
extern EventGroupHandle_t ib_sntp_event_group;

volatile uint8_t ib_log_initialized = 0;

QueueHandle_t g_queue;

/** \brief Post a log message.
 * 	\param msg Message to be sent
 * */
void ib_log_post(ib_log_t *msg) {
	xQueueSend(g_queue, msg, 0);
}

cJSON *create_json_msg(ib_log_t *logmsg) {
	cJSON *time_j = NULL;
	char code_buf[17];
	cJSON *logm = cJSON_CreateObject();
	if ( !logm || !logmsg) {
		return NULL;
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
	if (logm)
		return logm;
end:
	cJSON_Delete(logm);
	return NULL;
}

/** \brief JSON log info sender.
 * 	Send the JSON object waiting in queue or flash.
 * */
static void logsender_task() {			// TODO:LOGGER Change this task to implement logging to flash when HTTP err.
	ib_log_t msg;
	cJSON *msg_json = NULL;
	char *data_str;
	while ( 1 ) {
		xQueueReceive(g_queue, &msg, portMAX_DELAY);

		msg_json = create_json_msg(&msg);
		if ( msg_json ) {
			data_str = cJSON_Print(msg_json);
			cJSON_Delete(msg_json);
			// Send previous data from flash
			if ( data_str ) {
				if ( ib_client_send_logmsg(data_str, strlen(data_str) + 1) ) {	// Try to send current msg
					ESP_LOGE(__func__,"Cannot send");
				} else {
					ESP_LOGD(__func__,"Send JSON log msg");
				}
			} else {
				ESP_LOGE(TAG, "JSON print error");
			}
		} else {
			ESP_LOGE(TAG, "JSON cannot be created");
		}
	}
}

void ib_log_init() {
	if ( ib_log_initialized ) {
		ESP_LOGW(TAG,"Already initialized");
		return;
	}
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
	xEventGroupWaitBits(ib_sntp_event_group, IB_TIME_SET_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
	time_t rawtime;
	ib_log_t msg;
	time(&rawtime);
	localtime_r(&rawtime, &msg.timestamp);
	msg.code = 0;
	msg.log_type = IB_SYSTEM_UP;

	g_queue = xQueueCreate(QUEUE_DEPTH, sizeof(ib_log_t));
	if ( !g_queue ) {
		ESP_LOGE(TAG, "No heap memory for queue");
		return;
	}

	if ( xTaskCreate(&logsender_task, "Logsender", 4096, NULL, 4, NULL)
			!= pdPASS) {
		ESP_LOGE(TAG, "Cannot create task");
		return;
	}

	// todo LOGGER: Check flash free size

	xQueueSend(g_queue, &msg, 0);
	ESP_LOGI(TAG, "Initialized");
	ib_log_initialized = 1;
}




















