/* ============================================================================ *
* author: Feng Huang                                                            *
* email : huangfcn@gmail.com                                                    *
*                                                                               *
* fixed size memory blocks routines (startup, cleanup, alloc, free)             *
*    create a free list with fixed size memory block                            *
*    allocation of a memory block becomes getting a block from free list        *
*    free       of a memory block becomes putting a block into free list        *
*                                                                               *
* general memory malloc/free through fixed size memory blocks                   *
*    maintain fixed size memory blocks with different size                      *
*    allocation becomes getting a block from corresponding free list            *
*    free       becomes putting a block into corresponding free list            *
*                                                                               *
*       1 bytes -   240   bytes, maintained in blocks aligned to  16 bytes      *
*     241 bytes -  3,840  bytes, maintained in blocks aligned to 256 bytes      *
*   3,841 bytes -  61,440 bytes, maintained in blocks aligned to  4k bytes      *
*  61,441 bytes - 524,288 bytes, maintained in blocks aligned to 64k bytes      *
*     otherwise                , call system memory management calls            *
* ============================================================================ */

/* ============================================================================ *
Except where otherwise noted, all of the source code in the package is
copyrighted by Feng Huang.

Copyright (C) 2009-2019 Feng Huang. All rights reserved.

This software is provided "as-is," without any express or implied warranty.
In no event shall the author be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter and redistribute it,
provided that the following conditions are met:

1. All redistributions of source code files must retain all copyright
   notices that are currently in place, and this list of conditions without
   modification.

2. All redistributions in binary form must retain all occurrences of the
   above copyright notice and web site addresses that are currently in
   place (for example, in the About boxes).

3. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software to
   distribute a product, an acknowledgment in the product documentation
   would be appreciated but is not required.

4. Modified versions in source or binary form must be plainly marked as
   such, and must not be misrepresented as being the original software.
* ============================================================================ */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef _WIN32
/* mmap / mmunmap */
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef _WIN32
/* HeapAlloc / HeapFree */
#include <Windows.h>
#endif

#include "lffifo.h"
#include "fixedSizeMemoryLF.h"

/* ========================================================================== *
* System Level Memory Allocation, (Windows / Unix)                            *
* ========================================================================== */
void * systemMemoryAlloc(size_t size)
{
#ifdef _WIN32
   return (void *)HeapAlloc(GetProcessHeap(), 0, size);
#else
   return (void *)mmap(
      0,
      (size),
      PROT_READ | PROT_WRITE,
      MAP_ANON  | MAP_PRIVATE,
      (-1),
      0
   );
#endif
}

void  systemMemoryFree(void *pMem, size_t size)
{
#ifdef _WIN32
   HeapFree(GetProcessHeap(), 0, pMem);
#else
   munmap(pMem, size);
#endif
}

static inline int systemMemoryPageSize()
{
#ifdef _WIN32
   return (4096 * 4);
#else
   return getpagesize();
#endif
}

/* ========================================================================== *
* System Memory Blocks Management                                             *
*                                                                             *
* (1) define as thread specific variables                                     *
* (2) Using Mutex                                                             *
* ========================================================================== */

/* ========================================================================== *
* Fixed Size Memory block management                                          *
* ========================================================================== */
int fixedSizeMemoryStartup(
   fixedSizeMemoryControl * pblk,
   size_t                   unitSizeInBytes
)
{
   size_t blkSizeInBytes;
   size_t blkSizeInUnits;
   size_t pageSizeInBytes, pages;

   size_t hdrsize = sizeof(lf_pointer_t);

   /* unit size should be at least (hdrsize) bytes */
   unitSizeInBytes = (unitSizeInBytes + hdrsize - 1) / hdrsize * hdrsize;

   /*
   * get memory page size, (pages) * (page size)
   */
   pageSizeInBytes = systemMemoryPageSize();
   pages           = (unitSizeInBytes + pageSizeInBytes - 1) / pageSizeInBytes;
   blkSizeInBytes  = (pages) * pageSizeInBytes;

   /* initialize fixed memory control block */
   pblk->unitSizeInBytes  = unitSizeInBytes;
   pblk->blkSizeInBytes   = blkSizeInBytes;

   lfstack_init_internal(&(pblk->systemMemoryList));
   lfstack_init_internal(&(pblk->freeList        ));

   return (0);
}

void fixedSizeMemoryCleanup(
   fixedSizeMemoryControl * pblk
)
{
   lf_pointer_t * ptmp = NULL;
   while (ptmp = lfstack_pop_internal(&(pblk->systemMemoryList)))
   {
      /* free system memory */
      systemMemoryFree(
         ((void *)(((lfstack_node_t *)ptmp)->valu)), 
         pblk->blkSizeInBytes
      );

      /* free the control block */
      _aligned_free(ptmp);
   }
}

void * fixedSizeMemoryAlloc(
   fixedSizeMemoryControl * pblk
)
{
   /* try to allocate from freelist */
   void * ptmp = (void *)lfstack_pop_internal(
      &(pblk->freeList)
   );
   if (ptmp)
   {
      return ptmp;
   }

   /* increase by allocating from system memory */
   {
      unsigned char   * pExtraSystemMemory;

      size_t            extraSize;
      size_t            unitSize;

      /* load extraSize, unitSize */
      extraSize = pblk->blkSizeInBytes;
      unitSize  = pblk->unitSizeInBytes;

      /* allocate control block */
      lfstack_node_t * node = (lfstack_node_t *)_aligned_malloc(
         sizeof(lfstack_node_t), 16
      );
      if (node == NULL)
      {
         return NULL;
      }

      /* allocate system memory */
      pExtraSystemMemory = (unsigned char *)systemMemoryAlloc(extraSize);
      if (pExtraSystemMemory == NULL)
      {
         _aligned_free(node);
         return (NULL);
      }

      /* insert into system memory list */
      node->valu = (uint64_t)pExtraSystemMemory;
      lfstack_push_internal(&(pblk->systemMemoryList), (lf_pointer_t *)node);

      // pExtraSystemMemory += sizeof(lf_pointer_t);
      // extraSize          -= sizeof(lf_pointer_t);

      /* add extra into free list */
      extraSize -= unitSize;
      for ( ; extraSize >= unitSize; extraSize -= unitSize )
      {
         lfstack_push_internal(&(pblk->freeList), pExtraSystemMemory);
         pExtraSystemMemory += unitSize;
      }

      return pExtraSystemMemory;
   }
}

void fixedSizeMemoryFree(
   fixedSizeMemoryControl * pblk,
   void                   * p
)
{
   lfstack_push_internal(&(pblk->freeList), (lf_pointer_t *)p);
}

/* ========================================================================== *
* Memory management based on fixed size memory block                          *
* ========================================================================== */

/* from 16  bytes to  256 bytes, increased by 16  bytes
 * from 256 bytes to   4k bytes, increased by 256 bytes
 * from  4k bytes to  64k bytes, increased by  4k bytes
 * from 64k bytes to 512K bytes, increased by 64k bytes
 * otherwise, allocated from system memory
 *
 * the acurate range for 256-4k is 256-3840 (4k-256)
 */

static fixedSizeMemoryControl fixedSizeMemoryControl_016_240[15];
static fixedSizeMemoryControl fixedSizeMemoryControl_256_04k[15];
static fixedSizeMemoryControl fixedSizeMemoryControl_04k_64k[15];
static fixedSizeMemoryControl fixedSizeMemoryControl_64k_512k[8];

int mmFixedSizeMemoryStartup()
{
   int i;

   for (i = 0; i < 15; i++)
   {
      fixedSizeMemoryStartup(
         fixedSizeMemoryControl_016_240 + i,
         (i + 1) << 4
      );
   }

   for (i = 0; i < 15; i++)
   {
      fixedSizeMemoryStartup(
         fixedSizeMemoryControl_256_04k + i,
         (i + 1) << 8
      );
   }

   for (i = 0; i < 15; i++)
   {
      fixedSizeMemoryStartup(
         fixedSizeMemoryControl_04k_64k + i,
         (i + 1) << 12
      );
   }

   for (i = 0; i <  8; i++)
   {
      fixedSizeMemoryStartup(
         fixedSizeMemoryControl_64k_512k + i,
         (i + 1) << 16
      );
   }

   return (0);
}

void mmFixedSizeMemoryCleanup()
{
   int i;

   for (i = 0; i < 15; i++)
   {
      fixedSizeMemoryCleanup(
         fixedSizeMemoryControl_016_240 + i
      );
   }

   for (i = 0; i < 15; i++)
   {
      fixedSizeMemoryCleanup(
         fixedSizeMemoryControl_256_04k + i
      );
   }

   for (i = 0; i < 15; i++)
   {
      fixedSizeMemoryCleanup(
         fixedSizeMemoryControl_04k_64k + i
      );
   }

   for (i = 0; i < 8; i++)
   {
      fixedSizeMemoryCleanup(
         fixedSizeMemoryControl_64k_512k + i
      );
   }
}

void * mmFixedSizeMemoryAllocAcc (size_t * psize)
{
   size_t nSize = *psize;
   size_t nBlock;

   void * pbuf;

   /* trivial case */
   if (nSize == 0)
   {
      return (NULL);
   }

   /* beyond our limit, allocate from system */
   if (nSize > (512 * 1024))
   {
      size_t pageSize = systemMemoryPageSize();
      nSize = (nSize + pageSize - 1) / pageSize * pageSize;

      *psize = nSize;
      return systemMemoryAlloc(nSize);
   }

   /* 64k bytes - 512k */
   if ( nSize > ((64 * 1024) - (4 * 1024)) )
   {
      nBlock = (nSize + 64 * 1024 - 1) >> 16;

      *psize = (nBlock << 16);

      pbuf = fixedSizeMemoryAlloc(
         fixedSizeMemoryControl_64k_512k + (nBlock - 1)
      );
   }

   /* 3841 bytes - 64k */
   else if ( nSize > ((4 * 1024) - 256) )
   {
      nBlock = (nSize + 4 * 1024 - 1) >> 12;

      *psize = (nBlock << 12);

      pbuf = fixedSizeMemoryAlloc(
         fixedSizeMemoryControl_04k_64k + (nBlock - 1)
      );
   }

   /* 241 bytes - 3840 bytes */
   else if ( nSize > 241 )
   {
      nBlock = (nSize + 256 - 1) >> 8;

      *psize = (nBlock << 8);

      pbuf = fixedSizeMemoryAlloc(
         fixedSizeMemoryControl_256_04k + (nBlock - 1)
      );
   }

   /* 1 bytes - 240 bytes */
   else 
   {
      nBlock = (nSize + 16 - 1) >> 4;

      *psize = (nBlock << 4);

      pbuf = fixedSizeMemoryAlloc(
         fixedSizeMemoryControl_016_240 + (nBlock - 1)
      );
   }

   return (pbuf);
}

void * mmFixedSizeMemoryAlloc(size_t nsize)
{
   return mmFixedSizeMemoryAllocAcc(&nsize);
}

void mmFixedSizeMemoryFree(void *buf, size_t nSize)
{
   size_t nBlock;

   /* trivial case */
   if (nSize == 0){
      return;
   }

   /* beyond our limit, free to system */
   if (nSize > (512 * 1024))
   {
      systemMemoryFree(buf, nSize);
      return;
   }

   /* 64k bytes - 512k */
   if ( nSize > ((64 * 1024) - (4 * 1024)) )
   {
      nBlock = (nSize + 64 * 1024 - 1) >> 16;
      fixedSizeMemoryFree(
         fixedSizeMemoryControl_64k_512k + (nBlock - 1),
         buf
      );
   }

   /* 3841 bytes - 64k */
   else if ( nSize > ((4 * 1024) - 256) )
   {
      nBlock = (nSize + 4 * 1024 - 1) >> 12;
      fixedSizeMemoryFree(
         fixedSizeMemoryControl_04k_64k + (nBlock - 1),
         buf
      );
   }

   /* 241 bytes - 3840 bytes */
   else if ( nSize > 241 )
   {
      nBlock = (nSize + 256 - 1) >> 8;
      fixedSizeMemoryFree(
         fixedSizeMemoryControl_256_04k + (nBlock - 1),
         buf
      );
   }

   /* 1 bytes - 240 bytes */
   else 
   {
      nBlock = (nSize + 16 - 1) >> 4;
      fixedSizeMemoryFree(
         fixedSizeMemoryControl_016_240 + (nBlock - 1),
         buf
      );
   }
}

/////////////////////////////////////////////////////////////////////////
// GENERAL PURPOSE MEMORY MANAGEMENT (malloc/free/realloc/calloc)      //
/////////////////////////////////////////////////////////////////////////
typedef struct _slab_t
{
   size_t   buflen;
   size_t   used;

   /* if memory sanity check required
   */
   // uint32_t __d[4];
} slab_t;

void * slab_malloc(size_t size)
{
   size_t blksize = (size + sizeof(slab_t));

   slab_t * pblk = (slab_t *)mmFixedSizeMemoryAllocAcc(&blksize);
   if (pblk == NULL){return (NULL);}

   pblk->buflen = blksize;
   pblk->used   = size;

   /* if memory check required
   */
   // pblk->_d[0] = 0XDEADBEAF;
   // pblk->_d[1] = 0XFAEBDAED;
   // pblk->_d[2] = 0X5555AAAA;
   // pblk->_d[3] = 0XAAAA5555;
   return (void *)(pblk + 1);
}

void slab_free(void * pmem)
{
   if (pmem == NULL){return;}

   /* if memory check required
   */
   // if (((((slab_t *)(pmem))-1)->_d[0] != 0XDEADBEAF) ||
   //     ((((slab_t *)(pmem))-1)->_d[1] != 0XFAEBDAED) ||
   //     ((((slab_t *)(pmem))-1)->_d[2] != 0X5555AAAA) ||
   //     ((((slab_t *)(pmem))-1)->_d[3] != 0XAAAA5555) ){
   //    fprintf(stderr, "\nslab_free: ==!!ERROR!!==, memory sanity check failed @ %016X!\n", pmem);
   // };

   mmFixedSizeMemoryFree(
      (((slab_t *)(pmem))-1), 
      (((slab_t *)(pmem))-1)->buflen
   );
}

void * slab_realloc(void * pmem, size_t size)
{
   /* special case */
   if (pmem == NULL)
   {
      /* allocate new memory (allocate more for possibly next realloc)
      */
      void * pbuf = slab_malloc((size) << 2);
      if (pbuf == NULL)
      {
         return (NULL);
      }

      /* fix used (malloc will set it to (size << 2)) */
      (((slab_t *)(pbuf))-1)->used = size;
      return (pbuf);
   }

   else
   {
      /* if memory check required
      */
      // if (((((slab_t *)(pmem))-1)->_d[0] != 0XDEADBEAF) ||
      //     ((((slab_t *)(pmem))-1)->_d[1] != 0XFAEBDAED) ||
      //     ((((slab_t *)(pmem))-1)->_d[2] != 0X5555AAAA) ||
      //     ((((slab_t *)(pmem))-1)->_d[3] != 0XAAAA5555) ){
      //    fprintf(stderr, "\nslab_realloc: ==!!ERROR!!==, memory sanity check failed @ %016X!\n", pmem);
      // };

      /* check memory size */
      if ((size + sizeof(slab_t)) <= ((((slab_t *)(pmem))-1)->buflen))
      {
         (((slab_t *)(pmem))-1)->used = size;
         return (pmem);
      }

      /* allocate new memory (allocate more for possibly next realloc)
      */
      void * pbuf = slab_malloc((size) << 2);
      if (pbuf == NULL)
      {
         slab_free(pmem);
         return (NULL);
      }

      /* copy memory & free previous memory 
      */
      memcpy(pbuf, pmem, ((((slab_t *)(pmem))-1)->used));
      slab_free(pmem);

      /* fix used (malloc will set it to (size << 2)) */
      (((slab_t *)(pbuf))-1)->used = size;
      return (pbuf);
   }
}

void * slab_calloc(size_t blksize, size_t numblk)
{
   size_t size = blksize * numblk;
   void * pmem = slab_malloc(size);
   if (pmem == NULL)
   {
      return (NULL);
   }

   memset(pmem, 0, size);

   return (pmem);
}
/////////////////////////////////////////////////////////////////////////