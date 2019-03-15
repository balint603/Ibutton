/*
 * codeflash.c
 *
 *  Created on: Mar 10, 2019
 *      Author: root
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_partition.h"
#include "rom/spi_flash.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "codeflash.h"

#ifdef TEST_COMMANDS
#include "ib_reader.h"
#endif

#define MAX_VALUE(a) (((unsigned long long)1 << (sizeof(a) * CHAR_BIT)) - 1)
#define SIZE_OF_CRON_LENGTH_AND_CODE 9

static SemaphoreHandle_t g_semaphore_handle;

/** Based on partition.csv file! */
static const esp_partition_type_t PARTITION_TYPE = 0x40;
static const char PARTITION_A_NAME[] = "A_codefl";
static const char PARTITION_B_NAME[] = "B_codefl";

static const char NVS_ACTIVE_P_KEY[] = "cf_k";
static const char FLASH_NAMESPACE[] = "cf_ns";

/** Max address value of partition. */
static size_t OFFSET_END_PARTITION;

/** Static heap pool for a cron string, used by codeflash_get_by_code(). */
static char g_cron_data_storage[256];

/** Informations about partitions. */
static struct Flash_states{
	esp_partition_t *active_partition;
	esp_partition_t *inactive_partition;
	size_t active_first_free_offset;
	size_t inactive_first_free_offset;
} g_partition_state;


/** Get the first free address of a partition.
 *  Increment addr until 0xFF found in place of addr.
 * */
static size_t get_first_free_offset(esp_partition_t partition){
	size_t addr = 0;
	uint8_t cron_length;
	esp_err_t ret;

	while(addr < OFFSET_END_PARTITION){
		// READ data from the current address into value.
		ret = esp_partition_read(&partition, addr, &cron_length, sizeof(uint8_t));
		if(ESP_OK != ret){
			ESP_LOGE(__func__,"Partition read err code: '%i'",ret);
			return 1;
		}

		if(0xFF == cron_length)
			return addr;
		addr += (cron_length + 9);		//Get the next data.
	}
	return 1;
}
/** \brief Print out the free space of the selected partition.
 *
 * */
void show_partition_use(esp_partition_t partition){
	char out_str[] = "[                                ]";
	size_t used_space = get_first_free_offset(partition);
	if(used_space == 1){
		ESP_LOGE(__func__,"Get first free");
		return;
	}

	int i = 1;
	int cnt = used_space;
	cnt >>= 14;
	while(cnt-- > 0){
		out_str[i++] = '=';
	}

	printf("Used code flash space:\n	%s, in %iB\n"
			,out_str,(unsigned int)used_space);
}


/** \brief Change the partitions state and ERASE the new inactive partition.
 *
 * */
static void change_partitions(){
	nvs_handle handler;
	esp_partition_t *temp_p;
	size_t temp;
	esp_err_t ret;

	temp_p = g_partition_state.active_partition;
	g_partition_state.active_partition = g_partition_state.inactive_partition;
	g_partition_state.inactive_partition = temp_p;

	temp = g_partition_state.active_first_free_offset;
	g_partition_state.active_first_free_offset = g_partition_state.inactive_first_free_offset;
	g_partition_state.inactive_first_free_offset = temp;

	ESP_ERROR_CHECK(esp_partition_erase_range(g_partition_state.inactive_partition, 0,
			g_partition_state.inactive_partition->size));
	g_partition_state.inactive_first_free_offset = 0;

	ret = nvs_open(FLASH_NAMESPACE, NVS_READWRITE, &handler);
	if(ESP_OK != ret){
		ESP_LOGE(__func__,"NVS open '%i'",ret);
		return;
	}
	nvs_set_str(handler, NVS_ACTIVE_P_KEY, g_partition_state.active_partition->label);
	if(ESP_OK != ret){
		ESP_LOGE(__func__,"NVS set blob '%i'",ret);
		return;
	}
	nvs_commit(handler);
	nvs_close(handler);
}

/** \brief erasing both partitions.
 *
 * */
void codeflash_erase_both(){
	printf("Erase both partition... size: %i\n",g_partition_state.active_partition->size);
	esp_err_t ret;
	ret = esp_partition_erase_range(g_partition_state.active_partition,
			0,
			g_partition_state.active_partition->size);
	if(ESP_OK != ret){
		ESP_LOGE(__func__,"err code:'%i'",ret);
		return;
	}
	ret = esp_partition_erase_range(g_partition_state.inactive_partition,
			0,
			g_partition_state.inactive_partition->size);
	if(ESP_OK != ret){
		ESP_LOGE(__func__,"err code:'%i'",ret);
		return;
	}
	ESP_LOGI(__func__, "Partitions are erased");
}

/** \brief Check the data and correct cron_length fields if necessary.
 *  \param data it must be a codeflash_t based database
 *  \param length size of the database
 *  \ret ESP_OK when no error found or all errors were corrected
 *  \ret ???
 * */
esp_err_t codeflash_check_data(void *data, size_t length){
return ESP_OK;
}

/** \brief Initialize - get the data from the NVS.
 *  Save g_flash_state structure if not saved yet.
 *  \ret ESP_OK initialized
 *  \ret any nvs error codes, when an nvs function failed
 *    - while reading saved active-partition information.
 *  \ret ESP_ERR_NOT_FOUND when the partitions cannot be find
 *    - flash partition driver error
 * */
esp_err_t codeflash_init(){
	esp_err_t err;
	nvs_handle nvs_handler;
	char partition_label[17];			// Partition name / label
	const size_t size_of_nvs_data = (size_t)strlen(PARTITION_A_NAME) + 1;
	size_t size = size_of_nvs_data;
	uint8_t first_start = 0;

	g_semaphore_handle = xSemaphoreCreateMutex();
	if(g_semaphore_handle == NULL)
		ESP_LOGW(__func__,"Semaphore create NULL");

	err = nvs_open(FLASH_NAMESPACE, NVS_READWRITE, &nvs_handler);
	if(ESP_ERR_NVS_NOT_FOUND == err){
		ESP_LOGI(__func__,"First module init.");
		first_start = 1;
	}else if(ESP_OK != err){
		ESP_LOGE(__func__,"nvs_open error code: '%i'",err);
		return err;
	}
	// Get last active partition
	err = nvs_get_str(nvs_handler, NVS_ACTIVE_P_KEY, partition_label, &size);
	if(ESP_ERR_NVS_NOT_FOUND == err){
		ESP_LOGI(__func__,"No entry found.");
		first_start = 1;
	}else if(ESP_OK != err){
		ESP_LOGE(__func__,"nvs get str error code: '%i",err);
		return err;
	}

	esp_partition_iterator_t part_iterator_A;
	esp_partition_iterator_t part_iterator_B;
	esp_partition_t *partition_A = NULL;
	esp_partition_t *partition_B = NULL;
	part_iterator_A = esp_partition_find( PARTITION_TYPE,		// Partition A
			ESP_PARTITION_SUBTYPE_ANY,
					PARTITION_A_NAME);
	part_iterator_B = esp_partition_find( PARTITION_TYPE,		// Partition B
			ESP_PARTITION_SUBTYPE_ANY,
					PARTITION_B_NAME);
	if( !(part_iterator_A && part_iterator_B) ){
		ESP_LOGE(__func__,"Codeflash partition not found.");
		return ESP_ERR_NOT_FOUND;
	}
	partition_A = (esp_partition_t*)esp_partition_get(part_iterator_A);
	partition_B = (esp_partition_t*)esp_partition_get(part_iterator_B);

	if(first_start){
		g_partition_state.active_partition = partition_A;
		g_partition_state.inactive_partition = partition_B;
		codeflash_erase_both();

		err = nvs_set_str(nvs_handler, NVS_ACTIVE_P_KEY,
				g_partition_state.active_partition->label);
		if(ESP_OK != err){
			ESP_LOGE(__func__,"nvs set str err code: '%i'",err);
		}
	}else{			// NVS info found
		if(partition_label[0] == 'A'){
			g_partition_state.active_partition = partition_A;
			g_partition_state.inactive_partition = partition_B;
		}
		else{
			g_partition_state.active_partition = partition_B;
			g_partition_state.inactive_partition = partition_A;
			if(partition_label[0] != 'B'){
				ESP_LOGE(__func__,"Partition name in NVS corrupted: ['%s']",partition_label);
				// todo error handling: is it good to choose a random active partition?
			}
		}
	}
	// search the last item
	OFFSET_END_PARTITION = (size_t)g_partition_state.active_partition->size - 1;
	g_partition_state.active_first_free_offset = get_first_free_offset(*g_partition_state.active_partition);
	show_partition_use(*g_partition_state.active_partition);

	nvs_commit(nvs_handler);
	nvs_close(nvs_handler);
	return ESP_OK;
}

/** \brief Writes raw data into the flash memory.
 * Does NOT check the data.
 *  \param data to be written
 *  \param length length of the data
 *  \param into_inactive 0: append data into active, else into inactive partition
 *  \ret ESP_ERR_INVALID_ARG data is null
 *  \ret ESP_ERR_NO_MEM when the available space is not enough
 *  \ret ESP_OK
 *  \ret any error codes by esp_partition_write()
 * */
esp_err_t codeflash_append_raw_data(void *data, size_t length, int into_inactive){
	esp_err_t ret;
	size_t offset;
	esp_partition_t *partition;
	size_t first_free_offset;

#ifdef TEST_COMMANDS
	printf("Append param: <%i>\n",length);
#endif

	if(!data)
		return ESP_ERR_INVALID_ARG;
	if(!length)
		return ESP_OK;
	if(into_inactive){
		offset = g_partition_state.inactive_first_free_offset;
		partition = g_partition_state.inactive_partition;
		first_free_offset = g_partition_state.inactive_first_free_offset;
	}
	else{
		offset = g_partition_state.active_first_free_offset;
		partition = g_partition_state.active_partition;
		first_free_offset = g_partition_state.active_first_free_offset;
	}
	if( (partition->size - first_free_offset) < length )			// Check available space
		return ESP_ERR_NO_MEM;
	xSemaphoreTake(g_semaphore_handle,portMAX_DELAY);
	ret = esp_partition_write(partition, offset, data, length);
	xSemaphoreGive(g_semaphore_handle);
	if(ESP_OK == ret){
		if(into_inactive)
			g_partition_state.inactive_first_free_offset += length;
		else
			g_partition_state.active_first_free_offset += length;
	}else
		return ret;
	return ret;
}

/** \brief Search codeflash_t data by iButton serial number (code).
 * 	Cron string is stored in a static char array.
 *  \param code search by
 *  \param output data
 *  \ret ESP_INVALID_ARG
 *  \ret ESP_NOT_FOUND when code parameter cannot be found
 *  \ret ESP_OK when the data has been found and copied to parameter data_f
 * */
esp_err_t codeflash_get_by_code(unsigned long code, codeflash_t *data_f){
	esp_err_t ret;
	size_t addr;
	if(!data_f || code == MAX_VALUE(unsigned long))
		return ESP_ERR_INVALID_ARG;

	addr = 0;
	while(addr < g_partition_state.active_first_free_offset){
		ret = esp_partition_read(g_partition_state.active_partition,		// Read next entry
				addr, data_f, SIZE_OF_CRON_LENGTH_AND_CODE);
		if(ESP_OK != ret)
			return ret;
		if(code == data_f->code){		// Found?
			addr += SIZE_OF_CRON_LENGTH_AND_CODE; // read cron string into the global variable
			esp_partition_read(g_partition_state.active_partition, addr,
					g_cron_data_storage, data_f->crons_length);

			data_f->crons = g_cron_data_storage;

			return ESP_OK;
		}
		addr += (SIZE_OF_CRON_LENGTH_AND_CODE + data_f->crons_length);
	}	// End of the list?
	return ESP_ERR_NOT_FOUND;
}
/************************************************************************************************************/
#ifdef TEST_COMMANDS

void long_to_str(unsigned long l, char *data){
	for(int i = 0; i < 8; i++){
		*(data+i) = (unsigned char) l;
		l >>= 8;
	}
}

void print_codeflash_data(size_t offset, size_t length, int from_inactive){
	uint8_t buffer[length];
	esp_partition_t *part;
	int i = length;
	if(from_inactive){
		printf("Get data from inactive partition...\n\n");
		part = g_partition_state.inactive_partition;
	}
	else{
		printf("Get data from active partition...\n\n");
		part = g_partition_state.active_partition;
	}
	printf("Data from %X:\n",offset);
	esp_partition_read(part, offset, buffer, length);
	for(int j = 0;j < i; j++){
		printf("%02X:",buffer[j]);
	}
	printf("\n");
}

void test_read(){
	char data_pr[100];
	esp_partition_read(g_partition_state.active_partition, 0, data_pr, 100);
	printf("Flash data from 0:\n");
	for(int i = 0; i < 100; i++)
		printf("%x:",data_pr[i]);
	printf("\n");
	esp_err_t ret;
	unsigned long key_code = 0;
	codeflash_t data;
    int n = 1;
	printf("Start test_read:\n");
	ret = codeflash_get_by_code(0, &data);
	if(ret != ESP_ERR_NOT_FOUND)
		printf("Err! code: %i [%i]\n",ret,n++);
	ret = codeflash_get_by_code(19, &data);
		if(ret != ESP_OK)
			printf("Err! code: %i [%i]\n",ret,n++);
		else
			printf("Found data: [%s]\n",data.crons);

	ret = codeflash_get_by_code(key_code, NULL);
	if(ret != ESP_ERR_INVALID_ARG)
		printf("Err! code: %i [%i]\n",ret,n++);
	ret = codeflash_get_by_code(MAX_VALUE(unsigned long), &data);
	if(ret != ESP_ERR_INVALID_ARG)
		printf("Err! code: %i [%i]\n",ret,n++);
	ret = codeflash_get_by_code(MAX_VALUE(unsigned long) - 1, &data);
	if(ret != ESP_ERR_NOT_FOUND)
		printf("Err! code: %i [%i]\n",ret,n++);
	printf("End test_read\n");
}

TaskFunction_t test_write_data_task(){
	printf("Run write test\n");
	esp_err_t ret;
	const uint n_of_items = 1200;
	const int data_size = n_of_items * 9 + n_of_items * 10;
	char data[data_size];
	int data_cnt;
	unsigned long code[n_of_items];
	unsigned long code_data = g_partition_state.inactive_first_free_offset;
	TickType_t tick_start, tick_end;
	uint32_t tick_diff;

	data_cnt = 0;
	for(int i = 0; i < n_of_items; i++){
		unsigned long temp_code;

		data[data_cnt++] = (unsigned char)10;			// Set cron size (always 10 always write 5 asterix

		code[i] = (code_data += 19);
		temp_code = code[i];
		for(int j = 8; j > 0; j--){
			data[data_cnt++] = (unsigned char)temp_code;
			temp_code >>= 8;
		}
		strcpy(&(data[data_cnt]),"* * * * *");		// Cron
		data_cnt += 10;
	}
	tick_start = xTaskGetTickCount();
	ret = codeflash_append_raw_data(data, data_size, 1);
	ESP_LOGI(__func__,"codeflash_append_raw_data retval:'%x'",ret);
	tick_end = xTaskGetTickCount();
	tick_diff = (uint32_t)((tick_end - tick_start) / portTICK_RATE_MS);
	ESP_LOGI(__func__,"Execute codeflash_append_raw_data with '%i' bytes, took '%i'ms",data_size,tick_diff);
	ESP_LOGI(__func__,"'%i ticks",tick_end - tick_start);
	change_partitions();
	vTaskSuspend(xTaskGetCurrentTaskHandle());
	return 0;
}

void test_codeflash_write(){
	printf("Run init test\n");
	esp_err_t ret;
	unsigned long key_code;
	codeflash_t data;
	TickType_t tick_start, tick_end;
	uint32_t tick_diff;
	TaskHandle_t task_handler;

	xTaskCreate(test_write_data_task, "test_write_task", 65546, NULL, 8, &task_handler);
	//vTaskDelay(2000 / portTICK_RATE_MS);

	key_code = g_partition_state.active_first_free_offset - 19;

	printf("Active first free offset = %i\n",g_partition_state.active_first_free_offset);
	printf("Inactive first free offset = %i\n",g_partition_state.inactive_first_free_offset);
	printf("Active name: %s\n",g_partition_state.active_partition->label);
	vTaskDelay(2000 / portTICK_RATE_MS);
	vTaskPrioritySet(xTaskGetCurrentTaskHandle(), 15);				// Measure read time
	tick_start = xTaskGetTickCount();
	ret = codeflash_get_by_code(key_code, &data);
	tick_end = xTaskGetTickCount();
	vTaskPrioritySet(xTaskGetCurrentTaskHandle(), 5);
	tick_diff = (uint32_t)((tick_end - tick_start) / portTICK_RATE_MS);
	ESP_LOGI(__func__,"codeflash_get_by_code retval:'%x'",ret);
	ESP_LOGI(__func__,"Execute codeflash_get_by_code, took '%i'ms",tick_diff);
	ESP_LOGI(__func__,"'%i ticks",tick_end - tick_start);
	test_read();

}

void test_crons(unsigned long *code, char *crons, struct tm time_esp){
	if(!(crons && code))
		return;
	esp_err_t ret;
	int ret_i;
	int i = 0;
	uint8_t length_crons = strlen(crons) + 1;
	uint8_t length_data = length_crons + 9;
	char data[300];

	data[i] = (char)length_crons;	// First byte is length of the crons
	long_to_str(*code, &data[1]);
	strcpy(&data[9],crons);
	ret = codeflash_append_raw_data(data, length_data, 0);
	if(ESP_OK != ret){
		ESP_LOGE(__func__,"append data err='%x'",ret);
	}
	print_codeflash_data(0, 100, 0);

	ret_i = key_code_lookup(*code, time_esp);
	printf("Code:[%ld], Access=%i\n",*code,ret_i);
	(*code)++;
}

void test_codeflash_init(){
	printf("Run init test\n");
	esp_err_t ret;
	unsigned long key_code;
	ret = codeflash_init();
	ESP_ERROR_CHECK(ret);
	struct tm time_esp;

	time_esp.tm_min = 40;
	time_esp.tm_hour = 9;
	time_esp.tm_mday = 1;
	time_esp.tm_mon = 3;
	time_esp.tm_wday = 5;

	unsigned long code = 13;
	const char *cron_data[3];
	cron_data[0] = "* 9-11 * * 1-5";
	cron_data[1] = "40-59 9-11 * * 1-5";
	cron_data[2] = "* * * * 1-4";

	printf("Test Crons: [%s] [%s] [%s]\n",cron_data[0],cron_data[1],cron_data[2]);

	for(int i = 0; i < 3; i++){
		test_crons(&code, cron_data[i], time_esp);
	}

}

void test_codeflash_nvs_reset(){
	nvs_handle handler;
	nvs_open(FLASH_NAMESPACE, NVS_READWRITE,  &handler);
	nvs_erase_key(handler, NVS_ACTIVE_P_KEY);
}


#endif















