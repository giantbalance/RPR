/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "pgtable.h"

pgd_t *alloc_pgd(pt_t *pt, unsigned long addr)
{
	pgd_t **pgd = &pt->pgd[pgd_index(addr)];
	*pgd = calloc(1, sizeof(pgd_t));

	return *pgd;
}

pud_t *alloc_pud(pgd_t *pgd, unsigned long addr)
{
	pud_t **pud = &pgd->pud[pud_index(addr)];
	*pud = calloc(1, sizeof(pud_t));

	return *pud;
}

pmd_t *alloc_pmd(pud_t *pud, unsigned long addr)
{
	pmd_t **pmd = &pud->pmd[pmd_index(addr)];
	*pmd = calloc(1, sizeof(pmd_t));

	return *pmd;
}

pte_t *alloc_pte(pmd_t *pmd, unsigned long addr)
{
	pte_t **pte = &pmd->pte[pte_index(addr)];
	*pte = calloc(1, sizeof(pte_t));

	return *pte;
}

struct page *alloc_page(pte_t *pte, unsigned long addr)
{
	struct page *page;

	page = malloc(sizeof(struct page));
	page->addr = addr;
	page->pte = pte;
	page->referenced = false;
	page->private = NULL;
	INIT_LIST_HEAD(&page->entry);
	INIT_LIST_HEAD(&page->gentry);
	INIT_LIST_HEAD(&page->centry);
	INIT_LIST_HEAD(&page->rentry);

	return page;
}

void free_page(struct page *page)
{
	list_del(&page->entry);
	free(page);
}

void pt_init(pt_t **pt)
{
	int i;

	*pt = calloc(1, sizeof(pt_t));

	for (i = 0; i < PTRS_PER_PGD; i++) {
		(*pt)->pgd[i] = NULL;
	}
}

struct page *pt_walk(pt_t *pt, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct page *page;

	pgd = pgd_offset(pt, addr);
	if (!pgd)
		return NULL;

	pud = pud_offset(pgd, addr);
	if (!pud)
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (!pmd)
		return NULL;

	pte = pte_offset(pmd, addr);
	if (!pte)
		return NULL;

	page = pte->page;
	if (!page)
		return NULL;

	return page;
}

int map_page(pt_t *pt, unsigned long addr, struct page *page)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(pt, addr);
	if (!pgd)
		pgd = alloc_pgd(pt, addr);

	pud = pud_offset(pgd, addr);
	if (!pud)
		pud = alloc_pud(pgd, addr);

	pmd = pmd_offset(pud, addr);
	if (!pmd)
		pmd = alloc_pmd(pud, addr);

	pte = pte_offset(pmd, addr);
	if (!pte)
		pte = alloc_pte(pmd, addr);

	if (pte->page)
		return -EINVAL;
	pte->page = page;

	return 0;
}

struct page *map_alloc_page(pt_t *pt, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct page *page;

	pgd = pgd_offset(pt, addr);
	if (!pgd)
		pgd = alloc_pgd(pt, addr);

	pud = pud_offset(pgd, addr);
	if (!pud)
		pud = alloc_pud(pgd, addr);

	pmd = pmd_offset(pud, addr);
	if (!pmd)
		pmd = alloc_pmd(pud, addr);

	pte = pte_offset(pmd, addr);
	if (!pte)
		pte = alloc_pte(pmd, addr);

	page = pte->page;
	if (!page) {
		page = alloc_page(pte, addr);
		pte->page = page;
	}

	return page;
}

void unmap_addr(pt_t *pt, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(pt, addr);
	if (!pgd)
		return;

	pud = pud_offset(pgd, addr);
	if (!pud)
		return;

	pmd = pmd_offset(pud, addr);
	if (!pmd)
		return;

	pte = pte_offset(pmd, addr);
	if (!pte)
		return;

	pte->page = NULL;
}

int unmap_free_page(struct page *page)
{
	page->pte->page = NULL;
	free_page(page);

	return 0;
}
