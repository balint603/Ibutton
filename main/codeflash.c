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



static const esp_partition_type_t PARTITION_TYPE = 0x40;
static const char PARTITION_A_NAME[] = "code_nvs1";
static const char PARTITION_B_NAME[] = "code_nvs2";
static const char FLASH_STATE_KEY[] = "cf_state";
static const char FLASH_NAMESPACE[] = "cf_ns";

static size_t OFFSET_END_PARTITION;


static struct Flash_states{
	esp_partition_t *active_partition;
	esp_partition_t *inactive_partition;
	size_t first_free_offset;
} g_partition_state;

/** Get the first free address of a partition. */
static size_t get_first_free_offset(esp_partition_t partition){
	size_t addr = 0;
	size_t value;
	esp_err_t ret;

	while(addr < OFFSET_END_PARTITION){
		// READ data from the current address into value.
		ret = esp_partition_read(&partition, addr, &value, sizeof(value));
		if(ESP_OK != ret){
			ESP_LOGE(__func__,"Partition read err code: '%i'",ret);
			return 1;
		}

		if(0xFFFFFFFF == value)
			return addr;
		addr += value;		//Get the next data.
	}
	return 1;
}
/** \brief Print out the free space of the selected partition.
 *
 * */
void show_partition_use(esp_partition_t partition){
	char out_str[] = "[                ]";
	size_t used_space = get_first_free_offset(partition);
	size_t used_space_in_KB = used_space;
	used_space_in_KB >>= 10;

	if(used_space == 1)
		return;

	int i = 1;
	int cnt = used_space;
	while(cnt-- > 0){
		out_str[i++] = '=';
	}

	printf("Used code flash space:\n	%s, in %iB, in %iKB\n"
			,out_str,(unsigned int)used_space, (unsigned int)used_space_in_KB);
}

/** \brief Initialize - get the data from the NVS.
 *  Saved data is the g_flash_state structure.
 *  \ret ESP_OK when ok
 *  \ret NVS error codes
 *  \ret ESP_ERR_NOT_FOUND when the partitions cannot be find
 * */
esp_err_t codeflash_init(){
	esp_err_t err;
	nvs_handle nvs_handler;							// Get data
	err = nvs_open(FLASH_NAMESPACE, NVS_READWRITE, &nvs_handler);
	uint8_t first_start = 0;
	size_t size;

	if(ESP_ERR_NVS_NOT_FOUND == err){
		ESP_LOGI(__func__,"First module init.");
		first_start = 1;
	}else if(ESP_OK != err){
		ESP_LOGE(__func__,"nvs_open error code: '%i'",err);
		return err;
	}

	size = sizeof(g_partition_state);
	err = nvs_get_blob(nvs_handler, FLASH_STATE_KEY, &g_partition_state, &size);
	if(ESP_ERR_NVS_NOT_FOUND == err){
		ESP_LOGI(__func__,"No entry found.");
		first_start = 1;
	}else if(ESP_OK != err){
		ESP_LOGE(__func__,"nvs_get_blob error code: '%i",err);
		return err;
	}
	if(first_start){
		esp_partition_iterator_t part_iterators[2];
		part_iterators[0] = esp_partition_find( PARTITION_TYPE,
				ESP_PARTITION_SUBTYPE_ANY,
						PARTITION_A_NAME);
		part_iterators[1] = esp_partition_find( PARTITION_TYPE,
				ESP_PARTITION_SUBTYPE_ANY,
						PARTITION_B_NAME);
		if( !(part_iterators[0] && part_iterators[1]) ){
			ESP_LOGE(__func__,"Codeflash partition not found.");
			return ESP_ERR_NOT_FOUND;
		}
		g_partition_state.active_partition = (esp_partition_t*)esp_partition_get(part_iterators[0]);
		g_partition_state.inactive_partition = (esp_partition_t*)esp_partition_get(part_iterators[1]);
		ESP_LOGI(__func__, "First start: Erasing code partitions...");
		esp_partition_erase_range(g_partition_state.active_partition, 0, 512*1024);
		esp_partition_erase_range(g_partition_state.inactive_partition, 0, 512*1024);
		ESP_LOGI(__func__, "Partitions are erased");
		nvs_set_blob(nvs_handler, FLASH_STATE_KEY, &g_partition_state,sizeof(g_partition_state));
	}
	OFFSET_END_PARTITION = (size_t)g_partition_state.active_partition->size - 1;
	g_partition_state.first_free_offset = get_first_free_offset(*g_partition_state.active_partition);
	show_partition_use(*g_partition_state.active_partition);
	nvs_commit(nvs_handler);
	nvs_close(nvs_handler);

	return ESP_OK;
}

#ifdef TEST_COMMANDS
void test_codeflash_init(){
	printf("Run init test\n");
	esp_err_t ret;
	ret = codeflash_init();
	ESP_ERROR_CHECK(ret);
	vTaskDelay(3000 / portTICK_RATE_MS);
}
#endif















