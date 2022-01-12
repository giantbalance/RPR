/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "clock.h"
#include "../lib/refault.h"

void init_CLOCK(policy_t *self, unsigned long memsz);
void fini_CLOCK(policy_t *self);
int malloc_CLOCK(policy_t *self, unsigned long addr, unsigned long size);
int mfree_CLOCK(policy_t *self, unsigned long addr);
int access_CLOCK(policy_t *self, unsigned long addr);

data_CLOCK_t data_CLOCK;
policy_t policy_CLOCK = {
	.name = "clock",
	.init = init_CLOCK,
	.fini = fini_CLOCK,
	.access = access_CLOCK,
	.mem_alloc = malloc_CLOCK,
	.mem_free = mfree_CLOCK,
	.data = &data_CLOCK,
};

void init_CLOCK(policy_t *self, unsigned long memsz)
{
	data_CLOCK_t *data = self->data;
	unsigned long nr_pages = (memsz * 1024) >> PAGE_SHIFT;

	if (!nr_pages) {
		fprintf(stderr, "Memory size is too low!\n");
		exit(1);
	}

	data->nr_pages = nr_pages;
	data->nr_present = 0;

	pt_init(&data->pt);

	INIT_LIST_HEAD(&data->page_list);
}

void fini_CLOCK(policy_t *self)
{
	if (!refault_stat)
		goto skip_refault;

	printf("refault dist (avg): %E\n", avg_refault_dist());

skip_refault:
	/* Let page table freed automatically at program termination */
	return;
}

int malloc_CLOCK(policy_t *self, unsigned long addr, unsigned long size)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_ALLOC]++;
	return 0;
}

int mfree_CLOCK(policy_t *self, unsigned long addr)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_FREE]++;
	return 0;
}

void add_page_CLOCK(struct list_head *page_list, struct page *page)
{
	list_add(&page->entry, page_list);
}

void evict_page_CLOCK(policy_t *self)
{
	data_CLOCK_t *data = (data_CLOCK_t *)self->data;
	struct list_head *page_list = &data->page_list;
	struct page *page, *victim = NULL;

	while (!victim) {
		list_for_each_entry_reverse(page, page_list, entry) {
			if (!test_and_clear_page_young(page)) {
				victim = page;
				break;
			}
		}
	}

	reg_evict(page->addr);

	/* move page_list->next ~ victim to the tail */
	list_bulk_move_tail(page_list, page_list->next, &victim->entry);

	/* delete victim from the list */
	unmap_free_page(victim);
	data->nr_present--;
}

static inline bool is_full_CLOCK(policy_t *self)
{
	data_CLOCK_t *data = (data_CLOCK_t *)self->data;

	if (data->nr_pages == data->nr_present)
		return true;

	return false;
}

static void
page_fault_CLOCK(policy_t *self, unsigned long addr, struct page *page)
{
	data_CLOCK_t *data = (data_CLOCK_t *)self->data;
	pt_t *pt = data->pt;
	struct list_head *page_list = &data->page_list;

	reg_fault(addr);

	if (debug)
		printf("MISS\n");

	if (page) {
		/* wrong path */
		fprintf(stderr, "page fault but present?!\n");
		exit(1);
	}

	if (is_full_CLOCK(self))
		evict_page_CLOCK(self);

	page = map_alloc_page(pt, addr);
	add_page_CLOCK(page_list, page);

	data->nr_present++;
	if (self->cold_state && data->nr_present == data->nr_pages)
		self->cold_state = false;

	policy_count_stat(self, NR_MISS, 1);
}

static void
page_hit_CLOCK(policy_t *self, struct page *page)
{
	if (debug)
		printf("HIT\n");

	page_mkyoung(page);
	policy_count_stat(self, NR_HIT, 1);
}

int access_CLOCK(policy_t *self, unsigned long vpn)
{
	data_CLOCK_t *data = (data_CLOCK_t *)self->data;
	pt_t *pt = data->pt;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;

	cnt_access(1);

	page = pt_walk(pt, addr);

	if (!page)
		page_fault_CLOCK(self, addr, page);
	else
		page_hit_CLOCK(self, page);

	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
