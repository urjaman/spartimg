#include <dos.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "fatinfo.h"
#include "sputil.h"


static unsigned short read2(unsigned char *p) {
	return p[0] | (p[1] << 8);
}

static unsigned long read4 (unsigned char*p) {
	unsigned long r;
	r = read2(p) | (((unsigned long)read2(p+2)) << 16);
	return r;
}

int fat_identify_buf(void * pbrbuf, struct fat_info* i) {
	unsigned long FatStartSector;
	unsigned long FatSectors;
	unsigned long RootDirStartSector;
	unsigned long RootDirSectors;
	unsigned long DataStartSector;
	unsigned long DataSectors;
	unsigned short BytesPerSec;
	unsigned long TotSec;
	unsigned long FATSz;
	unsigned long Clusters;
	struct fat_pbr *pbr = pbrbuf;

	BytesPerSec = read2(pbr->BytesPerSec);
	if (BytesPerSec != 512) oops("Unsupported sector size.");

	FatStartSector = read2(pbr->RsvdSecCnt);

	if (FatStartSector == 0)
		oops("Bad (zero) RsvdSecCnt");

	FATSz = read2(pbr->FATSz16);
	if (FATSz == 0)
		oops("FAT32 not supported (FATSz zero)");

	TotSec = read2(pbr->TotSec16);
	if (TotSec == 0)
		TotSec = read4(pbr->TotSec32);

	i->FatSectors = FATSz;
	i->FatCnt = pbr->NumFATs;
	FatSectors = FATSz * pbr->NumFATs;
	RootDirStartSector = FatStartSector + FatSectors;
	RootDirSectors = (32 * read2(pbr->RootEntCnt) + BytesPerSec - 1) / BytesPerSec;
	DataStartSector = RootDirStartSector + RootDirSectors;
	DataSectors = TotSec - DataStartSector;

	Clusters = DataSectors / pbr->SecPerClus;

	if (Clusters < 4085) {
		i->FatType = 12;
	} else {
		i->FatType = 16;
	}
	if (Clusters > 65525) oops("FAT32 is not supported (too many clusters)");

	i->Clusters = Clusters;
	i->SecPerClust = pbr->SecPerClus;
	i->LastLBA = TotSec - 1;
	i->DataStart = DataStartSector;
	i->DirStart = RootDirStartSector;
	i->FatStart = FatStartSector;
	i->ClustMap = NULL;
	i->SPT = read2(pbr->SecPerTrk);

	return 0;
}

void fat_info_print(struct fat_info *fi)
{
	printf("FAT%u Info:\n", fi->FatType);
	printf("\t%u Sectors Per Cluster (%u bytes)\n", fi->SecPerClust, fi->SecPerClust*512);
	printf("\t%u Clusters\n", fi->Clusters);
	printf("\tData Start LBA: %lu\n", fi->DataStart);
	printf("\tLast LBA: %lu\n",fi->LastLBA);
	printf("\tSectors Per Track: %u\n", fi->SPT);
}

/* Suggest a nice count of sectors to use when reading/writing
 * this filesystem */
unsigned fat_iosize(struct fat_info *fi)
{
	unsigned suggest = fi->SPT * 2;
	if (suggest < 16) {
		if (fi->FatType == 12)
			suggest = 36;
		else
			suggest = 32;
	}
	if (suggest > 36) {
		suggest = fi->SPT;
	}
	if (suggest > 36) {
		suggest = 32;
	}
	return suggest;
}