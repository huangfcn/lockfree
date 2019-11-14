1, lock free single producer single consumer queue based on ring buffer (magic queue)

	=============================== API ===============================
	#include "magicq.h"
  
	// initialize magicq (size will be (1 << order))
	bool magicq_init(magicq_t * cb, int order);

	// free magicq
	void magicq_free(magicq_t * cb);

	// push/pop/full/empty/size operations
	bool   magicq_push(magicq_t * cb, void * data);
	void * magicq_pop (magicq_t * cb);
	bool   magicq_full (const magicq_t * cb);
	bool   magicq_empty(const magicq_t * cb);
	size_t magicq_size (const magicq_t * cb);

2, lock free multiple producers multiple consumers queue based on ring buffer (RBQ)

	=============================== API ===============================
	#include "rbq.h"
  
	// initialize rbq (size will be (1 << order))
	bool bool rbq_init(rbq_t * rbq, int order);

	// free ring buffer queue
	void rbq_free(rbq_t * rbq);

	// push/pop/full/empty/size operations
	bool   rbq_push(rbq_t * rbq, void * data);
	void * rbq_pop (rbq_t * rbq);
	bool   rbq_full (const rbq_t * rbq);
	bool   rbq_empty(const rbq_t * rbq);
	size_t rbq_size (const rbq_t * rbq);

	// push/pop using the method in "Yet another implementation of a lock-free circular array queue" by Faustino Frechilla"
	bool   rbq_push2(rbq_t * rbq, void * data);
	void * rbq_pop2 (rbq_t * rbq);

	// push/pop if only one producer & one consumer
	bool   rbq_pushspsc(rbq_t * rbq, void * data);
	void * rbq_popspsc (rbq_t * rbq);

3, lock free multiple producers multiple consumers queue based on single list (Michael Scott)

	=============================== API ===============================
	#include "lffifo.h"

	// initialize lock-free msq (parameter order not used)
	bool lffifo_init(lffifo_t * fifo, int order);

	// free msq
	void lffifo_free(lffifo_t * fifo);

	// push/pop/full/empty/size operations
	bool   lffifo_push(lffifo_t * fifo, void * value);
	void * lffifo_pop (lffifo_t * fifo);
	bool   lffifo_full (const lffifo_t * fifo);
	bool   lffifo_empty(const lffifo_t * fifo);
	size_t lffifo_size (const lffifo_t * fifo);

4, lock free multiple producers multiple consumers stack based on single list

	=============================== API ===============================
	#include "lffifo.h"

	// initialize lock-free stack (parameter order not used)
	bool lfstack_init(lfstack_t * stack, int order);

	// free stack
	void lfstack_free(lfstack_t * stack);

	// push/pop/full/empty/size operations
	bool   lfstack_push(lfstack_t * stack, void * value);
	void * lfstack_pop (lfstack_t * stack);
	bool   lfstack_full (const lfstack_t * stack);
	bool   lfstack_empty(const lfstack_t * stack);
	size_t lfstack_size (const lfstack_t * stack);

5, lock free memory management based on fixed size memory blocks
   
	All memory blocks in same size are managed in a stack using single linked list. 
	Allocate & free memory operation only require one push/pop operation of stack.

	fixed size memory blocks routines (startup, cleanup, alloc, free)       
		create a free list with fixed size memory block                      
		allocation of a memory block becomes getting a block from free list  
		free       of a memory block becomes putting a block into free list  
																		   
	general memory malloc/free/realloc/calloc through fixed size memory blocks             
		maintain fixed size memory blocks with different size                
		allocation becomes getting a block from corresponding free list      
		free       becomes putting a block into corresponding free list      
																		   
		 1 bytes -   240   bytes, maintained in blocks aligned to  16 bytes
	   241 bytes -  3,840  bytes, maintained in blocks aligned to 256 bytes
	 3,841 bytes -  61,440 bytes, maintained in blocks aligned to  4k bytes
	61,441 bytes - 524,288 bytes, maintained in blocks aligned to 64k bytes
	   otherwise                , call system memory management calls

	=============================== API ===============================
	#include "fixedSizeMemoryLF.h"

	/* ============================================================ *
	 * Memory management based on fixed size memory block           *
	 * ============================================================ */
	// initialzie library
	int  mmFixedSizeMemoryStartup();

	// de-initialize library
	void mmFixedSizeMemoryCleanup();

	// allocate memory
	void * mmFixedSizeMemoryAlloc(size_t nsize);

	// free memory block
	void   mmFixedSizeMemoryFree (void * buf, size_t size);

	/* ============================================================== *
	 * GENERAL PURPOSE MEMORY MANAGEMENT (malloc/free/realloc/calloc) *
	 * ============================================================== */
	void * slab_malloc (size_t size);
	void   slab_free   (void * _pblk);
	void * slab_realloc(void * pmem, size_t size);
	void * slab_calloc (size_t blksize, size_t numblk);
	///////////////////////////////////////////////////////////////////////////////

5, performance (main.cpp)
	
	running on i7-8750H 2.2G, compiled with Visual Studio 2017.

	-------- Lock free ring buffer (SPSC) bench ----------
	threads count:   1       totSum = 0, perf (in us per pop/push):  0.089000

	-------- Lock free ring buffer (MPMC) bench ----------
	threads count:   1       totSum = 0, perf (in us per pop/push):  0.176400
	threads count:   2       totSum = 0, perf (in us per pop/push):  0.222120
	threads count:   3       totSum = 0, perf (in us per pop/push):  0.174881
	threads count:   4       totSum = 0, perf (in us per pop/push):  0.141935
	threads count:   5       totSum = 0, perf (in us per pop/push):  0.140524
	threads count:   6       totSum = 0, perf (in us per pop/push):  0.140971
	threads count:   7       totSum = 0, perf (in us per pop/push):  0.163714
	threads count:   8       totSum = 0, perf (in us per pop/push):  0.140792

	-------- Lock free queue (MSQ) bench ----------
	threads count:   1       totSum = 0, perf (in us per pop/push):  0.319150
	threads count:   2       totSum = 0, perf (in us per pop/push):  0.460602
	threads count:   3       totSum = 0, perf (in us per pop/push):  0.626329
	threads count:   4       totSum = 0, perf (in us per pop/push):  0.705676
	threads count:   5       totSum = 0, perf (in us per pop/push):  0.688684
	threads count:   6       totSum = 0, perf (in us per pop/push):  0.621664
	threads count:   7       totSum = 0, perf (in us per pop/push):  0.684918
	threads count:   8       totSum = 0, perf (in us per pop/push):  0.672412

	-------- Lock free stack bench ----------
	threads count:   1       totSum = 0, perf (in us per pop/push):  0.293375
	threads count:   2       totSum = 0, perf (in us per pop/push):  0.494783
	threads count:   3       totSum = 0, perf (in us per pop/push):  0.628804
	threads count:   4       totSum = 0, perf (in us per pop/push):  0.727656
	threads count:   5       totSum = 0, perf (in us per pop/push):  0.674413
	threads count:   6       totSum = 0, perf (in us per pop/push):  0.676848
	threads count:   7       totSum = 0, perf (in us per pop/push):  0.729400
	threads count:   8       totSum = 0, perf (in us per pop/push):  0.703722
