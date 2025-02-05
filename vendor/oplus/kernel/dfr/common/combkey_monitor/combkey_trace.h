// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
*/
#if !defined(_COMBKEY_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _COMBKEY_TRACE_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM combkey

TRACE_EVENT(combkey_monitor,
	TP_PROTO(long time_s, int app_id, const char* log_tag, const char* event_id, const char* log_type),
	TP_ARGS(time_s, app_id, log_tag, event_id, log_type),
	TP_STRUCT__entry(
		__field(	long,	time_s)
		__field(	int,	app_id)
		__string(	log_tag,	log_tag)
		__string(	event_id,	event_id)
		__string(	log_type,	log_type)
	),
	TP_fast_assign(
		__entry->time_s= time_s;
		__entry->app_id	= app_id;
		__assign_str(log_tag, log_tag);
		__assign_str(event_id, event_id);
		__assign_str(log_type, log_type);
	),
	TP_printk("time_s:%ld app_id:%d log_tag:%s event_id:%s log_type:%s", __entry->time_s, __entry->app_id, __get_str(log_tag),
		__get_str(event_id), __get_str(log_type))
);

#endif /* _COMBKEY_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../vendor/oplus/kernel/dfr/common/combkey_monitor

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE combkey_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
