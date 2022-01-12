/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "opt.h"
#include "../lib/refault.h"

void init_OPT(policy_t *self, unsigned long memsz);
void fini_OPT(policy_t *self);
int malloc_OPT(policy_t *self, unsigned long addr, unsigned long size);
int mfree_OPT(policy_t *self, unsigned long addr);
int access_OPT(policy_t *self, unsigned long addr);
void post_sim_OPT(policy_t *self);

data_OPT_t data_OPT;
policy_t policy_OPT = {
	.name = "opt",
	.init = init_OPT,
	.fini = fini_OPT,
	.access = access_OPT,
	.mem_alloc = malloc_OPT,
	.mem_free = mfree_OPT,
	.post_sim = post_sim_OPT,
	.data = &data_OPT,
};

typedef struct {
	ref_node_t *last;
	bool present;
} page_md_t;

#define page_md(_page)				((page_md_t *)(_page)->private)
#define page_present(_page)			(page_md(_page)->present)
#define page_mkpresent(_page)		{ page_present(_page) = true; }
#define page_mknotpresent(_page)	{ page_present(_page) = false; }

static inline void alloc_page_md(struct page *page)
{
	page->private = malloc(sizeof(page_md_t));
	page_md(page)->last = NULL;
	page_md(page)->present = false;
}

static inline ref_node_t *
get_ref_node_last(struct page *page)
{
	return page_md(page)->last;
}

static inline void
set_ref_node_last(struct page *page, ref_node_t *node)
{
	page_md(page)->last = node;
}

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

	(*ma)->nr_present = 0;

	stat = malloc(sizeof(ma_stat_t));
	stat->nr_present_acc = 0;
	stat->nr_ref = 0;

	(*ma)->stat = stat;
	(*ma)->obsolete = false;
}

void init_OPT(policy_t *self, unsigned long memsz)
{
	data_OPT_t *data = self->data;
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

	pt_init(&data->pt);

	INIT_LIST_HEAD(&data->page_list);
	INIT_LIST_HEAD(&data->ref_graph);

	mem_area_init(&def_ma, 0, 0, 0, 0);
	data->def_ma = def_ma;
	INIT_LIST_HEAD(&data->ma_list);
}

static void
create_mem_area(data_OPT_t *data, unsigned long addr, unsigned long size)
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
free_mem_area(data_OPT_t *data, unsigned long addr)
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
find_mem_area(data_OPT_t *data, unsigned long addr)
{
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;

	list_for_each_entry(ma, ma_list, entry) {
		if (ma->start <= addr && addr < ma->end)
			return ma;
	}

	return data->def_ma;
}

void fini_OPT(policy_t *self)
{
	if (!refault_stat)
		goto skip_refault;

	printf("refault dist (avg): %E\n", avg_refault_dist());

skip_refault:
	if (!policy_stat)
		goto skip;

	data_OPT_t *data = self->data;
	struct list_head *ma_list = &data->ma_list;
	mem_area_t *ma;
	ma_stat_t *mstat;

	mstat = data->def_ma->stat;
	double nr_present_avg = (double) mstat->nr_present_acc / mstat->nr_ref;

	printf("[default]\n");
	printf("--------------- page stats ----------------\n");
	printf("       nr_present: %20lf\n", nr_present_avg);
	printf("\n");

	list_for_each_entry(ma, ma_list, entry) {
		mstat = ma->stat;
		nr_present_avg = (double) mstat->nr_present_acc / mstat->nr_ref;

		printf("[%#14lx - %#14lx (%lu)]\n", ma->start, ma->end,
				(ma->end - ma->start));
		printf("--------------- page stats ----------------\n");
		printf("       nr_present: %20lf\n", nr_present_avg);
		printf("\n");
	}

skip:
	/* Let page table freed automatically at program termination */
	return;
}

int malloc_OPT(policy_t *self, unsigned long addr, unsigned long size)
{
	data_OPT_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	if (stat->malloc_size_max < size)
		stat->malloc_size_max = size;

	stat->malloc_size_acc += size;
	stat->malloc_cnt++;

	create_mem_area(data, addr, size);

	policy_count_stat(self, NR_MEM_ALLOC, 1);
	return 0;
}

int mfree_OPT(policy_t *self, unsigned long addr)
{
	data_OPT_t *data = self->data;
	mem_stat_t *stat = data->mem_stat;

	stat->mfree_cnt++;

	free_mem_area(data, addr);

	policy_count_stat(self, NR_MEM_FREE, 1);
	return 0;
}

static ref_node_t *
new_ref_node(struct list_head *ref_graph, unsigned long vpn,
		unsigned long rel_time)
{
	ref_node_t *node;

	node = malloc(sizeof(ref_node_t));
	node->vpn = vpn;
	node->next = NULL;
	node->rel_time = rel_time;
	node->unlock_time = rel_time;
	list_add_tail(&node->entry, ref_graph);

	return node;
}

static void
snapshot_buffer(const struct avl_tree_node *cand,
		const struct avl_tree_node *locked)
{
	ref_node_t *node;

	printf("====== buffer ======\n");

	avl_tree_for_each_in_order(node, cand, ref_node_t, cand_node)
		printf("[%9lu] vpn: %lu\n", node->time_left, node->vpn);

	printf("====== locked ======\n");
	avl_tree_for_each_in_order(node, locked, ref_node_t, locked_node)
		printf("[%9lu] vpn: %lu\n", node->unlock_time, node->vpn);

	printf("====================\n");
}

static int
cmp_locked(const struct avl_tree_node *a, const struct avl_tree_node *b)
{
	unsigned long a_key = avl_tree_entry(a, ref_node_t, locked_node)->unlock_time;
	unsigned long b_key = avl_tree_entry(b, ref_node_t, locked_node)->unlock_time;

	if (a_key < b_key)
		return -1;
	else if (a_key == b_key)
		return 0;
	else
		return 1;
}

static int
cmp_cand(const struct avl_tree_node *a, const struct avl_tree_node *b)
{
	unsigned long a_key = avl_tree_entry(a, ref_node_t, cand_node)->time_left;
	unsigned long b_key = avl_tree_entry(b, ref_node_t, cand_node)->time_left;

	if (a_key < b_key)
		return -1;
	else if (a_key == b_key)
		return 0;
	else
		return 1;
}

static struct avl_tree_node *
delete_max_cand(struct avl_tree_node **avl)
{
	struct avl_tree_node *node = avl_tree_last_in_order(*avl);

	avl_tree_remove(avl, node);
	return node;
}

static struct avl_tree_node *
delete_min_cand(struct avl_tree_node **avl)
{
	struct avl_tree_node *node = avl_tree_first_in_order(*avl);

	avl_tree_remove(avl, node);
	return node;
}

static void
insert_locked(struct avl_tree_node **avl, ref_node_t *node)
{
	avl_tree_insert(avl, &node->locked_node, cmp_locked);
}

static void
insert_cand(struct avl_tree_node **avl, ref_node_t *node)
{
	avl_tree_insert(avl, &node->cand_node, cmp_cand);
}

static void
update_opt_stat(policy_t *self)
{
	data_OPT_t *data = self->data;
	mem_area_t *ma;
	ma_stat_t *mstat;

	/* default memory area */
	ma = data->def_ma;
	mstat = ma->stat;

	mstat->nr_present_acc += ma->nr_present;
	mstat->nr_ref++;

	/* each memory area */
	list_for_each_entry(ma, &data->ma_list, entry) {
		mstat = ma->stat;

		mstat->nr_present_acc += ma->nr_present;
		mstat->nr_ref++;
	}
}

void
try_unlock(struct avl_tree_node **locked, struct avl_tree_node **cand,
		unsigned long rel_time, data_OPT_t *data, unsigned long *nr_obsolete)
{
	ref_node_t *node, *next;
	mem_area_t *ma;
	unsigned long addr;
	struct avl_tree_node *locked_node;

	while ((locked_node = avl_tree_first_in_order(*locked))) {
		node = avl_tree_entry(locked_node, ref_node_t, locked_node);
		if (rel_time < node->unlock_time)
			break;

		avl_tree_remove(locked, locked_node);

		addr = vpn_to_addr(node->vpn);
		ma = find_mem_area(data, addr);

		next = node->next;
		if (next) {
			insert_cand(cand, next);
		} else {
			(*nr_obsolete)++;
			ma->nr_present--;
		}
	}
}

void sim_opt(policy_t *self)
{
	data_OPT_t *data = self->data;
	struct list_head *ref_graph = &data->ref_graph;
	pt_t *pt = data->pt;
	mem_area_t *ma, *victim_ma;

	ref_node_t *node, *next, *victim_node;
	unsigned long nr_pages = data->nr_pages;
	unsigned long nr_present = 0;
	unsigned long addr;
	struct page *page, *victim;

	unsigned long rel_time_fin;
	unsigned long nr_obsolete = 0;

	struct avl_tree_node *cand = NULL, *locked = NULL;
	struct avl_tree_node *cand_node;

	unsigned long last_rel_time = 0, rel_time = 0;
	unsigned long hit_skipped;

	rel_time_fin = data->rel_time;

	list_for_each_entry(node, ref_graph, entry)
		node->time_left = rel_time_fin - node->rel_time;

	list_for_each_entry(node, ref_graph, entry) {
		addr = vpn_to_addr(node->vpn);
		page = pt_walk(pt, addr);
		ma = find_mem_area(data, addr);

		last_rel_time = rel_time;
		rel_time = node->rel_time;

		if (rel_time == 0) {
			hit_skipped = 0;
		} else {
			hit_skipped = rel_time - last_rel_time - 1;
		}

		try_unlock(&locked, &cand, rel_time, data, &nr_obsolete);

		policy_count_stat(self, NR_TOTAL, 1 + hit_skipped);
		policy_count_stat(self, NR_HIT, hit_skipped);
		cnt_access(1 + hit_skipped);

		if (page_present(page)) {
			delete_max_cand(&cand);
			nr_present--;

			if (rel_time < node->unlock_time) {
				insert_locked(&locked, node);
			} else {
				next = node->next;
				if (next) {
					insert_cand(&cand, next);
				} else {
					nr_obsolete++;
					ma->nr_present--;
				}
			}

			nr_present++;
			policy_count_stat(self, NR_HIT, 1);

			if (debug) {
				printf("HIT;  vpn: %lu\n", node->vpn);
				snapshot_buffer(cand, locked);
			}

			continue;
		}

		/* page fault */

		reg_fault(addr);

		if (nr_present == nr_pages) {
			if (nr_obsolete) {
				reg_evict(0);
				nr_obsolete--;
			} else {
				cand_node = delete_min_cand(&cand);
				victim_node = avl_tree_entry(cand_node, ref_node_t, cand_node);
				victim = pt_walk(pt, vpn_to_addr(victim_node->vpn));
				page_mknotpresent(victim);

				reg_evict(vpn_to_addr(victim_node->vpn));

				victim_ma = find_mem_area(data, vpn_to_addr(victim_node->vpn));
				victim_ma->nr_present--;
			}
			nr_present--;
		}

		if (rel_time < node->unlock_time) {
			insert_locked(&locked, node);
		} else {
			next = node->next;
			if (next)
				insert_cand(&cand, next);
			else
				nr_obsolete++;
		}

		page_mkpresent(page);
		ma->nr_present++;
		nr_present++;

		policy_count_stat(self, NR_MISS, 1);
		update_opt_stat(self);

		if (self->cold_state && nr_present == nr_pages) {
			self->cold_state = false;
			policy_count_stat(self, NR_COLD_MISS, self->stats.cnt[NR_MISS]);
			self->warm_state = true;
		}

		if (debug) {
			printf("MISS; vpn: %lu\n", node->vpn);
			snapshot_buffer(cand, locked);
		}
	}

	last_rel_time = rel_time;
	rel_time = data->rel_time;

	if (rel_time == 0) {
		hit_skipped = 0;
	} else {
		hit_skipped = rel_time - last_rel_time - 1;
	}

	policy_count_stat(self, NR_TOTAL, hit_skipped);
	policy_count_stat(self, NR_HIT, hit_skipped);
	cnt_access(hit_skipped);
}

void
post_sim_OPT(policy_t *self)
{
	sim_opt(self);
}

int access_OPT(policy_t *self, unsigned long vpn)
{
	data_OPT_t *data = (data_OPT_t *)self->data;
	pt_t *pt = data->pt;
	struct list_head *ref_graph = &data->ref_graph;
	unsigned long addr = vpn_to_addr(vpn);
	struct page *page;
	ref_node_t *node, *last;
	unsigned long rel_time = data->rel_time;

	page = pt_walk(pt, addr);
	if (!page) {
		/* first access to the vpn */
		page = map_alloc_page(pt, addr);
		alloc_page_md(page);

		node = new_ref_node(ref_graph, vpn, rel_time);
		set_ref_node_last(page, node);
	} else {
		/* get the last ref to the vpn */
		last = get_ref_node_last(page);

		if (rel_time - last->unlock_time <= data->nr_pages) {
			last->unlock_time = rel_time;
		} else {
			node = new_ref_node(ref_graph, vpn, rel_time);
			last->next = node;
			set_ref_node_last(page, node);
		}
	}

	data->rel_time++;

	return 0;
}
