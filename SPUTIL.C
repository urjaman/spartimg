#include <stdlib.h>
#include <io.h>
#include <dos.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "sputil.h"

void pexit(const char* doing) {
	perror(doing);
	exit(1);
}

void oops(const char* problem) {
	puts(problem);
	exit(1);
}

int driveno(const char *drivestr) {
	int drive;

	if (strlen(drivestr) > 2)
		return -1;

	if (drivestr[1] != ':')
		return -1;

	drive = toupper(drivestr[0]) - 'A';
	if ((drive < 0) || (drive > 25)) return -1;

	return drive;
}

/* C for "checked", that is auto-bail. */

int copen(const char*filename, int oflags) {
	int fd = _open(filename, oflags);
	if (fd == -1)
		pexit("open");
	return fd;
}

int ccreat(const char*filename) {
	int fd = _creat(filename, FA_ARCH);
	if (fd == -1)
		pexit("creat");
	return fd;
}

void cseek(int fd, long offset, int mode) {
	if (lseek(fd, offset, mode) == -1L)
		pexit("lseek");
}

void cread(int fd, void*buf, unsigned len) {
	unsigned char *cbuf = buf;
	unsigned char *tbuf = cbuf + len;
	while (cbuf < tbuf) {
		int rv = read(fd, cbuf, tbuf-cbuf);
		if (rv == -1)
			pexit("read");
		if (rv == 0)
			oops("read past eof");
		cbuf += rv;
	}
}

void cwrite(int fd, void*buf, unsigned len) {
	unsigned char *cbuf = buf;
	unsigned char *tbuf = cbuf + len;
	while (cbuf < tbuf) {
		int rv = write(fd, cbuf, tbuf-cbuf);
		if (rv == -1)
			pexit("write");
		cbuf += rv;
	}
}

void cclose(int fd) {
	if (_close(fd) != 0)
		perror("close");
}

void* cmalloc(unsigned len) {
	void *p = malloc(len);
	if (!p) oops("Out of memory.");
	return p;
}

void* ccalloc(unsigned count, unsigned sizeeach) {
	void *p = calloc(count,sizeeach);
	if (!p) oops("Out of memory.");
	return p;
}