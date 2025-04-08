#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif

#include "lab.h"

#define handle_error_and_die(msg) \
    do                            \
    {                             \
        perror(msg);              \
        raise(SIGKILL);          \
    } while (0)

/**
 * @brief Convert bytes to the correct K value
 *
 * @param bytes the number of bytes
 * @return size_t the K value that will fit bytes
 */
size_t btok(size_t bytes)
{
    //Validate bytes...probably not needed
    if (!bytes){
        return DEFAULT_K;
    }

    //initialize kvalue to smallest K value
    size_t kVal = SMALLEST_K;

    //initialize block size to smallest K value
    size_t size = UNINT_C(1) << kVal;

    //Find the appropriate k value where size~2^k is >= bytes
    while (size < bytes) {
        kVal++;

        if (kVal >= MAX_K) { //Max size reached, downsize
            return MAX_K - 1;
        }

        //double block size by bit shifting kVal
        size = UINT64_C(1) << kVal;
    }
    return kVal;
}

struct avail *buddy_calc(struct buddy_pool *pool, struct avail *buddy)
{
    //Validate Values
    if (!pool || !buddy){
        errno = ENOMEM;
        return NULL;
    }

    //calculate the offset in memory for the actual address
    uintptr_t baseAddress = pool->base; //store for safety
    uintptr_t currentAddress = (uintptr_t)buddy; //store for safety
    uintptr_t offset = currentAddress - baseAddress; //offset value
    
    //Calculate the Block size by initializing address and shifting by kVal
    uintptr_t blockSize = UNINT64_C(1) << buddy->kval;

    //Calculate the buddy address by XOR the offset value with the blocksize, and adding the offset
    //offset is needed as memory addresses do not start at 0
    uintptr_t buddyAddress = (offset ^ blockSize) + offset;

    return (struct avail*)buddyAddress;
}

void *buddy_malloc(struct buddy_pool *pool, size_t size)
{
    //Validate Values
    if (size == 0 || !pool){
        errno = ENOMEM;
        return NULL;
    }

    //calculate the size of the header
    size_t headerSize = sizeof(struct avail);

    //get the kval for the requested size with enough room for the tag and kval fields
    size_t calculatedKVal = btok(size + headerSize);

    //R1 Find a block
    size_t requiredKVal = calculatedKVal;
    size_t maxKVal = pool->kval_m
    while (requiredKVal <= maxKVal)
    //There was not enough memory to satisfy the request thus we need to set error and return NULL

    //R2 Remove from list;

    //R3 Split required?

    //R4 Split the block

}

void buddy_free(struct buddy_pool *pool, void *ptr)
{

}

//IF time allows....
// /**
//  * @brief This is a simple version of realloc.
//  *
//  * @param poolThe memory pool
//  * @param ptr  The user memory
//  * @param size the new size requested
//  * @return void* pointer to the new user memory
//  */
// void *buddy_realloc(struct buddy_pool *pool, void *ptr, size_t size)
// {
//     //Required for Grad Students
//     //Optional for Undergrad Students
// }

void buddy_init(struct buddy_pool *pool, size_t size)
{
    size_t kval = 0;
    if (size == 0)
        kval = DEFAULT_K;
    else
        kval = btok(size);

    if (kval < MIN_K)
        kval = MIN_K;
    if (kval > MAX_K)
        kval = MAX_K - 1;

    //make sure pool struct is cleared out
    memset(pool,0,sizeof(struct buddy_pool));
    pool->kval_m = kval;
    pool->numbytes = (UINT64_C(1) << pool->kval_m);
    //Memory map a block of raw memory to manage
    pool->base = mmap(
        NULL,                               /*addr to map to*/
        pool->numbytes,                     /*length*/
        PROT_READ | PROT_WRITE,             /*prot*/
        MAP_PRIVATE | MAP_ANONYMOUS,        /*flags*/
        -1,                                 /*fd -1 when using MAP_ANONYMOUS*/
        0                                   /* offset 0 when using MAP_ANONYMOUS*/
    );
    if (MAP_FAILED == pool->base)
    {
        handle_error_and_die("buddy_init avail array mmap failed");
    }

    //Set all blocks to empty. We are using circular lists so the first elements just point
    //to an available block. Thus the tag, and kval feild are unused burning a small bit of
    //memory but making the code more readable. We mark these blocks as UNUSED to aid in debugging.
    for (size_t i = 0; i <= kval; i++)
    {
        pool->avail[i].next = pool->avail[i].prev = &pool->avail[i];
        pool->avail[i].kval = i;
        pool->avail[i].tag = BLOCK_UNUSED;
    }

    //Add in the first block
    pool->avail[kval].next = pool->avail[kval].prev = (struct avail *)pool->base;
    struct avail *m = pool->avail[kval].next;
    m->tag = BLOCK_AVAIL;
    m->kval = kval;
    m->next = m->prev = &pool->avail[kval];
}

void buddy_destroy(struct buddy_pool *pool)
{
    int rval = munmap(pool->base, pool->numbytes);
    if (-1 == rval)
    {
        handle_error_and_die("buddy_destroy avail array");
    }
    //Zero out the array so it can be reused it needed
    memset(pool,0,sizeof(struct buddy_pool));
}

#define UNUSED(x) (void)x

/**
 * This function can be useful to visualize the bits in a block. This can
 * help when figuring out the buddy_calc function!
 */
static void printb(unsigned long int b)
{
     size_t bits = sizeof(b) * 8;
     unsigned long int curr = UINT64_C(1) << (bits - 1);
     for (size_t i = 0; i < bits; i++)
     {
          if (b & curr)
          {
               printf("1");
          }
          else
          {
               printf("0");
          }
          curr >>= 1L;
     }
}
