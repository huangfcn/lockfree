/* ============================================================================ *
 * author: Feng Huang                                                           *
 * email : huangfcn@gmail.com                                                   *
 *                                                                              *
 * fixed size memory block routines (startup, cleanup, alloc, free)             *
 *    create a free list with fixed size memory block                           *
 *    allocation of a memory block becomes getting a block from free list       *
 *    free       of a memory block becomes putting a block into free list       *
 *                                                                              *
 * general memory malloc/free through fixed size memory blocks                  *
 *    maintain fixed size memory blocks with different size                     *
 *    allocation becomes getting a block from corresponding free list           *
 *    free       becomes putting a block into corresponding free list           *
 *                                                                              *
 *       1 bytes -   240   bytes, mainained in blocks aligned to  16 bytes      *
 *     241 bytes -  3,840  bytes, mainained in blocks aligned to 256 bytes      *
 *   3,841 bytes -  61,440 bytes, mainained in blocks aligned to  4k bytes      *
 *  61,441 bytes - 524,288 bytes, mainained in blocks aligned to 64k bytes      *
 *     otherwise                , call system memory management calls           *
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
#include "lf_struct.hpp"

#ifndef _LOCKFREE_FIXED_SIZE_MEMORY_MANAGEMENT_H_
#define _LOCKFREE_FIXED_SIZE_MEMORY_MANAGEMENT_H_

   typedef struct _fixedSizeMemoryControl
   {
      std::atomic<lfstack_head_t> systemMemoryList;
      std::atomic<lfstack_head_t> freeList;

      size_t  unitSizeInBytes;
      size_t  blkSizeInBytes;
   } fixedSizeMemoryControl;

   /* ========================================================================== *
    * System Level Memory Allocation, (Windows / Unix)                           *
    * Linux:   mmap      / munmap                                                *
    * Windows: HeapAlloc / HeapFree                                              *
    * ========================================================================== */
   void * systemMemoryAlloc(size_t size);
   void   systemMemoryFree(void *pMem, size_t size);

   /* ========================================================================== *
    * Fixed Size Memory block management                                         *
    * ========================================================================== */
   int  fixedSizeMemoryStartup
   (
      fixedSizeMemoryControl * pblk,
      size_t                   unitSizeInBytes
   );

   void * fixedSizeMemoryAlloc
   (
      fixedSizeMemoryControl * pblk
   );

   void fixedSizeMemoryFree
   (
      fixedSizeMemoryControl * pblk,
      void                   * p
   );

   void fixedSizeMemoryCleanup
   (
      fixedSizeMemoryControl * pblk
   );

   /* ========================================================================== *
    * Memory management based on fixed size memory block                         *
    * ========================================================================== */
   int  mmFixedSizeMemoryStartup();
   void mmFixedSizeMemoryCleanup();

   void * mmFixedSizeMemoryAlloc(size_t nsize);
   void   mmFixedSizeMemoryFree (void * buf, size_t size);

   /* identical to mmFixedSizeMemoryAlloc, 
      return accurate size allocated.
    */
   void * mmFixedSizeMemoryAllocAcc(size_t * psize);

   /* ========================================================================== *
    * GENERAL PURPOSE MEMORY MANAGEMENT (malloc/free/realloc/calloc)             *
    * ========================================================================== */
   void * slab_malloc (size_t size);
   void   slab_free   (void * _pblk);
   void * slab_realloc(void * pmem, size_t size);
   void * slab_calloc (size_t blksize, size_t numblk);
   ///////////////////////////////////////////////////////////////////////////////

#endif //_FIXED_SIZE_MEMORY_MANAGEMENT_H_
