/*
 * sntp.h
 *
 *  Created on: Mar 15, 2019
 *      Author: root
 */

#ifndef MAIN_IB_SNTP_H_
#define MAIN_IB_SNTP_H_

#include "esp_err.h"

#define IB_TIME_SET_BIT BIT0

esp_err_t ib_sntp_set_ntp_server(const char *name);
void ib_sntp_obtain_time();

#endif /* MAIN_IB_SNTP_H_ */
