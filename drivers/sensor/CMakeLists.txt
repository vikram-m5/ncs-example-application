# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_EXAMPLE_SENSOR example_sensor)
add_subdirectory_ifdef(CONFIG_ADS1299 ads1299)
