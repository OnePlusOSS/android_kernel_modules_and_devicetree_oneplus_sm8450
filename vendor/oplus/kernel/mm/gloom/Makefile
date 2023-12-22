# SPDX-License-Identifier: GPL-2.0-only
KBUILD_OPTIONS += CONFIG_VIRTUAL_RESERVE_MEMORY=m

KERNEL_SRC ?= lib/module/$(shell uname -r)/build

ifeq ($(CHIPSET_COMPANY), QCOM)
EXTRA_SYMBOLS += $(KERNEL_SRC)/../vendor/oplus/kernel/cpu/sched/healthinfo/Module.symvers
else ifeq ($(CHIPSET_COMPANY), MTK)
EXTRA_SYMBOLS += ${OUT_DIR}/../vendor/oplus/kernel/cpu/sched/healthinfo/Module.symvers
endif

M ?= $(shell pwd)

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
