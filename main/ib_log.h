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


typedef struct ib_log {
	uint64_t code;
	uint8_t log_type;
	struct tm timestamp;
} ib_log_t;

/** LOGTYPES */
#define IB_LOG_KEY_ACCESS_GAINED 		0
#define IB_LOG_KEY_INVALID_KEY_TOUCH 	1
#define IB_LOG_KEY_OUT_OF_DOMAIN 		2

#define IB_LOG_ERR_CONNECTION_LOST 100
#define IB_LOG_ERR_CONNECTION_OK   200

#endif /* MAIN_IB_LOG_H_ */


int ib_send_json_logm(ib_log_t *logmsg);
