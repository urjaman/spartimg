#include <dos.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include "sputil.h"
#include "fatinfo.h"
#include "blockio.h"
#include "robustrd.h"

struct rr_state *rr_init(int drive) {
	struct rr_state *s = ccalloc(1,sizeof(struct rr_state));
	s->drive = drive;
	s->Buffers = ccalloc(1,sizeof(struct rr_buf));
	s->Buffers[0].cnt = 1;
	s->Buffers[0].buf = cmalloc(512);
	s->BuffersCnt = 1;
	return s;
}

void rr_fat_meta(struct rr_state *s, struct fat_info* i, unsigned long FatUsedSec, unsigned fat_iosz)
{
	struct rr_buf *newbuf;
	unsigned bufsz = fat_iosz * 512;
	unsigned bufcnt = 1;
	unsigned bi;
	s->FatStart = i->FatStart;
	s->FatUsedSec = FatUsedSec;
	s->FatCnt = i->FatCnt;
	s->FatSectors = i->FatSectors;
	s->RootDirStart = i->DirStart;
	s->DataStart = i->DataStart;
	bufcnt += (FatUsedSec + (fat_iosz-1)) / fat_iosz;
	newbuf = ccalloc(bufcnt, sizeof(struct rr_buf));
	newbuf[0] = s->Buffers[0];
	free(s->Buffers);
	s->Buffers = newbuf;
	for (bi = 1; bi < bufcnt; bi++) {
		s->Buffers[bi].lba = s->FatStart + (fat_iosz * (bi-1));
		s->Buffers[bi].cnt = fat_iosz;
		s->Buffers[bi].buf = malloc(bufsz);
	}
}

static const char* lbadesc(struct rr_state *s, unsigned long lba) {
	unsigned FatN;
	unsigned long FatSec;
	const char *usedness;
	static char desc[40];
	if (lba == 0) return "PBR";
	if (lba >= s->DataStart) return "Data";
	if (lba < s->FatStart) return "Reserved sector";
	if (lba >= s->RootDirStart) {
		unsigned long luu = lba - s->RootDirStart;
		sprintf(desc, "Root Directory sector %lu", luu);
		return desc;
	}
	FatSec = lba - s->FatStart;
	FatN = FatSec / s->FatSectors;
	FatSec = FatSec % s->FatSectors;
	if (FatSec >= s->FatUsedSec) usedness = "unused";
	else usedness = "used";

	sprintf(desc, "FAT%u Sector %lu - %s", FatN+1, FatSec, usedness);
	return desc;
}

struct rr_brq {
	unsigned long lba;
	unsigned short cnt;
	unsigned char *bufb;
};

static void rr_buffread(struct rr_state *s, struct rr_brq* r)
{
	while (r->lba < s->BufMaxLBA) {
		unsigned found = 0;
		unsigned bi;
		for (bi = 0;bi < s->Buffered; bi++) {
			struct rr_buf *bb = &(s->Buffers[bi]);
			/* Contains first sector of request ? */
			if ( (bb->lba <= r->lba) &&
				((bb->lba + bb->cnt) > r->lba) ) {
				unsigned char *bo = bb->buf;
				unsigned bob = r->lba - bb->lba;
				unsigned bufc = bb->cnt - bob;
				bo += 512 * bob;
				if (bufc > r->cnt)
					bufc = r->cnt;
				memcpy(r->bufb, bo, bufc * 512);
				r->cnt -= bufc;
				r->lba += bufc;
				r->bufb += bufc * 512;
				found = 1;
				break;
			}
			/* Contains last sector of request ? */
			if ( (bb->lba < (r->lba+r->cnt)) &&
				((bb->lba + bb->cnt) >= (r->lba+r->cnt)) ) {
				unsigned bob = (r->lba+r->cnt) - bb->lba;
				unsigned char *bufbo = r->bufb;
				bufbo += (bb->lba - r->lba) * 512;
				memcpy(bufbo, bb->buf, bob);
				r->cnt -= bob;
				found = 1;
				break;
			}
		}
		if (!found)
			return;
		if (!r->cnt)
			return;
	}

};

static int rr_diskread(struct rr_state *s, unsigned long lba, unsigned short cnt, unsigned char*bufb);
static int rr_errhandler(int errv, struct rr_state *s, unsigned long lba, unsigned short cnt, unsigned char *bufb)
{
	char numbuf[16];
	unsigned off;
	int skippable = 1;
	if (cnt > 1) {
		unsigned short i;
		printf("\nRead error 0x%04X - splitting request", errv);
		for (i = 0; i < cnt; i++) {
			rr_diskread(s, lba+i, 1, bufb+i*512);
		}
		printf("\nSplit finished.");
		return 0;
	}
	printf("\nRead error 0x%04X lba %lu, type: %s", errv, lba, lbadesc(s,lba));
	if ( (s->FatCnt > 1) && (lba < s->RootDirStart) && (lba >= s->FatStart) ) {
		unsigned ThisFAT;
		unsigned ThatFAT;
		unsigned long FatSect;
		FatSect = lba - s->FatStart;
		ThisFAT = FatSect / s->FatSectors;
		FatSect = FatSect % s->FatSectors;
		ThatFAT = (ThisFAT + 1 ) % s->FatCnt;
		if ((ThisFAT >= 1)&&(FatSect < s->FatUsedSec)) {
			struct rr_brq req;
			printf("\nChecking buffered copy of FAT 1.. ");
			req.lba = lba;
			req.cnt = cnt;
			req.bufb = bufb;
			rr_buffread(s, &req);
			if (!req.cnt) {
				printf("Found.");
				return 0;
			}
			printf("NOT Found?!?");
		}
		printf("\nTrying alternative FAT(s)..");
		do {
			int rv;
			unsigned long thatLBA = s->FatStart;
			thatLBA += ThatFAT * s->FatSectors;
			thatLBA += FatSect;
			rv = bigread(s->drive, thatLBA, cnt, bufb);
			if (!rv) {
				printf(" - FAT%u worked", ThatFAT+1);
				return 0;
			}
			ThatFAT = (ThatFAT + 1) % s->FatCnt;
		} while (ThatFAT != ThisFAT);
		printf(" - Tried.");
	}
	if (lba == 0) skippable = 0;
	if ((lba >= s->FatStart) && (lba < s->RootDirStart)) {
		unsigned long FatSect;
		FatSect = lba - s->FatStart;
		FatSect = FatSect % s->FatSectors;
		if (FatSect < s->FatUsedSec) skippable = 0;
	}
	if ((!skippable)||(!s->SkipAll)) {
		printf("\nAction? (Retry/Quit%s) ", skippable ? "/Skip/skipAll" : "");
		do {
			int ch = getch();
			ch = toupper(ch);
			if (skippable) {
				if (ch == 'A') {
					s->SkipAll = 1;
					break;
				}
				if (ch == 'S')  {
					break;
				}
			}
			if (ch == 'Q') {
				oops("Quit.");
			}
			if (ch == 'R') {
				return errv;
			}
		} while(1);
	}
	/* Only skip ends up here */
	/* Task: fabricate a sector... */
	if ((lba >= s->FatStart) && (lba < s->RootDirStart)) {
		/* Unused part of the FAT ... zero it out. */
		memset(bufb, 0, 512);
		return 0;
	}
	/* Baseline: make it ASCII, not binary. */
	memset(bufb, 0x20, 512);
	/* If in a directory, mark these entries as deleted (not end of dir). */
	for(off=0;off<512;off+= 32) {
		bufb[off] = 0xE5;
	}
	/* Make it not produce long lines. */
	for (off=0;off<512;off += 64) {
		bufb[off+1] = 0xD;
		bufb[off+2] = 0xA;
	}
	bufb[510] = 0xD;
	bufb[511] = 0xA;
	/* Encode who made it and what is up. */
	memcpy(bufb+3,    "SPARTIMG", 8);
	memcpy(bufb+64+3, "BADSECTOR", 9);
	off = sprintf(numbuf, "%lu", lba);
	memcpy(bufb+128+3, numbuf, off);
	return 0;
}

static int rr_diskread(struct rr_state *s, unsigned long lba, unsigned short cnt, unsigned char*bufb)
{
	int rv;
	int v = 0;
	do {
		rv = bigread(s->drive, lba, cnt, bufb);
		if (rv != 0) {
			v = 1;
			rv = rr_errhandler(rv, s, lba, cnt, bufb);
		}
	} while (rv);
	return v;

}

int rr_read(struct rr_state *s, unsigned long lba, unsigned short cnt, void * buffer)
{
	struct rr_brq req;
	int v;

	if (!cnt)
		return 0;

	req.lba = lba;
	req.cnt = cnt;
	req.bufb = buffer;

	rr_buffread(s, &req);
	if (!req.cnt)
		return 0;

	v =  rr_diskread(s, req.lba, req.cnt, req.bufb);
	if (v) printf("\n");

	if (s->Buffered < s->BuffersCnt) {
		if (s->Buffers[s->Buffered].lba == lba) {
			unsigned long maxlba;
			if (s->Buffers[s->Buffered].cnt < cnt) {
				cnt = s->Buffers[s->Buffered].cnt;
				printf("\nInternal Warning: unexpectedly small buffer (lba %lu)\n", lba);
				v = 1;
			}
			if (cnt < s->Buffers[s->Buffered].cnt) {
				s->Buffers[s->Buffered].cnt = cnt;
			}
			memcpy(s->Buffers[s->Buffered].buf, buffer, cnt*512);
			s->Buffered++;
			maxlba = lba+cnt;
			if (maxlba > s->BufMaxLBA)
				s->BufMaxLBA = maxlba;
		}
	}
	return v;
}







