/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "mallocstat.h"

void init_mallocstat(policy_t *self, unsigned long memsz);
void fini_mallocstat(policy_t *self);
int malloc_mallocstat(policy_t *self, unsigned long addr, unsigned long size);
int mfree_mallocstat(policy_t *self, unsigned long addr);
int access_mallocstat(policy_t *self, unsigned long addr);

data_mallocstat_t data_mallocstat;
policy_t policy_mallocstat = {
	.name = "mallocstat",
	.init = init_mallocstat,
	.fini = fini_mallocstat,
	.access = access_mallocstat,
	.mem_alloc = malloc_mallocstat,
	.mem_free = mfree_mallocstat,
	.data = &data_mallocstat,
};

static void
mem_area_init(mem_area_t **ma, unsigned long req_start, unsigned long req_end,
		unsigned long start, unsigned long end)
{
	*ma = malloc(sizeof(mem_area_t));
	(*ma)->req_start = req_start;
	(*ma)->req_end = req_end;
	(*ma)->start = start;
	(*ma)->end = end;
}

void init_mallocstat(policy_t *self, unsigned long memsz)
{
	data_mallocstat_t *data = self->data;
	unsigned long nr_pages = (memsz * 1024) >> PAGE_SHIFT;
	mem_area_t *def_ma;

	if (!nr_pages) {
		fprintf(stderr, "Memory size is too low!\n");
		exit(1);
	}

	data->nr_pages = nr_pages;
	data->nr_present = 0;

	mem_area_init(&def_ma, 0, 0, 0, 0);
	data->def_ma = def_ma;
	INIT_LIST_HEAD(&data->ma_list);
	INIT_LIST_HEAD(&data->ma_list_obs);
}

void fini_mallocstat(policy_t *self)
{
	data_mallocstat_t *data = self->data;
	struct list_head *ma_list = &data->ma_list;
	struct list_head *ma_list_obs = &data->ma_list_obs;
	mem_area_t *ma;

	unsigned long ma_size_total = 0;
	unsigned long ma_size;
	unsigned long nr_ma = 0;

	printf("================ memory areas =================\n");

	list_for_each_entry(ma, ma_list, entry) {
		ma_size = ma->end - ma->start;
		ma_size_total += ma_size;
		printf("[%#14lx - %#14lx (%lu)]\n", ma->start, ma->end, ma_size);
		nr_ma++;
	}

	list_for_each_entry(ma, ma_list_obs, entry) {
		ma_size = ma->end - ma->start;
		ma_size_total += ma_size;
		printf("[%#14lx - %#14lx (%lu)]\n", ma->start, ma->end, ma_size);
		nr_ma++;
	}

	if (nr_ma) {
		printf("============== memory area stats ==============\n");
		printf("# memory areas: %lu\n", nr_ma);
		printf("memory area size (avg): %lu\n", ma_size_total / nr_ma);
	}
}

static void
create_mem_area(data_mallocstat_t *data, unsigned long addr, unsigned long size)
{
	struct list_head *ma_list = &data->ma_list;
	struct list_head *next_entry = NULL;
	mem_area_t *ma, *new;
	unsigned long req_start, req_end;
	unsigned long start, end;

	req_start = addr;
	req_end = addr + size;
	start = PAGE_ALIGN(req_start);
	end = PAGE_ALIGN(req_end);

	/* Small chunks are managed by default buffer */
	if (end - start < MEM_AREA_THRESHOLD)
		return;

	/* Check for overlapping areas */
	list_for_each_entry(ma, ma_list, entry) {
		if (ma->req_start < req_end && req_start < ma->req_end) {
			/* Overlap! */
			fprintf(stderr, "Overlapping memory area!\n");
			exit(1);
		}

		if (req_end <= ma->req_start) {
			next_entry = &ma->entry;
			break;
		}
	}

	if (!next_entry)
		next_entry = ma_list;

	/* Alloc a new mem_area_t */
	mem_area_init(&new, req_start, req_end, start, end);

	/* Add to list */
	list_add_tail(&new->entry, next_entry);
}

static void
free_mem_area(data_mallocstat_t *data, unsigned long addr)
{
	struct list_head *ma_list = &data->ma_list;
	struct list_head *ma_list_obs = &data->ma_list_obs;
	mem_area_t *ma, *victim = NULL;

	list_for_each_entry(ma, ma_list, entry) {
		if (ma->req_start <= addr && addr < ma->req_end) {
			victim = ma;
			break;
		}
	}

	/* case 1: free from default buffer; no-op */
	if (!victim)
		return;

	/* case 2: free memory area */
	list_move_tail(&victim->entry, ma_list_obs);
}

int malloc_mallocstat(policy_t *self, unsigned long addr, unsigned long size)
{
	data_mallocstat_t *data = self->data;

	create_mem_area(data, addr, size);

	policy_count_stat(self, NR_MEM_ALLOC, 1);
	return 0;
}

int mfree_mallocstat(policy_t *self, unsigned long addr)
{
	data_mallocstat_t *data = self->data;

	free_mem_area(data, addr);

	policy_count_stat(self, NR_MEM_FREE, 1);
	return 0;
}

static inline bool is_full_mallocstat(policy_t *self)
{
	data_mallocstat_t *data = (data_mallocstat_t *)self->data;

	if (data->nr_pages == data->nr_present)
		return true;

	return false;
}

int access_mallocstat(policy_t *self, unsigned long vpn)
{
	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
