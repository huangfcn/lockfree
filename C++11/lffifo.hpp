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

//////////////////////////////////////////////////////////////
/* lock-free queue                                          */
//////////////////////////////////////////////////////////////
#ifdef _WIN32
#include <Windows.h>
#define CAS2(ptr, oldp, newp)  (_InterlockedCompareExchange128((ptr), (newp)[1], (newp)[0], (oldp)))
#else
static inline char CAS2(volatile int64_t* addr, volatile int64_t* oldval, volatile int64_t* newval)
{
    void* v1 = (void*)(oldval[0]);
    int64_t v2 = (int64_t)(oldval[1]);
    void* n1 = (void*)(newval[0]);
    int64_t n2 = (int64_t)(newval[1]);

    register bool ret;
    __asm__ __volatile__(
        "# CAS2 \n\t"
        "lock cmpxchg16b (%1) \n\t"
        "sete %0               \n\t"
        :"=a" (ret)
        : "D" (addr), "d" (v2), "a" (v1), "b" (n1), "c" (n2)
    );
    return ret;
}
#endif  // _WIN32

struct lffifo_pointer_t {
    std::atomic<lf_pointer_t> next;
};

template <typename T> class lffifo_t {
protected:
    alignas(64) std::atomic<lf_pointer_t> tail_;
    alignas(64) std::atomic<lf_pointer_t> head_;

    alignas(64) std::atomic<lf_pointer_t> freelist;
    alignas(64) std::atomic<uint64_t>     size;

    uint64_t       capacity;
    lf_node_t<T> * nodes;

public:
    lffifo_t(int order) : tail_(lf_pointer_t()), head_(lf_pointer_t()), freelist(lf_pointer_t())
    {
        /* allocate memory */
        capacity = (1ULL << order);
        nodes = new lf_node_t<T>[capacity];
        if (nodes == NULL) { return; };

        /* initialzie control block */
        nodes[0].node = nullptr;
        nodes[0].aba_ = 0;

        lf_pointer_t pt;
        pt.node = (lf_pointer_t*)(&nodes[0]);
        pt.aba_ = 0;

        head_.store(pt);
        tail_.store(pt);

        size.store(0);
        
        /* initialize freelist */
        for (uint64_t i = 1; i < capacity; ++i) {
            lfstack_push_internal(&freelist, (lf_pointer_t *)(&nodes[i]));
        }

        capacity -= 1;
    };

    ~lffifo_t() {
        if (nodes) { delete[]nodes; };
    };

    inline size_t getsize(){return (size.load(std::memory_order_acquire));};
    inline bool   isempty(){return (size.load(std::memory_order_acquire) == 0);};
    inline bool   isfull (){return (size.load(std::memory_order_acquire) == capacity);}

    bool push(const T & object)
    {
        /* node->next = NULL; */
        lf_node_t<T>* node = (lf_node_t<T>*)lfstack_pop_internal(&freelist);
        if (node == NULL){return false;}

        /* write (node) with release */
        node->valu = object;
        node->node = NULL;

        /* tail/next load with acquire (all change on other core we should know) */
        lf_pointer_t tail, next;
        lf_pointer_t* pt = (lf_pointer_t*)node;
        while (1)
        {
            tail = tail_.load(std::memory_order_acquire);

            next.node = tail.node->node;
            next.aba_ = tail.node->aba_;

            // if ((tail.node == tail_.load(std::memory_order_acquire).node) && (tail.aba_ == tail_.load(std::memory_order_acquire).aba_))
            {
                if (next.node == NULL) {
                    lf_pointer_t newp;

                    newp.node = pt;
                    newp.aba_ = next.aba_ + 1;

                    if (CAS2((int64_t*)(tail.node), (int64_t*)(&next), (int64_t*)(&newp))) {
                        break;  // Enqueue done!
                    }
                }
                else {
                    lf_pointer_t newp;
                    newp.node = next.node;
                    newp.aba_ = tail.aba_ + 1;

                    tail_.compare_exchange_weak(tail, newp);
                    // CAS2((int64_t*)(&(tail_)), (int64_t*)(&tail), (int64_t*)(&newp));
                }
            }
        }

        {
            lf_pointer_t newp;

            newp.node = pt;
            newp.aba_ = tail.aba_ + 1;
            tail_.compare_exchange_weak(tail, newp);
            // CAS2((int64_t*)(&(tail_)), (int64_t*)(&tail), (int64_t*)(&newp));
        }

        /* increament counter */
        size.fetch_add(1);

        return true;
    };

    bool pop(T & object)
    {
        /* head/tail/next load with acquire
        (all changes on other cores we should know)
        */
        lf_pointer_t tail, head, next;

        while (1)
        {
            head = head_.load(std::memory_order_acquire);
            tail = tail_.load(std::memory_order_acquire);

            /* next should load with acquire
            (all changes on other cores we should know)
            */
            next.node = (head.node)->node;
            next.aba_ = (head.node)->aba_;

            // if ((head.node == head_.load(std::memory_order_acquire).node) && (head.aba_ == head_.load(std::memory_order_acquire).aba_))
            {
                if (head.node == tail.node) {
                    /* queue empty (?) */
                    if (next.node == nullptr) {
                        return  false;
                    }

                    lf_pointer_t newp;
                    newp.node = next.node;
                    newp.aba_ = tail.aba_ + 1;
                    tail_.compare_exchange_weak(tail, newp);
                    // CAS2((int64_t*)(&(tail_)), (int64_t*)(&tail), (int64_t*)(&newp));
                }
                else {
                    /* copy valu (move if possible) */
                    object = ((lf_node_t<T>*)(next.node))->valu;

                    lf_pointer_t newp;
                    newp.node = next.node;
                    newp.aba_ = head.aba_ + 1;
                    if (head_.compare_exchange_weak(head, newp)){ // CAS2((int64_t*)(&(head_)), (int64_t*)(&head), (int64_t*)(&newp))) {
                        break;
                    }
                }
            }
        }

        /* decreament counter */
        size.fetch_sub(1);

        /* free the memory */
        lfstack_push_internal(
            &freelist,
            (lf_pointer_t*)(head.node)
        );
        return (true);
    };

    T pop(){
        T object(0); pop(object); return object;
    };
};
//////////////////////////////////////////////////////////////
#endif
