/* vim: set shiftwidth=4 softtabstop=4 tabstop=4 : */
#ifndef _LIB_REFAULT_H
#include "pgtable.h"

extern void reg_evict(unsigned long addr);
extern void reg_fault(unsigned long addr);
extern double avg_refault_dist(void);
extern void cnt_access(unsigned long cnt);

#endif
