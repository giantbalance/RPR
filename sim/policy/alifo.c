/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "alifo.h"
#include "../lib/refault.h"

void init_aLIFO(policy_t *self, unsigned long memsz);
void fini_aLIFO(policy_t *self);
int malloc_aLIFO(policy_t *self, unsigned long addr, unsigned long size);
int mfree_aLIFO(policy_t *self, unsigned long addr);
int access_aLIFO(policy_t *self, unsigned long addr);

data_aLIFO_t data_aLIFO;
policy_t policy_aLIFO = {
	.name = "alifo",
	.init = init_aLIFO,
	.fini = fini_aLIFO,
	.access = access_aLIFO,
	.mem_alloc = malloc_aLIFO,
	.mem_free = mfree_aLIFO,
	.data = &data_aLIFO,
};

#define MAX_POW_BIT					64

/* pow_val[x] := base^(2^x) */
double pow_val[MAX_POW_BIT];

static void
spow_init(double base)
{
	int i;

	pow_val[0] = base;
	for (i = 1; i < MAX_POW_BIT; i++)
		pow_val[i] = pow_val[i - 1] * pow_val[i - 1];
}

/* O(log N) implementation of power algorithm */
static double
spow(double base, unsigned long power)
{
	double res = 1;
	int bit;

	for (bit = 0; bit <= MAX_POW_BIT; bit++) {
		if (power & 0x1UL)
			res = res * pow_val[bit];
		power = power >> 1;
	}

	return res;
}


/* RL control meta data */
#define INITIAL_WEIGHT 0.5
long double learning_rate;
double discount_rate;


/*
 * Page metadata DSs and APIs
 */
typedef struct {
	bool gref;						/* global ref */
	bool lref;						/* local ref */
	bool marked;					/* marked if chosen by challenger */

	/*
	 * curr: current policy
	 * chal: challenging policy
	 */
	bool evicted;					/* true if evicted by chal or curr */
	bool present;					/* true if evicted by curr */

	bool lifo;						/* true iif curr is lifo at eviction */
	unsigned long stime;			/* challenge start time */
	bool challenging;

	pol_t *pol;
} page_md_t;

#define page_md(_page)				((page_md_t *)(_page)->private)
#define page_pol(_page)				(page_md(_page)->pol)
#define set_page_pol(_page, _pol)	{ page_pol(_page) = _pol; }

#define page_evicted(_page)			(page_md(_page)->evicted)
#define page_present(_page)			(page_md(_page)->present)
#define page_vevicted(_page)		(page_evicted(_page) && page_present(_page))
#define page_pevicted(_page)		(page_evicted(_page) && !page_present(_page))
#define page_evict_lifo(_page)		(page_evicted(_page) && (page_md(_page)->lifo))
#define page_evict_clock(_page)		(page_evicted(_page) && !(page_md(_page)->lifo))

#define page_starttime(_page)		(page_md(_page)->stime)
#define page_challenging(_page)		(page_md(_page)->challenging)

#define __page_young_local(_page)		(page_md((_page))->lref)
#define __page_young_global(_page)		(page_md((_page))->gref)
#define __page_mkyoung_local(_page)		{ __page_young_local(_page) = true; }
#define __page_mkyoung_global(_page)	{ __page_young_global(_page) = true; }
#define __page_mkold_local(_page)		{ __page_young_local(_page) = false; }
#define __page_mkold_global(_page)		{ __page_young_global(_page) = false; }
#define __page_vevict(_page)			{ page_vevicted(_page) = true; }
#define __page_pevict(_page)			{ page_pevicted(_page) = true; }

static inline bool
page_young_local(struct page *page)
{
	return page_young(page) || __page_young_local(page);
}

static inline bool
page_young_global(struct page *page)
{
	return page_young(page) || __page_young_global(page);
}

static inline void
page_mkold_local(struct page *page)
{
	if (test_and_clear_page_young(page))
		__page_mkyoung_global(page);

	__page_mkold_local(page);
}

static inline void
page_mkold_global(struct page *page)
{
	if (test_and_clear_page_young(page))
		__page_mkyoung_local(page);

	__page_mkold_global(page);
}

static inline void
page_mkold_all(struct page *page)
{
	page_mkold(page);
	__page_mkold_local(page);
	__page_mkold_global(page);
}

static inline void
vevict_page(struct page *page, int policy)
{
	page_evicted(page) = true;
	page_present(page) = true;
}

static void
pevict_page(struct page *page, int policy)
{
	data_aLIFO_t *data = &data_aLIFO;
	pol_t *pol = page_pol(page);

	page_evicted(page) = true;
	page_present(page) = false;

	assert(policy != DRAW);
	if (policy == CLOCK)
		list_del_init(&page->gentry);
	else {
		if (pol->reclaim_head == &page->entry)
			pol->reclaim_head = pol->reclaim_head->prev;
		list_del_init(&page->entry);
	}

	list_add(&page->centry, &data->ghost_list);

	if (policy == LIFO)
		pol->nr_entry--;

	pol->nr_present--;
	data->nr_present--;
	data->nr_ghost++;
}

static inline void
clear_page_evicted(struct page *page)
{
	page_evicted(page) = false;
}

static inline void
set_page_present(struct page *page)
{
	page_present(page) = true;
}

static inline void
clear_page_present(struct page *page)
{
	page_present(page) = false;
}

static inline void
set_page_evict_clock(struct page *victim)
{
	page_md(victim)->evicted = true;
	page_md(victim)->lifo = false;
}

static inline void
set_page_evict_lifo(struct page *victim)
{
	page_md(victim)->evicted = true;
	page_md(victim)->lifo = true;
}

static inline void
set_page_starttime(struct page *victim, unsigned long stime)
{
	page_starttime(victim) = stime;
}

static inline void
set_page_challenging(struct page *page)
{
	page_challenging(page) = true;
}

static inline void
clear_page_challenging(struct page *page)
{
	page_challenging(page) = false;
}

static inline void
refresh_page_chal(struct page *page)
{
	page_challenging(page) = false;
	clear_page_evicted(page);
	set_page_present(page);
}

static inline void
alloc_page_md(struct page *page)
{
	page->private = malloc(sizeof(page_md_t));
	__page_mkold_local(page);
	__page_mkold_global(page);

	clear_page_evicted(page);
	set_page_present(page);

	set_page_starttime(page, 0);
	refresh_page_chal(page);
}

static inline void
free_page_md(struct page *page)
{
	free(page->private);
}

static inline bool
pol_lifo(pol_t *pol)
{
	return pol->lifo;
}

static inline bool
pol_clock(pol_t *pol)
{
	return !pol->lifo;
}

static inline bool
pol_empty(pol_t *pol)
{
	return pol->nr_present == 0;
}

static inline unsigned long
nr_pol_entry(pol_t *pol)
{
	return pol->nr_entry;
}

static void
__snapshot_global_list(struct list_head *page_list)
{
	struct page *page;
	unsigned long addr;
	char *ref, *evict, *policy;
	char *reclaim_head;
	bool top = true;

	printf("GLOBAL LIST =====================================\n");

	list_for_each_entry(page, page_list, gentry) {
		addr = (page->addr / 0x1000) % 0x1000;
		if (page_young(page))
			ref = "R";
		else if (__page_young_global(page))
			ref = "r";
		else
			ref = "";

		if (page_vevicted(page))
			evict = "v";
		else if (page_pevicted(page))
			evict = "p";
		else
			evict = "";

		if (page_evict_clock(page))
			policy = "clock";
		else if (page_evict_lifo(page))
			policy = "lifo";
		else
			policy = "";

		if (top) {
			reclaim_head = "reclaim_head";
			top = false;
		} else {
			reclaim_head = "";
		}

		printf("%3lx%2s%2s %-5s%16s\n", addr, ref, evict, policy, reclaim_head);
	}

	printf("=================================================\n");
}

static void
__snapshot_local_list(pol_t *pol)
{
	struct page *page;
	unsigned long addr;
	char *ref, *evict, *policy;
	char *reclaim_head;

	printf("LOCAL LIST ======================================\n");

	list_for_each_entry(page, &pol->page_list, entry) {
		addr = (page->addr / 0x1000) % 0x1000;
		if (page_young(page))
			ref = "R";
		else if (__page_young_local(page))
			ref = "r";
		else
			ref = "";

		if (page_vevicted(page))
			evict = "v";
		else if (page_pevicted(page))
			evict = "p";
		else
			evict = "";

		if (page_evict_clock(page))
			policy = "clock";
		else if (page_evict_lifo(page))
			policy = "lifo";
		else
			policy = "";

		if (pol->reclaim_head == &page->entry)
			reclaim_head = "reclaim_head";
		else
			reclaim_head = "";

		printf("%3lx%2s%2s %-5s%16s\n", addr, ref, evict, policy, reclaim_head);
	}

	printf("=================================================\n");
}

static void
snapshot_global_list_start(struct list_head *page_list, const char *str)
{
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s START\n", str);
	__snapshot_global_list(page_list);
}

static void
snapshot_global_list_end(struct list_head *page_list, const char *str)
{
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s END\n", str);
	__snapshot_global_list(page_list);
}

static void
snapshot_local_list_start(pol_t *pol, const char *str)
{
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s START\n", str);
	__snapshot_local_list(pol);
}

static void
snapshot_local_list_end(pol_t *pol, const char *str)
{
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s END\n", str);
	__snapshot_local_list(pol);
}

static void
snapshot_list_start(struct list_head *page_list, pol_t *pol, const char *str)
{
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s START\n", str);
	__snapshot_global_list(page_list);
	__snapshot_local_list(pol);
}

static void
snapshot_list_end(struct list_head *page_list, pol_t *pol, const char *str)
{
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s END\n", str);
	__snapshot_global_list(page_list);
	__snapshot_local_list(pol);
}

static void
snapshot_all_list_start(data_aLIFO_t *data, const char *str)
{
	mem_area_t *ma, *def_ma = data->def_ma;
	struct list_head *page_list = &data->page_list;
	struct list_head *ma_list = &data->ma_list;

	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s START\n", str);
	__snapshot_global_list(page_list);
	__snapshot_local_list(def_ma->pol);
	list_for_each_entry(ma, ma_list, entry)
		__snapshot_local_list(ma->pol);
}

static void
snapshot_all_list_end(data_aLIFO_t *data, const char *str)
{
	mem_area_t *ma, *def_ma = data->def_ma;
	struct list_head *page_list = &data->page_list;
	struct list_head *ma_list = &data->ma_list;

	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s END\n", str);
	__snapshot_global_list(page_list);
	__snapshot_local_list(def_ma->pol);
	list_for_each_entry(ma, ma_list, entry)
		__snapshot_local_list(ma->pol);
}

static inline void
update_win_stat(pol_t *pol, int winner)
{
	if (winner == LIFO)
		pol->stat->lifo_win++;
	else if (winner == CLOCK)
		pol->stat->clock_win++;
	else if (winner == DRAW)
		pol->stat->draw++;
	else
		assert(false);
}

static void
score(pol_t *pol, unsigned long stime, int winner)
{
	// LEGACY
	/*
	long double reward;

	assert(winner != DRAW);

	if (pol->last_stime < stime) {
		pol->lifo_score *= spow(pol->decay, stime - pol->last_stime);
		pol->last_stime = stime;
	}

	reward = spow(pol->decay, pol->last_stime - stime);

	if (winner == LIFO)
		pol->lifo_score += reward;
	else
		pol->lifo_score -= reward;
	*/
	double reward;
	double long temp;
	reward = spow(discount_rate, stime - pol->last_stime);
	reward *= -1;
	if (winner == LIFO)
		pol->clock_weight *= exp(learning_rate * reward);
	else
		pol->lifo_weight *= exp(learning_rate * reward);
	// sum is 1
	temp = pol->lifo_weight + pol->clock_weight;
	pol->lifo_weight = pol->lifo_weight / temp;
	pol->clock_weight = pol->clock_weight / temp;

	if (pol->lifo_weight >= 0.99)
	{
		pol->lifo_weight = 0.99;
		pol->clock_weight = 0.01;
	}
	else if(pol->clock_weight >= 0.99)
	{
		pol->lifo_weight = 0.01;
		pol->clock_weight = 0.99;
	}
		

	if (debug) {
		printf("[CHAL] winner: ");
		if (winner == CLOCK)
			printf("CLOCK\n");
		else
			printf("LIFO\n");
		printf("[CHAL] stime: %lu, last_stime: %lu\n", stime, pol->last_stime);
		printf("[CHAL] lifo_weight: %Lf (reward: %f)\n", pol->lifo_weight,
				reward);
	}
}

/* Remove a page or a ghost */
static void
remove_page(struct page *page)
{
	data_aLIFO_t *data = &data_aLIFO;
	pol_t *pol = page_pol(page);

	if (pol->reclaim_head == &page->entry)
		pol->reclaim_head = pol->reclaim_head->prev;
	if (data->reclaim_head == &page->gentry)
		data->reclaim_head = data->reclaim_head->next;

	if (page_pevicted(page)) {
		if (page_evict_clock(page))
			pol->nr_entry--;

		data->nr_ghost--;
	} else {
		data->nr_present--;
		pol->nr_present--;
		pol->nr_entry--;
	}

	list_del_init(&page->gentry);
	list_del_init(&page->entry);
	list_del_init(&page->centry);

	free_page_md(page);
	unmap_free_page(page);
}

static void
start_challenge(pol_t *pol, struct page *victim, int policy)
{
	assert(policy != DRAW);

	if (policy == CLOCK)
		set_page_evict_clock(victim);
	else
		set_page_evict_lifo(victim);

	set_page_starttime(victim, pol->time);
	set_page_challenging(victim);

	return;
}

static void
end_challenge(pol_t *pol, struct page *page, int winner)
{
	unsigned long stime = page_starttime(page);

	update_win_stat(pol, winner);

	if (winner != DRAW)
		score(pol, stime, winner);
}

/* page is re-accessed after eviction */
static void
eval_challenge(pol_t *pol, struct page *page)
{
	int policy;
/* LEGACY
	if (page_evict_lifo(page))
		policy = CLOCK;
	else
		policy = LIFO;
*/
	/* TODO : RL train */
	if (page_evict_lifo(page))
		policy = CLOCK;
	else
		policy = LIFO;

	end_challenge(pol, page, policy);

	if (page_vevicted(page))
		refresh_page_chal(page);
	else if (page_pevicted(page))
		remove_page(page);
}

static void
evict_victims(data_aLIFO_t *data, pol_t *pol, struct page *clock,
		struct page *lifo)
{
	if (debug)
		snapshot_list_start(&data->page_list, pol, __func__);

	if (clock == lifo) {
		if (clock) {
			assert(!page_pevicted(clock));
			if (page_vevicted(clock)) {
				if (page_evict_lifo(clock))
					end_challenge(pol, clock, LIFO);
				else
					end_challenge(pol, clock, CLOCK);

				remove_page(clock);

			} else {
				/* no-op; do not start a challenge */
				remove_page(clock);
			}
		}

	} else {
		if (pol_clock(pol)) {
			/* CLOCK victim */
			if (clock) {
				if (page_evicted(clock)) {
					assert(page_vevicted(clock));
					if (page_evict_clock(clock)) {
						pevict_page(clock, CLOCK);
					} else {
						end_challenge(pol, clock, LIFO);
						remove_page(clock);
					}
				} else {
					start_challenge(pol, clock, CLOCK);
					pevict_page(clock, CLOCK);
				}
			}

			/* LIFO victim */
			if (lifo) {
				if (page_evicted(lifo)) {
					assert(page_evict_clock(lifo));
					end_challenge(pol, lifo, CLOCK);
					if (page_vevicted(lifo))
						refresh_page_chal(lifo);
					else
						remove_page(lifo);
				} else {
					start_challenge(pol, lifo, LIFO);
					vevict_page(lifo, LIFO);
				}
			}

		} else {
			/* LIFO victim */
			if (lifo) {
				if (page_evicted(lifo)) {
					assert(page_vevicted(lifo));
					if (page_evict_lifo(lifo)) {
						pevict_page(lifo, LIFO);
					} else {
						end_challenge(pol, lifo, CLOCK);
						remove_page(lifo);
					}
				} else {
					start_challenge(pol, lifo, LIFO);
					pevict_page(lifo, LIFO);
				}
			}

			/* CLOCK victim */
			if (clock) {
				if (page_evicted(clock)) {
					assert(page_evict_lifo(clock));
					end_challenge(pol, clock, LIFO);
					if (page_vevicted(clock))
						refresh_page_chal(clock);
					else
						remove_page(clock);
				} else {
					start_challenge(pol, clock, CLOCK);
					vevict_page(clock, CLOCK);
				}
			}
		}
	}

	pol->time++;

	if (debug)
		snapshot_list_end(&data->page_list, pol, __func__);
}

static inline bool
__test_and_clear_page_young_local(struct page *page)
{
	bool ref = __page_young_local(page);
	__page_mkold_local(page);
	return ref;
}

static inline bool
__test_and_clear_page_young_global(struct page *page)
{
	bool ref = __page_young_global(page);
	__page_mkold_global(page);
	return ref;
}

static inline bool
test_and_clear_page_young_local(struct page *page)
{
	bool ref, lref;

	ref = test_and_clear_page_young(page);
	lref = __test_and_clear_page_young_local(page);

	if (ref) {
		__page_mkyoung_global(page);
		if (page_evicted(page)) {
			assert(page_vevicted(page));
			eval_challenge(page_pol(page), page);
		}

		refresh_page_chal(page);
	}

	return ref | lref;
}

static inline bool
test_and_clear_page_young_global(struct page *page)
{
	bool ref, gref;

	ref = test_and_clear_page_young(page);
	gref = __test_and_clear_page_young_global(page);

	if (ref) {
		__page_mkyoung_local(page);
		if (page_evicted(page)) {
			assert(page_vevicted(page));
			eval_challenge(page_pol(page), page);
		}

		refresh_page_chal(page);
	}

	return ref | gref;
}

static void
pol_stat_init(pol_stat_t **stat)
{
	*stat = malloc(sizeof(pol_stat_t));
	(*stat)->nr_present_acc = 0;
	(*stat)->nr_ref = 0;
	(*stat)->nr_clock = 0;
	(*stat)->nr_lifo = 0;
	(*stat)->clock_win = 0;
	(*stat)->lifo_win = 0;
	(*stat)->draw = 0;
}

static void
pol_init(pol_t **pol)
{
	*pol = malloc(sizeof(pol_t));
	(*pol)->nr_present = 0;
	(*pol)->nr_entry = 0;

	pol_stat_init(&(*pol)->stat);

	(*pol)->reclaim_head = &(*pol)->page_list;
	INIT_LIST_HEAD(&(*pol)->page_list);

	(*pol)->time = 0;
	(*pol)->last_stime = 0;
	(*pol)->decay = DECAY_FACTOR_DEFAULT;
	(*pol)->lifo_weight = INITIAL_WEIGHT; 
	(*pol)->clock_weight = INITIAL_WEIGHT; 
	(*pol)->lifo = false;
}

static void
mem_area_init(mem_area_t **ma, unsigned long req_start, unsigned long req_end,
		unsigned long start, unsigned long end)
{
	*ma = malloc(sizeof(mem_area_t));
	(*ma)->req_start = req_start;
	(*ma)->req_end = req_end;
	(*ma)->start = start;
	(*ma)->end = end;

	pol_init(&(*ma)->pol);

	(*ma)->obsolete = false;
}

static void
create_mem_area(data_aLIFO_t *data, unsigned long addr, unsigned long size)
{
	struct list_head *ma_list = &data->ma_list;
	struct list_head *next_entry = NULL;
	mem_area_t *ma, *new;
	unsigned long req_start, req_end;
	unsigned long start, end;
	mem_stat_t *stat = data->mem_stat;

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
	stat->nr_mem_area++;
	stat->ma_alloc_cnt++;
}

static void
free_mem_area(data_aLIFO_t *data, unsigned long addr)
{
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma, *victim = NULL;
	mem_stat_t *stat = data->mem_stat;

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
	list_del(&victim->entry);
	free(victim);

	stat->nr_mem_area--;
	stat->ma_free_cnt++;
}

static mem_area_t *
find_mem_area(data_aLIFO_t *data, unsigned long addr)
{
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;

	list_for_each_entry(ma, ma_list, entry) {
		if (ma->start <= addr && addr < ma->end)
			return ma;
	}

	return data->def_ma;
}

void init_aLIFO(policy_t *self, unsigned long memsz)
{
	data_aLIFO_t *data = self->data;
	unsigned long nr_pages = (memsz * 1024) >> PAGE_SHIFT;
	mem_area_t *def_ma;

	if (!nr_pages) {
		fprintf(stderr, "Memory size is too low!\n");
		exit(1);
	}

	mem_stat_t *memstat;
	memstat = malloc(sizeof(mem_stat_t));

	memstat->malloc_size_max = 0;
	memstat->malloc_size_acc = 0;
	memstat->malloc_cnt = 0;
	memstat->mfree_cnt = 0;
	memstat->ma_alloc_cnt = 0;
	memstat->ma_free_cnt = 0;
	memstat->nr_mem_area = 0;

	data->mem_stat = memstat;

	data->nr_pages = nr_pages;
	data->nr_present = 0;
	data->nr_ghost = 0;

	pt_init(&data->pt);

	INIT_LIST_HEAD(&data->page_list);
	INIT_LIST_HEAD(&data->ghost_list);
	data->reclaim_head = &data->page_list;

	mem_area_init(&def_ma, 0, 0, 0, 0);
	data->def_ma = def_ma;
	INIT_LIST_HEAD(&data->ma_list);

	spow_init(DECAY_FACTOR_DEFAULT);

	// RL init
	learning_rate = 0.30;
	discount_rate = spow(0.005, 1/memsz);
}

void fini_aLIFO(policy_t *self)
{
	/* Let page table freed automatically at program termination */
	if (!refault_stat)
		goto skip_refault;

	printf("refault dist (avg): %E\n", avg_refault_dist());

skip_refault:
	if (!policy_stat)
		goto skip;

	data_aLIFO_t *data = self->data;
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;
	pol_stat_t *stat;

	unsigned long ma_size_total = 0;
	unsigned long ma_size;
	unsigned long nr_ma = 0;
	unsigned long nr_lifo, nr_clock;
	double lifo_ratio;
	unsigned long lifo_win, clock_win, draw;

	list_for_each_entry(ma, ma_list, entry) {
		ma_size = ma->end - ma->start;
		ma_size_total += ma_size;
		nr_ma++;
	}

	if (!nr_ma)
		goto skip;

	printf("============== memory area stats ==============\n");
	printf("# memory areas: %lu\n", nr_ma);
	printf("memory area size (avg): %lu KiB\n",
			ma_size_total / nr_ma / 1024);

	printf("---------------- memory areas -----------------\n");

	nr_ma = 0;
	list_for_each_entry(ma, ma_list, entry) {
		stat = ma->pol->stat;

		nr_lifo = stat->nr_lifo;
		nr_clock = stat->nr_clock;
		lifo_ratio = (double) 100 * nr_lifo / (nr_clock + nr_lifo);

		lifo_win = stat->lifo_win;
		clock_win = stat->clock_win;
		draw = stat->draw;

		ma_size = ma->end - ma->start;

		printf("%4lu: [%#14lx - %#14lx (%lu KiB)]\n",
				nr_ma, ma->start, ma->end, ma_size / 1024);
		printf("\t- LIFO ratio: %.2lf (%lu/%lu)\n", lifo_ratio, nr_lifo,
				nr_lifo + nr_clock);
		printf("\t- chal winner (lifo / clock / draw) : (%lu / %lu / %lu)\n",
				lifo_win, clock_win, draw);
		nr_ma++;
	}

skip:
	return;
}
int malloc_aLIFO(policy_t *self, unsigned long addr, unsigned long size)
{
	data_aLIFO_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	if (stat->malloc_size_max < size)
		stat->malloc_size_max = size;

	stat->malloc_size_acc += size;
	stat->malloc_cnt++;

	create_mem_area(data, addr, size);

	policy_count_stat(self, NR_MEM_ALLOC, 1);
	return 0;
}

int mfree_aLIFO(policy_t *self, unsigned long addr)
{
	data_aLIFO_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	stat->mfree_cnt++;

	free_mem_area(data, addr);

	policy_count_stat(self, NR_MEM_FREE, 1);
	return 0;
}

void reset_pol_head_lifo(pol_t *pol)
{
	struct page *page;

	pol->reclaim_head = pol->page_list.prev->prev;

	if (pol->reclaim_head == &pol->page_list)
		return;

	page = list_entry(pol->reclaim_head, struct page, entry);
	page_mkold_local(page);
}

void add_page_aLIFO(struct list_head *global_list, pol_t *pol,
		struct page *page)
{
	alloc_page_md(page);
	set_page_pol(page, pol);
	pol->nr_present++;
	pol->nr_entry++;

	list_add_tail(&page->entry, &pol->page_list);
	list_add_tail(&page->gentry, global_list);

	reset_pol_head_lifo(pol);
}

static struct page *
global_victim_clock(data_aLIFO_t *data, struct list_head *page_list)
{
	struct page *page, *victim = NULL;
	pol_t *pol;

	if (debug)
		snapshot_global_list_start(page_list, __func__);

	while (!victim) {
		while (data->reclaim_head == page_list)
			data->reclaim_head = data->reclaim_head->next;

		page = list_entry(data->reclaim_head, struct page, gentry);
		data->reclaim_head = data->reclaim_head->next;
		pol = page_pol(page);

		if (test_and_clear_page_young_global(page))
			continue;

		/* not referenced */
		if (pol_clock(pol)) {
			if (page_evicted(page)) {
				if (page_evict_clock(page))
					assert(page_vevicted(page));
				else {
					if (page_pevicted(page)) {
						end_challenge(pol, page, LIFO);
						remove_page(page);
						continue;
					}
				}
			}
		} else {
			if (pol_empty(pol)) {
				/* skip if page's pol is empty */
				assert(page_pevicted(page));
				end_challenge(pol, page, LIFO);
				remove_page(page);
				continue;
			}

			if (page_evicted(page)) {
				if (page_evict_clock(page)) {
					assert(page_vevicted(page));
					continue;
				}
			}
		}

		victim = page;
	}

	/* move page_list->next ~ victim to the tail */
	list_bulk_move_tail(page_list, page_list->next, &victim->gentry);

	if (debug)
		snapshot_global_list_end(page_list, __func__);

	return victim;
}

static struct page *
pol_victim_lifo(pol_t *pol)
{
	struct page *page, *victim = NULL;
	unsigned long nr_scanned = 0;
	unsigned long nr_scan_max = 2 * nr_pol_entry(pol);

	if (debug)
		snapshot_local_list_start(pol, __func__);

	assert(!list_empty(&pol->page_list));
	assert(!(pol_empty(pol) && pol_lifo(pol)));

	while (!victim && nr_scanned < nr_scan_max) {
		while (pol->reclaim_head == &pol->page_list)
			pol->reclaim_head = pol->reclaim_head->prev;

		page = list_entry(pol->reclaim_head, struct page, entry);
		pol->reclaim_head = pol->reclaim_head->prev;
		nr_scanned++;

		if (test_and_clear_page_young_local(page))
			continue;

		if (pol_lifo(pol)) {
			if (page_evicted(page)) {
				if (page_evict_lifo(page))
					assert(page_vevicted(page));
				else {
					if (page_pevicted(page)) {
						end_challenge(pol, page, CLOCK);
						remove_page(page);
						continue;
					}
				}
			}
		} else {
			if (page_evicted(page)) {
				if (page_evict_lifo(page)) {
					assert(page_vevicted(page));
					continue;
				}
			}
		}

		victim = page;
	}

	if (debug)
		snapshot_local_list_end(pol, __func__);

	return victim;
}


static inline void
get_lifo_score(pol_t* pol)
{
	double long num;
	srand(time(0));
	num = rand() / (double long)RAND_MAX;
	//TODO: optimize it
	//if (pol->clock_weight > pol->lifo_weight && pol->clock_weight > num)
	if (pol->clock_weight > pol->lifo_weight && pol-> clock_weight > num)
		// clock 
		pol->lifo = false;
	else if (pol->clock_weight < pol->lifo_weight && pol->lifo_weight < num)
		// lifo
		pol->lifo = true;
}

static inline void
update_pol_policy(pol_t *pol)
{
	/* LEGACY 
	if (pol->lifo_score > 0)
		pol->lifo = true;
	else if (pol->lifo_score < 0)
		pol->lifo = false;
	*/
	// TODO: RL Inference
	get_lifo_score(pol);
}

static void
update_pol_policies(data_aLIFO_t *data)
{
	mem_area_t *def_ma = data->def_ma;
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;

	update_pol_policy(def_ma->pol);
	list_for_each_entry(ma, ma_list, entry)
		update_pol_policy(ma->pol);
}

static inline void
update_pol_stat(pol_t *pol)
{
	if (pol_lifo(pol))
		pol->stat->nr_lifo++;
	else
		pol->stat->nr_clock++;
}

void evict_page_aLIFO(policy_t *self)
{
	data_aLIFO_t *data = (data_aLIFO_t *)self->data;
	struct list_head *page_list = &data->page_list;
	pol_t *pol;
	struct page *lifo_victim, *clock_victim;

	update_pol_policies(data);

	clock_victim = global_victim_clock(data, page_list);
	pol = page_pol(clock_victim);
	lifo_victim = pol_victim_lifo(pol);

	if (pol_lifo(pol))
		reg_evict(lifo_victim->addr);
	else
		reg_evict(clock_victim->addr);

	evict_victims(data, pol, clock_victim, lifo_victim);

	update_pol_stat(pol);
}

static inline bool is_full_aLIFO(policy_t *self)
{
	data_aLIFO_t *data = (data_aLIFO_t *)self->data;

	if (data->nr_pages <= data->nr_present)
		return true;

	return false;
}

static void
page_fault(policy_t *self, unsigned long addr, struct page *page)
{
	data_aLIFO_t *data = (data_aLIFO_t *)self->data;
	struct list_head *page_list = &data->page_list;
	pt_t *pt = data->pt;
	mem_area_t *ma = find_mem_area(data, addr);
	pol_t *pol = ma->pol;

	if (is_full_aLIFO(self))
		evict_page_aLIFO(self);

	page = map_alloc_page(pt, addr);
	add_page_aLIFO(page_list, pol, page);
}

static void
page_fault_ghost(policy_t *self, unsigned long addr, struct page *page)
{
	/* The page is removed in eval_challenge() */
	eval_challenge(page_pol(page), page);

	page_fault(self, addr, page);
}

static inline bool
ghost_overfull(data_aLIFO_t *data)
{
	/* TODO: threshold; 1x? 2x? 3x? */
	return data->nr_ghost > data->nr_pages;
}

static void
check_ghost_buffer(data_aLIFO_t *data)
{
	struct page *ghost;

	if (!ghost_overfull(data))
		return;

	if (debug)
		snapshot_all_list_start(data, __func__);

	while (ghost_overfull(data)) {
		assert(!list_empty(&data->ghost_list));
		ghost = list_last_entry(&data->ghost_list, struct page, centry);
		/* TODO: draw? win? */
		end_challenge(page_pol(ghost), ghost, DRAW);
		remove_page(ghost);
	}

	if (debug)
		snapshot_all_list_end(data, __func__);
}

static void
page_fault_aLIFO(policy_t *self, unsigned long addr, struct page *page)
{
	data_aLIFO_t *data = (data_aLIFO_t *)self->data;

	if (debug)
		printf("MISS\n");

	reg_fault(addr);

	if (page) {
		if (page_present(page)) {
			/* wrong path */
			fprintf(stderr, "page fault but present?!\n");
			exit(1);
		}

		page_fault_ghost(self, addr, page);

	} else {
		page_fault(self, addr, page);
	}

	/* check ghost page buffer overflow */
	check_ghost_buffer(data);

	data->nr_present++;
	assert(data->nr_present <= data->nr_pages);
	if (self->cold_state && data->nr_present == data->nr_pages)
		self->cold_state = false;

	policy_count_stat(self, NR_MISS, 1);
}

static void
page_hit_aLIFO(policy_t *self, struct page *page)
{
	if (debug)
		printf("HIT\n");

	page_mkyoung(page);
	policy_count_stat(self, NR_HIT, 1);
}

int access_aLIFO(policy_t *self, unsigned long vpn)
{
	data_aLIFO_t *data = (data_aLIFO_t *)self->data;
	pt_t *pt = data->pt;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;

	cnt_access(1);

	page = pt_walk(pt, addr);

	if (!page)
		page_fault_aLIFO(self, addr, page);
	else if (!page_present(page))
		page_fault_aLIFO(self, addr, page);
	else
		page_hit_aLIFO(self, page);

	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
