#!/usr/bin/env python3
import os
import sys
import struct
import stat
import argparse
import shutil
import fcntl

BLKGETSIZE=0x1260
BLKGETSIZE64=0x80081272

def ioctl_read_uint32(fd, req):
    buf = bytearray(4)
    fcntl.ioctl(fd, req, buf)
    return struct.unpack('I',buf)[0]

def ioctl_read_uint64(fd, req):
    buf = bytearray(8)
    fcntl.ioctl(fd, req, buf)
    return struct.unpack('L',buf)[0]

def device_size(fd):
    try:
        s = ioctl_read_uint64(fd, BLKGETSIZE64)
    except IOError as e:
        if e.errno == 25:
            s = ioctl_read_uint32(fd, BLKGETSIZE) * 512
        else: raise
    return s

parser = argparse.ArgumentParser()
parser.add_argument("smg", metavar="file.smg", help="Sparse Image File")
parser.add_argument("target", default=None, nargs='?', help="Unpacked Image or Device")
parser.add_argument("-s","--sparse", action='store_true', help="Write only the sectors from the sparse image.")
parser.add_argument("-a","--all", action='store_true', help="Write every sector (erases disk / creates file the size of the source)")
args = parser.parse_args()

hdrfmt = "<HHL"
hdrsz = struct.calcsize(hdrfmt)

src = open(args.smg, "rb")
hdr = src.read(hdrsz)
hdr = struct.unpack(hdrfmt, hdr)
if hdr[0] != 0x53D0:
    sys.exit("Not an SMG file (magic mismatch)")

print("SMG Header:")
print("\tSectors of Header:", hdr[1])
print("\tLast LBA:", hdr[2])

imglbacnt = hdr[2] + 1

print("Reading full header..")
lbalenfmt = "<LL"
lbalensz = ( hdr[1] * 512 ) - hdrsz
lbalens = src.read(lbalensz)

sectorcnt = 0
for lba, len in struct.iter_unpack(lbalenfmt, lbalens):
    if lba == 0 and len == 0:
        break
    sectorcnt += len

print("\tImage contains", sectorcnt, "sectors")

if args.target is not None:
    try:
        target = open(args.target, "r+b")
        s = os.stat(target.fileno())
        if stat.S_ISBLK(s.st_mode):
            sparse = True
            device = True
        else:
            sys.exit("Unexpected target (exists and is not a block device), Quit.")

        devsz = device_size(target.fileno())
    except FileNotFoundError:
        sparse = False
        device = False
        target = open(args.target, "wb")

    if args.sparse:
        sparse = True

    if args.all:
        sparse = False

    if sparse:
        writesectors = sectorcnt
    else:
        writesectors = hdr[2] + 1


    dotcols = shutil.get_terminal_size().columns - 2

    dot80 = 0
    nextdot = writesectors // dotcols
    writtensect = 0

    chunksz = 32*1024 // 512
    tgtpos = 0

    def dots(pos):
        global dot80
        global nextdot
        while pos >= nextdot:
            print(".", end='')
            sys.stdout.flush()
            dot80 += 1
            nextdot = dot80*writesectors // dotcols

    if device:
        if imglbacnt*512 > devsz:
            sys.exit("Target device too small for image.")
        if imglbacnt*512 < devsz:
            print("Note: target device bigger than image.")
        print("About to overwrite device", args.target)
        r = input("Continue (Y/N)? ")
        if r.upper()[0] != 'Y':
            sys.exit("Quit.")


    for lba, len in struct.iter_unpack(lbalenfmt, lbalens):
        if lba == 0 and len == 0:
            break
        if tgtpos != lba:
            if sparse:
                target.seek(lba*512, 0)
                tgtpos = lba
            else:
                while tgtpos < lba:
                    target.write(b'\0' * 512)
                    tgtpos += 1
                    dots(tgtpos)

        while len > 0:
            sz = len
            if sz > chunksz:
                sz = chunksz
            buf = src.read(sz*512)
            target.write(buf)
            tgtpos += sz
            writtensect += sz
            len -= sz
            if sparse:
                dots(writtensect)
            else:
                dots(tgtpos)

    if sparse and not device and tgtpos < imglbacnt:
        target.truncate(imglbacnt*512)

    target.close()
    print("\nDone")
