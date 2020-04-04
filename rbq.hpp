#include <atomic>
#include <climits>

#ifndef __LOCKFREE_RBQ_MPMC_H__
#define __LOCKFREE_RBQ_MPMC_H__

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

	static inline void usleep(__int64 usec)
	{
		HANDLE timer;
		LARGE_INTEGER ft;

		ft.QuadPart = -(10 * usec);

		timer = CreateWaitableTimer(NULL, TRUE, NULL);
		SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
		WaitForSingleObject(timer, INFINITE);
		CloseHandle(timer);
	}

	static inline int sched_yield()
	{
		SwitchToThread();
		return (0);
	}

#ifdef __cplusplus
};
#endif

#else

#include <sched.h>
#include <unistd.h>

#endif  // _WIN32

struct rbq_swap_t
{
	std::atomic<uint64_t> addr;
	uint64_t _d[7];

	rbq_swap_t() { addr.store(0); };
};

typedef struct _rbq_t
{
	uint64_t size;
	uint64_t vsiz;

	std::atomic<uint64_t> head;
	uint64_t _d1[7];

	std::atomic<uint64_t> tail;
	uint64_t _d2[7];

	rbq_swap_t* data;
} rbq_t;

static inline bool rbq_init(rbq_t* rbq, int order)
{
	rbq->size = (1ULL << order);
	rbq->vsiz = (2ULL << order);

	rbq->head.store(0);
	rbq->tail.store(0);

	rbq->data = new rbq_swap_t[rbq->size];

	return (rbq->data != NULL);
};

static inline void rbq_free(rbq_t* rbq)
{
	delete[] rbq->data;
}

static inline bool rbq_full(const rbq_t* rbq)
{
	uint64_t tail = rbq->tail.load(std::memory_order_acquire);
	uint64_t head = rbq->head.load(std::memory_order_acquire);
	uint64_t size = rbq->size;

	return (tail >= (head + size));
}

static inline bool rbq_empty(const rbq_t* rbq)
{
	uint64_t tail = rbq->tail.load(std::memory_order_acquire);
	uint64_t head = rbq->head.load(std::memory_order_acquire);

	return (head >= tail);
}

static inline size_t rbq_size(const rbq_t* rbq)
{
	uint64_t tail = rbq->tail.load(std::memory_order_acquire);
	uint64_t head = rbq->head.load(std::memory_order_acquire);

	return ((head >= tail) ? (0) : (tail - head));
}

/* push @ mutiple producers */
static inline bool rbq_push(rbq_t* rbq, void* data)
{
	uint64_t currReadIndex;
	uint64_t currWriteIndex;
	uint64_t nextWriteIndex;

	uint64_t d64 = ((uint64_t)data);
	uint64_t _null = 0;

	// do 
	{
		// check if queue full
		currWriteIndex = rbq->tail.load(std::memory_order_acquire);
		currReadIndex = rbq->head.load(std::memory_order_acquire);
		if (currWriteIndex >= (currReadIndex + rbq->size))
		{
			return false;
		}

		// now perfrom the FAA operation on the write index. 
		// the Space @ currWriteIndex will be reserved for us.
		nextWriteIndex = rbq->tail.fetch_add(1);
		currWriteIndex = (nextWriteIndex) & (rbq->size - 1);

		// now try to perfrom the CAS operation on the write index. 
		// If we succeed the space @ currWriteIndex will be reserved for us
		// nextWriteIndex = (currWriteIndex + 1) & (rbq->vsiz - 1);
		// if (CAS(&(rbq->tail), currWriteIndex, nextWriteIndex))
		// {
		//    break;
		// }
	}
	// while (1);

	// We know now that space @ currWriteIndex is reserved for us.
	// In case of slow reader, we use CAS to ensure a correct data swap
	while (!((rbq->data)[currWriteIndex].addr.compare_exchange_weak(_null, d64)))
	{
		usleep((currWriteIndex & 1) + 1); _null = 0;
	}

	return true;
};

/* pop @ mutiple consumers */
static inline void* rbq_pop(rbq_t* rbq)
{
	uint64_t currWriteIndex;
	uint64_t currReadIndex;
	uint64_t nextReadIndex;
	uint64_t currReadIndexA;

	// do
	{
		// check if queue empty
		currReadIndexA = rbq->head.load(std::memory_order_acquire);
		currWriteIndex = rbq->tail.load(std::memory_order_acquire);
		if (currReadIndexA >= currWriteIndex)
		{
			return NULL;
		}

		// now perfrom the FAA operation on the read index. 
		// the Space @ currReadIndex will be reserved for us.
		nextReadIndex = rbq->head.fetch_add(1);
		currReadIndex = (nextReadIndex) & (rbq->size - 1);
		// if (currReadIndex == currWriteIndex)
		// {
		//    return (NULL);
		// }

		// now try to perfrom the CAS operation on data @ currReadIndex. 
		// If we succeed the space @ currReadIndex will be reserved for us
		// nextReadIndex = (currReadIndex + 1) & (rbq->vsiz - 1);
		// if (CAS(&(rbq->head), currReadIndex, nextReadIndex))
		// {
		//    break;
		// }
	}
	// while(1);

	// We know now that space @ currReadIndex is reserved for us.
	// In case of slow writer, we use CAS to ensure a correct data swap
	while (1)
	{
		uint64_t d64 = rbq->data[currReadIndex].addr.load(std::memory_order_acquire);
		if (d64 && (rbq->data[currReadIndex].addr.compare_exchange_weak(d64, NULL)))
		{
			return ((void*)d64);
		}

		// sched_yield();
		usleep((currReadIndex & 1) + 1);
	}

	return (NULL);
};

#endif