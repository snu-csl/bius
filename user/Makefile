CFLAGS := -Wall -std=c11 -O2 -D_LARGEFILE64_SOURCE

LDFLAGS := -lpthread

EXECUTABLES := blktest ramdisk passthrough zoned-ramdisk zoned-passthrough

all: $(EXECUTABLES)

blktest: blktest.c

libbuse.o: libbuse.c libbuse.h command.h utils.h

ramdisk: ramdisk.c libbuse.o

passthrough: passthrough.c libbuse.o

zoned-ramdisk: zoned-ramdisk.c libbuse.o

zoned-passthrough: zoned-passthrough.c libbuse.o

clean:
	rm -rf $(EXECUTABLES) *.o

.PHONY: all clean
