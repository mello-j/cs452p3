#include <assert.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "harness/unity.h"
#include "../src/lab.h"


void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}



/**
 * Check the pool to ensure it is full.
 */
void check_buddy_pool_full(struct buddy_pool *pool)
{
  //A full pool should have all values 0-(kval-1) as empty
  for (size_t i = 0; i < pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }

  //The avail array at kval should have the base block
  assert(pool->avail[pool->kval_m].next->tag == BLOCK_AVAIL);
  assert(pool->avail[pool->kval_m].next->next == &pool->avail[pool->kval_m]);
  assert(pool->avail[pool->kval_m].prev->prev == &pool->avail[pool->kval_m]);

  //Check to make sure the base address points to the starting pool
  //If this fails either buddy_init is wrong or we have corrupted the
  //buddy_pool struct.
  assert(pool->avail[pool->kval_m].next == pool->base);
}

/**
 * Check the pool to ensure it is empty.
 */
void check_buddy_pool_empty(struct buddy_pool *pool)
{
  //An empty pool should have all values 0-(kval) as empty
  for (size_t i = 0; i <= pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }
}

/**
 * Test allocating 1 byte to make sure we split the blocks all the way down
 * to MIN_K size. Then free the block and ensure we end up with a full
 * memory pool again
 */
void test_buddy_malloc_one_byte(void)
{
  fprintf(stderr, "->Test allocating and freeing 1 byte\n");
  struct buddy_pool pool;
  int kval = MIN_K;
  size_t size = UINT64_C(1) << kval;
  buddy_init(&pool, size);
  void *mem = buddy_malloc(&pool, 1);
  //Make sure correct kval was allocated
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests the allocation of one massive block that should consume the entire memory
 * pool and makes sure that after the pool is empty we correctly fail subsequent calls.
 */
void test_buddy_malloc_one_large(void)
{
  fprintf(stderr, "->Testing size that will consume entire memory pool\n");
  struct buddy_pool pool;
  size_t bytes = UINT64_C(1) << MIN_K;
  buddy_init(&pool, bytes);

  //Ask for an exact K value to be allocated. This test makes assumptions on
  //the internal details of buddy_init.
  size_t ask = bytes - sizeof(struct avail);
  void *mem = buddy_malloc(&pool, ask);
  assert(mem != NULL);

  //Move the pointer back and make sure we got what we expected
  struct avail *tmp = (struct avail *)mem - 1;
  assert(tmp->kval == MIN_K);
  assert(tmp->tag == BLOCK_RESERVED);
  check_buddy_pool_empty(&pool);

  //Verify that a call on an empty tool fails as expected and errno is set to ENOMEM.
  void *fail = buddy_malloc(&pool, 5);
  assert(fail == NULL);
  assert(errno = ENOMEM);

  //Free the memory and then check to make sure everything is OK
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests to make sure that the struct buddy_pool is correct and all fields
 * have been properly set kval_m, avail[kval_m], and base pointer after a
 * call to init
 */
void test_buddy_init(void)
{
  fprintf(stderr, "->Testing buddy init\n");
  //Loop through all kval MIN_k-DEFAULT_K and make sure we get the correct amount allocated.
  //We will check all the pointer offsets to ensure the pool is all configured correctly
  for (size_t i = MIN_K; i <= DEFAULT_K; i++)
    {
      size_t size = UINT64_C(1) << i;
      struct buddy_pool pool;
      buddy_init(&pool, size);
      check_buddy_pool_full(&pool);
      buddy_destroy(&pool);
    }
}


void test_buddy_malloc_null_or_zero(void)
{
  fprintf(stderr, "->Testing NULL pool and zero size malloc\n");
  struct buddy_pool pool;
  buddy_init(&pool, UINT64_C(1) << MIN_K);
  
  // Test NULL pool
  void *mem = buddy_malloc(NULL, 10);
  assert(mem == NULL);
  assert(errno == ENOMEM);
  
  // Test zero size
  mem = buddy_malloc(&pool, 0);
  assert(mem == NULL);
  assert(errno == ENOMEM);
  
  // Test size larger than pool
  mem = buddy_malloc(&pool, (UINT64_C(1) << MIN_K) + 1);
  assert(mem == NULL);
  assert(errno == ENOMEM);
  
  buddy_destroy(&pool);
}

void test_buddy_calc(void)
{
  fprintf(stderr, "->Testing buddy calculation\n");
  struct buddy_pool pool;
  size_t pool_size = UINT64_C(1) << MIN_K;
  buddy_init(&pool, pool_size);
  
  // Get a block to work with
  void *mem = buddy_malloc(&pool, pool_size / 4);
  struct avail *block = (struct avail *)((uint8_t *)mem - HEADER_SIZE);
  
  // Test buddy calculation
  struct avail *buddy = buddy_calc(&pool, block);
  assert(buddy != NULL);
  
  // Test invalid inputs to buddy_calc
  buddy = buddy_calc(NULL, block);
  assert(buddy == NULL);
  assert(errno == ENOMEM);
  
  buddy = buddy_calc(&pool, NULL);
  assert(buddy == NULL);
  assert(errno == ENOMEM);
  
  buddy_free(&pool, mem);
  buddy_destroy(&pool);
}

void test_buddy_free_invalid(void)
{
  fprintf(stderr, "->Testing invalid free operations\n");
  struct buddy_pool pool;
  buddy_init(&pool, UINT64_C(1) << MIN_K);
  
  // Test NULL pool
  buddy_free(NULL, (void *)0x1234);
  
  // Test NULL pointer
  buddy_free(&pool, NULL);
  
  // Test pointer outside pool
  uint8_t fake_mem[10];
  buddy_free(&pool, fake_mem);
  
  // Allocate and free a block, then try to free it again (double-free)
  void *mem = buddy_malloc(&pool, 10);
  buddy_free(&pool, mem);
  buddy_free(&pool, mem); // This should be handled gracefully
  
  buddy_destroy(&pool);
}

void test_buddy_fragmentation(void)
{
  fprintf(stderr, "->Testing memory fragmentation\n");
  struct buddy_pool pool;
  size_t pool_size = UINT64_C(1) << (MIN_K + 4);
  buddy_init(&pool, pool_size);
  
  // Create a pattern of allocations that causes fragmentation
  void *blocks[100];
  int count = 0;
  
  // First, allocate many small blocks
  for (int i = 0; i < 20; i++) {
    blocks[count] = buddy_malloc(&pool, 16);
    if (blocks[count] != NULL) count++;
  }
  
  // Free every other block to cause fragmentation
  for (int i = 0; i < count; i += 2) {
    buddy_free(&pool, blocks[i]);
    blocks[i] = NULL;
  }
  
  // Try to allocate a block larger than any available fragment
  void *large_block = buddy_malloc(&pool, pool_size / 2);
  assert(large_block == NULL); // Should fail due to fragmentation
  assert(errno == ENOMEM);
  
  // Clean up remaining blocks
  for (int i = 0; i < count; i++) {
    if (blocks[i] != NULL) {
      buddy_free(&pool, blocks[i]);
    }
  }
  
  buddy_destroy(&pool);
}

void test_btok_boundary(void)
{
  fprintf(stderr, "->Testing btok boundary conditions\n");
  
  // Test minimum size
  size_t k = btok(1);
  assert(k >= SMALLEST_K);
  
  // Test power of 2 exactly
  k = btok(UINT64_C(1) << 10);
  assert(k == 10);
  
  // Test slightly more than power of 2
  k = btok((UINT64_C(1) << 10) + 1);
  assert(k == 11);
  
  // Test maximum size
  k = btok((UINT64_C(1) << (MAX_K - 1)) - 1);
  assert(k < MAX_K);
}

void test_buddy_multi_alloc_free(void)
{
  fprintf(stderr, "->Testing multiple allocations and frees\n");
  struct buddy_pool pool;
  size_t pool_size = UINT64_C(1) << (MIN_K + 4);
  buddy_init(&pool, pool_size);
  
  // Different sized allocations
  void *mem1 = buddy_malloc(&pool, 32);
  void *mem2 = buddy_malloc(&pool, 64);
  void *mem3 = buddy_malloc(&pool, 128);
  void *mem4 = buddy_malloc(&pool, 256);
  
  // Free in random order
  buddy_free(&pool, mem3);
  buddy_free(&pool, mem1);
  buddy_free(&pool, mem4);
  buddy_free(&pool, mem2);
  
  // Pool should be full again
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

int main(void) {
  time_t t;
  unsigned seed = (unsigned)time(&t);
  fprintf(stderr, "Random seed:%d\n", seed);
  srand(seed);
  printf("Running memory tests.\n");

  UNITY_BEGIN();
  RUN_TEST(test_buddy_init);
  RUN_TEST(test_buddy_malloc_one_byte);
  RUN_TEST(test_buddy_malloc_one_large);
  RUN_TEST(test_buddy_malloc_null_or_zero);
  RUN_TEST(test_buddy_calc);
  RUN_TEST(test_buddy_free_invalid);
  RUN_TEST(test_buddy_fragmentation);
  RUN_TEST(test_btok_boundary);
  RUN_TEST(test_buddy_multi_alloc_free);
  return UNITY_END();
}
