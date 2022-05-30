#ifndef _SPARTIMG_H
#define _SPARTIMG_H

#define SPMAGIC 0x53D0

struct spfmthdr {
	unsigned short magic;
	unsigned short entrysectors;
	unsigned long lastLBA;
};

struct spfmtentry {
	unsigned long lba;
	unsigned long count;
};

#endif