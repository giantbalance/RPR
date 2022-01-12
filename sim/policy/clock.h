/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _CLOCK_H
#define _CLOCK_H

#include "../sim.h"
#include "../lib/pgtable.h"

typedef struct {
	unsigned long nr_pages;
	unsigned long nr_present;
	pt_t *pt;
	struct list_head page_list;
} data_CLOCK_t;

#endif
