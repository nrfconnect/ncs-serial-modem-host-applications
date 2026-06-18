/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <nrf_fuel_gauge.h>

#include "modules/modem_at/modem_at.h"
#include "lp803448_model.h"
#include "battery.h"

LOG_MODULE_REGISTER(battery, CONFIG_APP_BATTERY_LOG_LEVEL);

/* nPM1300 BCHGCHARGESTATUS bits */
#define CHG_STATUS_COMPLETE_MASK BIT(1)
#define CHG_STATUS_TC_MASK       BIT(2)
#define CHG_STATUS_CC_MASK       BIT(3)
#define CHG_STATUS_CV_MASK       BIT(4)

static const struct device *const charger = DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

static bool gauge_ready;
static int64_t ref_time;
static int32_t prev_chg_status = -1;

static int read_sensors(float *voltage, float *current, float *temp, int32_t *chg_status,
			bool *vbus)
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

static void update_charge_state(int32_t chg_status)
{
	union nrf_fuel_gauge_ext_state_info_data ext;

	if (chg_status == prev_chg_status) {
		return;
	}
	prev_chg_status = chg_status;

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

static int fuel_gauge_setup(void)
{
	int err;
	int32_t chg_status;
	bool vbus;
	struct nrf_fuel_gauge_init_parameters params = { .model = &battery_model };
	float soc;

	err = read_sensors(&params.v0, &params.i0, &params.t0, &chg_status, &vbus);
	if (err) {
		return err;
	}

	err = nrf_fuel_gauge_init(&params, NULL);
	if (err) {
		return err;
	}

	(void)nrf_fuel_gauge_process(params.v0, params.i0, params.t0, 0.0f, &soc, NULL);

	return 0;
}

/* Sample the gauge and return state of charge in percent, or -1 on error. */
static int sample_soc(void)
{
	float voltage;
	float current;
	float temp;
	float soc;
	int32_t chg_status;
	bool vbus;

	if (read_sensors(&voltage, &current, &temp, &chg_status, &vbus)) {
		return -1;
	}

	(void)nrf_fuel_gauge_ext_state_update(
		vbus ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
		     : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED, NULL);
	update_charge_state(chg_status);

	float delta = (float)k_uptime_delta(&ref_time) / 1000.0f;

	if (nrf_fuel_gauge_process(voltage, current, temp, delta, &soc, NULL)) {
		return -1;
	}

	return (int)soc;
}

int battery_report(void)
{
	char cmd[64];
	int soc;
	int err;

	if (!gauge_ready) {
		if (!device_is_ready(charger)) {
			LOG_ERR("Charger device not ready");
			return -ENODEV;
		}
		if (fuel_gauge_setup()) {
			LOG_ERR("Fuel gauge init failed");
			return -EIO;
		}
		ref_time = k_uptime_get();
		gauge_ready = true;
	}

	soc = sample_soc();
	if (soc < 0) {
		return -EIO;
	}

	(void)snprintf(cmd, sizeof(cmd),
		       "AT%%NRFCLOUDMESSAGE={\"appId\":\"BATTERY\",\"data\":\"%d\"}", soc);

	err = modem_at_run(cmd, NULL, 0, CONFIG_APP_BATTERY_AT_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("Battery cloud message failed: %d", err);
		return err;
	}

	LOG_INF("Battery %d%% sent to nRF Cloud", soc);
	return 0;
}
