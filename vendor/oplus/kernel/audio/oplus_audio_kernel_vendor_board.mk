# Build audio kernel driver
ifneq ($(TARGET_BOARD_AUTO),true)
ifeq ($(call is-board-platform-in-list,$(TARGET_BOARD_PLATFORM)),true)
#ifdef OPLUS_ARCH_EXTENDS
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/tfa98xx-v6_dlkm.ko
#endif /* OPLUS_ARCH_EXTENDS */
endif
endif
