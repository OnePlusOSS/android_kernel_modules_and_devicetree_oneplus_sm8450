
#ifndef _OPLUS_QUIRKS_H_
#define _OPLUS_QUIRKS_H_

#define PLUGINFO_MAX_NUM		18
#define ABNORMAL_DISCONNECT_INTERVAL	1500
#define KEEP_CONNECT_TIME_OUT		(360*1000)
#define CONNECT_ERROR_COUNT_LEVEL_1	3
#define CONNECT_ERROR_COUNT_LEVEL_2	6
#define CONNECT_ERROR_COUNT_LEVEL_3	18
#define PLUGOUT_RETRY			1
#define PLUGOUT_RETRY			1
#define PLUGOUT_WAKEUP_TIMEOUT		(ABNORMAL_DISCONNECT_INTERVAL + 200)

enum  quirks_status {
	QUIRKS_NORMAL = 0,
	QUIRKS_STOP_ADSP_VOOCPHY,
	QUIRKS_STOP_PPS,
};

struct plug_info {
	unsigned long plugin_jiffies;
	unsigned long plugout_jiffies;
	bool abnormal_diconnect;
	int number;
	struct list_head list;
};

#endif /*_OPLUS_CHARGER_H_*/
