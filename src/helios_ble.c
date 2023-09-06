#include "helios_ble.h"

#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/scan.h>


#define LOG_MODULE_NAME helios_ble
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define BT_LE_OPEN_NETWORK_UUID { 0x2c, 0x4c, 0xc0, 0xf0, 0x37, 0x83, 0x84, 0xec, 0x97, 0xac, 0xfb, 0x32 }

typedef struct service_data_s
{
    uint8_t uuid[12];
    uint32_t network_id;
    helios_ble_data_t helios_data;
} service_data_t;
static service_data_t service_data = {
    .uuid = BT_LE_OPEN_NETWORK_UUID,
    .network_id = 0x12345678,
    .helios_data = { 0 },
};
static service_data_t received_service_data;

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(CONFIG_BT_DEVICE_NAME) - 1)
static const struct bt_data advertising_data[] = {
	BT_DATA(BT_DATA_SVC_DATA128, &service_data, sizeof(service_data)),
};
static struct bt_le_ext_adv *adv;
K_SEM_DEFINE(sem_data_received, 0, 1);

static bool bt_data_parser(struct bt_data * data, void * user_data)
{
	switch (data->type)
	{
//	case BT_DATA_NAME_COMPLETE:
//		if (data->data_len != DEVICE_NAME_LEN) return false;
//		if (memcmp(data->data, DEVICE_NAME, DEVICE_NAME_LEN) != 0) return false;
//		return true;
	case BT_DATA_SVC_DATA128:
		{
            //if (data->data_len != sizeof(service_data_t)) return false;
			//received data might be unaligned therefore we neeed to copy it in an aligned structure
			
			memcpy(&received_service_data, data->data, sizeof(service_data_t));
			if (memcmp(received_service_data.uuid, service_data.uuid, sizeof(service_data.uuid)) != 0) return false;
            if (received_service_data.network_id != service_data.network_id) return false;

            //LOG_INF("Service data received from %d", received_service_data.helios_data.node_id);
            k_sem_give(&sem_data_received);
		}
		return true;
	default:
		return true;
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *buf)
{
	bt_data_parse(buf, bt_data_parser, NULL);
    //LOG_INF("advertising received: %d dB, %d bytes", rssi, buf->len);
}

static void connected_cb(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info)
{
	LOG_INF("Connected");
}

static void scanned_cb(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_scanned_info *info)
{
	LOG_INF("Scanned");
}

static void sent_cb(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info)
{
	LOG_INF("Data sent");
}


helios_ble_return_code_t helios_ble_enable()
{
    int err;

    //BLE einschalten
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bluetooth init failed (err %d)", err);
		return HELIOS_BLE_RETURN_CODE_ERROR;
	}
	LOG_INF("bluetooth initialized\n");

    //Scanner einschalten
    const struct bt_le_scan_param ble_scan_params = { 
		.interval = 0x0100,
		.window = 0x0100,
		.timeout = 0,
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
	};
	
	err = bt_le_scan_start(&ble_scan_params, scan_cb);
	if (err) {
		LOG_ERR("starting scanning failed (err %d)", err);
		return HELIOS_BLE_RETURN_CODE_ERROR;
	}

    //Advertising vorbereiten
    static struct bt_le_adv_param * adv_params;
	adv_params = BT_LE_EXT_ADV_NCONN_NAME;
	
	static struct bt_le_ext_adv_cb ext_adv_cb = {
		.connected = connected_cb,
		.scanned = scanned_cb,
		.sent = sent_cb,
	};
	
	err = bt_le_ext_adv_create(adv_params, &ext_adv_cb, &adv);
	if (err) {
		LOG_ERR("failed to create bt_le_ext_adv struct (err %d)", err);
		return HELIOS_BLE_RETURN_CODE_ERROR;
	}

    return HELIOS_BLE_RETURN_CODE_SUCCESS;
}


helios_ble_return_code_t helios_ble_send(helios_ble_data_t const * p_data)
{
    memcpy(&(service_data.helios_data), p_data, sizeof(helios_ble_data_t));
    int err = bt_le_ext_adv_set_data(adv, advertising_data, ARRAY_SIZE(advertising_data), NULL, 0);
	if (err) {
		LOG_INF("failed to set adv data (err %d)", err);
		return HELIOS_BLE_RETURN_CODE_ERROR;
	}
	struct bt_le_ext_adv_start_param ext_start_param = {
		.num_events = 1,
		.timeout = 10,
	};
	err = bt_le_ext_adv_start(adv, &ext_start_param);
	if (err) {
		LOG_INF("failed to start advertising (err %d)", err);
		return HELIOS_BLE_RETURN_CODE_ERROR;
	}

    return HELIOS_BLE_RETURN_CODE_SUCCESS;
}


helios_ble_data_t const * helios_ble_receive(k_timeout_t timeout)
{
    if(k_sem_take(&sem_data_received, timeout)) return NULL;

    return &received_service_data.helios_data;
}