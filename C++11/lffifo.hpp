#include <stdint.h>
#include <atomic>

#ifndef __LOCKFREE_STRUCT_H__
#define __LOCKFREE_STRUCT_H__

//////////////////////////////////////////////////////////////
/* common structure used by fifo/stack                      */
//////////////////////////////////////////////////////////////
template <typename T> struct lf_node_t
{
    lf_node_t * node;
    uint64_t    aba_;

    T           valu;
    uint64_t    padd;
};

struct lf_pointer_t
{
    lf_pointer_t * node;
    uint64_t       aba_;
};
//////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////
/* common functions used by fifo/stack for freelist         */
//////////////////////////////////////////////////////////////
static inline void lfstack_init_internal(std::atomic<lf_pointer_t> * head) {
    lf_pointer_t pt;
    pt.node = nullptr;
    pt.aba_ = 0;

    head->store(pt);
}

static inline bool lfstack_push_internal(
    std::atomic<lf_pointer_t> * head, lf_pointer_t * pt
)
{
    lf_pointer_t orig;
    lf_pointer_t next;

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
    std::atomic<lf_pointer_t>* head
)
{
    lf_pointer_t orig;
    lf_pointer_t next;

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

//////////////////////////////////////////////////////////////
/* lock-free stack                                          */
//////////////////////////////////////////////////////////////
template <typename T> class lfstack_t {
protected:
    alignas(64) std::atomic<lf_pointer_t> worklist;
    alignas(64) std::atomic<lf_pointer_t> freelist;
    alignas(64) std::atomic<uint64_t>     size;

    uint64_t                capacity;
    lf_node_t<T> *          nodes;

public:
    lfstack_t(int order) : worklist(lf_pointer_t()), freelist(lf_pointer_t())
    {
        /* allocate memory */
        capacity = (1ULL << order);
        nodes    = new lf_node_t<T> [capacity];
        if (nodes == nullptr) { return; };

        /* initialize freelist */
        for (uint64_t i = 0; i < capacity; ++i){
            lfstack_push_internal(&freelist, (lf_pointer_t *)(&nodes[i]));
        }

        size.store(0);
    }

    ~lfstack_t(){
        if (nodes){delete []nodes;}
    }

    inline size_t getsize(){return (size.load(std::memory_order_acquire));            };
    inline bool   isempty(){return (size.load(std::memory_order_acquire) == 0);       };
    inline bool    isfull(){return (size.load(std::memory_order_acquire) == capacity);};

    bool push(const T & object)
    {
        //////////////////////////////////////
        // allocate a new node              //
        //////////////////////////////////////
        lf_node_t<T> * node = (lf_node_t<T> *)lfstack_pop_internal(&freelist);
        if (node == NULL){return false;}

        /* write (node) with release (using move here) */
        node->valu = object;
        //////////////////////////////////////

        lfstack_push_internal(
            &worklist,
            (lf_pointer_t*)(node)
        );

        /* increament counter */
        size.fetch_add(1);

        return (true);
    }

    bool pop(T & object)
    {
        lf_node_t<T> * node = (lf_node_t<T> *)lfstack_pop_internal(&worklist);
        if (node == NULL){return false;}

        /* load (node) with acquire */
        object = node->valu;

        /* free the node */
        lfstack_push_internal(
            &freelist,
            (lf_pointer_t*)(node)
        );

        /* decreament counter */
        size.fetch_sub(1);

        return true;
    }

    T pop(){
        T object(0); pop(object); return object;
    };
};
//////////////////////////////////////////////////////////////
#endif
