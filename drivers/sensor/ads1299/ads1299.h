/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_DRIVERS_ADS1299_INTERNAL_H_
#define APP_DRIVERS_ADS1299_INTERNAL_H_

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

/* ADS1299 Register Addresses */
#define ADS1299_REG_ID          0x00
#define ADS1299_REG_CONFIG1     0x01
#define ADS1299_REG_CONFIG2     0x02
#define ADS1299_REG_CONFIG3     0x03
#define ADS1299_REG_LOFF        0x04
#define ADS1299_REG_CH1SET      0x05
#define ADS1299_REG_CH2SET      0x06
#define ADS1299_REG_CH3SET      0x07
#define ADS1299_REG_CH4SET      0x08
#define ADS1299_REG_CH5SET      0x09
#define ADS1299_REG_CH6SET      0x0A
#define ADS1299_REG_CH7SET      0x0B
#define ADS1299_REG_CH8SET      0x0C
#define ADS1299_REG_BIAS_SENSP  0x0D
#define ADS1299_REG_BIAS_SENSN  0x0E
#define ADS1299_REG_LOFF_SENSP  0x0F
#define ADS1299_REG_LOFF_SENSN  0x10
#define ADS1299_REG_LOFF_FLIP   0x11
#define ADS1299_REG_LOFF_STATP  0x12
#define ADS1299_REG_LOFF_STATN  0x13
#define ADS1299_REG_GPIO        0x14
#define ADS1299_REG_MISC1       0x15
#define ADS1299_REG_MISC2       0x16
#define ADS1299_REG_CONFIG4     0x17

/* SPI Commands */
#define ADS1299_CMD_WAKEUP      0x02
#define ADS1299_CMD_STANDBY     0x04
#define ADS1299_CMD_RESET       0x06
#define ADS1299_CMD_START       0x08
#define ADS1299_CMD_STOP        0x0A
#define ADS1299_CMD_RDATAC      0x10
#define ADS1299_CMD_SDATAC      0x11
#define ADS1299_CMD_RDATA       0x12
#define ADS1299_CMD_RREG        0x20
#define ADS1299_CMD_WREG        0x40

/* ADS1299 ID register: bits [4:0] = device ID, bits [7:5] = revision
 * Device ID for ADS1299 = 0x1E (DEV_ID[2:0]=110, NU_CH[1:0]=10)
 * Full ID byte is typically 0x3E (rev 001, id 11110) */
#define ADS1299_ID_ADS1299     0x1E
#define ADS1299_ID_MASK        0x1F

/* CONFIG1 data rate bits [2:0] */
#define ADS1299_DR_16KSPS       0x00
#define ADS1299_DR_8KSPS        0x01
#define ADS1299_DR_4KSPS        0x02
#define ADS1299_DR_2KSPS        0x03
#define ADS1299_DR_1KSPS        0x04
#define ADS1299_DR_500SPS       0x05
#define ADS1299_DR_250SPS       0x06
#define ADS1299_CONFIG1_BASE    0x90  /* CLK_EN=0, DAISY_EN=1 for daisy chain */
#define ADS1299_CONFIG1_DAISY   0x90  /* DAISY_EN=1 (bit 6), CLK_EN=0 */
#define ADS1299_CONFIG1_MULTI   0xD0  /* DAISY_EN=0 (multi-readback), CLK_EN=1 */

/* CONFIG2 */
#define ADS1299_CONFIG2_BASE    0xC0
#define ADS1299_CONFIG2_TEST    0xD0  /* INT_CAL=1, enable test signal */
#define ADS1299_CONFIG2_TEST_AMP_1X 0x00
#define ADS1299_CONFIG2_TEST_AMP_2X 0x04
#define ADS1299_CONFIG2_TEST_FREQ_SLOW 0x01  /* fCLK / 2^21 */
#define ADS1299_CONFIG2_TEST_FREQ_FAST 0x02  /* fCLK / 2^20 */

/* CONFIG3 */
#define ADS1299_CONFIG3_BASE    0x60
#define ADS1299_CONFIG3_REFBUF  0xE0  /* PD_REFBUF=1 (on), BIAS_MEAS=0, BIASREF_INT=1 */
#define ADS1299_CONFIG3_BIAS    0xEC  /* + PD_BIAS=1, BIAS_LOFF_SENS=1 */

/* CHnSET register fields */
#define ADS1299_CHNSET_PD       0x80  /* Power down channel */
#define ADS1299_CHNSET_GAIN_1   0x00
#define ADS1299_CHNSET_GAIN_2   0x10
#define ADS1299_CHNSET_GAIN_4   0x20
#define ADS1299_CHNSET_GAIN_6   0x30
#define ADS1299_CHNSET_GAIN_8   0x40
#define ADS1299_CHNSET_GAIN_12  0x50
#define ADS1299_CHNSET_GAIN_24  0x60
#define ADS1299_CHNSET_SRB2     0x08  /* Connect SRB2 to channel negative input */
#define ADS1299_CHNSET_MUX_NORMAL    0x00
#define ADS1299_CHNSET_MUX_SHORT     0x01
#define ADS1299_CHNSET_MUX_BIAS_MEAS 0x02
#define ADS1299_CHNSET_MUX_SUPPLY    0x03
#define ADS1299_CHNSET_MUX_TEMP      0x04
#define ADS1299_CHNSET_MUX_TEST      0x05
#define ADS1299_CHNSET_MUX_BIAS_DRP  0x06
#define ADS1299_CHNSET_MUX_BIAS_DRN  0x07

/* MISC1 register */
#define ADS1299_MISC1_SRB1      0x20  /* Connect SRB1 to all channels */

/* Data sizes */
#define ADS1299_STATUS_BYTES    3
#define ADS1299_CHANNEL_BYTES   3
#define ADS1299_MAX_CHANNELS    8
#define ADS1299_SAMPLE_BYTES    (ADS1299_STATUS_BYTES + (ADS1299_MAX_CHANNELS * ADS1299_CHANNEL_BYTES))
#define ADS1299_MAX_DAISY_DEVICES 3
#define ADS1299_DAISY_BYTES     (ADS1299_SAMPLE_BYTES * ADS1299_MAX_DAISY_DEVICES)

struct ads1299_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec drdy_gpio;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec pwdn_gpio;
	struct gpio_dt_spec start_gpio;
	struct gpio_dt_spec clksel_gpio;
	uint8_t channel_mask;
	uint16_t sample_rate;
	uint8_t gain;
	bool daisy_chain;
};

struct ads1299_data {
	uint8_t raw_buf[ADS1299_DAISY_BYTES];
	int32_t channel_data[ADS1299_MAX_CHANNELS];
	uint32_t status;
	bool continuous_mode;

#ifdef CONFIG_ADS1299_TRIGGER
	const struct device *dev;
	struct gpio_callback drdy_cb;
	struct k_work work;
	sensor_trigger_handler_t trigger_handler;
	const struct sensor_trigger *trigger;
#endif
};

/* Internal helpers */
int ads1299_send_command(const struct device *dev, uint8_t cmd);
int ads1299_read_register(const struct device *dev, uint8_t reg, uint8_t *val);
int ads1299_write_register(const struct device *dev, uint8_t reg, uint8_t val);
int ads1299_read_data(const struct device *dev, uint8_t *buf, size_t len);

#ifdef CONFIG_ADS1299_TRIGGER
int ads1299_trigger_init(const struct device *dev);
int ads1299_trigger_set(const struct device *dev,
			const struct sensor_trigger *trig,
			sensor_trigger_handler_t handler);
#endif

#endif /* APP_DRIVERS_ADS1299_INTERNAL_H_ */
