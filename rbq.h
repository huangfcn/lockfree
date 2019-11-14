#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <stdbool.h>

// #include "mirrorbuf.h"

#ifndef __LOCKFREE_RBQ_MPMC_H__
#define __LOCKFREE_RBQ_MPMC_H__

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

#define CAS(ptr, oldval, newval ) __sync_bool_compare_and_swap(ptr, oldval, newval)
#define FAA(ptr                 ) __sync_fetch_and_add((ptr), 1) 
#define FAS(ptr                 ) __sync_fetch_and_sub((ptr), 1) 

#define _aligned_malloc(n, align) aligned_alloc((align), (n))
#define _aligned_free(x)          free(x)

#endif  // _LINUX_CAS
#endif  // _WIN32

   typedef struct _rbq_swap_t
   {
      uint64_t addr;
      uint64_t padding[7];
   } rbq_swap_t;

   typedef struct _rbq_t
   {
      uint64_t size;
      uint64_t vsiz;
      volatile uint64_t maxi;
      uint64_t pad1[5];

      volatile uint64_t head;
      uint64_t pad2[7];

      volatile uint64_t tail;
      uint64_t pad3[7];

      volatile rbq_swap_t * data;
      uint64_t pad4[7];
      // mirrorbuf_t mbuf;
   } rbq_t;

   static inline bool rbq_init(rbq_t * rbq, int order)
   {
      rbq->size = (1ULL << order);
      rbq->vsiz = (2ULL << order);
      rbq->head = 0;
      rbq->tail = 0;
      rbq->maxi = 0;

      // rbq->data = (uint64_t *)mirrorbuf_create(&(rbq->mbuf), rbq->size * sizeof(uint64_t));
      rbq->data = (rbq_swap_t *)_aligned_malloc(rbq->size * sizeof(rbq_swap_t), 16);
      memset((void *)rbq->data, 0, rbq->size * sizeof(rbq_swap_t));

      return (rbq->data != NULL);
   };

   static inline void rbq_free(rbq_t * rbq)
   {
      _aligned_free((rbq_swap_t *)(rbq->data)); 
      // mirrorbuf_destroy(&(rbq->mbuf));
   }

   static inline bool rbq_full(const rbq_t * rbq)
   {
      return (rbq->tail >= (rbq->head + rbq->size));
   }

   static inline bool rbq_empty(const rbq_t * rbq)
   {
      return (rbq->head >= rbq->tail);
   }

   static inline size_t rbq_size(const rbq_t * rbq)
   {
      return (rbq->tail - rbq->head);
   }

   /* push @ mutiple producers */
   static inline bool rbq_push(rbq_t * rbq, void * data)
   {
      uint64_t currReadIndex;
      uint64_t currWriteIndex;
      uint64_t nextWriteIndex;

      uint64_t d64 = ((uint64_t)data);

      // do 
      {
         // check if queue full
         currWriteIndex = rbq->tail;
         currReadIndex  = rbq->head;        
         if (currWriteIndex >= (currReadIndex + rbq->size))
         {
            return false;
         }

         // now perfrom the FAA operation on the write index. 
         // the Space @ currWriteIndex will be reserved for us.
         nextWriteIndex = FAA(&(rbq->tail)); 
         currWriteIndex = (nextWriteIndex - 1) & (rbq->size - 1);

         // if (currReadIndex == (currWriteIndex ^ rbq->size))
         // {
         //    return false;
         // }

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
      while (!CAS(&(rbq->data[currWriteIndex].addr), 0, d64))
      {
         // sched_yield();
         usleep((currWriteIndex & 1) + 1);
      }

      return true;
   };

   /* pop @ mutiple consumers */
   static inline void * rbq_pop(rbq_t * rbq)
   {
      uint64_t currWriteIndex;
      uint64_t currReadIndex;
      uint64_t nextReadIndex;

      // do
      {
         // check if queue empty
         currReadIndex  = rbq->head;
         currWriteIndex = rbq->tail;
         if (currReadIndex >= currWriteIndex)
         {
            return NULL;
         }

         // now perfrom the FAA operation on the read index. 
         // the Space @ currReadIndex will be reserved for us.
         nextReadIndex = FAA(&(rbq->head));
         currReadIndex = (nextReadIndex - 1) & (rbq->size - 1);

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
         uint64_t d64 = rbq->data[currReadIndex].addr;
         if (d64 && CAS(&(rbq->data[currReadIndex].addr), d64, NULL))
         {
            return ((void *)d64);
         }

         // sched_yield();
         usleep((currReadIndex & 1) + 1);
      }

      return (NULL);
   };

   /* push described in "Yet another implementation of a 
   lock-free circular array queue" by Faustino Frechilla
   */
   static inline bool rbq_push2(rbq_t * rbq, void * data)
   {
      uint64_t currReadIndex;
      uint64_t currWriteIndex;
      uint64_t nextWriteIndex;

      uint64_t d64 = ((uint64_t)data);

      do {
         currWriteIndex = rbq->tail;
         currReadIndex  = rbq->head;
         if (currWriteIndex >= (currReadIndex + rbq->size))
         {
            // the queue is full
            return false;
         }

         nextWriteIndex = (currWriteIndex + 1); // & (rbq->vsiz - 1);
         if (CAS(&(rbq->tail), currWriteIndex, nextWriteIndex))
         {
            break;
         }

         /* back-off */
      } while (1);

      // We know now that this index is reserved for us. Use it to save the data
      rbq->data[currWriteIndex & (rbq->size - 1)].addr = d64;

      // update the maximum read index after saving the data. 
      // It wouldn't fail if there is only one thread inserting in the queue. 
      // It might fail if there are more than 1 producer threads because this
      // operation has to be done in the same order as the previous CAS
      while (!CAS(&(rbq->maxi), currWriteIndex, nextWriteIndex))
      {
         // this is a good place to yield the thread in case there are more
         // software threads than hardware processors and you have more
         // than 1 producer thread have a look at sched_yield (POSIX.1b)
         sched_yield();
      };

      return true;
   };

   /* pop described in "Yet another implementation of a 
   lock-free circular array queue" by Faustino Frechilla
   */
   static inline void * rbq_pop2(rbq_t * rbq)
   {
      uint64_t currMaximumReadIndex;
      uint64_t currReadIndex;
      uint64_t nextReadIndex;

      do
      {
         // to ensure thread-safety when there is more than 1 producer thread
         // a second index is defined (m_maximumReadIndex)
         currReadIndex        = rbq->head;
         currMaximumReadIndex = rbq->maxi;
         if (currReadIndex >= currMaximumReadIndex)
         {
            // the queue is empty or
            // a producer thread has allocate space in the queue but is
            // waiting to commit the data into it
            return (NULL);
         }

         // retrieve the data from the queue
         uint64_t d64 = rbq->data[currReadIndex & (rbq->size - 1)].addr;

         // try to perfrom now the CAS operation on the read index. If we succeed
         // a_data already contains what m_readIndex pointed to before we
         // increased it
         nextReadIndex = (currReadIndex + 1); // & (rbq->vsiz - 1);
         if (CAS(&(rbq->head), currReadIndex, nextReadIndex))
         {
            return (void *)d64;
         }

         // it failed retrieving the element off the queue. Someone else must
         // have read the element stored at currReadIndex before we could 
         // perform the CAS operation

         /* back-off */
         usleep(((d64 >> 3) & 1) + 1);
      } while(1);

      return (NULL);
   };

   /* push @ single producer single consumer */
   static inline bool rbq_pushspsc(rbq_t * rbq, void * data)
   {
      if (rbq_full(rbq)) { return false; };

      rbq->data[(rbq->tail) & (rbq->size - 1)].addr = ((uint64_t)data);
      rbq->tail = (rbq->tail + 1); // & (rbq->size - 1);

      return true;
   }

   /* pop @ single producer single consumer */
   static inline void * rbq_popspsc(rbq_t * rbq)
   {
      if (rbq_empty(rbq)) { return NULL; };

      uint64_t d64 = rbq->data[rbq->head & (rbq->size - 1)].addr;
      rbq->head = (rbq->head + 1); // & (rbq->vsiz - 1);

      return ((void *)d64);
   };

#ifdef __cplusplus
};
#endif

#endif