/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _SEQ_H
#define _SEQ_H

#include "../sim.h"
#include "../lib/pgtable.h"

#define MAX_NR_SEQ		200
#define N				5
#define L				20
#define M				20

enum dir {
	NIL = 0,
	DOWN,
	UP,
	NR_DIRS,
};

typedef struct {
	int nr_seq;
	unsigned long fault_time;
	struct list_head seq_list;
	struct list_head young_list;
} global_seq_t;

typedef struct {
	unsigned long low_end;
	unsigned long high_end;
	enum dir dir;

	unsigned long fault_time[N];
	int most_recent_idx;

	struct list_head entry;
	struct list_head sentry;	/* sorted by Nth most recent fault */
} seq_t;

typedef struct {
	unsigned long nr_pages;
	unsigned long nr_present;

	pt_t *pt;
	struct list_head page_list;

	global_seq_t *gseq;
} data_SEQ_t;

#endif
