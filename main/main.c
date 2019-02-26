#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ibutton.h"


static const char* TAG = "example";

static void initialize_console(){
	gpio_pad_select_gpio(GPIO_NUM_2);
	gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
	gpio_set_level(GPIO_NUM_2, 0);
    /* Disable buffering on stdin and stdout */
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
            .baud_rate = CONFIG_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .use_ref_tick = 1
    };
    ESP_ERROR_CHECK( uart_param_config(CONFIG_CONSOLE_UART_NUM, &uart_config) );

    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_CONSOLE_UART_NUM,
            256, 0, 0, NULL, 0) );

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_CONSOLE_UART_NUM);

    /* Initialize the console */
    esp_console_config_t console_config = {
            .max_cmdline_args = 8,
            .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
            .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config));

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);


    /* Register commands */
    esp_console_register_help_command();
    register_system();
    register_wifi();
}

TaskFunction_t cmd_task(){
	nvs_flash_init();
	initialize_console();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    const char* prompt = LOG_COLOR_I "esp32> " LOG_RESET_COLOR;

    printf("\n"
           "---- ESP-IDF console component. ----\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n");

    /* Figure out if the terminal supports escape sequences */
    int probe_status = linenoiseProbe();
    if (probe_status) { /* zero indicates success */
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");
        linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        prompt = "esp32> ";
#endif //CONFIG_LOG_COLORS
    }
	while(1){
		/* Get a line using linenoise.
		 * The line is returned when ENTER is pressed.
		 */
		char* line = linenoise(prompt);
		if (line == NULL) { /* Ignore empty lines */
			continue;
		}
		/* Add the command to the history */
		linenoiseHistoryAdd(line);
		/* Try to run the command */
		int ret;
		esp_err_t err = esp_console_run(line, &ret);
		if (err == ESP_ERR_NOT_FOUND) {
			printf("Unrecognized command\n");
		} else if (err == ESP_ERR_INVALID_ARG) {
			// command was empty
		} else if (err == ESP_OK && ret != ESP_OK) {
			printf("Command returned non-zero command code.\n");
		} else if (err != ESP_OK) {
			printf("Internal error\n");
		}
		/* linenoise allocates line buffer on the heap, so need to free it */
		linenoiseFree(line);
	}
}

TaskFunction_t blink_task(){
	gpio_pad_select_gpio(GPIO_NUM_4);
	gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    /* Main loop */
    while(1) {
    	gpio_set_level(GPIO_NUM_4, 0);
    	vTaskDelay(4900/portTICK_RATE_MS);
    	gpio_set_level(GPIO_NUM_4, 1);
		vTaskDelay(100/portTICK_RATE_MS);
    }
}

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


void app_main(){
	TaskHandle_t cmd_handle;
	TaskHandle_t ib_read_handle;
	TaskHandle_t led_handle;

	if(xTaskCreate(cmd_task, "cmd_task", 4096,
				0, 5, &cmd_handle) !=pdPASS){
		printf("cmd_task cannot be created.\n");
	}
	if(xTaskCreate(ibutton_read_task, "ib_read_task", 2048,
			0, 5, &ib_read_handle) != pdPASS){
		printf("ib_read_task cannot be created.\n");
	}
	if(xTaskCreate(blink_task, "blink_task", configMINIMAL_STACK_SIZE,
				0, 6, &led_handle) !=pdPASS){
		printf("blink_task cannot be created.\n");
	}
	while(1){
		vTaskDelay(1000);
	}
}
