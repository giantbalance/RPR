/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "fifo.h"

void init_FIFO(policy_t *self, unsigned long memsz);
void fini_FIFO(policy_t *self);
int malloc_FIFO(policy_t *self, unsigned long addr, unsigned long size);
int mfree_FIFO(policy_t *self, unsigned long addr);
int access_FIFO(policy_t *self, unsigned long addr);

data_FIFO_t data_FIFO;
policy_t policy_FIFO = {
	.name = "fifo",
	.init = init_FIFO,
	.fini = fini_FIFO,
	.access = access_FIFO,
	.mem_alloc = malloc_FIFO,
	.mem_free = mfree_FIFO,
	.data = &data_FIFO,
};

void init_FIFO(policy_t *self, unsigned long memsz)
{
	data_FIFO_t *data = self->data;
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

void fini_FIFO(policy_t *self)
{
	/* Let page table freed automatically at program termination */
	return;
}

int malloc_FIFO(policy_t *self, unsigned long addr, unsigned long size)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_ALLOC]++;
	return 0;
}

int mfree_FIFO(policy_t *self, unsigned long addr)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_FREE]++;
	return 0;
}

void evict_page_FIFO(policy_t *self)
{
	data_FIFO_t *data = (data_FIFO_t *)self->data;
	struct page *page;

	/* Get page at the tail (MRU) of the list */
	page = list_last_entry(&data->page_list, struct page, entry);

	unmap_free_page(page);
	data->nr_present--;
}

static inline void add_page_FIFO(struct list_head *page_list, struct page *page)
{
	list_add(&page->entry, page_list);
}

static inline void update_page_FIFO(struct list_head *page_list, struct page *page)
{
	list_move(&page->entry, page_list);
}

static inline bool is_full_FIFO(policy_t *self)
{
	data_FIFO_t *data = (data_FIFO_t *)self->data;

	if (data->nr_pages == data->nr_present)
		return true;

	return false;
}

static void
page_fault_FIFO(policy_t *self, unsigned long addr, struct page *page)
{
	data_FIFO_t *data = (data_FIFO_t *)self->data;
	pt_t *pt = data->pt;
	struct list_head *page_list = &data->page_list;

	if (debug)
		printf("MISS\n");

	if (page) {
		/* wrong path */
		fprintf(stderr, "page fault but present?!\n");
		exit(1);
	}

	if (is_full_FIFO(self))
		evict_page_FIFO(self);

	page = map_alloc_page(pt, addr);

	data->nr_present++;
	if (self->cold_state && data->nr_present == data->nr_pages)
		self->cold_state = false;

	add_page_FIFO(page_list, page);
	policy_count_stat(self, NR_MISS, 1);
}

static inline void
page_hit_FIFO(policy_t *self, struct page *page)
{
	if (debug)
		printf("HIT\n");

	policy_count_stat(self, NR_HIT, 1);
}

int access_FIFO(policy_t *self, unsigned long vpn)
{
	data_FIFO_t *data = (data_FIFO_t *)self->data;
	pt_t *pt = data->pt;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;

	page = pt_walk(pt, addr);

	if (!page)
		page_fault_FIFO(self, addr, page);
	else
		page_hit_FIFO(self, page);

	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
