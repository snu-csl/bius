CFLAGS := -Wall -std=c11 -O2 -D_LARGEFILE64_SOURCE

LDFLAGS := -lpthread

EXECUTABLES := blktest ramdisk passthrough zoned-ramdisk zoned-passthrough

all: $(EXECUTABLES)

blktest: blktest.c

libbius.o: libbius.c libbius.h command.h utils.h ../kernel/config.h ../kernel/request_type.h

ramdisk: ramdisk.c libbius.o

passthrough: passthrough.c libbius.o

zoned-ramdisk: zoned-ramdisk.c libbius.o

zoned-passthrough: zoned-passthrough.c libbius.o

clean:
	rm -rf $(EXECUTABLES) *.o

.PHONY: all clean
