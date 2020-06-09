CC     := g++
CFLAGS := $(CFLAGS) -Wall -O3 -march=native -faligned-new -std=c++17

all : ffbench

ffbench : main.cpp lffifo.hpp rbq.hpp
	$(CC) $(CFLAGS) -g -O0 main.cpp -lpthread -latomic -o ffbench

clean :
	rm -f ffbench mirrorbuf.o
