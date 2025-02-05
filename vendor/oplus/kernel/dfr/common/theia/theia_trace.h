// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
*/

#if !defined(_TRACE_THEIA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THEIA_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM theia

TRACE_EVENT(black_screen_monitor,
	TP_PROTO(long time_s, int app_id, const char* log_tag, const char* event_id, const char* log_type,
		const char* error_id, int32_t error_count, int ss_pid, const char* stages),
	TP_ARGS(time_s, app_id, log_tag, event_id, log_type, error_id, error_count, ss_pid, stages),
	TP_STRUCT__entry(
		__field(	long,	time_s)
		__field(	int,	app_id)
		__string(	log_tag,	log_tag)
		__string(	event_id,	event_id)
		__string(	log_type,	log_type)
		__string(	error_id,	error_id)
		__field(	int32_t,	error_count)
		__field(	int,	ss_pid)
		__string(	stages,	stages)
	),
	TP_fast_assign(
		__entry->time_s = time_s;
		__entry->app_id = app_id;
		__assign_str(log_tag, log_tag);
		__assign_str(event_id, event_id);
		__assign_str(log_type, log_type);
		__assign_str(error_id, error_id);
		__entry->error_count = error_count;
		__entry->ss_pid = ss_pid;
		__assign_str(stages, stages);
	),
	TP_printk("time_s:%ld app_id:%d log_tag:%s event_id:%s, log_type:%s error_id:%s error_count:%d ss_pid:%d, stages:%s", __entry->time_s, __entry->app_id,
		__get_str(log_tag), __get_str(event_id), __get_str(log_type), __get_str(error_id), __entry->error_count, __entry->ss_pid, __get_str(stages))
);

TRACE_EVENT(bright_screen_monitor,
	TP_PROTO(long time_s, int app_id, const char* log_tag, const char* event_id, const char* log_type,
		const char* error_id, int32_t error_count, int ss_pid, const char* stages),
	TP_ARGS(time_s, app_id, log_tag, event_id, log_type, error_id, error_count, ss_pid, stages),
	TP_STRUCT__entry(
		__field(	long,	time_s)
		__field(	int,	app_id)
		__string(	log_tag,	log_tag)
		__string(	event_id,	event_id)
		__string(	log_type,	log_type)
		__string(	error_id,	error_id)
		__field(	int32_t,	error_count)
		__field(	int,	ss_pid)
		__string(	stages,	stages)
	),
	TP_fast_assign(
		__entry->time_s = time_s;
		__entry->app_id = app_id;
		__assign_str(log_tag, log_tag);
		__assign_str(event_id, event_id);
		__assign_str(log_type, log_type);
		__assign_str(error_id, error_id);
		__entry->error_count = error_count;
		__entry->ss_pid = ss_pid;
		__assign_str(stages, stages);
	),
	TP_printk("time_s:%ld app_id:%d log_tag:%s event_id:%s, log_type:%s error_id:%s error_count:%d ss_pid:%d, stages:%s", __entry->time_s, __entry->app_id,
		__get_str(log_tag), __get_str(event_id), __get_str(log_type), __get_str(error_id), __entry->error_count, __entry->ss_pid, __get_str(stages))
);

#endif /* _TRACE_THEIA_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../vendor/oplus/kernel/dfr/common/theia

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE theia_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
