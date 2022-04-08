ifeq ($(CONFIG_OPLUS_FEATURE_HANS), y)
obj-$(CONFIG_OPLUS_FEATURE_HANS)    += hans_help.o
obj-$(CONFIG_OPLUS_FEATURE_HANS)    += hans.o
obj-$(CONFIG_OPLUS_FEATURE_HANS)    += hans_netfilter.o
else
obj-$(CONFIG_OPLUS_FEATURE_HANS)          += oplus_hans.o
oplus_hans-objs                           += hans_help.o hans.o hans_netfilter.o
endif
