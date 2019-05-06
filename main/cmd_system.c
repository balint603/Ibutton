/* Console example — various system commands
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "rom/uart.h"
#include "cmd_system.h"
#include "sdkconfig.h"
#include "cmd_tests.h"
#include "ib_http_client.h"
#include "ib_reader.h"


#define TEST_COMMANDS

#ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#define WITH_TASKS_INFO 1
#endif

#define MALLOC_CAP_DEFAULT (1<<12) ///< Memory can be returned in a non-capability-specific memory allocation (e.g. malloc(),

static const char *TAG = "cmd_system";

#if WITH_TASKS_INFO
static void register_tasks();
#endif

/* 'version' command */
static int get_version(int argc, char **argv)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    printf("IDF Version:%s\r\n", esp_get_idf_version());
    printf("Chip info:\r\n");
    printf("\tmodel:%s\r\n", info.model == CHIP_ESP32 ? "ESP32" : "Unknow");
    printf("\tcores:%d\r\n", info.cores);
    printf("\tfeature:%s%s%s%s%d%s\r\n",
           info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
           info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
           info.features & CHIP_FEATURE_BT ? "/BT" : "",
           info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash:" : "/External-Flash:",
           spi_flash_get_chip_size() / (1024 * 1024), " MB");
    printf("\trevision number:%d\r\n", info.revision);
    return 0;
}

static void register_version()
{
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &get_version,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'restart' command restarts the program */

static int restart(int argc, char **argv)
{
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}

static void register_restart()
{
    const esp_console_cmd_t cmd = {
        .command = "restart",
        .help = "Software reset of the chip",
        .hint = NULL,
        .func = &restart,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'free' command prints available heap memory */

static int free_mem(int argc, char **argv)
{
    printf("%d\n", esp_get_free_heap_size());
    return 0;
}

static void register_free()
{
    const esp_console_cmd_t cmd = {
        .command = "free",
        .help = "Get the current size of free heap memory",
        .hint = NULL,
        .func = &free_mem,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/* 'heap' command prints minumum heap size */
static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "min heap size: %u", heap_size);
    return 0;
}

static void register_heap()
{
    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "Get minimum size of free heap memory that was available during program execution",
        .hint = NULL,
        .func = &heap_size,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&heap_cmd) );

}

static struct {
	struct arg_str *name;
    struct arg_end *end;
} setname_args;

static struct {
	struct arg_str *code;
    struct arg_end *end;
} setsu_args;

static struct {
	struct arg_int *time;
    struct arg_end *end;
} settime_args;

static struct {
	struct arg_int *mode;
    struct arg_end *end;
} setmode_args;

static int setname(int argc, char **argv) {
	int nerrors = arg_parse(argc, argv, (void**) &setname_args);
	if ( nerrors ) {
		arg_print_errors(stderr, setname_args.end, argv[0]);
		return 1;
	}
	ESP_LOGI(TAG, "Change name to %s", setname_args.name->sval[0]);
	ib_set_device_name(setname_args.name->sval[0]);
	return 0;
}

static int setsu(int argc, char **argv) {
	uint64_t code;
	int nerrors = arg_parse(argc, argv, (void**) &setsu_args);
	if ( nerrors ) {
		arg_print_errors(stderr, setsu_args.end, argv[0]);
		return 1;
	}
	code = strtoull(setsu_args.code->sval[0], NULL, 16);
	ESP_LOGI(TAG, "Set code to: %lld", code);
	ib_set_su_key(code);
	return 0;
}

static int settime(int argc, char **argv) {
	int nerrors = arg_parse(argc, argv, (void**) &settime_args);
	if ( nerrors ) {
		arg_print_errors(stderr, settime_args.end, argv[0]);
		return 1;
	}
	ESP_LOGI(TAG, "Set opening time to: %i",settime_args.time->ival[0]);
	ib_set_opening_time(settime_args.time->ival[0]);
	return 0;
}

static int setmode(int argc, char **argv) {
	int nerrors = arg_parse(argc, argv, (void**) &setmode_args);
	if ( nerrors ) {
		arg_print_errors(stderr, setmode_args.end, argv[0]);
		return 1;
	}
	ESP_LOGI(TAG, "Set mode to: %i",settime_args.time->ival[0]);
	ib_set_mode(setmode_args.mode->ival[0]);
	return 0;
}

static void register_setters() {
	setname_args.name = arg_str1(NULL, NULL, "<name>", "Device name");
	setname_args.end = arg_end(0);
	const esp_console_cmd_t setname_cmd = {
		.command = "name",
		.help = "Set the device name",
		.hint = NULL,
		.func = &setname,
		.argtable = &setname_args
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&setname_cmd))

	setsu_args.code = arg_str1(NULL, NULL, "<su code>", "Superuser key code");
	setsu_args.end = arg_end(0);
	const esp_console_cmd_t setsu_cmd = {
		.command = "setsu",
		.help = "Set the su key code",
		.hint = NULL,
		.func = &setsu,
		.argtable = &setsu_args
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&setsu_cmd))

	settime_args.time = arg_int1(NULL, NULL, "<ms>", "Set the opening time");
	settime_args.end = arg_end(0);
	const esp_console_cmd_t settime_cmd = {
		.command = "opening",
		.help = "Change opening time",
		.hint = NULL,
		.func = &settime,
		.argtable = &settime_args
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&settime_cmd))

	setmode_args.mode = arg_int1(NULL, NULL,
			"<mode>", "Set the operation mode: \n 0: monostable\n1: bistable\n2: bistable with the same key");
	setmode_args.end = arg_end(0);
	const esp_console_cmd_t setmode_cmd = {
		.command = "mode",
		.help = "Change the operation",
		.hint = NULL,
		.func = &setmode,
		.argtable = &setmode_args
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&setmode_cmd))
}

void register_system()
{
    register_free();
    register_heap();
    register_version();
    register_restart();
    register_setserver();
    register_setters();
#ifdef TEST_COMMANDS
	register_tests();
#endif
#if WITH_TASKS_INFO
    register_tasks();
#endif
}

/** 'tasks' command prints the list of tasks and related information */
#if WITH_TASKS_INFO

static int tasks_info(int argc, char **argv)
{
    const size_t bytes_per_task = 40; /* see vTaskList description */
    char *task_list_buffer = malloc(uxTaskGetNumberOfTasks() * bytes_per_task);
    if (task_list_buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate buffer for vTaskList output");
        return 1;
    }
    fputs("Task Name\tStatus\tPrio\tHWM\tTask#", stdout);
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
    fputs("\tAffinity", stdout);
#endif
    fputs("\n", stdout);
    vTaskList(task_list_buffer);
    fputs(task_list_buffer, stdout);
    free(task_list_buffer);
    return 0;
}

static void register_tasks()
{
    const esp_console_cmd_t cmd = {
        .command = "tasks",
        .help = "Get information about running tasks",
        .hint = NULL,
        .func = &tasks_info,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

#endif // WITH_TASKS_INFO
