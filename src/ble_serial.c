#include "ble_serial.h"

#include <stdio.h>

#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/nus.h>

#include "serial_internal.h"


#ifdef CONFIG_BT
#ifdef CONFIG_BT_PERIPHERAL
#ifdef CONFIG_BT_NUS
#ifdef CONFIG_BT_DEVICE_NAME
#if CONFIG_BT==1
#if CONFIG_BT_PERIPHERAL==1
#if CONFIG_BT_NUS==1
#define BLE_SERIAL_REQUIREMENTS_FULLFILLED 1
#endif
#endif
#endif
#endif
#endif
#endif
#endif

#ifdef BLE_SERIAL_REQUIREMENTS_FULLFILLED
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DEFINITIONS AND STATIC VARIABLES
#ifndef BLE_SERIAL_LOG_LEVEL
#ifdef SERIAL_LOG_LEVEL
#define BLE_SERIAL_LOG_LEVEL SERIAL_LOG_LEVEL
#else
#define BLE_SERIAL_LOG_LEVEL LOG_LEVEL_WRN
#endif // SERIAL_LOG_LEVEL
#endif // !BLE_SERIAL_LOG_LEVEL

#define LOG_MODULE_NAME ble_serial
LOG_MODULE_REGISTER(LOG_MODULE_NAME, BLE_SERIAL_LOG_LEVEL);

#ifndef BLE_SERIAL_INPUT_BUFFER_SIZE
#ifdef SERIAL_INPUT_BUFFER_SIZE
#define BLE_SERIAL_INPUT_BUFFER_SIZE SERIAL_INPUT_BUFFER_SIZE
#else
#define BLE_SERIAL_INPUT_BUFFER_SIZE 256
#endif // SERIAL_INPUT_BUFFER_SIZE
#endif // !BLE_SERIAL_BUFFER_SIZE

#ifndef BLE_SERIAL_OUTPUT_BUFFER_SIZE
#ifdef SERIAL_OUTPUT_BUFFER_SIZE
#define BLE_SERIAL_OUTPUT_BUFFER_SIZE SERIAL_OUTPUT_BUFFER_SIZE
#else
#define BLE_SERIAL_OUTPUT_BUFFER_SIZE 256
#endif // SERIAL_OUTPUT_BUFFER_SIZE
#endif // !BLE_SERIAL_BUFFER_SIZE

#ifndef BLE_SERIAL_CALLBACK_LIMIT
#define BLE_SERIAL_CALLBACK_LIMIT 1
#endif // !BLE_SERIAL_CALLBACK_LIMIT

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(CONFIG_BT_DEVICE_NAME) - 1)

static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static K_SEM_DEFINE(sem_wait_init, 0, 1);
static K_SEM_DEFINE(sem_data_ready, 0, 1);
static K_SEM_DEFINE(sem_wait_for_tx, 1, 1);

static bool enabled = false;
static char input_buffer[BLE_SERIAL_INPUT_BUFFER_SIZE + 1];
static char output_buffer[BLE_SERIAL_OUTPUT_BUFFER_SIZE + 1];
static char line_buffer[BLE_SERIAL_INPUT_BUFFER_SIZE + 1];

static serial_internal_line_t line = { 
	.mutable = { 
	.len = 0,
	.p_buffer = line_buffer,
	},
};

static int start_index = 0;
static int bytes_in_buffer = 0;
static int bt_data_len = 20;

static char const * end_character_list = NULL;
static int end_character_count = 0;

static serial_event_callback_t callback_list[BLE_SERIAL_CALLBACK_LIMIT] = { NULL };
static serial_event_t event;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATIC FUNCTION DECLARATIONS
static void fire_callbacks(serial_event_t const * p_evt);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// EVENT HANDLERS
static void bt_ready(int err)
{
	if (err)
	{
		LOG_ERR("bt_enable returned %d", err);
	}
	else
	{
		k_sem_give(&sem_wait_init);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		LOG_ERR("Connect failed (err: %d)", err);
	}
	else
	{
		LOG_INF("Connected");
	}
	LOG_DBG("MTU size is: %d", bt_gatt_get_mtu(conn));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (Reason: %d)", reason);
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	LOG_DBG("Connection parameters requested: \
	            \n\tInterval-Min: %d \
	            \n\tInterval-Max: %d \
	            \n\tLatency: %d \
	            \n\tTimeout: %d",
		param->interval_min,
		param->interval_max,
		param->latency,
		param->timeout);
    
	LOG_DBG("MTU size is: %d", bt_gatt_get_mtu(conn));

	return true;
}

static void gatt_exchange_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	LOG_DBG("MTU size after exchange is: %d", bt_gatt_get_mtu(conn));
	bt_data_len = bt_gatt_get_mtu(conn) - 3;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
	LOG_DBG("Connection parameters updated: \
	        \n\tInterval: %d \
	        \n\tLatency: %d \
	        \n\tTimeout: %d",
		interval,
		latency,
		timeout);
	
#ifdef CONFIG_BT_GATT_CLIENT
#if CONFIG_BT_GATT_CLIENT==1
	LOG_DBG("MTU size is: %d", bt_gatt_get_mtu(conn));
	static struct bt_gatt_exchange_params params = {
		.func = gatt_exchange_cb,
	};

	bt_gatt_exchange_mtu(conn, &params);
#endif
#endif
}

static void nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	int bytes_to_copy = len;
	while (bytes_to_copy > 0)
	{
		int buffer_index = (start_index + bytes_in_buffer) % BLE_SERIAL_INPUT_BUFFER_SIZE;
		int free_space = (BLE_SERIAL_INPUT_BUFFER_SIZE - buffer_index);
		int part_to_copy = (len < free_space) ? len : free_space;
		memcpy(input_buffer + buffer_index, data, part_to_copy);
		bytes_to_copy -= part_to_copy;
		bytes_in_buffer += part_to_copy;
	}
	k_sem_give(&sem_data_ready);
	LOG_DBG("Received %d bytes, %d bytes in buffer", len, bytes_in_buffer);
	event.type = SERIAL_EVENT_TYPE_NEW_DATA_RECEIVED;
	event.data.new_data.count = len;
	event.data.new_data.p_buf = data;
	fire_callbacks(&event);
}

static void nus_sent(struct bt_conn *conn)
{
	k_sem_give(&sem_wait_for_tx);
}

static void nus_send_enabled(enum bt_nus_send_status status)
{
	
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATIC FUNCTIONS
static void fire_callbacks(serial_event_t const * p_evt)
{
	for (int i = 0; i < BLE_SERIAL_CALLBACK_LIMIT; i++)
	{
		if (callback_list[i] != NULL)
		{
			callback_list[i](p_evt);
		}
	}
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

serial_ret_code_t ble_serial_add_callback(serial_event_callback_t callback)
{
	for (int i = 0; i < BLE_SERIAL_CALLBACK_LIMIT; i++)
	{
		if (callback_list[i] == NULL)
		{
			callback_list[i] = callback;
			return SERIAL_RET_CODE_SUCCESS;
		}
	}
	return SERIAL_RET_CODE_ERROR_NO_MEMORY;
}

serial_ret_code_t ble_serial_remove_callback(serial_event_callback_t callback)
{
	for (int i = 0; i < BLE_SERIAL_CALLBACK_LIMIT; i++)
	{
		if (callback_list[i] == callback)
		{
			callback_list[i] = NULL;
			return SERIAL_RET_CODE_SUCCESS;
		}
	}
	return SERIAL_RET_CODE_ERROR_NO_MEMORY;
}

serial_ret_code_t ble_serial_enable()
{
	k_sem_reset(&sem_wait_init);
	
	static struct bt_conn_cb connection_callbacks = {
		.connected = connected,
		.disconnected = disconnected,
		.le_param_req = le_param_req,
		.le_param_updated = le_param_updated,
	};
	bt_conn_cb_register(&connection_callbacks);
	
	int err;
	err = bt_enable(bt_ready);
	if (err == -EALREADY)
	{
		LOG_INF("bt already enabled, will instead attach nus service to existing ble interface");
		return ble_serial_attach();
	}
	else if (err)
	{
		LOG_ERR("bt_enable returned %d", err);
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
	err = k_sem_take(&sem_wait_init, K_MSEC(10));
	if (err)
	{
		LOG_ERR("bt_enable timed out %d", err);
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
	
	static struct bt_nus_cb nus_callbacks = {
		.received = nus_received,
		.sent = nus_sent,
		.send_enabled = nus_send_enabled,
	};
	err = bt_nus_init(&nus_callbacks);
	if (err)
	{
		LOG_ERR("could not init nus (err = %d)", err);
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
	
	struct bt_le_adv_param * adv_params = BT_LE_ADV_CONN;
	adv_params->interval_min = BT_GAP_ADV_FAST_INT_MIN_1;
	adv_params->interval_max = BT_GAP_ADV_FAST_INT_MAX_1;
	err = bt_le_adv_start(adv_params, advertising_data, ARRAY_SIZE(advertising_data), NULL, 0);
	if (err)
	{
		LOG_ERR("could not start advertising (err = %d)", err);
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
	
	k_sem_give(&sem_wait_for_tx);
	enabled = true;
	
	return SERIAL_RET_CODE_SUCCESS;
}

serial_ret_code_t ble_serial_attach()
{
	static struct bt_conn_cb connection_callbacks = {
		.connected = connected,
		.disconnected = disconnected,
		.le_param_req = le_param_req,
		.le_param_updated = le_param_updated,
	};
	bt_conn_cb_register(&connection_callbacks);
	
	int err;
	static struct bt_nus_cb nus_callbacks = {
		.received = nus_received,
		.sent = nus_sent,
		.send_enabled = nus_send_enabled,
	};
	err = bt_nus_init(&nus_callbacks);
	if (err)
	{
		LOG_ERR("could not init nus (err = %d)", err);
		return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
	}
	
	k_sem_give(&sem_wait_for_tx);
	enabled = true;
	
	return SERIAL_RET_CODE_SUCCESS;
}

serial_ret_code_t ble_serial_disable()
{
	
	return SERIAL_RET_CODE_ERROR_UNKNOWN;
}

serial_ret_code_t ble_serial_detach()
{
	
	return SERIAL_RET_CODE_ERROR_UNKNOWN;
}

serial_line_t const * ble_serial_get_line(k_timeout_t timeout)
{
	LOG_DBG("getting next line");
	line.mutable.len = 0;
	if (!enabled)
	{
		LOG_ERR("ble_serial not enabled");
		return &(line.fixed);
	}
	
	return serial_internal_get_line(&sem_data_ready, timeout, &line, input_buffer, BLE_SERIAL_INPUT_BUFFER_SIZE, &start_index, &bytes_in_buffer, end_character_list, end_character_count, fire_callbacks);
}

serial_ret_code_t ble_serial_send(k_timeout_t timeout, char const * p_data, int len)
{
	if (len == 0) return SERIAL_RET_CODE_SUCCESS;
	
	if (k_sem_take(&sem_wait_for_tx, timeout) != 0)
	{
		LOG_WRN("uart tx busy");
		return SERIAL_RET_CODE_ERROR_BUSY;
	}
	
	serial_ret_code_t ret_code = SERIAL_RET_CODE_SUCCESS;
	int bytes_sent = 0;
	while (bytes_sent < len)
	{
		int bytes_to_send = ((len - bytes_sent) < bt_data_len) ? (len - bytes_sent) : bt_data_len;
		int err = bt_nus_send(NULL, p_data + bytes_sent, bytes_to_send);
		if (err != 0)
		{
			k_sem_give(&sem_wait_for_tx);
			if (err != -ENOTCONN)
			{
				LOG_ERR("bt_nus_send returned error: %d", err);
				ret_code = SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
			}
			break;
		}
		bytes_sent += bytes_to_send;
		LOG_INF("%d bytes sent (of %d)", bytes_sent, len);
	}
	
	return ret_code;
}

serial_ret_code_t ble_serial_vsendf(k_timeout_t timeout, const char * format, va_list args)
{
	if (k_sem_take(&sem_wait_for_tx, timeout) != 0)
	{
		LOG_WRN("uart tx busy");
		return SERIAL_RET_CODE_ERROR_BUSY;
	}
	k_sem_give(&sem_wait_for_tx);
	
	int len = vsnprintf(output_buffer, BLE_SERIAL_OUTPUT_BUFFER_SIZE, format, args);
	if ((len < 0) || (len > BLE_SERIAL_OUTPUT_BUFFER_SIZE)) return SERIAL_RET_CODE_ERROR_BUFFER_FULL;
	return ble_serial_send(timeout, output_buffer, len);
}

serial_ret_code_t ble_serial_sendf(k_timeout_t timeout, char const * format, ...)
{
	va_list args;
	va_start(args, format);
	serial_ret_code_t result = ble_serial_vsendf(timeout, format, args);
	va_end(args);
	return result;
}

serial_ret_code_t ble_serial_set_end_character_list(char const * p_list, int len)
{
	end_character_count = len;
	end_character_list = p_list;
	LOG_INF("end character list updated");
	return SERIAL_RET_CODE_SUCCESS;
}


#else
#ifndef BLE_SERIAL_LOG_LEVEL
#ifdef SERIAL_LOG_LEVEL
#define BLE_SERIAL_LOG_LEVEL SERIAL_LOG_LEVEL
#else
#define BLE_SERIAL_LOG_LEVEL LOG_LEVEL_WRN
#endif // SERIAL_LOG_LEVEL
#endif // !BLE_SERIAL_LOG_LEVEL

#define LOG_MODULE_NAME ble_serial
LOG_MODULE_REGISTER(LOG_MODULE_NAME, BLE_SERIAL_LOG_LEVEL);
serial_ret_code_t ble_serial_add_callback(serial_event_callback_t callback)
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_remove_callback(serial_event_callback_t callback)
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_enable()
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_attach()
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_disable()
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_detach()
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_line_t const * ble_serial_get_line(k_timeout_t timeout)
{
	static serial_line_t const line = { 0 };
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return &line;
}

serial_ret_code_t ble_serial_send(k_timeout_t timeout, char const * p_data, int len)
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_vsendf(k_timeout_t timeout, const char * format, va_list args)
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_sendf(k_timeout_t timeout, char const * format, ...)
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}

serial_ret_code_t ble_serial_set_end_character_list(char const * p_list, int len)
{
	LOG_ERR("this module uses nordic uart service (CONFIG_BT=y,CONFIG_BT_PERIPHERAL=y,CONFIG_BT_NUS=y,CONFIG_BT_DEVICE_NAME=\"[name]\")");
	return SERIAL_RET_CODE_ERROR_DEVICE_NOT_READY;
}
#endif