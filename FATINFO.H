#ifndef _FATINFO_H
#define _FATINFO_H


struct fat_info {
	unsigned long FatStart;
	unsigned long DataStart;
	unsigned long LastLBA;
	unsigned long Clusters;
	unsigned short SecPerClust;
	unsigned FatType;
	unsigned SPT;
	unsigned char *ClustMap;
};

int fat_identify_buf(void * pbrbuf, struct fat_info* i);
int fat_identify(int drive, struct fat_info* i);
int fat_clustermap(int drive, struct fat_info *i);
void fat_info_print(struct fat_info *fi);
unsigned fat_iosize(struct fat_info *fi);


#endif