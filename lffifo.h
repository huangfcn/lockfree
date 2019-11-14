#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <stdbool.h>

#include "lf_struct.h"
#include "fixedSizeMemoryLF.h"

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

      ft.QuadPart = -(10*usec);

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

   // static inline bool CAS2(volatile int64_t * addr,  volatile int64_t * old_value, volatile int64_t * new_value) 
   // { 
   //    bool ret; 
   //    __asm__ __volatile__( 
   //       "lock cmpxchg16b %1;\n" 
   //       "sete %0;\n" 
   //       :"=m"(ret),"+m" (*(volatile int64_t *) (addr)) 
   //       :"a" (old_value[0]), "d" (old_value[1]), "b" (new_value[0]), "c" (new_value[1])); 
   //    return ret; 
   // }

   static inline char CAS2 (volatile int64_t * addr, volatile int64_t * oldval, volatile int64_t * newval) 
   {
      void *  v1 = (void  *)(oldval[0]); 
      int64_t v2 = (int64_t)(oldval[1]);
      void *  n1 = (void  *)(newval[0]);
      int64_t n2 = (int64_t)(newval[1]);

      register bool ret;
      __asm__ __volatile__ (
         "# CAS2 \n\t"
         "lock cmpxchg16b (%1) \n\t"
         "sete %0               \n\t"
         :"=a" (ret)
         :"D" (addr), "d" (v2), "a" (v1), "b" (n1), "c" (n2)
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

   ////////////////////////////////////////////////////////////////////////////
   // LIFO / STACK                                                           //
   ////////////////////////////////////////////////////////////////////////////
   static inline bool lfstack_init_internal(volatile lfstack_head_t * head)
   {
      head->aba_ = 0;
      head->node = NULL;

      return true;
   }

   static inline bool lfstack_push_internal(
      volatile lfstack_head_t * head, lf_pointer_t * pt
   )
   {
      lfstack_head_t orig;
      lfstack_head_t next;

      do {
         orig.aba_ = head->aba_;
         orig.node = head->node;

         next.aba_ = orig.aba_ + 1;
         next.node = pt;

         pt->node = head->node;
         pt->aba_ = next.aba_;

      } while (!CAS2((int64_t *)head, (int64_t *)(&orig), (int64_t *)(&next)));

      return (true);
   }

   static inline lf_pointer_t * lfstack_pop_internal(
      volatile lfstack_head_t * head
   )
   {
      lfstack_head_t orig;
      lfstack_head_t next;

      lf_pointer_t * node;

      do {
         orig.aba_ = head->aba_;
         orig.node = head->node;

         node = orig.node;
         if (node == NULL){
            return NULL;
         }

         next.aba_ = orig.aba_ + 1;
         next.node = node->node;

      } while (!CAS2((int64_t *)head, (int64_t *)(&orig), (int64_t *)(&next)));

      return (node);
   }

   static inline bool lfstack_init(lfstack_t * stack, int order)
   {
      lfstack_init_internal(&(stack->worklist));
      
      stack->size = 0;
      return true;
   }

   static inline size_t lfstack_size(lfstack_t * stack)
   {
      return (stack->size);
   }

   static inline bool lfstack_empty(lfstack_t * stack)
   {
      return (stack->size == 0);
   }

   static inline bool lfstack_full(lfstack_t * stack)
   {
      return (false);
   }

   static inline bool lfstack_push(lfstack_t * stack, void  * value)
   {
      //////////////////////////////////////
      // allocate a new node              //
      //////////////////////////////////////
      lfstack_node_t * node = (lfstack_node_t *)mmFixedSizeMemoryAlloc(
         sizeof(lfstack_node_t)
      );
      if (node == NULL)
      {
         return false;
      }
      node->valu = (uint64_t)value;
      //////////////////////////////////////

      lfstack_push_internal(
         &(stack->worklist),
         (lf_pointer_t *)(node)
      );

      /* increament counter */
      FAA(&(stack->size));

      return (true);
   }

   static inline void * lfstack_pop(lfstack_t * stack)
   {
      lfstack_node_t * node = (lfstack_node_t *)lfstack_pop_internal(
         &(stack->worklist)
      );
      if (node == NULL)
      {
         return NULL;
      }

      uint64_t value = node->valu;

      /* free the node */
      mmFixedSizeMemoryFree(node, sizeof(lfstack_node_t));

      /* decreament counter */
      FAS(&(stack->size));

      return ((void *)value);
   }

   static inline void lfstack_free(lfstack_t * stack)
   {
      while (stack->size)
      {
         lfstack_pop(stack);
      }
   }
   ////////////////////////////////////////////////////////////////////////////////////

   ////////////////////////////////////////////////////////////////////////////////////
   // FIFO                                                                           //
   ////////////////////////////////////////////////////////////////////////////////////
   static inline bool lffifo_init(lffifo_t * fifo, int order)
   {
      /* initialize fifo control block */
      lffifo_node_t * node = (lffifo_node_t *)mmFixedSizeMemoryAlloc(
         sizeof(lffifo_node_t)
      );
      if (node == NULL)
      {
         return false;
      }

      node->node = NULL;
      node->aba_ = 0;
      node->valu = 0;

      fifo->head_.node = (lf_pointer_t *)node;
      fifo->head_.aba_ = 0;

      fifo->tail_.node = (lf_pointer_t *)node;
      fifo->tail_.aba_ = 0;

      fifo->size = 0;

      return (true);
   }
   
   static inline size_t lffifo_size(const lffifo_t * fifo)
   {
      return fifo->size;
   }

   static inline bool lffifo_empty(const lffifo_t * fifo)
   {
      return (fifo->size == 0);
   }

   static inline bool lffifo_full(const lffifo_t * fifo)
   {
      return false;
   }

   static inline bool lffifo_push(lffifo_t * fifo, void * value)
   {
      /* node->next = NULL; */
      lffifo_node_t * node = (lffifo_node_t *)mmFixedSizeMemoryAlloc(
         sizeof(lffifo_node_t)
      );
      if (node == NULL)
      {
         return false;
      }

      node->valu = (uint64_t)(value);
      node->node = NULL;

      /* tail/next load with acquire (all change on other core we should know) */
      lf_pointer_t tail, next;
      lf_pointer_t * pt = (lf_pointer_t *)node;
      while (1)
      {
         tail.node = fifo->tail_.node;
         tail.aba_ = fifo->tail_.aba_;

         next.node = tail.node->node;
         next.aba_ = tail.node->aba_;

         if  ((tail.node == fifo->tail_.node) &&  (tail.aba_ == fifo->tail_.aba_))
         {
            if (next.node == NULL) {
               lf_pointer_t newp;

               newp.node = pt;
               newp.aba_ = next.aba_ + 1;

               if (CAS2((int64_t *)(tail.node), (int64_t *)(&next), (int64_t *)(&newp))){
                  break ;  // Enqueue done!
               }
            } else  {
               lf_pointer_t newp;
               newp.node = next.node;
               newp.aba_ = tail.aba_ + 1;

               CAS2((int64_t *)(&(fifo->tail_)), (int64_t *)(&tail), (int64_t *)(&newp));
            }
         }
      }

      {
         lf_pointer_t newp;

         newp.node = pt;
         newp.aba_ = tail.aba_ + 1;
         CAS2((int64_t *)(&(fifo->tail_)), (int64_t *)(&tail), (int64_t *)(&newp));
      }

      /* increament counter */
      FAA(&(fifo->size));

      return true;
   };

   static inline void * lffifo_pop(lffifo_t * fifo)
   {
      uint64_t valu;

      /* head/tail/next load with acquire 
         (all changes on other cores we should know) 
      */
      lf_pointer_t tail, head, next;

      /* define a atmic pointer here to load (next) from 
         (head.node) in with acquire 
      */
      volatile lf_pointer_t * head_node;

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

         if  ((head.node == fifo->head_.node) && (head.aba_ == fifo->head_.aba_))
         {
            if (head.node == tail.node){

               /* queue empty (?) */
               if (next.node == NULL) {
                  return  NULL;
               }

               lf_pointer_t newp;
               newp.node = next.node;
               newp.aba_ = tail.aba_+1;

               CAS2((int64_t *)(&(fifo->tail_)), (int64_t *)(&tail), (int64_t *)(&newp));
            }  
            else {
               /* copy valu */
               valu = ((lffifo_node_t *)next.node)->valu;

               lf_pointer_t newp;
               newp.node = next.node;
               newp.aba_ = head.aba_ + 1;
               if (CAS2((int64_t *)(&(fifo->head_)), (int64_t *)(&head), (int64_t *)(&newp))){
                  break ;
               }
            }
         }
      }

      /* decreament counter */
      FAS(&(fifo->size));

      /* free the memory */
      mmFixedSizeMemoryFree(head.node, sizeof(lffifo_node_t));
      return ((void *)valu);
   };

   static inline void lffifo_free(lffifo_t * fifo)
   {
      while (fifo->size)
      {
         lffifo_pop(fifo);
      }

      mmFixedSizeMemoryFree(fifo->head_.node, sizeof(lffifo_node_t));
   }
   ////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
};
#endif

#endif