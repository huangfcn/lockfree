#include <stdint.h>
#include <stdbool.h>

#include "mirrorbuf.h"

#ifndef __MAGICQ_SPSC_H__
#define __MAGICQ_SPSC_H__

#ifdef __cplusplus
extern "C" {
#endif

   typedef struct _magicq_t
   {
      uint32_t size, vsiz;

      volatile uint32_t head;
      volatile uint32_t tail;

      volatile uint64_t * data;

      mirrorbuf_t mbuf;
   } magicq_t;

   static inline bool magicq_create(magicq_t * cb, int order)
   {
      cb->size = (1UL << order);
      cb->vsiz = (2UL << order);
      cb->head = 0;
      cb->tail = 0;

      cb->data = (uint64_t *)mirrorbuf_create(&(cb->mbuf), cb->size * sizeof(uint64_t));
      return (cb->data != NULL);
   }

   static inline void magicq_destroy(magicq_t * cb)
   {
      mirrorbuf_destroy(&(cb->mbuf));
   }

   static inline bool magicq_full(const magicq_t * cb)
   {
      return (cb->head == (cb->tail ^ cb->size));
   }

   static inline bool magicq_empty(const magicq_t * cb)
   {
      return (cb->head == cb->tail);
   }

   static inline void * magicq_top(const magicq_t * cb)
   {
      return cb->data[cb->head];
   }

   static inline bool magicq_push(magicq_t * cb, void * data)
   {
      if (magicq_full(cb)) { return false; };

      cb->data[cb->tail] = ((uint64_t)data);
      cb->tail = (cb->tail + 1) & (cb->vsiz - 1);

      return true;
   }

   static inline void * magicq_pop(magicq_t * cb)
   {
      if (magicq_empty(cb)) { return NULL; };

      void * data = ((void *)(cb->data[cb->head]));
      cb->head = (cb->head + 1) & (cb->vsiz - 1);

      return data;
   };

#ifdef __cplusplus
};
#endif

#endif