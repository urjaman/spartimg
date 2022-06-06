#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "sputil.h"
#include "fatinfo.h"
#include "robustrd.h"
#include "fatinfrr.h"
#include "blockio.h"

int fat_identify(struct rr_state *rr, struct fat_info *fi) {
	struct fat_pbr pbr;
	int rv;

	assert(sizeof(pbr) == 512);
	rr_read(rr, 0, 1, &pbr);
	return fat_identify_buf(&pbr, fi);
}

static int fat16_clustermap(struct rr_state *rr, struct fat_info *i) {
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

	rr_fat_meta(rr,i, UsedFatSectors, FBufSect);

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

		rr_read(rr,i->FatStart + j,LeftSec,FatBuffer);

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

static int fat12_clustermap(struct rr_state *rr, struct fat_info *i) {
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

	rr_fat_meta(rr,i, UsedFatSectors, FBufSect);


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

		rr_read(rr,i->FatStart + j,LeftSec,FatBuffer);

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

int fat_clustermap(struct rr_state *rr, struct fat_info *i)
{
	if (i->FatType == 12)
		return fat12_clustermap(rr, i);

	return fat16_clustermap(rr, i);
}
