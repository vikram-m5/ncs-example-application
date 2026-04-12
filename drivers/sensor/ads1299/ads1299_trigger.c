/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(ads1299, CONFIG_SENSOR_LOG_LEVEL);

#include "ads1299.h"

static void ads1299_drdy_callback(const struct device *port,
				  struct gpio_callback *cb,
				  uint32_t pins)
{
	struct ads1299_data *data = CONTAINER_OF(cb, struct ads1299_data, drdy_cb);

	k_work_submit(&data->work);
}

static void ads1299_work_handler(struct k_work *work)
{
	struct ads1299_data *data = CONTAINER_OF(work, struct ads1299_data, work);
	const struct device *dev = data->dev;
	const struct ads1299_config *cfg = dev->config;

	if (data->continuous_mode) {
		size_t read_len;

		if (cfg->daisy_chain) {
			read_len = ADS1299_DAISY_BYTES;
		} else {
			read_len = ADS1299_SAMPLE_BYTES;
		}

		int ret = ads1299_read_data(dev, data->raw_buf, read_len);

		if (ret < 0) {
			LOG_ERR("Failed to read data in DRDY handler (%d)", ret);
			return;
		}

		/* Parse status */
		data->status = ((uint32_t)data->raw_buf[0] << 16) |
			       ((uint32_t)data->raw_buf[1] << 8) |
			       ((uint32_t)data->raw_buf[2]);

		/* Parse channel data for this device */
		for (int ch = 0; ch < ADS1299_MAX_CHANNELS; ch++) {
			int offset = ADS1299_STATUS_BYTES +
				     (ch * ADS1299_CHANNEL_BYTES);
			data->channel_data[ch] =
				((int32_t)(data->raw_buf[offset] << 24 |
					   data->raw_buf[offset + 1] << 16 |
					   data->raw_buf[offset + 2] << 8)) >> 8;
		}
	}

	if (data->trigger_handler) {
		data->trigger_handler(dev, data->trigger);
	}
}

int ads1299_trigger_set(const struct device *dev,
			const struct sensor_trigger *trig,
			sensor_trigger_handler_t handler)
{
	struct ads1299_data *data = dev->data;
	const struct ads1299_config *cfg = dev->config;

	if (trig->type != SENSOR_TRIG_DATA_READY) {
		return -ENOTSUP;
	}

	if (!cfg->drdy_gpio.port) {
		return -ENOTSUP;
	}

	data->trigger_handler = handler;
	data->trigger = trig;

	if (handler) {
		return gpio_pin_interrupt_configure_dt(&cfg->drdy_gpio,
						       GPIO_INT_EDGE_TO_ACTIVE);
	} else {
		return gpio_pin_interrupt_configure_dt(&cfg->drdy_gpio,
						       GPIO_INT_DISABLE);
	}
}

int ads1299_trigger_init(const struct device *dev)
{
	struct ads1299_data *data = dev->data;
	const struct ads1299_config *cfg = dev->config;
	int ret;

	data->dev = dev;

	if (!gpio_is_ready_dt(&cfg->drdy_gpio)) {
		LOG_ERR("DRDY GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->drdy_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure DRDY GPIO (%d)", ret);
		return ret;
	}

	gpio_init_callback(&data->drdy_cb, ads1299_drdy_callback,
			   BIT(cfg->drdy_gpio.pin));

	ret = gpio_add_callback(cfg->drdy_gpio.port, &data->drdy_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add DRDY callback (%d)", ret);
		return ret;
	}

	k_work_init(&data->work, ads1299_work_handler);

	data->trigger_handler = NULL;
	data->trigger = NULL;

	return 0;
}
