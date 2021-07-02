CFLAGS := -Wall -std=c11 -O2 -D_LARGEFILE64_SOURCE -I../include

LDFLAGS := -lpthread

LIBRARY := ../library/libbius.a

EXECUTABLES := blktest ramdisk passthrough zoned-ramdisk zoned-passthrough

all: $(EXECUTABLES)

blktest: blktest.c

ramdisk: ramdisk.c $(LIBRARY)

passthrough: passthrough.c $(LIBRARY)

zoned-ramdisk: zoned-ramdisk.c $(LIBRARY)

zoned-passthrough: zoned-passthrough.c $(LIBRARY)

clean:
	rm -rf $(EXECUTABLES) *.o

.PHONY: all clean
