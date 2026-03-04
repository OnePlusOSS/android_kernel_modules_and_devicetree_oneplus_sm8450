#ifndef __OP_CHG_MODULE_H__
#define __OP_CHG_MODULE_H__

#include <linux/types.h>
#include <linux/module.h>
#include <linux/version.h>

#ifdef MODULE

typedef int (*chg_module_init_t) (void);
typedef void (*chg_module_exit_t) (void);

struct oplus_chg_module {
	const char *name;
	size_t magic;
	chg_module_init_t chg_module_init;
	chg_module_exit_t chg_module_exit;
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))

#define OPLUS_CHG_MODULE_MAGIC 0x20300000

#define oplus_chg_module_register(__name)			\
__attribute__((section(".oplus_chg_module.normal.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODULE_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#define oplus_chg_module_core_register(__name)			\
__attribute__((section(".oplus_chg_module.core.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODULE_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#define oplus_chg_module_early_register(__name)			\
__attribute__((section(".oplus_chg_module.early.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODULE_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#define oplus_chg_module_late_register(__name)			\
__attribute__((section(".oplus_chg_module.late.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODULE_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)) */

#define OPLUS_CHG_MODULE_CORE_MAGIC	0x20300000
#define OPLUS_CHG_MODULE_EARLY_MAGIC	0x20300001
#define OPLUS_CHG_MODULE_NORMAL_MAGIC	0x20300002
#define OPLUS_CHG_MODULE_LATE_MAGIC	0x20300003

#define oplus_chg_module_register(__name)				\
__attribute__((section(".oplus_chg_module.normal.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_NORMAL_MAGIC,				\
	.chg_module_init = __name##_init,				\
	.chg_module_exit = __name##_exit,				\
}

#define oplus_chg_module_register_null(__name)				\
__attribute__((section(".oplus_chg_module.normal.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_NORMAL_MAGIC,				\
	.chg_module_init = NULL,					\
	.chg_module_exit = NULL,					\
}

#define oplus_chg_module_core_register(__name)				\
__attribute__((section(".oplus_chg_module.core.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_CORE_MAGIC,				\
	.chg_module_init = __name##_init,				\
	.chg_module_exit = __name##_exit,				\
}

#define oplus_chg_module_core_register_null(__name)			\
__attribute__((section(".oplus_chg_module.core.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_CORE_MAGIC,				\
	.chg_module_init = NULL,					\
	.chg_module_exit = NULL,					\
}

#define oplus_chg_module_early_register(__name)				\
__attribute__((section(".oplus_chg_module.early.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_EARLY_MAGIC,				\
	.chg_module_init = __name##_init,				\
	.chg_module_exit = __name##_exit,				\
}

#define oplus_chg_module_early_register_null(__name)			\
__attribute__((section(".oplus_chg_module.early.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_EARLY_MAGIC,				\
	.chg_module_init = NULL,					\
	.chg_module_exit = NULL,					\
}

#define oplus_chg_module_late_register(__name)				\
__attribute__((section(".oplus_chg_module.late.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_LATE_MAGIC,				\
	.chg_module_init = __name##_init,				\
	.chg_module_exit = __name##_exit,				\
}

#define oplus_chg_module_late_register_null(__name)			\
__attribute__((section(".oplus_chg_module.late.data"), used))		\
struct oplus_chg_module __name##_module = {				\
	.name = #__name,						\
	.magic = OPLUS_CHG_MODULE_LATE_MAGIC,				\
	.chg_module_init = NULL,					\
	.chg_module_exit = NULL,					\
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)) */

#else /* MODULE */

#define oplus_chg_module_register(__name)	\
	module_init(__name##_init);		\
	module_exit(__name##_exit)

#define oplus_chg_module_core_register(__name)	\
	fs_initcall(__name##_init);		\
	module_exit(__name##_exit)

#define oplus_chg_module_early_register(__name)	\
	rootfs_initcall(__name##_init);		\
	module_exit(__name##_exit)

#define oplus_chg_module_late_register(__name)	\
	late_initcall(__name##_init);		\
	module_exit(__name##_exit)

#endif /* MODULE */

#endif /* __OP_CHG_MODULE_H__ */
