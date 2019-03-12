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
#include "esp_partition.h"
#include "rom/spi_flash.h"
#include "nvs.h"
#include "nvs_flash.h"


#include "codeflash.h"

#define MAX_VALUE(a) (((unsigned long long)1 << (sizeof(a) * CHAR_BIT)) - 1)
#define SIZE_OF_CRON_LENGTH_AND_CODE 9

static const esp_partition_type_t PARTITION_TYPE = 0x40;
static const char PARTITION_A_NAME[] = "A_codefl";
static const char PARTITION_B_NAME[] = "B_codefl";

static const char NVS_ACTIVE_P_KEY[] = "cf_k";
static const char FLASH_NAMESPACE[] = "cf_ns";

static size_t OFFSET_END_PARTITION;

static char g_cron_data_storage[256];

static struct Flash_states{
	esp_partition_t *active_partition;
	esp_partition_t *inactive_partition;
	size_t active_first_free_offset;
	size_t inactive_first_free_offset;
} g_partition_state;

/** Get the first free address of a partition. */
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
	size_t used_space_in_KB = used_space;
	used_space_in_KB >>= 10;

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

	printf("Used code flash space:\n	%s, in %iB, in %iKB\n"
			,out_str,(unsigned int)used_space, (unsigned int)used_space_in_KB);
}


/** \brief Change and erase the new inactive partition.
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

/** \brief Initialize - get the data from the NVS.
 *  Save g_flash_state structure if not saved yet.
 *  \ret ESP_OK when ok
 *  \ret NVS error codes
 *  \ret ESP_ERR_NOT_FOUND when the partitions cannot be find
 * */
esp_err_t codeflash_init(){
	esp_err_t err;
	nvs_handle nvs_handler;							// Get data
	char partition_label[17];
	const size_t size_of_nvs_data = (size_t)strlen(PARTITION_A_NAME) + 1;
	size_t size = size_of_nvs_data;
	err = nvs_open(FLASH_NAMESPACE, NVS_READWRITE, &nvs_handler);
	uint8_t first_start = 0;

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
	esp_partition_t *partition_A;
	esp_partition_t *partition_B;
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
		ESP_LOGI(__func__, "First start: Erasing code partitions...");
		esp_partition_erase_range(g_partition_state.active_partition, 0,
				g_partition_state.active_partition->size);
		esp_partition_erase_range(g_partition_state.inactive_partition, 0,
				g_partition_state.inactive_partition->size);
		ESP_LOGI(__func__, "Partitions are erased");
		err = nvs_set_str(nvs_handler, NVS_ACTIVE_P_KEY,
				g_partition_state.active_partition->label);
		if(ESP_OK != err){
			ESP_LOGE(__func__,"NVS data set");
		}
	}else{			// NVS info found
		if(partition_label[0] == 'A'){
			g_partition_state.active_partition = partition_A;
			g_partition_state.inactive_partition = partition_B;
		}
		else{
			g_partition_state.active_partition = partition_B;
			g_partition_state.inactive_partition = partition_A;
			if(partition_label[0] != 'B')
				ESP_LOGE(__func__,"Partition name in NVS corrupted: ['%s']",partition_label);
		}
	}
	// search the last item
	OFFSET_END_PARTITION = (size_t)g_partition_state.active_partition->size - 1;
	g_partition_state.active_first_free_offset = get_first_free_offset(*g_partition_state.active_partition);
	show_partition_use(*g_partition_state.active_partition);
	// commit NVS
	nvs_commit(nvs_handler);
	nvs_close(nvs_handler);

	return ESP_OK;
}

/** \brief Writes raw data into the flash memory.
 *  \param data to be written
 *  \param length length of the data
 *  \param into_inactive 0: append data into active, else into inactive partition
 * */
esp_err_t codeflash_append_raw_data(void *data, size_t length, int into_inactive){
	esp_err_t ret;
	if(!data)
		return ESP_ERR_INVALID_ARG;
	if(!length)
		return ESP_ERR_INVALID_SIZE;

	size_t offset;
	esp_partition_t *partition;
	if(into_inactive){
		offset = g_partition_state.inactive_first_free_offset;
		partition = g_partition_state.inactive_partition;
	}
	else{
		offset = g_partition_state.active_first_free_offset;
		partition = g_partition_state.active_partition;
	}// todo change 0 back to offset

	ret = esp_partition_write(partition, offset, data, length);
	if(ret == ESP_OK){
		if(into_inactive)
			g_partition_state.inactive_first_free_offset += length;
		else
			g_partition_state.active_first_free_offset += length;
	}
	return ret;
}

/** \brief Search a key entry from the active partition.
 *  Allocate memory for storing cron string.
 *  \param code search by
 *  \param output data
 *  \ret esp errors.
 *  \ret ESP_NOT_FOUND when the code is not in the flash memory.
 *  \ret ESP_OK when the data has been found and memory allocated for cron string.
 * */
esp_err_t codeflash_get_by_code(long code, codeflash_t *data_f){
	esp_err_t ret;
	size_t addr;
	if(!data_f || code == MAX_VALUE(long))
		return ESP_ERR_INVALID_ARG;

	addr = 0;
	while(addr < g_partition_state.active_first_free_offset){
		ret = esp_partition_read(g_partition_state.active_partition,		// Read next entry
				addr, data_f, SIZE_OF_CRON_LENGTH_AND_CODE);
		if(ESP_OK != ret)
			return ret;
#ifdef TEST_COMMANDS
		printf("Addr value:\n <%i>\n",addr);
		printf("Found data:\n [%ld]\n",data_f->code);
		printf("Search this data:\n [%ld]\n",code);
#endif
		if(code == data_f->code){		// Found?
			addr += SIZE_OF_CRON_LENGTH_AND_CODE; // read cron string into the global variable
			esp_partition_read(g_partition_state.active_partition, addr,
					g_cron_data_storage, data_f->cron_length);
			data_f->cron = g_cron_data_storage;
#ifdef TEST_COMMANDS
			printf("Found string:\n %s",data_f->cron);
#endif
			return ESP_OK;
		}
		addr += (SIZE_OF_CRON_LENGTH_AND_CODE + data_f->cron_length);
	}	// End of the list?
	return ESP_ERR_NOT_FOUND;
}

void codeflash_erase_both(){
	printf("Erase both partition... size: %i\n",g_partition_state.active_partition->size);
	ESP_ERROR_CHECK(esp_partition_erase_range(g_partition_state.active_partition,
			0,
			g_partition_state.active_partition->size));

	ESP_ERROR_CHECK(esp_partition_erase_range(g_partition_state.inactive_partition,
			0,
			g_partition_state.inactive_partition->size));
}

#ifdef TEST_COMMANDS

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

void test_codeflash_write(){
	printf("Run write test\n");
	esp_err_t ret;
	const int data_size = 20 * 9 + 20 * 12;
	char data[data_size];
	int data_cnt;
	long code[20];
	long code_data = 13;
	codeflash_t flash_data;

	data_cnt = 0;
	for(int i = 0; i < 20; i++){
		long temp_code;

		data[data_cnt++] = (unsigned char)12;			// Set cron size

		code[i] = code_data++;
		temp_code = code[i];
		for(int j = 8; j > 0; j--){
			data[data_cnt++] = (unsigned char)temp_code;
			temp_code >>= 8;
		}
		strcpy(&(data[data_cnt]),"* * * * * *");		// Cron
		data_cnt += 12;
	}

	ret = codeflash_append_raw_data(data, data_size, 1);
	ESP_ERROR_CHECK(ret);

	change_partitions();

	for(int i = 0; i < 20; i++){
		ret = codeflash_get_by_code(code[i], &flash_data);
		ESP_ERROR_CHECK(ret);
		printf("Get by code: %ld %s %i\n",flash_data.code,flash_data.cron,flash_data.cron_length);
	}
	int cnt = 0;
	for(int i = 0; i < 5; i++){
		print_codeflash_data(0,256,0);
		print_codeflash_data(0,256,1);
		cnt += 256;
	}
}

void test_codeflash_init(){
	printf("Run init test\n");
	esp_err_t ret;
	ret = codeflash_init();
	ESP_ERROR_CHECK(ret);
	vTaskDelay(3000 / portTICK_RATE_MS);
}

void test_codeflash_nvs_reset(){
	nvs_handle handler;
	nvs_open(FLASH_NAMESPACE, NVS_READWRITE,  &handler);
	nvs_erase_key(handler, NVS_ACTIVE_P_KEY);
}


#endif















