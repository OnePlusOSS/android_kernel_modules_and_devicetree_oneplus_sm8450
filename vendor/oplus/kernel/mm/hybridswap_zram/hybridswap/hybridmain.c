// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HYB_ZRAM]" fmt

#include <linux/slab.h>
#include <linux/cpu.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/vmscan.h>
#include <linux/genhd.h>
#include <linux/proc_fs.h>
#include <linux/version.h>

#include "../zram_drv.h"
#include "../zram_drv_internal.h"
#include "internal.h"
#include "hybridswap.h"
#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
#include "chp_ext.h"
#endif /* CONFIG_OPLUS_FEATURE_SCHED_ASSIST */

#define HS_AID_APP_START 10000 /* first app user */
#define HS_AID_APP_END 19999   /* last app user */
#define HS_AID_USER_OFFSET 100000 /* offset for uid ranges for each user */

static const char *swapd_text[NR_EVENT_ITEMS] = {
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	"swapd_wakeup",
	"swapd_hit_refaults",
	"swapd_medium_press",
	"swapd_critical_press",
	"swapd_memcg_ratio_skip",
	"swapd_memcg_refault_skip",
	"swapd_shrink_anon",
	"swapd_swapout",
	"swapd_skip_swapout",
	"swapd_empty_round",
	"swapd_over_min_buffer_skip_times",
	"swapd_empty_round_skip_times",
	"swapd_snapshot_times",
	"swapd_skip_shrink_of_window",
	"swapd_manual_pause",
#ifdef CONFIG_OPLUS_JANK
	"swapd_cpu_busy_skip_times",
	"swapd_cpu_busy_break_times",
#endif
#endif
};

static int log_level = HYB_MAX;
static struct kmem_cache *hybridswap_cache;
static struct list_head score_head;
static DEFINE_SPINLOCK(score_list_lock);
static DEFINE_MUTEX(hybridswap_enable_lock);
static bool hybridswap_enabled;
struct hybridswapd_operations *hybridswapd_ops;

DEFINE_MUTEX(reclaim_para_lock);
DEFINE_PER_CPU(struct swapd_event_state, swapd_event_states);

void hybridswap_loglevel_set(int level)
{
	log_level = level;
}

int hybridswap_loglevel(void)
{
	return log_level;
}

void __put_memcg_cache(memcg_hybs_t *hybs)
{
	kmem_cache_free(hybridswap_cache, (void *)hybs);
}

static inline void sum_hybridswap_vm_events(unsigned long *ret)
{
	int cpu;
	int i;

	memset(ret, 0, NR_EVENT_ITEMS * sizeof(unsigned long));

	for_each_online_cpu(cpu) {
		struct swapd_event_state *this =
			&per_cpu(swapd_event_states, cpu);

		for (i = 0; i < NR_EVENT_ITEMS; i++)
			ret[i] += this->event[i];
	}
}

static inline void all_hybridswap_vm_events(unsigned long *ret)
{
	get_online_cpus();
	sum_hybridswap_vm_events(ret);
	put_online_cpus();
}

ssize_t hybridswap_vmstat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned long *vm_buf = NULL;
	int len = 0;
	int i;

	vm_buf = kzalloc(sizeof(struct swapd_event_state), GFP_KERNEL);
	if (!vm_buf)
		return -ENOMEM;
	all_hybridswap_vm_events(vm_buf);

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
			"fault_out_pause",
			atomic_long_read(hybridswapd_ops->fault_out_pause));
	len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
			"fault_out_pause_cnt",
			atomic_long_read(hybridswapd_ops->fault_out_pause_cnt));
#endif

	for (i = 0; i < NR_EVENT_ITEMS; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%-32s %12lu\n",
				swapd_text[i], vm_buf[i]);
		if (len == PAGE_SIZE)
			break;
	}
	kfree(vm_buf);

	return len;
}

ssize_t hybridswap_loglevel_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *type_buf = NULL;
	unsigned long val;

	type_buf = strstrip((char *)buf);
	if (kstrtoul(type_buf, 0, &val))
		return -EINVAL;

	if (val >= HYB_MAX) {
		log_err("val %lu is not valid\n", val);
		return -EINVAL;
	}
	hybridswap_loglevel_set((int)val);

	return len;
}

ssize_t hybridswap_loglevel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;

	size += scnprintf(buf + size, PAGE_SIZE - size,
			"Hybridswap log level: %d\n", hybridswap_loglevel());

	return size;
}

/* Make sure the memcg is not NULL in caller */
memcg_hybs_t *hybridswap_cache_alloc(struct mem_cgroup *memcg, bool atomic)
{
	memcg_hybs_t *hybs;
	u64 ret;
	gfp_t flags = GFP_KERNEL;

	if (memcg->android_oem_data1)
		BUG();

	if (atomic)
		flags &= ~__GFP_DIRECT_RECLAIM;

	hybs = (memcg_hybs_t *)kmem_cache_zalloc(hybridswap_cache, flags);
	if (unlikely(hybs == NULL)) {
		log_err("alloc memcg_hybs_t failed\n");
		return NULL;
	}

	INIT_LIST_HEAD(&hybs->score_node);
#ifdef CONFIG_HYBRIDSWAP_CORE
	spin_lock_init(&hybs->zram_init_lock);
#endif
	atomic64_set(&hybs->app_score, 300);
	atomic64_set(&hybs->ub_ufs2zram_ratio, 100);
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	atomic_set(&hybs->ub_mem2zram_ratio, 80);
	atomic_set(&hybs->ub_zram2ufs_ratio, 50);
	atomic_set(&hybs->refault_threshold, 50);
#endif
	hybs->memcg = memcg;
	refcount_set(&hybs->usage, 1);

	ret = atomic64_cmpxchg((atomic64_t *)&memcg->android_oem_data1, 0, (u64)hybs);
	if (ret != 0) {
		put_memcg_cache(hybs);
		return (memcg_hybs_t *)ret;
	}

	return hybs;
}

static void mem_cgroup_alloc_hook(void *data, struct mem_cgroup *memcg)
{
	if (memcg->android_oem_data1)
		BUG();

	hybridswap_cache_alloc(memcg, true);
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_HEALTHINFO)
bool is_fg_mem_cgroup(struct mem_cgroup *memcg)
{
	int uid, app_id;

	if (unlikely(!memcg || !MEMCGRP_ITEM_DATA(memcg)))
		return false;

	uid = (int)atomic64_read(&MEMCGRP_ITEM(memcg, app_uid));
	app_id = uid % HS_AID_USER_OFFSET;

	if (app_id < HS_AID_APP_START || app_id > HS_AID_APP_END)
		return false;

	if (is_fg(uid))
		return true;
	return false;
}

static void shrink_node_memcgs_hook(void *data, struct mem_cgroup *memcg,
				    bool *skip)
{
	*skip = is_fg_mem_cgroup(memcg);
}
#endif /* CONFIG_OPLUS_FEATURE_HEALTHINFO */

static void mem_cgroup_free_hook(void *data, struct mem_cgroup *memcg)
{
	memcg_hybs_t *hybs;

	if (!memcg->android_oem_data1)
		return;

	hybs = (memcg_hybs_t *)memcg->android_oem_data1;
	memcg->android_oem_data1 = 0;
	put_memcg_cache(hybs);
}

void memcg_app_score_update(struct mem_cgroup *target)
{
	struct list_head *pos = NULL;
	unsigned long flags;

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	hybridswapd_ops->update_memcg_param(target);
#endif
	spin_lock_irqsave(&score_list_lock, flags);
	list_for_each(pos, &score_head) {
		memcg_hybs_t *hybs = list_entry(pos, memcg_hybs_t, score_node);

		if (atomic64_read(&hybs->app_score) <
		    atomic64_read(&MEMCGRP_ITEM(target, app_score)))
			break;
	}
	list_move_tail(&MEMCGRP_ITEM(target, score_node), pos);
	spin_unlock_irqrestore(&score_list_lock, flags);
}

static void mem_cgroup_css_online_hook(void *data,
		struct cgroup_subsys_state *css, struct mem_cgroup *memcg)
{
	if (memcg->android_oem_data1) {
		memcg_app_score_update(memcg);
		css_get(css);
	}
}

static void mem_cgroup_css_offline_hook(void *data,
		struct cgroup_subsys_state *css, struct mem_cgroup *memcg)
{
	unsigned long flags;

	if (memcg->android_oem_data1) {
		spin_lock_irqsave(&score_list_lock, flags);
		list_del_init(&MEMCGRP_ITEM(memcg, score_node));
		spin_unlock_irqrestore(&score_list_lock, flags);
		css_put(css);
	}
}

#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
extern bool test_task_ux(struct task_struct *task);
#else
static inline bool test_task_ux(struct task_struct *task)
{
	return false;
}
#endif /* CONFIG_OPLUS_FEATURE_SCHED_ASSIST */

static void pcplist_add_cma_pages_bypass_hook(void *unused, int migratetype,
					      bool *ok)
{
	if (migratetype == CHP_VH_FREE_ZRAM_IS_OK)
		*ok = hybridswapd_ops->free_zram_is_ok();
	else if (migratetype == CHP_VH_CURRENT_IS_UX)
		*ok = test_task_ux(current);
}
#endif /* CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM */

#define REGISTER_HOOK(name) do {					\
	rc = register_trace_android_vh_##name(name##_hook, NULL);	\
	if (rc) {							\
		log_err("%s:%d register hook %s failed", __FILE__,	\
			__LINE__, #name);				\
		goto err_out_##name;					\
	}								\
} while (0)

#define UNREGISTER_HOOK(name) unregister_trace_android_vh_##name(name##_hook, NULL)

static int register_all_hooks(void)
{
	int rc;

	/* mem_cgroup_alloc_hook */
	REGISTER_HOOK(mem_cgroup_alloc);
	/* mem_cgroup_free_hook */
	REGISTER_HOOK(mem_cgroup_free);
	/* mem_cgroup_css_online_hook */
	REGISTER_HOOK(mem_cgroup_css_online);
	/* mem_cgroup_css_offline_hook */
	REGISTER_HOOK(mem_cgroup_css_offline);
#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
	REGISTER_HOOK(pcplist_add_cma_pages_bypass);
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	rc = register_trace_android_vh_rmqueue(hybridswapd_ops->vh_rmqueue, NULL);
	if (rc) {
		log_err("register rmqueue_hook failed\n");
		goto err_out_rmqueue;
	}

	rc = register_trace_android_vh_tune_scan_type(hybridswapd_ops->vh_tune_scan_type, NULL);
	if (rc) {
		log_err("register tune_scan_type_hook failed\n");
		goto err_out_tune_scan_type;
	}
#endif
#ifdef CONFIG_HYBRIDSWAP_CORE
	/* mem_cgroup_id_remove_hook */
	REGISTER_HOOK(mem_cgroup_id_remove);
#endif
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_HEALTHINFO)
	REGISTER_HOOK(shrink_node_memcgs);
#endif /* CONFIG_OPLUS_FEATURE_HEALTHINFO */
	return 0;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_HEALTHINFO)
	UNREGISTER_HOOK(shrink_node_memcgs);
err_out_shrink_node_memcgs:
#endif	/* CONFIG_OPLUS_FEATURE_HEALTHINFO */
#ifdef CONFIG_HYBRIDSWAP_CORE
	UNREGISTER_HOOK(mem_cgroup_id_remove);
err_out_mem_cgroup_id_remove:
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	unregister_trace_android_vh_tune_scan_type(hybridswapd_ops->vh_tune_scan_type, NULL);
err_out_tune_scan_type:
	unregister_trace_android_vh_rmqueue(hybridswapd_ops->vh_rmqueue, NULL);
err_out_rmqueue:
#endif
#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
	UNREGISTER_HOOK(pcplist_add_cma_pages_bypass);
err_out_pcplist_add_cma_pages_bypass:
#endif
	UNREGISTER_HOOK(mem_cgroup_css_offline);
err_out_mem_cgroup_css_offline:
	UNREGISTER_HOOK(mem_cgroup_css_online);
err_out_mem_cgroup_css_online:
	UNREGISTER_HOOK(mem_cgroup_free);
err_out_mem_cgroup_free:
	UNREGISTER_HOOK(mem_cgroup_alloc);
err_out_mem_cgroup_alloc:
	return rc;
}

static void unregister_all_hook(void)
{
	UNREGISTER_HOOK(mem_cgroup_alloc);
	UNREGISTER_HOOK(mem_cgroup_free);
	UNREGISTER_HOOK(mem_cgroup_css_offline);
	UNREGISTER_HOOK(mem_cgroup_css_online);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_HEALTHINFO)
	UNREGISTER_HOOK(shrink_node_memcgs);
#endif /* CONFIG_OPLUS_FEATURE_HEALTHINFO */
#ifdef CONFIG_HYBRIDSWAP_CORE
	UNREGISTER_HOOK(mem_cgroup_id_remove);
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	unregister_trace_android_vh_tune_scan_type(hybridswapd_ops->vh_tune_scan_type, NULL);
	unregister_trace_android_vh_rmqueue(hybridswapd_ops->vh_rmqueue, NULL);
#endif
}

unsigned long memcg_anon_pages(struct mem_cgroup *memcg)
{
	if (unlikely(!memcg))
		return 0;

	return (memcg_page_state_local(memcg, NR_ACTIVE_ANON) +
		memcg_page_state_local(memcg, NR_INACTIVE_ANON));
}

unsigned long memcg_file_pages(struct mem_cgroup *memcg)
{
	if (unlikely(!memcg))
		return 0;

	return (memcg_page_state_local(memcg, NR_ACTIVE_FILE) +
		memcg_page_state_local(memcg, NR_INACTIVE_FILE));
}

static unsigned long memcg_lru_pages(struct mem_cgroup *memcg, enum lru_list lru)
{
	if (!memcg)
		return 0;

	return memcg_page_state_local(memcg, NR_LRU_BASE + lru);
}

/* Shrink by free a batch of pages */
static ssize_t force_shrink_batch(struct mem_cgroup * memcg,
				  unsigned long nr_need_reclaim,
				  unsigned long batch,
				  bool may_swap)
{
	unsigned long nr_reclaimed = 0;

	while (nr_reclaimed < nr_need_reclaim) {
		unsigned long reclaimed;
		reclaimed = try_to_free_mem_cgroup_pages(memcg,
			batch, GFP_KERNEL, may_swap);

		if (reclaimed == 0)
			break;

		nr_reclaimed += reclaimed;

		/* Abort shrink when abort_shrink */
		if (MEMCGRP_ITEM_DATA(memcg) &&
		    MEMCGRP_ITEM(memcg, abort_shrink)) {
			/* reset abort_shrink */
			MEMCGRP_ITEM(memcg, abort_shrink) = false;
			log_info("abort reclaim\n");
			break;
		}
	}

	return nr_reclaimed;
}

#define	RECLAIM_INACTIVE	0
#define	RECLAIM_ALL		1
unsigned long get_reclaim_pages(struct mem_cgroup *memcg, bool file,
				char *buf, unsigned long *batch)
{
	unsigned long nr_need_reclaim = 0;
	unsigned long reclaim_flag = 0;
	unsigned long reclaim_batch = 0;
	int ret;
	int lru_file = file ? LRU_FILE : 0;

	buf = strstrip(buf);
	ret = sscanf(buf, "%lu %lu", &reclaim_flag, &reclaim_batch);
	if (ret != 1 && ret != 2) {
		log_err("reclaim_flag %s value is error!\n",  buf);
		return 0;
	}

	if (reclaim_flag == RECLAIM_INACTIVE)
		nr_need_reclaim = memcg_lru_pages(memcg, LRU_BASE + lru_file);
	else if (reclaim_flag == RECLAIM_ALL)
		nr_need_reclaim = memcg_lru_pages(memcg, LRU_BASE + lru_file) +
			memcg_lru_pages(memcg, LRU_BASE + lru_file + LRU_ACTIVE);
	else
		nr_need_reclaim = reclaim_flag;

	if (reclaim_batch > 0)
		*batch = reclaim_batch;

	log_info("%s batch %lu, nr_need_reclaim %lu, file %d\n",
		__func__, *batch, nr_need_reclaim, file);
	return nr_need_reclaim;
}

#define	BATCH_4M	(1 << 10)
#define	BATCH_8M	(1 << 11)
static ssize_t mem_cgroup_force_shrink_anon(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg;
	unsigned long nr_need_reclaim, nr_reclaimed;
	unsigned long batch = BATCH_4M;

	memcg = mem_cgroup_from_css(of_css(of));
	nr_need_reclaim = get_reclaim_pages(memcg, false, buf, &batch);

	/* In the hook of scan_type, only reclaim anon */
	current->flags |= PF_SHRINK_ANON;
	/* Set may_swap as true, to reclaim file+anon */
	nr_reclaimed = force_shrink_batch(memcg, nr_need_reclaim,
					  batch, true);
	current->flags &= ~PF_SHRINK_ANON;

	log_info("%s try to reclaim %d pages and reclaim %d pages\n",
		 MEMCGRP_ITEM(memcg, name), nr_need_reclaim, nr_reclaimed);
	return nbytes;
}

static ssize_t mem_cgroup_force_shrink_file(struct kernfs_open_file *of,
		char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg;
	unsigned long nr_need_reclaim, nr_reclaimed;
	unsigned long batch = BATCH_8M;

	memcg = mem_cgroup_from_css(of_css(of));
	nr_need_reclaim = get_reclaim_pages(memcg, true, buf, &batch);

	/* Set may_swap as false, to reclaim file which cannot swap for now */
	nr_reclaimed = force_shrink_batch(memcg, nr_need_reclaim,
					  batch, false);

	log_info("%s try to reclaim %d pages and reclaim %d pages\n",
		 MEMCGRP_ITEM(memcg, name), nr_need_reclaim, nr_reclaimed);
	return nbytes;
}

static int memcg_total_info_per_app_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long anon_size;
	unsigned long zram_compress_size;
	unsigned long eswap_compress_size;
	unsigned long zram_page_size;
	unsigned long eswap_page_size;
	int uid;

	seq_printf(m, "%-8s %-8s %-8s %-8s %-8s  %-8s %s\n",
		   "anon", "zram_c", "zram_p", "eswap_c", "eswap_p",
		   "app_uid", "memcg_n");
	while ((memcg = get_next_memcg(memcg))) {
		if (!MEMCGRP_ITEM_DATA(memcg))
			continue;

		anon_size = memcg_anon_pages(memcg);
		zram_compress_size = hybridswap_read_memcg_stats(memcg,
				MCG_ZRAM_STORED_SZ);
		eswap_compress_size = hybridswap_read_memcg_stats(memcg,
				MCG_DISK_STORED_SZ);
		zram_page_size = hybridswap_read_memcg_stats(memcg,
				MCG_ZRAM_STORED_PG_SZ);
		eswap_page_size = hybridswap_read_memcg_stats(memcg,
				MCG_DISK_STORED_PG_SZ);
		uid = (int)atomic64_read(&MEMCGRP_ITEM(memcg, app_uid));

		anon_size *= PAGE_SIZE / SZ_1K;
		zram_compress_size /= SZ_1K;
		eswap_compress_size /= SZ_1K;
		zram_page_size *= PAGE_SIZE / SZ_1K;
		eswap_page_size *= PAGE_SIZE / SZ_1K;

		seq_printf(m, "%-8lu %-8lu %-8lu %-8lu %-8lu %-8d %s\n",
			   anon_size, zram_compress_size, zram_page_size,
			   eswap_compress_size, eswap_page_size, uid,
			   MEMCGRP_ITEM(memcg, name));
	}

	return 0;
}

static int memcg_swap_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long eswap_out_cnt;
	unsigned long eswap_out_size;
	unsigned long eswap_in_size;
	unsigned long eswap_in_cnt;
	unsigned long page_fault_cnt;
	unsigned long cur_eswap_size;
	unsigned long max_eswap_size;
	unsigned long zram_compress_size, zram_page_size;
	unsigned long eswap_compress_size, eswap_page_size;

	memcg = mem_cgroup_from_css(seq_css(m));

	zram_compress_size = hybridswap_read_memcg_stats(memcg, MCG_ZRAM_STORED_SZ);
	zram_page_size = hybridswap_read_memcg_stats(memcg, MCG_ZRAM_STORED_PG_SZ);
	eswap_compress_size = hybridswap_read_memcg_stats(memcg, MCG_DISK_STORED_SZ);
	eswap_page_size = hybridswap_read_memcg_stats(memcg, MCG_DISK_STORED_PG_SZ);

	eswap_out_cnt = hybridswap_read_memcg_stats(memcg, MCG_ESWAPOUT_CNT);
	eswap_out_size = hybridswap_read_memcg_stats(memcg, MCG_ESWAPOUT_SZ);
	eswap_in_size = hybridswap_read_memcg_stats(memcg, MCG_ESWAPIN_SZ);
	eswap_in_cnt = hybridswap_read_memcg_stats(memcg, MCG_ESWAPIN_CNT);
	page_fault_cnt = hybridswap_read_memcg_stats(memcg, MCG_DISK_FAULT_CNT);
	cur_eswap_size = hybridswap_read_memcg_stats(memcg, MCG_DISK_SPACE);
	max_eswap_size = hybridswap_read_memcg_stats(memcg, MCG_DISK_SPACE_PEAK);

	seq_printf(m, "%-32s %12lu KB\n", "zramCompressedSize:",
			zram_compress_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "zramOrignalSize:",
			zram_page_size << (PAGE_SHIFT - 10));
	seq_printf(m, "%-32s %12lu KB\n", "eswapCompressedSize:",
			eswap_compress_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "eswapOrignalSize:",
			eswap_page_size << (PAGE_SHIFT - 10));
	seq_printf(m, "%-32s %12lu\n", "eswapOutTotal:", eswap_out_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapOutSize:", eswap_out_size / SZ_1K);
	seq_printf(m, "%-32s %12lu\n", "eswapInTotal:", eswap_in_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapInSize:", eswap_in_size / SZ_1K);
	seq_printf(m, "%-32s %12lu\n", "pageInTotal:", page_fault_cnt);
	seq_printf(m, "%-32s %12lu KB\n", "eswapSizeCur:", cur_eswap_size / SZ_1K);
	seq_printf(m, "%-32s %12lu KB\n", "eswapSizeMax:", max_eswap_size / SZ_1K);

	return 0;
}

static ssize_t mem_cgroup_name_write(struct kernfs_open_file *of, char *buf,
		size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	memcg_hybs_t *hybp = MEMCGRP_ITEM_DATA(memcg);
	int len, w_len;

	if (unlikely(hybp == NULL)) {
		hybp = hybridswap_cache_alloc(memcg, false);
		if (!hybp)
			return -EINVAL;
	}

	buf = strstrip(buf);
	len = strlen(buf) + 1;
	if (len > MEM_CGROUP_NAME_MAX_LEN)
		len = MEM_CGROUP_NAME_MAX_LEN;

	w_len = snprintf(hybp->name, len, "%s", buf);
	if (w_len > len)
		hybp->name[len - 1] = '\0';

	return nbytes;
}

static int mem_cgroup_name_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	seq_printf(m, "%s\n", MEMCGRP_ITEM(memcg, name));

	return 0;
}

static int mem_cgroup_app_score_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg;
	memcg_hybs_t *hybs;

	if (val > MAX_APP_SCORE || val < 0)
		return -EINVAL;

	memcg = mem_cgroup_from_css(css);
	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs) {
		hybs = hybridswap_cache_alloc(memcg, false);
		if (!hybs)
			return -EINVAL;
	}

	if (atomic64_read(&MEMCGRP_ITEM(memcg, app_score)) != val)
		atomic64_set(&MEMCGRP_ITEM(memcg, app_score), val);
	memcg_app_score_update(memcg);

	return 0;
}

static s64 mem_cgroup_app_score_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, app_score));
}

int mem_cgroup_app_uid_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg;
	memcg_hybs_t *hybs;

	if (val < 0)
		return -EINVAL;

	memcg = mem_cgroup_from_css(css);
	hybs = MEMCGRP_ITEM_DATA(memcg);

	if (unlikely(hybs == NULL)) {
		hybs = hybridswap_cache_alloc(memcg, false);
		if (!hybs)
			return -EINVAL;
	}

	if (atomic64_read(&MEMCGRP_ITEM(memcg, app_uid)) != val)
		atomic64_set(&MEMCGRP_ITEM(memcg, app_uid), val);

	return 0;
}

static s64 mem_cgroup_app_uid_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, app_uid));
}

static int mem_cgroup_ub_ufs2zram_ratio_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	if (val > MAX_RATIO || val < MIN_RATIO)
		return -EINVAL;

	atomic64_set(&MEMCGRP_ITEM(memcg, ub_ufs2zram_ratio), val);

	return 0;
}

static s64 mem_cgroup_ub_ufs2zram_ratio_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	return atomic64_read(&MEMCGRP_ITEM(memcg, ub_ufs2zram_ratio));
}

static int mem_cgroup_force_swapin_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	memcg_hybs_t *hybs;
	unsigned long size = 0;
	const unsigned int ratio = 100;
	int ret;

	hybs = MEMCGRP_ITEM_DATA(memcg);
	if (!hybs)
		return -EPERM;

#ifdef	CONFIG_HYBRIDSWAP_CORE
	size = atomic64_read(&hybs->hybridswap_stored_size);
#endif
	size = atomic64_read(&hybs->ub_ufs2zram_ratio) * size / ratio;
	size = EXTENT_ALIGN_UP(size);

	if (!size)
		return 0;

#ifdef CONFIG_HYBRIDSWAP_CORE
	ret = hybridswap_batch_out(memcg, size, val ? true : false);
#endif
	log_err("ret:%d uid:%d size:%lu\n", ret,
		(int)atomic64_read(&hybs->app_uid), size);

	return 0;
}

static int mem_cgroup_force_swapout_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
#ifdef CONFIG_HYBRIDSWAP_CORE
	hybridswap_force_reclaim(mem_cgroup_from_css(css));
#endif
	return 0;
}

static int mem_cgroup_abort_shrink_write(struct cgroup_subsys_state *css,
		struct cftype *cft, s64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (!MEMCGRP_ITEM_DATA(memcg))
		return -EPERM;

	if (val)
		MEMCGRP_ITEM(memcg, abort_shrink) = true;
	else
		MEMCGRP_ITEM(memcg, abort_shrink) = false;

	log_info("%s abort shrink\n", __func__);
	return 0;
}


struct mem_cgroup *get_next_memcg(struct mem_cgroup *prev)
{
	memcg_hybs_t *hybs = NULL;
	struct mem_cgroup *memcg = NULL;
	struct list_head *pos = NULL;
	unsigned long flags;
	bool prev_got = true;

	spin_lock_irqsave(&score_list_lock, flags);
find_again:
	if (unlikely(!prev))
		pos = &score_head;
	else
		pos = &MEMCGRP_ITEM(prev, score_node);

	if (list_empty(pos)) /* deleted node */
		goto unlock;

	if (pos->next == &score_head)
		goto unlock;

	hybs = list_entry(pos->next, struct mem_cgroup_hybridswap, score_node);
	memcg = hybs->memcg;
	if (unlikely(!memcg))
		goto unlock;

	if (!css_tryget(&memcg->css)) {
		if (prev && prev_got)
			css_put(&prev->css);
		prev = memcg;
		prev_got = false;
		goto find_again;
	}

unlock:
	spin_unlock_irqrestore(&score_list_lock, flags);
	if (prev && prev_got)
		css_put(&prev->css);

	return memcg;
}

void get_next_memcg_break(struct mem_cgroup *memcg)
{
	if (memcg)
		css_put(&memcg->css);
}

static struct cftype mem_cgroup_hybridswap_legacy_files[] = {
	{
		.name = "force_shrink_anon",
		.write = mem_cgroup_force_shrink_anon,
	},
	{
		.name = "force_shrink_file",
		.write = mem_cgroup_force_shrink_file,
	},
	{
		.name = "total_info_per_app",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = memcg_total_info_per_app_show,
	},
	{
		.name = "swap_stat",
		.seq_show = memcg_swap_stat_show,
	},
	{
		.name = "name",
		.write = mem_cgroup_name_write,
		.seq_show = mem_cgroup_name_show,
	},
	{
		.name = "app_score",
		.write_s64 = mem_cgroup_app_score_write,
		.read_s64 = mem_cgroup_app_score_read,
	},
	{
		.name = "app_uid",
		.write_s64 = mem_cgroup_app_uid_write,
		.read_s64 = mem_cgroup_app_uid_read,
	},
	{
		.name = "ub_ufs2zram_ratio",
		.write_s64 = mem_cgroup_ub_ufs2zram_ratio_write,
		.read_s64 = mem_cgroup_ub_ufs2zram_ratio_read,
	},
	{
		.name = "force_swapin",
		.write_s64 = mem_cgroup_force_swapin_write,
	},
	{
		.name = "force_swapout",
		.write_s64 = mem_cgroup_force_swapout_write,
	},
	{
		.name = "abort_shrink",
		.write_s64 = mem_cgroup_abort_shrink_write,
	},
#ifdef CONFIG_HYBRIDSWAP_CORE
	{
		.name = "psi",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = hybridswap_psi_show,
	},
	{
		.name = "stored_wm_ratio",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.write_s64 = mem_cgroup_stored_wm_ratio_write,
		.read_s64 = mem_cgroup_stored_wm_ratio_read,
	},
#endif
	{ }, /* terminate */
};

static int hybridswap_enable(struct zram **zram_arr)
{
	int ret = 0;

	if (hybridswap_enabled) {
		log_warn("enabled is true\n");
		return ret;
	}

	if (!hybridswap_get_stat_obj()) {
		log_warn("hybridswap_get_stat_obj() is null\n");
		return -EINVAL;
	}

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	ret = hybridswapd_ops->init(zram_arr);
	if (ret)
		return ret;
#endif

#if defined(CONFIG_HYBRIDSWAP_CORE)
	if (!chp_supported) {
		ret = hybridswap_core_enable();
		if (ret)
			goto hybridswap_core_enable_fail;
	}
#endif
	hybridswap_enabled = true;

	return 0;

#if defined(CONFIG_HYBRIDSWAP_CORE)
hybridswap_core_enable_fail:
#endif
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	hybridswapd_ops->deinit();
#endif
	return ret;
}

static void hybridswap_disable(struct zram **zram)
{
	if (!hybridswap_enabled) {
		log_warn("enabled is false\n");
		return;
	}

#ifdef CONFIG_HYBRIDSWAP_CORE
	hybridswap_core_disable();
#endif

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	hybridswapd_ops->deinit();
#endif
	hybridswap_enabled = false;
}

ssize_t hybridswap_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = snprintf(buf, PAGE_SIZE, "hybridswap %s reclaim_in %s swapd %s\n",
			hybridswap_core_enabled() ? "enable" : "disable",
			hybridswap_reclaim_in_enable() ? "enable" : "disable",
			hybridswapd_ops->enabled() ? "enable" : "disable");

	return len;
}

ssize_t hybridswap_swapd_pause_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	char *type_buf = NULL;
	bool val;

	type_buf = strstrip((char *)buf);
	if (kstrtobool(type_buf, &val))
		return -EINVAL;
	atomic_set(hybridswapd_ops->swapd_pause, val);
	return len;
}

ssize_t hybridswap_swapd_pause_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;

	size += scnprintf(buf + size, PAGE_SIZE - size,
			  "%d\n", atomic_read(hybridswapd_ops->swapd_pause));
	return size;
}

ssize_t hybridswap_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned long val;
	char *kbuf;
	struct zram *zram;

	kbuf = strstrip((char *)buf);
	ret = kstrtoul(kbuf, 0, &val);
	if (unlikely(ret)) {
		log_err("val %s is invalid!\n", kbuf);

		return -EINVAL;
	}

	mutex_lock(&hybridswap_enable_lock);
	zram = dev_to_zram(dev);
	if (val == 0)
		hybridswap_disable(zram_arr);
	else
		ret = hybridswap_enable(zram_arr);
	mutex_unlock(&hybridswap_enable_lock);

	if (ret == 0)
		ret = len;
	return ret;
}

int __init hybridswap_pre_init(void)
{
	int ret;

	INIT_LIST_HEAD(&score_head);
	log_level = HS_LOG_INFO;

	hybridswap_cache = kmem_cache_create("mem_cgroup_hybridswap",
			sizeof(struct mem_cgroup_hybridswap),
			0, SLAB_PANIC, NULL);
	if (!hybridswap_cache) {
		log_err("create hybridswap_cache failed\n");
		ret = -ENOMEM;
		return ret;
	}

	ret = cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
			mem_cgroup_hybridswap_legacy_files);
	if (ret) {
		log_info("add mem_cgroup_hybridswap_legacy_files failed\n");
		goto error_out;
	}

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	hybridswapd_ops = kzalloc(sizeof(struct hybridswapd_operations),
				  GFP_KERNEL);
	if (!hybridswapd_ops)
		goto error_out;

#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
	if (chp_supported && chp_pool) {
		log_info("init for hybridswapd_chp_ops");
		hybridswapd_chp_ops_init(hybridswapd_ops);
	} else {
		log_info("init for hybridswapd_ops");
		hybridswapd_ops_init(hybridswapd_ops);
	}
#else
	hybridswapd_ops_init(hybridswapd_ops);
#endif
	hybridswapd_ops->pre_init();

	ret = cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
					hybridswapd_ops->memcg_legacy_files);
	if (ret) {
		log_info("add mem_cgroup_swapd_legacy_files failed!\n");
		goto fail_out;
	}
#endif
	ret = register_all_hooks();
	if (ret)
		goto fail_out;

	log_info("hybridswap inited success!\n");
	return 0;

fail_out:
#ifdef CONFIG_HYBRIDSWAP_SWAPD
	hybridswapd_ops->pre_deinit();
	kfree(hybridswapd_ops);
#endif
error_out:
	kmem_cache_destroy(hybridswap_cache);
	hybridswap_cache = NULL;
	return ret;
}

void __exit hybridswap_exit(void)
{
	unregister_all_hook();

#ifdef CONFIG_HYBRIDSWAP_SWAPD
	hybridswapd_ops->pre_deinit();
#endif
	kmem_cache_destroy(hybridswap_cache);
	hybridswap_cache = NULL;
}
