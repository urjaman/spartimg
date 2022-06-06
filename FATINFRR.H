#ifndef _FATINFRR_H_
#define _FATINFRR_H_
#include "fatinfo.h"
#include "robustrd.h"

int fat_identify(struct rr_state *rr, struct fat_info *fi);
int fat_clustermap(struct rr_state *rr, struct fat_info *i);

#endif