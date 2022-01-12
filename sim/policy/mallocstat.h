/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _MALLOCSTAT_H
#define _MALLOCSTAT_H

#include "../sim.h"
#include "../lib/list.h"

#define MEM_AREA_THRESHOLD		(PAGE_SIZE * 10)

typedef struct {
	unsigned long req_start;		/* inclusive */
	unsigned long req_end;			/* exclusive */

	/* page-aligned boundaries */
	unsigned long start;			/* inclusive */
	unsigned long end;				/* exclusive */

	struct list_head entry;
} mem_area_t;

typedef struct {
	unsigned long nr_pages;
	unsigned long nr_present;
	mem_area_t *def_ma;
	struct list_head ma_list;
	struct list_head ma_list_obs;
} data_mallocstat_t;

#endif
