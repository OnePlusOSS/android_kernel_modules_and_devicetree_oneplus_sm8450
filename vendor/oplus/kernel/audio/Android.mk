# Android makefile for audio kernel modules

LOCAL_PATH := $(call my-dir)

# ifeq ($(call is-board-platform-in-list,taro), true)
$(shell mkdir -p $(PRODUCT_OUT)/obj/vendor;)
$(shell mkdir -p $(PRODUCT_OUT)/obj/vendor/oplus;)
$(shell mkdir -p $(PRODUCT_OUT)/obj/vendor/oplus/kernel;)
$(shell mkdir -p $(PRODUCT_OUT)/obj/vendor/oplus/kernel/audio;)
$(shell mkdir -p $(PRODUCT_OUT)/obj/vendor/oplus/kernel/audio/include;)
$(shell rm -rf $(PRODUCT_OUT)/obj/DLKM_OBJ/vendor/oplus/kernel/audio/Module.symvers)
# endif

ifeq ($(call is-board-platform, taro),true)
AUDIO_SELECT  := CONFIG_SND_SOC_WAIPIO=m
endif

# Build/Package only in case of supported target
# ifeq ($(call is-board-platform-in-list,taro), true)

LOCAL_PATH := $(call my-dir)

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

OPLUS_AUDIO_BLD_DIR := $(shell pwd)/vendor/oplus/kernel/audio

DLKM_DIR := $(TOP)/device/qcom/common/dlkm

###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := OPLUS_AUDIO_ROOT=$(OPLUS_AUDIO_BLD_DIR)

# We are actually building audio.ko here, as per the
# requirement we are specifying <chipset>_audio.ko as LOCAL_MODULE.
# This means we need to rename the module to <chipset>_audio.ko
# after audio.ko is built.
KBUILD_OPTIONS += MODNAME=audio_dlkm
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
KBUILD_OPTIONS += $(AUDIO_SELECT)

########################### TFA98xx-v6 CODEC  ###########################
#ifdef OPLUS_ARCH_EXTENDS
include $(CLEAR_VARS)
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := tfa98xx-v6_dlkm.ko
LOCAL_MODULE_KBUILD_NAME  := codecs/tfa98xx-v6/tfa98xx-v6_dlkm.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
#endif /* OPLUS_ARCH_EXTENDS */
###########################################################

endif # DLKM check
# endif # supported target check
