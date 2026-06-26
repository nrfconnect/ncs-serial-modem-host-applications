/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <nrf_fuel_gauge.h>

#include "app_common.h"
#include "modules/cloud/cloud.h"
#include "modules/network/network.h"
#include "lp803448_model.h"
#include "battery.h"

LOG_MODULE_REGISTER(battery, CONFIG_APP_BATTERY_LOG_LEVEL);

/* nPM1300 BCHGCHARGESTATUS bits */
#define CHG_STATUS_COMPLETE_MASK BIT(1)
#define CHG_STATUS_TC_MASK       BIT(2)
#define CHG_STATUS_CC_MASK       BIT(3)
#define CHG_STATUS_CV_MASK       BIT(4)

ZBUS_CHAN_DEFINE(battery_chan,
		 struct battery_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_MSG_SUBSCRIBER_DEFINE(battery);

#define CHANNEL_LIST(X)				\
	X(network_chan, struct network_msg)	\
	X(battery_chan, struct battery_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, battery, 0);

CHANNEL_LIST(ADD_OBSERVERS)

/* Forward declarations */

static int read_sensors(const struct device *charger, float *voltage, float *current,
			float *temp, int32_t *chg_status, bool *vbus);
static int update_charge_state(int32_t chg_status, int32_t *prev);
static int fuel_gauge_setup(const struct device *charger);
static int battery_sample_and_report(const struct device *charger, int64_t *ref_time,
				     int32_t *prev_chg_status);

/* Helper functions */

static int read_sensors(const struct device *charger, float *voltage, float *current,
			float *temp, int32_t *chg_status, bool *vbus)
{
	int err;
	struct sensor_value val = {0};
	struct sensor_value vbus_val;

	err = sensor_sample_fetch(charger);
	if (err) {
		return err;
	}

	err = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &val);
	if (err) {
		return err;
	}
	*voltage = sensor_value_to_float(&val);

	err = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &val);
	if (err) {
		return err;
	}
	*temp = sensor_value_to_float(&val);

	err = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &val);
	if (err) {
		return err;
	}
	/* Fuel gauge wants charge-positive; the sensor API is discharge-negative. */
	*current = -sensor_value_to_float(&val);

	err = sensor_channel_get(charger,
				 (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &val);
	if (err) {
		return err;
	}
	*chg_status = val.val1;

	err = sensor_attr_get(charger,
			      (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
			      (enum sensor_attribute)SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT,
			      &vbus_val);
	/* VBUS read is non-fatal: assume disconnected if it cannot be read. */
	*vbus = (err == 0) && (vbus_val.val1 != 0);

	return 0;
}

static int update_charge_state(int32_t chg_status, int32_t *prev)
{
	int err;
	union nrf_fuel_gauge_ext_state_info_data ext;

	if (chg_status == *prev) {
		return 0;
	}

	*prev = chg_status;

	if (chg_status & CHG_STATUS_COMPLETE_MASK) {
		ext.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE;
	} else if (chg_status & CHG_STATUS_TC_MASK) {
		ext.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE;
	} else if (chg_status & CHG_STATUS_CC_MASK) {
		ext.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC;
	} else if (chg_status & CHG_STATUS_CV_MASK) {
		ext.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CV;
	} else {
		ext.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_IDLE;
	}

	err = nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE,
					      &ext);
	if (err) {
		LOG_ERR("nrf_fuel_gauge_ext_state_update, error: %d", err);
		return err;
	}

	return 0;
}

static int fuel_gauge_setup(const struct device *charger)
{
	int err;
	int32_t chg_status;
	bool vbus;
	struct nrf_fuel_gauge_init_parameters params = { .model = &battery_model };
	float soc;

	err = read_sensors(charger, &params.v0, &params.i0, &params.t0, &chg_status, &vbus);
	if (err) {
		LOG_ERR("read_sensors, error: %d", err);
		return err;
	}

	err = nrf_fuel_gauge_init(&params, NULL);
	if (err) {
		LOG_ERR("nrf_fuel_gauge_init, error: %d", err);
		return err;
	}

	err = nrf_fuel_gauge_process(params.v0, params.i0, params.t0, 0.0f, &soc, NULL);
	if (err) {
		LOG_ERR("nrf_fuel_gauge_process, error: %d", err);
		return err;
	}

	return 0;
}

static int battery_sample_and_report(const struct device *charger, int64_t *ref_time,
				      int32_t *prev_chg_status)
{
	float voltage;
	float current;
	float temp;
	float soc;
	int32_t chg_status;
	bool vbus;
	int err;

	err = read_sensors(charger, &voltage, &current, &temp, &chg_status, &vbus);
	if (err) {
		LOG_ERR("read_sensors, error: %d", err);
		return err;
	}

	err = nrf_fuel_gauge_ext_state_update(
		vbus ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
		     : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED, NULL);
	if (err) {
		LOG_ERR("nrf_fuel_gauge_ext_state_update, error: %d", err);
		return err;
	}

	err = update_charge_state(chg_status, prev_chg_status);
	if (err) {
		LOG_ERR("update_charge_state, error: %d", err);
		return err;
	}

	float delta = (float)k_uptime_delta(ref_time) / 1000.0f;

	err = nrf_fuel_gauge_process(voltage, current, temp, delta, &soc, NULL);
	if (err) {
		LOG_ERR("nrf_fuel_gauge_process, error: %d", err);
		return err;
	}

	struct cloud_msg msg = { .type = CLOUD_BATTERY_SAMPLE, .battery_percent = (int)soc };

	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub cloud_chan, error: %d", err);
		return err;
	}

	return 0;
}

static void battery_thread(void)
{
	int err;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	int64_t ref_time;
	int32_t prev_chg_status = -1;
	bool connected = false;
	const struct device *const charger = DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

	if (!device_is_ready(charger)) {
		LOG_ERR("Charger device not ready");
		FATAL_ERROR();
	}

	err = fuel_gauge_setup(charger);
	if (err) {
		LOG_ERR("fuel_gauge_setup, error: %d", err);
		FATAL_ERROR();
	}

	ref_time = k_uptime_get();

	while (true) {
		err = zbus_sub_wait_msg(&battery, &chan, msg_buf, K_FOREVER);
		if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
		}

		if (chan == &network_chan) {
			const struct network_msg *msg = (const struct network_msg *)msg_buf;

			connected = (msg->type == NETWORK_CONNECTED);
		} else if (chan == &battery_chan && connected) {
			err = battery_sample_and_report(charger, &ref_time, &prev_chg_status);
			if (err) {
				LOG_ERR("battery_sample_and_report, error: %d", err);
				FATAL_ERROR();
			}
		} else {
			LOG_WRN("Unhandled message in battery thread");
		}
	}
}

K_THREAD_DEFINE(battery_tid, CONFIG_APP_BATTERY_THREAD_STACK_SIZE, battery_thread,
		NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
