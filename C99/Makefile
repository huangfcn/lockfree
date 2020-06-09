CC     := gcc
CFLAGS := $(CFLAGS) -Wall -O3 -march=native

all : ffbench

ffbench : main.c mirrorbuf.c lffifo.h rbq.h magicq.h
	$(CC) $(CFLAGS) main.c mirrorbuf.c -lpthread -o ffbench

clean :
	rm -f ffbench mirrorbuf.o
