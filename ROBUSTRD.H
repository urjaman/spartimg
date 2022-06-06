#ifndef _ROBUSTRD_H
#define _ROBUSTRD_H
#include "fatinfo.h"

struct rr_buf {
	unsigned long lba;
	unsigned cnt;
	unsigned char *buf;
};

struct rr_state {
	int drive;
	unsigned long FatStart;
	unsigned long FatUsedSec; /* in one FAT */
	unsigned long FatSectors; /* in one FAT */
	unsigned long RootDirStart;
	unsigned long DataStart;
	unsigned char FatCnt;
	unsigned char SkipAll;
	struct rr_buf *Buffers;
	unsigned Buffered;
	unsigned BuffersCnt;
	unsigned long BufMaxLBA;
};

struct rr_state *rr_init(int drive);
void rr_fat_meta(struct rr_state *s, struct fat_info* i, unsigned long FatUsedSec, unsigned fat_iosz);
int rr_read(struct rr_state *s, unsigned long lba, unsigned short cnt, void * buffer);

#endif
