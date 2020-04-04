#include <stdint.h>
#include <atomic>

#ifndef __LOCKFREE_STRUCT_H__
#define __LOCKFREE_STRUCT_H__

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
      std::atomic<lfstack_head_t> worklist;
      uint64_t pad1[6];

      std::atomic<uint64_t>       size;
      uint64_t pad2[7];

   } lfstack_t;
   //////////////////////////////////////////////////////////////

   //////////////////////////////////////////////////////////////
   /* lock-free queue                                          */
   //////////////////////////////////////////////////////////////
   typedef lf_node_t    lffifo_node_t;
   typedef lf_pointer_t lffifo_head_t;

   typedef struct {
      std::atomic<lffifo_head_t> tail_;
      uint64_t pad1[6];

      std::atomic<lffifo_head_t> head_;
      uint64_t pad2[6];

      std::atomic<uint64_t>      size;
      uint64_t pad3[7];
   } lffifo_t;
   //////////////////////////////////////////////////////////////

#endif
