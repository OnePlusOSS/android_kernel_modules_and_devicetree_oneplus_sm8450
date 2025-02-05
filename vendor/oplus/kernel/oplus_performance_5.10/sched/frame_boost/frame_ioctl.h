/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#ifndef _SCHED_ASSIST_IOCTL_H_
#define _SCHED_ASSIST_IOCTL_H_

#define FRAMEBOOST_PROC_NODE "oplus_frame_boost"
#define INVALID_VAL (INT_MIN)

enum BoostStage {
	BOOST_NONE = 0,
	BOOST_ALL_STAGE = 1,
	BOOST_QUEUE_BUFFER_TIMEOUT,
	BOOST_DEADLINE_NOTIFY,
	BOOST_DEADLINE_EXCEEDED,

	BOOST_INPUT_ANIMATE = 100,
	BOOST_ANIMATE_TRAVERSAL,
	BOOST_TRAVERSAL_DRAW,
	BOOST_DRAW_SYNC_QUEUE,
	BOOST_SYNC_QUEUE_SYNC_START,
	BOOST_SYNC_START_ISSUE_DRAW,
	BOOST_ISSUE_DRAW_FLUSH,
	BOOST_FLUSH_COMPLETE,
	BOOST_RT_SYNC,

	BOOST_FRAME_START = 200,
	BOOST_MOVE_FG,
	BOOST_MOVE_BG,
	BOOST_OBTAIN_VIEW,
	BOOST_FRAME_END,
	BOOST_SET_HWUITASK,
	BOOST_INPUT_TIMEOUT = 206,
	BOOST_ANIMATION_TIMEOUT = 207,
	BOOST_INSETS_ANIMATION_TIMEOUT = 208,
	BOOST_TRAVERSAL_TIMEOUT = 209,
	BOOST_COMMIT_TIMEOUT = 210,
	FRAME_BOOST_END = 211,
	BOOST_FRAME_TIMEOUT = 212,
	BOOST_SET_RENDER_THREAD,
	BOOST_ADD_FRAME_TASK,

	BOOST_MSG_TRANS_START = 214,
	BOOST_CLIENT_COMPOSITION = 215,
	BOOST_SF_EXECUTE = 216,
};

enum ComposeStage     {
	NONE = 0,
	INVALIDATE,
	PRIMARY_PREPARE_FRAME,
	PRIMARY_FINISH_FRAME,
	PRIMARY_POST_FRAME,
	OTHER_PREPARE_FRAME,
	OTHER_FINISH_FRAME,
	OTHER_POST_FRAME,
	POST_COMP,
	GPU_BOOST_HINT_START = 100,
	GPU_BOOST_HINT_END,
};

enum ofb_ctrl_cmd_id {
	SET_FPS = 1,
	BOOST_HIT,
	END_FRAME,
	SF_FRAME_MISSED,
	SF_COMPOSE_HINT,
	IS_HWUI_RT,
	SET_TASK_TAGGING,
	SET_SF_MSG_TRANS,
	RELATED_SCHED_CONFIG,
	CMD_ID_MAX
};

struct ofb_ctrl_data {
	pid_t pid;
	pid_t tid;
	pid_t hwtid1;
	pid_t hwtid2;
	int level;
	int64_t frameNumber;
	int stage;
	int64_t sourceDelta;
	int64_t targetDelta;
	int64_t fpsNs;
	int64_t vsyncNs;
	int continuousdropframes;
	int util_min;
	int util_max;
	int boost_freq;
	int boost_migr;
	int vutil_margin;
	int capacity_need;
	int related_width;
	int related_depth;
};


#define OFB_MAGIC 0XDE
#define CMD_ID_SET_FPS \
	_IOWR(OFB_MAGIC, SET_FPS, struct ofb_ctrl_data)
#define CMD_ID_BOOST_HIT \
	_IOWR(OFB_MAGIC, BOOST_HIT, struct ofb_ctrl_data)
#define CMD_ID_END_FRAME \
	_IOWR(OFB_MAGIC, END_FRAME, struct ofb_ctrl_data)
#define CMD_ID_SF_FRAME_MISSED \
	_IOWR(OFB_MAGIC, SF_FRAME_MISSED, struct ofb_ctrl_data)
#define CMD_ID_SF_COMPOSE_HINT \
	_IOWR(OFB_MAGIC, SF_COMPOSE_HINT, struct ofb_ctrl_data)
#define CMD_ID_IS_HWUI_RT \
	_IOWR(OFB_MAGIC, IS_HWUI_RT, struct ofb_ctrl_data)
#define CMD_ID_SET_TASK_TAGGING \
	_IOWR(OFB_MAGIC, SET_TASK_TAGGING, struct ofb_ctrl_data)
#define CMD_ID_SET_SF_MSG_TRANS \
	_IOWR(OFB_MAGIC, SET_SF_MSG_TRANS, struct ofb_ctrl_data)
#define CMD_ID_RELATED_SCHED_CONFIG \
	_IOWR(OFB_MAGIC, RELATED_SCHED_CONFIG, struct ofb_ctrl_data)

extern int frame_ioctl_init(void);
#endif /* _SCHED_ASSIST_IOCTL_H_ */
