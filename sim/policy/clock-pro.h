/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _CLOCK_PRO_H
#define _CLOCK_PRO_H

#include "../sim.h"
#include "../lib/pgtable.h"

#define MEM_AREA_THRESHOLD			(PAGE_SIZE * 100)

typedef struct {
	unsigned long nr_pages;
	unsigned long nr_hot;
	unsigned long nr_cold;			// resident cold pages: max: nr_cold_max
	unsigned long nr_ghost;			// non-resident cold pages; max: nr_pages
	unsigned long nr_cold_max;		// self-tuning parameter; 0 at first
	unsigned long nr_ghost_max;		// same as nr_pages

	struct list_head page_list;

	/* For finding resident cold pages quickly */
	struct list_head cold_list;

	struct list_head *hand_hot;
	struct list_head *hand_cold;
	struct list_head *hand_test;
} clock_pro_t;

typedef struct {
	unsigned long nr_present_acc;
	unsigned long nr_hot_acc;
	unsigned long nr_cold_acc;
	unsigned long nr_ghost_acc;
	unsigned long nr_cold_max_acc;
	unsigned long nr_ref;
} clock_pro_stat_t;

typedef struct {
	unsigned long nr_present_acc;
	unsigned long nr_hot_acc;
	unsigned long nr_cold_acc;
	unsigned long nr_ghost_acc;
	unsigned long nr_cold_max_acc;
	unsigned long nr_ref;

	unsigned long nr_hand_hot_move;		/* sum of each watch's move */
	unsigned long nr_hand_cold_move;
	unsigned long nr_hand_test_move;
	unsigned long nr_promote;
	unsigned long nr_demote;
} ma_stat_t;

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

	unsigned long nr_hot;
	unsigned long nr_cold;			// resident cold pages: max: nr_cold_max
	unsigned long nr_ghost;			// non-resident cold pages; max: nr_pages

	ma_stat_t *stat;
	struct list_head entry;

	bool obsolete;
} mem_area_t;

typedef struct {
	mem_area_t *def_ma;
	struct list_head ma_list;
	clock_pro_t *clock;
	clock_pro_stat_t *stat;
	mem_stat_t *mem_stat;
	pt_t *pt;
} data_CLOCK_Pro_t;

#endif
