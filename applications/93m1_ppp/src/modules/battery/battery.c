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
#include "battery.h"
#include "lp803448_model.h"

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

#define CHANNEL_LIST(X) X(battery_chan, struct battery_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, battery, 0);

CHANNEL_LIST(ADD_OBSERVERS)

static int read_sensors(const struct device *charger, float *voltage, float *current,
			float *temp, int32_t *chg_status, bool *vbus);
static void update_charge_state(int32_t chg_status, int32_t *prev);
static int fuel_gauge_setup(const struct device *charger);
static void battery_sample(const struct device *charger, int64_t *ref_time,
			   int32_t *prev_chg_status);
static void battery_thread(void);

static int read_sensors(const struct device *charger, float *voltage, float *current,
			float *temp, int32_t *chg_status, bool *vbus)
{
	int err;
	struct sensor_value val = {0};

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

	err = sensor_channel_get(charger, (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_STATUS,
				 &val);
	if (err) {
		return err;
	}
	*chg_status = val.val1;

	struct sensor_value vbus_val;

	err = sensor_attr_get(charger, (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
			      (enum sensor_attribute)SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT,
			      &vbus_val);
	*vbus = (err == 0) && (vbus_val.val1 != 0);

	return 0;
}

static void update_charge_state(int32_t chg_status, int32_t *prev)
{
	union nrf_fuel_gauge_ext_state_info_data ext;

	if (chg_status == *prev) {
		return;
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

	(void)nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE,
					      &ext);
}

static int fuel_gauge_setup(const struct device *charger)
{
	int err;
	int32_t chg_status;
	bool vbus;
	struct nrf_fuel_gauge_init_parameters params = { .model = &battery_model };
	struct sensor_value desired;

	err = read_sensors(charger, &params.v0, &params.i0, &params.t0, &chg_status, &vbus);
	if (err) {
		return err;
	}

	/* The Memfault --wrap on nrf_fuel_gauge_init() marks the gauge ready. */
	err = nrf_fuel_gauge_init(&params, NULL);
	if (err) {
		return err;
	}

	/* Seed an initial SoC so soc_get() is valid before the first loop process(). */
	float soc;

	(void)nrf_fuel_gauge_process(params.v0, params.i0, params.t0, 0.0f, &soc, NULL);

	/* Seed charge-current limits for time-to-full prediction. */
	if (sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT,
				&desired) == 0) {
		float limit = sensor_value_to_float(&desired);
		union nrf_fuel_gauge_ext_state_info_data ext = { .charge_current_limit = limit };

		(void)nrf_fuel_gauge_ext_state_update(
			NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_CURRENT_LIMIT, &ext);
		ext.charge_term_current = limit / 10.0f;
		(void)nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_TERM_CURRENT,
						      &ext);
	}

	return 0;
}

/* Owns the fuel gauge: init once, then keep it fed. Memfault reads SoC/SoH via
 * nrf_fuel_gauge_soc_get()/soh_get() on its own heartbeat.
 */
static void battery_sample(const struct device *charger, int64_t *ref_time,
			   int32_t *prev_chg_status)
{
	int err;
	float voltage;
	float current;
	float temp;
	float soc;
	int32_t chg_status;
	bool vbus;

	err = read_sensors(charger, &voltage, &current, &temp, &chg_status, &vbus);
	if (err) {
		LOG_WRN("read_sensors, error: %d", err);
		return;
	}

	(void)nrf_fuel_gauge_ext_state_update(
		vbus ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
		     : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED, NULL);
	update_charge_state(chg_status, prev_chg_status);

	float delta = (float)k_uptime_delta(ref_time) / 1000.0f;

	err = nrf_fuel_gauge_process(voltage, current, temp, delta, &soc, NULL);
	if (err) {
		LOG_WRN("nrf_fuel_gauge_process, error: %d", err);
		return;
	}

	LOG_DBG("SoC %d%%, %d mV, %s", (int)soc, (int)(voltage * 1000),
		vbus ? "VBUS" : "battery");
}

static void battery_thread(void)
{
	int err;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	int64_t ref_time;
	int32_t prev_chg_status = -1;
	const struct device *const charger = DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

	if (!device_is_ready(charger)) {
		LOG_ERR("Charger device not ready");
		return;
	}

	err = fuel_gauge_setup(charger);
	if (err) {
		LOG_ERR("fuel_gauge_setup, error: %d", err);
		return;
	}
	ref_time = k_uptime_get();

	while (true) {
		err = zbus_sub_wait_msg(&battery, &chan, msg_buf, K_FOREVER);
		if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			return;
		}

		battery_sample(charger, &ref_time, &prev_chg_status);
	}
}

K_THREAD_DEFINE(battery_tid, CONFIG_APP_BATTERY_THREAD_STACK_SIZE, battery_thread,
		NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
