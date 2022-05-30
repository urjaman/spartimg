#include <dos.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "fatinfo.h"
#include "sputil.h"
#include "blockio.h"

struct fat_pbr {
	unsigned char bootjmp[3];
	unsigned char oemname[8];
	unsigned char BytesPerSec[2];
	unsigned char SecPerClus;
	unsigned char RsvdSecCnt[2];
	unsigned char NumFATs;
	unsigned char RootEntCnt[2];
	unsigned char TotSec16[2];
	unsigned char Media;
	unsigned char FATSz16[2];
	unsigned char SecPerTrk[2];
	unsigned char NumHeads[2];
	unsigned char HiddSec[4];
	unsigned char TotSec32[4];
	/* Following is only valid on FAT12/16 */
	unsigned char DrvNum;
	unsigned char Reserved0_NT;
	unsigned char BootSig; /* 0x29 */
	unsigned char VolID[4];
	unsigned char VolLab[11];
	unsigned char FilSysType[8];
	unsigned char BootCode[448];
	unsigned char BootSign[2];
};

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
	i->FatStart = FatStartSector;
	i->ClustMap = NULL;
	i->SPT = read2(pbr->SecPerTrk);

	return 0;
}

int fat_identify(int drive, struct fat_info *fi) {
	struct fat_pbr pbr;
	int rv;

	assert(sizeof(pbr) == 512);
	if ((rv = bigread(drive,0,1,&pbr)) != 0) {
		printf("Read error: 0x%04X\n", rv);
		exit(1);
	}
	return fat_identify_buf(&pbr, fi);
}

static int fat16_clustermap(int drive, struct fat_info *i) {
	const unsigned FBufSect = 16;
	unsigned short * FatBuffer = NULL;

	unsigned long ClustersWithData = 0;

	/* The Map has an extra byte, to make it sure the
	   scan runs into non-0xFF before the end of Map */

	unsigned int MapSize = ((i->Clusters + 7 ) / 8) + 1;
	unsigned long j;
	unsigned long ClustProcessed = 0;
	unsigned ClustByte = 0;
	unsigned char ClustBit = 1;
	unsigned char ClustOut = 0;
	int rv;

	/* FAT16 dependency here */
	unsigned long UsedFatSectors = (((i->Clusters+2) * 2) + 511) / 512;


	i->ClustMap = calloc(1,MapSize);
	if (i->ClustMap == NULL)
		oops("Out Of Memory (FAT Cluster usage Map).");


	FatBuffer = malloc(FBufSect * 512);
	if (!FatBuffer)
		oops("Out of Memory (FAT Disk read buffer).");


	for (j=0; j<UsedFatSectors; j += FBufSect) {
		unsigned BufOff = 0;
		unsigned k;
		unsigned ClustInBuf;
		unsigned long ClustLeft;
		unsigned long LeftSec = UsedFatSectors - j;
		if (LeftSec > FBufSect)
			LeftSec = FBufSect;

		if ((rv = bigread(drive,i->FatStart + j,LeftSec,FatBuffer)) != 0) {
			printf("Read error: 0x%04X\n", rv);
			exit(1);
		}

		ClustInBuf = FBufSect * (512 / 2);

		if (j == 0) {
			BufOff = 2;
			ClustInBuf -= 2;
		}
		ClustLeft = i->Clusters - ClustProcessed;
		if (ClustLeft < ClustInBuf)
			ClustInBuf = ClustLeft;

		for (k = 0; k < ClustInBuf; k++) {
			unsigned short e = FatBuffer[BufOff + k];
			if ((e != 0)&&(e != 0xFFF7)) {
				ClustOut |= ClustBit;
				ClustersWithData++;
			}
			ClustBit = ClustBit << 1;
			if (!ClustBit) {
				i->ClustMap[ClustByte] = ClustOut;
				ClustOut = 0;
				ClustBit = 1;
				ClustByte += 1;
			}
		}
		ClustProcessed += ClustInBuf;

	}
	if (ClustOut)
		i->ClustMap[ClustByte] = ClustOut;


	free(FatBuffer);

	printf("Cluster Bitmap generated.\n\t%lu / %lu used / total\n",
		ClustersWithData, i->Clusters);
	return 0;

}

static int fat12_clustermap(int drive, struct fat_info *i) {
	const unsigned FBufSect = 6;
	unsigned char * FatBuffer = NULL;

	unsigned ClustersWithData = 0;

	/* The Map has an extra byte, to make it sure the
	   scan runs into non-0xFF before the end of Map */

	unsigned int MapSize = ((i->Clusters + 7 ) / 8) + 1;
	unsigned j;
	unsigned ClustProcessed = 0;
	unsigned ClustByte = 0;
	unsigned char ClustBit = 1;
	unsigned char ClustOut = 0;
	int rv;

	/* FAT12 */
	unsigned UsedFatBytes = (((i->Clusters+2) * 12) + 7) / 8;
	unsigned UsedFatSectors = (UsedFatBytes + 511) / 512;


	i->ClustMap = calloc(1,MapSize);
	if (i->ClustMap == NULL)
		oops("Out Of Memory (FAT Cluster usage Map).");


	FatBuffer = malloc(FBufSect * 512);
	if (!FatBuffer)
		oops("Out of Memory (FAT Disk read buffer).");


	for (j=0; j < UsedFatSectors; j += FBufSect) {
		unsigned BufOff = 0;
		unsigned k;
		unsigned ClustInBuf;
		unsigned long ClustLeft;
		unsigned long LeftSec = UsedFatSectors - j;
		if (LeftSec > FBufSect)
			LeftSec = FBufSect;

		if ((rv = bigread(drive,i->FatStart + j,LeftSec,FatBuffer)) != 0) {
			printf("Read error: 0x%04X\n", rv);
			exit(1);
		}

		ClustInBuf = (FBufSect * 512 * 8) / 12;

		if (j == 0) {
			BufOff = 3;
			ClustInBuf -= 2;
		}
		ClustLeft = i->Clusters - ClustProcessed;
		if (ClustLeft < ClustInBuf)
			ClustInBuf = ClustLeft;

		for (k = 0; k < ClustInBuf; k++) {
			unsigned short BB;
			unsigned short e;
			BB = ((k/2) * 3) + BufOff;
			if (k & 1) { /* Odd */
				e = (FatBuffer[BB+1] >> 4) | (FatBuffer[BB+2] << 4);
			} else { /* Even */
				e = FatBuffer[BB] | ((FatBuffer[BB+1] & 0xF) << 8);
			}
			if ((e != 0)&&(e != 0xFF7)) {
				ClustOut |= ClustBit;
				ClustersWithData++;
			}
			ClustBit = ClustBit << 1;
			if (!ClustBit) {
				i->ClustMap[ClustByte] = ClustOut;
				ClustOut = 0;
				ClustBit = 1;
				ClustByte += 1;
			}
		}
		ClustProcessed += ClustInBuf;

	}
	if (ClustOut)
		i->ClustMap[ClustByte] = ClustOut;


	free(FatBuffer);

	printf("Cluster Bitmap generated.\n\t%u / %lu used / total\n",
		ClustersWithData, i->Clusters);
	return 0;

}

int fat_clustermap(int drive, struct fat_info *i)
{
	if (i->FatType == 12)
		return fat12_clustermap(drive, i);

	return fat16_clustermap(drive, i);
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