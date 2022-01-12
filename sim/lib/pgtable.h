/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _PGTABLE_H
#define _PGTABLE_H

#include "list.h"

/*
 * 4-level page table implementation forked from Linux
 * PGD - PUD - PMD - PTE
 */

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGD_SHIFT			39
#define PTRS_PER_PGD		512

/*
 * 3rd level page
 */
#define PUD_SHIFT			30
#define PTRS_PER_PUD		512

/*
 * PMD_SHIFT determines the size of the area a middle-level
 * page table can map
 */
#define PMD_SHIFT			21
#define PTRS_PER_PMD		512

/*
 * entries per page directory level
 */
#define PTE_SHIFT			12
#define PTRS_PER_PTE		512

#define PMD_SIZE	(_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE - 1))
#define PUD_SIZE	(_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE - 1))
#define PGDIR_SIZE	(_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))

struct page;

typedef struct { struct page *page; } pte_t;
typedef struct { pte_t *pte[PTRS_PER_PTE]; } pmd_t;
typedef struct { pmd_t *pmd[PTRS_PER_PMD]; } pud_t;
typedef struct { pud_t *pud[PTRS_PER_PUD]; } pgd_t;
typedef struct { pgd_t *pgd[PTRS_PER_PGD]; } pt_t;

struct page {
	pte_t *pte;
	unsigned long addr;
	bool referenced;
	struct list_head entry;
	struct list_head gentry;	// used in policies using two list entries
	struct list_head centry;	// used in policies using three list entries
	struct list_head rentry;	// used in policies using four list entries
	void *private;				// used for policy data structures
};

static inline unsigned long pgd_index(unsigned long addr)
{
	return (addr >> PGD_SHIFT) & (PTRS_PER_PGD - 1);
}

static inline unsigned long pud_index(unsigned long addr)
{
	return (addr >> PUD_SHIFT) & (PTRS_PER_PUD - 1);
}

static inline unsigned long pmd_index(unsigned long addr)
{
	return (addr >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
}

static inline unsigned long pte_index(unsigned long addr)
{
	return (addr >> PTE_SHIFT) & (PTRS_PER_PTE - 1);
}

static inline pgd_t *pgd_offset(pt_t *pt, unsigned long addr)
{
	return pt->pgd[pgd_index(addr)];
}

static inline pud_t *pud_offset(pgd_t *pgd, unsigned long addr)
{
	return pgd->pud[pud_index(addr)];
}

static inline pmd_t *pmd_offset(pud_t *pud, unsigned long addr)
{
	return pud->pmd[pmd_index(addr)];
}

static inline pte_t *pte_offset(pmd_t *pmd, unsigned long addr)
{
	return pmd->pte[pte_index(addr)];
}

static inline bool page_young(struct page *page)
{
	return page->referenced;
}

static inline void page_mkyoung(struct page *page)
{
	page->referenced = true;
}

static inline void page_mkold(struct page *page)
{
	page->referenced = false;
}

static inline bool test_and_clear_page_young(struct page *page)
{
	bool ret = page_young(page);
	page_mkold(page);
	return ret;
}

extern void pt_init(pt_t **pt);
extern struct page *pt_walk(pt_t *pt, unsigned long addr);
int map_page(pt_t *pt, unsigned long addr, struct page *page);
extern struct page *map_alloc_page(pt_t *pt, unsigned long addr);
extern void unmap_addr(pt_t *pt, unsigned long addr);
extern int unmap_free_page(struct page *page);

extern struct page *alloc_page(pte_t *pte, unsigned long addr);
extern void free_page(struct page *page);

#endif
