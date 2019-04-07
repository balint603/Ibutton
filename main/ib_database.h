/*
 * ib_database.h
 *
 *  Created on: Mar 25, 2019
 *      Author: root
 *
 *
 * The functions of this module use spiffs component.
 * All functions must be called, after file system has been initailized!
 *
 * This module uses the defined size of flash memory and the free storage place must be large enough to store these files.
 *
 * How this module works:
 *  - A csv file can be filled up with entries by function ibd_append_csv_file() which needs a checksum as parameter.
 * If current csv file checksum does not match the saved one, a new csv file will be created.
 *  - After the csv file filled up with entries, function ib_make_bin_database() must called to generate a binary database
 * from the csv file. After done, the actual database is working.
 *  - Call ibd_get_by_code() function to search the wanted entry specified with iButton key code.
 *
 *
 *
 * spiffs component:
 * https://github.com/espressif/esp-idf/tree/master/components/spiffs
 *
 * CSV format:
 *  - "|" delimiter or choose a character which is not included in crons.
 * String format of a line in CSV file:
 *
 *
 * Field: Code:
 * 	- not case sensitive
 * 	- begin with or without: "0x" "0X" accepted
 * 	- extra spaces are allowed only the start or end of the string.
 * Field: Crons:
 *  - crons are separated by ";" or choose a character which is not included in crons.
 *
 * Examples:
 * "8899aabbccddeeff|* 6-16 * * 1-5;* 9-13 * * 1,7\n"
 * "8899AABBCCDDEEFF|* 6-16 * * 1-5;* 9-13 * * 1,7\r\n"
 * "0x_8899AABBCCDDEEFF_|_* 6-16 * * 1-5_;_* 9-13 * * 1,7_\r"     -> "_"shows all possible place of extra spaces.
 *
 *
 *
 * A binary entry in file: ( or ib_code_t objec)
 *
 * Prev.|____ ________________ _____________       ___| Next data
 * 		|    |                |                       |
 * 		|    |      code [8B] |cron[0-256]B  o o o    |
 * 		|____|________________|_____________       ___|
 *        ^              ^            ^
 	 	  |				 |			  |
 *    mem_d_size [2B]	 |			  |
 * 	                     |            |
 * 	  code_______________|            |
 * 	  -iButton serial number          |
 * 	  								  |
 * 	  cron string format______________|
 * 	  - ';' seperated list
 * 	  - example string: "* 7-17 * * 1-5;* 12-20 * * 0,6"
 * 	  - 'or' relation between crons
 * 	  - includes null terminator
 */

#ifndef MAIN_IB_DATABASE_H_
#define MAIN_IB_DATABASE_H_

#include "esp_err.h"

/** File informations */
#define IBD_PARTITION_LABEL 	NULL 			// NULL: basic spiffs name will be searched.
#define IBD_FILE_SIZE 			(256 * 1024)	// temporary and active files size
#define IBD_CSV_FILE_SIZE		(512 * 1024)	// csv file size

/** CSV file informations */
#define IBD_CRON_MAX_SIZE 		256				// Max cron size
#define IBD_CSV_CODE_SIZE 		16				// Min Size of iButton code in file csv
#define IBD_CSV_FIELDS 			2				// Number of fields in csv
#define IBD_CSV_LINE_MAX_SIZE 		(IBD_CRON_MAX_SIZE + IBD_CSV_CODE_SIZE + 1)

#define DELIMITER 				"|"
#define CRON_DELIMITER 			';'
#define CSV_DELIMITER_LENGTH 	 1


/** Binary file informations */
#define IBD_CODE_SIZE			(sizeof(uint64_t))
#define IBD_CRONS_L_SIZE		(sizeof(uint16_t))
#define IBD_MIN_SIZE			(IBD_CODE_SIZE + IBD_CRONS_L_SIZE + sizeof(char*))
#define IBD_MIN_MEM_SIZE		(IBD_CODE_SIZE + IBD_CRONS_L_SIZE)

#define IB_C_MIN_SIZE			(IBD_CODE_SIZE + IBD_CRONS_L_SIZE)

/** ERROR CODES  */
#define IBD_OK					(0)
#define IBD_FOUND				(IBD_OK)
#define IBD_ERR_BASE			(0x666)
#define IBD_ERR_NOT_FOUND		(IBD_ERR_BASE + 0x01)
#define IBD_ERR_READ			(IBD_ERR_BASE + 0x02)
#define IBD_ERR_FILE_OPEN		(IBD_ERR_BASE + 0x03)
#define IBD_ERR_DATA			(IBD_ERR_BASE + 0x04)
#define IBD_ERR_INVALID_PARAM   (IBD_ERR_BASE + 0x05)
#define IBD_ERR_NO_MEM   		(IBD_ERR_BASE + 0x06)
#define IBD_ERR_WRITE   		(IBD_ERR_BASE + 0x07)


/** \brief Part of the ib_data_t structure.
 *  \var mem_d_size size of the code and crons string
 *  \var code iButton code.
 *  */
typedef struct __attribute__((__packed__)) ib_code{
	uint16_t mem_d_size;
	uint64_t code;
} ib_code_t;

/** \brief IButton data object.
 *  \var crons pointer to crons.
 *  \var code_s code part.
 * */
typedef struct __attribute__((__packed__)) ib_data{
	char *crons;
	ib_code_t code_s;
	// CRON STRING space
} ib_data_t;

/** \brief Information of the database state.
 * */
typedef struct __attribute__((__packed__)) info_data{
	uint64_t checksum_cur;		// Current "running" database
	uint64_t checksum_temp;		// Temporary binary database checksum (Needed while processing csv file)
	uint64_t checksum_csv;		// Downloaded csv file checksum.
} info_t;

/** Get or change the current checksums  */
int ibd_get_checksum(info_t *d);
int ibd_save_checksum(info_t *d);

ib_data_t *create_ib_data(uint64_t code, char *crons);

unsigned long ib_get_checksum();



esp_err_t ibd_init();

esp_err_t ibd_get_by_code(uint64_t code_val, ib_data_t **d_ptr);

esp_err_t ibd_append_from_str(char *csv, size_t *bytes_left);

esp_err_t ibd_append_csv_file(char *data, int *data_length, uint64_t checksum);

esp_err_t ibd_make_bin_database();



uint32_t csv_eat_a_line(char *line, int size, char **from);

ib_data_t *csv_process_line(char *line);

size_t get_file_size(FILE *fptr);

char *str_chomp(char *buf);

#ifdef test_mode
void test_process_csv();
void test_process_line();
void test_process_csv();
#endif
#endif /* MAIN_IB_DATABASE_H_ */
