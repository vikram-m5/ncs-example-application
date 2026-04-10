# Copyright (c) 2024
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(ads1299.c)
zephyr_library_sources_ifdef(CONFIG_ADS1299_TRIGGER ads1299_trigger.c)
