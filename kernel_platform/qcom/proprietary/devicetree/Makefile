vendor := $(srctree)/$(src)

ifneq "$(wildcard $(vendor)/qcom)" ""
#OPLUS_DTS_OVERLAY start
	#subdir-y += qcom
#OPLUS_DTS_OVERLAY end
endif
#OPLUS_DTS_OVERLAY start
$(warning dts select, MSM_ARCH: -$(MSM_ARCH)-)
ifeq ("$(MSM_ARCH)X", "waipio_tuivmX")
subdir-y += qcom
else
subdir-y += oplus
endif
#OPLUS_DTS_OVERLAY end
