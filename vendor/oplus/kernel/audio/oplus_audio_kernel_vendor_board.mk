# Build audio kernel driver
ifeq ($(BUILD_AUDIO_MODULES),true)
ifneq ($(TARGET_BOARD_AUTO),true)
# Build/Package only in case of supported target
ifeq ($(call is-board-platform-in-list,taro), true)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/tfa98xx-v6_dlkm.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/audio_extend_dlkm.ko

#ifdef OPLUS_ARCH_EXTENDS
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/sia9175_dlkm.ko
#endif /* OPLUS_ARCH_EXTENDS */
endif
endif
endif
