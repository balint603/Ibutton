/*
 * ib_database.c
 *
 *  Created on: Mar 25, 2019
 *      Author: root
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

#define TEST_MODE

const char *FILE_DB_BIN = 		  	"/spiffs/ib_database/ibd.bin";
const char *FILE_DB_TEMP = 	  		"/spiffs/ib_database/ibd_temp.bin";
const char *FILE_INFO = 		  	"/spiffs/ib_database/info_object.bin";
const char *FILE_CSV = 			  	"/spiffs/ib_database/database.csv";
const char *READ_PARAM = 			"rb";
const char *WRITE_PARAM = 			"wb";
const char *APPEND_PARAM = 			"ab";
const char *APPEND_CSV_PARAM = 		"a";
const char *READ_CSV_PARAM = 		"r";


/** ret 0 no info file */
int get_checksum(info_t *d) {
	FILE *fptr = fopen(FILE_INFO,"rb");
	if ( !fptr )
		return 0;
	fread(d, sizeof(info_t), 1, fptr);
	fclose(fptr);
	return 1;
}

/** \ret 1 fwrite error or fopen return null */
int save_checksum(info_t *d) {
	FILE *fptr = NULL;
	remove(FILE_INFO);
	fptr = fopen(FILE_INFO,"wb");
	if ( !fptr ) {
		ESP_LOGE(__func__,"File cannot be created");
		return 1;
	}
	if ( 1 != fwrite(d, sizeof(info_t), 1, fptr) ) {
		fclose(fptr);
		return 1;
	}
	fclose(fptr);
	return 0;
}

size_t get_file_size(FILE *fptr) {
	uint32_t free;
	fseek(fptr, 0L, SEEK_END);
	free = ftell(fptr);
	rewind(fptr);
	return free;
}

/** \brief Saves data into FILE_CSV
 *  If checksum param and the current saved csv checksum does not match, current data will be lost.
 *  \param data data to be saved in csv file
 *  	   data_length length of data in bytes
 *  	   checksum database sum
 *  \ret IBD_ERR_FILE_OPEN
 *  	 IBD_ERR_NO_MEM
 *  	 IBD_OK
 * */
esp_err_t ibd_append_csv_file(char *data, int *data_length, uint64_t checksum) {
	FILE *fptr;
	info_t checks;
	struct stat fstat;
	if ( get_checksum(&checks) ) {
		if (checks.checksum_csv != checksum) {
			checks.checksum_csv = checksum;
			save_checksum(&checks);
			if ( !stat(FILE_CSV,&fstat) ) {
				unlink(FILE_CSV);
			}
		}
	}
	fptr = fopen(FILE_CSV, APPEND_PARAM);
	if ( !fptr ) {
		return IBD_ERR_FILE_OPEN;
	}
	size_t free_space = IBD_CSV_FILE_SIZE - get_file_size(fptr);
	if ( *data_length > free_space ) {
		return IBD_ERR_NO_MEM;				// TODO rather: cut the data until last line which fits in
	}
	if ( *data_length != fwrite(data, sizeof(char), *data_length, fptr) ) {
		fclose(fptr);
		return IBD_ERR_WRITE;
	}

	fclose(fptr);
	return IBD_OK;
}

/** \brief Rename the inactive file to active if it exists.
 * Use after appending data in file finished.
 * */
static void activate_database() {
	esp_err_t ret;
	info_t checks;
	struct stat filestat;
	get_checksum(&checks);

	if ( (stat(FILE_DB_TEMP, &filestat)) )  {
		ESP_LOGW(__func__,"File does not exist:%s",FILE_DB_TEMP);
	}
	else {	// When inactive file is not empty
		if ( !stat(FILE_DB_BIN, &filestat) ) {			// According to ESP IDF component: SPIFFS
			unlink(FILE_DB_BIN);
		}
		ret = rename(FILE_DB_TEMP, FILE_DB_BIN);
		if ( ret ) {
			ESP_LOGI(__func__,"rename ret:%i", ret);
			return;
		}
		checks.checksum_cur = checks.checksum_temp;
		checks.checksum_temp = 0;
		if ( save_checksum(&checks) ) {
			ESP_LOGE(__func__,"Checksum cannot be saved");
		}
	}
}

/** \brief Creates an ib_data_t object.
 * Allocated memory space!
 * */
ib_data_t *create_ib_data(uint64_t code, char *crons) {
	uint16_t mem_d_size;
	uint8_t cron_size;
	ib_data_t *ret_data;

	if ( crons )
		cron_size = (uint8_t)strlen(crons) + 1;
	else
		cron_size = 0;
	mem_d_size = sizeof(ib_code_t) + cron_size;
	ret_data = malloc(cron_size + sizeof(ib_data_t));
	ret_data->code_s.code = code;
	ret_data->code_s.mem_d_size = mem_d_size;
	if ( cron_size ) {
		ret_data->crons =  (size_t*)( (size_t)&(ret_data->code_s) + (size_t)sizeof(ib_code_t));
		strcpy(ret_data->crons, crons);
	}
	else
		ret_data->crons = NULL;
	return ret_data;
}

/** \brief Check the SPIFFS partition and delete active file when found space is not enough. Used when initialization.
 * Removes active database file when the memory is not big enough to hold an another file.
 * \ret 1 there is enough free space to update database.
 * \ret 0 not enough space.
 *  */
static int is_place_enough() {
	size_t fsize = 0;
	size_t used_bytes, total_bytes;
	long free_bytes;
	FILE *fptr = fopen(FILE_DB_BIN, "rb");
	if ( fptr ) {
		fsize = get_file_size(fptr);
		fclose(fptr);
	}
	esp_spiffs_info(IBD_PARTITION_LABEL, &total_bytes, &used_bytes);
	free_bytes = total_bytes - used_bytes;
	if ( fsize > IBD_FILE_SIZE ) {				// Current file is greater then it must to be
		if ( free_bytes >= IBD_FILE_SIZE ) {
			return 1;											// Enough space for update.
		} else {
			remove(FILE_DB_BIN);	// Try to delete the huge file...
			ESP_LOGW(__func__,"Active databased deleted due to its size");
			fsize = 0;
		}
	}	// When fsize > IBD_FILE_SIZE or there are no free space and deleted the current.
	used_bytes = used_bytes - fsize;
	free_bytes = total_bytes - used_bytes;
	if ( (free_bytes) <  2*IBD_FILE_SIZE )
		return 0;
	fclose(fptr);
	return 1;
}

/** \brief Initialize.
 *  \ret ESP_ERR_NOT_FOUND the ESP spiffs component is not mounted
 *	\ret ESP_ERR_NNO_MEM not enough place for file with defined size
 *	\ret ESP_OK database is ready for read / write operations
 * */
esp_err_t ibd_init() {
	FILE *fptr;
	info_t info = {.checksum_temp = 0, .checksum_csv = 0, .checksum_cur = 0};
	if ( !esp_spiffs_mounted(IBD_PARTITION_LABEL) )
		return ESP_ERR_NOT_FOUND;
	if ( !get_checksum(&info) )					// Checksum reserve its place
		save_checksum(&info);
	fptr = fopen(FILE_DB_TEMP,"rb");
	if ( fptr ) {
		remove(FILE_DB_TEMP);
		fclose(fptr);
	}
	if ( is_place_enough() ) {
		fptr = fopen(FILE_DB_BIN, "rb");
		if ( !fptr ) {
			ESP_LOGI(__func__,"Empty database");
			return ESP_OK;	// TODO Jelezni?
		}
		fclose(fptr);
		return ESP_OK;
	}
	return ESP_ERR_NO_MEM;
}
/** \brief Get a ib_data_t from file with specified code value.
 * \param code_val search by this value
 * \param d_ptr
 * \ret IBD_FOUND ib_data_t found, d_ptr points to it
 * \ret IBD_ERR_FILE_OPEN
 * \ret IBD_ERR_DATA
 * \ret IBD_ERR_READ
 * */
esp_err_t ibd_get_by_code(uint64_t code_val, ib_data_t **d_ptr) {
	FILE *fptr = fopen(FILE_DB_BIN, READ_PARAM);
	if ( !fptr ) {
		ESP_LOGE(__func__,"File cannot be opened");
		return IBD_ERR_FILE_OPEN;
	}
	*d_ptr = NULL;
	uint32_t offset = 0;
	uint32_t end_offset = get_file_size(fptr) - IB_C_MIN_SIZE;
	ib_code_t code_s;
	char crons[IBD_CRON_MAX_SIZE];
	uint32_t crons_size;

	while ( offset < end_offset ) {
		fseek(fptr, offset, SEEK_SET);

		if ( 1 == fread(&code_s, sizeof(ib_code_t), 1, fptr) ) {	// Read data ok
			if ( code_s.code == code_val ) {	// Found
				crons_size = code_s.mem_d_size - IB_C_MIN_SIZE;
				if ( crons_size ) {
					if ( 1 != fread(crons, crons_size, 1, fptr) ) {
						ESP_LOGE(__func__,"Read cron from file error!");
						fclose(fptr);
						return IBD_ERR_READ;
					}
				}
				*d_ptr = create_ib_data(code_s.code, crons);
				if ( !(*d_ptr) ) {
					ESP_LOGE(__func__,"Data object cannot be created");
					return IBD_ERR_DATA;
				}
				fclose(fptr);
				return IBD_FOUND;
			} else {	// Not found, try next
				offset += code_s.mem_d_size;
			}
		} else {
			if ( feof(fptr) ) {
				ESP_LOGW(__func__,"EOF");
			} else if ( ferror(fptr) ) {
				ESP_LOGW(__func__,"File read error");
			}
			return IBD_ERR_FILE_OPEN;
			fclose(fptr);
		}

	}
	fclose(fptr);
	return IBD_ERR_NOT_FOUND;
}

/** \brief Create an ib_data_t object and save it to the flash.
 *  \ret The number of processed bytes from buffer csv.
 * */
static int process_csv(char *csv, size_t size, FILE *fptr) {
	char line[IBD_CSV_LINE_MAX_SIZE];
	uint32_t processed_bytes = 0;
	uint32_t read_bytes;
	ib_data_t *data;
	uint32_t free_bytes = IBD_FILE_SIZE - get_file_size(fptr);
#ifdef TEST_MODE
	ESP_LOGD(__func__,"free_bytes val:%i",free_bytes);
#endif
	while ( size ) {
		fseek(fptr, 0L, SEEK_CUR);
		read_bytes = csv_eat_a_line(line, size, &csv);
		processed_bytes += read_bytes;
		size -= read_bytes;
#ifdef TEST_MODE
		ESP_LOGD(__func__,"Processed bytes:%i",processed_bytes);
#endif
		if ( !read_bytes ) {
			ESP_LOGW(__func__,"Unexpected NULL char, or csv NULL ptr: at line:%i",processed_bytes);
			return processed_bytes;
		}
		data = csv_process_line(line);
		if ( !data ) {
			ESP_LOGW(__func__,"Invalid line at:[%i]", processed_bytes);
		} else {
#ifdef TEST_MODE
			printf("ib_data_t s:\n code[%lld]\n mems[%i]\n",data->code_s.code, data->code_s.mem_d_size);
			if ( data->crons ) {
				printf(" crons:[%s]\n",data->crons);
			}
#endif
			if ( free_bytes < data->code_s.mem_d_size ) {
				ESP_LOGW(__func__,"Run out of memory: at:[%i]",processed_bytes);
				return processed_bytes;
			}

			if ( 1 == fwrite(&(data->code_s), data->code_s.mem_d_size, 1, fptr) ){					// Is object saved?
				free_bytes -= data->code_s.mem_d_size;
			} else {
				ESP_LOGE(__func__,"Cannot save: at byte: [%i]"
						, processed_bytes);
				if ( feof(fptr) ) {
					ESP_LOGW(__func__,"EOF");
				} else if ( ferror(fptr) ) {
					ESP_LOGE(__func__,"FERROR");
				}
			}
		}
		free(data);
	}
	return processed_bytes;
}
/** \brief Write into FILE_DB_BIN or FILE_DB_TEMP
 *	Select FILE_DB_BIN when cannot find.
 * */
static FILE *select_file_to_write() {
	FILE *fptr = fopen(FILE_DB_BIN, READ_PARAM);
	const char *filename;
	const char *fparam;
	info_t checks;
	uint64_t *checksum_val;
	if ( !get_checksum(&checks) ) {
		ESP_LOGE(__func__,"Cannot open checksum file!");
		return NULL;
	}

	if ( fptr )	{		// There is an Active file
		filename = FILE_DB_TEMP;
		checksum_val = &(checks.checksum_temp);
	}
	else {
		filename = FILE_DB_BIN;
		checksum_val = &(checks.checksum_cur);
	}
	if ( *checksum_val != checks.checksum_csv ) {
		*checksum_val = checks.checksum_csv;
		fparam = WRITE_PARAM;
	} else {
		fparam = APPEND_PARAM;
	}
	fclose(fptr);
	if ( save_checksum(&checks) ) {
		ESP_LOGE(__func__,"Cannot save checksum!");
		return NULL;
	}
	return fopen(filename,fparam);
}

/** \brief Process and save the csv data into binary file.
 * Current file (FILE_DB_BIN or FILE_DB_TEMP) will be overwritten
 *  when info_t data .new_checksum (set by save_checksum function) is not equals with .active_checksum or .inactive_checksum.
 *  \param new_checksum
 *         csv
 *     	   data_len maximum bytes to process from file
 *  \ret IBD_OK all bytes all written successfully
 *       IBD_ERR_DATA when the remaining bytes cannot be saved
 *       ESP_ERR_NOT_FOUND fopen error
 *       ESP_ERR_INVALID ARG csv is null
 * */
esp_err_t ibd_append_from_str(char *csv, size_t *bytes_left) {
	FILE *fptr;
	if ( !(fptr = select_file_to_write()) ) {
		ESP_LOGE(__func__,"File cannot be opened!");
		return IBD_ERR_NOT_FOUND;
	}
	if ( !csv )
		return IBD_ERR_INVALID_PARAM;

	(*bytes_left) -= process_csv(csv, *bytes_left, fptr);
	fclose(fptr);
	if ( *bytes_left ) {
		return IBD_ERR_DATA;
	}
	return IBD_OK;
}
/** \brief Cut the \n or \r characters from a line string. */
char *str_chomp(char *buf) {
	char *begin = buf;
	while (*buf++)
		;
	while ( --buf >= begin ) {
		if (*buf >= ' ')
			return begin;

		if ( *buf == '\r' || *buf == '\n' )
			*buf = '\0';
	}
	return begin;
}

/** \brief csv to binary file processing.
 *  \param fcsv csv file pointer
 * 		   fbin bin file pointer
 * 		   uint32_t linecnt line counter this holds when the processing stipped
 * 	\return IBD_OK *linecnt lines are successully saved.
 * 	\return IBD_ERR_NO_MEM runned out of memory
 * */
static esp_err_t process_csv_to_bin(FILE *fcsv, FILE *fbin, uint32_t *linecnt) {
	char *linebuf = malloc(IBD_CSV_LINE_MAX_SIZE);
	size_t linesize = IBD_CSV_LINE_MAX_SIZE;
	*linecnt = 0;
	ib_data_t *data;
	size_t freebytes = IBD_FILE_SIZE - get_file_size(fbin);
	uint32_t cnt;
	while ( -1 != (cnt = __getline(&linebuf, &linesize, fcsv) ) ) {

		if ( cnt > 0) {
			fseek(fbin,0L,SEEK_CUR);
			if ( ( data = csv_process_line(str_chomp(linebuf)) ) ) {// Process ok // @suppress("Assignment in condition")
	#ifdef TEST_MODE
				ESP_LOGD(__func__,"ib_data_t s:\n code[%lld]\n mems[%i]\n",data->code_s.code, data->code_s.mem_d_size);
				if ( data->crons ) {
					ESP_LOGD(__func__," crons:[%s]\n",data->crons);
				}
	#endif
				if ( freebytes < data->code_s.mem_d_size ) {// Check the free space
					ESP_LOGW(__func__,"Run out of memory at line:[%i]",*linecnt);
					free(linebuf);
					return IBD_ERR_NO_MEM;
				}

				if ( 1 == fwrite(&(data->code_s), data->code_s.mem_d_size, 1, fbin) ){// Is object saved?
					freebytes -= data->code_s.mem_d_size;
				} else {
					ESP_LOGE(__func__,"Cannot save processed data at line:[%i]", *linecnt);
					if ( feof(fbin) ) {
						ESP_LOGW(__func__,"EOF");
					} else if ( ferror(fbin) ) {
						ESP_LOGE(__func__,"FERROR");
					}
				}
			} else {// Process not ok
				ESP_LOGW(__func__,"Cannot process line at:[%i]",*linecnt);
			}
			(*linecnt)++;
			free(data);
		}
	}//EOF
	if ( feof(fcsv) ) {
		ESP_LOGD(__func__,"EOF reached.");
	}
	free(linebuf);
	return IBD_OK;
}

/** \brief Make a binary database from csv file.
 *  \return IBD_OK when file successfully loaded.
 *  		IBD_ERR_FILE_OPEN when destination binary file cannot be opened
 *  		IBD_ERR_INVALID_PARAM when the FILE_PATH null
 *  		IBD_ERR_NOT_FOUND when FILE_PATH file cannot be opened
 *  		IBD_ERR_DATA file processing stopped
 * */
esp_err_t ibd_make_bin_database(const char *FILE_PATH) {
	FILE *fptr_bin;
	FILE *fptr_csv;
	esp_err_t ret;
	uint32_t line;
	struct stat filestat;

	if ( !(fptr_bin = select_file_to_write()) ) {
		ESP_LOGE(__func__,"File cannot be opened!"); // sterror?
		return IBD_ERR_FILE_OPEN;
	}
	if ( !FILE_PATH ) {
		ESP_LOGE(__func__,"File path NULL");
		return IBD_ERR_INVALID_PARAM;
	}
	if ( !(fptr_csv = fopen(FILE_PATH, READ_CSV_PARAM)) ) {
		ESP_LOGE(__func__,"File cannot be opened!:%s",FILE_PATH); // sterror?
		return IBD_ERR_NOT_FOUND;
	}
	ESP_LOGD(__func__,"Start reading path: [%s]",FILE_PATH);
	ret = process_csv_to_bin(fptr_csv, fptr_bin, &line);
	fclose(fptr_bin);
	fclose(fptr_csv);
	if ( ret ) {
		ESP_LOGE(__func__,"Errcode:%x\nFile processing stopped at line:[%i]",ret, line);
		return IBD_ERR_DATA;
	}
	activate_database();
	if ( !stat(FILE_CSV, &filestat) ) {			// Delete csv file: According to ESP IDF component: SPIFFS
		unlink(FILE_CSV);
	}

	return IBD_OK;
}

void test_process_csv() {
	ESP_LOGI(__func__,"START");
	const char test_filename[] = "/spiffs/testfile";
	remove(test_filename);
	FILE *fptr = fopen(test_filename,"wb");
	char line[3][80] = {"01300EBC1A0000D0| ",
			"01300EBC1A0000D1| * * * * *  01300EBC1A0000D1 * * * * * ; * * * * * ",
			"01300EBC1A0000D2| * * * * *;  01300EBC1A0000D2| * * * * * ; * * * * * "
	};
	line[0][17] = '\n';
	line[1][27] = '\n';
	line[2][28] = '\n';
	uint32_t retval;
	for ( int i = 0; i < 3; i++ ) {
		if ( ( retval = process_csv(line[i], strlen(line[i]) + 1, fptr) ) != (strlen(line[i]) + 1 ) ) {
			ESP_LOGE(__func__,"process_csv ret:%i, data size:%i", retval, strlen(line[i]) + 1 );
		} else {
			ESP_LOGI(__func__,"csv[%i] OK", i);
		}
	}
	fclose(fptr);
	ESP_LOGI(__func__,"END");
}



