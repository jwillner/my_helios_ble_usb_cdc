#ifndef UART_SERIAL_H_
#define UART_SERIAL_H_

#include <stddef.h>

#include <zephyr/kernel.h>

#include "serial.h"

serial_ret_code_t uart_serial_add_callback(serial_event_callback_t callback);
serial_ret_code_t uart_serial_remove_callback(serial_event_callback_t callback);
serial_ret_code_t uart_serial_enable();
serial_ret_code_t uart_serial_disable();
serial_line_t const * uart_serial_get_line(k_timeout_t timeout);
serial_ret_code_t uart_serial_send(k_timeout_t timeout, char const * p_data, int len);
serial_ret_code_t uart_serial_vsendf(k_timeout_t timeout, const char * format, va_list args);
serial_ret_code_t uart_serial_sendf(k_timeout_t timeout, char const * format, ...);
serial_ret_code_t uart_serial_set_end_character_list(char const * p_list, int len);


#endif  /* _ UART_SERIAL_H_ */