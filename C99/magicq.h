#include <stdint.h>
#include <stdbool.h>

#include "mirrorbuf.h"

#ifndef __MAGICQ_SPSC_H__
#define __MAGICQ_SPSC_H__

#define MAGICQ_TYPE(name, type)                                         \
   typedef struct name##_t                                              \
   {                                                                    \
      uint32_t size, vsiz;                                              \
                                                                        \
      volatile uint32_t head;                                           \
      volatile uint32_t tail;                                           \
                                                                        \
               type *   data;                                           \
                                                                        \
      mirrorbuf_t mbuf;                                                 \
   } name##_t;

#define MAGICQ_INIT(name, type)                                         \
   static inline bool name##_init(name##_t * cb, int order)             \
   {                                                                    \
      cb->size = (1UL << order);                                        \
      cb->vsiz = (2UL << order);                                        \
      cb->head = 0;                                                     \
      cb->tail = 0;                                                     \
                                                                        \
      cb->data = (type *)mirrorbuf_create(                              \
         &(cb->mbuf), cb->size * sizeof(type)                           \
         );                                                             \
      return (cb->data != NULL);                                        \
   };

#define MAGICQ_FREE(name)                                               \
   static inline void name##_free(name##_t * cb)                        \
   {                                                                    \
      mirrorbuf_destroy(&(cb->mbuf));                                   \
   };

#define MAGICQ_FULL(name)                                               \
   static inline bool name##_full(const name##_t * cb)                  \
   {                                                                    \
      return (cb->head == (cb->tail ^ cb->size));                       \
   };

#define MAGICQ_EMPT(name)                                               \
   static inline bool name##_empty(const name##_t * cb)                 \
   {                                                                    \
      return (cb->head == cb->tail);                                    \
   };

#define MAGICQ_TOP(name, type, copyfunc)                                \
   static inline bool name##_top(const name##_t * cb, type * pobj)      \
   {                                                                    \
      copyfunc(&cb->data[cb->head], pobj);                              \
      return true;                                                      \
   };

#define MAGICQ_SIZE(name)                                               \
   static inline size_t name##_size(const name##_t * cb)                \
   {                                                                    \
      return ( cb->tail < cb->head            ) ?                       \
             ( cb->tail + cb->vsiz - cb->head ) :                       \
             ( cb->tail - cb->head            ) ;                       \
   };

#define MAGICQ_PUSH(name, type, copyfunc)                               \
   static inline bool name##_push(name##_t * cb, const type * data)     \
   {                                                                    \
      if (name##_full(cb)) { return false; };                           \
                                                                        \
      copyfunc(data, &(cb->data[cb->tail]));                            \
      cb->tail = (cb->tail + 1) & (cb->vsiz - 1);                       \
                                                                        \
      return true;                                                      \
   };

#define MAGICQ_POP(name, type, copyfunc)                                \
   static inline bool name##_pop(name##_t * cb, type * data)            \
   {                                                                    \
      if (name##_empty(cb)) { return false; };                          \
                                                                        \
      copyfunc(&(cb->data[cb->head]), data);                            \
      cb->head = (cb->head + 1) & (cb->vsiz - 1);                       \
                                                                        \
      return true;                                                      \
   };

#define MAGICQ_PROTOTYPE(name, type, copyfunc)                          \
   MAGICQ_TYPE(name, type);                                             \
   MAGICQ_INIT(name, type);                                             \
   MAGICQ_FREE(name      );                                             \
   MAGICQ_FULL(name      );                                             \
   MAGICQ_EMPT(name      );                                             \
   MAGICQ_SIZE(name      );                                             \
   MAGICQ_PUSH(name, type, copyfunc);                                   \
   MAGICQ_POP (name, type, copyfunc);

#endif