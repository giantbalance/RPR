/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "watch-pro.h"

void init_WATCH_Pro(policy_t *self, unsigned long memsz);
void fini_WATCH_Pro(policy_t *self);
int malloc_WATCH_Pro(policy_t *self, unsigned long addr, unsigned long size);
int mfree_WATCH_Pro(policy_t *self, unsigned long addr);
int access_WATCH_Pro(policy_t *self, unsigned long addr);

data_WATCH_Pro_t data_WATCH_Pro;
policy_t policy_WATCH_Pro = {
	.name = "watch-pro",
	.init = init_WATCH_Pro,
	.fini = fini_WATCH_Pro,
	.access = access_WATCH_Pro,
	.mem_alloc = malloc_WATCH_Pro,
	.mem_free = mfree_WATCH_Pro,
	.data = &data_WATCH_Pro,
};

#define max(x, y)					((x) > (y)? (x) : (y))
#define min(x, y)					((x) > (y)? (y) : (x))

/*
 * Page metadata DSs and APIs
 */
typedef struct {
	bool ghot;						/* global hotness */
	bool whot;						/* local hotness */
	bool resident;
	bool gtesting;					/* is the page in the stack S in LIRS? */
	bool wtesting;					/* is the page in the stack S in LIRS? */
	bool gref;						/* gclock ref */
	bool wref;						/* watch ref */
	watch_t *watch;
} page_md_t;

#define page_md(_page)				((page_md_t *)(_page)->private)
#define page_hot_local(_page)		(page_md(_page)->whot)
#define page_hot_global(_page)		(page_md(_page)->ghot)
#define page_resident(_page)		(page_md(_page)->resident)
#define page_testing_local(_page)	(page_md(_page)->wtesting)
#define page_testing_global(_page)	(page_md(_page)->gtesting)

#define page_mkhot_local(_page)		{ page_hot_local(_page) = true; }
#define page_mkhot_global(_page)	{ page_hot_global(_page) = true; }
#define page_mkcold_local(_page)	{ page_hot_local(_page) = false; }
#define page_mkcold_global(_page)	{ page_hot_global(_page) = false; }
#define page_mkresident(_page)		{ page_resident(_page) = true; }
#define page_mkghost(_page)			{ page_resident(_page) = false; }

#define page_test_start_local(_page)	\
	{ page_testing_local(_page) = true; }
#define page_test_start_global(_page)	\
	{ page_testing_global(_page) = true; }
#define page_test_end_local(_page)		\
	{ page_testing_local(_page) = false; }
#define page_test_end_global(_page)		\
	{ page_testing_global(_page) = false; }

#define page_watch(_page)			(page_md(_page)->watch)

#define page_set_watch(_page, _watch)	{ page_watch(_page) = _watch; }

static inline bool
page_young_local(struct page *page)
{
	return page_md(page)->wref;
}

static inline bool
page_young_global(struct page *page)
{
	return page_md(page)->gref;
}

static inline void
page_mkyoung_local(struct page *page)
{
	page_md(page)->wref = true;
}

static inline void
page_mkyoung_global(struct page *page)
{
	page_md(page)->gref = true;
}

static inline void
page_mkold_local(struct page *page)
{
	page_md(page)->wref = false;
}

static inline void
page_mkold_global(struct page *page)
{
	page_md(page)->gref = false;
}

static inline void
page_mkold_all(struct page *page)
{
	page_mkold(page);
	page_mkold_local(page);
	page_mkold_global(page);
}

static inline void
alloc_page_md(struct page *page)
{
	page->private = malloc(sizeof(page_md_t));
	page_mkold_local(page);
	page_mkold_global(page);
}

static inline void
free_page_md(struct page *page)
{
	free(page->private);
}

static inline bool
test_and_clear_page_young_global(struct page *page)
{
	bool ref, gref;

	ref = test_and_clear_page_young(page);
	gref = page_young_global(page);
	page_mkold_global(page);

	/* TODO: OR/COPY? */
	if (ref)
		page_mkyoung_local(page);

	return ref | gref;
}

static inline bool
test_and_clear_page_young_local(struct page *page)
{
	bool ref, wref;

	ref = test_and_clear_page_young(page);
	wref = page_young_local(page);
	page_mkold_local(page);

	/* TODO: OR/COPY? */
	if (ref)
		page_mkyoung_global(page);

	return ref | wref;
}
/* end of page metadata DSs and APIs */

/*
 * Watch and gclock parameter APIs
 */
static inline unsigned long
watch_cold_max(watch_t *watch)
{
	unsigned long nr_total = watch->nr_cold + watch->nr_hot;

	return max(1, (unsigned long) (watch->cold_ratio * (double) nr_total));
}

static unsigned long
gclock_cold_max(gclock_t *gclock)
{
	unsigned long nr_total = gclock->nr_cold + gclock->nr_hot;

	return max(1, (unsigned long) (gclock->cold_ratio * (double) nr_total));
}

static void
adj_cold_ratio_global(gclock_t *gclock, double delta)
{
	unsigned long nr_total = gclock->nr_hot + gclock->nr_cold;

	if (debug)
		printf("[GLOBAL UPDATE] cold_ratio: %lf%% -> ", gclock->cold_ratio * 100);

	if (delta > 0)
		delta = min(delta, (double) 1 / nr_total);
	else
		delta = max(delta, (double) -1 / nr_total);

	gclock->cold_ratio += delta;

	if (gclock->cold_ratio > 1)
		gclock->cold_ratio = 1;
	else if (gclock->cold_ratio < 0)
		gclock->cold_ratio = 0;

	if (debug)
		printf("%lf%%\n", gclock->cold_ratio * 100);
}

static void
adj_cold_ratio_local(watch_t *watch, double delta)
{
	unsigned long nr_total = watch->nr_hot + watch->nr_cold;

	if (debug)
		printf("[LOCAL UPDATE] cold_ratio: %lf%% -> ", watch->cold_ratio * 100);

	if (delta > 0)
		delta = min(delta, (double) 1 / nr_total);
	else
		delta = max(delta, (double) -1 / nr_total);

	watch->cold_ratio += delta;

	if (watch->cold_ratio > 1)
		watch->cold_ratio = 1;
	else if (watch->cold_ratio < 0)
		watch->cold_ratio = 0;

	if (debug)
		printf("%lf%%\n", watch->cold_ratio * 100);
}

static void
inc_cold_ratio_global(gclock_t *gclock)
{
	adj_cold_ratio_global(gclock, 0.01);
}

static void
dec_cold_ratio_global(gclock_t *gclock)
{
	adj_cold_ratio_global(gclock, -0.01);
}

static void
inc_cold_ratio_local(watch_t *watch)
{
	adj_cold_ratio_local(watch, 0.01);
}

static void
dec_cold_ratio_local(watch_t *watch)
{
	adj_cold_ratio_local(watch, -0.01);
}
/* End of watch and gclock parameter APIs */

static void
print_global_stats(gclock_t *gclock)
{
	unsigned long nr_hot, nr_cold, nr_ghost;
	unsigned long nr_hot_max, nr_cold_max, nr_ghost_max;

	nr_hot = gclock->nr_hot;
	nr_cold = gclock->nr_cold;
	nr_ghost = gclock->nr_ghost;
	nr_ghost_max = gclock->nr_ghost_max;
	nr_cold_max = gclock_cold_max(gclock);
	nr_hot_max = gclock->nr_pages - nr_cold_max;

	printf("----------------------------------------------\n");
	printf("      hot      |     cold      |     ghost    \n");
	printf(" %5lu / %-5lu | %5lu / %-5lu | %5lu / %-5lu \n",
			nr_hot, nr_hot_max, nr_cold, nr_cold_max, nr_ghost, nr_ghost_max);
	printf("----------------------------------------------\n");
}

static void
print_watch_stats(watch_t *watch)
{
	unsigned long nr_hot, nr_cold, nr_ghost;
	unsigned long nr_hot_max, nr_cold_max;

	nr_hot = watch->nr_hot;
	nr_cold = watch->nr_cold;
	nr_ghost = watch->nr_ghost;
	nr_cold_max = watch_cold_max(watch);
	if (nr_hot + nr_cold >= nr_cold_max)
		nr_hot_max = nr_hot + nr_cold - nr_cold_max;
	else
		nr_hot_max = 0;

	printf("----------------------------------------------\n");
	printf("      hot      |     cold      |     ghost    \n");
	printf(" %5lu / %-5lu | %5lu / %-5lu | %5lu / ---   \n",
			nr_hot, nr_hot_max, nr_cold, nr_cold_max, nr_ghost);
	printf("----------------------------------------------\n");
}

static void
snapshot_gclock(gclock_t *gclock)
{
	struct page *page;
	unsigned long addr;
	char *status, *ref, *test;
	char *hand_hot, *hand_cold, *hand_test;

	list_for_each_entry(page, &gclock->page_list, gentry) {
		addr = (unsigned long) page;
		if (page_hot_global(page))
			status = "H";
		else if (page_resident(page))
			status = "C";
		else
			status = "NC";

		if (page_young(page))
			ref = "R";
		else if (page_young_global(page))
			ref = "r";
		else
			ref = "";

		if (page_testing_global(page))
			test = "T";
		else
			test = "";

		if (&page->gentry == gclock->hand_hot)
			hand_hot = "HAND(hot)";
		else
			hand_hot = "";

		if (&page->rentry == gclock->cold_list.next)
			hand_cold = "HAND(cold)";
		else
			hand_cold = "";

		if (&page->gentry == gclock->hand_test)
			hand_test = "HAND(test)";
		else
			hand_test = "";

		printf("%3lx%3s%2s%2s%11s%11s%11s\n", addr % 0x1000, status, ref, test,
				hand_cold, hand_hot, hand_test);
	}
}

static void
snapshot_watch(watch_t *watch)
{
	struct page *page;
	unsigned long addr;
	char *status, *ref, *test;
	char *hand_hot, *hand_cold;

	list_for_each_entry(page, &watch->page_list, entry) {
		addr = (unsigned long) page;
		if (page_hot_local(page))
			status = "H";
		else if (page_resident(page))
			status = "C";
		else
			status = "NC";

		if (page_young(page))
			ref = "R";
		else if (page_young_local(page))
			ref = "r";
		else
			ref = "";

		if (page_testing_local(page))
			test = "T";
		else
			test = "";

		if (&page->entry == watch->hand_hot)
			hand_hot = "HAND(hot)";
		else
			hand_hot = "";

		if (&page->centry == watch->cold_list.next)
			hand_cold = "HAND(cold)";
		else
			hand_cold = "";

		printf("%3lx%3s%2s%2s%11s%11s\n", addr % 0x1000, status, ref, test,
				hand_cold, hand_hot);
	}
}

static void
snapshot_gclock_watch(gclock_t *gclock, watch_t *watch, const char *str)
{
	printf("============================================== %s\n", str);
	if (gclock) {
		printf("<gclock>\n");
		snapshot_gclock(gclock);
		print_global_stats(gclock);
	}
	if (watch) {
		printf("<watch>\n");
		snapshot_watch(watch);
		print_watch_stats(watch);
	}
	printf("==============================================\n");
}

static void
snapshot_all(data_WATCH_Pro_t *data, const char *str)
{
	mem_area_t *ma;
	int watch_cnt = 0;

	printf("============================================== %s\n", str);
	if (data->gclock) {
		printf("<gclock>\n");
		snapshot_gclock(data->gclock);
		print_global_stats(data->gclock);
	}

	if (data->def_ma) {
		printf("<watch %3d>\n", watch_cnt++);
		snapshot_watch(data->def_ma->watch);
		print_watch_stats(data->def_ma->watch);
	}

	list_for_each_entry(ma, &data->ma_list, entry) {
		if (ma->watch) {
			printf("<watch %3d>\n", watch_cnt++);
			snapshot_watch(ma->watch);
			print_watch_stats(ma->watch);
		}
	}

	printf("==============================================\n");
}

static void
watch_init(watch_t **watch)
{
	watch_stat_t *stat;

	*watch = malloc(sizeof(watch_t));
	(*watch)->nr_hot = 0;
	(*watch)->nr_cold = 0;
	(*watch)->nr_ghost = 0;
	(*watch)->nr_hot_global = 0;
	(*watch)->nr_cold_global = 0;
	(*watch)->cold_ratio = 0.01;
	(*watch)->obsolete = false;

	INIT_LIST_HEAD(&(*watch)->page_list);
	INIT_LIST_HEAD(&(*watch)->cold_list);
	(*watch)->hand_hot = &(*watch)->page_list;
	(*watch)->mrf = NULL;

	stat = malloc(sizeof(watch_stat_t));
	stat->nr_present_acc = 0;
	stat->nr_hot_acc = 0;
	stat->nr_cold_acc = 0;
	stat->nr_ghost_acc = 0;
	stat->nr_cold_max_acc = 0;
	stat->nr_ref = 0;

	stat->nr_hand_cold_move = 0;
	stat->nr_hand_hot_move = 0;
	stat->nr_promote = 0;
	stat->nr_demote = 0;
	(*watch)->stat = stat;
}

static void
watch_free(watch_t *watch)
{
	free(watch);
}

static void
gclock_init(gclock_t **gclock, unsigned long nr_pages)
{
	gclock_stat_t *stat;

	*gclock = malloc(sizeof(gclock_t));
	(*gclock)->nr_pages = nr_pages;
	(*gclock)->nr_hot = 0;
	(*gclock)->nr_cold = 0;
	(*gclock)->nr_ghost = 0;
	(*gclock)->nr_ghost_max = nr_pages;
	(*gclock)->cold_ratio = 0.01;

	INIT_LIST_HEAD(&(*gclock)->page_list);
	INIT_LIST_HEAD(&(*gclock)->cold_list);
	(*gclock)->hand_hot = &(*gclock)->page_list;
	(*gclock)->hand_test = &(*gclock)->page_list;

	stat = malloc(sizeof(gclock_stat_t));
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
	(*gclock)->stat = stat;
}

/*
 * Memory area APIs
 */
static void
mem_area_init(mem_area_t **ma, unsigned long req_start, unsigned long req_end,
		unsigned long start, unsigned long end)
{
	*ma = malloc(sizeof(mem_area_t));
	(*ma)->req_start = req_start;
	(*ma)->req_end = req_end;
	(*ma)->start = start;
	(*ma)->end = end;
	watch_init(&(*ma)->watch);
}

static void
create_mem_area(data_WATCH_Pro_t *data, unsigned long addr, unsigned long size)
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
free_mem_area(data_WATCH_Pro_t *data, unsigned long addr)
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
	/* DO NOT FREE THE WATCH! it should be freed when it becomes empty */
	victim->watch->obsolete = true;
	list_del(&victim->entry);
	free(victim);

	stat->nr_mem_area--;
	stat->ma_free_cnt++;
}
/* End of memory area APIs */

void init_WATCH_Pro(policy_t *self, unsigned long memsz)
{
	data_WATCH_Pro_t *data = self->data;
	mem_area_t *def_ma;
	unsigned long nr_pages = (memsz * 1024) >> PAGE_SHIFT;

	mem_stat_t *stat;
	stat = malloc(sizeof(mem_stat_t));

	stat->malloc_size_max = 0;
	stat->malloc_size_acc = 0;
	stat->malloc_cnt = 0;
	stat->mfree_cnt = 0;
	stat->ma_alloc_cnt = 0;
	stat->ma_free_cnt = 0;
	stat->nr_mem_area = 0;

	data->mem_stat = stat;

	if (!nr_pages) {
		fprintf(stderr, "Memory size is too low!\n");
		exit(1);
	}

	mem_area_init(&def_ma, 0, 0, 0, 0);
	data->def_ma = def_ma;
	INIT_LIST_HEAD(&data->ma_list);

	gclock_init(&data->gclock, nr_pages);

	pt_init(&data->pt);
}

void fini_WATCH_Pro(policy_t *self)
{
	if (!policy_stat)
		goto skip;

	data_WATCH_Pro_t *data = self->data;
	gclock_stat_t *gstat = data->gclock->stat;
	mem_stat_t *mstat = data->mem_stat;
	mem_area_t *ma;
	struct list_head *ma_list = &data->ma_list;
	watch_stat_t *wstat;

	double nr_present_avg, nr_hot_avg, nr_cold_avg, nr_ghost_avg,
		   nr_cold_max_avg;
	double nr_hot_global_avg, nr_cold_global_avg;

	nr_present_avg = (double) gstat->nr_present_acc / gstat->nr_ref;
	nr_hot_avg = (double) gstat->nr_hot_acc / gstat->nr_ref;
	nr_cold_avg = (double) gstat->nr_cold_acc / gstat->nr_ref;
	nr_ghost_avg = (double) gstat->nr_ghost_acc / gstat->nr_ref;
	nr_cold_max_avg = (double) gstat->nr_cold_max_acc / gstat->nr_ref;

	double nr_hand_hot_move_avg = (double) gstat->nr_hand_hot_move /
									gstat->nr_ref;
	double nr_hand_cold_move_avg = (double) gstat->nr_hand_cold_move /
									gstat->nr_ref;
	double nr_hand_test_move_avg = (double) gstat->nr_hand_test_move /
									gstat->nr_ref;
	double nr_promote_avg = (double) gstat->nr_promote /
									gstat->nr_ref;
	double nr_demote_avg = (double) gstat->nr_demote /
									gstat->nr_ref;

	unsigned long malloc_size_max = mstat->malloc_size_max;
	double malloc_size_avg = (double) mstat->malloc_size_acc / mstat->malloc_cnt;
	unsigned long malloc_cnt = mstat->malloc_cnt;
	unsigned long mfree_cnt = mstat->mfree_cnt;
	unsigned long ma_alloc_cnt = mstat->ma_alloc_cnt;
	unsigned long ma_free_cnt = mstat->ma_free_cnt;
	unsigned long nr_mem_area = mstat->nr_mem_area;

	printf("============== global stats ==============\n");
	printf("--------------- page stats ----------------\n");
	printf("       nr_present: %20lf\n", nr_present_avg);
	printf("           nr_hot: %20lf\n", nr_hot_avg);
	printf("          nr_cold: %20lf\n", nr_cold_avg);
	printf("         nr_ghost: %20lf\n", nr_ghost_avg);
	printf("      nr_cold_max: %20lf\n", nr_cold_max_avg);
	printf("-------------- action stats ---------------\n");
	printf("        hand(HOT): %20lf\n", nr_hand_hot_move_avg);
	printf("       hand(COLD): %20lf\n", nr_hand_cold_move_avg);
	printf("       hand(TEST): %20lf\n", nr_hand_test_move_avg);
	printf("          promote: %20lf\n", nr_promote_avg);
	printf("           demote: %20lf\n", nr_demote_avg);
	printf("-------------- alloc stats ---------------\n");
	printf("malloc size (max): %lu\n", malloc_size_max);
	printf("malloc size (avg): %lf\n", malloc_size_avg);
	printf("        nr_malloc: %lu\n", malloc_cnt);
	printf("         nr_mfree: %lu\n", mfree_cnt);
	printf("      nr_ma_alloc: %lu\n", ma_alloc_cnt);
	printf("       nr_ma_free: %lu\n", ma_free_cnt);
	printf("      nr_mem_area: %lu\n", nr_mem_area);
	printf("============ per-object stats ============\n");

	wstat = data->def_ma->watch->stat;
	nr_present_avg = (double) wstat->nr_present_acc / wstat->nr_ref;
	nr_hot_avg = (double) wstat->nr_hot_acc / wstat->nr_ref;
	nr_cold_avg = (double) wstat->nr_cold_acc / wstat->nr_ref;
	nr_hot_global_avg = (double) wstat->nr_hot_global_acc / wstat->nr_ref;
	nr_cold_global_avg = (double) wstat->nr_cold_global_acc / wstat->nr_ref;
	nr_ghost_avg = (double) wstat->nr_ghost_acc / wstat->nr_ref;
	nr_cold_max_avg = (double) wstat->nr_cold_max_acc / wstat->nr_ref;

	nr_hand_hot_move_avg = (double) wstat->nr_hand_hot_move / wstat->nr_ref;
	nr_hand_cold_move_avg = (double) wstat->nr_hand_cold_move / wstat->nr_ref;
	nr_promote_avg = (double) wstat->nr_promote / wstat->nr_ref;
	nr_demote_avg = (double) wstat->nr_demote / wstat->nr_ref;

	printf("[default]\n");
	printf("--------------- page stats ----------------\n");
	printf("       nr_present: %20lf\n", nr_present_avg);
	printf("           nr_hot: %20lf\n", nr_hot_avg);
	printf("          nr_cold: %20lf\n", nr_cold_avg);
	printf("   nr_hot(global): %20lf\n", nr_hot_global_avg);
	printf("  nr_cold(global): %20lf\n", nr_cold_global_avg);
	printf("         nr_ghost: %20lf\n", nr_ghost_avg);
	printf("      nr_cold_max: %20lf\n", nr_cold_max_avg);
	printf("-------------- action stats ---------------\n");
	printf("        hand(HOT): %20lf\n", nr_hand_hot_move_avg);
	printf("       hand(COLD): %20lf\n", nr_hand_cold_move_avg);
	printf("          promote: %20lf\n", nr_promote_avg);
	printf("           demote: %20lf\n", nr_demote_avg);
	printf("\n");

	list_for_each_entry(ma, ma_list, entry) {
		wstat = ma->watch->stat;
		nr_present_avg = (double) wstat->nr_present_acc / wstat->nr_ref;
		nr_hot_avg = (double) wstat->nr_hot_acc / wstat->nr_ref;
		nr_cold_avg = (double) wstat->nr_cold_acc / wstat->nr_ref;
		nr_hot_global_avg = (double) wstat->nr_hot_global_acc / wstat->nr_ref;
		nr_cold_global_avg = (double) wstat->nr_cold_global_acc / wstat->nr_ref;
		nr_ghost_avg = (double) wstat->nr_ghost_acc / wstat->nr_ref;
		nr_cold_max_avg = (double) wstat->nr_cold_max_acc / wstat->nr_ref;

		nr_hand_hot_move_avg = (double) wstat->nr_hand_hot_move / wstat->nr_ref;
		nr_promote_avg = (double) wstat->nr_promote / wstat->nr_ref;
		nr_demote_avg = (double) wstat->nr_demote / wstat->nr_ref;

		printf("[%#14lx - %#14lx (%lu)]\n", ma->start, ma->end,
				(ma->end - ma->start));
		printf("--------------- page stats ----------------\n");
		printf("       nr_present: %20lf\n", nr_present_avg);
		printf("           nr_hot: %20lf\n", nr_hot_avg);
		printf("          nr_cold: %20lf\n", nr_cold_avg);
		printf("   nr_hot(global): %20lf\n", nr_hot_global_avg);
		printf("  nr_cold(global): %20lf\n", nr_cold_global_avg);
		printf("         nr_ghost: %20lf\n", nr_ghost_avg);
		printf("      nr_cold_max: %20lf\n", nr_cold_max_avg);
		printf("-------------- action stats ---------------\n");
		printf("        hand(HOT): %20lf\n", nr_hand_hot_move_avg);
		printf("       hand(COLD): %20lf\n", nr_hand_cold_move_avg);
		printf("          promote: %20lf\n", nr_promote_avg);
		printf("           demote: %20lf\n", nr_demote_avg);
		printf("\n");
	}

skip:
	/* Let page table freed automatically at program termination */
	return;
}

int malloc_WATCH_Pro(policy_t *self, unsigned long addr, unsigned long size)
{
	data_WATCH_Pro_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	if (stat->malloc_size_max < size)
		stat->malloc_size_max = size;

	stat->malloc_size_acc += size;
	stat->malloc_cnt++;

	create_mem_area(data, addr, size);

	policy_count_stat(self, NR_MEM_ALLOC, 1);
	return 0;
}

int mfree_WATCH_Pro(policy_t *self, unsigned long addr)
{
	data_WATCH_Pro_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	stat->mfree_cnt++;

	free_mem_area(data, addr);

	policy_count_stat(self, NR_MEM_FREE, 1);
	return 0;
}

/*
 * watch and gclock status APIs
 */
static inline bool
global_hot_empty(gclock_t *gclock)
{
	return gclock->nr_hot == 0;
}

static inline bool
global_cold_empty(gclock_t *gclock)
{
	return gclock->nr_cold == 0;
}

static inline bool
global_ghost_empty(gclock_t *gclock)
{
	return gclock->nr_ghost == 0;
}

static inline bool
global_empty(gclock_t *gclock)
{
	return gclock->nr_hot + gclock->nr_cold == 0;
}

static inline bool
global_full(gclock_t *gclock)
{
	return gclock->nr_hot + gclock->nr_cold >= gclock->nr_pages;
}

static inline bool
global_hot_overfull(gclock_t *gclock)
{
	return !global_empty(gclock) && (gclock->nr_cold < gclock_cold_max(gclock));
}

static inline bool
global_cold_overfull(gclock_t *gclock)
{
	return gclock->nr_cold > gclock_cold_max(gclock);
}

static inline bool
global_ghost_overfull(gclock_t *gclock)
{
	return gclock->nr_ghost > gclock->nr_ghost_max;
}

static inline unsigned long
watch_size(watch_t *watch)
{
	return watch->nr_cold + watch->nr_hot;
}

static inline bool
watch_hot_empty(watch_t *watch)
{
	return watch->nr_hot == 0;
}

static inline bool
watch_cold_empty(watch_t *watch)
{
	return watch->nr_cold == 0;
}

static inline bool
watch_ghost_empty(watch_t *watch)
{
	return watch->nr_ghost == 0;
}

static inline bool
watch_empty(watch_t *watch)
{
	return watch->nr_cold + watch->nr_hot == 0;
}

static inline bool
watch_hot_overfull(watch_t *watch)
{
	return !watch_empty(watch) && (watch->nr_cold < watch_cold_max(watch));
}

static inline bool
watch_cold_overfull(watch_t *watch)
{
	return watch->nr_cold > watch_cold_max(watch);
}

static inline bool
watch_obsolete(watch_t *watch)
{
	return watch->obsolete;
}
/* End of watch and gclock status APIs */

static watch_t *
find_watch(data_WATCH_Pro_t *data, unsigned long addr)
{
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;

	list_for_each_entry(ma, ma_list, entry) {
		if (ma->start <= addr && addr < ma->end)
			return ma->watch;
	}

	return data->def_ma->watch;
}

static void
update_page_stat(policy_t *self)
{
	data_WATCH_Pro_t *data = self->data;
	gclock_t *gclock = data->gclock;
	gclock_stat_t *stat = gclock->stat;
	mem_area_t *ma;
	watch_t *watch;
	watch_stat_t *wstat;

	if (!global_full(gclock))
		return;

	if (self->cold_state)
		self->cold_state = false;

	stat->nr_present_acc += gclock->nr_hot + gclock->nr_cold;
	stat->nr_hot_acc += gclock->nr_hot;
	stat->nr_cold_acc += gclock->nr_cold;
	stat->nr_ghost_acc += gclock->nr_ghost;

	stat->nr_cold_max_acc += gclock_cold_max(gclock);
	stat->nr_ref++;

	/* default watch */
	watch = data->def_ma->watch;
	wstat = watch->stat;

	wstat->nr_present_acc += watch->nr_hot + watch->nr_cold;
	wstat->nr_hot_acc += watch->nr_hot;
	wstat->nr_cold_acc += watch->nr_cold;
	wstat->nr_hot_global_acc += watch->nr_hot_global;
	wstat->nr_cold_global_acc += watch->nr_cold_global;
	wstat->nr_ghost_acc += watch->nr_ghost;

	wstat->nr_cold_max_acc += watch_cold_max(watch);
	wstat->nr_ref++;

	/* per-object watch */
	list_for_each_entry(ma, &data->ma_list, entry) {
		watch = ma->watch;
		wstat = watch->stat;

		wstat->nr_present_acc += watch->nr_hot + watch->nr_cold;
		wstat->nr_hot_acc += watch->nr_hot;
		wstat->nr_cold_acc += watch->nr_cold;
		wstat->nr_hot_global_acc += watch->nr_hot_global;
		wstat->nr_cold_global_acc += watch->nr_cold_global;
		wstat->nr_ghost_acc += watch->nr_ghost;

		wstat->nr_cold_max_acc += watch_cold_max(watch);
		wstat->nr_ref++;
	}
}

/*
 * watch and gclock hand movement APIs
 */
static inline struct page *
watch_get_page_move(struct list_head **hptr)
{
	struct page *page = list_entry(*hptr, struct page, entry);
	*hptr = (*hptr)->next;
	return page;
}

static inline struct page *
gclock_get_page_move(struct list_head **hptr)
{
	struct page *page = list_entry(*hptr, struct page, gentry);
	*hptr = (*hptr)->next;
	return page;
}

static inline struct page *
watch_get_cold_page_move(watch_t *watch)
{
	struct page *page;

	page = list_entry(watch->cold_list.next, struct page, centry);
	list_rotate_left(&watch->cold_list);
	watch->stat->nr_hand_cold_move++;

	return page;
}

static inline struct page *
gclock_get_cold_page_move(gclock_t *gclock)
{
	struct page *page;

	page = list_entry(gclock->cold_list.next, struct page, rentry);
	list_rotate_left(&gclock->cold_list);
	gclock->stat->nr_hand_cold_move++;

	return page;
}

static inline struct page *
gclock_get_ghost_page_move(gclock_t *gclock)
{
	struct page *page;

	page = gclock_get_page_move(&gclock->hand_test);
	gclock->stat->nr_hand_test_move++;

	return page;
}
/* End of watch and gclock hand movement APIs */

static void
__add_hot_page_local(watch_t *watch, struct page *page)
{
	watch->nr_hot++;

	list_add_tail(&page->entry, watch->hand_hot);
}

static void
__add_hot_page_global(gclock_t *gclock, struct page *page)
{
	gclock->nr_hot++;
	page_watch(page)->nr_hot_global++;

	list_add_tail(&page->gentry, gclock->hand_hot);
}

static void
__add_cold_page_local(watch_t *watch, struct page *page)
{
	watch->nr_cold++;

	list_add_tail(&page->entry, watch->hand_hot);
	list_add_tail(&page->centry, &watch->cold_list);
}

static void
__add_cold_page_global(gclock_t *gclock, struct page *page)
{
	gclock->nr_cold++;
	page_watch(page)->nr_cold_global++;

	list_add_tail(&page->gentry, gclock->hand_hot);
	list_add_tail(&page->rentry, &gclock->cold_list);
}

static void
__add_cold_page(gclock_t *gclock, watch_t *watch, struct page *page)
{
	__add_cold_page_local(watch, page);
	__add_cold_page_global(gclock, page);
}

static void
__add_ghost_page_local(watch_t *watch, struct page *page)
{
	struct page *cold_tail;

	watch->nr_ghost++;
	page_mkold_local(page);
	page_test_start_local(page);
	page_mkcold_local(page);
	page_mkghost(page);

	if (list_empty(&watch->cold_list)) {
		list_add_tail(&page->entry, watch->hand_hot);
	} else {
		cold_tail = list_entry(watch->cold_list.next, struct page, centry);
		list_add_tail(&page->entry, &cold_tail->entry);
	}
}

static void
__add_ghost_page_global(gclock_t *gclock, struct page *page)
{
	struct page *cold_tail;

	gclock->nr_ghost++;
	page_mkold_global(page);
	page_test_start_global(page);
	page_mkcold_global(page);
	page_mkghost(page);

	if (list_empty(&gclock->cold_list)) {
		list_add_tail(&page->gentry, gclock->hand_hot);
	} else {
		cold_tail = list_entry(gclock->cold_list.next, struct page, rentry);
		list_add_tail(&page->gentry, &cold_tail->gentry);
	}
}

static void
__add_ghost_page(gclock_t *gclock, watch_t *watch, struct page *page)
{
	page_mkold(page);

	__add_ghost_page_local(watch, page);
	__add_ghost_page_global(gclock, page);
}

static void
add_ghost_page(gclock_t *gclock, watch_t *watch, struct page *page)
{
	__add_ghost_page(gclock, watch, page);
}

static void
isolate_page_global(gclock_t *gclock, struct page *page)
{
	if (!page) {
		/* wrong path */
		fprintf(stderr, "isolating NULL page?!\n");
		exit(1);
	}

	if (gclock->hand_hot == &page->gentry)
		gclock->hand_hot = gclock->hand_hot->next;
	if (gclock->hand_test == &page->gentry)
		gclock->hand_test = gclock->hand_test->next;

	if (page_hot_global(page)) {
		gclock->nr_hot--;
		page_watch(page)->nr_hot_global--;
	} else if (page_resident(page)) {
		gclock->nr_cold--;
		page_watch(page)->nr_cold_global--;
	} else {
		gclock->nr_ghost--;
	}

	list_del_init(&page->gentry);	/* gclock->page_list */
	list_del_init(&page->rentry);	/* gclock->cold_list */
}

static void
isolate_page_local(watch_t *watch, struct page *page)
{
	if (!page) {
		/* wrong path */
		fprintf(stderr, "isolating NULL page?!\n");
		exit(1);
	}

	if (watch->hand_hot == &page->entry)
		watch->hand_hot = watch->hand_hot->next;

	if (page_hot_local(page))
		watch->nr_hot--;
	else if (page_resident(page))
		watch->nr_cold--;
	else
		watch->nr_ghost--;

	list_del_init(&page->entry);	/* watch->page_list */
	list_del_init(&page->centry);	/* watch->cold_list */
}

static void
isolate_page(gclock_t *gclock, struct page *page)
{
	watch_t *watch = page_watch(page);

	isolate_page_global(gclock, page);
	isolate_page_local(watch, page);
}

static inline void
remove_page(gclock_t *gclock, struct page *page)
{
	watch_t *watch = page_watch(page);

	isolate_page(gclock, page);
	free_page_md(page);
	unmap_free_page(page);

	if (watch->mrf == page)
		watch->mrf = NULL;

	if (watch_obsolete(watch)) {
		if (watch_empty(watch) && watch_ghost_empty(watch))
			watch_free(watch);
	}
}

static void
refresh_cold_page_global(gclock_t *gclock, struct page *page)
{
	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);

	isolate_page_global(gclock, page);

	page_mkresident(page);
	page_mkcold_global(page);
	page_test_start_global(page);

	__add_cold_page_global(gclock, page);

	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);
}

static void
refresh_cold_page_local(watch_t *watch, struct page *page)
{
	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);

	isolate_page_local(watch, page);

	page_mkresident(page);
	page_mkcold_local(page);
	page_test_start_local(page);

	__add_cold_page_local(watch, page);

	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);
}

static void
__promote_page_global(gclock_t *gclock, struct page *page)
{
	isolate_page_global(gclock, page);

	page_mkresident(page);
	page_mkhot_global(page);
	page_test_end_global(page);

	gclock->stat->nr_promote++;

	__add_hot_page_global(gclock, page);
}

static void
__promote_page_local(watch_t *watch, struct page *page)
{
	isolate_page_local(watch, page);

	page_mkresident(page);
	page_mkhot_local(page);
	page_test_end_local(page);

	watch->stat->nr_promote++;

	__add_hot_page_local(watch, page);
}

static void
adjust_hand_hot_global(gclock_t *gclock)
{
	gclock_stat_t *gstat = gclock->stat;
	struct page *page;
	int ref;

	while (!global_hot_empty(gclock)) {
		page = gclock_get_page_move(&gclock->hand_hot);

		if (gclock->hand_hot->prev == &gclock->page_list)
			goto next;

		ref = test_and_clear_page_young_global(page);
		if (page_hot_global(page)) {
			if (ref)
				goto next;
			else {
				gclock->hand_hot = gclock->hand_hot->prev;
				break;
			}

		} else {
			if (ref) {
				if (page_testing_global(page)) {
					inc_cold_ratio_global(gclock);
					__promote_page_global(gclock, page);
				} else {
					refresh_cold_page_global(gclock, page);
				}
			} else {
				if (page_resident(page)) {
					if (page_testing_global(page))
						dec_cold_ratio_global(gclock);
					page_test_end_global(page);
				} else {
					dec_cold_ratio_global(gclock);
					dec_cold_ratio_local(page_watch(page));
					remove_page(gclock, page);
				}
			}
		}
next:
		gstat->nr_hand_hot_move++;
	}
}

static void
adjust_hand_test_global(gclock_t *gclock)
{
	gclock_stat_t *stat = gclock->stat;
	struct page *page;
	int ref;

	while (!global_ghost_empty(gclock)) {
		page = gclock_get_page_move(&gclock->hand_test);

		if (gclock->hand_test->prev == &gclock->page_list)
			goto next;

		if (page_hot_global(page))
			goto next;

		ref = test_and_clear_page_young_global(page);
		if (page_resident(page)) {
			/* TODO: terminate test? or no-op? it's a design issue. */
			if (!ref) {
				if (page_testing_global(page))
					dec_cold_ratio_global(gclock);

				page_test_end_global(page);
			}
		} else {
			/* this is the stop point */
			gclock->hand_test = gclock->hand_test->prev;
			break;
		}
next:
		stat->nr_hand_test_move++;
	}
}

static void
adjust_hand_hot_local(gclock_t *gclock, watch_t *watch)
{
	watch_stat_t *wstat = watch->stat;
	struct page *page;
	int ref;

	while (!watch_hot_empty(watch)) {
		page = watch_get_page_move(&watch->hand_hot);

		if (watch->hand_hot->prev == &watch->page_list)
			goto next;

		ref = test_and_clear_page_young_local(page);
		if (page_hot_local(page)) {
			if (ref)
				goto next;
			else {
				/* This is the stop point */
				watch->hand_hot = watch->hand_hot->prev;
				break;
			}

		} else {
			if (ref) {
				if (page_testing_local(page)) {
					inc_cold_ratio_local(watch);
					__promote_page_local(watch, page);
				} else {
					refresh_cold_page_local(watch, page);
				}
			} else {
				if (page_resident(page)) {
					if (page_testing_local(page))
						dec_cold_ratio_local(watch);
					page_test_end_local(page);
				} else {
					dec_cold_ratio_global(gclock);
					dec_cold_ratio_local(watch);
					remove_page(gclock, page);
				}
			}
		}
next:
		wstat->nr_hand_hot_move++;
	}
}

static void
demote_hot_page_global(gclock_t *gclock)
{
	struct page *page;
	int ref;

	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);

	page = gclock_get_page_move(&gclock->hand_hot);
	gclock->stat->nr_hand_hot_move++;

	ref = test_and_clear_page_young_global(page);
	if (!page_resident(page) || !page_hot_global(page) || ref) {
		fprintf(stderr, "demoting a wrong page!\n");
		exit(1);
	}

	isolate_page_global(gclock, page);

	page_mkresident(page);
	page_mkcold_global(page);
	page_test_end_global(page);

	gclock->stat->nr_demote++;

	__add_cold_page_global(gclock, page);

	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);
}

static void
demote_hot_page_local(watch_t *watch)
{
	struct page *page;
	int ref;

	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);

	page = watch_get_page_move(&watch->hand_hot);
	watch->stat->nr_hand_hot_move++;

	ref = test_and_clear_page_young_local(page);
	if (!page_resident(page) || !page_hot_local(page) || ref) {
		fprintf(stderr, "demoting a wrong page!\n");
		exit(1);
	}

	isolate_page_local(watch, page);

	page_mkresident(page);
	page_mkcold_local(page);
	page_test_end_local(page);

	watch->stat->nr_demote++;

	__add_cold_page_local(watch, page);

	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);
}

static void
demote_hot_pages_global(gclock_t *gclock)
{
	adjust_hand_hot_global(gclock);
	while (global_hot_overfull(gclock)) {
		demote_hot_page_global(gclock);
		adjust_hand_hot_global(gclock);
	}
}

static void
demote_hot_pages_local(gclock_t *gclock, watch_t *watch)
{
	adjust_hand_hot_local(gclock, watch);
	while (watch_hot_overfull(watch)) {
		demote_hot_page_local(watch);
		adjust_hand_hot_local(gclock, watch);
	}
}

static void
promote_page_global(gclock_t *gclock, struct page *page)
{
	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);

	__promote_page_global(gclock, page);

	if (global_hot_overfull(gclock))
		demote_hot_pages_global(gclock);

	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);
}

static void
promote_page_local(gclock_t *gclock, struct page *page)
{
	watch_t *watch = page_watch(page);

	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);

	__promote_page_local(watch, page);

	if (watch_hot_overfull(watch))
		demote_hot_pages_local(gclock, watch);

	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);
}

static struct page *
move_hand_cold_local(gclock_t *gclock, watch_t *watch)
{
	struct page *page, *evicted = NULL;
	int ref;

	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);

	while (watch_cold_empty(watch))
		demote_hot_pages_local(gclock, watch);

	page = watch_get_cold_page_move(watch);

	if (!page_resident(page) || page_hot_local(page)) {
		fprintf(stderr, "local cold list containing other types?!\n");
		exit(1);
	}

	ref = test_and_clear_page_young_local(page);
	if (ref) {
		if (page_testing_local(page)) {
			inc_cold_ratio_local(watch);
			promote_page_local(gclock, page);
		} else {
			refresh_cold_page_local(watch, page);
		}
	} else {
		/* evict the cold page; make it non-resident */
		if (page_testing_local(page)) {
			isolate_page(gclock, page);
			add_ghost_page(gclock, watch, page);
		} else {
			remove_page(gclock, page);
		}

		evicted = page;
	}

	if (debug)
		snapshot_gclock_watch(NULL, watch, __func__);

	return evicted;
}

static void
move_hand_cold_global(gclock_t *gclock)
{
	struct page *page, *evicted;
	watch_t *watch;
	int ref;

	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);

	while (global_cold_empty(gclock))
		demote_hot_pages_global(gclock);

	page = gclock_get_cold_page_move(gclock);
	watch = page_watch(page);

	if (!page_resident(page) || page_hot_global(page)) {
		fprintf(stderr, "global cold list containing other types?!\n");
		exit(1);
	}

	/* NULL if no eviction */
	evicted = move_hand_cold_local(gclock, watch);

	if (page == evicted)
		return;

	ref = test_and_clear_page_young_global(page);
	if (ref) {
		if (page_testing_global(page)) {
			inc_cold_ratio_global(gclock);
			promote_page_global(gclock, page);
		} else {
			refresh_cold_page_global(gclock, page);
		}
	}

	if (debug)
		snapshot_gclock_watch(gclock, NULL, __func__);
}

static void
evict_cold_pages(gclock_t *gclock)
{
	while (global_full(gclock))
		move_hand_cold_global(gclock);
}

static void
add_cold_page(gclock_t *gclock, struct page *page)
{
	watch_t *watch = page_watch(page);

	if (debug)
		snapshot_gclock_watch(gclock, page_watch(page), __func__);

	if (global_full(gclock))
		evict_cold_pages(gclock);

	page_mkresident(page);
	page_mkcold_global(page);
	page_mkcold_local(page);
	page_test_start_global(page);
	page_test_start_local(page);

	__add_cold_page(gclock, watch, page);

	if (watch_cold_overfull(watch))
		__promote_page_local(watch, page);

	if (debug)
		snapshot_gclock_watch(gclock, page_watch(page), __func__);
}

static void
promote_ghost_page(gclock_t *gclock, watch_t *watch, struct page *page)
{
	watch_t *curr = page_watch(page);

	if (debug)
		snapshot_gclock_watch(gclock, page_watch(page), __func__);

	isolate_page(gclock, page);

	/* if page_watch(page) is obsolete, move the page to the new watch */
	if (watch_obsolete(curr)) {
		if (watch_empty(curr) && watch_ghost_empty(curr))
			watch_free(curr);
		page_set_watch(page, watch);
	}

	/* parameter tuning */
	inc_cold_ratio_global(gclock);
	inc_cold_ratio_local(watch);

	add_cold_page(gclock, page);

	if (!page_hot_global(page))
		promote_page_global(gclock, page);
	if (!page_hot_local(page))
		promote_page_local(gclock, page);

	if (debug)
		snapshot_gclock_watch(gclock, page_watch(page), __func__);
}

static void
prune_ghost_page(gclock_t *gclock)
{
	struct page *page;

	page = gclock_get_ghost_page_move(gclock);

	if (page_resident(page) || page_hot_global(page) || page_hot_local(page)) {
		fprintf(stderr, "HAND(test) at a wrong target!\n");
		exit(1);
	}

	if (debug)
		snapshot_gclock_watch(gclock, page_watch(page), __func__);

	dec_cold_ratio_global(gclock);
	dec_cold_ratio_local(page_watch(page));
	remove_page(gclock, page);

	if (debug)
		snapshot_gclock_watch(gclock, page_watch(page), __func__);
}

static void
prune_ghost_pages(gclock_t *gclock)
{
	adjust_hand_test_global(gclock);
	while (global_ghost_overfull(gclock)) {
		prune_ghost_page(gclock);
		adjust_hand_test_global(gclock);
	}
}

static void
do_page_fault(policy_t *self, unsigned long addr, struct page *page)
{
	data_WATCH_Pro_t *data = self->data;
	pt_t *pt = data->pt;
	gclock_t *gclock = data->gclock;
	watch_t *watch;
	unsigned long nr_present = gclock->nr_hot + gclock->nr_cold;

	if (debug)
		snapshot_all(data, __func__);

	watch = find_watch(data, addr);

	/* Two cases
	 * 1. Page is not in the list: add as a cold page
	 * 2. Page is in the list (as ghost page): add as a hot page
	 */
	if (!page) {
		/* Case 1 */
		page = map_alloc_page(pt, addr);
		alloc_page_md(page);
		page_set_watch(page, watch);
		add_cold_page(gclock, page);

	} else {
		/* Case 2 */
		if (page_resident(page)) {
			/* wrong path */
			fprintf(stderr, "adding already present page?!\n");
			exit(1);
		}

		adjust_hand_hot_global(gclock);
		adjust_hand_hot_local(gclock, page_watch(page));
		page = pt_walk(pt, addr);
		if (page) {
			promote_ghost_page(gclock, watch, page);
		} else {
			page = map_alloc_page(pt, addr);
			alloc_page_md(page);
			page_set_watch(page, watch);
			add_cold_page(gclock, page);
		}
	}

	if (global_ghost_overfull(gclock))
		prune_ghost_pages(gclock);

	if (gclock->nr_hot + gclock->nr_cold < nr_present) {
		printf("buffer shrink?! %lu -> %lu", nr_present,
				gclock->nr_hot + gclock->nr_cold);
	}

	if (gclock->nr_hot + gclock->nr_cold > gclock->nr_pages) {
		fprintf(stderr, "buffer overflow!\n");
		exit(1);
	}

	if (watch->mrf)
		page_mkold_all(watch->mrf);
	watch->mrf = page;

	if (debug)
		snapshot_all(data, __func__);
}

static void
page_fault(policy_t *self, unsigned long addr, struct page *page)
{
	if (debug)
		printf("MISS\n");

	do_page_fault(self, addr, page);

	policy_count_stat(self, NR_MISS, 1);
	update_page_stat(self);
}

static void
page_hit(policy_t *self, struct page *page)
{
	if (debug)
		printf("HIT\n");

	page_mkyoung(page);
	policy_count_stat(self, NR_HIT, 1);
}

int access_WATCH_Pro(policy_t *self, unsigned long vpn)
{
	data_WATCH_Pro_t *data = self->data;
	pt_t *pt = data->pt;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;
	bool fault = false;

	page = pt_walk(pt, addr);

	if (!page)
		fault = true;
	else if (!page_resident(page))
		fault = true;

	if (fault)
		page_fault(self, addr, page);
	else
		page_hit(self, page);

	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
