/**
 * ib_reader.h
 *
 *  Created on: Feb 26, 2019
 *      Author: root
 *  @defgroup ib_reader
 *  @{
 *  @defgroup state_functions State functions
 */

#ifndef MAIN_IB_READER_H_
#define MAIN_IB_READER_H_

#include "esp_system.h"
#include <time.h>

/** @defgroup reader_settings Reader settings
 * @{*/

/** Time interval in ms. After a successful read reader is blocked for this time. */
#define READER_DISABLE_TICKS pdMS_TO_TICKS(400)

/** Stay defined ms in access allowed state */
#define STANDARD_OPENING_TIME 	3000

#define STANDARD_DEVICE_NAME	"iBreader1"

/** Data pin connected to data wire of the iButton reader. */
#define PIN_DATA 		GPIO_NUM_5
/** Pushbutton. */
#define PIN_BUTTON 		GPIO_NUM_18
#define PIN_RED			GPIO_NUM_22
#define PIN_GREEN 		GPIO_NUM_23
#define PIN_RELAY 		GPIO_NUM_19
#define PIN_SU_ENABLE	GPIO_NUM_15
/** @} */

/** @defgroup op_modes Operation mode types
 * This macros define the possible operational modes.
 * As you can see at the state functions, there are 3 types of access allow state.
 * Only one access allow state works, depends on the current setting.
 *
 * There are 3 types of modes.
 * @{*/
/** Access allow (relay opened) for defined time. */
#define IB_READER_MODE_NORMAL 0
/** Access allow until a new valid key was not touched. */
#define IB_READER_MODE_BISTABLE 1
/** Access allow until the same key was not touched. */
#define IB_READER_MODE_BISTABLE_SAME_KEY 2
/** @} */

void start_ib_reader();
void ib_need_su_touch();
void ib_not_need_su_touch();
int ib_waiting_for_su_touch();

char *ib_get_device_name();
void ib_set_device_name(const char* name);
void ib_set_su_key(uint64_t code);
void ib_set_opening_time(uint32_t ms);
void ib_set_mode(uint32_t mode);

#endif /* MAIN_IB_READER_H_ */

/** @} */
