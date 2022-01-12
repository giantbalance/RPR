/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "lru.h"

void init_LRU(policy_t *self, unsigned long memsz);
void fini_LRU(policy_t *self);
int malloc_LRU(policy_t *self, unsigned long addr, unsigned long size);
int mfree_LRU(policy_t *self, unsigned long addr);
int access_LRU(policy_t *self, unsigned long addr);

data_LRU_t data_LRU;
policy_t policy_LRU = {
	.name = "lru",
	.init = init_LRU,
	.fini = fini_LRU,
	.access = access_LRU,
	.mem_alloc = malloc_LRU,
	.mem_free = mfree_LRU,
	.data = &data_LRU,
};

void init_LRU(policy_t *self, unsigned long memsz)
{
	data_LRU_t *data = self->data;
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

void fini_LRU(policy_t *self)
{
	/* Let page table freed automatically at program termination */
	return;
}

int malloc_LRU(policy_t *self, unsigned long addr, unsigned long size)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_ALLOC]++;
	return 0;
}

int mfree_LRU(policy_t *self, unsigned long addr)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_FREE]++;
	return 0;
}

void evict_page_LRU(policy_t *self)
{
	data_LRU_t *data = (data_LRU_t *)self->data;
	struct page *page;

	/* Get page at the tail (MRU) of the list */
	page = list_last_entry(&data->page_list, struct page, entry);

	unmap_free_page(page);
	data->nr_present--;
}

static inline void add_page_LRU(struct list_head *page_list, struct page *page)
{
	list_add(&page->entry, page_list);
}

static inline void update_page_LRU(struct list_head *page_list, struct page *page)
{
	list_move(&page->entry, page_list);
}

static inline bool is_full_LRU(policy_t *self)
{
	data_LRU_t *data = (data_LRU_t *)self->data;

	if (data->nr_pages == data->nr_present)
		return true;

	return false;
}

static void
page_fault_LRU(policy_t *self, unsigned long addr, struct page *page)
{
	data_LRU_t *data = (data_LRU_t *)self->data;
	pt_t *pt = data->pt;
	struct list_head *page_list = &data->page_list;

	if (debug)
		printf("MISS\n");

	if (page) {
		/* wrong path */
		fprintf(stderr, "page fault but present?!\n");
		exit(1);
	}

	if (is_full_LRU(self))
		evict_page_LRU(self);

	page = map_alloc_page(pt, addr);

	data->nr_present++;
	if (self->cold_state && data->nr_present == data->nr_pages)
		self->cold_state = false;

	add_page_LRU(page_list, page);
	policy_count_stat(self, NR_MISS, 1);
}

static inline void
page_hit_LRU(policy_t *self, struct page *page)
{
	data_LRU_t *data = (data_LRU_t *)self->data;
	struct list_head *page_list = &data->page_list;

	if (debug)
		printf("HIT\n");

	update_page_LRU(page_list, page);

	policy_count_stat(self, NR_HIT, 1);
}

int access_LRU(policy_t *self, unsigned long vpn)
{
	data_LRU_t *data = (data_LRU_t *)self->data;
	pt_t *pt = data->pt;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;

	page = pt_walk(pt, addr);

	if (!page)
		page_fault_LRU(self, addr, page);
	else
		page_hit_LRU(self, page);

	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
