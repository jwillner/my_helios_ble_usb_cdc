#ifndef HELIOS_BLE_H_
#define HELIOS_BLE_H_

#include "stdint.h"
#include <zephyr/kernel.h>

typedef enum helios_ble_return_code_e
{
	HELIOS_BLE_RETURN_CODE_SUCCESS,
    HELIOS_BLE_RETURN_CODE_ERROR,
} helios_ble_return_code_t;

typedef enum helios_ble_pattern_e
{
	PATTERN_TRAIL,
	PATTERN_PRECEDE,
	PATTERN_SYNC,
	PATTERN_ON,
	PATTERN_OFF,
	PATTERN_KNIGHT_RIDER,
} helios_ble_pattern_t;

typedef struct helios_ble_data_s {
    int64_t network_time;
    helios_ble_pattern_t pattern;
    uint8_t node_count;
    uint8_t node_id;
} helios_ble_data_t;

helios_ble_return_code_t helios_ble_enable();
helios_ble_return_code_t helios_ble_send(helios_ble_data_t const * p_data);
helios_ble_data_t const * helios_ble_receive(k_timeout_t timeout);

#endif /* HELIOS_BLE_H_ */