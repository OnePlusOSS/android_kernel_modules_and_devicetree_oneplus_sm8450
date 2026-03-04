# Build audio kernel driver
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/tfa98xx-v6_dlkm.ko
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/audio_extend_dlkm.ko

#ifdef OPLUS_ARCH_EXTENDS
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/sia9175_dlkm.ko
#endif /* OPLUS_ARCH_EXTENDS */
