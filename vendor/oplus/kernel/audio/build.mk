# Build/Package only in case of supported target
ifeq ($(call is-board-platform-in-list,taro), true)

MY_LOCAL_PATH := $(call my-dir)

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(MY_LOCAL_PATH)),)

ifneq ($(findstring audio-kernel,$(MY_LOCAL_PATH)),)

DLKM_DIR := $(TOP)/device/qcom/common/dlkm

OPLUS_AUDIO_SRC_FILES := \
	$(wildcard $(MY_LOCAL_PATH)/*) \
	$(wildcard $(MY_LOCAL_PATH)/*/*) \
	$(wildcard $(MY_LOCAL_PATH)/*/*/*) \
	$(wildcard $(MY_LOCAL_PATH)/*/*/*/*) \
	$(wildcard $(MY_LOCAL_PATH)/*/*/*/*/*)

########################### Audio extend driver  ###########################
#ifdef OPLUS_ARCH_EXTENDS
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(OPLUS_AUDIO_SRC_FILES)
LOCAL_MODULE              := audio_extend_dlkm.ko
LOCAL_MODULE_KBUILD_NAME  := oplus/asoc/audio_extend_dlkm.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
#endif /* OPLUS_ARCH_EXTENDS */
###########################################################

########################### TFA98xx-v6 CODEC  ###########################
#ifdef OPLUS_ARCH_EXTENDS
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(OPLUS_AUDIO_SRC_FILES)
LOCAL_MODULE              := tfa98xx-v6_dlkm.ko
LOCAL_MODULE_KBUILD_NAME  := oplus/asoc/codecs/tfa98xx-v6/tfa98xx-v6_dlkm.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
#endif /* OPLUS_ARCH_EXTENDS */
###########################################################

########################### SIA9175 CODEC  ###########################
#ifdef OPLUS_ARCH_EXTENDS
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(OPLUS_AUDIO_SRC_FILES)
LOCAL_MODULE              := sia9175_dlkm.ko
LOCAL_MODULE_KBUILD_NAME  := oplus/asoc/codecs/sia9175/sia9175_dlkm.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
#endif /* OPLUS_ARCH_EXTENDS */
###########################################################

endif # audio-kernel
endif # DLKM check
endif # supported target check