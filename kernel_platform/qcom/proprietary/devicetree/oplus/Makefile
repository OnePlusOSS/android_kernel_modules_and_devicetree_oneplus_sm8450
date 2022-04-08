ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_WAIPIO) +=  \
	wly-20846-waipio-overlay.dtbo \
	wly-2084A-waipio-overlay.dtbo \
	wly-20846-waipio-overlay-EVB.dtbo \
	wly-2084A-waipio-overlay-EVB.dtbo \
	wly-20846-waipio-overlay-preT0.dtbo \
	wly-2084A-waipio-overlay-preT0.dtbo \

wly-20846-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-2084A-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-20846-waipio-overlay-EVB.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-2084A-waipio-overlay-EVB.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-20846-waipio-overlay-preT0.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-2084A-waipio-overlay-preT0.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
endif

always-y	:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
