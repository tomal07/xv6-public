#include "syscall.h"
#include "traps.h"

#define SYSCALL_SPECIFY_FUNCTION_NAME(funcname, syscallname) \
  .globl funcname; \
  funcname: \
    movl $SYS_ ## syscallname, %eax; \
    int $T_SYSCALL; \
    ret

#define SYSCALL(name) SYSCALL_SPECIFY_FUNCTION_NAME(name, name)

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

// thread_create is a special case, as there needs to be the user-space function called thread_create
// for it to supply a user-space function as a wrapper. So the syscall itself would be called thread_create_sys
// while the function the user-space will use to interface with it will be called thread_create, otherwise we will get
// a 'multiple definitions' error
SYSCALL_SPECIFY_FUNCTION_NAME(thread_create_sys, thread_create)
