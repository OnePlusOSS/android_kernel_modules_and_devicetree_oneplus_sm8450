#ifndef _TASK_LOAD_H
#define _TASK_LOAD_H

#define task_load_err(fmt, ...) \
		printk(KERN_ERR "[TASK_LOAD_INFO_ERR][%s]"fmt, __func__, ##__VA_ARGS__)

enum {
	camera = 0,
	cameraserver,
	cameraprovider,
};


#endif /* _TASK_LOAD_H */

