/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _OPT_H
#define _OPT_H

#include "../sim.h"
#include "../lib/list.h"
#include "../lib/pgtable.h"
#include "../lib/avltree.h"

#define MEM_AREA_THRESHOLD			(PAGE_SIZE * 100)

typedef struct ref_node_t {
	unsigned long vpn;
	unsigned long rel_time;
	unsigned long time_left;
	unsigned long unlock_time;
	struct ref_node_t *next;
	struct list_head entry;
	struct avl_tree_node cand_node;
	struct avl_tree_node locked_node;
} ref_node_t;

typedef struct {
	unsigned long nr_present_acc;
	unsigned long nr_ref;
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

	unsigned long nr_present;

	ma_stat_t *stat;
	struct list_head entry;

	bool obsolete;
} mem_area_t;

typedef struct {
	unsigned long nr_pages;
	unsigned long nr_present;

	unsigned long rel_time;

	mem_area_t *def_ma;
	struct list_head ma_list;
	mem_stat_t *mem_stat;

	pt_t *pt;

	struct list_head ref_graph;
	struct list_head page_list;
} data_OPT_t;

#endif
