ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_WAIPIO) +=  \
	wly-20846-waipio-overlay.dtbo \
	wly-2084A-waipio-overlay.dtbo \
	wly-20846-waipio-overlay-EVB.dtbo \
	wly-2084A-waipio-overlay-EVB.dtbo \
	wly-20846-waipio-overlay-preT0.dtbo \
	wly-2084A-waipio-overlay-preT0.dtbo \
	mt-20845-waipio-overlay.dtbo \
	jnc-20865-waipio-overlay.dtbo \
	baize-21001-waipio-overlay.dtbo \
	kunpeng-21009-waipio-overlay.dtbo \
	ferrari-21631-waipio-overlay.dtbo

wly-20846-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-2084A-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-20846-waipio-overlay-EVB.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-2084A-waipio-overlay-EVB.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-20846-waipio-overlay-preT0.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
wly-2084A-waipio-overlay-preT0.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
mt-20845-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
jnc-20865-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
baize-21001-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
kunpeng-21009-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
ferrari-21631-waipio-overlay.dtbo-base := waipio.dtb waipiop.dtb waipio-v2.dtb waipiop-v2.dtb
endif

always-y	:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
