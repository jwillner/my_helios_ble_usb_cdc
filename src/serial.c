#include "serial.h"

#include <zephyr/logging/log.h>

#include "uart_serial.h"
#include "ble_serial.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DEFINITIONS AND STATIC VARIABLES
#ifndef SERIAL_LOG_LEVEL
#define SERIAL_LOG_LEVEL LOG_LEVEL_WRN
#endif // !SERIAL_LOG_LEVEL

#define LOG_MODULE_NAME serial
LOG_MODULE_REGISTER(LOG_MODULE_NAME, SERIAL_LOG_LEVEL);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATIC FUNCTION DECLARATIONS
static K_SEM_DEFINE(sem_wait_for_data, 0, 1);

static serial_type_t enabled_serial_types = SERIAL_TYPE_NONE;

static char const * end_character_list = NULL;
static int end_character_count = 0;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// EVENT HANDLERS
void uart_serial_callback(serial_event_t const * p_evt)
{
	switch (p_evt->type)
	{
	case SERIAL_EVENT_TYPE_NEW_DATA_RECEIVED:
		k_sem_give(&sem_wait_for_data);
		LOG_DBG("new data from uart_serial received");
		break;
	default:
		break;
	}
}

void ble_serial_callback(serial_event_t const * p_evt)
{
	switch (p_evt->type)
	{
	case SERIAL_EVENT_TYPE_NEW_DATA_RECEIVED:
		k_sem_give(&sem_wait_for_data);
		LOG_DBG("new data from uart_serial received");
		break;
	default:
		break;
	}
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATIC FUNCTIONS

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

serial_ret_code_t serial_enable(serial_type_t type)
{
	serial_ret_code_t ret_code = SERIAL_RET_CODE_ERROR_UNKNOWN;
	
	if (type & SERIAL_TYPE_UART)
	{
		ret_code = uart_serial_set_end_character_list(end_character_list, end_character_count);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to set end-character-list for uart_serial!");
			return ret_code;
		}
		
		ret_code = uart_serial_enable();
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to enable uart_serial!");
			return ret_code;
		}
		ret_code = uart_serial_add_callback(uart_serial_callback);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to add uart_serial callback!");
			return ret_code;
		}
		enabled_serial_types |= SERIAL_TYPE_UART;
	}
	
	if (type & SERIAL_TYPE_BLE)
	{
		ret_code = ble_serial_set_end_character_list(end_character_list, end_character_count);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to set end-character-list for ble_serial!");
			return ret_code;
		}
		
		ret_code = ble_serial_enable();
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to enable ble_serial!");
			return ret_code;
		}
		ret_code = ble_serial_add_callback(ble_serial_callback);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to add ble_serial callback!");
			return ret_code;
		}
		enabled_serial_types |= SERIAL_TYPE_BLE;
	}
	
	return ret_code;
}

serial_ret_code_t serial_disable(serial_type_t type)
{
	serial_ret_code_t ret_code = SERIAL_RET_CODE_SUCCESS;
	
	if (type & enabled_serial_types & SERIAL_TYPE_UART)
	{
		ret_code = uart_serial_disable();
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to disable uart_serial!");
			return ret_code;
		}
		ret_code = uart_serial_remove_callback(uart_serial_callback);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to remove uart_serial callback!");
			return ret_code;
		}
		enabled_serial_types &= ~SERIAL_TYPE_UART;
	}
	
	if (type & enabled_serial_types & SERIAL_TYPE_BLE)
	{
		ret_code = ble_serial_disable();
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to disable ble_serial!");
			return ret_code;
		}
		ret_code = ble_serial_remove_callback(uart_serial_callback);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to remove ble_serial callback!");
			return ret_code;
		}
		enabled_serial_types &= ~SERIAL_TYPE_BLE;
	}
	
	return ret_code;
}

serial_line_t const * serial_get_line(k_timeout_t timeout)
{
	static serial_line_t empty_line = { 
		.len = 0,
		.p_data = NULL,
	};
	while (true)
	{
		serial_line_t const * p_line = &empty_line;
		
		if (enabled_serial_types & SERIAL_TYPE_UART)
		{
			p_line = uart_serial_get_line(K_NO_WAIT);
			if (p_line->len > 0) return p_line;
		}
		
		if (enabled_serial_types & SERIAL_TYPE_BLE)
		{
			p_line = ble_serial_get_line(K_NO_WAIT);
			if (p_line->len > 0) return p_line;
		}
		
		if (k_sem_take(&sem_wait_for_data, timeout) != 0)
		{
			LOG_DBG("timeout reached");
			k_sleep(K_MSEC(10));
			return &empty_line;
		}
		
	}
	
}

serial_ret_code_t serial_send(k_timeout_t timeout, char const * p_data, int len)
{
	serial_ret_code_t ret_code = SERIAL_RET_CODE_ERROR_UNKNOWN;
	
	if (enabled_serial_types & SERIAL_TYPE_UART)
	{
		ret_code = uart_serial_send(timeout, p_data, len);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to send over uart_serial (code: %d)", ret_code);
		}
	}
	
	if (enabled_serial_types & SERIAL_TYPE_BLE)
	{
		ret_code = ble_serial_send(timeout, p_data, len);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to send over ble_serial (code: %d)", ret_code);
		}
	}
	
	return ret_code;
}

serial_ret_code_t serial_sendf(k_timeout_t timeout, char const * format, ...)
{
	va_list args;
	va_start(args, format);
	serial_ret_code_t ret_code = SERIAL_RET_CODE_ERROR_UNKNOWN;
	
	if (enabled_serial_types & SERIAL_TYPE_UART)
	{
		ret_code = uart_serial_vsendf(timeout, format, args);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to send over uart_serial (code: %d)", ret_code);
		}
	}
	
	if (enabled_serial_types & SERIAL_TYPE_BLE)
	{
		ret_code = ble_serial_vsendf(timeout, format, args);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to send over ble_serial (code: %d)", ret_code);
		}
	}
	
	va_end(args);
	return ret_code;
}

serial_ret_code_t serial_set_end_character_list(char const * p_list, int len)
{
	serial_ret_code_t ret_code = SERIAL_RET_CODE_SUCCESS;
	
	end_character_list = p_list;
	end_character_count = len;
	
	if (enabled_serial_types & SERIAL_TYPE_UART)
	{
		ret_code = uart_serial_set_end_character_list(p_list, len);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to update uart_serial end-character list!");
			return ret_code;
		}
	}
	
	if (enabled_serial_types & SERIAL_TYPE_BLE)
	{
		ret_code = ble_serial_set_end_character_list(p_list, len);
		if (ret_code != SERIAL_RET_CODE_SUCCESS)
		{
			LOG_ERR("unable to update uart_serial end-character list!");
			return ret_code;
		}
	}
	
	return ret_code;
}