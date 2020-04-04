#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <stdbool.h>

#include "lf_struct.hpp"
#include "fixedSizeMemoryLF.hpp"

#ifndef __LOCKFREE_LFFIFO_H__
#define __LOCKFREE_LFFIFO_H__

////////////////////////////////////////////////////////////////////////////
// LIFO / STACK                                                           //
////////////////////////////////////////////////////////////////////////////
static inline bool lfstack_init_internal(std::atomic<lfstack_head_t>* head)
{
	head->store(lfstack_head_t());
	return true;
}

static inline bool lfstack_push_internal(
	std::atomic<lfstack_head_t>* head, lf_pointer_t* pt
)
{
	lfstack_head_t orig;
	lfstack_head_t next;

	do {
		orig = head->load(std::memory_order_acquire);

		next.aba_ = orig.aba_ + 1;
		next.node = pt;

		/* make a link */
		pt->node = orig.node;
		pt->aba_ = next.aba_;
	} while (!head->compare_exchange_weak(orig, next));

	return (true);
}

static inline lf_pointer_t* lfstack_pop_internal(
	std::atomic<lfstack_head_t>* head
)
{
	lfstack_head_t orig;
	lfstack_head_t next;

	lf_pointer_t * node;

	do {
		orig = head->load(std::memory_order_acquire);

		node = orig.node;
		if (node == NULL) {
			return NULL;
		}

		/* load (node) with acquire */
		next.aba_ = orig.aba_ + 1;
		next.node = node->node;

	} while (!head->compare_exchange_weak(orig, next));

	return (node);
}

static inline bool lfstack_init(lfstack_t* stack)
{
	lfstack_init_internal(&(stack->worklist));

	stack->size.store(0);
	return true;
}

static inline size_t lfstack_size(const lfstack_t* stack)
{
	return (stack->size.load(std::memory_order_acquire));
}

static inline bool lfstack_empty(const lfstack_t* stack)
{
	return (stack->size.load(std::memory_order_acquire) == 0);
}

static inline bool lfstack_full(const lfstack_t* stack)
{
	return (false);
}

static inline bool lfstack_push(lfstack_t* stack, void * value)
{
	//////////////////////////////////////
	// allocate a new node              //
	//////////////////////////////////////
	lfstack_node_t* node = (lfstack_node_t*)mmFixedSizeMemoryAlloc(
		sizeof(lfstack_node_t)
	);
	if (node == NULL){return false;}

	/* write (node) with release */
	node->valu = (uint64_t)value;
	//////////////////////////////////////

	lfstack_push_internal(
		&(stack->worklist),
		(lf_pointer_t*)(node)
	);

	/* increament counter */
	stack->size.fetch_add(1);

	return (true);
}

static inline void* lfstack_pop(lfstack_t* stack)
{
	lfstack_node_t* node = (lfstack_node_t*)lfstack_pop_internal(
		&(stack->worklist)
	);
	if (node == NULL){return NULL;}

	/* load (node) with acquire */
	uint64_t value = node->valu;

	/* free the node */
	mmFixedSizeMemoryFree(node, sizeof(lfstack_node_t));

	/* decreament counter */
	stack->size.fetch_sub(1);

	return ((void*)value);
}

static inline void lfstack_free(lfstack_t* stack)
{
	while (stack->size.load(std::memory_order_acquire))
	{
		lfstack_pop(stack);
	}
}
////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////
// FIFO                                                                           //
////////////////////////////////////////////////////////////////////////////////////
static inline bool lffifo_init(lffifo_t* fifo)
{
	/* initialize fifo control block */
	lffifo_node_t* node = (lffifo_node_t*)mmFixedSizeMemoryAlloc(
		sizeof(lffifo_node_t)
	);
	if (node == NULL){return false;}

	/* write (node) with release */
	node->node = NULL;
	node->aba_ = 0;
	node->valu = 0;

	fifo->head_.store(lf_pointer_t());
	fifo->tail_.store(lf_pointer_t());

	fifo->size.store(0);
	return (true);
}

static inline size_t lffifo_size(const lffifo_t* fifo)
{
	return fifo->size.load(std::memory_order_acquire);
}

static inline bool lffifo_empty(const lffifo_t* fifo)
{
	return (fifo->size.load(std::memory_order_acquire) == 0);
}

static inline bool lffifo_full(const lffifo_t* fifo)
{
	return false;
}

static inline bool lffifo_push(lffifo_t* fifo, void* value)
{
	/* node->next = NULL; */
	lffifo_node_t* node = (lffifo_node_t*)mmFixedSizeMemoryAlloc(
		sizeof(lffifo_node_t)
	);
	if (node == NULL){return false;}

	/* write (node) with release */
	node->valu = (uint64_t)(value);
	node->node = NULL;

	/* tail/next load with acquire (all change on other core we should know) */
	lf_pointer_t tail, next;
	std::atomic<lf_pointer_t>* tail_node;

	lf_pointer_t* pt = (lf_pointer_t*)node;
	while (1)
	{
		tail = fifo->tail_.load(std::memory_order_acquire);
		
		tail_node = (std::atomic<lf_pointer_t>*)(tail.node);
		next = tail_node->load(std::memory_order_acquire);

		// if ((tail.node == fifo->tail_.node) && (tail.aba_ == fifo->tail_.aba_))
		{
			if (next.node == NULL) {
				lf_pointer_t newp;

				newp.node = pt;
				newp.aba_ = next.aba_ + 1;

				if (tail_node->compare_exchange_weak(next, newp)){
					break;  // Enqueue done!
				}
			}
			else {
				lf_pointer_t newp;
				newp.node = next.node;
				newp.aba_ = tail.aba_ + 1;

				fifo->tail_.compare_exchange_weak(tail, newp);
			}
		}
	}

	{
		lf_pointer_t newp;

		newp.node = pt;
		newp.aba_ = tail.aba_ + 1;
		fifo->tail_.compare_exchange_weak(tail, newp);
	}

	/* increament counter */
	fifo->size.fetch_add(1);

	return true;
};

static inline void* lffifo_pop(lffifo_t* fifo)
{
	uint64_t valu;

	/* head/tail/next load with acquire
	   (all changes on other cores we should know)
	*/
	lf_pointer_t tail, head, next;

	/* define a atmic pointer here to load (next) from
	   (head.node) in with acquire
	*/
	std::atomic<lf_pointer_t> * head_node;

	while (1)
	{
		head = fifo->head_.load(std::memory_order_acquire);
		tail = fifo->tail_.load(std::memory_order_acquire);

		/* next should load with acquire
		   (all changes on other cores we should know)
		*/
		head_node = (std::atomic<lf_pointer_t> * )(head.node);
		next = head_node->load(std::memory_order_acquire);

		// if ((head.node == fifo->head_.node) && (head.aba_ == fifo->head_.aba_))
		{
			if (head.node == tail.node) {

				/* queue empty (?) */
				if (next.node == NULL) {
					return  NULL;
				}

				lf_pointer_t newp;
				newp.node = next.node;
				newp.aba_ = tail.aba_ + 1;

				fifo->tail_.compare_exchange_weak(tail, newp);
			}
			else {
				/* copy valu */
				valu = ((volatile lffifo_node_t*)(next.node))->valu;

				lf_pointer_t newp;
				newp.node = next.node;
				newp.aba_ = head.aba_ + 1;
				if (fifo->head_.compare_exchange_weak(head, newp)){
					break;
				}
			}
		}
	}

	/* decreament counter */
	fifo->size.fetch_sub(1);

	/* free the memory */
	mmFixedSizeMemoryFree(head.node, sizeof(lffifo_node_t));
	return ((void*)valu);
};

static inline void lffifo_free(lffifo_t* fifo)
{
	while (fifo->size)
	{
		lffifo_pop(fifo);
	}

	lf_pointer_t head = fifo->head_.load(std::memory_order_acquire);
	mmFixedSizeMemoryFree(head.node, sizeof(lffifo_node_t));
}
////////////////////////////////////////////////////////////////////////////////////

#endif