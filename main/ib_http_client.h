/**
 * @defgroup ib_http_client
 * @{
 *
 * ib_http_client.h
 *
 *  Created on: Apr 5, 2019
 *      Author: root
 */

#ifndef MAIN_IB_HTTP_CLIENT_H_
#define MAIN_IB_HTTP_CLIENT_H_

#include "esp_err.h"
#include <stdint.h>

/** Database update check in ms */
#define UPDATES_PERIOD_MS 10000

esp_err_t ib_client_init();
char *ib_client_get_log_url();
void register_setserver();
int ib_client_send_logmsg(char *data, size_t length);

#endif /* MAIN_IB_HTTP_CLIENT_H_ */

/** @} */
