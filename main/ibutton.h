/*
 * ibutton.h
 *
 *  Created on: Feb 23, 2019
 *      Author: root
 */

#ifndef MAIN_IBUTTON_H_
#define MAIN_IBUTTON_H_

#define DATA_GPIO_NUM 5


typedef unsigned long ib_code_t;


void ib_init();
int ib_presence();
int ib_read_code(ib_code_t *ib_code);



#endif /* MAIN_IBUTTON_H_ */
