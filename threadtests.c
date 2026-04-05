// Test threads

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "param.h"
#include "stat.h"
#include "testsutils.h"

// Definitions for tests
#define NORMAL_STACK_SIZE 1000
#define SMALL_STACK_SIZE 64
#define SECRET_VALUE1 0xdead
#define SECRET_VALUE2 0xbeef
#define MESSAGE_FILE "msg.txt"
#define MESSAGE "This is a message"
#define DIR1 "my_dir1"
#define DIR2 "my_dir2"
#define SLEEP_LARGE_AMOUNT 100
#define SLEEP_SMALL_AMOUNT 20
#define SHOULDNT_EXIST_FILE "shouldnt_exist.txt"
#define ILLEGAL_ADDRESS 0x80000000

// Globals
int g_secret, g_fd, g_tid, g_pid;

// Functions for threads
void
empty(void)
{
}

void
loop_forever(void)
{
  for(;;){}
}

void
return_tid(void)
{
  int *tid;

  ASSERT((tid = malloc(sizeof(int))) > 0);

  *tid = gettid();

  thread_exit(tid);
}

void
thread_exit_illegal_address(void)
{
  int *tid = (int*)ILLEGAL_ADDRESS;

  thread_exit(tid);
}

void
change_secret_from_1_to_2(void)
{
  ASSERT(g_secret == SECRET_VALUE1);
  g_secret = SECRET_VALUE2;
}

void
write_to_global_open_fd(void)
{
  ASSERT(write(g_fd, MESSAGE, sizeof(MESSAGE)) == sizeof(MESSAGE));
}

void
close_global_fd(void)
{
  ASSERT(close(g_fd) == 0);
}

void
open_file_and_set_global_fd(void)
{
  ASSERT((g_fd = open(MESSAGE_FILE, O_WRONLY|O_CREATE)) >= 0);
}

void
open_file_expect_failure(void)
{
  ASSERT(open(MESSAGE_FILE, O_WRONLY|O_CREATE) < 0);
}

void
chdir_to_dir1(void)
{
  ASSERT(chdir(DIR1) == 0);
}

void
sleep_some_time(void)
{
  ASSERT(sleep(SLEEP_LARGE_AMOUNT) == 0);
}

void
join_global_tid(void)
{
  ASSERT(thread_join(g_tid, 0) == g_tid);  
}

void
wait_on_global_pid(void)
{
  ASSERT(wait() == g_pid);
}

void sleep_and_create_file(void)
{
  int fd;

  ASSERT_HANG(sleep(SLEEP_SMALL_AMOUNT) == 0); // Sleeping to give enough time to terminate this thread.
  ASSERT_HANG((fd = open(SHOULDNT_EXIST_FILE, O_CREATE)) >= 0);
  ASSERT_HANG(close(fd) == 0);
}

void
fork_and_thread_exit(void)
{
  g_pid = fork();
  ASSERT(g_pid >= 0);

  if(g_pid == 0){
    sleep(SLEEP_SMALL_AMOUNT);
    exit();
  }

  thread_exit(0);
}

void
access_illegal_memory(void)
{
  int *addr = (int*)ILLEGAL_ADDRESS;
  printf(1, "%d\n", *addr);
}

void
sleep_and_exit(void)
{
  sleep(SLEEP_LARGE_AMOUNT);
  exit();
}

void
spam_link_unlink_close(void)
{
  int i;
  unsigned int x = gettid();

  for(i = 0; i < 100; i++){
    x = x * 1103515245 + 12345;
    if((x % 3) == 0){
      close(open("x", O_RDWR | O_CREATE));
    } else if((x % 3) == 1){
      link("cat", "x");
    } else {
      unlink("x");
    }
  }
}

// Tests
void
verify_different_tids_same_process_group(void)
{  
  char stack1[NORMAL_STACK_SIZE];
  char stack2[NORMAL_STACK_SIZE];
  int tid1, tid2;
  int ourtid = gettid();

  ASSERT((tid1 = thread_create(empty, stack1, sizeof(stack1))) > 0);
  ASSERT((tid2 = thread_create(empty, stack2, sizeof(stack2))) > 0);

  ASSERT(tid1 != tid2);
  ASSERT(tid1 != ourtid);
  ASSERT(tid2 != ourtid);

  ASSERT(thread_join(tid1, 0) == tid1);
  ASSERT(thread_join(tid2, 0) == tid2);
}

void
illegal_thread_create(void)
{
  char stack[NORMAL_STACK_SIZE];

  ASSERT(thread_create(empty, stack, 0) < 0);                   // Zero sized stack.
  ASSERT(thread_create(empty, stack, NORMAL_STACK_SIZE*3) < 0); // Out of bounds stack.
}

void
gettid_twice(void)
{
  ASSERT(gettid() == gettid());
}

void
join_the_current_thread(void)
{
  ASSERT(thread_join(gettid(), 0) == -1);
}

void
kill_child_with_threads(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid, pid = fork();
  ASSERT(pid >= 0);

  if(pid == 0){
    // Create a thread that loops forever, and then join it.
    ASSERT((tid = thread_create(loop_forever, stack, sizeof(stack))) > 0);
    thread_join(tid, 0);
    ASSERT(0); // Shouldn't reach here
  }
  
  ASSERT(sleep(SLEEP_SMALL_AMOUNT) == 0); // Sleeping to make the child create the thread before we kill
  ASSERT(kill(pid) == 0);
  ASSERT(wait() == pid);
}

void
exit_without_thread_join(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid, pid = fork();
  ASSERT(pid >= 0);

  if(pid == 0){
    ASSERT((tid = thread_create(empty, stack, sizeof(stack))) > 0);
    exit();
  }
  
  // Sleeping to make the child create the thread before we kill
  ASSERT(sleep(SLEEP_SMALL_AMOUNT) == 0);

  ASSERT(wait() == pid);
}

void
fork_and_wait_in_thread(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid;

  g_pid = fork();

  ASSERT(g_pid >= 0);

  if(g_pid == 0)
    exit();

  ASSERT((tid = thread_create(wait_on_global_pid, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);
}

void
fork_wait_in_parent_thread_exit_in_child(void)
{
  int pid;

  pid = fork();

  ASSERT(pid >= 0);

  if(pid == 0)
    thread_exit(0);

  ASSERT(wait() == pid);
}

void
wait_in_another_thread_after_parent_thread_died(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid;

  ASSERT((tid = thread_create(fork_and_thread_exit, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  ASSERT(wait() == g_pid);
}

void
return_tid_from_thread(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid, *rettid;

  ASSERT((tid = thread_create(return_tid, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, (void**)&rettid) == tid);
  ASSERT(*rettid == tid);

  // return_tid dynamically allocates an int* in order to return the tid, so now we need to free it.
  free(rettid);
}

void
thread_exit_illegal_address_from_thread(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid, *rettid;

  ASSERT((tid = thread_create(thread_exit_illegal_address, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, (void**)&rettid) == tid);
  ASSERT(rettid == (int*)ILLEGAL_ADDRESS);
}

void
read_and_change_global_in_thread(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid;

  g_secret = SECRET_VALUE1;

  ASSERT((tid = thread_create(change_secret_from_1_to_2, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  ASSERT(g_secret == SECRET_VALUE2);
}

void
open_global_fd_and_write_in_thread(void)
{
  char buf[sizeof(MESSAGE)];
  char stack[NORMAL_STACK_SIZE];
  int tid, fd;

  ASSERT((g_fd = open(MESSAGE_FILE, O_WRONLY|O_CREATE)) >= 0);
  ASSERT((tid = thread_create(write_to_global_open_fd, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  ASSERT(close(g_fd) == 0);

  // Reading from the file to check if it was written as expected
  ASSERT((fd = open(MESSAGE_FILE, O_RDONLY)) >= 0);
  ASSERT(read(fd, buf, sizeof(MESSAGE)) == sizeof(MESSAGE));
  ASSERT(strcmp(buf, MESSAGE) == 0);

  ASSERT(close(fd) == 0);
}

void
unable_to_write_after_close_in_thread(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid;

  ASSERT((g_fd = open(MESSAGE_FILE, O_WRONLY|O_CREATE)) >= 0);
  ASSERT((tid = thread_create(close_global_fd, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  ASSERT(write(g_fd, MESSAGE, sizeof(MESSAGE)) == -1);
}

void
check_pid_overflow(void)
{
  int i, pid, ourpid;

  ourpid = getpid();

  for(i = 0; i < MAX_PID * 2; i++){
    pid = fork();    
    if(pid == 0)
      exit();

    ASSERT(pid > 0);
    ASSERT(wait() == pid);
    ASSERT(pid != ourpid && pid != MIN_PID);
  }
}

void
check_tid_overflow(void)
{
  char stack[NORMAL_STACK_SIZE];
  int i, tid;
  int ourtid = gettid();

  ourtid = gettid();

  for(i = 0; i < MAX_TID * 2; i++){
    ASSERT((tid = thread_create(empty, stack, sizeof(stack))) > 0);
    ASSERT(thread_join(tid, 0) == tid);

    ASSERT(tid != ourtid && tid != MIN_TID);
  }
}

void
max_threads(void)
{
  int tids[NTHREADS - 1];
  int random_index = (NTHREADS - 1) / 2;
  int n;
  char *stack = malloc(SMALL_STACK_SIZE * (NTHREADS - 1));

  ASSERT(stack);

  memset(tids, 0, sizeof(tids));

  for(n = 0; n < NTHREADS - 1; n++)
    ASSERT((tids[n] = thread_create(empty, stack + (n * SMALL_STACK_SIZE), SMALL_STACK_SIZE)) > 0);

  // We should have reached the max, so creating new ones shouldn't succeed.
  ASSERT(thread_create(empty, stack + (n * SMALL_STACK_SIZE), SMALL_STACK_SIZE) < 0);

  // Join some random thread, and create again in order to check if "holes" in the tid can be filled.
  // The tid of the new thread should be the same as the one joined as we are at full capacity before the join.
  ASSERT(thread_join(tids[random_index], 0) == tids[random_index]);
  ASSERT(thread_create(empty, stack + (random_index * SMALL_STACK_SIZE), SMALL_STACK_SIZE) == tids[random_index]);

  // Check that we can join all the threads that were created.
  for(; n > 0; n--)
    ASSERT(thread_join(tids[n-1], 0) == tids[n-1]);

  free(stack);
}

void
chdir_in_thread(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid;
  struct stat st;

  ASSERT(chdir("/") == 0);
  ASSERT(mkdir(DIR1) == 0);
  ASSERT(mkdir(DIR1 "/" DIR2) == 0);

  // The created structure is as follows:
  // cwd -> /
  //          DIR1
  //            DIR2

  ASSERT(stat(DIR1, &st) == 0);
  ASSERT(stat(DIR2, &st) < 0);

  ASSERT((tid = thread_create(chdir_to_dir1, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  // Thread should have chdir-ed to DIR1, so accessing DIR2 should work but accessing DIR1 fail, as threads share their cwd.
  ASSERT(stat(DIR1, &st) < 0);
  ASSERT(stat(DIR2, &st) == 0);

  // Clean.
  ASSERT(chdir("/") >= 0);
  ASSERT(unlink(DIR1 "/" DIR2) >= 0);
  ASSERT(unlink(DIR1) >= 0);
}

void
join_same_thread_twice_when_its_still_running(void)
{
  char stack1[NORMAL_STACK_SIZE];
  char stack2[NORMAL_STACK_SIZE];
  int tid;

  ASSERT((g_tid = thread_create(sleep_some_time, stack1, sizeof(stack1))) > 0);
  ASSERT((tid = thread_create(join_global_tid, stack2, sizeof(stack2))) > 0);

  // Give the other thread some time to join.
  ASSERT(sleep(SLEEP_SMALL_AMOUNT) == 0);

  // This should happen after the `join_global_tid` thread joined `sleep_some_time` but before it exited.
  ASSERT(thread_join(g_tid, 0) == -2);

  ASSERT(thread_join(tid, 0) == tid);
}

void
join_twice(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid;

  ASSERT((tid = thread_create(empty, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);
  ASSERT(thread_join(tid, 0) == -1);
}

void
child_cant_join_parent_thread(void)
{
  int tid, pid;
  char stack[NORMAL_STACK_SIZE];

  ASSERT((tid = thread_create(empty, stack, sizeof(stack))) > 0);

  pid = fork();
  ASSERT(pid >= 0);

  if(pid == 0){
    ASSERT_HANG(thread_join(tid, 0) == -1);
    exit();
  }

  ASSERT(wait() == pid);
  ASSERT(thread_join(tid, 0) == tid);
}

void
check_different_fds_across_threads(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid, fd;

  ASSERT((tid = thread_create(open_file_and_set_global_fd, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  ASSERT((fd = open(MESSAGE_FILE, O_WRONLY|O_CREATE)) >= 0);
  ASSERT(fd != g_fd);

  ASSERT(close(fd) == 0);
  ASSERT(close(g_fd) == 0);
}

void
check_threads_share_max_open_files_amount(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid, fd;
  int fds[NOFILE];
  int i = 0;
  
  memset(fds, 0, sizeof(fds));

  // Open as many fds as possible (Should typically be 13, as 0-2 are already taken and there are 16 total, but it's not something this test should assume).
  while((fd = open(MESSAGE_FILE, O_WRONLY|O_CREATE)) >= 0){
    fds[i++] = fd;
    ASSERT(i < NOFILE);
  }

  ASSERT(i >= 1); // This test can't work if we couldn't have opened at least 1 file.

  // This should not succeed in opening a file as the table is supposed to be full.
  ASSERT((tid = thread_create(open_file_expect_failure, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  // Arbitrarily close one of the opened files.
  ASSERT(close(fds[0]) == 0);
  fds[0] = 0;

  // This should succeed in opening a file.
  ASSERT((tid = thread_create(open_file_and_set_global_fd, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);

  // Close all the open fds.
  ASSERT(close(g_fd) == 0);

  for (; i > 0; i--)
    if (fds[i - 1] != 0)
      ASSERT(close(fds[i - 1]) == 0);
}

void
create_thread_and_call_exec(void)
{
  char stack[NORMAL_STACK_SIZE];
  struct stat st;
  char *argv[] = { 0 };
  int tid, pid = fork();

  ASSERT(pid >= 0);

  // Doing the exec in a child so we can still continue in the tests afterwards
  if(pid == 0){
    ASSERT_HANG((tid = thread_create(sleep_and_create_file, stack, sizeof(stack))) > 0);

    // We are using thread_exit in order to not terminate the above created thread just by
    // the exec-ed program exit-ing.
    ASSERT_HANG(exec("thread_exit", argv) != -1);

    ASSERT_HANG(0); // Shouldn't reach here, but just to be safe.
  }

  ASSERT(wait() == pid);

  if(stat(SHOULDNT_EXIST_FILE, &st) == 0){
    PRINT_ERROR("File %s shouldn't exist but it does", SHOULDNT_EXIST_FILE);
    exit();
  }
  else
    unlink(SHOULDNT_EXIST_FILE);
}

void
access_illegal_memory_in_thread(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid;

  printf(1, "NOTE: trap 14 is expected bellow\n");

  ASSERT((tid = thread_create(access_illegal_memory, stack, sizeof(stack))) > 0);
  ASSERT(thread_join(tid, 0) == tid);
}

void
wait_and_exit(void)
{
  char stack[NORMAL_STACK_SIZE];
  int tid, pid1, pid2;

  ASSERT((pid1 = fork()) >= 0);
  if(pid1 == 0){    
    ASSERT_HANG((pid2 = fork()) >= 0);
    if(pid2 == 0){
      for(;;){}
    }

    ASSERT_HANG((tid = thread_create(sleep_and_exit, stack, sizeof(stack))) > 0);

    wait();
    ASSERT_HANG(0); // Shouldn't have reached here.
  }

  ASSERT(wait() == pid1);
}

// Like usertests' linkunlink test, but with threads instead of procs.
void
linkunlink(void)
{
  int tids[NTHREADS - 1];
  int n;
  char *stack;

  stack = malloc(SMALL_STACK_SIZE * (NTHREADS - 1));
  ASSERT(stack);

  memset(tids, 0, sizeof(tids));

  unlink("x");

  for(n = 0; n < NTHREADS - 1; n++)
    ASSERT((tids[n] = thread_create(spam_link_unlink_close, stack + (n * SMALL_STACK_SIZE), SMALL_STACK_SIZE)) > 0);

  for(; n > 0; n--)
    ASSERT(thread_join(tids[n-1], 0) == tids[n-1]);

  free(stack);
}

int
main(void)
{
  printf(1, "Starting thread tests...\n");

  TEST(verify_different_tids_same_process_group);
  TEST(illegal_thread_create);
  TEST(gettid_twice);
  TEST(join_the_current_thread);
  TEST(kill_child_with_threads);
  TEST(exit_without_thread_join);
  TEST(fork_and_wait_in_thread);
  TEST(fork_wait_in_parent_thread_exit_in_child);
  TEST(wait_in_another_thread_after_parent_thread_died);
  TEST(return_tid_from_thread);
  TEST(thread_exit_illegal_address_from_thread);
  TEST(read_and_change_global_in_thread);
  TEST(open_global_fd_and_write_in_thread);
  TEST(unable_to_write_after_close_in_thread);
  TEST(check_pid_overflow);
  TEST(check_tid_overflow);
  TEST(max_threads);
  TEST(chdir_in_thread);
  TEST(join_same_thread_twice_when_its_still_running);
  TEST(join_twice);
  TEST(child_cant_join_parent_thread);
  TEST(check_different_fds_across_threads);
  TEST(check_threads_share_max_open_files_amount);
  TEST(create_thread_and_call_exec);
  TEST(access_illegal_memory_in_thread);
  TEST(wait_and_exit);
  TEST(linkunlink); // TODO: fix

  printf(1, PARTYPOPPER " All tests passed " PARTYPOPPER "\n");

  exit();
}