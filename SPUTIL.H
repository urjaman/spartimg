#ifndef _SPUTIL_H
#define _SPUTIL_H
void pexit(const char* doing);
void oops(const char* problem);
int driveno(const char* drivestr);

/* File I/O helpers */
int copen(const char *filename, int oflags);
int ccreat(const char *filename);
void cseek(int fd, long offset, int mode);
void cread(int fd, void*buf, unsigned len);
void cwrite(int fd, void*buf, unsigned len);
void cclose(int fd);



#endif