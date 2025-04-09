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
    //initialize kvalue to smallest K value
    size_t k_val = SMALLEST_K;

    //initialize block size to smallest K value
    size_t size = UINT64_C(1) << k_val;

    //Find the appropriate k value where size~2^k is >= bytes
    while (size < bytes && k_val < MAX_K) {
        k_val++;

        //double block size by bit shifting kVal
        size = UINT64_C(1) << k_val;
    }
    return k_val;
}

struct avail *buddy_calc(struct buddy_pool *pool, struct avail *buddy)
{
    //Validate Values
    if (!pool || !buddy){
        errno = ENOMEM;
        return NULL;
    }

    //calculate the offset in memory for the actual address
    uintptr_t base_address = (uintptr_t)pool->base; //store for safety
    uintptr_t current_address = (uintptr_t)buddy; //store for safety
    uintptr_t address_offset = current_address - base_address; //offset value
    
    //Calculate the Block size by initializing address and shifting by kVal
    size_t block_size = UINT64_C(1) << buddy->kval;

    //Calculate the buddy address by XOR the offset value with the blocksize, and adding the base
    //offset is needed as memory addresses do not start at 0
    uintptr_t buddy_address = (address_offset ^ block_size) + base_address;

    return (struct avail*)buddy_address;
}

void *buddy_malloc(struct buddy_pool *pool, size_t size)
{
    //Validate Values
    if (size == 0 || !pool || (size > pool->numbytes)){
        errno = ENOMEM;
        return NULL;
    }

    //get the kval for the requested size with enough room for the tag and kval fields
    size_t required_kval = btok(size + HEADER_SIZE);

    //R1 Find a block where k <= j <= m, if no available block, fail to allocate and return
    size_t target_kval = required_kval;
    size_t max_kval = pool->kval_m;


    while (target_kval <= max_kval){
        //check for available block
        if(pool->avail[target_kval].next != &pool->avail[target_kval]){
            //a block found so exit
            break;
        }
        target_kval++;
    }
    //There was not enough memory to satisfy the request we set error and return NULL
    if (target_kval > max_kval){
        errno = ENOMEM;
        return NULL;
    }

    //R2 Remove from list;
    struct avail *current_block = pool->avail[target_kval].next; //grab block
    current_block->prev->next = current_block->next; //update next pointer
    current_block->next->prev = current_block->prev; //update next prev pointer

    //R3 Split required?
    //If the currentKValue is greater than the current kVal, we can split it for efficiency
    while (target_kval > required_kval){
        //decrease until reach correct kVal
        target_kval--;

        //calculate buddy address
         //R4 Split the block
        size_t buddy_size = (UINT64_C(1) << target_kval);
        struct avail *buddy = (struct avail *)((uint8_t *)current_block + buddy_size);
        
        buddy->kval = target_kval;
        buddy->tag = BLOCK_AVAIL;

        //Make the buddy available
        buddy->next = pool->avail[target_kval].next;
        buddy->prev = &pool->avail[target_kval];
        pool->avail[target_kval].next->prev = buddy;
        pool->avail[target_kval].next = buddy;
        
        //update current block
        current_block->kval = target_kval;
    }
    current_block->tag = BLOCK_RESERVED;
   
    return (void *)((uint8_t *)current_block + HEADER_SIZE);
}

void buddy_free(struct buddy_pool *pool, void *ptr)
{
    //Validate Values
    if ( !pool || !ptr){
        return;
    }

     // Check if the pointer is within the managed memory range (LLM Suggested)
     if ((uint8_t*)ptr < (uint8_t*)pool->base || (uint8_t*)ptr >= (uint8_t*)pool->base + pool->numbytes) {
        return; // Pointer is outside our pool
    }
     // Find the header by subtracting the header size from the ptr
     struct avail *block = (struct avail *)((uint8_t *)ptr - HEADER_SIZE);
    
     // Validate that this is a reserved block
     if (block->tag != BLOCK_RESERVED) {
         return; // ignore unreserved blocs
     }
     
     // Mark the block as available
     block->tag = BLOCK_AVAIL;
     
     // Try to coalesce with buddy
     size_t k_val = block->kval;
     while (k_val < pool->kval_m) {
         // Calculate the buddy
         struct avail *buddy = buddy_calc(pool, block);
         
         // check buddy is available with same kval
         if (buddy->tag != BLOCK_AVAIL || buddy->kval != k_val) {
             break; // Can't coalesce
         }
         
         // remove buddy
         buddy->prev->next = buddy->next;
         buddy->next->prev = buddy->prev;
         
         // Determine which block is lower in memory
         if (buddy < block) {
             // merger lower memory block
             block = buddy;
         }
         
         k_val++;
         block->kval = k_val;
     }
     
     // Add the block
     block->next = pool->avail[k_val].next;
     block->prev = &pool->avail[k_val];
     pool->avail[k_val].next->prev = block;
     pool->avail[k_val].next = block;
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
