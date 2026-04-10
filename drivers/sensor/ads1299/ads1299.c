/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_ads1299

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ads1299, CONFIG_SENSOR_LOG_LEVEL);

#include "ads1299.h"

#define ADS1299_SPI_OP (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | \
			SPI_TRANSFER_MSB | SPI_MODE_CPHA)

static uint8_t ads1299_gain_to_reg(uint8_t gain)
{
	switch (gain) {
	case 1:  return ADS1299_CHNSET_GAIN_1;
	case 2:  return ADS1299_CHNSET_GAIN_2;
	case 4:  return ADS1299_CHNSET_GAIN_4;
	case 6:  return ADS1299_CHNSET_GAIN_6;
	case 8:  return ADS1299_CHNSET_GAIN_8;
	case 12: return ADS1299_CHNSET_GAIN_12;
	case 24: return ADS1299_CHNSET_GAIN_24;
	default: return ADS1299_CHNSET_GAIN_24;
	}
}

static uint8_t ads1299_rate_to_reg(uint16_t rate)
{
	switch (rate) {
	case 16000: return ADS1299_DR_16KSPS;
	case 8000:  return ADS1299_DR_8KSPS;
	case 4000:  return ADS1299_DR_4KSPS;
	case 2000:  return ADS1299_DR_2KSPS;
	case 1000:  return ADS1299_DR_1KSPS;
	case 500:   return ADS1299_DR_500SPS;
	case 250:   return ADS1299_DR_250SPS;
	default:    return ADS1299_DR_250SPS;
	}
}

int ads1299_send_command(const struct device *dev, uint8_t cmd)
{
	const struct ads1299_config *cfg = dev->config;
	uint8_t tx_buf = cmd;

	const struct spi_buf tx = { .buf = &tx_buf, .len = 1 };
	const struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };

	int ret = spi_write_dt(&cfg->spi, &tx_set);

	/* ADS1299 requires 4 tCLK (~2us) between commands */
	k_busy_wait(2);

	return ret;
}

int ads1299_read_register(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct ads1299_config *cfg = dev->config;
	uint8_t tx_buf[3];
	uint8_t rx_buf[3];

	tx_buf[0] = ADS1299_CMD_RREG | (reg & 0x1F);
	tx_buf[1] = 0x00; /* Read 1 register (n-1 = 0) */
	tx_buf[2] = 0x00; /* Dummy byte to clock out data */

	const struct spi_buf tx = { .buf = tx_buf, .len = 3 };
	const struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	const struct spi_buf rx = { .buf = rx_buf, .len = 3 };
	const struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

	int ret = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);

	if (ret == 0) {
		*val = rx_buf[2];
	}

	k_busy_wait(2);
	return ret;
}

int ads1299_write_register(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct ads1299_config *cfg = dev->config;
	uint8_t tx_buf[3];

	tx_buf[0] = ADS1299_CMD_WREG | (reg & 0x1F);
	tx_buf[1] = 0x00; /* Write 1 register (n-1 = 0) */
	tx_buf[2] = val;

	const struct spi_buf tx = { .buf = tx_buf, .len = 3 };
	const struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };

	int ret = spi_write_dt(&cfg->spi, &tx_set);

	k_busy_wait(2);
	return ret;
}

int ads1299_read_data(const struct device *dev, uint8_t *buf, size_t len)
{
	const struct ads1299_config *cfg = dev->config;

	const struct spi_buf rx = { .buf = buf, .len = len };
	const struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };

	return spi_read_dt(&cfg->spi, &rx_set);
}

static int32_t ads1299_sign_extend_24(const uint8_t *buf)
{
	int32_t val = ((int32_t)(buf[0] << 24 | buf[1] << 16 | buf[2] << 8)) >> 8;
	return val;
}

static int ads1299_configure_channels(const struct device *dev)
{
	const struct ads1299_config *cfg = dev->config;
	uint8_t gain_bits = ads1299_gain_to_reg(cfg->gain);
	int ret;

	/* Configure active channels: gain + normal input + SRB2 */
	for (int ch = 0; ch < cfg->num_channels; ch++) {
		uint8_t ch_reg = gain_bits | ADS1299_CHNSET_MUX_NORMAL;
		ret = ads1299_write_register(dev, ADS1299_REG_CH1SET + ch, ch_reg);
		if (ret < 0) {
			LOG_ERR("Failed to configure channel %d (%d)", ch + 1, ret);
			return ret;
		}
	}

	/* Power down unused channels */
	for (int ch = cfg->num_channels; ch < ADS1299_MAX_CHANNELS; ch++) {
		ret = ads1299_write_register(dev, ADS1299_REG_CH1SET + ch,
					     ADS1299_CHNSET_PD | ADS1299_CHNSET_MUX_SHORT);
		if (ret < 0) {
			LOG_ERR("Failed to power down channel %d (%d)", ch + 1, ret);
			return ret;
		}
	}

	return 0;
}

static int ads1299_hw_reset(const struct device *dev)
{
	const struct ads1299_config *cfg = dev->config;

	if (!cfg->reset_gpio.port) {
		return ads1299_send_command(dev, ADS1299_CMD_RESET);
	}

	gpio_pin_set_dt(&cfg->reset_gpio, 1);
	k_busy_wait(2);
	gpio_pin_set_dt(&cfg->reset_gpio, 0);

	/* Wait for power-on reset (tPOR) */
	k_msleep(128);

	return 0;
}

static int ads1299_sample_fetch(const struct device *dev,
				enum sensor_channel chan)
{
	struct ads1299_data *data = dev->data;
	const struct ads1299_config *cfg = dev->config;
	int ret;

	if (data->continuous_mode) {
		/* In RDATAC mode, data is read by the trigger handler */
		return 0;
	}

	/* Single-shot read */
	ret = ads1299_send_command(dev, ADS1299_CMD_RDATA);
	if (ret < 0) {
		return ret;
	}

	size_t read_len;

	if (cfg->daisy_chain) {
		read_len = ADS1299_DAISY_BYTES;
	} else {
		read_len = ADS1299_SAMPLE_BYTES;
	}

	ret = ads1299_read_data(dev, data->raw_buf, read_len);
	if (ret < 0) {
		return ret;
	}

	/* Parse status (first 3 bytes) */
	data->status = ((uint32_t)data->raw_buf[0] << 16) |
		       ((uint32_t)data->raw_buf[1] << 8) |
		       ((uint32_t)data->raw_buf[2]);

	/* Parse channel data */
	for (int ch = 0; ch < cfg->num_channels; ch++) {
		int offset = ADS1299_STATUS_BYTES + (ch * ADS1299_CHANNEL_BYTES);
		data->channel_data[ch] = ads1299_sign_extend_24(&data->raw_buf[offset]);
	}

	return 0;
}

static int ads1299_channel_get(const struct device *dev,
			       enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct ads1299_data *data = dev->data;

	if (chan == SENSOR_CHAN_ALL) {
		/* Return first channel for SENSOR_CHAN_ALL */
		chan = SENSOR_CHAN_VOLTAGE;
	}

	if (chan != SENSOR_CHAN_VOLTAGE) {
		return -ENOTSUP;
	}

	/*
	 * Convert raw 24-bit value to microvolts.
	 * Vref = 4.5V, LSB = Vref / (2^23 * gain)
	 * For gain=24: LSB = 4.5 / (8388608 * 24) = ~22.35 nV
	 * Store as val1 = raw_value (integer part for flexibility)
	 * val2 = 0
	 * Application can compute: voltage_uV = raw * 4500000 / (8388608 * gain)
	 */
	val->val1 = data->channel_data[0];
	val->val2 = 0;

	return 0;
}

/* Public API implementations */

int ads1299_start_continuous(const struct device *dev)
{
	struct ads1299_data *data = dev->data;
	const struct ads1299_config *cfg = dev->config;
	int ret;

	ret = ads1299_send_command(dev, ADS1299_CMD_RDATAC);
	if (ret < 0) {
		return ret;
	}

	data->continuous_mode = true;

	/* Assert START pin if available */
	if (cfg->start_gpio.port) {
		gpio_pin_set_dt(&cfg->start_gpio, 1);
	} else {
		ret = ads1299_send_command(dev, ADS1299_CMD_START);
	}

	return ret;
}

int ads1299_stop_continuous(const struct device *dev)
{
	struct ads1299_data *data = dev->data;
	const struct ads1299_config *cfg = dev->config;
	int ret;

	/* Deassert START pin */
	if (cfg->start_gpio.port) {
		gpio_pin_set_dt(&cfg->start_gpio, 0);
	} else {
		ret = ads1299_send_command(dev, ADS1299_CMD_STOP);
		if (ret < 0) {
			return ret;
		}
	}

	data->continuous_mode = false;

	ret = ads1299_send_command(dev, ADS1299_CMD_SDATAC);
	return ret;
}

int ads1299_enable_test_signal(const struct device *dev)
{
	const struct ads1299_config *cfg = dev->config;
	int ret;

	/* Enable internal test signal generation */
	ret = ads1299_write_register(dev, ADS1299_REG_CONFIG2,
				     ADS1299_CONFIG2_TEST |
				     ADS1299_CONFIG2_TEST_AMP_1X |
				     ADS1299_CONFIG2_TEST_FREQ_SLOW);
	if (ret < 0) {
		return ret;
	}

	/* Set all active channels to test signal mux */
	uint8_t gain_bits = ads1299_gain_to_reg(cfg->gain);

	for (int ch = 0; ch < cfg->num_channels; ch++) {
		ret = ads1299_write_register(dev, ADS1299_REG_CH1SET + ch,
					     gain_bits | ADS1299_CHNSET_MUX_TEST);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

int ads1299_disable_test_signal(const struct device *dev)
{
	int ret;

	/* Disable test signal */
	ret = ads1299_write_register(dev, ADS1299_REG_CONFIG2, ADS1299_CONFIG2_BASE);
	if (ret < 0) {
		return ret;
	}

	/* Restore normal input mode */
	return ads1299_configure_channels(dev);
}

int ads1299_get_channel_raw(const struct device *dev, uint8_t channel,
			    int32_t *value)
{
	struct ads1299_data *data = dev->data;
	const struct ads1299_config *cfg = dev->config;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	*value = data->channel_data[channel];
	return 0;
}

int ads1299_get_raw_buffer(const struct device *dev, const uint8_t **buf,
			   size_t *len)
{
	struct ads1299_data *data = dev->data;
	const struct ads1299_config *cfg = dev->config;

	*buf = data->raw_buf;

	if (cfg->daisy_chain) {
		*len = ADS1299_DAISY_BYTES;
	} else {
		*len = ADS1299_SAMPLE_BYTES;
	}

	return 0;
}

static DEVICE_API(sensor, ads1299_api) = {
	.sample_fetch = ads1299_sample_fetch,
	.channel_get = ads1299_channel_get,
#ifdef CONFIG_ADS1299_TRIGGER
	.trigger_set = ads1299_trigger_set,
#endif
};

static int ads1299_init(const struct device *dev)
{
	const struct ads1299_config *cfg = dev->config;
	struct ads1299_data *data = dev->data;
	uint8_t id;
	int ret;

	data->continuous_mode = false;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	/* Configure CLKSEL GPIO - use internal oscillator */
	if (cfg->clksel_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->clksel_gpio)) {
			LOG_ERR("CLKSEL GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->clksel_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure CLKSEL (%d)", ret);
			return ret;
		}
	}

	/* Configure control GPIOs (only on master device) */
	if (cfg->reset_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
			LOG_ERR("RESET GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	if (cfg->pwdn_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->pwdn_gpio)) {
			LOG_ERR("PWDN GPIO not ready");
			return -ENODEV;
		}
		/* Deassert power down (active low, so set inactive = high = powered on) */
		ret = gpio_pin_configure_dt(&cfg->pwdn_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	if (cfg->start_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->start_gpio)) {
			LOG_ERR("START GPIO not ready");
			return -ENODEV;
		}
		/* START low initially */
		ret = gpio_pin_configure_dt(&cfg->start_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	/* Perform hardware reset if we have the reset GPIO */
	if (cfg->reset_gpio.port) {
		ret = ads1299_hw_reset(dev);
		if (ret < 0) {
			LOG_ERR("Hardware reset failed (%d)", ret);
			return ret;
		}
	}

	/* Wait for device to settle after power-up */
	k_msleep(10);

	/* Exit continuous read mode (required before register access) */
	ret = ads1299_send_command(dev, ADS1299_CMD_SDATAC);
	if (ret < 0) {
		LOG_ERR("SDATAC command failed (%d)", ret);
		return ret;
	}

	k_msleep(1);

	/* Read and verify device ID */
	ret = ads1299_read_register(dev, ADS1299_REG_ID, &id);
	if (ret < 0) {
		LOG_ERR("Failed to read ID register (%d)", ret);
		return ret;
	}

	if ((id & ADS1299_ID_MASK) != ADS1299_ID_ADS1299) {
		LOG_ERR("Unexpected ADS1299 ID: 0x%02X (expected 0x%02X)",
			id & ADS1299_ID_MASK, ADS1299_ID_ADS1299);
		return -EIO;
	}

	LOG_INF("ADS1299 detected, ID: 0x%02X, channels: %d", id, cfg->num_channels);

	/* Configure CONFIG1: data rate + daisy chain mode */
	uint8_t config1;

	if (cfg->daisy_chain) {
		config1 = ADS1299_CONFIG1_DAISY | ads1299_rate_to_reg(cfg->sample_rate);
	} else {
		config1 = ADS1299_CONFIG1_MULTI | ads1299_rate_to_reg(cfg->sample_rate);
	}

	ret = ads1299_write_register(dev, ADS1299_REG_CONFIG1, config1);
	if (ret < 0) {
		return ret;
	}

	/* CONFIG2: no test signal */
	ret = ads1299_write_register(dev, ADS1299_REG_CONFIG2, ADS1299_CONFIG2_BASE);
	if (ret < 0) {
		return ret;
	}

	/* CONFIG3: internal reference buffer on, bias reference internal */
	ret = ads1299_write_register(dev, ADS1299_REG_CONFIG3, ADS1299_CONFIG3_REFBUF);
	if (ret < 0) {
		return ret;
	}

	/* Wait for internal reference to settle */
	k_msleep(150);

	/* Configure channel settings */
	ret = ads1299_configure_channels(dev);
	if (ret < 0) {
		return ret;
	}

	/* Connect SRB1 as reference for all channels */
	ret = ads1299_write_register(dev, ADS1299_REG_MISC1, ADS1299_MISC1_SRB1);
	if (ret < 0) {
		return ret;
	}

#ifdef CONFIG_ADS1299_TRIGGER
	if (cfg->drdy_gpio.port) {
		ret = ads1299_trigger_init(dev);
		if (ret < 0) {
			LOG_ERR("Trigger init failed (%d)", ret);
			return ret;
		}
	}
#endif

	LOG_INF("ADS1299 initialized: %d channels, %d SPS, gain=%d",
		cfg->num_channels, cfg->sample_rate, cfg->gain);

	return 0;
}

#define ADS1299_GPIO_SPEC_OR_EMPTY(inst, prop)                     \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, prop),            \
		     (GPIO_DT_SPEC_INST_GET(inst, prop)),          \
		     ({0}))

#define ADS1299_INIT(inst)                                                     \
	static struct ads1299_data ads1299_data_##inst;                        \
                                                                               \
	static const struct ads1299_config ads1299_config_##inst = {           \
		.spi = SPI_DT_SPEC_INST_GET(inst, ADS1299_SPI_OP, 0),         \
		.drdy_gpio = ADS1299_GPIO_SPEC_OR_EMPTY(inst, drdy_gpios),    \
		.reset_gpio = ADS1299_GPIO_SPEC_OR_EMPTY(inst, reset_gpios),  \
		.pwdn_gpio = ADS1299_GPIO_SPEC_OR_EMPTY(inst, pwdn_gpios),    \
		.start_gpio = ADS1299_GPIO_SPEC_OR_EMPTY(inst, start_gpios),  \
		.clksel_gpio = ADS1299_GPIO_SPEC_OR_EMPTY(inst, clksel_gpios),\
		.num_channels = DT_INST_PROP(inst, num_channels),              \
		.sample_rate = DT_INST_PROP(inst, sample_rate),                \
		.gain = DT_INST_PROP(inst, gain),                              \
		.daisy_chain = DT_INST_PROP(inst, daisy_chain),                \
	};                                                                     \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, ads1299_init, NULL,                        \
			      &ads1299_data_##inst,                            \
			      &ads1299_config_##inst, POST_KERNEL,             \
			      CONFIG_ADS1299_INIT_PRIORITY, &ads1299_api);

DT_INST_FOREACH_STATUS_OKAY(ADS1299_INIT)
