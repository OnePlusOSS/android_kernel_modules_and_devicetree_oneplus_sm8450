// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2020-2022 Oplus. All rights reserved.
*/
#include <linux/export.h>
#include <linux/module.h>
#include <linux/rmap.h>
#include <linux/bitops.h>
#include <trace/hooks/vmscan.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/debug.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/sys.h>
#include <trace/hooks/mm.h>

#define LOOK_AROUND_MAX 8

#define PG_lookaround_ref (__NR_PAGEFLAGS + 1)
#define SetPageLookAroundRef(page) set_bit(PG_lookaround_ref, &(page)->flags)
#define ClearPageLookAroundRef(page) clear_bit(PG_lookaround_ref, &(page)->flags)
#define TestClearPageLookAroundRef(page) test_and_clear_bit(PG_lookaround_ref, &(page)->flags)


enum page_references {
	PAGEREF_RECLAIM,
	PAGEREF_RECLAIM_CLEAN,
	PAGEREF_KEEP,
	PAGEREF_ACTIVATE,
};

static void page_referenced_look_around(struct page_vma_mapped_walk *pvmw)
{
	int i;
	pte_t *pte;
	unsigned long start;
	unsigned long end;
	unsigned long addr;
	unsigned long look_around_pages = LOOK_AROUND_MAX;
	struct pglist_data *pgdat = page_pgdat(pvmw->page);
	unsigned long bitmap[BITS_TO_LONGS(LOOK_AROUND_MAX *2)] = {};

	lockdep_assert_held(pvmw->ptl);
	VM_BUG_ON_PAGE(PageTail(pvmw->page), pvmw->page);

	start = max(pvmw->address & PMD_MASK, pvmw->vma->vm_start);
	end = pmd_addr_end(pvmw->address, pvmw->vma->vm_end);

	if (end - start > look_around_pages * 2 * PAGE_SIZE) {
		if (pvmw->address - start < look_around_pages * PAGE_SIZE)
			end = start + look_around_pages * 2 * PAGE_SIZE;
		else if (end - pvmw->address < look_around_pages * PAGE_SIZE)
			start = end - look_around_pages * 2 * PAGE_SIZE;
		else {
			start = pvmw->address - look_around_pages * PAGE_SIZE;
			end = pvmw->address + look_around_pages * PAGE_SIZE;
		}
	}
	pte = pvmw->pte - (pvmw->address - start) / PAGE_SIZE;

	arch_enter_lazy_mmu_mode();

	for (i = 0, addr = start; addr != end; i++, addr += PAGE_SIZE) {
		struct page *page;
		unsigned long pfn = pte_pfn(pte[i]);

		if (!pte_present(pte[i]) || is_zero_pfn(pfn))
			continue;

		if (WARN_ON_ONCE(pte_devmap(pte[i]) || pte_special(pte[i])))
			continue;

		if (!pte_young(pte[i]))
			continue;

		VM_BUG_ON(!pfn_valid(pfn));
		if (pfn < pgdat->node_start_pfn || pfn >= pgdat_end_pfn(pgdat))
			continue;

		page = compound_head(pfn_to_page(pfn));
		if (page_to_nid(page) != pgdat->node_id)
			continue;

		VM_BUG_ON(addr < pvmw->vma->vm_start || addr >= pvmw->vma->vm_end);
		if (!ptep_test_and_clear_young(pvmw->vma, addr, pte + i))
			continue;

		if (pte_dirty(pte[i]) && !PageDirty(page) &&
		    !(PageAnon(page) && PageSwapBacked(page) && !PageSwapCache(page)))
			__set_bit(i, bitmap);

		/*
		 * mark the neighbour pages lookaroundref so that we skip
		 * rmap in eviction accordingly
		 */
		SetPageLookAroundRef(page);
		if (pvmw->vma->vm_flags & VM_EXEC)
			SetPageReferenced(page);
	}

	arch_leave_lazy_mmu_mode();

	for_each_set_bit(i, bitmap, look_around_pages * 2)
		set_page_dirty(pte_page(pte[i]));
}

static void look_around_handler(void *unused, struct page_vma_mapped_walk *pvmw, struct page *page, struct vm_area_struct *vma, int *referenced)
{
	if (!PageActive(page) && pte_young(*pvmw->pte) &&
		!(vma->vm_flags & (VM_SEQ_READ | VM_RAND_READ))) {
		page_referenced_look_around(pvmw);
		*referenced += 1;
	}
}

static void check_page_look_around_ref_handler(void *unused, struct page *page, int *look_around_ref)
{
	/*
	* look-around has seen the page is active so we can skip the rmap
	* we can neither use PG_active nor invent a new PG_ flag, so we
	* hardcode PG_lookaround_ref by __NR_PAGEFLAGS + 1
	*/
	if (TestClearPageLookAroundRef(page)) {
		if (PageReferenced(page))
			*look_around_ref = PAGEREF_ACTIVATE;
		else {
			SetPageReferenced(page);
			*look_around_ref = PAGEREF_KEEP;
		}
	} else {
		*look_around_ref = 0;
	}
}

static void test_clear_look_around_ref_handler(void *unused, struct page *page)
{
	ClearPageLookAroundRef(page);
}

static void look_around_migrate_page_handler(void *unused, struct page *old_page, struct page *new_page)
{
	if (TestClearPageLookAroundRef(old_page)) {
		SetPageReferenced(new_page);
	}
}

static int register_look_around_vendor_hooks(void)
{
	int ret = 0;
	ret = register_trace_android_vh_look_around(look_around_handler, NULL);
	if (ret != 0) {
		pr_err("register look_around vendor_hooks failed! ret=%d\n", ret);
		goto out;
	}

	ret = register_trace_android_vh_check_page_look_around_ref(check_page_look_around_ref_handler, NULL);
	if (ret != 0) {
		pr_err("register check_page_look_around_ref vendor_hooks failed! ret=%d\n", ret);
		goto out1;
	}

	ret = register_trace_android_vh_test_clear_look_around_ref(test_clear_look_around_ref_handler, NULL);
	if (ret != 0) {
		pr_err("register check_page_look_around_ref vendor_hooks failed! ret=%d\n", ret);
		goto out2;
	}
	ret = register_trace_android_vh_look_around_migrate_page(look_around_migrate_page_handler, NULL);
	if (ret != 0) {
		pr_err("register check_page_look_around_ref vendor_hooks failed! ret=%d\n", ret);
		goto out3;
	}
	return ret;

out3:
	register_trace_android_vh_test_clear_look_around_ref(test_clear_look_around_ref_handler, NULL);
out2:
	unregister_trace_android_vh_check_page_look_around_ref(check_page_look_around_ref_handler, NULL);
out1:
	unregister_trace_android_vh_look_around(look_around_handler, NULL);
out:
	return ret;
}

static void unregister_look_around_vendor_hooks(void)
{
	unregister_trace_android_vh_look_around(look_around_handler, NULL);
	unregister_trace_android_vh_check_page_look_around_ref(check_page_look_around_ref_handler, NULL);
	unregister_trace_android_vh_test_clear_look_around_ref(test_clear_look_around_ref_handler, NULL);
	unregister_trace_android_vh_look_around_migrate_page(look_around_migrate_page_handler, NULL);
}

static int __init look_around_init(void)
{
	int ret = 0;

	ret = register_look_around_vendor_hooks();
	if (ret != 0)
		return ret;
	pr_info("look_around_init succeed!\n");
	return 0;
}

static void __exit look_around_exit(void)
{
	unregister_look_around_vendor_hooks();
	pr_info("look_around exit succeed!\n");
	return;
}

module_init(look_around_init);
module_exit(look_around_exit);

MODULE_LICENSE("GPL v2");
