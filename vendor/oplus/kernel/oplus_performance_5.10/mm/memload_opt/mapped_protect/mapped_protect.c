// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "mapped-protect: " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <trace/hooks/vmscan.h>
#include <trace/hooks/mm.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/gfp.h>
#include <linux/printk.h>

static void should_skip_page_referenced(void *data, struct page *page, unsigned long nr_to_scan, int lru, bool *bypass)
{
	if ((atomic_read(&page->_mapcount) + 1) >= 20)
		*bypass = true;
}

static int register_mapped_protect_vendor_hooks(void)
{
	int ret = 0;

	ret = register_trace_android_vh_page_referenced_check_bypass(should_skip_page_referenced, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_skip_page_referenced failed! ret=%d\n", ret);
		goto out;
	}

out:
	return ret;
}

static void unregister_mapped_protect_vendor_hooks(void)
{
	unregister_trace_android_vh_page_referenced_check_bypass(should_skip_page_referenced, NULL);

	return;
}

static int __init mapped_protect_init(void)
{
	int ret = 0;

	ret = register_mapped_protect_vendor_hooks();
	if (ret != 0)
		return ret;

	pr_info("mapped_protect_init succeed!\n");
	return 0;
}

static void __exit mapped_protect_exit(void)
{
	unregister_mapped_protect_vendor_hooks();

	pr_info("mapped_protect_exit exit succeed!\n");

	return;
}

module_init(mapped_protect_init);
module_exit(mapped_protect_exit);

MODULE_LICENSE("GPL v2");
