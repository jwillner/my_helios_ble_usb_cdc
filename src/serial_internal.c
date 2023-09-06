#include "serial_internal.h"

#include <zephyr/logging/log.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DEFINITIONS AND STATIC VARIABLES
#ifndef SERIAL_INTERNAL_LOG_LEVEL
#ifdef SERIAL_LOG_LEVEL
#define SERIAL_INTERNAL_LOG_LEVEL SERIAL_LOG_LEVEL
#else
#define SERIAL_INTERNAL_LOG_LEVEL LOG_LEVEL_WRN
#endif // SERIAL_LOG_LEVEL
#endif // !SERIAL_INTERNAL_LOG_LEVEL

#define LOG_MODULE_NAME serial_internal
LOG_MODULE_REGISTER(LOG_MODULE_NAME, SERIAL_INTERNAL_LOG_LEVEL);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
	serial_event_callback_t fire_callbacks_function)
{
	static serial_event_t event;
	static serial_line_t const empty_line = { 0 };
	
	LOG_DBG("getting next line");
	p_line->mutable.len = 0;
	
	while (true)
	{
		if ((*p_bytes_in_buffer == p_line->mutable.len) && (k_sem_take(p_sem_data_ready, timeout) != 0))
		{
			LOG_DBG("timeout reached");
			return &empty_line;
		}
		
		bool line_end_found = false;

		int bytes_to_check = *p_bytes_in_buffer;
		if (bytes_to_check > input_buffer_size)
		{
			int lost_bytes = *p_bytes_in_buffer - input_buffer_size; 
			*p_bytes_in_buffer -= lost_bytes;
			bytes_to_check = input_buffer_size;
			*p_input_buffer_index = (*p_input_buffer_index + lost_bytes) % input_buffer_size;
			p_line->mutable.len = 0;
			LOG_WRN("overflow! %d bytes overwritten! (StartIndex: %d, BytesInBuffer: %d)", lost_bytes, *p_input_buffer_index, *p_bytes_in_buffer);
			event.type = SERIAL_EVENT_TYPE_BUFFER_OVERFLOW;
			event.data.buf_ovf.count = lost_bytes;
			fire_callbacks_function(&event);
		}
		
		for (int i = p_line->mutable.len; i < bytes_to_check; i++)
		{
			int buffer_index = (*p_input_buffer_index + i) % input_buffer_size;
			p_line->mutable.p_buffer[i] = input_buffer[buffer_index];
			p_line->mutable.len++;
			for (int j = 0; j < end_character_count; j++)
			{
				if (p_line->mutable.p_buffer[i] == end_character_list[j])
				{
					p_line->mutable.p_buffer[p_line->mutable.len] = '\0';
					line_end_found = true;
					break;
				}
			}
			if (line_end_found)
			{
				break;
			}
		}
		
		if ((end_character_count == 0 && p_line->mutable.len > 0) || line_end_found)
		{
			if (*p_bytes_in_buffer > input_buffer_size)
			{
				line_end_found = false;
				p_line->mutable.len = 0;
				LOG_WRN("overflow while copying data to line structure!");
				break;
			}
			*p_input_buffer_index = (*p_input_buffer_index + p_line->mutable.len) % input_buffer_size;
			*p_bytes_in_buffer -= p_line->mutable.len;
			LOG_INF("line with length %d returned, bytes left in buffer: %d", p_line->mutable.len, *p_bytes_in_buffer);
			return &(p_line->fixed);
		}
		
		LOG_DBG("no line in received data, waiting for new data...");
	}
	
	LOG_ERR("fell through line preparation!");
	return &(p_line->fixed);
}