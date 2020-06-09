#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <stdbool.h>

#ifndef __LOCKFREE_FIFO_LIFO_H__
#define __LOCKFREE_FIFO_LIFO_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32

#ifndef _WIN32_CAS
#define _WIN32_CAS

#include <malloc.h>
#include <Windows.h>
#define CAS2(ptr, oldp, newp)       (_InterlockedCompareExchange128((ptr), (newp)[1], (newp)[0], (oldp)))
#define CAS(ptr, oldval, newval)    (_InterlockedCompareExchange((ptr), (newval), (oldval)) == (oldval))
#define FAA(ptr                )    (_InterlockedIncrement(ptr))
#define FAS(ptr                )    (_InterlockedDecrement(ptr))

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

#endif // _WIN32_CAS

#else

#ifndef _LINUX_CAS
#define _LINUX_CAS

#include <sched.h>

#ifndef _DEFAULT_SOURCE

#define _DEFAULT_SOURCE  // usleep
#include <unistd.h>
#undef  _DEFAULT_SOURCE

#else
#include <unistd.h>
#endif   
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

// #define CAS2(ptr, oldptr, newptr) __sync_bool_compare_and_swap_16((long long *)(ptr), *(long long *)(oldptr), *(long long *)(newptr))
#define CAS(ptr, oldval, newval ) __sync_bool_compare_and_swap(ptr, oldval, newval)
#define FAA(ptr                 ) __sync_fetch_and_add((ptr), 1) 
#define FAS(ptr                 ) __sync_fetch_and_sub((ptr), 1) 

#define _aligned_malloc(n, align) aligned_alloc((align), (n))
#define _aligned_free(x)          free(x)

#endif  // _LINUX_CAS
#endif  // _WIN32

    //////////////////////////////////////////////////////////////
    /* common structure used by fifi and stack                  */
    //////////////////////////////////////////////////////////////
    struct lf_node;
    typedef struct lf_node lf_node_t;

    struct lf_pointer;
    typedef struct lf_pointer lf_pointer_t;

    struct lf_node
    {
        lf_node_t * node;
        uint64_t    aba_;

        uint64_t    valu;
        uint64_t    padd;
    };

    struct lf_pointer
    {
        lf_pointer_t * node;
        uint64_t       aba_;
    };
    //////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////
    /* lock-free stack                                          */
    //////////////////////////////////////////////////////////////
    typedef lf_node_t    lfstack_node_t;
    typedef lf_pointer_t lfstack_head_t;

    typedef struct {
        volatile lfstack_head_t worklist;
        uint64_t _d1[6];

        volatile lfstack_head_t freelist;
        uint64_t _d2[6];

        volatile size_t size;

        size_t          capa;
        lf_node_t *     bufa;
    } lfstack_t;
    //////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////
    /* lock-free queue                                          */
    //////////////////////////////////////////////////////////////
    typedef lf_node_t    lffifo_node_t;
    typedef lf_pointer_t lffifo_head_t;

    typedef struct {
        volatile lffifo_head_t tail_;
        uint64_t pad1[6];

        volatile lffifo_head_t head_;
        uint64_t pad2[6];

        volatile lfstack_head_t freelist;
        uint64_t pad3[6];

        volatile size_t size;

        size_t          capa;
        lf_node_t *     bufa;
    } lffifo_t;
    //////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////
    // LIFO / STACK                                                           //
    ////////////////////////////////////////////////////////////////////////////
    static inline bool lfstack_init_internal(volatile lfstack_head_t* head)
    {
        head->aba_ = 0;
        head->node = NULL;

        return true;
    }

    static inline bool lfstack_push_internal(
        volatile lfstack_head_t* head, lf_pointer_t* pt
    )
    {
        lfstack_head_t orig;
        lfstack_head_t next;

        do {
            orig.aba_ = head->aba_;
            orig.node = head->node;

            next.aba_ = orig.aba_ + 1;
            next.node = pt;

            /* write (pt) with release */
            ((volatile lf_pointer_t*)(pt))->node = head->node;
            ((volatile lf_pointer_t*)(pt))->aba_ = next.aba_;

        } while (!CAS2((int64_t*)head, (int64_t*)(&orig), (int64_t*)(&next)));

        return (true);
    }

    static inline lf_pointer_t* lfstack_pop_internal(
        volatile lfstack_head_t* head
    )
    {
        lfstack_head_t orig;
        lfstack_head_t next;

        lf_pointer_t* node;

        do {
            orig.aba_ = head->aba_;
            orig.node = head->node;

            node = orig.node;
            if (node == NULL) {
                return NULL;
            }

            /* load (node) with acquire */
            next.aba_ = orig.aba_ + 1;
            next.node = ((volatile lf_pointer_t*)(node))->node;

        } while (!CAS2((int64_t*)head, (int64_t*)(&orig), (int64_t*)(&next)));

        return (node);
    }

    static inline bool lfstack_init(lfstack_t* stack, int order)
    {
        /* initialize work list as empty */
        lfstack_init_internal(&(stack->worklist));

        /* initialize free nodes list */
        lfstack_init_internal(&(stack->freelist));
        stack->capa = (1ULL << order);
        stack->bufa = (lf_node_t *)_aligned_malloc(sizeof(lf_node_t) * stack->capa, 64);
        for (size_t i = 0; i < stack->capa; ++i) {
            lfstack_push_internal(&(stack->freelist), (lf_pointer_t *)(stack->bufa + i));
        }

        /* set size to 0 */
        stack->size = 0;
        return true;
    }

    static inline size_t lfstack_size(const lfstack_t* stack)
    {
        return (stack->size);
    }

    static inline bool lfstack_empty(const lfstack_t* stack)
    {
        return (stack->size == 0);
    }

    static inline bool lfstack_full(const lfstack_t* stack)
    {
        return (stack->size == stack->capa);
    }

    static inline bool lfstack_push(lfstack_t* stack, void* value)
    {
        //////////////////////////////////////
        // allocate a new node              //
        //////////////////////////////////////
        lfstack_node_t* node = (lfstack_node_t*)lfstack_pop_internal(&(stack->freelist));
        if (node == NULL) { return false; };

        /* write (node) with release */
        ((volatile lfstack_node_t*)(node))->valu = (uint64_t)value;
        //////////////////////////////////////
        
        /* push into working list */
        lfstack_push_internal(&(stack->worklist), (lf_pointer_t*)(node));

        /* increament counter */
        FAA(&(stack->size));

        return (true);
    }

    static inline void* lfstack_pop(lfstack_t* stack)
    {
        lfstack_node_t* node = (lfstack_node_t*)lfstack_pop_internal(&(stack->worklist));
        if (node == NULL) { return NULL; };

        /* load (node) with acquire */
        uint64_t value = ((volatile lfstack_node_t*)(node))->valu;

        /* free the node */
        lfstack_push_internal(&(stack->freelist), (lf_pointer_t *)(node));

        /* decreament counter */
        FAS(&(stack->size));

        return ((void*)value);
    }

    static inline void lfstack_free(lfstack_t* stack)
    {
        if (stack->bufa) { _aligned_free(stack->bufa); };
    }
    ////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////
    // FIFO                                                                           //
    ////////////////////////////////////////////////////////////////////////////////////
    static inline bool lffifo_init(lffifo_t* fifo, int order)
    {
        /* setup free nodes list */
        lfstack_init_internal(&(fifo->freelist));
        fifo->capa = (1ULL << order);
        fifo->bufa = (lf_node_t *)_aligned_malloc(sizeof(lf_node_t) * fifo->capa, 64);
        for (size_t i = 1; i < fifo->capa; ++i) {
            lfstack_push_internal(&(fifo->freelist), (lf_pointer_t *)(fifo->bufa + i));
        }
        fifo->capa -= 1;

        /* initialize fifo control block */
        lffifo_node_t* node = fifo->bufa;

        /* write (node) with release */
        ((volatile lffifo_node_t*)(node))->node = NULL;
        ((volatile lffifo_node_t*)(node))->aba_ = 0;
        ((volatile lffifo_node_t*)(node))->valu = 0;

        fifo->head_.node = (lf_pointer_t*)node;
        fifo->head_.aba_ = 0;

        fifo->tail_.node = (lf_pointer_t*)node;
        fifo->tail_.aba_ = 0;

        fifo->size = 0;

        return (true);
    }

    static inline size_t lffifo_size(const lffifo_t* fifo)
    {
        return fifo->size;
    }

    static inline bool lffifo_empty(const lffifo_t* fifo)
    {
        return (fifo->size == 0);
    }

    static inline bool lffifo_full(const lffifo_t* fifo)
    {
        return false;
    }

    static inline bool lffifo_push(lffifo_t* fifo, void* value)
    {
        /* node->next = NULL; */
        lffifo_node_t* node = (lffifo_node_t*)lfstack_pop_internal(&(fifo->freelist));
        if (node == NULL) { return false; };

        /* write (node) with release */
        ((volatile lffifo_node_t*)(node))->valu = (uint64_t)(value);
        ((volatile lffifo_node_t*)(node))->node = NULL;

        /* tail/next load with acquire (all change on other core we should know) */
        lf_pointer_t tail, next;
        lf_pointer_t* pt = (lf_pointer_t*)node;
        while (1)
        {
            tail.node = fifo->tail_.node;
            tail.aba_ = fifo->tail_.aba_;

            next.node = tail.node->node;
            next.aba_ = tail.node->aba_;

            if ((tail.node == fifo->tail_.node) && (tail.aba_ == fifo->tail_.aba_))
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

                    CAS2((int64_t*)(&(fifo->tail_)), (int64_t*)(&tail), (int64_t*)(&newp));
                }
            }
        }

        {
            lf_pointer_t newp;

            newp.node = pt;
            newp.aba_ = tail.aba_ + 1;
            CAS2((int64_t*)(&(fifo->tail_)), (int64_t*)(&tail), (int64_t*)(&newp));
        }

        /* increament counter */
        FAA(&(fifo->size));

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
        volatile lf_pointer_t* head_node;

        while (1)
        {
            head.node = fifo->head_.node;
            head.aba_ = fifo->head_.aba_;

            tail.node = fifo->tail_.node;
            tail.aba_ = fifo->tail_.aba_;

            /* next should load with acquire
               (all changes on other cores we should know)
            */
            head_node = head.node;
            next.node = (head_node)->node;
            next.aba_ = (head_node)->aba_;

            if ((head.node == fifo->head_.node) && (head.aba_ == fifo->head_.aba_))
            {
                if (head.node == tail.node) {

                    /* queue empty (?) */
                    if (next.node == NULL) {
                        return  NULL;
                    }

                    lf_pointer_t newp;
                    newp.node = next.node;
                    newp.aba_ = tail.aba_ + 1;

                    CAS2((int64_t*)(&(fifo->tail_)), (int64_t*)(&tail), (int64_t*)(&newp));
                }
                else {
                    /* copy valu */
                    valu = ((volatile lffifo_node_t*)(next.node))->valu;

                    lf_pointer_t newp;
                    newp.node = next.node;
                    newp.aba_ = head.aba_ + 1;
                    if (CAS2((int64_t*)(&(fifo->head_)), (int64_t*)(&head), (int64_t*)(&newp))) {
                        break;
                    }
                }
            }
        }

        /* decreament counter */
        FAS(&(fifo->size));

        /* free the memory */
        lfstack_push_internal(&(fifo->freelist), (lf_pointer_t *)(head.node));
        return ((void*)valu);
    };

    static inline void lffifo_free(lffifo_t* fifo)
    {
        if (fifo->bufa) { _aligned_free(fifo->bufa); };
    }
    ////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
};
#endif

#endif