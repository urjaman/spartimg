#include <dos.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include "sputil.h"
#include "spartimg.h"
#include "fatinfo.h"
#include "blockio.h"


void usage(const char* me)  {
	printf("Usage: %s <FILE.SMG> <DRIVE:|FILE.RAW>\n", me);
	exit(1);
}

void infotext(void) {
	puts("Sparse FAT-12/16 Partition Writer; built "  __DATE__ "\n");
	puts("\tReads files written by SPREAD, and writes them to a");
	puts("\tdisk partition or raw image file\n");
	usage("SPWRITE");
}


int open_spartimg(const char *fn, struct spfmthdr * hdr) {
	int fd = copen(fn, O_RDONLY);

	cread(fd, hdr, sizeof(struct spfmthdr));

	if (hdr->magic != SPMAGIC)
		oops("Not a spartimg file (wrong magic)");

	return fd;
}

int fatinfo_spart(int fd, struct spfmthdr *hdr, struct fat_info *fi) {
	unsigned char buf[512];

	cseek(fd, hdr->entrysectors*512UL, SEEK_SET);

	if (read(fd, buf, 512) != 512)
		pexit("read sparse pbr");

	return fat_identify_buf(buf, fi);
}

#define SPMEB_MAXSECT 64

struct spm_rm {
	struct spfmtentry *eb;
	unsigned eblba;
	unsigned off;
	unsigned ebsize;
	unsigned totalsect;
};


void spm_readinit(int fd, struct spm_rm *r, struct spfmthdr *hdr)
{
	const unsigned ebpersec = 512 / sizeof(struct spfmtentry);
	unsigned bufsectors = SPMEB_MAXSECT;
	r->totalsect = hdr->entrysectors;
	r->off = 1;
	r->eblba = 0;
	if (bufsectors > r->totalsect)
		bufsectors = r->totalsect;
	r->eb = calloc(bufsectors, 512);
	r->ebsize = bufsectors * ebpersec;

	cseek(fd, 0, SEEK_SET);
	cread(fd, r->eb, bufsectors * 512);
	cseek(fd, r->totalsect * 512UL, SEEK_SET);
}

void spm_finish(struct spm_rm *r) {
	free(r->eb);
	r->eb = NULL;
	r->ebsize = 0;
}


struct spfmtentry* spm_next(int fd, struct spm_rm *r) {
	const unsigned ebpersec = 512 / sizeof(struct spfmtentry);
	unsigned secleft;
	long readpos;
	if (r->off < r->ebsize) {
		struct spfmtentry *rv;
		rv = &(r->eb[r->off]);
		if ((rv->count == 0)&&(rv->lba == 0)) {
			spm_finish(r);
			return NULL;
		}
		r->off++;
		return rv;
	}
	secleft = r->totalsect - r->eblba;
	if (secleft <= SPMEB_MAXSECT) {
		spm_finish(r);
		return NULL;
	}
	r->eblba += SPMEB_MAXSECT;
	secleft = r->totalsect - r->eblba;
	if (secleft > SPMEB_MAXSECT) {
		secleft = SPMEB_MAXSECT;
	}
	readpos = tell(fd);
	cseek(fd, r->eblba * 512UL, SEEK_SET);
	cread(fd, r->eb, secleft * 512);
	cseek(fd, readpos, SEEK_SET);
	r->off = 0;
	r->ebsize = secleft * ebpersec;
	return r->eb;
}

struct spw_tgt {
	int drive;
	int wfd;
	unsigned long fdlba;
	void *nullbuf;
	unsigned nullsz;
	unsigned long totalLBA;
	unsigned dot80;
	unsigned long nextdotsec;
};

void spw_anim(struct spw_tgt *t, unsigned long lba) {
	while (lba >= t->nextdotsec) {
		printf(".");
		t->dot80 += 1;
		t->nextdotsec = (t->totalLBA * t->dot80) / 78;
	}
}

void spw_setup(struct spw_tgt *t, int drive, int wfd, unsigned maxw, unsigned long lastLBA)
{
	t->drive = drive;
	t->wfd = wfd;
	t->fdlba = 0;
	t->totalLBA = lastLBA + 1;
	if (drive < 0) {
		t->nullbuf = calloc(maxw, 512);
		t->nullsz = maxw;
	} else {
		t->nullbuf = NULL;
		t->nullsz = 0;
	}
	t->dot80 = 0;
	t->nextdotsec = t->totalLBA / 78;
}

void spw_finalize(struct spw_tgt *t)
{
	unsigned long tgtLBA = t->totalLBA;
	if (t->drive >= 0) {
		/* Only files need to be "finalized" */
		spw_anim(t, tgtLBA);
		return;
	}
	if (t->fdlba < tgtLBA) {
		unsigned long i;
		unsigned long nlpadsec = tgtLBA - t->fdlba;
		for (i = 0; i < nlpadsec; i += t->nullsz) {
			unsigned long wsz = nlpadsec - i;
			if (wsz > t->nullsz) {
				wsz = t->nullsz;
			}
			cwrite(t->wfd, t->nullbuf, wsz*512);
			spw_anim(t, t->fdlba + i + wsz);
		}
		free(t->nullbuf);
		t->nullbuf = NULL;
	}
	cclose(t->wfd);
}

void spw_tgtwrite(struct spw_tgt *t, unsigned long lba, unsigned count, void* buffer)
{
	if (t->drive < 0) {
		if (lba < t->fdlba) {
			oops("Unpack of non-linear image to file not supported.");
		}
		if (lba > t->fdlba) {
			unsigned long i;
			unsigned long nlpadsec = lba - t->fdlba;
			for (i = 0; i < nlpadsec; i += t->nullsz) {
				unsigned long wsz = nlpadsec - i;
				if (wsz > t->nullsz) {
					wsz = t->nullsz;
				}
				cwrite(t->wfd, t->nullbuf, wsz*512);
				spw_anim(t, t->fdlba + i + wsz);
			}
		}
		cwrite(t->wfd, buffer, count*512);
	} else {
		int rv = bigwrite(t->drive, lba, count, buffer);
		if (rv != 0) {
			printf("\nTarget write error: 0x%04X\n", rv);
			exit(1);
		}
	}
	t->fdlba = lba + count;
	spw_anim(t, t->fdlba);
}

void write_spout(struct spw_tgt *t, int rfd, struct spfmthdr *hdr, unsigned iosize)
{
	void *datbuf = calloc(iosize, 512);
	struct spm_rm rmeta;
	struct spfmtentry *e;
	spm_readinit(rfd, &rmeta, hdr);
	printf("Writing image data...\n");

	while ((e = spm_next(rfd, &rmeta)) != NULL) {
		unsigned long i;
		for (i = 0; i < e->count;i += iosize) {
			unsigned sectors = iosize;
			unsigned long left = e->count - i;
			if (left < sectors)
				sectors = left;

			cread(rfd, datbuf, sectors*512);
			spw_tgtwrite(t, e->lba + i, sectors, datbuf);
		}
	}
	spw_finalize(t);
	printf("\nDone.\n");
}

int main(int argc, char**argv)
{
	int rfd;

	int wfd;
	int drive;

	struct spfmthdr imghdr;
	struct fat_info img_fi;
	struct fat_info tgt_fi;
	struct spw_tgt tgt;

	assert(sizeof(struct spfmthdr) == sizeof(struct spfmtentry));


	if (argc < 2) infotext();
	if (strcmp(argv[1], "/?")==0) infotext();

	rfd = open_spartimg(argv[1], &imghdr);

	printf("SPARTIMG detected\n");
	printf("\tLast LBA: %lu\n", imghdr.lastLBA);
	printf("\tHeader Size: %u sectors\n", imghdr.entrysectors);

	fatinfo_spart(rfd, &imghdr, &img_fi);

	printf("Image ");
	fat_info_print(&img_fi);

	if (argc < 3) {
		exit(0);
	}

	drive = driveno(argv[2]);
	if (drive < 0) {
		wfd = ccreat(argv[2]);
	} else {
		int rv;
		int ch;
		unsigned char buf[512];
		wfd = -1;
		/* TODO check target drive */
		rv = bigread(drive, 0, 1, buf);
		if (rv != 0) {
			printf("Target drive read error: 0x%04X\n", rv);
			printf("Note: the target needs to be formatted for DOS to allow access.\n");
			exit(1);
		}
		fat_identify_buf(buf, &tgt_fi);
		printf("Target ");
		fat_info_print(&tgt_fi);
		if (tgt_fi.FatType != img_fi.FatType)
			oops("Image vs Target FAT type mismatch; refusing write.");
		if (img_fi.LastLBA > tgt_fi.LastLBA)
			oops("Target partition too small for image.");
		if (img_fi.LastLBA < tgt_fi.LastLBA)
			printf("Note: target bigger than image.\n");
		if (img_fi.SPT != tgt_fi.SPT)
			printf("Note: Sectors/Track mismatch (could cause boot problems).\n");
		printf("Write image (overwrite target)? (Y/N)");
		do {
			ch = getch();
			ch = toupper(ch);
			if ((ch == 'N')||(ch == 'Q')) {
				printf("\nOkay, quitting.");
				exit(0);
			}
		} while (ch != 'Y');
		printf("\n");
	}

	spw_setup(&tgt, drive, wfd, fat_iosize(&img_fi), imghdr.lastLBA);
	write_spout(&tgt, rfd, &imghdr, fat_iosize(&img_fi));
	cclose(rfd);

	if ((drive >= 0)&&(img_fi.FatType == 16))
		printf("Please reboot before accessing %s\n", strupr(argv[2]) );

	return 0;
}
