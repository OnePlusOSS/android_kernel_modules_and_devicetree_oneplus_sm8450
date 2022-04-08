ifeq ($(CONFIG_ARCH_WAIPIO), y)
dtbo-y += waipio-vidc.dtbo
endif
ifeq ($(CONFIG_ARCH_DIWALI), y)
dtbo-y += diwali-vidc.dtbo
endif
always-y    := $(dtb-y) $(dtbo-y)
subdir-y    := $(dts-dirs)
clean-files    := *.dtb *.dtbo