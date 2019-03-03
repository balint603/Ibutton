/*
 * ib_reader.h
 *
 *  Created on: Feb 26, 2019
 *      Author: root
 */

#ifndef MAIN_IB_READER_H_
#define MAIN_IB_READER_H_

#include "esp_system.h"


#define READER_DISABLE_TICKS pdMS_TO_TICKS(400)

#define STANDARD_OPENING_TIME 3000

#define PIN_DATA 		GPIO_NUM_5
#define PIN_BUTTON 		GPIO_NUM_18
#define PIN_RED			GPIO_NUM_22
#define PIN_GREEN 		GPIO_NUM_23
#define PIN_RELAY 		GPIO_NUM_19
#define PIN_SU_ENABLE	GPIO_NUM_15

void start_ib_reader();


#endif /* MAIN_IB_READER_H_ */
