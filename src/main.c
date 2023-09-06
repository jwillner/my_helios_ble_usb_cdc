/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include "serial.h"

void main(void)
{
	serial_enable(SERIAL_TYPE_UART|SERIAL_TYPE_BLE);
	serial_set_end_character_list("\n", 1);
	serial_sendf(K_FOREVER, "Hello World!\n");
	
	while (true)
	{
		serial_line_t const * p_line = serial_get_line(K_FOREVER);
		serial_send(K_FOREVER, p_line->p_data, p_line->len);
	   	serial_sendf(K_FOREVER, "Hello World\n");
	}
}
