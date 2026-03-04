ifeq ($(CONFIG_OPLUS_CHG),n)

ifeq ($(CONFIG_OPLUS_CHG_V2),n)
obj-y				+= v1/
else ifeq ($(CONFIG_OPLUS_CHG_V2),)
obj-y				+= v1/
else
obj-$(CONFIG_OPLUS_CHG_V2)	+= v2/
endif

else ifeq ($(CONFIG_OPLUS_CHG),)

ifeq ($(CONFIG_OPLUS_CHG_V2),n)
obj-y				+= v1/
else ifeq ($(CONFIG_OPLUS_CHG_V2),)
obj-y				+= v1/
else
obj-$(CONFIG_OPLUS_CHG_V2)	+= v2/
endif

else

obj-$(CONFIG_OPLUS_CHG)		+= v1/
obj-$(CONFIG_OPLUS_CHG_V2)	+= v2/
ifeq ($(CONFIG_OPLUS_CHG),m)
ifeq ($(CONFIG_OPLUS_CHG_V2),m)
obj-m	+= oplus_chg_symbol.o
endif
endif

endif

obj-$(CONFIG_OPLUS_CHG_TEST_KIT) += test-kit/
obj-$(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT) += debug-kit/
obj-$(CONFIG_OPLUS_DYNAMIC_CONFIG) += config/
obj-$(CONFIG_OPLUS_WIRELESS_PEN) += wireless_pen/
obj-$(CONFIG_OPLUS_I2C_RST_NOTIFY) += gauge_i2c_rst/

ifdef CONFIG_OPLUS_PD_EXT_SUPPORT
obj-$(CONFIG_TCPC_CLASS) += pd_ext/
endif
