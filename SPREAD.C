/* Sparse Partition Read */

#include <dos.h>
#include <io.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include "spartimg.h"
#include "sputil.h"
#include "fatinfo.h"
#include "blockio.h"

void usage(const char* me)  {
	printf("Usage: %s <DRIVE:> <FILE>\n", me);
	exit(1);
}

void infotext(void) {
	puts("Sparse FAT-12/16 Partition Reader; built "  __DATE__ "\n");
	puts("\tReads used portions of specified drive into a");
	puts("\tcustom format file.\n");
	usage("SPREAD");
}

int gen_sparse_entry(struct fat_info *fi, struct spfmtentry *e, long* clust) {
	long cl = *clust;
	unsigned long lba;
	unsigned long cnt;
	unsigned ClustByt;
	unsigned char ClustBit;
	unsigned char bm;
	unsigned SPC = fi->SecPerClust;
	unsigned char* Map = fi->ClustMap;
	int go = 1;

	if (cl < 0) {
		lba = 0;
		cnt = fi->DataStart;
		cl = 0;
	} else {
		lba = fi->DataStart + (cl * SPC);
		cnt = 0;
	}

	ClustByt = cl >> 3;
	ClustBit = 1 << (cl & 7);
	do {
		bm = Map[ClustByt];
		if ((ClustBit == 1)&&(bm == 0xFF)) {
			cnt += (8 * SPC);
			ClustByt += 1;
			cl += 8;
			continue;
		}
		do {
			if (bm & ClustBit) {
				cnt += SPC;
				cl += 1;
				ClustBit = ClustBit << 1;
			} else {
				go = 0;
			}
		} while (go && ClustBit);
		if (!ClustBit) {
			ClustByt += 1;
			ClustBit = 1;
		}
	} while (go);

	e->lba = lba;
	e->count = cnt;
	do {
		bm = Map[ClustByt];
		if ((ClustBit == 1)&&(bm == 0)) {
			ClustByt += 1;
			cl += 8;
			continue;
		}
		do {
			if (bm & ClustBit) {
				*clust = cl;
				return 1;
			} else {
				cl += 1;
				ClustBit = ClustBit << 1;
			}
		} while (ClustBit);
		ClustByt += 1;
		ClustBit = 1;
	} while (cl < fi->Clusters);

	/* if we fall off, that was the last entry */
	return 0;
}

void print_sparse_entries(struct fat_info *fi) {
	struct spfmtentry e;
	long clust = -1;
	int r;
	do {
		r = gen_sparse_entry(fi, &e, &clust);
		printf("LBA %08lu LEN %08lu\n", e.lba, e.count);
	} while (r);
}

unsigned long write_sparse_header(int fd, struct fat_info *fi) {
	unsigned long outbytes = 0;
	unsigned long outsectors = 0;
	struct spfmthdr hdr;
	struct spfmtentry e;
	unsigned char zerobuf[512];
	unsigned pad;
	unsigned long ecnt = 0;
	unsigned long datasectors = 0;
	long clust = -1;
	int r;

	hdr.magic = SPMAGIC;
	hdr.entrysectors = 0;
	hdr.lastLBA = fi->LastLBA;

	cwrite(fd, &hdr, sizeof(hdr));
	outbytes += sizeof(hdr);

	do {
		r = gen_sparse_entry(fi, &e, &clust);
		cwrite(fd, &e, sizeof(e));
		outbytes += sizeof(e);
		ecnt += 1;
		datasectors += e.count;
	} while(r);

	outsectors = (outbytes + 511) / 512;
	pad = 512 - (outbytes & 511);
	memset(zerobuf, 0, pad);

	cwrite(fd, zerobuf, pad);
	cseek(fd, 0, SEEK_SET);

	hdr.entrysectors = outsectors;

	cwrite(fd, &hdr, sizeof(hdr));
	cseek(fd, 0, SEEK_END);

	printf("Header: %lu LBA&CNT entries\n", ecnt);
	return datasectors;
}


void write_spdata(int fd, int drive, struct fat_info *fi, unsigned long dsects) {
	unsigned WBufSect = fat_iosize(fi);
	struct spfmtentry e;
	long clust = -1;
	void *DatBuf = malloc(WBufSect * 512);
	unsigned long donesects = 0;
	unsigned dot80 = 0;
	unsigned long nextdotsec = dsects/78;
	int r;

	if (!DatBuf)
		oops("Out Of Memory (Data Buffer)");

	printf("Copying data to file...\n");
	do {
		unsigned long i;
		r = gen_sparse_entry(fi, &e, &clust);
		for (i = 0; i < e.count;i += WBufSect) {
			int rv;
			unsigned sectors = WBufSect;
			unsigned long left = e.count - i;

			if (left < sectors)
				sectors = left;

			if ((rv = bigread(drive,e.lba + i,sectors,DatBuf)) != 0) {
				printf("\nRead error: 0x%04X\n", rv);
				exit(1);
			}

			cwrite(fd, DatBuf, sectors*512);

			donesects += sectors;
			while (donesects >= nextdotsec) {
				printf(".");
				dot80 += 1;
				nextdotsec = (dsects * dot80) / 78;
			}
		}
	} while (r);
	printf("\nDone\n");
	free(DatBuf);
}


int main(int argc, char**argv) {
	int drive;
	int wfd;

	struct fat_info fi;
	unsigned long datasectors;

	if (argc < 2) infotext();
	if (strcmp(argv[1], "/?")==0) infotext();

	drive = driveno(argv[1]);
	if (drive < 0) usage(argv[0]);

	printf("Preparing to image drive %d\n", drive);

	if (fat_identify(drive, &fi) != 0)
		oops("Unexplained fat_identify failure");

	fat_info_print(&fi);

	printf("Reading FAT..\n");
	fat_clustermap(drive, &fi);

	if (argc < 3) {
		printf("Dump of the sparse entries:\n");
		print_sparse_entries(&fi);

		printf("Pass filename to actually do something.");
		exit(0);
	}

	wfd = ccreat(argv[2]);

	datasectors = write_sparse_header(wfd, &fi);
	write_spdata(wfd, drive, &fi, datasectors);

	close(wfd);
	return 0;
}