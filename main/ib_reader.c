/**
 * ib_reader.c
 *
 *  Created on: Feb 26, 2019
 *      Author: root
 * @addtogroup ib_reader
 * @{
 * This module implements the access control functions like opening relay, blink the LEDs.
 *
 *
 *  It is state a machine which has more inputs:
 *  	- iButton reader,
 *  	- button,
 *  	- timeouts.
 *
 *  Inputs are realized with FreeRTOS queue, and inputs_t.\n
 *  Two tasks:
 *  	1.: reads the button state, and read the iButton data.
 *  	2.: informs the user of the state machine, example: blink leds
 * 	Uses
 * 		- software timers to create timeouts,
 * 		- queues to perform communications between the input generators and the state machine.
 *
 * 	<b> State diagram: </b>
 * 	\htmlonly <style>div.image img[src="draws/FSM.png"]{width:10px;}</style> \endhtmlonly
 *	@image html draws/FSM.png "Finite State Machine"
 */

#include "ib_reader.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "driver/uart.h"
#include "time.h"
#include "nvs.h"

#include "ibutton.h"
#include "cron.h"
#include "ib_database.h"
#include "ib_log.h"

#define TAG "IB_READER"

const char READER_KEY_NVS[] = "reader_config";
const char READER_NSPACE_NVS[] = "reader_ns";

/** \brief Input types of the state machine.
 *  Only these types of input works.
 * */
typedef enum inputs {
    input_touched,
    input_su_touched,
	input_invalid_touched,
    input_button,
	input_tout,
} inputs_t;

/** \brief Indicating the current state.
 *  This enumeration data is used as an input to a queue which is used by LED blinker task.
 *  Put any type of these in to queue to change color.
 * */
typedef enum infos {
	blink_green,
	blink_red,
	blink_both,
	blink_both2,
	blink_none,
} infos_t;

/** Configuration settings.
 *  */
typedef struct ib_conf{
	/** Super user key code. */
	uint64_t su_key;
	/** Opening time interval when mode is 0. */
	unsigned int openingtime;
	/** Operational mode. */ // todo change to enumeration
	unsigned int mode;
	unsigned int buttonenable;
	char devicename[128];
} ib_conf_t;

/** Holds the current state of the FSM. */
typedef void ( *p_state_handler )(inputs_t input);

/** FreeRTOS handlers */
typedef struct ib_handles{
	p_state_handler fsm_state;
	p_state_handler fsm_prev_state;
	SemaphoreHandle_t st_semaphor;
	QueueHandle_t input_q;
	TaskHandle_t reader_t;
	TaskHandle_t info_t;
	QueueHandle_t info_q;
	TimerHandle_t timeout_tim;
} ib_handlers;

#define ON GPIO_MODE_INPUT
#define OFF GPIO_MODE_OUTPUT
#define LED_RED(x)\
	gpio_set_direction(PIN_RED,x);
#define LED_GREEN(x)\
	gpio_set_direction(PIN_GREEN,x);

#define RELAY_OPEN gpio_set_level(PIN_RELAY,1);
#define RELAY_CLOSE gpio_set_level(PIN_RELAY,0);


#define INFO_QUEUE_LENGTH 5
#define INFO_QUEUE_ITEM_SIZE sizeof(infos_t)
#define INPUT_QUEUE_LENGTH 1
#define INPUT_QUEUE_ITEM_SIZE sizeof(inputs_t)

/** MUTEX wait */
#define MUT_WAIT (100 / portTICK_PERIOD_MS)

/** Basic time in ms */
#define TIMEOUT_BASIC_MS 30000

volatile uint32_t initialized;

ib_conf_t g_config = {
		.openingtime = STANDARD_OPENING_TIME,
		.devicename = STANDARD_DEVICE_NAME
};
ib_handlers g_handlers;

volatile BaseType_t g_enable_reader = pdTRUE;

volatile uint64_t g_accessed_key_prev;
volatile uint64_t g_accessed_key;

/** addtogroup state_functions
 * @{ */
static void st_check_touch();
static void st_wait_for_clear_log();
static void st_access_allow();
static void st_su_mode();
static void st_check_touch();
/** @} */

static void save_config();

static void refresh_config() {
	nvs_handle handl;
	esp_err_t ret;
	size_t size = sizeof(g_config);

	ret = nvs_open(READER_NSPACE_NVS, NVS_READONLY, &handl);
	if ( ret == ESP_OK ) { // Found
		ret = nvs_get_blob(handl, READER_KEY_NVS, &g_config, &size);
		nvs_close(handl);
		if ( ret == ESP_OK ) {
			ESP_LOGI(TAG, "Found configuration in NVS");
		} else {
			ESP_LOGE(__func__,"NVS get: %s", esp_err_to_name(ret));
		}
		return;
	} else {
		ESP_LOGE(__func__,"NVS open: %s", esp_err_to_name(ret));
	}
	ESP_LOGI(TAG, "Not found configuration in NVS");
	g_config.buttonenable = 1;
	strcpy(g_config.devicename, STANDARD_DEVICE_NAME);
	g_config.mode = IB_READER_MODE_NORMAL;
	g_config.openingtime = 3000;
	g_config.su_key = 0;
	save_config();
}

/** \brief Saves the current (ib_conf_t) configurations. */
static void save_config() {
	nvs_handle handler;
	esp_err_t ret;
	ret = nvs_open(READER_NSPACE_NVS, NVS_READWRITE, &handler);
	if ( ret != ESP_OK ) {
		ESP_ERROR_CHECK(ret);
		return;
	}
	ret = nvs_set_blob(handler, READER_KEY_NVS, &g_config, sizeof(g_config));
	if ( ret != ESP_OK ) {
		ESP_ERROR_CHECK(ret);
		nvs_close(handler);
		return;
	}
	ret = nvs_commit(handler);
	if ( ret != ESP_OK ) {
		ESP_ERROR_CHECK(ret);
	}
	nvs_close(handler);
	refresh_config();
}

/** \brief Change the device name. */
void ib_set_device_name(const char *name){
	strncpy(g_config.devicename, name, 127);
	save_config();
}
/** \brief Change superuser key code. */
void ib_set_su_key(uint64_t code){
	g_config.su_key = code;
	save_config();
}
/** \brief Opening time. */
void ib_set_opening_time(uint32_t ms){
	g_config.openingtime = ms;
	save_config();
}
/** \brief Change operational mode. */
void ib_set_mode(uint32_t mode){
	if ( mode == IB_READER_MODE_NORMAL ||
		 mode == IB_READER_MODE_BISTABLE ||
		 mode == IB_READER_MODE_BISTABLE_SAME_KEY) {
		g_config.mode = mode;
		save_config();
		return;
	}
	ESP_LOGE(TAG,"Invalid mode");
}

/** \brief timeout_tim software timer callback function.
 *  Add a input_tout (timeout) to FSM's queue.
 * */
static void timeout_callback(TimerHandle_t timer){
	inputs_t input = input_tout;
	xQueueOverwrite(g_handlers.input_q,&input);
}

/** \brief Called by the reader disable timer.
 * */
static void reader_enable_callback(TimerHandle_t timer){
	g_enable_reader = pdTRUE;
}

/** \brief Check su enable pin. */	// TODO not implemented.
static int is_su_mode_enable(){
	return gpio_get_level(PIN_SU_ENABLE);
}

/** \brief Makes FSM input. (FreeRTOS timer) */
static void timeout_set(int ms){
	xTimerStop(g_handlers.timeout_tim, 0);
	xTimerChangePeriod(g_handlers.timeout_tim,
						ms / portTICK_RATE_MS,
						0);
	xTimerStart(g_handlers.timeout_tim, 0);
}

/** \brief Change the reader LED's. */
static void send_info(infos_t info){
	xQueueSend(g_handlers.info_q,&info,0);
	timeout_set(TIMEOUT_BASIC_MS);
}

/** \brief Change the FSM state.
 * 	Using MUTEX.
 *  */
static void switch_state_to( p_state_handler new_state, TickType_t wait){
	if ( xSemaphoreTake(g_handlers.st_semaphor, wait) == pdTRUE ) {
		g_handlers.fsm_prev_state = g_handlers.fsm_state;
		g_handlers.fsm_state = new_state;
		xSemaphoreGive(g_handlers.st_semaphor);
	} else {
		ESP_LOGW(TAG, "Could not change state");
	}
}

/** \brief Search key and check its cron.
 *  \return 0 key is not in the database or out of the time domains
 *  \return 1 access allow
 * */
static int key_code_lookup(uint64_t code){
	esp_err_t ret, retval;
	ib_data_t *data = NULL;
	time_t time_raw;
	struct tm time_info;
	char *type = NULL;

	time(&time_raw);
	localtime_r(&time_raw, &time_info);

	if ( ib_waiting_for_su_touch() ) {
		type = IB_LOG_LOG_FILE_FULL;
		retval = 0;
	} else {
		ret = ibd_get_by_code(code, &data);
		if( ret == IBD_FOUND ) { // @suppress("Assignment in condition")
			if ( !data ) {
				ESP_LOGE(__func__, "Object ptr null");
				return 0;
			}
			if ( checkcrons(data->crons, &time_info) ) {
				type = IB_LOG_KEY_ACCESS_GAINED;
				ESP_LOGI(TAG, "Key gained access");
				retval = 1;
			} else {
				type = IB_LOG_KEY_OUT_OF_DOMAIN;
				ESP_LOGW(TAG, "Key out of time-domain");
				retval = 0;
			}
		}
		else if(ret == IBD_ERR_NOT_FOUND) {
			type = IB_LOG_KEY_INVALID_KEY_TOUCH;
			ESP_LOGW(__func__,"Key not found!");
			retval = 0;
		}
		else {
			ESP_LOGW(__func__,"ibd_get_by_code errcode:%x",ret);
			return 0;
		}
	}
	ib_log_t msg = { .log_type = type, .value = code};
	ib_log_post(&msg);
	return retval;
}

/** \brief Called when a key has been touched.
 *
 *  It looks up the key in the database
 * and makes a decision which input must be generated.
 * Put an input related to the return of key_code_lookup function.
 * */
static void key_touched_event(uint64_t code){
	inputs_t input;

	if (key_code_lookup(code)) {
		g_accessed_key = code;
		input = input_touched;
	}
	else if (code == g_config.su_key){
		input = input_su_touched;
	} else {
		input = input_invalid_touched;
	}
	xQueueSend(g_handlers.input_q,&input,0);
}


/** \brief Task function of iButton reader module.
 *
 *  Checks the button state then reads the iButton reader.
 *  It ensures that the touched iButton will generate an input which type
 * depends on the key whether has a right to access or not.
 *  After a successful key reading, the reader will be disabled for a defined time
 * and that time will be recalculated while the key is still connected.
 * To enable the reader again, the key has to disconnect for the defined time.
 * */
static void ib_reader_task(void *pvParam){

	g_handlers.fsm_state = st_check_touch;
	LED_RED(ON);
	LED_GREEN(OFF);

	TimerHandle_t reader_tim = xTimerCreate("reader timer",
			READER_DISABLE_TICKS, pdFALSE, 0, reader_enable_callback);
	g_handlers.timeout_tim = xTimerCreate("timeout alarm",
				30000, pdFALSE, 0, timeout_callback);

	uint64_t ibutton_code;
	inputs_t input_incoming;
	int button_prev_state = 0;

	vTaskDelay(100 / portTICK_PERIOD_MS); 		// Button capacitance!
	while(1){
		if( !gpio_get_level(PIN_BUTTON)){
			if(!button_prev_state){
				g_handlers.fsm_state(input_button);
				ESP_LOGD(TAG,"Button pressed");
			}
			button_prev_state = 1;
		}
		else if(ib_presence()){
			if(pdTRUE == g_enable_reader){
				switch (ib_read_code(&ibutton_code)) {
					case IB_OK:
						ESP_LOGD(TAG,"READ: Code: %llu",ibutton_code);
						if(pdPASS == xTimerReset(reader_tim,0))
							g_enable_reader = pdFALSE;
							key_touched_event(ibutton_code);
						break;
					case IB_FAM_ERR:
						ESP_LOGD(TAG,"Family code");

						break;
					case IB_CRC_ERR:
						ESP_LOGD(TAG,"Invalid crc");
						break;
					default:
						break;
				}
			}
			if(pdFAIL == xTimerReset(reader_tim,0))
				g_enable_reader = pdTRUE;
		}
		else
			button_prev_state = 0;

		if(pdTRUE == xQueueReceive(g_handlers.input_q, &input_incoming, 10 / portTICK_RATE_MS) )
			g_handlers.fsm_state(input_incoming);
	}
}
/** \brief Outputs information for users.
 * Blink or change the lighting LEDs on the reader.
 * The task waits in the blocked state until incoming a command to change information output.
 *  */
static void ib_info_task(void *pvParam){
	infos_t info_state = blink_none;
	int delay_ms = 500;
	gpio_mode_t led_out_state = GPIO_MODE_OUTPUT;
	while(1){
		if(blink_none == info_state){
			if(pdPASS == xQueueReceive(g_handlers.info_q,&info_state,portMAX_DELAY)){
				led_out_state = GPIO_MODE_OUTPUT;
				ESP_LOGD(TAG,"infoqueue got");
			}
		}
		else{
			if(pdPASS == xQueueReceive(g_handlers.info_q,&info_state,delay_ms / portTICK_RATE_MS)){
				led_out_state = GPIO_MODE_OUTPUT;
				ESP_LOGD(TAG,"infoqueue got during blinking");
			}
			ESP_LOGD(TAG,"Time to blink it");
		}
		switch(info_state) {
			case blink_both:
				LED_RED(led_out_state);
				LED_GREEN(led_out_state);
				delay_ms = 500;
				break;
			case blink_green:
				LED_GREEN(led_out_state);
				delay_ms = 500;
				break;
			case blink_red:
				LED_RED(led_out_state);
				delay_ms = 500;
				break;
			case blink_both2:
				LED_GREEN(led_out_state);
				if(led_out_state == ON){
					LED_RED(OFF);
				}
				else{
					LED_RED(ON);
				}
				delay_ms = 250;
				break;
			default:
				break;
		}
		led_out_state = (led_out_state == ON) ? OFF : ON;
	}
}

/** \brief Creates two tasks. */
static void create_tasks(){

	g_handlers.info_q = xQueueCreate(INFO_QUEUE_LENGTH,INFO_QUEUE_ITEM_SIZE);
	if(g_handlers.info_q == 0)
		ESP_LOGE(__func__,"info_q queue create err");

	g_handlers.input_q = xQueueCreate(INPUT_QUEUE_LENGTH,INPUT_QUEUE_ITEM_SIZE);
		if(g_handlers.info_q == 0)
			ESP_LOGE(__func__,"input_q queue create err");

	const char task_name[] = "ibutton reader task";
	if(xTaskCreate(ib_reader_task, task_name, 4096,
			0, 7, &g_handlers.reader_t) != pdPASS){
		ESP_LOGE(__func__,"'%s' cannot be created",task_name);
	}
	const char task2_name[] = "ibutton info task";
	if(xTaskCreate(ib_info_task, task2_name, 4096,
			0, 7, &g_handlers.info_t) != pdPASS){
		ESP_LOGE(__func__,"'%s' cannot be created",task2_name);
	}
}

static void gpio_set(){
	gpio_pad_select_gpio(PIN_BUTTON);
	gpio_set_level(PIN_BUTTON, 1);
	gpio_set_direction(PIN_BUTTON, GPIO_MODE_INPUT);
	gpio_set_pull_mode(PIN_BUTTON, GPIO_PULLUP_ONLY);

	gpio_pad_select_gpio(PIN_RELAY);
	gpio_set_direction(PIN_RELAY, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_RELAY, 0);

	gpio_pad_select_gpio(PIN_GREEN);
	gpio_set_direction(PIN_GREEN, GPIO_MODE_INPUT);
	gpio_set_level(PIN_GREEN, 0);

	gpio_pad_select_gpio(PIN_RED);
	gpio_set_direction(PIN_RED, GPIO_MODE_INPUT);
	gpio_set_level(PIN_RED, 0);

	gpio_pad_select_gpio(PIN_SU_ENABLE);
	gpio_set_level(PIN_SU_ENABLE, 1);
	gpio_set_direction(PIN_SU_ENABLE, GPIO_MODE_INPUT);
	gpio_set_pull_mode(PIN_SU_ENABLE, GPIO_PULLUP_ONLY);
}

/** \brief Returns the current device name. */
char *ib_get_device_name() {
	return g_config.devicename;
}

/** \brief Starts an iButton reader module.
 * */
void start_ib_reader(){
	if(initialized){
		ESP_LOGW(__func__, "Already initialized");
		return;
	}

	gpio_set();
	refresh_config();
	onewire_init(PIN_DATA);
	create_tasks();
	g_handlers.st_semaphor = xSemaphoreCreateMutex();
	initialized = 1;
}

/** \brief Is FSM in the error state? */
int ib_waiting_for_su_touch() {
	return (g_handlers.fsm_state == st_wait_for_clear_log) ? 1 : 0;
}

/** \brief Wait for superuser touch intervention.
 *	Bring the device into a waiting state. Used when logfile is full.
 * */
void ib_need_su_touch() {
	RELAY_CLOSE;
	LED_GREEN(ON);
	LED_RED(ON);
	send_info(blink_both);
	switch_state_to(st_wait_for_clear_log, portMAX_DELAY);
}

/** \brief Go back to normal operation. */
void ib_not_need_su_touch() {
	LED_GREEN(OFF);
	LED_RED(ON);
	switch_state_to(st_check_touch, portMAX_DELAY);
}
/** ___________________________________________________________________________________________________  */
/** @ingroup state_functions
 *  This is a special state, when the iButton reader is turned down: All access denied.
 *  Used in case of fatal error.
 * */
static void st_wait_for_clear_log(inputs_t input) {
	switch (input) {
		case input_su_touched:
			LED_GREEN(OFF);
			LED_RED(ON);
			switch_state_to(st_check_touch, MUT_WAIT);
			ibd_log_delete();
			break;
		case input_button:
			RELAY_OPEN;
			timeout_set(g_config.openingtime);
			break;
		case input_tout:
			RELAY_CLOSE;
			break;
		default:
			break;
	}
}

/** @ingroup state_functions
 *  Open relay, access gained.
 * */
static void st_access_allow(inputs_t input) {
	switch (input) {
		case input_tout:
			LED_GREEN(OFF);
			LED_RED(ON);
			RELAY_CLOSE;
			infos_t info = blink_none;
			send_info(info);
			switch_state_to(g_handlers.fsm_prev_state, MUT_WAIT);
			break;
		default:
			break;
	}
}

/** @ingroup state_functions
 *  Access allow in bistable mode.
 *  Any next key touch closes the relay.
 * */
static void st_acces_allow_bistable(inputs_t input) {
	switch(input) {
		case input_touched:
			LED_GREEN(OFF);
			LED_RED(ON);
			RELAY_CLOSE;
			infos_t info = blink_none;
			send_info(info);
			switch_state_to(g_handlers.fsm_prev_state, MUT_WAIT);
			break;
		default:
			break;
	}
}

/** @ingroup state_functions
 *  Access allow in bistable mode.
 *  Only that key will close the relay which gained access.
 * */
static void st_acces_allow_bistable_same_key(inputs_t input) {
	switch(input) {
		case input_touched:
			if ( g_accessed_key == g_accessed_key_prev ) {
				LED_GREEN(OFF);
				LED_RED(ON);
				RELAY_CLOSE;
				infos_t info = blink_none;
				send_info(info);
				switch_state_to(g_handlers.fsm_prev_state, MUT_WAIT);
			}
			break;
		default:
			break;
	}
}

/** @ingroup state_functions
 * Super user mode.
 * */
static void st_su_mode(inputs_t input) {
	switch(input) {
		case input_tout:
			send_info(blink_none);
			switch_state_to(st_check_touch, MUT_WAIT);
			break;
		default:
			break;
	}
}

/** @ingroup state_functions
 *  This state is the standard one.
 * */
static void st_check_touch(inputs_t input) {
	switch(input) {
		case input_su_touched:
			if(is_su_mode_enable()){
				send_info(blink_green);
				switch_state_to(st_su_mode, MUT_WAIT);
				ESP_LOGD(TAG,"Touched su");
				break;
			}
			else{
				input = input_touched;
				// OPEN IT LIKE A NORMAL KEY
			}
			/* no break */
		case input_touched:
			LED_RED(OFF);
			LED_GREEN(ON);
			RELAY_OPEN;
			switch (g_config.mode) {
				case IB_READER_MODE_BISTABLE:
					switch_state_to(st_acces_allow_bistable, MUT_WAIT);
					break;
				case IB_READER_MODE_BISTABLE_SAME_KEY:
					g_accessed_key_prev = g_accessed_key;
					switch_state_to(st_acces_allow_bistable_same_key, MUT_WAIT);
					break;
				default:
					timeout_set(g_config.openingtime);
					switch_state_to(st_access_allow, MUT_WAIT);
					break;
			}
			ESP_LOGD(TAG,"Touched");
			break;
		case input_invalid_touched:
			LED_RED(OFF);
			LED_GREEN(OFF);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			LED_RED(ON);
			LED_GREEN(OFF);
			ESP_LOGD(TAG,"Invalid touched");
			break;
		case input_button:
			if ( g_config.mode == IB_READER_MODE_NORMAL ) {
				LED_RED(OFF);
				LED_GREEN(ON);
				RELAY_OPEN;
				timeout_set(g_config.openingtime);
				switch_state_to(st_access_allow, MUT_WAIT);
			}
			break;
		default:
			break;
	}

}
	/** @} */
/** @} */


























