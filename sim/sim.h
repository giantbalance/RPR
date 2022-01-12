/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _SIM_H
#define _SIM_H

#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define NR_READ_CHUNK			1024
#define MAX_NR_POLICY			20

#define PAGE_SHIFT				12
#define PAGE_SIZE				(1UL << PAGE_SHIFT)
#define PAGE_MASK				(~(PAGE_SIZE - 1))
#define PAGE_ALIGN(_addr)		(((_addr) + PAGE_SIZE - 1) & PAGE_MASK)
#define addr_to_vpn(_addr)		((_addr) >> PAGE_SHIFT)
#define vpn_to_addr(_vpn)		((_vpn) << PAGE_SHIFT)

/*
 * Trace entry types
 *
 * 4 MSB in addr are used for the signitures
 *
 * 0000: memory ref
 * 1000: malloc
 * 1001: calloc
 * 1010: realloc
 * 1011: free
 * 1100: mmap?
 * 1111: icount
 */

#define TYPE_REF				0x0UL
#define TYPE_MALLOC				0x8UL
#define TYPE_CALLOC				0x9UL
#define TYPE_REALLOC			0xaUL
#define TYPE_FREE				0xbUL
#define TYPE_ICOUNT				0xfUL

#define TYPE_SHIFT				60
#define ADDR_MASK				((0x1UL << TYPE_SHIFT) - 1)
#define TYPE_MASK				~ADDR_MASK
#define ENTRY_TYPE(_addr)		((_addr) >> TYPE_SHIFT)
#define ENTRY_ADDR(_addr)		((_addr) & ADDR_MASK)

#define policy_count_stat(_policy, _stat, _cnt)		{	\
	(_policy)->stats.cnt[_stat] += _cnt;						\
}


enum sim_stat {
	NR_STATS = 0,
	NR_HIT = NR_STATS,
	NR_MISS,
	NR_COLD_MISS,
	NR_TOTAL,
	NR_INST,
	NR_MEM_ALLOC,
	NR_MEM_FREE,
	NR_STATS_VERBOSE
};

struct sim_stats {
	long cnt[NR_STATS_VERBOSE];
};

struct __attribute__((__packed__)) ref_entry {
	unsigned long addr;
	int size;
};

struct __attribute__((__packed__)) malloc_entry {
	unsigned long addr;
	unsigned long size;
};

struct __attribute__((__packed__)) calloc_entry {
	unsigned long addr;
	unsigned long nmemb;
	unsigned long size;				// size per member
};

struct __attribute__((__packed__)) relloc_entry {
	unsigned long addr;
	unsigned long ptr;
	unsigned long size;
};

struct __attribute__((__packed__)) free_entry {
	unsigned long addr;
};

typedef struct policy_t {
	char name[20];
	void (*init)(struct policy_t *policy, unsigned long memsz);
	void (*fini)(struct policy_t *policy);
	int (*access)(struct policy_t *policy, unsigned long vpn);
	int (*mem_alloc)(struct policy_t *policy,
			unsigned long addr, unsigned long size);
	int (*mem_free)(struct policy_t *policy, unsigned long addr);
	void (*post_sim)(struct policy_t *policy);
	struct sim_stats stats;
	bool cold_state;
	bool warm_state;
	void *data;
} policy_t;

extern policy_t policy[];
extern int nr_policy;
extern bool debug;
extern bool verbose;
extern bool policy_stat;
extern bool refault_stat;

#endif
