/*
 * ib_log.h
 *
 *  Created on: Apr 9, 2019
 *      Author: root
 */

#ifndef MAIN_IB_LOG_H_
#define MAIN_IB_LOG_H_

#include <inttypes.h>
#include <time.h>
#include "cJSON.h"

typedef struct ib_log {
	uint64_t value;
	const char *log_type;
} ib_log_t;

/** LOGTYPES */
#define IB_LOG_KEY_ACCESS_GAINED 		"AA"
#define IB_LOG_KEY_INVALID_KEY_TOUCH 	"AD"
#define IB_LOG_LOG_FILE_FULL	 		"FF"
#define IB_LOG_KEY_OUT_OF_DOMAIN 		"OD"
#define IB_SYSTEM_UP			 		"UP"
#define IB_LOG_DATAB 			 		"DOWN"

#define IB_LOG_ERR_CONNECTION_LOST 100
#define IB_LOG_ERR_CONNECTION_OK   200

#endif /* MAIN_IB_LOG_H_ */

void ib_log_init();
void ib_log_post(ib_log_t *msg);
cJSON *create_json_msg(ib_log_t *logmsg);
