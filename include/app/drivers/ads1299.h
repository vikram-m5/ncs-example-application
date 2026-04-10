/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 *
 * Public API for the ADS1299 EEG ADC driver.
 */

#ifndef APP_DRIVERS_ADS1299_H_
#define APP_DRIVERS_ADS1299_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start continuous data acquisition mode (RDATAC).
 *
 * Enters continuous read mode and asserts the START pin (if available).
 * Data will be automatically read on each DRDY interrupt if a trigger
 * handler is registered.
 *
 * @param dev ADS1299 device (master in daisy chain).
 * @return 0 on success, negative error code on failure.
 */
int ads1299_start_continuous(const struct device *dev);

/**
 * @brief Stop continuous data acquisition mode.
 *
 * Deasserts START pin and sends SDATAC command.
 *
 * @param dev ADS1299 device (master in daisy chain).
 * @return 0 on success, negative error code on failure.
 */
int ads1299_stop_continuous(const struct device *dev);

/**
 * @brief Enable the internal test signal on all active channels.
 *
 * Configures the ADS1299 to generate a known square wave for
 * hardware validation. Channels are set to test signal mux input.
 *
 * @param dev ADS1299 device.
 * @return 0 on success, negative error code on failure.
 */
int ads1299_enable_test_signal(const struct device *dev);

/**
 * @brief Disable test signal and restore normal input mode.
 *
 * @param dev ADS1299 device.
 * @return 0 on success, negative error code on failure.
 */
int ads1299_disable_test_signal(const struct device *dev);

/**
 * @brief Get raw 24-bit channel data from the last sample.
 *
 * Returns the sign-extended 24-bit raw value for the specified channel
 * from the most recent sample_fetch or DRDY trigger read.
 *
 * @param dev ADS1299 device.
 * @param channel Channel index (0-7).
 * @param value Pointer to store the raw 24-bit value.
 * @return 0 on success, -EINVAL for invalid channel.
 */
int ads1299_get_channel_raw(const struct device *dev, uint8_t channel,
			    int32_t *value);

/**
 * @brief Get pointer to the raw data buffer.
 *
 * Returns a pointer to the raw SPI data buffer from the last read.
 * In daisy chain mode this contains data from all devices:
 *   [status_U1(3B)][ch1-8_U1(24B)][status_U2(3B)][ch1-8_U2(24B)][status_U3(3B)][ch1-8_U3(24B)]
 *
 * @param dev ADS1299 device (master).
 * @param buf Pointer to store buffer address.
 * @param len Pointer to store buffer length.
 * @return 0 on success.
 */
int ads1299_get_raw_buffer(const struct device *dev, const uint8_t **buf,
			   size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* APP_DRIVERS_ADS1299_H_ */
