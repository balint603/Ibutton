/*
 * sntp.c
 *
 *  Created on: Mar 15, 2019
 *      Author: root
 */


#include "ib_sntp.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "time.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "lwip/err.h"
#include "/home/major/Documents/ESP32/ESP-IDF/IDF/components/lwip/include/lwip/apps/sntp/sntp.h"
#include "/home/major/Documents/ESP32/ESP-IDF/IDF/components/lwip/include/lwip/lwip/ip_addr.h"

#include "ib_reader.h"	// Set esp time

#include "/home/major/Documents/ESP32/ESP-IDF/IDF/components/lwip/include/lwip/lwip/dns.h"

#define YEAR_NOT_VALID 2018

#define SERVER_NAME_MAX_SIZE 64
#define SERVER_NAMES_N 4

EventGroupHandle_t ib_sntp_event_group;


/** NTP Server names */
const char *SERVER_NAMES[SERVER_NAMES_N]
			 = {"pool.ntp.org","time1.google.com","time2.google.com","time3.google.com"};
/** Wanted server name */
char g_user_defined_server[SERVER_NAME_MAX_SIZE];

/** Current NTP server name */
int g_current_server_index = 0;
volatile char *g_chosen_server_name = NULL;

/** \brief Specify the server domain name
 * Must be called before ib_sntp_obtain_time
 * */
esp_err_t ib_sntp_set_ntp_server(const char *name){
	if(!name || (strlen(name)) >= SERVER_NAME_MAX_SIZE)
		return ESP_ERR_INVALID_ARG;
	strcpy(g_user_defined_server, name);
	g_chosen_server_name = g_user_defined_server;
	return ESP_OK;
}
/**
 * \ret 1 No more servers
 * \ret 0 There are
 * */
static int choose_another_server(){
	if(++g_current_server_index < SERVER_NAMES_N){
		g_chosen_server_name = SERVER_NAMES[g_current_server_index];
		return 0;
	}
	ESP_LOGW(__func__,"No more NTP server domains to try.\n");
	return 1;
}

/** \brief Get time from NTP server task.
 *
 * */
static void obtain_time_task(){
	time_t now = 0;
	struct tm time_info = { 0 };

	int retries = 1;
	const int retries_max = 5;
	while ( time_info.tm_year < (2019 - 1900)){
		ESP_LOGI(__func__,"Waiting for time to be set... %i/%i",retries,retries_max);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		time(&now);
		localtime_r(&now, &time_info);
		if(++retries > retries_max){
			ESP_LOGW(__func__,"SNTP server not available, try another...\n Domain:%s",g_chosen_server_name);
			if( choose_another_server() ){
				ESP_LOGE(__func__,"No NTP servers available");
				vTaskDelete(xTaskGetCurrentTaskHandle());
			}
			printf("Try another server:%s\n",g_chosen_server_name);
			retries = 1;
		}
	}
	setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);
	tzset();
	time(&now);
	localtime_r(&now, &time_info);
	ESP_LOGI(__func__,"Got time from SNTP server. Domain:%s",g_chosen_server_name);
	ESP_LOGI(__func__,"Current local time: %s", asctime(&time_info));
	xEventGroupSetBits(ib_sntp_event_group, IB_TIME_SET_BIT);
	vTaskDelete(xTaskGetCurrentTaskHandle());
}

/** \brief Make a task to obtain time.
 *
 * */
void ib_sntp_obtain_time(){
	ESP_LOGI(__func__,"Obtain local time via SNTP");
	sntp_stop();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	if(g_chosen_server_name == NULL)
		g_chosen_server_name = SERVER_NAMES[0];
	//sntp_setserver(idx, addr)
	sntp_setservername(0, g_chosen_server_name);
	sntp_init();
	ib_sntp_event_group = xEventGroupCreate();
	TaskHandle_t obtain_time_task_h;	// Listener task, decide the time is successfully set or not
	xTaskCreate(obtain_time_task, "sntp_obtain", 4096, NULL, 5, &obtain_time_task_h);
}























