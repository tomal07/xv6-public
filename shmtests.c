// Test threads

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "testsutils.h"

// Definitions
#define NON_ZERO_VALUE1 0xdead
#define NON_ZERO_VALUE2 0xbeef
#define NON_ZERO_VALUE3 0xabcd
#define SPAM_COUNT 300
#define STACK_SIZE 1000

// Globals
void *g_addr;

// Utility functions
void
spam_backwards(void)
{
  int key, i, ref;

  for(key = MAX_SHM_KEY; key >= MIN_SHM_KEY; key--){
    for(i = 0; i < SPAM_COUNT; i++){
      ASSERT(shmgetat(key, 1) != -1);
      ref = shm_refcount(key);
      ASSERT(ref >= 1 && ref <= 2);
    }
  }
}

void
assert_shm_is_addr(void)
{
  ASSERT((void*)shmgetat(0, 0) == g_addr);
  ASSERT(shm_refcount(0) == 1);
}

void
spam_read_write(void *addr, char val1, char val2)
{
  int i;
  
  for(i = 0; i < SPAM_COUNT; i++){
    ASSERT(*((char*)addr) == val1 || *((char*)addr) == val2);
    *((char*)addr) = val1;
  }
}

// Tests
void
basic_usage(void)
{
  int pid;
  void *addr;

  ASSERT((addr = (void*)shmgetat(0, 1)) != (void*)-1);
  ASSERT(*((int*)addr) == 0);
  *((int*)addr) = NON_ZERO_VALUE1;
  ASSERT((pid = fork()) >= 0);

  if(pid == 0){
    ASSERT_HANG(shm_refcount(0) == 2);
    ASSERT_HANG((void*)shmgetat(0, 0) == addr);
    ASSERT_HANG(shm_refcount(0) == 2);

    ASSERT_HANG(*((int*)addr) == NON_ZERO_VALUE1);
    *((int*)addr) = NON_ZERO_VALUE2;
    exit();
  }

  ASSERT(wait() == pid);
  ASSERT(shm_refcount(0) == 1);
  ASSERT(*((int*)addr) == NON_ZERO_VALUE2);
  
  ASSERT(sbrk(-PGSIZE) >= 0);
  ASSERT(shm_refcount(0) == 0);
}

void
invalid_usage(void)
{
  int key;

  ASSERT(shmgetat(MIN_SHM_KEY, MIN_SHM_PAGES - 1) == -1);
  ASSERT(shmgetat(MIN_SHM_KEY - 1, MIN_SHM_PAGES) == -1);
  ASSERT(shmgetat(MAX_SHM_KEY + 1, MIN_SHM_PAGES) == -1);
  ASSERT(shmgetat(MIN_SHM_KEY, MAX_SHM_PAGES + 1) == -1);

  ASSERT(shm_refcount(MIN_SHM_KEY - 1) == -1);
  ASSERT(shm_refcount(MAX_SHM_KEY + 1) == -1);

  // Make sure that none of them created refs
  for(key = MIN_SHM_KEY; key <= MAX_SHM_KEY; key++)
    ASSERT(shm_refcount(key) == 0);
}

void
use_after_some_pages_freed_crashes(void)
{
  int pid;
  void *addr;

  ASSERT((addr = (void*)shmgetat(0, 2)) != (void*)-1);
  ASSERT(*((int*)addr) == 0);
  *((int*)addr) = NON_ZERO_VALUE1;
  ASSERT(sbrk(-PGSIZE) > 0); // Can't be zero because one shm page is still there.
  ASSERT((pid = fork()) >= 0);

  if(pid == 0){
    ASSERT_HANG(shm_refcount(0) == 2);
    ASSERT_HANG((void*)shmgetat(0, 0) == addr);
    ASSERT_HANG(shm_refcount(0) == 2);

    ASSERT_HANG(*((int*)addr) == NON_ZERO_VALUE1);
    *((int*)addr) = NON_ZERO_VALUE2;

    *((int*)addr + PGSIZE) = NON_ZERO_VALUE2;
    for(;;){}
  }

  ASSERT(wait() == pid);
  ASSERT(shm_refcount(0) == 1);
  ASSERT(*((int*)addr) == NON_ZERO_VALUE2);

  ASSERT(sbrk(-PGSIZE) >= 0);
  ASSERT(shm_refcount(0) == 0);
}

void
different_keys_not_shared(void)
{
  int pid;
  void *addr1, *addr2;

  ASSERT((addr1 = (void*)shmgetat(0, 1)) != (void*)-1);
  ASSERT(*((int*)addr1) == 0);
  *((int*)addr1) = NON_ZERO_VALUE1;
  ASSERT((pid = fork()) >= 0);

  if(pid == 0){
    ASSERT_HANG(shm_refcount(0) == 2);
    ASSERT_HANG(shm_refcount(1) == 0);
    ASSERT_HANG((addr2 = (void*)shmgetat(1, 1)) != addr1);
    ASSERT_HANG(shm_refcount(0) == 2);
    ASSERT_HANG(shm_refcount(1) == 1);

    ASSERT_HANG(*((int*)addr2) != NON_ZERO_VALUE1);
    *((int*)addr2) = NON_ZERO_VALUE2;
    exit();
  }

  ASSERT(wait() == pid);
  ASSERT(shm_refcount(0) == 1);
  ASSERT(shm_refcount(1) == 0);
  ASSERT(*((int*)addr1) != NON_ZERO_VALUE2);
  
  ASSERT(sbrk(-PGSIZE) >= 0);
  ASSERT(shm_refcount(0) == 0);
}


void
spam(void)
{
  int key, i, pid, tid, ref;
  char stack[STACK_SIZE];

  ASSERT((pid = fork()) >= 0);

  // TODO: if pid == 0 then ASSERT_HANGs are needed...
  ASSERT((tid = thread_create(spam_backwards, stack, sizeof(stack))) >= 0);

  for(key = MIN_SHM_KEY; key <= MAX_SHM_KEY; key++){
    for(i = 0; i < SPAM_COUNT; i++){
      ASSERT(shmgetat(key, 1) != -1);
      ref = shm_refcount(key);
      ASSERT(ref >= 1 && ref <= 2);
    }
  }

  ASSERT(thread_join(tid, 0) == tid);

  ASSERT(sbrk(-PGSIZE*(MAX_SHM_KEY - MIN_SHM_KEY + 1)) >= 0);

  if(pid == 0)
    exit();

  ASSERT(wait() == pid);
}

void
address_checks(void)
{
  void *addr1, *addr2, *addr3;

  // Check that shm pages are allocated at the end.
  ASSERT((addr1 = sbrk(0)) >= 0);
  ASSERT((addr2 = (void*)shmgetat(0, 2)) == addr1);
  ASSERT((addr3 = (void*)shmgetat(1, 1)) == addr2 + 2 * PGSIZE);

  // Clean them and ensure that we returned to the previous size.
  ASSERT((void*)sbrk(-3*PGSIZE) == addr3 + PGSIZE);
  ASSERT((void*)sbrk(0) == addr1);
}

void
shm_page_is_not_shm_after_free(void)
{
  int sid1, sid2, pid;
  void *addr;

  ASSERT((addr = (void*)shmgetat(0, 1)) != (void*)-1);
  ASSERT(*((int*)addr) == 0);
  *((int*)addr) = NON_ZERO_VALUE1;

  ASSERT((sid1 = binary_sem_init(0)) >= 0);
  ASSERT((sid2 = binary_sem_init(0)) >= 0);

  ASSERT((pid = fork()) >= 0);

  if(pid == 0){
    ASSERT_HANG(shm_refcount(0) == 2);
    ASSERT_HANG((void*)shmgetat(0, 0) == addr);
    ASSERT_HANG(shm_refcount(0) == 2);

    ASSERT_HANG(*((int*)addr) == NON_ZERO_VALUE1);

    ASSERT(binary_sem_post(sid2) == 0);
    ASSERT(binary_sem_wait(sid1) == 0);
    *((int*)addr) = NON_ZERO_VALUE2;
    ASSERT(binary_sem_post(sid2) == 0);
    ASSERT(binary_sem_wait(sid1) == 0);

    exit();
  }

  // Wait for the child to create the shm and check the value.
  ASSERT(binary_sem_wait(sid2) == 0);

  // Free the shm page.
  ASSERT(shm_refcount(0) == 2);
  ASSERT(sbrk(-PGSIZE) >= 0);
  ASSERT(shm_refcount(0) == 1);

  // Create a page which is not shm, which has the same
  // user address as the previous shm page.
  ASSERT(sbrk(PGSIZE) >= 0);
  ASSERT(shm_refcount(0) == 1);

  // Now it's not a shm page => changes in shm in the child shouldn't affect it.
  ASSERT(*((int*)addr) == 0);
  ASSERT(binary_sem_post(sid1) == 0);
  ASSERT(binary_sem_wait(sid2) == 0);
  ASSERT(*((int*)addr) == 0);

  // Ensure that we are able to write to that page.
  *((int*)addr) = NON_ZERO_VALUE3;
  ASSERT(*((int*)addr) == NON_ZERO_VALUE3);
  
  // Let the child exit.
  ASSERT(binary_sem_post(sid1) == 0);
  ASSERT(wait() == pid);

  // Make sure that this page isn't cleaned after ref goes to 0.
  // It shouldn't as it's not a shm page now.
  ASSERT(*((int*)addr) == NON_ZERO_VALUE3);
  ASSERT(sbrk(-PGSIZE) >= 0);
}

void
same_address_in_thread_and_one_ref(void)
{
  int tid;
  char stack[STACK_SIZE];

  ASSERT((g_addr = (void*)shmgetat(0, 1)) >= 0);
  ASSERT((tid = thread_create(assert_shm_is_addr, stack, sizeof(stack))) >= 0);
  ASSERT(thread_join(tid, 0) == tid);

  ASSERT(sbrk(-PGSIZE) >= 0);
}

void
parallel_access(void)
{
  int pid;
  void *addr;

  ASSERT((addr = (void*)shmgetat(0, 1)) >= 0);
  ASSERT((pid = fork()) >= 0);

  spam_read_write(addr, pid ? 0 : 1, pid ? 1 : 0);

  if(pid == 0)
    exit();

  ASSERT(wait() == pid);
  ASSERT(sbrk(-PGSIZE) >= 0);
}

int
main(void)
{
  printf(1, "Starting shm tests...\n");

  TEST(basic_usage);
  TEST(invalid_usage);
  TEST(use_after_some_pages_freed_crashes);
  TEST(different_keys_not_shared);
  TEST(spam);
  TEST(address_checks);
  TEST(shm_page_is_not_shm_after_free);
  TEST(same_address_in_thread_and_one_ref);
  TEST(parallel_access);

  printf(1, PARTYPOPPER " All tests passed " PARTYPOPPER "\n");

  exit();
}