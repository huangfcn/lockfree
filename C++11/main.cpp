#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#ifdef _WIN32
#include <Windows.h>
#define THRRET  DWORD WINAPI

static inline int sleep(int sec)
{
    Sleep(sec);
    return (0);
}
#else
#include <unistd.h>
#include <pthread.h>
#define THRRET  void *
#endif

/* MODE 0: SPSC RING BUFFER QUEUE
   MODE 1: MPMC RING BUFFER QUEUE
   MODE 2: LOCK FREE STACK
   MODE 3: LOCK FREE FIFO (MSQUE)
*/
#define TESTMODE     1
#define MAXTHREADS   8
#define MAXITER      8

#define LIMIT        5000000

typedef struct pont {
    long  limit;
    long  stopped;
    long  duration;
} pont;

#if (TESTMODE == 0)
#include "magicq.hpp"

typedef magicq<uint64_t> pile;

#define INIT(f)
#define FREE(f)

#define PUSH(f, val) ((f)->push((uint64_t)val))
#define POP(f)       ((void *)((f)->pop()))

#define SIZE(f)      ((f)->getsize())

pile  gstack(12);

#elif (TESTMODE == 1)
#include "rbq.hpp"

typedef rbqueue<uint64_t> pile;

#define INIT(f)
#define FREE(f)

#define PUSH(f, val) ((f)->push((uint64_t)val))
#define POP(f)       ((void *)((f)->pop()))

#define SIZE(f)      ((f)->getsize())

pile  gstack(12);

#elif (TESTMODE == 2)
#include "lffifo.hpp"

typedef lfstack_t<uint64_t> pile;

#define INIT(f)      
#define FREE(f)      

#define PUSH(f, val) ((f)->push((uint64_t)(val)))
#define POP(f)       ((void *)((f)->pop()))

#define SIZE(f)      ((f)->getsize())

pile gstack(12);
#endif


/*
*  Global variables
*/

int   tbl[MAXITER * MAXTHREADS];
int   nFinished = 0;

int64_t totSum = 0;

static inline void recvi(int64_t d)
{
#ifdef _WIN32
    InterlockedXor64(&totSum, (+d));
    // InterlockedAdd64(&totSum, (+d));
#else
    // __sync_fetch_and_add(&totSum, (+d));
    __sync_fetch_and_xor(&totSum, (+d));
#endif
}

static inline void posti(int64_t d)
{
#ifdef _WIN32
    InterlockedXor64(&totSum, (+d));
    // InterlockedAdd64(&totSum, (-d));
#else
    // __sync_fetch_and_sub(&totSum, (+d));
    __sync_fetch_and_xor(&totSum, (+d));
#endif
}

// init stack with cells
void initstack(pile* f)
{
    INIT(f);
}

long hybrid(long n)
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
            while (!PUSH(&gstack, (void*)(t2)));
            posti(t2);
        }
        for (i = 0; i < MAXITER; i++) {
            while (!(r = (int64_t)(POP(&gstack))));
            recvi(r);
        }
    }
    return clock() - t;
}

long producer(long n)
{
    clock_t t;
    n = n * MAXITER;

    t = clock();
    while (n--) {
        // int64_t t2 = (n + 2);
        time_t t2 = time(NULL);
        // clock_t t2 = clock();
        // int64_t t2 = rand();
        while (!PUSH(&gstack, (void*)(t2)));
        posti(t2);
    }
    return clock() - t;
}

long consumer(long n)
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

THRRET hybridthread(void* pp)
{
    pont* p = (pont*)pp;
    p->duration = hybrid(p->limit);
    p->stopped = 1;
    return 0;
}

THRRET producerthread(void* pp)
{
    pont* p = (pont*)pp;
    p->duration = producer(p->limit);
    p->stopped = 1;
    return 0;
}

THRRET consumerthread(void* pp)
{
    pont* p = (pont*)pp;
    p->duration = consumer(p->limit);
    p->stopped = 1;
    return 0;
}

//-----------------------------------------------------------------
void bench(int max)
{
#ifdef _WIN32
    DWORD fils[MAXTHREADS];
#else
    pthread_t fils[MAXTHREADS];
#endif

    pont  bridge_p[MAXTHREADS];
    pont  bridge_c[MAXTHREADS];
    pont  bridge_h[MAXTHREADS];

    long   i, end, th;
    double perf = 0;

    for (th = 1; th <= max; ++th)
    {
        srand(time(NULL));

        initstack(&gstack);
        totSum = 0;
        printf("threads count:\t %ld \t", th); fflush(stdout);
        for (i = 0; i < th; i++)
        {
            bridge_p[i].limit = LIMIT;
            bridge_p[i].stopped = 0;
            bridge_p[i].duration = 0;

            bridge_c[i].limit = LIMIT;
            bridge_c[i].stopped = 0;
            bridge_c[i].duration = 0;

            bridge_h[i].limit = LIMIT;
            bridge_h[i].stopped = (TESTMODE == 0) ? 1 : 0;
            bridge_h[i].duration = 0;

#ifdef _WIN32         
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

#if (TESTMODE != 0)
            CreateThread(
                NULL,
                0L,
                hybridthread,
                &bridge_h[i],
                0L,
                &(fils[i])
            );
#endif
#else
            pthread_create(
                &fils[i],
                NULL,
                producerthread,
                &bridge_p[i]
            );

            pthread_create(
                &fils[i],
                NULL,
                consumerthread,
                &bridge_c[i]
            );

#if (TESTMODE != 0)
            pthread_create(
                &fils[i],
                NULL,
                hybridthread,
                &bridge_h[i]
            );
#endif
#endif
        }

        nFinished = 0;
        do {
            sleep(1);
            end = 1;
            int nn = 0;
            for (i = 0; i < th; i++) {
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
        } while (end == 0);

        for (i = 0; i < th; i++) {
            perf += bridge_p[i].duration;
        }
        perf /= th;

        perf /= th * LIMIT * MAXITER;
        perf *= 1000000 / CLOCKS_PER_SEC;
        printf(" totSum = %ld, perf (in us per pop/push):\t %2f\n", totSum, perf); fflush(stdout);

        FREE(&gstack);
    }
}

int main()
{
    // mmFixedSizeMemoryStartup();

#if (TESTMODE == 0)
    printf("\n-------- Lock free ring buffer (SPSC) bench ----------\n");
#elif (TESTMODE == 1)
    printf("\n-------- Lock free ring buffer (MPMC) bench ----------\n");
#elif (TESTMODE == 2)
    printf("\n-------- Lock free stack bench ----------\n");
#elif (TESTMODE == 3)
    printf("\n-------- Lock free queue (MSQ) bench ----------\n");
#endif

    bench((TESTMODE == 0) ? (1) : MAXTHREADS);

    // mmFixedSizeMemoryCleanup();

    return 0;
}
