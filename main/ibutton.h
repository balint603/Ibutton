/*
 * ibutton.h
 *
 *  Created on: Feb 23, 2019
 *      Author: root
 */

#ifndef MAIN_IBUTTON_H_
#define MAIN_IBUTTON_H_


typedef unsigned long ib_code_t;
typedef int ib_ret_t;

#define IB_OK 0
#define IB_FAM_ERR 1
#define IB_CRC_ERR 2

void onewire_init(gpio_num_t data_pin);
int ib_presence();
ib_ret_t ib_read_code(ib_code_t *ib_code);



#endif /* MAIN_IBUTTON_H_ */
