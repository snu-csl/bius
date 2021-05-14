CFLAGS := -Wall -std=c11

all: test blktest test-passthrough

test: test.c command.h utils.h

blktest: blktest.c

test-passthrough: test-passthrough.c command.h utils.h

clean:
	rm -rf test test-passthrough blktest *.o

.PHONY: all clean
