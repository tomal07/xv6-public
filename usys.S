#include "syscall.h"
#include "traps.h"

#define SYSCALL(name) \
  .globl name; \
  name: \
    movl $SYS_ ## name, %eax; \
    int $T_SYSCALL; \
    ret

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(pipe)
SYSCALL(read)
SYSCALL(write)
SYSCALL(close)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(open)
SYSCALL(mknod)
SYSCALL(unlink)
SYSCALL(fstat)
SYSCALL(link)
SYSCALL(mkdir)
SYSCALL(chdir)
SYSCALL(dup)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
SYSCALL(thread_exit)
SYSCALL(thread_join)
SYSCALL(gettid)
SYSCALL(thread_create)
SYSCALL(counting_sem_init)
SYSCALL(counting_sem_wait)
SYSCALL(counting_sem_post)
SYSCALL(counting_sem_destroy)
SYSCALL(binary_sem_init)
SYSCALL(binary_sem_wait)
SYSCALL(binary_sem_post)
SYSCALL(binary_sem_destroy)
