/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "refault.h"

/* Data structures */
typedef struct {
	unsigned long refault_dist_acc;
	unsigned long nr_evict;
	unsigned long nr_access;
} refault_stat_t;

typedef struct {
	unsigned long addr;
	unsigned long time_evict;
	struct list_head entry;
} refault_data_t;

typedef struct {
	struct list_head list;
	pt_t *pt;
} refault_table_t;

/* Variables */
static refault_stat_t refault_stat;
static refault_table_t refault_table;

static bool need_init = true;
static bool done = false;

/* Handlers */
static void
init_refault(void)
{
	refault_stat.refault_dist_acc = 0;
	refault_stat.nr_evict = 0;
	refault_stat.nr_access = 0;

	INIT_LIST_HEAD(&refault_table.list);
	pt_init(&refault_table.pt);

	need_init = false;
}

static refault_data_t *
new_data(unsigned long addr)
{
	refault_data_t *new = malloc(sizeof(*new));

	new->addr = addr;
	new->time_evict = refault_stat.nr_access;

	return new;
}

void reg_evict(unsigned long addr)
{
	refault_data_t *data;

	assert(!need_init);
	assert(!done);

	data = new_data(addr);

	list_add_tail(&data->entry, &refault_table.list);

	/* obsolete page from OPT does not have addr */
	if (addr)
		assert(!map_page(refault_table.pt, addr, (struct page *)data));

	refault_stat.nr_evict++;
}

static void
__reg_fault(refault_data_t *data)
{
	refault_stat.refault_dist_acc +=
		refault_stat.nr_access - data->time_evict;

	/* obsolete page from OPT does not have addr */
	if (data->addr)
		unmap_addr(refault_table.pt, data->addr);

	list_del(&data->entry);
	free(data);
}

void reg_fault(unsigned long addr)
{
	refault_data_t *data;

	assert(!done);

	if (need_init)
		init_refault();

	data = (refault_data_t *)pt_walk(refault_table.pt, addr);
	if (!data)
		return;		/* not a refault */

	__reg_fault(data);
}

void cnt_access(unsigned long cnt)
{
	refault_stat.nr_access += cnt;
}

double
avg_refault_dist(void)
{
	refault_data_t *data, *n;
	double avg;

	list_for_each_entry_safe(data, n, &refault_table.list, entry)
		__reg_fault(data);

	avg = (double) refault_stat.refault_dist_acc / refault_stat.nr_evict;
	done = true;

	return avg;
}
