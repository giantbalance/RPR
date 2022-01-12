/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "clock-pro.h"
#include "../lib/refault.h"

void init_CLOCK_Pro(policy_t *self, unsigned long memsz);
void fini_CLOCK_Pro(policy_t *self);
int malloc_CLOCK_Pro(policy_t *self, unsigned long addr, unsigned long size);
int mfree_CLOCK_Pro(policy_t *self, unsigned long addr);
int access_CLOCK_Pro(policy_t *self, unsigned long addr);

data_CLOCK_Pro_t data_CLOCK_Pro;
policy_t policy_CLOCK_Pro = {
	.name = "clock-pro",
	.init = init_CLOCK_Pro,
	.fini = fini_CLOCK_Pro,
	.access = access_CLOCK_Pro,
	.mem_alloc = malloc_CLOCK_Pro,
	.mem_free = mfree_CLOCK_Pro,
	.data = &data_CLOCK_Pro,
};

typedef struct {
	bool hot;
	bool resident;
	bool testing;
	mem_area_t *ma;
} page_md_t;

#define page_md(_page)				((page_md_t *)(_page)->private)
#define page_hot(_page)				(page_md(_page)->hot)
#define page_resident(_page)		(page_md(_page)->resident)
#define page_testing(_page)			(page_md(_page)->testing)

#define page_mkhot(_page)			{ page_hot(_page) = true; }
#define page_mkcold(_page)			{ page_hot(_page) = false; }
#define page_mkresident(_page)		{ page_resident(_page) = true; }
#define page_mkghost(_page)			{ page_resident(_page) = false; }
#define page_test_start(_page)		{ page_testing(_page) = true; }
#define page_test_end(_page)		{ page_testing(_page) = false; }

#define page_mem_area(_page)			(page_md(_page)->ma)

#define page_set_mem_area(_page, _ma)	{ page_mem_area(_page) = _ma; }

static void print_clock_stats(clock_pro_t *clock)
{
	unsigned long nr_hot, nr_cold, nr_ghost;
	unsigned long nr_hot_max, nr_cold_max, nr_ghost_max;

	nr_hot = clock->nr_hot;
	nr_cold = clock->nr_cold;
	nr_ghost = clock->nr_ghost;
	nr_hot_max = clock->nr_pages - clock->nr_cold_max;
	nr_cold_max = clock->nr_cold_max;
	nr_ghost_max = clock->nr_ghost_max;

	printf("      hot      |     cold      |     ghost    \n");
	printf(" %5lu / %-5lu | %5lu / %-5lu | %5lu / %-5lu \n",
			nr_hot, nr_hot_max, nr_cold, nr_cold_max, nr_ghost, nr_ghost_max);
	printf("----------------------------------------------\n");
}

static void print_list_snapshot(const char *str, clock_pro_t *clock)
{
	struct page *page;
	unsigned long addr;
	char *status, *ref, *test;
	char *hand_hot, *hand_cold, *hand_test;

	printf("============================================== %s\n", str);
	list_for_each_entry(page, &clock->page_list, entry) {
		addr = (unsigned long) page;
		if (page_hot(page))
			status = "H";
		else if (page_resident(page))
			status = "C";
		else
			status = "NC";

		if (page_young(page))
			ref = "R";
		else
			ref = "";

		if (page_testing(page))
			test = "T";
		else
			test = "";

		if (&page->entry == clock->hand_hot)
			hand_hot = "HAND(hot)";
		else
			hand_hot = "";

		if (&page->entry == clock->hand_cold)
			hand_cold = "HAND(cold)";
		else
			hand_cold = "";

		if (&page->entry == clock->hand_test)
			hand_test = "HAND(test)";
		else
			hand_test = "";

		printf("%3lx%3s%2s%2s%11s%11s%11s\n", addr % 0x1000, status, ref, test,
				hand_hot, hand_cold, hand_test);
	}
	printf("==============================================\n");
	print_clock_stats(clock);
}

static inline void alloc_page_md(struct page *page)
{
	page->private = malloc(sizeof(page_md_t));
}

static inline void free_page_md(struct page *page)
{
	free(page->private);
}

static void run_hand_test(clock_pro_t *clock);
static void run_hand_hot(clock_pro_t *clock);
static void run_hand_cold(clock_pro_t *clock);
static void add_hot_page(clock_pro_t *clock, struct page *page);
static void add_cold_page(clock_pro_t *clock, struct page *page);
static void __promote_page(clock_pro_t *clock, struct page *page);
static void promote_page(clock_pro_t *clock, struct page *page);
static void demote_page(clock_pro_t *clock, struct page *page);
static void refresh_cold_page(clock_pro_t *clock, struct page *page);

static void
mem_area_init(mem_area_t **ma, unsigned long req_start, unsigned long req_end,
		unsigned long start, unsigned long end)
{
	ma_stat_t *stat;

	*ma = malloc(sizeof(mem_area_t));
	(*ma)->req_start = req_start;
	(*ma)->req_end = req_end;
	(*ma)->start = start;
	(*ma)->end = end;

	(*ma)->nr_hot = 0;
	(*ma)->nr_cold = 0;
	(*ma)->nr_ghost = 0;

	stat = malloc(sizeof(ma_stat_t));
	stat->nr_present_acc = 0;
	stat->nr_hot_acc = 0;
	stat->nr_cold_acc = 0;
	stat->nr_ghost_acc = 0;
	stat->nr_cold_max_acc = 0;
	stat->nr_ref = 0;

	stat->nr_hand_hot_move = 0;
	stat->nr_hand_cold_move = 0;
	stat->nr_hand_test_move = 0;
	stat->nr_promote = 0;
	stat->nr_demote = 0;
	(*ma)->stat = stat;
	(*ma)->obsolete = false;
}

void init_CLOCK_Pro(policy_t *self, unsigned long memsz)
{
	data_CLOCK_Pro_t *data = self->data;
	clock_pro_t *clock;
	clock_pro_stat_t *stat;
	mem_area_t *def_ma;
	unsigned long nr_pages = (memsz * 1024) >> PAGE_SHIFT;

	if (debug)
		printf("INIT\n");

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

	if (!nr_pages) {
		fprintf(stderr, "Memory size is too low!\n");
		exit(1);
	}

	clock = malloc(sizeof(clock_pro_t));
	stat = malloc(sizeof(clock_pro_stat_t));

	clock->nr_pages = nr_pages;
	clock->nr_hot = 0;
	clock->nr_cold = 0;
	clock->nr_ghost = 0;
	clock->nr_cold_max = nr_pages > 100? nr_pages / 100 : 1;
	clock->nr_ghost_max = nr_pages;

	INIT_LIST_HEAD(&clock->page_list);
	INIT_LIST_HEAD(&clock->cold_list);

	mem_area_init(&def_ma, 0, 0, 0, 0);
	data->def_ma = def_ma;
	INIT_LIST_HEAD(&data->ma_list);

	clock->hand_hot = &clock->page_list;
	clock->hand_cold = &clock->page_list;
	clock->hand_test = &clock->page_list;

	stat->nr_present_acc = 0;
	stat->nr_hot_acc = 0;
	stat->nr_cold_acc = 0;
	stat->nr_ghost_acc = 0;
	stat->nr_cold_max_acc = 0;
	stat->nr_ref = 0;

	data->clock = clock;
	data->stat = stat;
	pt_init(&data->pt);
}

void fini_CLOCK_Pro(policy_t *self)
{
	if (!refault_stat)
		goto skip_refault;

	printf("refault dist (avg): %E\n", avg_refault_dist());

skip_refault:
	if (!policy_stat)
		goto skip;

	data_CLOCK_Pro_t *data = (data_CLOCK_Pro_t *)self->data;
	clock_pro_stat_t *stat = data->stat;
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;
	ma_stat_t *mstat;

	double nr_present_avg = (double) stat->nr_present_acc / stat->nr_ref;
	double nr_hot_avg = (double) stat->nr_hot_acc / stat->nr_ref;
	double nr_cold_avg = (double) stat->nr_cold_acc / stat->nr_ref;
	double nr_ghost_avg = (double) stat->nr_ghost_acc / stat->nr_ref;
	double nr_cold_max_avg = (double) stat->nr_cold_max_acc / stat->nr_ref;

	printf("==== average statistics ====\n");
	printf(" nr_present: %20lf\n", nr_present_avg);
	printf("     nr_hot: %20lf\n", nr_hot_avg);
	printf("    nr_cold: %20lf\n", nr_cold_avg);
	printf("   nr_ghost: %20lf\n", nr_ghost_avg);
	printf("nr_cold_max: %20lf\n", nr_cold_max_avg);

	mstat = data->def_ma->stat;
	nr_present_avg = (double) mstat->nr_present_acc / mstat->nr_ref;
	nr_hot_avg = (double) mstat->nr_hot_acc / mstat->nr_ref;
	nr_cold_avg = (double) mstat->nr_cold_acc / mstat->nr_ref;
	nr_ghost_avg = (double) mstat->nr_ghost_acc / mstat->nr_ref;

	printf("[default]\n");
	printf("--------------- page stats ----------------\n");
	printf("       nr_present: %20lf\n", nr_present_avg);
	printf("           nr_hot: %20lf\n", nr_hot_avg);
	printf("          nr_cold: %20lf\n", nr_cold_avg);
	printf("         nr_ghost: %20lf\n", nr_ghost_avg);
	printf("\n");

	list_for_each_entry(ma, ma_list, entry) {
		mstat = ma->stat;
		nr_present_avg = (double) mstat->nr_present_acc / mstat->nr_ref;
		nr_hot_avg = (double) mstat->nr_hot_acc / mstat->nr_ref;
		nr_cold_avg = (double) mstat->nr_cold_acc / mstat->nr_ref;
		nr_ghost_avg = (double) mstat->nr_ghost_acc / mstat->nr_ref;

		printf("[%#14lx - %#14lx (%lu)]\n", ma->start, ma->end,
				(ma->end - ma->start));
		printf("--------------- page stats ----------------\n");
		printf("       nr_present: %20lf\n", nr_present_avg);
		printf("           nr_hot: %20lf\n", nr_hot_avg);
		printf("          nr_cold: %20lf\n", nr_cold_avg);
		printf("         nr_ghost: %20lf\n", nr_ghost_avg);
		printf("\n");
	}

skip:
	/* Let page table freed automatically at program termination */
	return;
}

static void
create_mem_area(data_CLOCK_Pro_t *data, unsigned long addr, unsigned long size)
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
free_mem_area(data_CLOCK_Pro_t *data, unsigned long addr)
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

int malloc_CLOCK_Pro(policy_t *self, unsigned long addr, unsigned long size)
{
	data_CLOCK_Pro_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	if (stat->malloc_size_max < size)
		stat->malloc_size_max = size;

	stat->malloc_size_acc += size;
	stat->malloc_cnt++;

	create_mem_area(data, addr, size);

	policy_count_stat(self, NR_MEM_ALLOC, 1);
	return 0;
}

int mfree_CLOCK_Pro(policy_t *self, unsigned long addr)
{
	data_CLOCK_Pro_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	stat->mfree_cnt++;

	free_mem_area(data, addr);

	policy_count_stat(self, NR_MEM_FREE, 1);
	return 0;
}

#define TEST_HAND_SANITY(_hand, _head)			{	\
	if ((_hand) == (NULL) || (_hand) == (_head)) {	\
		fprintf(stderr, "Invalid hand position\n");	\
		exit(1);									\
	}												\
}

static inline bool
is_buffer_full(clock_pro_t *clock)
{
	return clock->nr_hot + clock->nr_cold >= clock->nr_pages;
}

static inline bool
is_hot_empty(clock_pro_t *clock)
{
	return clock->nr_hot == 0;
}

static inline bool
is_ghost_overfull(clock_pro_t *clock)
{
	return clock->nr_ghost > clock->nr_ghost_max;
}

static inline bool
is_hot_overfull(clock_pro_t *clock)
{
	return clock->nr_hot > (clock->nr_pages - clock->nr_cold_max);
}

static inline bool
is_hot_full(clock_pro_t *clock)
{
	return clock->nr_hot >= (clock->nr_pages - clock->nr_cold_max);
}

static inline bool
is_cold_overfull(clock_pro_t *clock)
{
	return clock->nr_cold > clock->nr_cold_max;
}

static inline bool
is_cold_full(clock_pro_t *clock)
{
	return clock->nr_cold >= clock->nr_cold_max;
}

static inline bool
in_init(clock_pro_t *clock)
{
	return clock->nr_cold == 0 && !is_hot_full(clock);
}

static bool
need_run_hand_test(clock_pro_t *clock)
{
	struct page *page;

	if (clock->nr_ghost == 0 && clock->nr_cold == 0) {
		return false;
	}

	if (clock->hand_test == &clock->page_list)
		return true;

	page = list_entry(clock->hand_test, struct page, entry);
	if (!page_hot(page) && !page_resident(page))
		return false;

	return true;
}

static mem_area_t *
find_mem_area(data_CLOCK_Pro_t *data, unsigned long addr)
{
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;

	list_for_each_entry(ma, ma_list, entry) {
		if (ma->start <= addr && addr < ma->end)
			return ma;
	}

	return data->def_ma;
}

static bool
need_run_hand_cold(clock_pro_t *clock)
{
	struct page *page;

	if (clock->nr_cold == 0) {
		return false;
	}

	if (clock->hand_cold == &clock->page_list)
		return true;

	page = list_entry(clock->hand_cold, struct page, entry);
	if (!page_hot(page) && page_resident(page) && !page_young(page))
		return false;

	return true;
}

static bool
need_run_hand_hot(clock_pro_t *clock)
{
	struct page *page;

	if (clock->nr_hot == 0) {
		return false;
	}

	if (clock->hand_hot == &clock->page_list)
		return true;

	page = list_entry(clock->hand_hot, struct page, entry);
	if (page_hot(page) && !page_young(page))
		return false;

	return true;
}

static inline void move_hand(struct list_head **hand, struct list_head *head)
{
	do {
		*hand = (*hand)->next;
	} while (*hand == head);
}

static inline void move_hand_hot(clock_pro_t *clock)
{
	if (clock->nr_hot + clock->nr_cold + clock->nr_ghost == 0) {
		clock->hand_hot = &clock->page_list;
		return;
	}
	move_hand(&clock->hand_hot, &clock->page_list);
}

static inline void move_hand_cold(clock_pro_t *clock)
{
	if (clock->nr_hot + clock->nr_cold + clock->nr_ghost == 0) {
		clock->hand_cold = &clock->page_list;
		return;
	}
	move_hand(&clock->hand_cold, &clock->page_list);
}

static inline void move_hand_test(clock_pro_t *clock)
{
	if (clock->nr_hot + clock->nr_cold + clock->nr_ghost == 0) {
		clock->hand_test = &clock->page_list;
		return;
	}
	move_hand(&clock->hand_test, &clock->page_list);
}

void isolate_page(clock_pro_t *clock, struct page *page)
{
	if (!page) {
		/* wrong path */
		fprintf(stderr, "isolating NULL page?!\n");
		exit(1);
	}

	if (page_hot(page)) {
		clock->nr_hot--;
		page_mem_area(page)->nr_hot--;
	} else if (page_resident(page)) {
		clock->nr_cold--;
		page_mem_area(page)->nr_cold--;
	} else {
		clock->nr_ghost--;
		page_mem_area(page)->nr_ghost--;
	}

	if (clock->hand_hot == &page->entry) {
		move_hand_hot(clock);
	}
	if (clock->hand_cold == &page->entry) {
		move_hand_cold(clock);
	}
	if (clock->hand_test == &page->entry) {
		move_hand_test(clock);
	}

	list_del_init(&page->entry);
	if (!page_hot(page) && page_resident(page))
		list_del_init(&page->centry);
}

static inline void
remove_page(clock_pro_t *clock, struct page *page)
{
	isolate_page(clock, page);
	free_page_md(page);
	unmap_free_page(page);
}

static inline void
inc_cold_size(clock_pro_t *clock)
{
	if (debug)
		printf("[PARAM UPDATE] nr_cold_max: %lu -> ", clock->nr_cold_max);

	if (clock->nr_cold_max >= clock->nr_pages)
		clock->nr_cold_max = clock->nr_pages;
	else
		clock->nr_cold_max++;

	if (debug)
		printf("%lu\n", clock->nr_cold_max);
}

static void
dec_cold_size(clock_pro_t *clock)
{
	if (debug)
		printf("[PARAM UPDATE] nr_cold_max: %lu -> ", clock->nr_cold_max);

	if (clock->nr_cold_max <= 1)
		clock->nr_cold_max = 1;
	else
		clock->nr_cold_max--;

	if (debug)
		printf("%lu\n", clock->nr_cold_max);
}

static void
run_hand_test(clock_pro_t *clock)
{
	struct page *page;
	int ref;

	if (clock->nr_ghost == 0 && clock->nr_cold == 0) {
		fprintf(stderr, "shrinking cold buffer below zero?!\n");
		exit(1);
	}
	if (clock->hand_test == &clock->page_list)
		clock->hand_test = clock->hand_test->next;

	if (debug)
		print_list_snapshot(__func__, clock);

	page = list_entry(clock->hand_test, struct page, entry);
	move_hand_test(clock);

	if (page_hot(page) || !page_testing(page))
		return;

	ref = test_and_clear_page_young(page);
	if (ref) {
		/* move to the list tail */
		if (page_testing(page)) {
			/* parameter tuning; increase nr_cold_max by 1 */
			inc_cold_size(clock);
			/* TODO: do not promote; only parameter tuning */
			__promote_page(clock, page);

		} else {
			refresh_cold_page(clock, page);
		}
	} else {
		if (page_testing(page)) {
			/* parameter tuning; decrease nr_cold_max by 1 */
			dec_cold_size(clock);
		}

		/* terminate its test period */
		page_test_end(page);
		if (!page_resident(page))
			remove_page(clock, page);
	}
}

static void
run_hand_cold(clock_pro_t *clock)
{
	struct page *page;
	int ref;

	if (clock->nr_cold == 0) {
		fprintf(stderr, "shrinking cold buffer below zero?!\n");
		exit(1);
	}
	if (clock->hand_cold == &clock->page_list)
		clock->hand_cold = clock->hand_cold->next;

	if (debug)
		print_list_snapshot(__func__, clock);

	page = list_entry(clock->cold_list.next, struct page, centry);
	list_rotate_left(&clock->cold_list);
	clock->hand_cold = page->entry.next;

	if (page_hot(page) || !page_resident(page))
		return;

	ref = test_and_clear_page_young(page);
	if (ref) {
		/* move to the list tail */
		if (page_testing(page)) {
			/* parameter tuning: increase nr_cold_max by 1 */
			inc_cold_size(clock);
			promote_page(clock, page);
		} else {
			refresh_cold_page(clock, page);
		}
	} else {
		/* target position */
		clock->hand_cold = &page->entry;
		list_move(&page->centry, &clock->cold_list);
	}
}

static void
run_hand_hot(clock_pro_t *clock)
{
	struct page *page;
	int ref;

	if (clock->nr_hot == 0) {
		fprintf(stderr, "shrinking hot buffer below zero?!\n");
		exit(1);
	}
	if (clock->hand_hot == &clock->page_list)
		clock->hand_hot = clock->hand_hot->next;

	if (debug)
		print_list_snapshot(__func__, clock);

	page = list_entry(clock->hand_hot, struct page, entry);
	move_hand_hot(clock);

	ref = test_and_clear_page_young(page);
	if (page_hot(page)) {
		if (!ref) {
			/* demote to cold */
			demote_page(clock, page);
		}
	} else {
		if (ref) {
			/* move to the list tail */
			if (page_testing(page)) {
				/* parameter tuning; increase nr_cold_max by 1 */
				inc_cold_size(clock);
				/* TODO: do not promote; only parameter tuning */
				__promote_page(clock, page);

			} else {
				refresh_cold_page(clock, page);
			}
		} else {
			if (page_testing(page)) {
				/* parameter tuning; decrease nr_cold_max by 1 */
				dec_cold_size(clock);
			}

			/* terminate its test period */
			page_test_end(page);
			if (!page_resident(page))
				remove_page(clock, page);
		}
	}
}

static inline void
__add_cold_page(clock_pro_t *clock, struct page *page)
{
	list_add_tail(&page->entry, clock->hand_hot);
	list_add_tail(&page->centry, &clock->cold_list);

	if (debug)
		print_list_snapshot(__func__, clock);
}

static inline void
__add_hot_page(clock_pro_t *clock, struct page *page)
{
	list_add_tail(&page->entry, clock->hand_hot);

	if (debug)
		print_list_snapshot(__func__, clock);
}

void refresh_cold_page(clock_pro_t *clock, struct page *page)
{
	if (!page_resident(page)) {
		fprintf(stderr, "refreshing non-resident cold page?!\n");
		exit(1);
	}
	isolate_page(clock, page);

	page_mkresident(page);
	page_mkcold(page);
	page_test_start(page);
	clock->nr_cold++;
	page_mem_area(page)->nr_cold++;
	__add_cold_page(clock, page);
}

void evict_cold_page(clock_pro_t *clock)
{
	struct page *page;

	page = list_entry(clock->hand_cold, struct page, entry);
	move_hand_cold(clock);

	reg_evict(page->addr);

	/* replace the page */
	if (page_testing(page)) {
		/* update status to non-resident */
		page_mkghost(page);
		list_del_init(&page->centry);
		clock->nr_ghost++;
		page_mem_area(page)->nr_ghost++;
		clock->nr_cold--;
		page_mem_area(page)->nr_cold--;
	} else {
		/* revove from clock */
		remove_page(clock, page);
	}
}

void add_cold_page(clock_pro_t *clock, struct page *page)
{
	/* 1. evict 1 cold page */
	/* 2. if promotion is held during the eviction, demote some for balance */
	while (is_buffer_full(clock)) {
		do {
			run_hand_cold(clock);
		} while (need_run_hand_cold(clock));
		evict_cold_page(clock);
	}

	/* 3. add the page as cold */
	page_mkresident(page);
	page_mkcold(page);
	page_test_start(page);
	clock->nr_cold++;
	page_mem_area(page)->nr_cold++;
	__add_cold_page(clock, page);
}

void add_hot_page(clock_pro_t *clock, struct page *page)
{
	add_cold_page(clock, page);
	promote_page(clock, page);
}

static void
__promote_page(clock_pro_t *clock, struct page *page)
{
	isolate_page(clock, page);

	page_mkresident(page);
	page_mkhot(page);
	page_test_end(page);
	clock->nr_hot++;
	page_mem_area(page)->nr_hot++;
	__add_hot_page(clock, page);
}

static void
promote_page(clock_pro_t *clock, struct page *page)
{
	__promote_page(clock, page);

	while (is_hot_overfull(clock)) {
		do {
			run_hand_hot(clock);
		} while (need_run_hand_hot(clock));
	}
}

static void
demote_page(clock_pro_t *clock, struct page *page)
{
	isolate_page(clock, page);

	page_mkresident(page);
	page_mkcold(page);
	page_test_end(page);
	clock->nr_cold++;
	page_mem_area(page)->nr_cold++;
	__add_cold_page(clock, page);
}

static void
prune_ghost_pages(clock_pro_t *clock)
{
	while (is_ghost_overfull(clock)) {
		do {
			run_hand_test(clock);
		} while (need_run_hand_test(clock));
	}
}

void add_page_CLOCK_Pro(policy_t *self, unsigned long addr, struct page *page)
{
	data_CLOCK_Pro_t *data = (data_CLOCK_Pro_t *)self->data;
	clock_pro_t *clock = data->clock;
	pt_t *pt = data->pt;
	mem_area_t *ma, *curr;

	unsigned long nr_present = clock->nr_hot + clock->nr_cold;

	ma = find_mem_area(data, addr);

	/* Two cases
	 * 1. Page is not in the list: add as a cold page
	 * 2. Page is in the list (as ghost page): add as a hot page
	 */
	if (!page) {
		/* Case 1 */
		page = map_alloc_page(pt, addr);
		alloc_page_md(page);
		page_set_mem_area(page, ma);
		if (in_init(clock))
			add_hot_page(clock, page);
		else
			add_cold_page(clock, page);

	} else {
		/* Case 2 */
		if (page_resident(page)) {
			/* wrong path */
			fprintf(stderr, "adding already present page?!\n");
			exit(1);
		}

		/*
		 * If hot is full, first check if the cold is full
		 * If so, reserve cold space first
		 */
		isolate_page(clock, page);

		curr = page_mem_area(page);
		if (curr != ma)
			page_set_mem_area(page, ma);

		inc_cold_size(clock);
		add_hot_page(clock, page);
	}

	prune_ghost_pages(clock);

	if (clock->nr_hot + clock->nr_cold < nr_present) {
		printf("buffer shrink?! %lu -> %lu", nr_present,
				clock->nr_hot + clock->nr_cold);
	}

	if (clock->nr_hot + clock->nr_cold > clock->nr_pages) {
		fprintf(stderr, "buffer overflow!\n");
		exit(1);
	}
}

static void
update_clock_stat(policy_t *self)
{
	data_CLOCK_Pro_t *data = (data_CLOCK_Pro_t *)self->data;
	clock_pro_t *clock = data->clock;
	clock_pro_stat_t *stat = data->stat;
	mem_area_t *ma;
	ma_stat_t *mstat;

	if (in_init(clock))
		return;

	if (self->cold_state)
		self->cold_state = false;

	stat->nr_present_acc += clock->nr_hot + clock->nr_cold;
	stat->nr_hot_acc += clock->nr_hot;
	stat->nr_cold_acc += clock->nr_cold;
	stat->nr_ghost_acc += clock->nr_ghost;
	stat->nr_cold_max_acc += clock->nr_cold_max;
	stat->nr_ref++;

	/* default memory area */
	ma = data->def_ma;
	mstat = ma->stat;

	mstat->nr_present_acc += ma->nr_hot + ma->nr_cold;
	mstat->nr_hot_acc += ma->nr_hot;
	mstat->nr_cold_acc += ma->nr_cold;
	mstat->nr_ghost_acc += ma->nr_ghost;

	mstat->nr_ref++;

	/* each memory area */
	list_for_each_entry(ma, &data->ma_list, entry) {
		mstat = ma->stat;

		mstat->nr_present_acc += ma->nr_hot + ma->nr_cold;
		mstat->nr_hot_acc += ma->nr_hot;
		mstat->nr_cold_acc += ma->nr_cold;
		mstat->nr_ghost_acc += ma->nr_ghost;

		mstat->nr_ref++;
	}
}

static void
page_fault_CLOCK_Pro(policy_t *self, unsigned long addr, struct page *page)
{
	if (debug) {
		printf("MISS\n");
		print_list_snapshot(__func__, ((data_CLOCK_Pro_t *)self->data)->clock);
	}
	reg_fault(addr);
	add_page_CLOCK_Pro(self, addr, page);
	policy_count_stat(self, NR_MISS, 1);
	update_clock_stat(self);
}

static void
page_hit_CLOCK_Pro(policy_t *self, struct page *page)
{
	if (debug)
		printf("HIT\n");

	page_mkyoung(page);
	policy_count_stat(self, NR_HIT, 1);
}

int access_CLOCK_Pro(policy_t *self, unsigned long vpn)
{
	data_CLOCK_Pro_t *data = (data_CLOCK_Pro_t *)self->data;
	pt_t *pt = data->pt;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;
	bool fault = false;

	cnt_access(1);

	page = pt_walk(pt, addr);

	if (!page)
		fault = true;
	else if (!page_resident(page))
		fault = true;

	if (fault)
		page_fault_CLOCK_Pro(self, addr, page);
	else
		page_hit_CLOCK_Pro(self, page);

	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
