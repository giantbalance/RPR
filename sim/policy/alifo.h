/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _ALIFO_H
#define _ALIFO_H

#include "../sim.h"
#include "../lib/pgtable.h"
#include "../lib/avltree.h"

#define MEM_AREA_THRESHOLD			(PAGE_SIZE * 10)
#define DECAY_FACTOR_DEFAULT		0.9

enum chal_policy {
	DRAW = 0,
	CLOCK,
	LIFO,
};

typedef struct {
	unsigned long malloc_size_max;
	unsigned long malloc_size_acc;
	unsigned long malloc_cnt;
	unsigned long mfree_cnt;
	unsigned long ma_alloc_cnt;
	unsigned long ma_free_cnt;
	unsigned long nr_mem_area;
} mem_stat_t;

typedef struct {
	unsigned long nr_present_acc;
	unsigned long nr_ref;
	unsigned long nr_clock;
	unsigned long nr_lifo;

	unsigned long clock_win;
	unsigned long lifo_win;
	unsigned long draw;
} pol_stat_t;

typedef struct {
	unsigned long nr_present;
	unsigned long nr_entry;
	pol_stat_t *stat;

	struct list_head *reclaim_head;
	struct list_head page_list;

	unsigned long time;
	unsigned long last_stime;
	long double decay;
	long double lifo_weight;
	long double clock_weight;

	bool lifo;
} pol_t;

typedef struct {
	unsigned long req_start;		/* inclusive */
	unsigned long req_end;			/* exclusive */

	/* page-aligned boundaries */
	unsigned long start;			/* inclusive */
	unsigned long end;				/* exclusive */

	struct list_head entry;

	pol_t *pol;

	bool obsolete;
} mem_area_t;

typedef struct {
	unsigned long nr_pages;
	unsigned long nr_present;
	unsigned long nr_ghost;

	mem_area_t *def_ma;
	struct list_head ma_list;
	mem_stat_t *mem_stat;

	pt_t *pt;

	struct list_head *reclaim_head;
	struct list_head page_list;
	struct list_head ghost_list;
} data_aLIFO_t;

#endif
