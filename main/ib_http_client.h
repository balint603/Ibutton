/*
 * ib_http_client.h
 *
 *  Created on: Apr 5, 2019
 *      Author: root
 */

#ifndef MAIN_IB_HTTP_CLIENT_H_
#define MAIN_IB_HTTP_CLIENT_H_

#include "esp_err.h"

/** SETTINGS START____________________________________________*/

#define UPDATES_PERIOD_MS 10000					// Database update check

/** SETTINGS END______________________________________________*/

/** ERRORS */



/** ERRORS END*/


esp_err_t ib_client_init();




void register_setserver();

#endif /* MAIN_IB_HTTP_CLIENT_H_ */
