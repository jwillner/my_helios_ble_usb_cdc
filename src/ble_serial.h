#ifndef BLE_SERIAL_H_
#define BLE_SERIAL_H_

#include <stddef.h>

#include <zephyr/kernel.h>

#include "serial.h"

serial_ret_code_t ble_serial_add_callback(serial_event_callback_t callback);
serial_ret_code_t ble_serial_remove_callback(serial_event_callback_t callback);
serial_ret_code_t ble_serial_enable();
serial_ret_code_t ble_serial_attach();
serial_ret_code_t ble_serial_disable();
serial_ret_code_t ble_serial_detach();
serial_line_t const * ble_serial_get_line(k_timeout_t timeout);
serial_ret_code_t ble_serial_send(k_timeout_t timeout, char const * p_data, int len);
serial_ret_code_t ble_serial_vsendf(k_timeout_t timeout, const char * format, va_list args);
serial_ret_code_t ble_serial_sendf(k_timeout_t timeout, char const * format, ...);
serial_ret_code_t ble_serial_set_end_character_list(char const * p_list, int len);

#endif  /* _ BLE_SERIAL_H_ */