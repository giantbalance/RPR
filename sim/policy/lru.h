/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _LRU_H
#define _LRU_H

#include "../sim.h"
#include "../lib/list.h"
#include "../lib/pgtable.h"

typedef struct {
	unsigned long nr_pages;
	unsigned long nr_present;
	pt_t *pt;
	struct list_head page_list;
} data_LRU_t;

#endif
