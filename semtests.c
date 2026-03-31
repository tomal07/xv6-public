// Test threads

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "param.h"
#include "stat.h"
#include "boundedbuffer.h"
#include "testsutils.h"

// Definitions
#define STACK_SIZE 1000
#define LOOP_AMOUNT 4
#define SLEEP_AMOUNT 50
#define FILE_NAME "file.txt"

// Globals
sem_bounded_buffer_t g_bounded_buffer;
int g_binary_sem, g_counting_sem, g_value;

// Utility functions
void
produce(void)
{
  int i;

  for(i = 0; i < LOOP_AMOUNT; i++)
    ASSERT(bounded_buffer_push(&g_bounded_buffer, i) == 0);
}

void
change_value_to_1_in_critical_section(void)
{
  ASSERT(binary_sem_wait(g_binary_sem) == 0);

  ASSERT(g_value == 0);

  g_value = 1;
  sleep(SLEEP_AMOUNT);
  g_value = 0;

  ASSERT(binary_sem_post(g_binary_sem) == 0);
}

void
increment_value_using_counting_and_binary_semaphore(void)
{
  ASSERT(counting_sem_wait(g_counting_sem) == 0);

  ASSERT(binary_sem_wait(g_binary_sem) == 0);
  g_value++;
  ASSERT(binary_sem_post(g_binary_sem) == 0);
}

int
create_and_unlink_file(int sid)
{
  int fd;

  if(binary_sem_wait(sid) != 0)
    return -1;

  // Verify that it doesn't exist
  if(open(FILE_NAME, 0) >= 0)
    return -1;

  // Create it
  if((fd = open(FILE_NAME, O_CREATE)) < 0
      || close(fd) != 0)
    return -1;

  // Verify that it exists now
  if((fd = open(FILE_NAME, 0)) < 0
      || close(fd) != 0)
    return -1;

  sleep(SLEEP_AMOUNT);

  // Verify that it still exists
  if((fd = open(FILE_NAME, 0)) < 0
      || close(fd) != 0)
    return -1;

  // Remove it
  if(unlink(FILE_NAME) != 0)
    return -1;

  // Verify that it doesn't exist
  if(open(FILE_NAME, 0) >= 0)
    return -1;

  if(binary_sem_post(sid) != 0)
    return -1;

  return 0;
}

// Tests
void
invalid_semaphores_operations(void)
{
  int i, sid;
  int sids[NSEM];
  sem_bounded_buffer_t boundedbuffer;  

  // Low counter
  ASSERT(binary_sem_init(-1) == -1);
  ASSERT(counting_sem_init(-1) == -1);
  ASSERT(counting_sem_init(-1) == -1);
  ASSERT(bounded_buffer_init(&boundedbuffer, 0) == -1);

  // High counter
  ASSERT(binary_sem_init(2) == -1);
  ASSERT(counting_sem_init(MAX_COUNTING_SEM_COUNT + 1) == -1);

  // Max semaphores
  memset(sids, 0, sizeof(sids));
  for(i = 0; i < NSEM; i++){
    ASSERT((sid = counting_sem_init(1)) >= 0);
    sids[i] = sid;
  }
  ASSERT(counting_sem_init(1) == -1);
  for(i = 0; i < NSEM; i++)
    ASSERT(counting_sem_destroy(sids[i]) == 0);

  memset(sids, 0, sizeof(sids));
  for(i = 0; i < NSEM; i++){
    ASSERT((sid = binary_sem_init(1)) >= 0);
    sids[i] = sid;
  }
  ASSERT(binary_sem_init(1) == -1);
  for(i = 0; i < NSEM; i++)
    ASSERT(binary_sem_destroy(sids[i]) == 0);

  // wait/post/destroy wrong type
  ASSERT((sid = binary_sem_init(1)) == 0);
  ASSERT(counting_sem_wait(sid) == -1);
  ASSERT(counting_sem_post(sid) == -1);
  ASSERT(counting_sem_destroy(sid) == -1);
  ASSERT(binary_sem_destroy(sid) == 0);

  ASSERT((sid = counting_sem_init(1)) == 0);
  ASSERT(binary_sem_wait(sid) == -1);
  ASSERT(binary_sem_post(sid) == -1);
  ASSERT(binary_sem_destroy(sid) == -1);
  ASSERT(counting_sem_destroy(sid) == 0);

  // wait/post/destroy uninitialized
  ASSERT(binary_sem_wait(0) == -1);
  ASSERT(binary_sem_post(0) == -1);
  ASSERT(binary_sem_destroy(0) == -1);

  ASSERT(counting_sem_wait(0) == -1);
  ASSERT(counting_sem_post(0) == -1);
  ASSERT(counting_sem_destroy(0) == -1);
}

void
producer_consumer(void)
{
  int i, value, tid;
  int consumed[LOOP_AMOUNT];
  char stack[STACK_SIZE];

  memset(consumed, 0, sizeof(consumed));

  ASSERT(bounded_buffer_init(&g_bounded_buffer, 50) == 0);
  ASSERT((tid = thread_create(produce, stack, sizeof(stack))) > 0);

  for(i = 0; i < LOOP_AMOUNT; i++){
    ASSERT(bounded_buffer_pop(&g_bounded_buffer, &value) == 0);
    consumed[value] = 1;
  }

  for(i = 0; i < LOOP_AMOUNT; i++)
    ASSERT(consumed[i] == 1);

  ASSERT(thread_join(tid, 0) == tid);
  ASSERT(bounded_buffer_destroy(&g_bounded_buffer) == 0);
}

void
check_binary_semaphore_blocks(void)
{
  int tid1, tid2;
  char stack1[STACK_SIZE];
  char stack2[STACK_SIZE];

  ASSERT((g_binary_sem = binary_sem_init(1)) == 0);

  g_value = 0;

  ASSERT((tid1 = thread_create(change_value_to_1_in_critical_section, stack1, sizeof(stack1))) > 0);
  ASSERT((tid2 = thread_create(change_value_to_1_in_critical_section, stack2, sizeof(stack2))) > 0);

  ASSERT(thread_join(tid1, 0) == tid1);
  ASSERT(thread_join(tid2, 0) == tid2);

  ASSERT(binary_sem_destroy(g_binary_sem) == 0);
}

void
check_counting_semaphore_only_count_are_executed(void)
{
  int tid1, tid2, tid3;

  char stack1[STACK_SIZE];
  char stack2[STACK_SIZE];
  char stack3[STACK_SIZE];

  g_value = 0;

  ASSERT((g_binary_sem = binary_sem_init(1)) == 0);
  ASSERT((g_counting_sem = counting_sem_init(2)) == 0);

  ASSERT((tid1 = thread_create(increment_value_using_counting_and_binary_semaphore, stack1, sizeof(stack1))) > 0);
  ASSERT((tid2 = thread_create(increment_value_using_counting_and_binary_semaphore, stack2, sizeof(stack2))) > 0);
  ASSERT((tid3 = thread_create(increment_value_using_counting_and_binary_semaphore, stack3, sizeof(stack3))) > 0);

  sleep(SLEEP_AMOUNT);
  ASSERT(g_value == 2);

  ASSERT(counting_sem_post(g_counting_sem) == 0);
  sleep(SLEEP_AMOUNT);
  ASSERT(g_value == 3);

  ASSERT(thread_join(tid1, 0) == tid1);
  ASSERT(thread_join(tid2, 0) == tid2);
  ASSERT(thread_join(tid3, 0) == tid3);
}

void
use_binary_semaphore_with_procs(void)
{
  int pid, sid;

  unlink(FILE_NAME);

  ASSERT((sid = binary_sem_init(1)) >= 0);
  ASSERT((pid = fork()) >= 0);

  if(pid == 0){
    ASSERT_HANG(create_and_unlink_file(sid) == 0);
    exit();
  }

  ASSERT(create_and_unlink_file(sid) == 0);
  ASSERT(wait() == pid);

  ASSERT(binary_sem_destroy(sid) == 0);
}

void
kill_waiting_child(void)
{
  int pid, sid;

  ASSERT((sid = binary_sem_init(0)) >= 0);
  ASSERT((pid = fork()) >= 0);

  if(pid == 0){
    ASSERT_HANG(binary_sem_wait(sid) == 0);
    exit();
  }

  ASSERT(kill(pid) == 0);
  ASSERT(wait() == pid);

  ASSERT(binary_sem_destroy(sid) == 0);
}

int
main(void)
{
  printf(1, "Starting semaphore tests...\n");

  TEST(invalid_semaphores_operations);
  TEST(producer_consumer);
  TEST(check_binary_semaphore_blocks);
  TEST(check_counting_semaphore_only_count_are_executed);
  TEST(use_binary_semaphore_with_procs);
  TEST(kill_waiting_child);

  printf(1, PARTYPOPPER " All tests passed " PARTYPOPPER "\n");

  exit();
}