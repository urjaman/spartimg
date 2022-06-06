#ifndef _FATINFO_H
#define _FATINFO_H

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


struct fat_info {
	unsigned long FatStart;
        unsigned long DirStart;
	unsigned long DataStart;
	unsigned long LastLBA;
	unsigned long Clusters;
	unsigned short SecPerClust;
	unsigned FatType;
	unsigned SPT;
	unsigned char *ClustMap;
	unsigned long FatSectors; /* in one FAT */
	unsigned char FatCnt;

};

int fat_identify_buf(void * pbrbuf, struct fat_info* i);
void fat_info_print(struct fat_info *fi);
unsigned fat_iosize(struct fat_info *fi);


#endif