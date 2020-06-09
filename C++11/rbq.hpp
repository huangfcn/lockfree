#include <atomic>

#ifndef __LOCKFREE_RBQ_MPMC_H__
#define __LOCKFREE_RBQ_MPMC_H__

#ifdef _WIN32
#include <Windows.h>
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

constexpr uint32_t STATUS_EMPT = 0;
constexpr uint32_t STATUS_FILL = 1;
constexpr uint32_t STATUS_READ = 2;
constexpr uint32_t STATUS_FULL = 3;

template <typename T> class rbqueue
{
	struct alignas(64) rbnode
	{
		std::atomic<uint32_t> status; T object;
		rbnode() { status.store(STATUS_EMPT); };
	};

protected:
	alignas(64) std::atomic<uint64_t> head;
	alignas(64) std::atomic<uint64_t> tail;

	size_t   size;
	rbnode * data;
	
	rbqueue() { ; };
public:
	rbqueue(int order) {
		size = (1ULL << order);
		data = new rbnode[size];

		head.store(0);
		tail.store(0);
	};

	virtual ~rbqueue() {
		delete[] data;
	};

	inline bool isfull() {
		uint64_t _tail = tail.load(std::memory_order_relaxed);
		uint64_t _head = head.load(std::memory_order_relaxed);

		return (_tail >= (_head + size));
	};

	inline bool isempty() {
		uint64_t _tail = tail.load(std::memory_order_relaxed);
		uint64_t _head = head.load(std::memory_order_relaxed);

		return (_head >= _tail);
	};

	inline size_t getsize() { return size; };

	inline bool push(const T & object)
	{
		uint64_t currReadIndexA, currWriteIndex, nextWriteIndex;

		// do 
		{
			// check if queue full
			currWriteIndex = tail.load(std::memory_order_relaxed);
			currReadIndexA = head.load(std::memory_order_relaxed);
			if (currWriteIndex >= (currReadIndexA + size)) { return false; }

			// now perfrom the FAA operation on the write index. 
			// the Space @ currWriteIndex will be reserved for us.
			nextWriteIndex = tail.fetch_add(1);
			currWriteIndex = nextWriteIndex & (size - 1);
		}
		// while (1);

		// We know now that space @ currWriteIndex is reserved for us.
		// In case of slow reader, we use CAS to ensure a correct data swap
		rbnode * pnode = data + currWriteIndex; uint32_t S0 = STATUS_EMPT;
		while (!pnode->status.compare_exchange_weak(S0, STATUS_FILL))
		{
			S0 = STATUS_EMPT;
			usleep((currWriteIndex & 1) + 1);
		}

		/* fill - exclusive */
		pnode->object = object;

		/* done - update status */
		pnode->status.store(STATUS_FULL, std::memory_order_release);

		return true;
	};

	/* pop @ mutiple consumers */
	inline bool pop(T & object)
	{
		uint64_t currWritIndex, currReadIndex, nextReadIndex;

		// do
		{
			// check if queue empty
			currReadIndex = head.load(std::memory_order_relaxed);
			currWritIndex = tail.load(std::memory_order_relaxed);
			if (currReadIndex >= currWritIndex) { return false; }

			// now perfrom the FAA operation on the read index. 
			// the Space @ currReadIndex will be reserved for us.
			nextReadIndex = head.fetch_add(1);
			currReadIndex = nextReadIndex & (size - 1);
		}
		// while(1);

		// We know now that space @ currReadIndex is reserved for us.
		// In case of slow writer, we use CAS to ensure a correct data swap
		rbnode * pnode = data + currReadIndex; uint32_t S0 = STATUS_FULL;
		while (!pnode->status.compare_exchange_weak(S0, STATUS_READ))
		{
			S0 = STATUS_FULL;
			usleep((currReadIndex & 1) + 1);
		}

		/* read - exclusive */
		object = pnode->object;

		/* done - update status */
		pnode->status.store(STATUS_EMPT, std::memory_order_release);

		/* return - data */
		return true;
	};

	/* pop @ mutiple consumers */
	inline T pop()
	{
		uint64_t currWritIndex, currReadIndex, nextReadIndex;

		// do
		{
			// check if queue empty
			currReadIndex = head.load(std::memory_order_relaxed);
			currWritIndex = tail.load(std::memory_order_relaxed);
			if (currReadIndex >= currWritIndex) {
				return (T(0));
			}

			// now perfrom the FAA operation on the read index. 
			// the Space @ currReadIndex will be reserved for us.
			nextReadIndex = head.fetch_add(1);
			currReadIndex = nextReadIndex & (size - 1);
		}
		// while(1);

		// We know now that space @ currReadIndex is reserved for us.
		// In case of slow writer, we use CAS to ensure a correct data swap
		rbnode* pnode = data + currReadIndex; uint32_t S0 = STATUS_FULL;
		while (!pnode->status.compare_exchange_weak(S0, STATUS_READ))
		{
			S0 = STATUS_FULL;
			usleep((currReadIndex & 1) + 1);
		}

		/* read - exclusive */
		T object(pnode->object);

		/* done - update status */
		pnode->status.store(STATUS_EMPT, std::memory_order_release);

		/* return - data */
		return object;
	};

	/* push @ single producer single consumer */
	inline bool pushspsc(const T & object)
	{
		uint64_t currWriteIndex = tail.load(std::memory_order_relaxed);
		uint64_t currReadIndexA = head.load(std::memory_order_relaxed);
		if (currWriteIndex >= (currReadIndexA + size)) { return false; }

		data[currWriteIndex & (size - 1)].object = object;
		tail.store(currWriteIndex + 1, std::memory_order_relaxed);
		
		return true;
	}

	/* pop @ single producer single consumer */
	inline bool popspsc(T & object)
	{
		uint64_t currReadIndex = head.load(std::memory_order_relaxed);
		uint64_t currWritIndex = tail.load(std::memory_order_relaxed);
		if (currReadIndex >= currWritIndex) { return false; };

		object = data[currReadIndex & (size - 1)].object;
		head.store(currReadIndex + 1, std::memory_order_relaxed);
		
		return (true);
	};

	/* pop @ single producer single consumer */
	inline T popspsc()
	{
		uint64_t currReadIndex = head.load(std::memory_order_relaxed);
		uint64_t currWritIndex = tail.load(std::memory_order_relaxed);
		if (currReadIndex >= currWritIndex) { return false; };

		T object(data[currReadIndex & (size - 1)].object);
		head.store(currReadIndex + 1, std::memory_order_relaxed);

		return (object);
	};
};

#endif