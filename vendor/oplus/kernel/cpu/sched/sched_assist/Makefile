# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-2020 Oplus. All rights reserved.

LINUXINCLUDE += -I$(srctree)/

obj-$(CONFIG_OPLUS_FEATURE_SCHED_ASSIST) += oplus_bsp_sched_assist.o

oplus_bsp_sched_assist-y := sched_assist.o sa_common.o sa_sysfs.o sa_exec.o sa_fair.o sa_jankinfo.o sched_assist_locking.o
oplus_bsp_sched_assist-$(CONFIG_OPLUS_CPU_AUDIO_PERF) += sa_audio.o
