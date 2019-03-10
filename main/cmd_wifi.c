#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"
#include "esp_event_loop.h"
#include "cmd_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#define true 1
#define false 0

const int DEFAULT_TIMEOUT_MS = 8000;

static EventGroupHandle_t wifi_event_group;

const int CONNECTED_BIT = BIT0;

const char key_wifi_settings[] = "wifi_settings";
const char namespace_wifi[] = "wifi_nvs";

/** Arguments used by 'join' function */
static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_CONNECTED:
    	gpio_set_level(GPIO_NUM_2, 1);
    	break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        gpio_set_level(GPIO_NUM_2, 1);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        gpio_set_level(GPIO_NUM_2, 0);
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static int initialized = false;
    if (initialized) {
        return;
    }
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    initialized = true;
}
/**\brief Saves the actual config of the WiFi.
 * \ret NVS_ERR_NVS* when error occurs.
 * */
static esp_err_t wifi_save(const wifi_config_t wifi_config){
	nvs_handle handler;
	esp_err_t err;
	uint32_t size = sizeof(wifi_config_t);

	err = nvs_open(namespace_wifi, NVS_READWRITE, &handler);
	if(ESP_OK != err)
		return err;
	err = nvs_set_blob(handler, key_wifi_settings, &wifi_config, size);
	if(ESP_OK != err)
		return err;
	err = nvs_commit(handler);
	if(ESP_OK != err)
		return err;
	nvs_close(handler);
	return ESP_OK;
}
/** \brief Restors the WiFi config from flash.
 *  \return ESP_ERR_NVS* when error occurs.
 * */
esp_err_t wifi_restore(){
	nvs_handle handler;
	wifi_config_t wifi_config;
	uint32_t size = sizeof(wifi_config);
	esp_err_t err = nvs_open(namespace_wifi, NVS_READONLY, &handler);
	int timeout_ms = 10000;

	if(ESP_OK == err){
		err = nvs_get_blob(handler, key_wifi_settings, &wifi_config, &size);
		if(ESP_OK != err)
			return err;
		else{
			ESP_LOGI(__func__,"Found WiFi settings");
		}
	}
	else{
		return err;
	}

    ESP_LOGI(__func__, "Connecting to '%s'", wifi_config.sta.ssid);
    nvs_close(handler);

    initialise_wifi();
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
            1, 1, timeout_ms / portTICK_PERIOD_MS);
    if(bits & CONNECTED_BIT){
    	ESP_LOGW(__func__,"Connection timed out");
    }
    else{
    	ESP_LOGI(__func__,"Connected");
    }
    return ESP_OK;
}
/** \brief Make a WiFi connection with the parameters.
 *  \return 0 connection timed out.
 *  \return 1 properly connected.
 * */
static int wifi_join(const char* ssid, const char* pass, int timeout_ms)
{
	esp_err_t err;
    initialise_wifi();
    wifi_config_t wifi_config = { 0 };
    strncpy((char*) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strncpy((char*) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
            1, 1, timeout_ms / portTICK_PERIOD_MS);
    err = wifi_save(wifi_config);
    if(ESP_OK != err){
    	ESP_LOGE(__func__,"WiFi settings cannot be saved\n code:'%x'",err);
    }
    else{
    	ESP_LOGI(__func__,"WiFi settings has been saved");
    }
    return (bits & CONNECTED_BIT) != 0;
}

/** \brief Makes the console arguments to WiFi parameters.
 *  \return 0 WiFi connected.
 *  \return 1 WiFi connection error occurs.
 * */
static int connect(int argc, char** argv)
{
	esp_err_t err;
    int nerrors = arg_parse(argc, argv, (void**) &join_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, join_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(__func__, "Connecting to '%s'",
            join_args.ssid->sval[0]);

    int connected = wifi_join(join_args.ssid->sval[0],
                           join_args.password->sval[0],
                           join_args.timeout->ival[0]);
    if (!connected) {
        ESP_LOGW(__func__, "Connection timed out");
        return 1;
    }
    ESP_LOGI(__func__, "Connected");
    return 0;
}

void register_wifi()
{
    join_args.timeout = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
    join_args.timeout->ival[0] = DEFAULT_TIMEOUT_MS; // set default value
    join_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    join_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
    join_args.end = arg_end(2);

    const esp_console_cmd_t join_cmd = {
        .command = "join",
        .help = "Join WiFi AP as a station",
        .hint = NULL,
        .func = &connect,
        .argtable = &join_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&join_cmd) );
}

