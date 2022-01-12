/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _WATCH_PRO_H
#define _WATCH_PRO_H

#include "../sim.h"
#include "../lib/list.h"
#include "../lib/pgtable.h"

/* TODO: Find the proper threshold value */
#define MEM_AREA_THRESHOLD			(PAGE_SIZE * 100)

typedef struct {
	unsigned long nr_present_acc;
	unsigned long nr_hot_acc;
	unsigned long nr_cold_acc;
	unsigned long nr_ghost_acc;
	unsigned long nr_cold_max_acc;
	unsigned long nr_ref;

	unsigned long nr_hand_hot_move;
	unsigned long nr_hand_cold_move;
	unsigned long nr_hand_test_move;
	unsigned long nr_promote;
	unsigned long nr_demote;
} gclock_stat_t;

typedef struct {
	unsigned long nr_present_acc;
	unsigned long nr_hot_acc;
	unsigned long nr_cold_acc;
	unsigned long nr_hot_global_acc;
	unsigned long nr_cold_global_acc;
	unsigned long nr_ghost_acc;
	unsigned long nr_cold_max_acc;
	unsigned long nr_ref;

	unsigned long nr_hand_cold_move;
	unsigned long nr_hand_hot_move;
	unsigned long nr_promote;
	unsigned long nr_demote;
} watch_stat_t;

/* global clock for managing cold pages; similar to the list Q in LIRS */
typedef struct {
	/* stats below represent the global stats */
	unsigned long nr_pages;
	unsigned long nr_hot;
	unsigned long nr_cold;
	unsigned long nr_ghost;
	unsigned long nr_ghost_max;
	double cold_ratio;

	struct list_head *hand_hot;
	struct list_head *hand_test;
	struct list_head page_list;
	/* For finding resident pages quickly */
	struct list_head cold_list;

	gclock_stat_t *stat;
} gclock_t;

/* watch (per-object clock); similar to the stack S in LIRS  */
typedef struct {
	unsigned long nr_hot;
	unsigned long nr_cold;
	unsigned long nr_hot_global;
	unsigned long nr_cold_global;
	unsigned long nr_ghost;
	double cold_ratio;

	struct list_head *hand_hot;
	struct list_head page_list;
	/* For finding resident cold pages quickly */
	struct list_head cold_list;

	struct page *mrf;

	watch_stat_t *stat;

	bool obsolete;					/* is in freed memory area */
} watch_t;

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
	unsigned long req_start;		/* inclusive */
	unsigned long req_end;			/* exclusive */

	/* page-aligned boundaries */
	unsigned long start;			/* inclusive */
	unsigned long end;				/* exclusive */
	struct list_head entry;
	watch_t *watch;
} mem_area_t;

typedef struct {
	mem_area_t *def_ma;
	struct list_head ma_list;
	gclock_t *gclock;
	mem_stat_t *mem_stat;
	pt_t *pt;
} data_WATCH_Pro_t;

#endif
