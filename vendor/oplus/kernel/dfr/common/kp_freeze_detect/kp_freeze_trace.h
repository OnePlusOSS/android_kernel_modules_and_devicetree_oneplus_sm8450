// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Your Company. All rights reserved.
 */

#if !defined(_TRACE_KP_FREEZE_DETECT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KP_FREEZE_DETECT_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kp_freeze_detect

TRACE_EVENT(key_process_frozen,
	TP_PROTO(long fault_timestamp_ms, int app_id, const char* log_tag, const char* event_id, const char *comm),
	TP_ARGS(fault_timestamp_ms, app_id, log_tag, event_id, comm),
	TP_STRUCT__entry(
		__field(	long,	fault_timestamp_ms)
		__field(	int,	app_id)
		__string(	log_tag,	log_tag)
		__string(	event_id,	event_id)
		__string(	comm,	comm)
	),
	TP_fast_assign(
		__entry->fault_timestamp_ms = fault_timestamp_ms;
		__entry->app_id = app_id;
		__assign_str(log_tag, log_tag);
		__assign_str(event_id, event_id)
		__assign_str(comm, comm);
	),
	TP_printk("fault_timestamp_ms:%ld app_id:%d log_tag:%s event_id:%s,key process (comm:%s)frozen detected",
		__entry->fault_timestamp_ms, __entry->app_id, __get_str(log_tag), __get_str(event_id),
		__get_str(comm))
);

#endif /* _TRACE_KP_FREEZE_PROTECT_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../vendor/oplus/kernel/dfr/common/kp_freeze_detect

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE kp_freeze_trace

/* This part must be outside protection */
#include <trace/define_trace.h>

