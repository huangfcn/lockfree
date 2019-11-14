#include <stdio.h>
#include <time.h>
#include <Windows.h>

#include "rbq.h"
#include "lffifo.h"
#include "fixedSizeMemoryLF.h"

#define LIMIT	5000000

#define MAXTHREADS	8
#define MAXITER      8

typedef struct pont {
   long 	limit;
   long	stopped;
   long	duration;
} pont;

#define pile lffifo_t

#define INIT(f)      lffifo_init((f), 16)
#define FREE(f)      lffifo_free((f))

#define PUSH(f, val) lffifo_push((f), (val))
#define POP(f)       lffifo_pop(f)

#define SIZE(f)      lffifo_size(f)

/*
*	Global variables
*/
pile	gstack;
int	tbl[MAXITER * MAXTHREADS];
int   nFinished = 0;

int64_t totSum = 0;

static inline void recvi(int64_t d)
{
#ifdef _WIN32
   InterlockedXor64(&totSum, (+d));
   // InterlockedAdd64(&totSum, (+d));
#else
   __sync_fetch_and_add(&totSum, (+d));
#endif
}

static inline void posti(int64_t d)
{
#ifdef _WIN32
   InterlockedXor64(&totSum, (+d));
   // InterlockedAdd64(&totSum, (-d));
#else
   __sync_fetch_and_sub(&totSum, (+d));
#endif
}

// init stack with cells
void initstack (pile *f)
{
   INIT(f);
}

long hybrid (long n)
{
   int64_t r;
   int i; clock_t t;

   t = clock();
   while (n--) {
      for (i = 0; i < MAXITER; i++) 
      {
         // int64_t t2 = (n + 2);
         time_t t2 = time(NULL);
         // clock_t t2 = clock();
         // int64_t t2 = rand();
         while (!PUSH(&gstack, (void *)(t2)));
         posti(t2);
      }
      for (i = 0; i < MAXITER; i++) {
         while (!(r = (int64_t)(POP(&gstack))));
         recvi(r);
      }
   }
   return clock() - t;
}

long producer (long n)
{
   clock_t t;
   n = n * MAXITER;

   t = clock();
   while (n--) {
      // int64_t t2 = (n + 2);
      time_t t2 = time(NULL);
      // clock_t t2 = clock();
      // int64_t t2 = rand();
      while (!PUSH(&gstack, (void *)(t2)));
      posti(t2);
   }
   return clock() - t;
}

long consumer (long n)
{
   clock_t t;
   n = n * MAXITER;

   int64_t r;

   t = clock();
   while (n--) {
      while (!(r = (int64_t)POP(&gstack)));
      recvi(r);
   }
   return clock() - t;
}

DWORD WINAPI hybridthread(void * pp)
{
   pont* p = (pont*) pp;
   p->duration = hybrid(p->limit);
   p->stopped = 1;
   return 0;
}

DWORD WINAPI producerthread(void * pp)
{
   pont* p = (pont*) pp;
   p->duration = producer(p->limit);
   p->stopped = 1;
   return 0;
}

DWORD WINAPI consumerthread(void * pp)
{
   pont* p = (pont*) pp;
   p->duration = consumer(p->limit);
   p->stopped = 1;
   return 0;
}

//-----------------------------------------------------------------
void bench (int max)
{
   DWORD fils[MAXTHREADS];

   pont  bridge_p[MAXTHREADS]; 
   pont  bridge_c[MAXTHREADS]; 
   pont  bridge_h[MAXTHREADS]; 

   long   i, end, th;
   double perf = 0;

   for (th = 1; th <= max; ++th)
   {
      srand(time(NULL));

      initstack (&gstack);
      totSum = 0;
      printf("threads count:\t %ld \t", th); fflush (stdout);
      for (i = 0; i < th; i++)
      {
         bridge_p[i].limit    = LIMIT;
         bridge_p[i].stopped  = 0;
         bridge_p[i].duration = 0;

         bridge_c[i].limit    = LIMIT;
         bridge_c[i].stopped  = 0;
         bridge_c[i].duration = 0;

         CreateThread(
            NULL,
            0L,
            producerthread,
            &bridge_p[i],
            0L,
            &(fils[i])
         );
         
         CreateThread(
            NULL,
            0L,
            consumerthread,
            &bridge_c[i],
            0L,
            &(fils[i])
         );
         
         bridge_h[i].limit    = LIMIT;
         bridge_h[i].stopped  = 0;
         bridge_h[i].duration = 0;
         
         CreateThread(
            NULL,
            0L,
            hybridthread,
            &bridge_h[i],
            0L,
            &(fils[i])
         );

      }

      nFinished = 0;
      do {
         Sleep(1);
         end = 1; 
         int nn  = 0;
         for (i=0; i<th; i++) {
            end &= bridge_p[i].stopped; 
            end &= bridge_c[i].stopped;
            end &= bridge_h[i].stopped;

            nn += (bridge_p[i].stopped ? (1) : (0));
            nn += (bridge_c[i].stopped ? (1) : (0));
            nn += (bridge_h[i].stopped ? (1) : (0));
         }
         if (nFinished != nn)
         {
            // printf("%d tasks finished.\n", nn);
            nFinished = nn;

            // if (nFinished >= ((MAXTHREADS * 3) - 1))
            // {
            //    printf("remain data in queue = %d\n", SIZE(&gstack));
            // }
         }
      } while ( end == 0 );

      for (i = 0; i < th; i++) { 
         perf += bridge_p[i].duration;
      }
      perf /= th;

      perf /= th * LIMIT * MAXITER;
      perf *= 1000000 / CLOCKS_PER_SEC;
      printf (" totSum = %lld, perf (in us per pop/push):\t %2f\n", totSum, perf); fflush (stdout);

      FREE(&gstack);
   }
}

int main()
{
   mmFixedSizeMemoryStartup();

   printf("\n-------- Lock free fifo stack bench ----------\n");
   bench (MAXTHREADS);

   mmFixedSizeMemoryCleanup();

   return 0;
}