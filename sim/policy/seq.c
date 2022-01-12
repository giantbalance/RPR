/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include "seq.h"
#include "../lib/refault.h"

void init_SEQ(policy_t *self, unsigned long memsz);
void fini_SEQ(policy_t *self);
int malloc_SEQ(policy_t *self, unsigned long addr, unsigned long size);
int mfree_SEQ(policy_t *self, unsigned long addr);
int access_SEQ(policy_t *self, unsigned long addr);

data_SEQ_t data_SEQ;
policy_t policy_SEQ = {
	.name = "seq",
	.init = init_SEQ,
	.fini = fini_SEQ,
	.access = access_SEQ,
	.mem_alloc = malloc_SEQ,
	.mem_free = mfree_SEQ,
	.data = &data_SEQ,
};

#define seq_nth_fault_time(_seq, _n)	\
	(_seq->fault_time[(_seq->most_recent_idx + (N + 1 - _n)) % N])

static void
__snapshot_seq(seq_t *seq)
{
	char *dir;

	if (seq->dir == UP)
		dir = "UP";
	else if (seq->dir == DOWN)
		dir = "DOWN";
	else if (seq->dir == NIL)
		dir = "NIL";
	else
		assert(false);

	printf("<%lx, %lx, %4s>, %9lu, %9lu\n", seq->low_end, seq->high_end, dir,
			seq_nth_fault_time(seq, 1), seq_nth_fault_time(seq, N));
}

static void
__snapshot_all_seq(global_seq_t *gseq)
{
	seq_t *seq;

	printf("==================== SEQ LIST ====================\n");

	list_for_each_entry(seq, &gseq->seq_list, entry)
		__snapshot_seq(seq);
}

static void
snapshot_all_seq_start(global_seq_t *gseq, const char *str)
{
	printf("-------------------------------------------------> %s START\n", str);
	__snapshot_all_seq(gseq);
}

static void
snapshot_all_seq_end(global_seq_t *gseq, const char *str)
{
	printf("-------------------------------------------------> %s end\n", str);
	__snapshot_all_seq(gseq);
}

static void
gseq_init(global_seq_t **gseq)
{
	global_seq_t *new;

	new = malloc(sizeof(global_seq_t));
	new->nr_seq = 0;
	new->fault_time = 0;

	INIT_LIST_HEAD(&new->seq_list);
	INIT_LIST_HEAD(&new->young_list);

	*gseq = new;
}

void init_SEQ(policy_t *self, unsigned long memsz)
{
	data_SEQ_t *data = self->data;
	unsigned long nr_pages = (memsz * 1024) >> PAGE_SHIFT;

	if (!nr_pages) {
		fprintf(stderr, "Memory size is too low!\n");
		exit(1);
	}

	data->nr_pages = nr_pages;
	data->nr_present = 0;
	gseq_init(&data->gseq);

	pt_init(&data->pt);

	INIT_LIST_HEAD(&data->page_list);
}

void fini_SEQ(policy_t *self)
{
	data_SEQ_t *data = self->data;
	global_seq_t *gseq = data->gseq;

	if (!refault_stat)
		goto skip_refault;

	printf("refault dist (avg): %E\n", avg_refault_dist());

skip_refault:
	if (!policy_stat)
		goto skip;

	__snapshot_all_seq(gseq);
skip:
	/* Let page table freed automatically at program termination */
	return;
}

int malloc_SEQ(policy_t *self, unsigned long addr, unsigned long size)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_ALLOC]++;
	return 0;
}

int mfree_SEQ(policy_t *self, unsigned long addr)
{
	struct sim_stats *stats = &self->stats;

	stats->cnt[NR_MEM_FREE]++;
	return 0;
}

static inline bool
seq_overfull(global_seq_t *gseq)
{
	if (MAX_NR_SEQ < gseq->nr_seq)
		return true;

	return false;
}

static void
update_young_list(global_seq_t *gseq, seq_t *up)
{
	unsigned long seq_time, up_time;
	struct list_head *young_list = &gseq->young_list;
	seq_t *seq;

	up_time = seq_nth_fault_time(up, N);

	list_del(&up->sentry);

	list_for_each_entry(seq, young_list, sentry) {
		seq_time = seq_nth_fault_time(seq, N);
		if (seq_time < up_time) {
			list_add_tail(&up->sentry, &seq->sentry);
			return;
		}
	}

	list_add_tail(&up->sentry, young_list);
}

static void
record_fault_time(global_seq_t *gseq, seq_t *seq)
{
	unsigned long fault_time = gseq->fault_time;

	seq->most_recent_idx = (seq->most_recent_idx + 1) % N;
	seq->fault_time[seq->most_recent_idx] = fault_time;

	update_young_list(gseq, seq);
}

static void
remove_seq(global_seq_t *gseq, seq_t *seq)
{
	gseq->nr_seq--;
	list_del(&seq->entry);
	list_del(&seq->sentry);
	free(seq);
}

static void
remove_oldest_seq(global_seq_t *gseq)
{
	struct list_head *seq_list = &gseq->seq_list;
	unsigned long length, length_min = -1UL;
	long length_thr;
	seq_t *seq, *oldest;
	unsigned long fault_time, oldest_fault_time = -1UL;

	/*
	 * 1. try removing the oldest seq of length <= L.
	 * 2. if failed, try removing the oldest seq of length <= 2 * L.
	 * 3. if failed, try removing the oldest seq of length <= 4 * L.
	 * ...
	 */

	list_for_each_entry(seq, seq_list, entry) {
		length = seq->high_end - seq->low_end + 1;
		if (length < length_min)
			length_min = length;
	}

	for (length_thr = L; length_thr < length_min; length_thr *= 2)
		assert(length_thr > 0);

	list_for_each_entry(seq, seq_list, entry) {
		fault_time = seq_nth_fault_time(seq, 1);
		if (fault_time < oldest_fault_time) {
			oldest_fault_time = fault_time;
			oldest = seq;
		}
	}

	remove_seq(gseq, oldest);
}

static seq_t *
new_seq(unsigned long vpn, unsigned long fault_time)
{
	int i;
	seq_t *new = malloc(sizeof(*new));

	new->low_end = vpn;
	new->high_end = vpn;
	new->dir = NIL;

	for (i = 0; i < N; i++)
		new->fault_time[i] = 0;

	new->fault_time[0] = fault_time;
	new->most_recent_idx = 0;

	return new;
}

/* Use for only fresh sequences */
static void
add_seq(global_seq_t *gseq, seq_t *new)
{
	struct list_head *seq_list = &gseq->seq_list;
	struct list_head *young_list = &gseq->young_list;
	bool added = false;
	seq_t *seq;
	unsigned long vpn = new->low_end;

	assert(new->low_end == new->high_end);

	list_for_each_entry(seq, seq_list, entry) {
		assert(!(seq->low_end <= vpn && vpn <= seq->high_end));
		if (vpn < seq->low_end) {
			list_add_tail(&new->entry, &seq->entry);
			added = true;
			break;
		}
	}

	if (!added)
		list_add_tail(&new->entry, seq_list);

	list_add_tail(&new->sentry, young_list);

	gseq->nr_seq++;

	if (seq_overfull(gseq))
		remove_oldest_seq(gseq);
}

static void
add_new_seq(global_seq_t *gseq, unsigned long vpn)
{
	seq_t *new = new_seq(vpn, gseq->fault_time);
	add_seq(gseq, new);
	record_fault_time(gseq, new);
}

static bool
try_extend_seq(global_seq_t *gseq, unsigned long vpn)
{
	struct list_head *seq_list = &gseq->seq_list;
	seq_t *seq, *n, *cand = NULL;
	seq_t *prev, *next;
	unsigned long cand_fault, seq_fault;

	list_for_each_entry_safe(seq, n, seq_list, entry) {
		/* Don't waste time.. */
		if (vpn < seq->low_end - 1)
			break;

		switch (seq->dir) {
			case UP:
				if (vpn == seq->high_end + 1) {
					assert(!cand);
					cand = seq;
				}
				break;

			case DOWN:
				if (vpn == seq->low_end - 1) {
					if (cand) {
						cand_fault = seq_nth_fault_time(cand, 1);
						seq_fault = seq_nth_fault_time(seq, 1);
						if (cand_fault < seq_fault) {
							remove_seq(gseq, cand);
							cand = seq;
						} else {
							remove_seq(gseq, seq);
						}
					} else {
						cand = seq;
					}
				}
				break;

			case NIL:
				assert(seq->low_end == seq->high_end);
				if (vpn == seq->high_end + 1 || vpn == seq->low_end - 1) {
					if (cand) {
						cand_fault = seq_nth_fault_time(cand, 1);
						seq_fault = seq_nth_fault_time(seq, 1);
						if (cand_fault < seq_fault) {
							remove_seq(gseq, cand);
							cand = seq;
						} else {
							remove_seq(gseq, seq);
						}
					} else {
						cand = seq;
					}
				}
				break;

			default:
				assert(false);
		}
	}

	if (!cand)
		return false;

	if (cand->entry.prev == seq_list)
		prev = NULL;
	else
		prev = list_prev_entry(cand, entry);

	if (cand->entry.next == seq_list)
		next = NULL;
	else
		next = list_next_entry(cand, entry);

	if (vpn == cand->high_end + 1) {
		cand->high_end++;
		record_fault_time(gseq, cand);

		if (next && next->low_end <= cand->high_end)
			remove_seq(gseq, next);

		cand->dir = UP;

	} else if (vpn == cand->low_end - 1) {
		cand->low_end--;
		record_fault_time(gseq, cand);

		if (prev && cand->low_end <= prev->high_end)
			remove_seq(gseq, prev);

		cand->dir = DOWN;
	} else {
		assert(false);
	}

	return true;
}

static bool
try_split_seq(global_seq_t *gseq, unsigned long vpn)
{
	struct list_head *seq_list = &gseq->seq_list;
	seq_t *seq, *n;

	list_for_each_entry_safe(seq, n, seq_list, entry) {
		if (seq->low_end <= vpn && vpn <= seq->high_end) {
			switch (seq->dir) {
				case UP:
					if (vpn - 1 < seq->low_end) {
						remove_seq(gseq, seq);
					} else {
						seq->high_end = vpn - 1;
					}
					add_new_seq(gseq, vpn);
					break;

				case DOWN:
					if (seq->high_end < vpn + 1) {
						remove_seq(gseq, seq);
					} else {
						seq->low_end = vpn + 1;
					}
					add_new_seq(gseq, vpn);
					break;

				case NIL:
					assert(seq->low_end == seq->high_end);
					if (vpn == seq->low_end) {
						record_fault_time(gseq, seq);
						return true;
					}
					break;

				default:
					assert(false);
			}

			return true;
		}
	}

	return false;
}

void add_page_SEQ(struct list_head *page_list, global_seq_t *gseq,
		struct page *page, unsigned long addr)
{
	unsigned long vpn = addr_to_vpn(addr);
	bool extended, split;

	/*
	 * 1. no overlap and extension
	 * 2. extendible
	 * 3. overlap
	 */
	extended = try_extend_seq(gseq, vpn);
	if (!extended) {
		split = try_split_seq(gseq, vpn);
		if (!split) {
			add_new_seq(gseq, vpn);
		}
	}

	gseq->fault_time++;

	list_add(&page->entry, page_list);
}

static struct page *
choose_victim_in_seq(data_SEQ_t *data, seq_t *seq)
{
	unsigned long vpn;
	unsigned long addr;
	struct page *page;

	assert(seq->dir != NIL);

	if (seq->dir == UP) {
		for (vpn = seq->high_end - M; vpn >= seq->low_end; vpn--) {
			addr = vpn_to_addr(vpn);
			page = pt_walk(data->pt, addr);
			if (page)
				return page;
		}

	} else if (seq->dir == DOWN) {
		for (vpn = seq->low_end + M; vpn <= seq->high_end; vpn++) {
			addr = vpn_to_addr(vpn);
			page = pt_walk(data->pt, addr);
			if (page)
				return page;
		}

	} else {
		assert(false);
	}

	return NULL;
}

static bool
try_evict_page_seq(data_SEQ_t *data)
{
	global_seq_t *gseq = data->gseq;
	struct list_head *young_list = &gseq->young_list;
	unsigned long length;
	struct page *victim;
	seq_t *seq;

	list_for_each_entry(seq, young_list, sentry) {
		length = seq->high_end - seq->low_end + 1;
		if (length < L)
			continue;

		victim = choose_victim_in_seq(data, seq);
		if (victim) {
			unmap_free_page(victim);
			data->nr_present--;
			reg_evict(victim->addr);
			return true;
		}
	}

	return false;
}

static void
evict_page_clock(data_SEQ_t *data)
{
	struct page *page, *victim = NULL;
	struct list_head *page_list = &data->page_list;

	while (!victim) {
		list_for_each_entry_reverse(page, page_list, entry) {
			if (!test_and_clear_page_young(page)) {
				victim = page;
				break;
			}
		}
	}

	/* move page_list->next ~ victim to the tail */
	list_bulk_move_tail(page_list, page_list->next, &victim->entry);

	reg_evict(page->addr);

	/* delete victim from the list */
	unmap_free_page(victim);
	data->nr_present--;
}

void evict_page_SEQ(policy_t *self)
{
	bool evicted;

	evicted = try_evict_page_seq(self->data);
	if (!evicted)
		evict_page_clock(self->data);
}

static inline bool is_full_SEQ(policy_t *self)
{
	data_SEQ_t *data = (data_SEQ_t *)self->data;

	if (data->nr_pages == data->nr_present)
		return true;

	return false;
}

static void
page_fault_SEQ(policy_t *self, unsigned long addr, struct page *page)
{
	data_SEQ_t *data = (data_SEQ_t *)self->data;
	pt_t *pt = data->pt;
	struct list_head *page_list = &data->page_list;
	global_seq_t *gseq = data->gseq;

	reg_fault(addr);

	if (debug)
		printf("MISS\n");

	if (page) {
		/* wrong path */
		fprintf(stderr, "page fault but present?!\n");
		exit(1);
	}

	if (debug)
		snapshot_all_seq_start(gseq, __func__);

	if (is_full_SEQ(self))
		evict_page_SEQ(self);

	page = map_alloc_page(pt, addr);
	add_page_SEQ(page_list, gseq, page, addr);

	data->nr_present++;
	if (self->cold_state && data->nr_present == data->nr_pages)
		self->cold_state = false;

	if (debug)
		snapshot_all_seq_end(gseq, __func__);

	policy_count_stat(self, NR_MISS, 1);
}

static void
page_hit_SEQ(policy_t *self, struct page *page)
{
	if (debug)
		printf("HIT\n");

	page_mkyoung(page);
	policy_count_stat(self, NR_HIT, 1);
}

int access_SEQ(policy_t *self, unsigned long vpn)
{
	data_SEQ_t *data = (data_SEQ_t *)self->data;
	pt_t *pt = data->pt;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;

	cnt_access(1);

	page = pt_walk(pt, addr);

	if (!page)
		page_fault_SEQ(self, addr, page);
	else
		page_hit_SEQ(self, page);

	policy_count_stat(self, NR_TOTAL, 1);
	return 0;
}
