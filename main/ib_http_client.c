/**
 * ib_http_client.c
 * \addtogroup ib_http_client
 * @{
 *
 *  Created on: Apr 5, 2019
 *      Author: root
 *
 *  This module is created to allow HTTP communication with specified server.
 *
 *  The checksum from server is periodically downloaded and compared to the local one.
 *  If the checksum does not match, the DSV file will be pulled down.
 *  The URLS can be configured on console with a serial terminal.
 *
 *
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
#include "ib_reader.h"

//#define TESTMODE

#define TAG "iB_client"

/** \brief HTTP stream buffer size. */
#define HTTP_RECEIVE_BUFFER 	2048

/** \brief GET request for checksum file buffer size. */
#define HTTP_CHECKSUM_BUFFER 	512

/** \brief POST data log message buffer. */
#define HTTP_POST_FILE_BUFFER	4096

/** \brief SPIFFS key. */
const char key_server_info[] = "db_url";
/** \brief SPIFFS namespace. */
const char namespace[] = "wifi_nvs";

const int HTTP_USERD_CHECKSUM = 55;

extern EventGroupHandle_t wifi_event_group;
extern const int CONNECTED_BIT;

/** \brief Server paths. */
static struct {
	struct arg_str *server_URL;
    struct arg_str *checksum_file_path;
    struct arg_str *database_file_path;
    struct arg_str *logfile_dir_path;
    struct arg_end *end;
} setserver_args;

#define URL_MAXLEN 63

/** \brief Server URLS. */
typedef struct ib_server_conf {
	char server_url[32];
	char ch_url[URL_MAXLEN + 1];	// Checksum
	char db_url[URL_MAXLEN + 1];	// Database
	char log_url[URL_MAXLEN + 2];	// Log
} ib_server_conf_t;

/** \brief Event group bits. */
#define BIT_START_UPDATING 		BIT0
#define BIT_START_UPDATE_NOW 	BIT1

EventGroupHandle_t g_client_event_group;
TaskHandle_t g_update_handler;

ib_server_conf_t g_server_conf;

/** \brief Based on sample project. */
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
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

#endif
            }
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

/** \brief Refresh server configuration.
 *  It copies the configuration data from NVS. Used after initialization.
 */
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
	ESP_LOGI(TAG,"data from nvs:\nChecksum path:%s\nDatabase path:%s\nLogfile path:%s",
			g_server_conf.ch_url, g_server_conf.db_url, g_server_conf.log_url);
	nvs_close(nvs);
	return ESP_OK;
}

char *ib_client_get_log_url() {
	return g_server_conf.log_url;
}

/** \brief Download from server and write via SPIFFS.
 * This function downloads the whole DSV file from server.
 * File will be saved in the specified DSV file in file system.
 * \return -1 Not enough memory
 * \return 1 Any error occurred.
 * \return ESP_OK when successfully download.
 */
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
		.event_handler = http_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if ( !client ) {
    	ESP_LOGE(TAG, "Invalid URL");
    	free(buffer);
    	return 1;
    }
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
    return ESP_OK;
}

/** \brief Get checksum value from server.
 *  Get the checksum file from server and copy to argument.
 *  \param checksum value from server.
 *  \return 0 Successfully downloaded.
 *  \return -1 Malloc failed
 *  \return 1 HTTP failure
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
		.event_handler = http_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if ( !client ) {
    	free(buffer);
    	ESP_LOGE(TAG, "Invalid URL");
    	return 1;
    }
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

/** \brief Post the logfile from flash.
 *  This function uploads the logfile from local file. The logfile is deleted when HTTP request ends successful.
 *
 * \return ESP_ERR_NOT_FOUND File cannot be opened.
 * \return ESP_ERR_HTTP_CONNECT HTTP connection failure.
 * \return ESP_ERR_HTTP_WRITE_DATA When the whole file cannot be read or cannot write to http stream.
 */
esp_err_t post_logfile() {
	esp_err_t ret;
	FILE *fptr;
	char buff[HTTP_POST_FILE_BUFFER];
	size_t fsize = 0, buflen;

	fptr = fopen(FILE_LOG, "r");
	if ( !fptr ) {
		return ESP_ERR_NOT_FOUND;
	}
	fsize = get_file_size(fptr);
	esp_http_client_config_t config = {
			.url = g_server_conf.log_url,
			.event_handler = http_event_handler
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);
	if ( !client ) {
		fclose(fptr);
		return ESP_ERR_HTTP_CONNECT;
	}
	esp_http_client_set_method(client, HTTP_METHOD_POST);
	ret = esp_http_client_open(client, fsize);
	if ( ret != ESP_OK ) {
		esp_http_client_cleanup(client);
		fclose(fptr);
		return ESP_ERR_HTTP_FETCH_HEADER;
	}
	while ( fsize > 0 ) {
		buflen = (fsize > HTTP_POST_FILE_BUFFER) ?
				HTTP_POST_FILE_BUFFER : fsize;

		ret = fread(buff, sizeof(char), buflen, fptr);
		if ( ret != buflen ) {
			ESP_LOGD(__func__, "FILE read bytes remaining");
			fclose(fptr);
			return ESP_ERR_HTTP_WRITE_DATA;
		}
#ifdef TESTMODE
		ESP_LOGD(__func__,"%s", buff);
#endif
		ret = esp_http_client_write(client, buff, buflen);
		fsize -= ret;
		if ( ret != buflen ) {
			ESP_LOGD(__func__, "HTTP write failed: %i bytes remaining", fsize - ret);
			fclose(fptr);
			return ESP_ERR_HTTP_WRITE_DATA;
		}
	}
	ret = esp_http_client_get_status_code(client);
	ESP_LOGD(__func__,"Response status code: %i", ret);
	esp_http_client_close(client);
	esp_http_client_cleanup(client);
	fclose(fptr);
	return ESP_OK;
}

/** \brief Sync task.
 * Get the current database checksum and post the logfile if exist.
 * Task can be hold by groupbit: BIT_START_UPDATING
 * */
void update_from_server_task() {
	uint64_t checksum_got;
	info_t checksums;
	esp_err_t ret;
    while ( 1 ) {
    	xEventGroupWaitBits(g_client_event_group, BIT_START_UPDATING,
    			pdFALSE, pdTRUE, portMAX_DELAY);

    	if ( !get_checksum_from_server(&checksum_got) ) {		// Successfully connected and downloaded
			ibd_get_checksum(&checksums);
			if ( checksum_got != checksums.checksum_cur ) {
				ESP_LOGI(TAG, "Start downloading csv file");
				if ( ESP_OK != save_csv_from_server(checksum_got) ) {
					ESP_LOGI(TAG, "DSV file cannot be download.");
				} else {
					ESP_LOGI(TAG, "Successful download.");
				}
			}
    	}
    	if ( ibd_log_check_file_exist() ) {
    		ret = post_logfile();
    		if ( ret != ESP_OK ) {
    			ESP_LOGE(__func__,"Cannot post logfile: %s", esp_err_to_name(ret));
    		} else {
    			ibd_log_delete();
    			if ( ib_waiting_for_su_touch() ) {
    				ib_not_need_su_touch();
    			}
    		}
    	}
    	xEventGroupWaitBits(g_client_event_group, BIT_START_UPDATE_NOW,
    			pdTRUE, pdTRUE, UPDATES_PERIOD_MS / portTICK_PERIOD_MS);
    }
}

/** \brief Send a specific cJSON message.
 * Post HTTP data to specified file path.
 * 	\return 0 Successfully sent.
 * 	\return 1 HTTP error.
 *  \return 2 JSON error.
 * 	\return -1 Argument null.
 * */
int ib_client_send_logmsg(char *data, size_t length) {
	if ( !data )
		return -1;
	//char length_str[64];
	esp_err_t ret;
	esp_http_client_config_t config = {
			.url = g_server_conf.log_url,
			.event_handler = http_event_handler
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);
	if ( !client ) {
		ESP_LOGE(TAG, "Invalid URL");
		return 1;
	}
	esp_http_client_set_method(client, HTTP_METHOD_POST);
	ret = esp_http_client_open(client, length);
	if ( ret != ESP_OK ) {
		ESP_LOGE(__func__, "HTTP open failed: %s", esp_err_to_name(ret));
		esp_http_client_cleanup(client);
		return 1;
	}
	//itoa(length,length_str, 10);
	//esp_http_client_set_header(client, "Content-Length", length_str);
	ret = esp_http_client_write(client, data, length);
	if ( ret != length ) {
		ESP_LOGE(__func__, "HTTP write failed: %i bytes remaining", length - ret);
	}
	ret = esp_http_client_get_status_code(client);
	ESP_LOGD(__func__,"Response status code: %i", ret);
	esp_http_client_close(client);
	esp_http_client_cleanup(client);
	return 0;
}

/** Stop updating periodically */
void ib_client_stop_updating() {
	xEventGroupClearBits(g_client_event_group, BIT_START_UPDATING);
}

/** Start updating periodically */
void ib_client_start_updating() {
	xEventGroupSetBits(g_client_event_group, BIT_START_UPDATING);
}

/** \brief Create a polling task.
 * */
esp_err_t ib_client_init() {
	esp_err_t ret;
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

	g_client_event_group = xEventGroupCreate();

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

/** \brief Copy the parameters to NVS. */
static int save_servers_conf(int argc, char *URL, char *ch_file, char *db_file, char *log_dir) {
	if ( !ch_file || !db_file || !URL)
		return 1;
	nvs_handle nvs;
	esp_err_t ret;
	const size_t url_len = strlen(URL);
	if ( url_len > URL_MAXLEN/2 ) {
		ESP_LOGE(TAG, "Too long URL");
		return 1;
	}
	strcpy(g_server_conf.server_url, URL);
	if ( strlen(ch_file) <= URL_MAXLEN/2 ) {
		strcpy(g_server_conf.ch_url, URL);
		strcat(g_server_conf.ch_url, ch_file);
	} else {
		ESP_LOGW(TAG, "Too long checksum file path");
	}

	if ( strlen(db_file) <= URL_MAXLEN/2 ) {
			strcpy(g_server_conf.db_url, URL);
			strcat(g_server_conf.db_url, db_file);
	} else {
		ESP_LOGW(TAG, "Too long data file path");
	}

	if ( argc >= 5 || log_dir) {	// LOGFILE URL
		strcpy(g_server_conf.log_url, URL);
		if ( URL[url_len-1] != '/' ) {
			g_server_conf.log_url[url_len] = '/';
			g_server_conf.log_url[url_len+1] ='\0';
		}
		strcat(g_server_conf.log_url, log_dir);
		strcat(g_server_conf.log_url, ib_get_device_name());
		strcat(g_server_conf.log_url, ".log");
	} else {
		g_server_conf.log_url[0] = '\0';
		// TODO TURN OFF LOGGING
	}
	ESP_LOGI(TAG, "Logfile path:%s", g_server_conf.log_url);

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

/** \brief Command callback function. */
static int setserver_url(int argc, char** argv) {
	if ( argc < 4 ) {
		if ( argc <= 1)
			printf("Missing server URL\n ");
		if ( argc <= 2)
			printf("Missing checksum file path\n ");
		if ( argc <= 3 )
			printf("Missing database file path\n");
		return 1;
	}
	if ( !(argv[1] && argv[2] && argv[3])  )
		return 1;
	int nerrors = arg_parse(argc, argv, (void**) &setserver_args);
	if ( nerrors ) {
		arg_print_errors(stderr, setserver_args.end, argv[0]);
		return 1;
	}

	return save_servers_conf(argc, argv[1], argv[2], argv[3], argv[4]);
}

/** \brief Command register function. */
void register_setserver() {
	setserver_args.server_URL 		  = arg_str1(NULL, NULL, "<URL>", "URL of server");
	setserver_args.checksum_file_path = arg_str1(NULL, NULL, "<File path>", "File path of checksum file");
	setserver_args.database_file_path = arg_str1(NULL, NULL, "<File path>", "File path of csv file");
	setserver_args.logfile_dir_path   = arg_str0(NULL, NULL, "<Dir path>", "Dir path of logging file");
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


/** @} */
















