/*
 * ib_reader.c
 *
 *  Created on: Feb 26, 2019
 *      Author: root
 *
 *  This module implements the access control functions.
 *  It is state a machine which has more inputs:
 *  	- iButton reader,
 *  	- button,
 *  	- timeouts.
 *  Two tasks:
 *  	1.: reads the button state, and read the iButton data.
 *  	2.: informs the user of the state machine, example: blink leds
 * 	Uses
 * 		- software timers to create timeouts,
 * 		- queues to perform communications between the input generators and the state machine,
 */

#include "ib_reader.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "time.h"

#include "ibutton.h"
#include "cron.h"
#include "/home/major/Documents/ESP32/Workbench/Test_ib_database/main/ib_database.h"

#define TAG "IB_READER"

/** Input types of the state machine */
typedef enum inputs {
    input_touched,
    input_su_touched,
	input_invalid_touched,
    input_button,
	input_tout,
} inputs_t;

/** Type of indicating the state of the machine */
typedef enum infos {
	blink_green,
	blink_red,
	blink_both,
	blink_both2,
	blink_none,
} infos_t;

/** Configuration settings */
typedef struct ib_conf{
	unsigned long su_key;
	unsigned int opening_time;
	unsigned int mode;
	unsigned int button_enable;
} ib_conf_t;

/** Operation mode types */
#define MODE_NORMAL 0
#define MODE_BISTABLE 255
#define MODE_BISTABLE_SAME_KEY 511

/** Holds the current state */
typedef void ( *p_state_handler )(inputs_t input);

/** FreeRTOS handlers */
typedef struct ib_handles{
	p_state_handler fsm_state;
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

/** Basic time in ms */
#define TIMEOUT_BASIC_MS 30000

/** called: state functions.
 *  This functions performs several operations,
 *  they are the states of the machine.
 *  */
static void check_touch(inputs_t input);



volatile uint32_t initialized;

ib_conf_t g_data = {
		.opening_time = STANDARD_OPENING_TIME
};
ib_handlers g_handlers;

volatile BaseType_t g_enable_reader = pdTRUE;



/**
 * timeout_tim software timer callback function.
 * */
static void timeout_callback(TimerHandle_t timer){
	inputs_t input = input_tout;
	xQueueOverwrite(g_handlers.input_q,&input);
}

/**
 * Called by the reader disable timer.
 * */
static void reader_enable_callback(TimerHandle_t timer){
	g_enable_reader = pdTRUE;
}

static int is_su_mode_enable(){
	return gpio_get_level(PIN_SU_ENABLE);
}

/** \brief Search key and check cron.
 *  \ret 0 key is not in the database or out of the time domains
 *  \ret 1 access allow
 *
 * */
int key_code_lookup(uint64_t code){
	esp_err_t ret;
	ib_data_t *data = NULL;
	time_t time_raw;
	struct tm time_info;

	ret = ibd_get_by_code(code, &data);
	if(ret == IBD_FOUND){
		if ( !data ) {
			ESP_LOGE(__func__, "Object ptr null");
			return 0;
		}
		time(&time_raw);
		localtime_r(&time_raw, &time_info);
		ret = checkcrons(data->crons, &time_info, sizeof(data->crons));
		if ( ret ) {
			ESP_LOGI(TAG, "Key gained access");
		} else {
			ESP_LOGW(TAG, "Key out of time-domain");
		}
		return ret;
	}
	else if(ret == IBD_ERR_NOT_FOUND)
		{ESP_LOGW(__func__,"Key not found!");}
	else
		{ESP_LOGW(__func__,"ibd_get_by_code errcode:%x",ret);}
	return 0;
}

/**
 * \brief Called when a key has been touched.
 *  It looks up the key in the database
 * and makes a decision which input must be generated.
 * */
static void key_touched_event(uint64_t code){
	inputs_t input;
// todo search from memory and make decision
	if(key_code_lookup(code))
		input = input_touched;
	else
		input = input_invalid_touched;
	xQueueSend(g_handlers.input_q,&input,0);
}


/** \brief Task function of iButton reader module.
 *  Checks the button state then reads the iButton reader.
 *  It ensures that the touched iButton will generate an input which type
 * depends on the key whether has a right to access or not.
 *  After a successful key reading, the reader will be disabled for a defined time
 * and that time will be recalculated while the key is still connected.
 * To enable the reader again, the key has to disconnect for the defined time.
 * */
TaskFunction_t ib_reader_task(void *pvParam){

	g_handlers.fsm_state = check_touch;

	TimerHandle_t reader_tim = xTimerCreate("reader timer",
			READER_DISABLE_TICKS, pdFALSE, 0, reader_enable_callback);
	g_handlers.timeout_tim = xTimerCreate("timeout alarm",
				30000, pdFALSE, 0, timeout_callback);


	uint64_t ibutton_code;
	inputs_t input_incoming;
	int button_prev_state = 0;

	LED_RED(OFF);

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
 * The task waits in the blocked state until incomes a need to change information output.
 *  */
TaskFunction_t ib_info_task(void *pvParam){
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

static void create_tasks(){

	g_handlers.info_q = xQueueCreate(INFO_QUEUE_LENGTH,INFO_QUEUE_ITEM_SIZE);
	if(g_handlers.info_q == 0)
		ESP_LOGE(__func__,"info_q queue create err");

	g_handlers.input_q = xQueueCreate(INPUT_QUEUE_LENGTH,INPUT_QUEUE_ITEM_SIZE);
		if(g_handlers.info_q == 0)
			ESP_LOGE(__func__,"input_q queue create err");

	const char task_name[] = "ibutton reader task";
	if(xTaskCreate(ib_reader_task, task_name, 4096,
			0, 5, &g_handlers.reader_t) != pdPASS){
		ESP_LOGE(__func__,"'%s' cannot be created",task_name);
	}
	const char task2_name[] = "ibutton info task";
	if(xTaskCreate(ib_info_task, task2_name, 4096,
			0, 5, &g_handlers.info_t) != pdPASS){
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

/** \brief Starts an iButton reader module.
 *
 * */
void start_ib_reader(){
	if(initialized){
		ESP_LOGW(__func__, "Already initialized");
		return;
	}

	gpio_set();
	onewire_init(PIN_DATA);
	create_tasks();
	initialized = 1;
}

/** Helper functions of State functions */

static void timeout_set(int ms){
	xTimerStop(g_handlers.timeout_tim, 0);
	xTimerChangePeriod(g_handlers.timeout_tim,
						ms / portTICK_RATE_MS,
						0);
	xTimerStart(g_handlers.timeout_tim, 0);
}

/** STATE FUNCTIONS _________________________________________________________________________*/

static void switch_state_to( p_state_handler new_state ){
	g_handlers.fsm_state = new_state;
}

static void send_info(infos_t info){
	xQueueSend(g_handlers.info_q,&info,0);
	timeout_set(TIMEOUT_BASIC_MS);
}

static void access_allow(inputs_t input){
	switch (input) {
		case input_tout:
			LED_GREEN(ON);
			RELAY_CLOSE;
			infos_t info = blink_none;
			send_info(info);
			g_handlers.fsm_state = check_touch;
			break;
		default:
			break;
	}
}

static void su_mode(inputs_t input){
	switch(input){
		case input_tout:
			send_info(blink_none);
			switch_state_to(check_touch);
			break;
		default:
			break;
	}
}

static void check_touch(inputs_t input){
	switch(input){
		case input_su_touched:
			if(is_su_mode_enable()){
				send_info(blink_green);
				switch_state_to(su_mode);
				ESP_LOGD(TAG,"Touched su");
				break;
			}
			else{
				input = input_touched;
				// OPEN IT LIKE A NORMAL KEY
			}
			/* no break */
		case input_touched:
			LED_GREEN(OFF);
			RELAY_OPEN;
			switch (g_data.mode) {
				case MODE_BISTABLE:
					// todo
					break;
				case MODE_BISTABLE_SAME_KEY:
					// todo
					break;
				default:
					timeout_set(g_data.opening_time);
					g_handlers.fsm_state = access_allow;
					break;
			}
			ESP_LOGD(TAG,"Touched\n");
			break;
		case input_button:
			LED_GREEN(OFF);
			RELAY_OPEN;
			timeout_set(g_data.opening_time);
			g_handlers.fsm_state = access_allow;
			break;
		default:
			break;
	}

}
































