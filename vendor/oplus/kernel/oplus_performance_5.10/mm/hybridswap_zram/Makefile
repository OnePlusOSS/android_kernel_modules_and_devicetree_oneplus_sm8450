# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_HYBRIDSWAP_ZRAM) += oplus_bsp_hybridswap_zram.o

oplus_bsp_hybridswap_zram-y	:=	zcomp.o zram_drv.o
oplus_bsp_hybridswap_zram-$(CONFIG_HYBRIDSWAP) += hybridswap/hybridswap_main.o
oplus_bsp_hybridswap_zram-$(CONFIG_HYBRIDSWAP_SWAPD) += hybridswap/hybridswap_swapd.o
oplus_bsp_hybridswap_zram-$(CONFIG_HYBRIDSWAP_ASYNC_COMPRESS) += hybridswap/hybridswap_akcompress.o
oplus_bsp_hybridswap_zram-$(CONFIG_HYBRIDSWAP_CORE) += hybridswap/hybridswap_area.o hybridswap/hybridswap_core.o hybridswap/hybridswap_ctrl.o hybridswap/hybridswap_list.o hybridswap/hybridswap_lru_rmap.o hybridswap/hybridswap_manager.o hybridswap/hybridswap_perf.o hybridswap/hybridswap_schedule.o hybridswap/hybridswap_stats.o
