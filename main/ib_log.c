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

	time_t time_raw;
	struct tm time_now;
	time(&time_raw);
	localtime_r(&time_raw, &time_now);

	if ( !cJSON_AddStringToObject(logm, "device", ib_get_device_name()) ) {
		goto end;
	}

	snprintf(code_buf, sizeof(code_buf), "%llX", logmsg->value);
	if ( !cJSON_AddStringToObject(logm, "key code", code_buf) ) {
		goto end;
	}

	if ( !cJSON_AddStringToObject(logm, "type", logmsg->log_type) ) {
		goto end;
	}

	time_j = cJSON_CreateObject();
	if ( !time_j ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "sec", time_now.tm_sec) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "min", time_now.tm_min) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "hour", time_now.tm_hour) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "day", time_now.tm_mday) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "month", time_now.tm_mon) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "year", time_now.tm_year) ) {
		goto end;
	}

	if ( !cJSON_AddNumberToObject(time_j, "weekday", time_now.tm_wday) ) {
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

void save_to_flash(char *data, size_t len) {
	esp_err_t ret;
	ret = ibd_log_append_file(data, &len);
	if ( ret == IBD_ERR_CRITICAL_SIZE ) {
		ib_need_su_touch();
	}
}

/** \brief JSON log info sender.
 * 	Send the JSON object waiting in queue or flash.
 * */
static void logsender_task() {
	ib_log_t msg;
	cJSON *msg_json = NULL;
	char *data_str;
	size_t data_len;
	while ( 1 ) {
		xQueueReceive(g_queue, &msg, portMAX_DELAY);

		msg_json = create_json_msg(&msg);
		if ( msg_json ) {
			data_str = cJSON_Print(msg_json);
			cJSON_Delete(msg_json);
			// Send previous data from flash
			if ( data_str ) {
				data_len = strlen(data_str) + 1;
				if ( ib_client_send_logmsg(data_str, data_len) ) {	// Try to send current msg
					ESP_LOGE(__func__,"Cannot send");
					save_to_flash(data_str, data_len);
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

	ib_log_t msg = {.log_type = IB_SYSTEM_UP, .value = 0};

	if ( ibd_log_check_mem_enough() ) {
		ESP_LOGE(TAG, "Not enough memory in partition SPIFFS");
		return;
	}

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



	xQueueSend(g_queue, &msg, 0);
	ESP_LOGI(TAG, "Initialized");
	ib_log_initialized = 1;
}




















