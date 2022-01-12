/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "policy/common.h"

policy_t policy[MAX_NR_POLICY];
int nr_policy;
bool debug;
bool verbose;
bool policy_stat;
bool refault_stat;

const char * const sim_stat_text[] = {
	"      nr_hit",
	"     nr_miss",
	"nr_cold_miss",
	"    nr_total",
	"     nr_inst",
	"nr_mem_alloc",
	" nr_mem_free",
};

void wrong_args(int argc, char **argv)
{
	printf("usage: %s <policy> <memory size (kB)> <trace file> [-v] [-s] [-d]\n", argv[0]);
	printf("-v: verbose mode\n");
	printf("-s: print policy stat\n");
	printf("-d: debug mode\n");
	printf("-r: print refault stat\n");
	exit(1);
}

void check_args(int argc, char **argv)
{
	if (argc < 4)
		wrong_args(argc, argv);
}

void parse_opt_args(int argc, char **argv)
{
	int i;

	for (i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "-v"))
			verbose = true;
		else if (!strcmp(argv[i], "-s"))
			policy_stat = true;
		else if (!strcmp(argv[i], "-d"))
			debug = true;
		else if (!strcmp(argv[i], "-r"))
			refault_stat = true;
		else
			wrong_args(argc, argv);
	}
}

void init_policy_list(void)
{
	nr_policy = 0;
	__init_policy_list(policy, &nr_policy);

	if (nr_policy > MAX_NR_POLICY) {
		fprintf(stderr, "Too many policies registered..\n");
		exit(1);
	}
}

policy_t *search_policy(const char *name)
{
	int i;

	for (i = 0; i < nr_policy; i++) {
		if (!strcmp(name, policy[i].name))
			return &policy[i];
	}

	return NULL;
}

void init_policy(policy_t *policy, unsigned long memsz)
{
	int i;

	policy->init(policy, memsz);
	policy->cold_state = true;
	policy->warm_state = false;

	for (i = NR_HIT; i < NR_STATS_VERBOSE; i++)
		(policy->stats).cnt[i] = 0;
}

size_t read_trace(void *ptr, size_t size, FILE *tracefile, bool term)
{
	size_t nr;

	nr = fread(ptr, 1, size, tracefile);
	if (term && nr != size) {
		fprintf(stderr, "Invalid remaining trace length\n");
		exit(1);
	}

	return nr;
}

void sim_ref(unsigned long addr, int size, policy_t *policy)
{
	unsigned long vpn;

	if (debug)
		printf("%#018lx %#x\n", addr, size);

	for (vpn = addr_to_vpn(addr);
			vpn <= addr_to_vpn(addr + size - 1); vpn++) {
		policy->access(policy, vpn);
		if (!policy->cold_state && !policy->warm_state) {
			policy->stats.cnt[NR_COLD_MISS] = policy->stats.cnt[NR_MISS];
			policy->warm_state = true;
		}
	}
}

void sim_malloc(unsigned long addr, unsigned long size,
		policy_t *policy)
{
	int err;

	if (debug)
		printf("%#lx = malloc(%#lx)\n", addr, size);

	err = policy->mem_alloc(policy, addr, size);
	if (err) {
		fprintf(stderr, "malloc() failed\n");
		exit(1);
	}
}

void sim_calloc(unsigned long addr, unsigned long nmemb, unsigned long size,
		policy_t *policy)
{
	int err;

	if (debug)
		printf("%#lx = calloc(%#lx, %#lx)\n", addr, nmemb, size);

	err = policy->mem_alloc(policy, addr, nmemb * size);
	if (err) {
		fprintf(stderr, "calloc() failed\n");
		exit(1);
	}
}

void sim_realloc(unsigned long addr, unsigned long ptr, unsigned long size,
		policy_t *policy)
{
	int err;

	if (debug)
		printf("%#lx = realloc(%#lx, %#lx)\n", addr, ptr, size);

	err = policy->mem_free(policy, ptr);
	if (err) {
		fprintf(stderr, "realloc() failed\n");
		exit(1);
	}

	err = policy->mem_alloc(policy, addr, size);
	if (err) {
		fprintf(stderr, "realloc() failed\n");
		exit(1);
	}
}

void sim_free(unsigned long addr, policy_t *policy)
{
	int err;

	if (debug)
		printf("free(%#lx)\n", addr);

	err = policy->mem_free(policy, addr);
	if (err) {
		fprintf(stderr, "free() failed\n");
		exit(1);
	}
}

void count_inst(unsigned long icount, policy_t *policy)
{
	policy->stats.cnt[NR_INST] = icount;
}

void simulate(policy_t *policy, FILE *tracefile)
{
	unsigned long addr, type;
	int ref_size;
	unsigned long m_size, m_nmemb, m_ptr;
	size_t nr;

	nr = read_trace(&addr, sizeof(unsigned long), tracefile, false);

	while (nr == sizeof(unsigned long)) {
		type = ENTRY_TYPE(addr);
		addr = ENTRY_ADDR(addr);

		switch (type) {
			case TYPE_REF:
				read_trace(&ref_size, sizeof(int), tracefile, true);
				sim_ref(addr, ref_size, policy);
				break;

			case TYPE_MALLOC:
				read_trace(&m_size, sizeof(unsigned long), tracefile, true);
				sim_malloc(addr, m_size, policy);
				break;

			case TYPE_CALLOC:
				read_trace(&m_nmemb, sizeof(unsigned long), tracefile, true);
				read_trace(&m_size, sizeof(unsigned long), tracefile, true);
				sim_calloc(addr, m_nmemb, m_size, policy);
				break;

			case TYPE_REALLOC:
				read_trace(&m_ptr, sizeof(unsigned long), tracefile, true);
				read_trace(&m_size, sizeof(unsigned long), tracefile, true);
				sim_realloc(addr, m_ptr, m_size, policy);
				break;

			case TYPE_FREE:
				sim_free(addr, policy);
				break;

			case TYPE_ICOUNT:
				count_inst(addr, policy);
				break;

			default:
				/* wrong path */
				printf("Wrong trace entry type..\n");
				exit(1);
		}

		nr = read_trace(&addr, sizeof(unsigned long), tracefile, false);
	}
}

void post_sim(policy_t *policy)
{
	if (policy->post_sim)
		policy->post_sim(policy);
}

void report(policy_t *policy)
{
	struct sim_stats *stats = &policy->stats;
	unsigned long hit, miss, total;
	unsigned long cold_miss, warm_miss;
	unsigned long inst;
	double hit_ratio, miss_ratio;
	double miss_rate;
	int i;

	if (!policy->warm_state)
		stats->cnt[NR_COLD_MISS] = stats->cnt[NR_MISS];

	hit = stats->cnt[NR_HIT];
	miss = stats->cnt[NR_MISS];
	cold_miss = stats->cnt[NR_COLD_MISS];
	total = stats->cnt[NR_TOTAL];
	inst = stats->cnt[NR_INST];

	hit_ratio = (double) hit / total * 100;
	miss_ratio = (double) miss / total * 100;

	warm_miss = miss - cold_miss;

	if (inst)
		miss_rate = (double) warm_miss / inst * 1000000;

	if (inst) {
		printf("+----------------------------+\n");
		printf("|  hit ratio:    %6.2lf %%    |\n", hit_ratio);
		printf("| miss ratio:    %6.2lf %%    |\n", miss_ratio);
		printf("|  miss rate: %9.2lf mpmi |\n", miss_rate);
		printf("+----------------------------+\n");
	} else {
		printf("+----------------------------+\n");
		printf("|  hit ratio:    %6.2lf %%    |\n", hit_ratio);
		printf("| miss ratio:    %6.2lf %%    |\n", miss_ratio);
		printf("|  miss rate: no icount info |\n");
		printf("+----------------------------+\n");
	}

	if (verbose) {
		for (i = 0; i < NR_STATS_VERBOSE; i++)
			printf("%s\t%ld\n", sim_stat_text[i], stats->cnt[i]);
	} else {
		for (i = 0; i < NR_STATS; i++)
			printf("%s\t%ld\n", sim_stat_text[i], stats->cnt[i]);
	}
}

void fini_policy(policy_t *policy)
{
	policy->fini(policy);
}

int main(int argc, char **argv)
{
	FILE *tracefile;
	policy_t *policy;
	unsigned long memsz;

	check_args(argc, argv);

	init_policy_list();
	policy = search_policy(argv[1]);
	if (!policy) {
		printf("No matching policy..\n");
		exit(1);
	}

	memsz = atoi(argv[2]);
	tracefile = fopen(argv[3], "rb");

	parse_opt_args(argc, argv);

	init_policy(policy, memsz);
	simulate(policy, tracefile);
	post_sim(policy);
	report(policy);
	fini_policy(policy);

	fclose(tracefile);

	return 0;
}
