// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hungtask_trace

#if !defined(_HUNGTASK_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _HUNGTASK_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(init_hung,
	TP_PROTO(long fault_timestamp_ms, int app_id, const char *log_tag, const char *event_id, const char *log_type, const char *extra_info),
	TP_ARGS(fault_timestamp_ms, app_id, log_tag, event_id, log_type, extra_info),
	TP_STRUCT__entry(
		__field(long,		fault_timestamp_ms)
		__field(int,		app_id)
		__string(log_tag,	log_tag)
		__string(event_id,	event_id)
		__string(log_type,	log_type)
		__string(extra_info,	extra_info)),

	TP_fast_assign(
		__entry->fault_timestamp_ms	= fault_timestamp_ms;
		__entry->app_id			= app_id;
		__assign_str(log_tag,		log_tag);
		__assign_str(event_id,		event_id);
		__assign_str(log_type,		log_type);
		__assign_str(extra_info,	extra_info);),
	TP_printk("fault_timestamp_ms:%ld app_id:%d log_tag:%s event_id:%s log_type:%s extra_info:%s",
		__entry->fault_timestamp_ms, __entry->app_id, __get_str(log_tag), __get_str(event_id), __get_str(log_type), __get_str(extra_info))
);

TRACE_EVENT(hungtask_monitor,
	TP_PROTO(long fault_timestamp_ms, int app_id, const char *log_tag, const char *event_id, const char *log_type,
				int duration, const char *task_name, int task_pid),
	TP_ARGS(fault_timestamp_ms, app_id, log_tag, event_id, log_type, duration, task_name, task_pid),
	TP_STRUCT__entry(
		__field(long,	fault_timestamp_ms)
		__field(int,		app_id)
		__string(log_tag,	log_tag)
		__string(event_id,	event_id)
		__string(log_type,	log_type)
		__field(int,	duration)
		__string(task_name,	task_name)
		__field(int,	task_pid)),

	TP_fast_assign(
		__entry->fault_timestamp_ms	= fault_timestamp_ms;
		__entry->app_id			= app_id;
		__assign_str(log_tag,		log_tag);
		__assign_str(event_id,		event_id);
		__assign_str(log_type, log_type);
		__entry->duration	= duration;
		__assign_str(task_name, task_name);
		__entry->task_pid	= task_pid;),
	TP_printk("fault_timestamp_ms:%ld app_id:%d log_tag:%s event_id:%s log_type:%s duration:%d task_name:%s task_pid:%d",
	__entry->fault_timestamp_ms, __entry->app_id, __get_str(log_tag), __get_str(event_id), __get_str(log_type),
	__entry->duration, __get_str(task_name), __entry->task_pid)
);
#endif /* _HUNGTASK_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../vendor/oplus/kernel/dfr/common/hung_task_enhance
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hungtask_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
