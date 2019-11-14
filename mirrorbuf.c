#ifdef _WIN32
#include <Windows.h>
#include "mirrorbuf.h"

void mirrorbuf_destroy(mirrorbuf_t * map)
{
   if (map->pbuf)
   {
      UnmapViewOfFile(map->pbuf            );
      UnmapViewOfFile(map->pbuf + map->bsiz);
   }

   if (map->mapf)
   {
      CloseHandle(map->mapf);
   }

   map->mapf = map->pbuf = NULL;
}

void * mirrorbuf_create(mirrorbuf_t * map, size_t bsiz)
{
   map->mapf = map->pbuf = NULL;

   // is ring_size a multiple of 64k? if not, this won't ever work!
   if ((bsiz & 0xffff) != 0)
      return NULL;

   HANDLE mapf = CreateFileMapping(
      INVALID_HANDLE_VALUE,
      NULL,
      PAGE_READWRITE,
      0,
      bsiz*2,
      L"Mapping");

   if (mapf == NULL)
   {
      return (NULL);
   }

   BYTE *pBuf = (BYTE*)MapViewOfFile(mapf,
      FILE_MAP_ALL_ACCESS,
      0,                   
      0,                   
      bsiz);

   BYTE * pBuf2 = (BYTE *)MapViewOfFileEx(mapf,
      FILE_MAP_ALL_ACCESS,
      0,                   
      0,                   
      bsiz,
      pBuf+bsiz);

   if ((pBuf == NULL) || (pBuf2 == NULL))
   {
      CloseHandle(mapf);
      return NULL;
   }

   /* setup map */
   map->mapf = mapf;
   map->pbuf = pBuf;
   map->bsiz = bsiz;

   return ((void *)pBuf);
}

#else
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mirrorbuf.h"

/** OSX needs some help here */
#ifndef MAP_ANONYMOUS
#  define MAP_ANONYMOUS MAP_ANON
#endif

void * mirrorbuf_create(mirrorbuf_t * map, size_t bsiz)
{
   char path[] = "/tmp/SPSC-XXXXXX";
   int fd, status;
   unsigned char * addr;
   unsigned char * data;

   map->pbuf = NULL;

   fd = mkstemp(path);
   if (fd < 0)
   {
      return (NULL);
   }

   status = unlink(path);
   if (status)
   {
      return (NULL);
   }

   status = ftruncate(fd, bsiz);
   if (status)
   {
      return (NULL);
   }

   /* create the array of data */
   data = (unsigned char *)mmap(
      NULL,
      bsiz << 1,
      PROT_NONE,
      MAP_ANONYMOUS | MAP_PRIVATE,
      -1,
      0
   );
   if (data == MAP_FAILED)
   {
      return (NULL);
   }

   addr = (unsigned char *)mmap(
      data,
      bsiz,
      PROT_READ | PROT_WRITE,
      MAP_FIXED | MAP_SHARED,
      fd,
      0
   );
   if (addr != data)
   {
      return (NULL);
   }

   addr = (unsigned char *)mmap(
      data + bsiz,
      bsiz,
      PROT_READ | PROT_WRITE,
      MAP_FIXED | MAP_SHARED,
      fd,
      0
   );
   if (addr != (data + bsiz))
   {
      return (NULL);
   }

   status = close(fd);
   if (status)
   {
      return (NULL);
   }

   map->pbuf = data;
   map->bsiz = bsiz;
   return ((void *)data);
}

void mirrorbuf_destroy(mirrorbuf_t * map)
{
   if (map->pbuf)
   {
      munmap(map->pbuf, map->bsiz);
   }
   map->pbuf = NULL;
}

#endif
