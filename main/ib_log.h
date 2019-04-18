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
	uint64_t code;
	uint8_t log_type;
	struct tm timestamp;
} ib_log_t;

/** LOGTYPES */
#define IB_LOG_KEY_ACCESS_GAINED 		0
#define IB_LOG_KEY_INVALID_KEY_TOUCH 	1
#define IB_LOG_KEY_OUT_OF_DOMAIN 		2
#define IB_SYSTEM_UP			 		3

#define IB_LOG_ERR_CONNECTION_LOST 100
#define IB_LOG_ERR_CONNECTION_OK   200

#endif /* MAIN_IB_LOG_H_ */


void ib_log_init();
void ib_log_post(ib_log_t *msg);
cJSON *create_json_msg(ib_log_t *logmsg);
