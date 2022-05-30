#ifndef _BLOCKIO_H
#define _BLOCKIO_H

extern int bigread(int drive, unsigned long lba, unsigned short cnt, void *far buffer);
extern int bigwrite(int drive, unsigned long lba, unsigned short cnt,void *far buffer);

#endif
