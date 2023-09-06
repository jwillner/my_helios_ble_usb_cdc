#include "uart_serial.h"

#include <stdio.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>

#include "serial_internal.h"

#ifndef UART_SERIAL_INSTANCE
#define UART_SERIAL_INSTANCE DT_CHOSEN(zephyr_console)
#endif // !UART_SERIAL_INSTANCE

#ifdef CONFIG_SERIAL
#if CONFIG_SERIAL==1

#if DT_NODE_HAS_COMPAT(UART_SERIAL_INSTANCE, zephyr_cdc_acm_uart)
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#if CONFIG_UART_INTERRUPT_DRIVEN==1
#define UART_SERIAL_REQUIREMENTS_FULLFILLED 1
#endif
#endif
#else
#ifdef CONFIG_UART_ASYNC_API
#if CONFIG_UART_ASYNC_API==1
#define UART_SERIAL_REQUIREMENTS_FULLFILLED 1
#endif
#endif
#endif
#endif
#endif

#ifdef UART_SERIAL_REQUIREMENTS_FULLFILLED
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DEFINITIONS AND STATIC VARIABLES
#ifndef UART_SERIAL_LOG_LEVEL
#ifdef SERIAL_LOG_LEVEL
#define UART_SERIAL_LOG_LEVEL SERIAL_LOG_LEVEL
#else
#define UART_SERIAL_LOG_LEVEL LOG_LEVEL_WRN
#endif // SERIAL_LOG_LEVEL
#endif // !UART_SERIAL_LOG_LEVEL

#define LOG_MODULE_NAME uart_serial
LOG_MODULE_REGISTER(LOG_MODULE_NAME, UART_SERIAL_LOG_LEVEL);

#ifndef UART_SERIAL_INPUT_BUFFER_SIZE
#ifdef SERIAL_INPUT_BUFFER_SIZE
#define UART_SERIAL_INPUT_BUFFER_SIZE SERIAL_INPUT_BUFFER_SIZE
#else
#define UART_SERIAL_INPUT_BUFFER_SIZE 256
#endif // SERIAL_INPUT_BUFFER_SIZE
#endif // !UART_SERIAL_BUFFER_SIZE
#define PARTITION_SIZE (UART_SERIAL_INPUT_BUFFER_SIZE / 2)

#if UART_SERIAL_INPUT_BUFFER_SIZE % 2 != 0
#error "UART_SERIAL_INPUT_BUFFER_SIZE should be divisible by 2, to ensure proper partition buffers for uart"
#endif

#ifndef UART_SERIAL_OUTPUT_BUFFER_SIZE
#ifdef SERIAL_OUTPUT_BUFFER_SIZE
#define UART_SERIAL_OUTPUT_BUFFER_SIZE SERIAL_OUTPUT_BUFFER_SIZE
#else
#define UART_SERIAL_OUTPUT_BUFFER_SIZE 256
#endif // SERIAL_OUTPUT_BUFFER_SIZE
#endif // !UART_SERIAL_BUFFER_SIZE

#ifndef UART_SERIAL_CALLBACK_LIMIT
#define UART_SERIAL_CALLBACK_LIMIT 1
#endif // !UART_SERIAL_CALLBACK_LIMIT

#define RECEIVE_TIMEOUT 100

static const struct device *const uart = DEVICE_DT_GET(UART_SERIAL_INSTANCE);
static K_SEM_DEFINE(sem_data_ready, 0, 1);
static K_SEM_DEFINE(sem_wait_for_disable, 0, 1);
static K_SEM_DEFINE(sem_wait_for_tx, 1, 1);

static char input_buffer[UART_SERIAL_INPUT_BUFFER_SIZE + 1];
static char output_buffer[UART_SERIAL_OUTPUT_BUFFER_SIZE + 1];
static char line_buffer[UART_SERIAL_INPUT_BUFFER_SIZE + 1];
static serial_internal_line_t line = { 
	.mutable = { 
		.len = 0,
		.p_buffer = line_buffer,
	},
};

static int start_index = 0;
static volatile int bytes_in_buffer = 0;
static volatile int input_buffer_index = 0;

static char const * end_character_list = NULL;
static int end_character_count = 0;
static bool enabled = false;

static serial_event_callback_t callback_list[UART_SERIAL_CALLBACK_LIMIT] = { NULL };
static serial_event_t event;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATIC FUNCTION DECLARATIONS
static void fire_callbacks(serial_event_t const * p_evt);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// EVENT HANDLERS
#if !(DT_NODE_HAS_COMPAT(UART_SERIAL_INSTANCE, zephyr_cdc_acm_uart))
static char * buffer1 = input_buffer;
static char * buffer2 = input_buffer + PARTITION_SIZE;
static char * next_buffer;
void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	switch (evt->type)
	{
	case UART_RX_RDY:
		{
			bytes_in_buffer += evt->data.rx.len;
			k_sem_give(&sem_data_ready);
			event.type = SERIAL_EVENT_TYPE_NEW_DATA_RECEIVED;
			event.data.new_data.count = evt->data.rx.len;
			event.data.new_data.p_buf = evt->data.rx.buf + evt->data.rx.offset;
			fire_callbacks(&event);
			LOG_DBG("received %d bytes, %d bytes in buffer", evt->data.rx.len, bytes_in_buffer);
			break;
		}
	case UART_RX_STOPPED:
		LOG_DBG("RX stopped");
		break;
	case UART_RX_DISABLED:
		enabled = false;
		k_sem_give(&sem_wait_for_disable);
		LOG_INF("uart_serial disabled");
		break;
	case UART_RX_BUF_REQUEST:
		LOG_DBG("RX buffer requested");
		uart_rx_buf_rsp(dev, next_buffer, PARTITION_SIZE);
		LOG_DBG("new buffer: 0x%032x", (uint32_t)next_buffer);
		next_buffer = (next_buffer == buffer1) ? buffer2 : buffer1;
		break;
	case UART_RX_BUF_RELEASED:
		LOG_DBG("RX buffer released");
		break;
	case UART_TX_DONE:
		k_sem_give(&sem_wait_for_tx);
		LOG_INF("%d bytes sent", evt->data.tx.len);
		break;
	case UART_TX_ABORTED:
		k_sem_give(&sem_wait_for_tx);
		LOG_DBG("TX aborted");
		break;
	}
}
#else
void serial_cb(const struct device *dev, void *user_data)
{
	if (!uart_irq_update(dev)) {
		return;
	}

	//read input
	int bytes_received = 0;
	while (uart_irq_rx_ready(dev))
	{
		uart_fifo_read(dev, input_buffer + input_buffer_index, 1);
		bytes_received++;
		input_buffer_index = (input_buffer_index + 1) % UART_SERIAL_INPUT_BUFFER_SIZE;
	}
	if (bytes_received > 0)
	{
		bytes_in_buffer += bytes_received;
		k_sem_give(&sem_data_ready);
		event.type = SERIAL_EVENT_TYPE_NEW_DATA_RECEIVED;
		event.data.new_data.count = 0;
		event.data.new_data.p_buf = NULL;
		fire_callbacks(&event);
		LOG_DBG("received %d bytes, %d bytes in buffer", bytes_received, bytes_in_buffer);
	}
	
	if (uart_irq_tx_complete(dev))
	{
		k_sem_give(&sem_wait_for_tx);
		LOG_DBG("data sent");
	}
}
#endif
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATIC FUNCTIONS
static void fire_callbacks(serial_event_t const * p_evt)
{
	for (int i = 0; i < UART_SERIAL_CALLBACK_LIMIT; i++)
	{
		if (callback_list[i] != NULL)
		{
			callback_list[i](p_evt);
		}
	}
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

serial_ret_code_t uart_serial_add_callback(serial_event_callback_t callback)
{
	for (int i = 0; i < UART_SERIAL_CALLBACK_LIMIT; i++)
	{
		if (callback_list[i] == NULL)
		{
			callback_list[i] = callback;
			return SERIAL_RET_CODE_SUCCESS;
		}
	}
	return SERIAL_RET_CODE_ERROR_NO_MEMORY;
}

serial_ret_code_t uart_serial_remove_callback(serial_event_callback_t callback)
{
	for (int i = 0; i < UART_SERIAL_CALLBACK_LIMIT; i++)
	{
		if (callback_list[i] == callback)
		{
			callback_list[i] = NULL;
			return SERIAL_RET_CODE_SUCCESS;
		}
	}
	return SERIAL_RET_CODE_ERROR_NO_MEMORY;
}

serial_ret_code_t uart_serial_enable()
{
	if (enabled) return SERIAL_RET_CODE_SUCCESS;
	
	if (!device_is_ready(uart)) {
		LOG_ERR("UART device not ready!");
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
	
#if !(DT_NODE_HAS_COMPAT(UART_SERIAL_INSTANCE, zephyr_cdc_acm_uart))
	int err = uart_callback_set(uart, uart_callback, NULL);
	if (err)
	{
		LOG_ERR("unable to set uart_callback, make sure CONFIG_SERIAL=y and CONFIG_UART_ASYNC_API=y are set!");
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
	
	next_buffer = buffer2;
	err = uart_rx_enable(uart, buffer1, PARTITION_SIZE, RECEIVE_TIMEOUT);
	if (err)
	{
		LOG_ERR("unable to enable rx");
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
#else
	uart_irq_callback_user_data_set(uart, serial_cb, NULL);	
	uart_irq_rx_enable(uart);
	uart_irq_tx_enable(uart);
#endif
	
	k_sem_give(&sem_wait_for_tx);
	input_buffer[UART_SERIAL_INPUT_BUFFER_SIZE] = '\0';
	enabled = true;
	
	LOG_INF("uart_serial enabled");
	return SERIAL_RET_CODE_SUCCESS;
}

serial_ret_code_t uart_serial_disable()
{
	if (!enabled) return SERIAL_RET_CODE_SUCCESS;
	
#if !(DT_NODE_HAS_COMPAT(UART_SERIAL_INSTANCE, zephyr_cdc_acm_uart))
	uart_rx_disable(uart);
	if (k_sem_take(&sem_wait_for_disable, K_MSEC(10)) != 0)
	{
		LOG_ERR("unable to disable uart_serial");
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
#else
	uart_irq_rx_disable(uart);
	uart_irq_tx_disable(uart);
#endif
	
	return SERIAL_RET_CODE_SUCCESS;
}

serial_line_t const * uart_serial_get_line(k_timeout_t timeout)
{
	LOG_DBG("getting next line");
	line.mutable.len = 0;
	if (!enabled)
	{
		LOG_ERR("uart_serial not enabled");
		return &(line.fixed);
	}
	
	return serial_internal_get_line(&sem_data_ready, timeout, &line, input_buffer, UART_SERIAL_INPUT_BUFFER_SIZE, &start_index, &bytes_in_buffer, end_character_list, end_character_count, fire_callbacks);
}

serial_ret_code_t uart_serial_send(k_timeout_t timeout, char const * p_data, int len)
{
	if (len == 0) return SERIAL_RET_CODE_SUCCESS;
	
	if (k_sem_take(&sem_wait_for_tx, timeout) != 0)
	{
		LOG_WRN("uart tx busy");
		return SERIAL_RET_CODE_ERROR_BUSY;
	}
#if !(DT_NODE_HAS_COMPAT(UART_SERIAL_INSTANCE, zephyr_cdc_acm_uart))
	int err = uart_tx(uart, p_data, len, 100);
#else
	int err = uart_fifo_fill(uart, p_data, len);
#endif
	
	if (err < 0)
	{
		k_sem_give(&sem_wait_for_tx);
	}
	else
	{
		err = 0;
	}
	
	switch (err)
	{
	case 0:
		return SERIAL_RET_CODE_SUCCESS;
	case -EBUSY:
		return SERIAL_RET_CODE_ERROR_BUSY;
	case -ENOTSUP:
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	default:
		return SERIAL_RET_CODE_ERROR_UNKNOWN;
	}
}

serial_ret_code_t uart_serial_sendf(k_timeout_t timeout, char const * format, ...)
{
	va_list args;
	va_start(args, format);
	serial_ret_code_t result = uart_serial_vsendf(timeout, format, args);
	va_end(args);
	return result;
}

serial_ret_code_t uart_serial_vsendf(k_timeout_t timeout, const char * format, va_list args)
{
	if (k_sem_take(&sem_wait_for_tx, timeout) != 0)
	{
		LOG_WRN("uart tx busy");
		return SERIAL_RET_CODE_ERROR_BUSY;
	}
	k_sem_give(&sem_wait_for_tx);
	
	int len = vsnprintf(output_buffer, UART_SERIAL_OUTPUT_BUFFER_SIZE, format, args);
	if ((len < 0) || (len > UART_SERIAL_OUTPUT_BUFFER_SIZE)) return SERIAL_RET_CODE_ERROR_BUFFER_FULL;
	return uart_serial_send(timeout, output_buffer, len);
}

serial_ret_code_t uart_serial_set_end_character_list(char const * p_list, int len)
{
	end_character_count = len;
	end_character_list = p_list;
	LOG_INF("end character list updated");
	return SERIAL_RET_CODE_SUCCESS;
}
#else
#ifndef UART_SERIAL_LOG_LEVEL
#ifdef SERIAL_LOG_LEVEL
#define UART_SERIAL_LOG_LEVEL SERIAL_LOG_LEVEL
#else
#define UART_SERIAL_LOG_LEVEL LOG_LEVEL_WRN
#endif // SERIAL_LOG_LEVEL
#endif // !UART_SERIAL_LOG_LEVEL

#define LOG_MODULE_NAME uart_serial
LOG_MODULE_REGISTER(LOG_MODULE_NAME, UART_SERIAL_LOG_LEVEL);

serial_ret_code_t uart_serial_add_callback(serial_event_callback_t callback)
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t uart_serial_remove_callback(serial_event_callback_t callback)
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t uart_serial_enable()
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t uart_serial_disable()
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_line_t const * uart_serial_get_line(k_timeout_t timeout)
{
	static serial_line_t const line = { 0 };
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return &line;
}

serial_ret_code_t uart_serial_send(k_timeout_t timeout, char const * p_data, int len)
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t uart_serial_sendf(k_timeout_t timeout, char const * format, ...)
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t uart_serial_vsendf(k_timeout_t timeout, const char * format, va_list args)
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t uart_serial_set_end_character_list(char const * p_list, int len)
{
	LOG_ERR("this module uses the async api (CONFIG_SERIAL=y,CONFIG_UART_ASYNC_API=y)");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}
#endif