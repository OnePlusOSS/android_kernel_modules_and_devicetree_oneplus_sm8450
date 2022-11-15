# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-2021 Oplus. All rights reserved.
CONSUMER_IR_QC_SELECT := CONFIG_OPLUS_SENSOR_CONSUMER_IR_QC=m
KBUILD_OPTIONS += $(CONSUMER_IR_QC_SELECT)

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

modules modules_install clean:
        $(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) W=1 $(@)

