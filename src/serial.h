#ifndef SERIAL_H_
#define SERIAL_H_

#include <stddef.h>

#include <zephyr/kernel.h>

typedef enum serial_ret_code_e
{
	SERIAL_RET_CODE_SUCCESS,
	SERIAL_RET_CODE_ERROR_UNKNOWN = -1,
	SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY = -2,
	SERIAL_RET_CODE_ERROR_TIMEOUT = -3,
	SERIAL_RET_CODE_ERROR_BUFFER_FULL = -4,
	SERIAL_RET_CODE_ERROR_BUSY = -5,
	SERIAL_RET_CODE_ERROR_NO_MEMORY = -6,
} serial_ret_code_t;

typedef enum serial_type_e
{
	SERIAL_TYPE_NONE = 0x00,
	SERIAL_TYPE_UART = (1<<0),
	SERIAL_TYPE_BLE = (1<<1),
	SERIAL_TYPE_ALL = 0xFFFFFFFF,
} serial_type_t;

typedef struct serial_event_new_data_s
{
	size_t count;
	char const * p_buf;
} serial_event_new_data_t;

typedef struct serial_event_buff_ovf_s
{
	size_t count;
} serial_event_buff_ovf_t;


typedef struct serial_event_s
{
	enum {
		SERIAL_EVENT_TYPE_NEW_DATA_RECEIVED,
		SERIAL_EVENT_TYPE_BUFFER_OVERFLOW,
	} type;
	union {
		serial_event_new_data_t new_data;
		serial_event_buff_ovf_t buf_ovf;
	} data;
} serial_event_t;

typedef void(*serial_event_callback_t)(serial_event_t const * p_evt);

typedef struct serial_line_s
{
	size_t const len;
	char const * const p_data;
} serial_line_t;

serial_ret_code_t serial_enable(serial_type_t type);
serial_ret_code_t serial_disable(serial_type_t type);
serial_line_t const * serial_get_line(k_timeout_t timeout);
serial_ret_code_t serial_send(k_timeout_t timeout, char const * p_data, int len);
serial_ret_code_t serial_sendf(k_timeout_t timeout, char const * format, ...);
serial_ret_code_t serial_set_end_character_list(char const * p_list, int len);


#endif  /* _ SERIAL_H_ */