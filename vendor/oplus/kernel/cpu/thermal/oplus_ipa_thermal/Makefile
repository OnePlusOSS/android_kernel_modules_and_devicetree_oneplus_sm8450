KBUILD_OPTIONS += CONFIG_OPLUS_IPA_THERMAL=m

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

ifeq ($(CHIPSET_COMPANY), QCOM)
EXTRA_SYMBOLS += $(KERNEL_SRC)/../vendor/oplus/kernel/cpu/thermal/horae_shell_temp/Module.symvers
else ifeq ($(CHIPSET_COMPANY), MTK)
EXTRA_SYMBOLS += ${OUT_DIR}/../vendor/oplus/kernel/cpu/thermal/horae_shell_temp/Module.symvers
endif

M ?= $(shell pwd)

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)