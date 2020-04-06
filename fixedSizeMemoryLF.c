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
void* systemMemoryAlloc(size_t size)
{
#ifdef _WIN32
    return (void*)HeapAlloc(GetProcessHeap(), 0, size);
#else
    return (void*)mmap(
        0,
        (size),
        PROT_READ | PROT_WRITE,
        MAP_ANON | MAP_PRIVATE,
        (-1),
        0
    );
#endif
}

void  systemMemoryFree(void* pMem, size_t size)
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
* Control Block Memory Management                                             *
* ========================================================================== */

static volatile lfstack_head_t freeGlobalControlBlockList = { 0 };
static volatile lfstack_head_t globalControlBlockMemoList = { 0 };

static int controlBlockSystemStartup()
{
    lfstack_init_internal(&freeGlobalControlBlockList);
    return (0);
}

static void controlBlockSystemCleanup()
{
    lf_pointer_t * ptmp = NULL;
    while ((ptmp = lfstack_pop_internal(&globalControlBlockMemoList)))
    {
        systemMemoryFree(
            (void *)(ptmp),
            systemMemoryPageSize()
        );
    }
}

static lfstack_node_t* controlBlockAlloc()
{
    /* allocate control block */
    lfstack_node_t* node = (lfstack_node_t*)lfstack_pop_internal(
        &(freeGlobalControlBlockList)
    );
    if (node == NULL) {
        int nTotalSize = systemMemoryPageSize();
        int nBlockSize = sizeof(lfstack_node_t);
        node = (lfstack_node_t*)systemMemoryAlloc(nTotalSize);
        if (node == NULL) { return NULL; }

        /* first node used to manage system memory block itself */
        lfstack_push_internal(&globalControlBlockMemoList, (lf_pointer_t*)node);
        node += 1; nTotalSize -= nBlockSize;

        /* keep one for this time */
        nTotalSize -= nBlockSize;
        for (; nTotalSize >= nBlockSize; nTotalSize -= nBlockSize) {
            lfstack_push_internal(&(freeGlobalControlBlockList), (lf_pointer_t*)node);
            node += 1;
        }
    }
    return node;
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
    fixedSizeMemoryControl* pblk,
    size_t                  unitSizeInBytes
)
{
    size_t blkSizeInBytes;
    size_t pageSizeInBytes, pages;

    size_t hdrsize = sizeof(lf_pointer_t);

    /* unit size should be at least (hdrsize) bytes */
    unitSizeInBytes = (unitSizeInBytes + hdrsize - 1) / hdrsize * hdrsize;

    /*
    * get memory page size, (pages) * (page size)
    */
    pageSizeInBytes = systemMemoryPageSize();
    pages = (unitSizeInBytes + pageSizeInBytes - 1) / pageSizeInBytes;
    blkSizeInBytes = (pages)*pageSizeInBytes;

    /* initialize fixed memory control block */
    pblk->unitSizeInBytes = unitSizeInBytes;
    pblk->blkSizeInBytes = blkSizeInBytes;

    lfstack_init_internal(&(pblk->systemMemoryList));
    lfstack_init_internal(&(pblk->freeList));

    return (0);
}

void fixedSizeMemoryCleanup(
    fixedSizeMemoryControl* pblk
)
{
    lf_pointer_t* ptmp = NULL;
    while ((ptmp = lfstack_pop_internal(&(pblk->systemMemoryList))))
    {
        /* free system memory */
        systemMemoryFree(
            ((void*)(((lfstack_node_t*)ptmp)->valu)),
            pblk->blkSizeInBytes
        );
    }
}

void* fixedSizeMemoryAlloc(
    fixedSizeMemoryControl* pblk
)
{
    /* allocate from freelist first */
    void* ptmp = (void*)lfstack_pop_internal(
        &(pblk->freeList)
    );
    if (ptmp) {
        return ptmp;
    }

    /* increase by allocating from system memory */
    {
        unsigned char* pExtraSystemMemory;

        size_t extraSize;
        size_t unitSize;

        /* load extraSize, unitSize */
        extraSize = pblk->blkSizeInBytes;
        unitSize  = pblk->unitSizeInBytes;

        /* allocate control block */
        lfstack_node_t* node = controlBlockAlloc();
        if (node == NULL) {
            return NULL;
        }

        /* allocate system memory */
        pExtraSystemMemory = (unsigned char*)systemMemoryAlloc(extraSize);
        if (pExtraSystemMemory == NULL)
        {
            return (NULL);
        }

        /* insert into system memory list */
        node->valu = (uint64_t)pExtraSystemMemory;
        lfstack_push_internal(&(pblk->systemMemoryList), (lf_pointer_t*)node);

        /* add extra into free list */
        extraSize -= unitSize;
        for (; extraSize >= unitSize; extraSize -= unitSize)
        {
            lfstack_push_internal(&(pblk->freeList), (lf_pointer_t*)pExtraSystemMemory);
            pExtraSystemMemory += unitSize;
        }

        return pExtraSystemMemory;
    }
}

void fixedSizeMemoryFree(
    fixedSizeMemoryControl* pblk,
    void* p
)
{
    lfstack_push_internal(&(pblk->freeList), (lf_pointer_t*)p);
}

/* ========================================================================== *
* Memory management based on fixed size memory block                          *
* ========================================================================== */

/* from  16 bytes to  512 bytes, increased by  16 bytes
 * from 512 bytes to  16K bytes, increased by 512 bytes
 * from 16K bytes to 512k bytes, increased by 16K bytes
 * otherwise, allocated from system memory
 *
 * the acurate range for (00 - 4k) is (00 - 4064). (4k-32)
 */

static fixedSizeMemoryControl fixedSizeMemoryControl_0016_0512[31];
static fixedSizeMemoryControl fixedSizeMemoryControl_0512_016K[31];
static fixedSizeMemoryControl fixedSizeMemoryControl_016K_512K[32];

#define UB_SEG_0512  ((  1 <<  9) - ( 1 <<  4))
#define UB_SEG_016K  (( 16 << 10) - ( 1 <<  9))
#define UB_SEG_512K  ((512 << 10) - (16 << 10))

#define SHFT_0016_0512    ( 4)
#define SHFT_0512_016K    ( 9)
#define SHFT_016K_512K    (14)

int mmFixedSizeMemoryStartup()
{
    size_t i;

    controlBlockSystemStartup();

    for (i = 0; i < sizeof(fixedSizeMemoryControl_0016_0512) / sizeof(fixedSizeMemoryControl); i++) {
        fixedSizeMemoryStartup(
            fixedSizeMemoryControl_0016_0512 + i,
            (i + 1) << SHFT_0016_0512
        );
    }

    for (i = 0; i < sizeof(fixedSizeMemoryControl_0512_016K) / sizeof(fixedSizeMemoryControl); i++) {
        fixedSizeMemoryStartup(
            fixedSizeMemoryControl_0512_016K + i,
            (i + 1) << SHFT_0512_016K
        );
    }

    for (i = 0; i < sizeof(fixedSizeMemoryControl_016K_512K) / sizeof(fixedSizeMemoryControl); i++) {
        fixedSizeMemoryStartup(
            fixedSizeMemoryControl_016K_512K + i,
            (i + 1) << SHFT_016K_512K
        );
    }

    return (0);
}

void mmFixedSizeMemoryCleanup()
{
    int i;

    for (i = 0; i < sizeof(fixedSizeMemoryControl_0016_0512) / sizeof(fixedSizeMemoryControl); i++) {
        fixedSizeMemoryCleanup(
            fixedSizeMemoryControl_0016_0512 + i
        );
    }

    for (i = 0; i < sizeof(fixedSizeMemoryControl_0512_016K) / sizeof(fixedSizeMemoryControl); i++) {
        fixedSizeMemoryCleanup(
            fixedSizeMemoryControl_0512_016K + i
        );
    }

    for (i = 0; i < sizeof(fixedSizeMemoryControl_016K_512K) / sizeof(fixedSizeMemoryControl); i++) {
        fixedSizeMemoryCleanup(
            fixedSizeMemoryControl_016K_512K + i
        );
    }

    controlBlockSystemCleanup();
}

void* mmFixedSizeMemoryAllocAcc(size_t* psize)
{
    size_t nSize = *psize;
    size_t nBlock;

    void* pbuf;

    /* trivial case */
    if (nSize == 0) {
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

    /* 16k bytes - 512k */
    if (nSize > UB_SEG_016K)
    {
        nBlock = (nSize + (1 << SHFT_016K_512K) - 1) >> SHFT_016K_512K;

        *psize = (nBlock << SHFT_016K_512K);

        pbuf = fixedSizeMemoryAlloc(
            fixedSizeMemoryControl_016K_512K + (nBlock - 1)
        );
    }

    /* 512 bytes - 16K */
    else if (nSize > UB_SEG_0512)
    {
        nBlock = (nSize + (1 << SHFT_0512_016K) - 1) >> SHFT_0512_016K;

        *psize = (nBlock << SHFT_0512_016K);

        pbuf = fixedSizeMemoryAlloc(
            fixedSizeMemoryControl_0512_016K + (nBlock - 1)
        );
    }

    /* 16 bytes - 512 bytes */
    else {
        nBlock = (nSize + (1 << SHFT_0016_0512) - 1) >> SHFT_0016_0512;

        *psize = (nBlock << SHFT_0016_0512);

        pbuf = fixedSizeMemoryAlloc(
            fixedSizeMemoryControl_0016_0512 + (nBlock - 1)
        );
    }

    return (pbuf);
}

void* mmFixedSizeMemoryAlloc(size_t nsize)
{
    return mmFixedSizeMemoryAllocAcc(&nsize);
}

void mmFixedSizeMemoryFree(void* buf, size_t nSize)
{
    size_t nBlock;

    /* trivial case */
    if ((nSize == 0) || (buf == NULL)) {
        return;
    }

    /* beyond our limit, free to system */
    if (nSize > (512 * 1024))
    {
        systemMemoryFree(buf, nSize);
        return;
    }

    /* 16k bytes - 512k */
    if (nSize > UB_SEG_016K)
    {
        nBlock = (nSize + (1 << SHFT_016K_512K) - 1) >> SHFT_016K_512K;
        fixedSizeMemoryFree(
            fixedSizeMemoryControl_016K_512K + (nBlock - 1),
            buf
        );
    }

    /* 512 bytes - 16K */
    else if (nSize > UB_SEG_0512)
    {
        nBlock = (nSize + (1 << SHFT_0512_016K) - 1) >> SHFT_0512_016K;
        fixedSizeMemoryFree(
            fixedSizeMemoryControl_0512_016K + (nBlock - 1),
            buf
        );
    }

    /* 0 bytes - 512 bytes */
    else
    {
        nBlock = (nSize + (1 << SHFT_0016_0512) - 1) >> SHFT_0016_0512;
        fixedSizeMemoryFree(
            fixedSizeMemoryControl_0016_0512 + (nBlock - 1),
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

void* slab_malloc(size_t size)
{
    size_t blksize = (size + sizeof(slab_t));

    slab_t* pblk = (slab_t*)mmFixedSizeMemoryAllocAcc(&blksize);
    if (pblk == NULL) { return (NULL); }

    pblk->buflen = blksize;
    pblk->used = size;

    /* if memory check required
    */
    // pblk->_d[0] = 0XDEADBEAF;
    // pblk->_d[1] = 0XFAEBDAED;
    // pblk->_d[2] = 0X5555AAAA;
    // pblk->_d[3] = 0XAAAA5555;
    return (void*)(pblk + 1);
}

void slab_free(void* pmem)
{
    if (pmem == NULL) { return; }

    /* if memory check required
    */
    // if (((((slab_t *)(pmem))-1)->_d[0] != 0XDEADBEAF) ||
    //     ((((slab_t *)(pmem))-1)->_d[1] != 0XFAEBDAED) ||
    //     ((((slab_t *)(pmem))-1)->_d[2] != 0X5555AAAA) ||
    //     ((((slab_t *)(pmem))-1)->_d[3] != 0XAAAA5555) ){
    //    fprintf(stderr, "\nslab_free: ==!!ERROR!!==, memory sanity check failed @ %016X!\n", pmem);
    // };

    mmFixedSizeMemoryFree(
        (((slab_t*)(pmem)) - 1),
        (((slab_t*)(pmem)) - 1)->buflen
    );
}

void* slab_realloc(void* pmem, size_t size)
{
    /* special case */
    if (pmem == NULL)
    {
        /* allocate new memory (allocate more for possibly next realloc)
        */
        void* pbuf = slab_malloc((size) << 2);
        if (pbuf == NULL)
        {
            return (NULL);
        }

        /* fix used (malloc will set it to (size << 2)) */
        (((slab_t*)(pbuf)) - 1)->used = size;
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
        if ((size + sizeof(slab_t)) <= ((((slab_t*)(pmem)) - 1)->buflen))
        {
            (((slab_t*)(pmem)) - 1)->used = size;
            return (pmem);
        }

        /* allocate new memory (allocate more for possibly next realloc)
        */
        void* pbuf = slab_malloc((size) << 2);
        if (pbuf == NULL)
        {
            slab_free(pmem);
            return (NULL);
        }

        /* copy memory & free previous memory
        */
        memcpy(pbuf, pmem, ((((slab_t*)(pmem)) - 1)->used));
        slab_free(pmem);

        /* fix used (malloc will set it to (size << 2)) */
        (((slab_t*)(pbuf)) - 1)->used = size;
        return (pbuf);
    }
}

void* slab_calloc(size_t blksize, size_t numblk)
{
    size_t size = blksize * numblk;
    void* pmem = slab_malloc(size);
    if (pmem == NULL)
    {
        return (NULL);
    }

    memset(pmem, 0, size);

    return (pmem);
}
/////////////////////////////////////////////////////////////////////////