#include <atomic>
#ifndef __LOCKFREE_MAGICQ_SPSC_H__
#define __LOCKFREE_MAGICQ_SPSC_H__

template <typename T> class magicq
{
protected:
	std::atomic<uint64_t> nobj;

	uint64_t head;
	uint64_t tail;

	size_t   size;
	T *      data;
	
	magicq() { ; };
public:
	magicq(int order) : nobj(0) {
		size = (1ULL << order);
		data = new T[size];

		head = 0;
		tail = 0;
	};

	virtual ~magicq() {
		delete[] data;
	};

	inline bool isfull() { return (nobj.load(std::memory_order_acquire) == size); }
	inline bool isempty() { return (nobj.load(std::memory_order_acquire) == 0); }
	inline size_t getsize() { return nobj.load(std::memory_order_acquire); };

	/* push @ single producer single consumer */
	inline bool push(const T & object)
	{
		if (isfull()) { return false; }

		/* copy object (move) */
		data[tail] = object;
		tail = (tail + 1) & (size - 1);
		nobj.fetch_add(1);

		return true;
	}

	/* pop @ single producer single consumer */
	inline bool pop(T & object)
	{
		if (isempty()) { return false; }

		/* copy object, move should be used here */
		object = data[head];
		head = (head + 1) & (size - 1);
		nobj.fetch_sub(1);

		return (true);
	};

	/* pop @ single producer single consumer */
	inline T pop()
	{
		T object(0); pop(object); return object;
	};
};

#endif