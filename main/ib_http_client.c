/*
 * ib_http_client.c
 *
 *  Created on: Apr 5, 2019
 *      Author: root
 */

#include "ib_http_client.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "cmd_wifi.h"
#include "ib_database.h"

#define TESTMODE

#define HTTP_RECEIVE_BUFFER 	2048
#define HTTP_CHECKSUM_BUFFER 	512

const char key_server_info[] = "db_url";
const char namespace[] = "wifi_nvs";

const int HTTP_USERD_CHECKSUM = 55;

extern EventGroupHandle_t wifi_event_group;
extern const int CONNECTED_BIT;

static struct {
    struct arg_str *checksum_file_path;
    struct arg_str *database_file_path;
    struct arg_end *end;
} setserver_args;

#define URL_MAXLEN 63
typedef struct ib_server_conf {
	char ch_url[URL_MAXLEN + 1];	// Checksum
	char db_url[URL_MAXLEN + 1];	// Database
} ib_server_conf_t;


#define TAG "iB_client"

#define BIT_START_UPDATING 		BIT0					// Event group bit
#define BIT_START_UPDATE_NOW 	BIT1

EventGroupHandle_t g_event_group;
TaskHandle_t g_update_handler;				// Task handler

ib_server_conf_t g_server_conf;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
#ifdef TESTMODE
            	if ( evt->data_len && evt->data )
            		printf("HTTPS_EVENT_DATA[%.*s]", evt->data_len, (char*)evt->data);
            }
#endif
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}


/** \brief Get and Set NVS data about server URL.
 *
 *  */
static esp_err_t refresh_server_conf() {
	nvs_handle nvs;
	esp_err_t ret;
	uint32_t length = sizeof(g_server_conf);
	ret = nvs_open(namespace, NVS_READONLY, &nvs);
	if ( ESP_OK != ret ) {
		ESP_LOGE(TAG,"cannot open nvs,%x",ret);
		return ret;
	}
	ret = nvs_get_blob(nvs, key_server_info, &g_server_conf, &length);
	if ( ESP_OK != ret ) {
		ESP_LOGE(TAG,"cannot get data from nvs:%s", esp_err_to_name(ret));
		nvs_close(nvs);
		return ret;
	}
	ESP_LOGI(TAG,"data from nvs:\nChecksum path:%s\nDatabase path:%s",g_server_conf.ch_url, g_server_conf.db_url);
	nvs_close(nvs);
	return ESP_OK;
}

/** \brief Download from server and write via spiffs.
 *  BUFFERSIZE < CSV?
 *  PROTOTYPE!!!
 * */
esp_err_t save_csv_from_server(uint64_t checksum) {
	char *buffer = malloc(HTTP_RECEIVE_BUFFER+1);
	esp_err_t ret;


	if ( !buffer ) {
		ESP_LOGE(__func__,"Cannot malloc for receive buffer");
		return -1;
	}

    esp_http_client_config_t config = {
        .url = g_server_conf.db_url,
		.buffer_size = HTTP_RECEIVE_BUFFER + 1,
		.event_handler = _http_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    ret = esp_http_client_open(client, 0);
    if ( ESP_OK != ret ) {
    	ESP_LOGE(TAG,"Failed to open HTTP connection: %s", esp_err_to_name(ret));
    	free(buffer);
    	return 1;
    }

	int read_len;
	int to_read_len;
	int content_left = esp_http_client_fetch_headers(client);
    ESP_LOGD(TAG, "content_len:%i", content_left);

	do  {
		to_read_len = (content_left <= HTTP_RECEIVE_BUFFER) ? content_left : HTTP_RECEIVE_BUFFER;
		read_len = esp_http_client_read(client, buffer, to_read_len);
		if ( read_len == -1 ) {		// RET -1
			ESP_LOGE(TAG, "Read HTTP stream error");
		}
		content_left -= read_len;
		buffer[read_len] = '\0';
		ESP_LOGD(TAG, "read_len:%d",read_len);
		ESP_LOGD(TAG, "data:%s", buffer);
		ret = ibd_append_csv_file(buffer, &read_len, checksum);
		if ( ESP_OK != ret ) {
			ESP_LOGE(TAG, "Cannot save to file: %s", esp_err_to_name(ret));
		}
	} while ( (content_left) > 0 );
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ibd_make_bin_database();
    free(buffer);
    return 1;
}

/** \brief Get checksum value from server.
 *
 * */
int get_checksum_from_server(uint64_t *checksum) {
	char *buffer = malloc(HTTP_CHECKSUM_BUFFER+1);
	esp_err_t ret;
	int content_len;
	int read_len;

	if ( !buffer ) {
		ESP_LOGE(__func__,"Cannot malloc for receive buffer");
		return -1;
	}

    esp_http_client_config_t config = {
        .url = g_server_conf.ch_url,
		.buffer_size = HTTP_CHECKSUM_BUFFER + 1,
		.event_handler = _http_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    ret = esp_http_client_open(client, 0);
    if ( ESP_OK != ret ) {
    	ESP_LOGE(TAG,"Failed to open HTTP connection: %s", esp_err_to_name(ret));
    	free(buffer);
    	return 1;
    }

    content_len = esp_http_client_fetch_headers(client);
    ESP_LOGD(TAG, "content_len:%i", content_len);
    if( 16 <= content_len && content_len <= (HTTP_CHECKSUM_BUFFER) ) {
    	read_len = esp_http_client_read(client, buffer, content_len);
    	if ( read_len <= 0) {		// RET -1
    		ESP_LOGE(TAG, "Read HTTP stream");
    	}
    	buffer[read_len] = '\0';
    	*checksum = strtoull(buffer, NULL, 16);
		ESP_LOGD(TAG, "read_len:%d",read_len);
    	ESP_LOGD(TAG, "data:%s", buffer);
    }
    if ( *checksum ) {
    	ESP_LOGD(TAG, "Checksum data:%llu", *checksum);
    	ret = ESP_OK;
    } else {
    	ret = 1;
    	ESP_LOGE(TAG, "Checksum data error");
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buffer);
    return 0;
}


void update_from_server_task() {
	uint64_t checksum_got;
	info_t checksums;

    while ( 1 ) {
    	xEventGroupWaitBits(g_event_group, BIT_START_UPDATING,
    			pdFALSE, pdTRUE, portMAX_DELAY);

    	get_checksum_from_server(&checksum_got);
    	ibd_get_checksum(&checksums);
    	if ( checksum_got != checksums.checksum_cur ) {
    		ESP_LOGI(TAG, "Start downloading csv file");
    		if ( ESP_OK != save_csv_from_server(checksum_got) ) {

    		} else {
    			ESP_LOGI(TAG, "Succesfully downloaded");
    		}
    	}
    	xEventGroupWaitBits(g_event_group, BIT_START_UPDATE_NOW,
    			pdTRUE, pdTRUE, UPDATES_PERIOD_MS / portTICK_PERIOD_MS);
    }
}

/** Stop updating periodically */
void ib_client_stop_updating() {
	xEventGroupClearBits(g_event_group, BIT_START_UPDATING);
}

/** Start updating periodically */
void ib_client_start_updating() {
	xEventGroupSetBits(g_event_group, BIT_START_UPDATING);
}

/** \brief Create a polling task.
 *
 * */
esp_err_t ib_client_init() {
	esp_err_t ret;
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

	g_event_group = xEventGroupCreate();

	if ( pdPASS != xTaskCreate(&update_from_server_task, "Database update",
			8192, NULL, 5, &g_update_handler) ) {
		ESP_LOGE(TAG, "Cannot create task");
		return 1;
	}
	ret = refresh_server_conf();
	if ( ESP_OK == ret ) {
		ib_client_start_updating();
	}
	else if (ESP_ERR_NVS_NOT_FOUND == ret ) {
		ESP_LOGW(TAG,"Settings cannot be find about database server");
	}
	else {
		ESP_LOGE(TAG,"Refresh error code:%x",ret);
	}
	return ESP_OK;
}

static int save_servers_conf(char *ch_url, char *db_url) {
	if ( !ch_url || !db_url )
		return 1;

	nvs_handle nvs;
	esp_err_t ret;
	if ( strlen(ch_url) <= URL_MAXLEN) {
		strcpy(g_server_conf.ch_url,ch_url);
	} else {
		ESP_LOGE(__func__,"Checksum URL too long");
	}
	if ( strlen(db_url) <= URL_MAXLEN) {
			strcpy(g_server_conf.db_url,db_url);
	} else {
		ESP_LOGE(__func__,"Database URL too long");
	}

	ret = nvs_open(namespace, NVS_READWRITE, &nvs);
	if ( ESP_OK != ret ) {
		return ret;
	}
	ret = nvs_set_blob(nvs, key_server_info, &g_server_conf, sizeof(g_server_conf));
	if ( ESP_OK != ret ) {
		nvs_close(nvs);
		return ret;
	}
	ret = nvs_commit(nvs);
	nvs_close(nvs);
	refresh_server_conf();
	return ret;
}

static int setserver_url(int argc, char** argv) {
	if ( argc < 3 ) {
		if ( argc <= 1)
			printf("Missing checksum URL\n ");
		if ( argc <= 2 )
			printf("Missing database URL\n");
		return 1;
	}
	if ( !(argv[1] && argv[2])  )
		return 1;
	int nerrors = arg_parse(argc, argv, (void**) &setserver_args);
	if ( nerrors ) {
		arg_print_errors(stderr, setserver_args.end, argv[0]);
		return 1;
	}

	return save_servers_conf(argv[1], argv[2]);
}

void register_setserver() {
	setserver_args.checksum_file_path = arg_str1(NULL, NULL, "<URL checksum>", "URL of checksum file");
	setserver_args.database_file_path = arg_str1(NULL, NULL, "<URL database>", "URL of database file");
	setserver_args.end = arg_end(0);
	const esp_console_cmd_t setserver_cmd = {
			.command = "setserver",
		    .help = "Change the database URL",
			.hint = NULL,
			.func = &setserver_url,
			.argtable = &setserver_args
	};
	ESP_ERROR_CHECK( esp_console_cmd_register(&setserver_cmd) );
}



















