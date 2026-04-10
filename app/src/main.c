/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 *
 * EEG acquisition application for nRF5340 + 3x ADS1299 daisy chain.
 * Reads 19 EEG channels and streams data over USB CDC ACM.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>

#include <app/drivers/ads1299.h>
#include <app_version.h>

#include <string.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* Device references */
#define ADS1299_0_NODE DT_NODELABEL(ads1299_0)
#define ADS1299_1_NODE DT_NODELABEL(ads1299_1)
#define ADS1299_2_NODE DT_NODELABEL(ads1299_2)

/* Total EEG channels: U1(8) + U2(8) + U3(3) = 19 */
#define EEG_CHANNELS_U1  8
#define EEG_CHANNELS_U2  8
#define EEG_CHANNELS_U3  3
#define EEG_TOTAL_CHANNELS (EEG_CHANNELS_U1 + EEG_CHANNELS_U2 + EEG_CHANNELS_U3)

/* ADS1299 sample bytes: 3 status + 8*3 channel = 27 bytes per device */
#define ADS_SAMPLE_BYTES  27
#define ADS_STATUS_BYTES  3
#define ADS_CHANNEL_BYTES 3

/* Packet format: sync(1) + counter(4) + 19*3 channel data(57) = 62 bytes */
#define PACKET_SYNC_BYTE  0xA0
#define PACKET_SIZE       62

static const struct device *ads_master;
static const struct device *ads_dev[3];
static const struct device *cdc_dev;

static K_SEM_DEFINE(data_ready_sem, 0, 1);
static uint32_t sample_counter;

/* LED nodes */
static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});
static const struct gpio_dt_spec err_led =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

static void ads1299_drdy_handler(const struct device *dev,
				 const struct sensor_trigger *trig)
{
	k_sem_give(&data_ready_sem);
}

static int32_t sign_extend_24(const uint8_t *buf)
{
	return ((int32_t)(buf[0] << 24 | buf[1] << 16 | buf[2] << 8)) >> 8;
}

static void send_eeg_packet(const uint8_t *raw_buf, size_t len)
{
	uint8_t packet[PACKET_SIZE];
	int pos = 0;

	/* Sync byte */
	packet[pos++] = PACKET_SYNC_BYTE;

	/* Sample counter (big-endian) */
	packet[pos++] = (sample_counter >> 24) & 0xFF;
	packet[pos++] = (sample_counter >> 16) & 0xFF;
	packet[pos++] = (sample_counter >> 8) & 0xFF;
	packet[pos++] = sample_counter & 0xFF;

	/*
	 * In daisy chain mode, data arrives as:
	 * [U1 status(3)][U1 ch1-8(24)][U2 status(3)][U2 ch1-8(24)][U3 status(3)][U3 ch1-8(24)]
	 *
	 * Extract 8 channels from U1, 8 from U2, 3 from U3.
	 */
	int dev_offsets[3] = {
		0,                    /* U1 at byte 0 */
		ADS_SAMPLE_BYTES,     /* U2 at byte 27 */
		ADS_SAMPLE_BYTES * 2  /* U3 at byte 54 */
	};
	int dev_channels[3] = { EEG_CHANNELS_U1, EEG_CHANNELS_U2, EEG_CHANNELS_U3 };

	for (int d = 0; d < 3; d++) {
		int base = dev_offsets[d] + ADS_STATUS_BYTES;

		for (int ch = 0; ch < dev_channels[d]; ch++) {
			int src = base + (ch * ADS_CHANNEL_BYTES);

			packet[pos++] = raw_buf[src];
			packet[pos++] = raw_buf[src + 1];
			packet[pos++] = raw_buf[src + 2];
		}
	}

	/* Send over USB CDC */
	if (cdc_dev) {
		for (int i = 0; i < pos; i++) {
			uart_poll_out(cdc_dev, packet[i]);
		}
	}

	sample_counter++;
}

static int init_usb(void)
{
	int ret;

	cdc_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
	if (!device_is_ready(cdc_dev)) {
		LOG_WRN("USB CDC device not ready, data output disabled");
		cdc_dev = NULL;
		return -ENODEV;
	}

	ret = usb_enable(NULL);
	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to enable USB (%d)", ret);
		return ret;
	}

	/* Wait for DTR signal from host */
	uint32_t dtr = 0;

	LOG_INF("Waiting for USB host connection...");
	while (!dtr) {
		uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
		k_msleep(100);
	}
	LOG_INF("USB host connected");

	return 0;
}

static int init_leds(void)
{
	if (status_led.port && gpio_is_ready_dt(&status_led)) {
		gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	}
	if (err_led.port && gpio_is_ready_dt(&err_led)) {
		gpio_pin_configure_dt(&err_led, GPIO_OUTPUT_INACTIVE);
	}
	return 0;
}

int main(void)
{
	int ret;

	printk("EEG Acquisition Firmware %s\n", APP_VERSION_STRING);
	printk("Channels: %d (U1:%d + U2:%d + U3:%d)\n",
	       EEG_TOTAL_CHANNELS, EEG_CHANNELS_U1, EEG_CHANNELS_U2, EEG_CHANNELS_U3);

	init_leds();

	/* Get ADS1299 device references */
	ads_dev[0] = DEVICE_DT_GET(ADS1299_0_NODE);
	ads_dev[1] = DEVICE_DT_GET(ADS1299_1_NODE);
	ads_dev[2] = DEVICE_DT_GET(ADS1299_2_NODE);
	ads_master = ads_dev[0]; /* U1 is the daisy chain master */

	for (int i = 0; i < 3; i++) {
		if (!device_is_ready(ads_dev[i])) {
			LOG_ERR("ADS1299_%d not ready", i);
			if (err_led.port) {
				gpio_pin_set_dt(&err_led, 1);
			}
			return -ENODEV;
		}
		LOG_INF("ADS1299_%d ready", i);
	}

	/* Initialize USB CDC for data streaming */
	ret = init_usb();
	if (ret < 0) {
		LOG_WRN("USB init failed, continuing without data output");
	}

	/* Register DRDY trigger on the master device */
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ALL,
	};

	ret = sensor_trigger_set(ads_master, &trig, ads1299_drdy_handler);
	if (ret < 0) {
		LOG_ERR("Failed to set DRDY trigger (%d)", ret);
		return ret;
	}

	/* Start continuous acquisition on all devices via shared START pin */
	ret = ads1299_start_continuous(ads_master);
	if (ret < 0) {
		LOG_ERR("Failed to start continuous mode (%d)", ret);
		return ret;
	}

	LOG_INF("EEG acquisition started at 250 SPS");
	if (status_led.port) {
		gpio_pin_set_dt(&status_led, 1);
	}

	sample_counter = 0;

	/* Main acquisition loop */
	while (1) {
		/* Wait for DRDY interrupt */
		ret = k_sem_take(&data_ready_sem, K_MSEC(100));
		if (ret == -EAGAIN) {
			/* Timeout - no data ready, toggle LED to indicate waiting */
			continue;
		}

		/* Get the raw daisy chain buffer from master device */
		const uint8_t *raw_buf;
		size_t raw_len;

		ret = ads1299_get_raw_buffer(ads_master, &raw_buf, &raw_len);
		if (ret < 0) {
			LOG_ERR("Failed to get raw buffer (%d)", ret);
			continue;
		}

		/* Send 19-channel packet over USB */
		send_eeg_packet(raw_buf, raw_len);

		/* Toggle status LED every 250 samples (1 second at 250 SPS) */
		if ((sample_counter % 250) == 0) {
			if (status_led.port) {
				gpio_pin_toggle_dt(&status_led);
			}
		}
	}

	return 0;
}
