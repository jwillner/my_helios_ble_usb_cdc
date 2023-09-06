#ifndef SERIAL_INTERNAL_H_
#define SERIAL_INTERNAL_H_

#include <zephyr/kernel.h>

#include "serial.h"

typedef union serial_internal_line_u
{
	serial_line_t fixed;
	struct modifiable_line_s
	{
		size_t len;
		char * p_buffer;
	} mutable;
} serial_internal_line_t;

serial_line_t const * serial_internal_get_line(
	struct k_sem * p_sem_data_ready,
	k_timeout_t timeout,
	serial_internal_line_t * p_line,
	uint8_t const * input_buffer,
	size_t const input_buffer_size,
	int * p_input_buffer_index,
	int volatile * p_bytes_in_buffer,
	char const * end_character_list,
	int const end_character_count, 
	serial_event_callback_t fire_callbacks_function);

#endif  /* _ SERIAL_INTERNAL_H_ */
