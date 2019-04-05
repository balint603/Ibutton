/*
 * ib_csv.c
 *
 *  Created on: Mar 29, 2019
 *      Author: root
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "ib_database.h"

//#define TEST_MODE

#define CODE_FAMILY_MASK 	0xFF00000000000000
#define CODE_FAMILY 	 	0x0100000000000000
#define CRON_MINIMUM_LEN 	9
#define CRON_MAXIMUM_SIZE	(IBD_CRON_MAX_SIZE)




/** \brief String check algorithm.
 *   - Remove the ' ' characters at the beginning of a cron.
 *   - Write only a '\0' in crons when from is NULL.
 *  \param crons output
 *  \param bytes of crons
 *  \param from containing raw crons
 *  \ret 0 if String copied
 *  \ret -1 crons NULL
 * */
static int copy_cron(char *crons, int size, char *from){
	if ( !(crons) )
		return -1;
	if ( !size )
		return 0;
	if ( !from ) {
		*crons = '\0';
		return 0;
	}
	char *cur_ptr = from;
	while ( *cur_ptr == ' ' )
		cur_ptr++;
	if ( *cur_ptr == '\0' ) {
		*crons = '\0';
		return 0;
	}
	size--;
	while ( size ) {							// Need place for '\0'
		if ( *(cur_ptr) == CRON_DELIMITER ) {
			*crons = *cur_ptr;
			cur_ptr++;
			while ( *cur_ptr == ' ' )
				cur_ptr++;
		} else {
			*crons = *cur_ptr;
			cur_ptr++;
		}
		crons++;
		size--;
		if( *cur_ptr == '\0')
			break;
	}
	*crons = '\0';
	return 0;
}

/** \brief Convert a string line to ib_data_t object.
 *	Allocated memory!
 *	\ret NULL if the ib_data_t object cannot be created from the line
 *	\ret Pointer to a created ib_data_t object
 * */
ib_data_t *csv_process_line(char *line){
	ib_data_t *ib_d;
	uint64_t code_temp;
	uint8_t cron_len_temp;
	char cron_temp[CRON_MAXIMUM_SIZE];
	int fields = IBD_CSV_FIELDS;
	char *num_str; //CSV
	char *cron_str; //CSV

	if ( !line )
		return NULL;

	num_str = strtok(line, DELIMITER);
	fields--;
	if ( !num_str )
		return NULL;
#ifdef TEST_MODE
	ESP_LOGD(__func__,"num_str:[%s]",num_str);
#endif
	do {
		cron_str = strtok(NULL, DELIMITER);
#ifdef TEST_MODE
	if( cron_str )
		ESP_LOGD(__func__,"cron_str:[%s]",cron_str);
#endif
	} while ( cron_str && --fields );

	code_temp = strtoll(num_str, NULL, 16);

	if ( ! ((code_temp & CODE_FAMILY_MASK) == CODE_FAMILY) )	// TODO CHECK THIS BUGce
		return NULL;
#ifdef TEST_MODE
	ESP_LOGD(__func__,"num_val:[%lld]",code_temp);
#endif
	if ( copy_cron(cron_temp, CRON_MAXIMUM_SIZE, cron_str) ) {
		;
	}
	cron_len_temp = strlen(cron_temp);
    if ( cron_len_temp >= CRON_MINIMUM_LEN ) {
    	ib_d = create_ib_data(code_temp, cron_temp);
    }
    else {
    	ib_d = create_ib_data(code_temp, NULL);
    }
	return ib_d;
}

/** \brief Copy the content of the next line.
 *  \param line buffer
 *  \param size maximum size to be read in a line
 *  \param from where the lines read from
 *  \ret   0 wrong param or size = 0, or **from is '\0'
 *  \ret   n of bytes are read
 * */
uint32_t csv_eat_a_line(char *line, int size, char **from){
	int index = 0;
	if ( !( line && from && *from ) )
		return index;
	if ( !size )
		return index;
	size--; 													// '\0' need place too!
	while ( size &&  '\n' != (**from)  &&  (**from) != '\r' ) {
		if ((**from) == '\0') {
			*(line + index) = '\0';
			return index + 1;									// Ret without incrementing from ptr
		}
		*(line + index++) = *(*from)++;
		size--;
	}
	(*from) = ((*from)+1);
	*(line + index) = '\0';
	return index + 1;
}

#ifdef TEST_MODE
void test_process_line(){
	ESP_LOGI(__func__,"START");
	ib_data_t *data;
	char line[] = "01300EBC1A0000D0|* * * * *";
	if( (data = csv_process_line(line)) == NULL ) {
		ESP_LOGE(__func__,"NULL");
		return;
	}
	printf("Return of csv_process_line:\n code[%lld]\n mems[%i]\n crons[%s]\n",data->code_s.code, data->code_s.mem_d_size, data->crons);
	free(data);
	ESP_LOGI(__func__,"END");
}
#endif
