CFLAGS := -Wall -std=c11 -O2 -D_LARGEFILE64_SOURCE -I../include

all: libbius.a

libbius.a: libbius.o
	ar -Drc $@ $^
	ranlib -D $@

clean:
	rm -rf *.o *.a

.PHONY: all clean
