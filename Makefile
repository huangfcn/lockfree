CC     := gcc
CFLAGS := $(CFLAGS) -Wall -O3 -march=native

all : ffbench

ffbench : main.cpp mirrorbuf.c fixedSizeMemoryLF.c
	$(CC) $(CFLAGS) main.c mirrorbuf.c fixedSizeMemoryLF.c -lpthread  -o ffbench

clean :
	rm -f ffbench mirrorbuf.o fixedSizeMemoryLF.o