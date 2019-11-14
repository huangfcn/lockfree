#include <stdint.h>

#ifndef __LOCKFREE_STRUCT_H__
#define __LOCKFREE_STRUCT_H__

#ifdef __cplusplus
extern "C" {
#endif

   struct lf_node;
   typedef struct lf_node lf_node_t;

   struct lf_node
   {
      lf_node_t * node;
      uint64_t    aba_;
      uint64_t    valu;
      uint64_t    padd;
   };

   typedef struct lf_pointer
   {
      struct lf_pointer * node;
      uint64_t    aba_;
   } lf_pointer_t;

   typedef lf_node_t    lfstack_node_t;
   typedef lf_pointer_t lfstack_head_t;

   typedef struct {
      volatile lfstack_head_t worklist;
      uint64_t _d1[6];

      volatile size_t size;
   } lfstack_t;

   typedef lf_node_t    lffifo_node_t;
   typedef lf_pointer_t lffifo_head_t;

   typedef struct {
      volatile lffifo_head_t tail_;
      uint64_t pad1[6];

      volatile lffifo_head_t head_;
      uint64_t pad2[6];

      volatile size_t size;
   } lffifo_t;
   
#ifdef __cplusplus
};
#endif

#endif
