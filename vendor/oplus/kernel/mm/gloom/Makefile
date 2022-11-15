# SPDX-License-Identifier: GPL-2.0-only
KBUILD_OPTIONS += CONFIG_VIRTUAL_RESERVE_MEMORY=m

KERNEL_SRC ?= lib/module/$(shell uname -r)/build
M ?= $(shell pwd)

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) $(@)
