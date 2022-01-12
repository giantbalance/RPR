/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _COMMON_H
#define _COMMON_H

#include "../sim.h"

extern policy_t policy_OPT;
extern policy_t policy_LRU;
extern policy_t policy_FIFO;
extern policy_t policy_CLOCK;
extern policy_t policy_CLOCK_Pro;
extern policy_t policy_WATCH_Pro;
extern policy_t policy_mallocstat;
extern policy_t policy_aLIFO;
extern policy_t policy_SEQ;

static void __init_policy_list(policy_t *policy, int *nr_policy)
{
	int nr = 0;

	policy[nr++] = policy_OPT;
	policy[nr++] = policy_LRU;
	policy[nr++] = policy_CLOCK;
	policy[nr++] = policy_FIFO;
	policy[nr++] = policy_CLOCK_Pro;
	policy[nr++] = policy_WATCH_Pro;
	policy[nr++] = policy_mallocstat;
	policy[nr++] = policy_aLIFO;
	policy[nr++] = policy_SEQ;

	*nr_policy = nr;
}

#endif
