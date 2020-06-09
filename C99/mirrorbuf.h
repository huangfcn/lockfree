#ifndef __MIRROR_BUF_H__
#define __MIRROR_BUF_H__

#ifdef _WIN32

typedef struct mirrorbuf_t
{
   HANDLE          mapf;
   unsigned char * pbuf;
   size_t          bsiz;
} mirrorbuf_t;

#else

typedef struct mirrorbuf_t
{
   unsigned char * pbuf;
   size_t          bsiz;
} mirrorbuf_t;

#endif

#ifdef __cplusplus
extern "C" {
#endif

   void   mirrorbuf_destroy(mirrorbuf_t * map             );
   void * mirrorbuf_create (mirrorbuf_t * map, size_t bsiz);

#ifdef __cplusplus
};
#endif

#endif