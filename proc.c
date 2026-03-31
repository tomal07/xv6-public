#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct spinlock processlocks[NPROCGRPS];

int currmaxpid = 0;
int currmaxtid[NPROCGRPS];

static struct proc *initproc;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  int i;

  memset(currmaxtid, 0, sizeof(currmaxtid));
  initlock(&ptable.lock, "ptable");

  for(i = 0; i < NPROCGRPS; i++)
    initlock(&processlocks[i], "processlock");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// Assumes the ptable is locked
static int
getfreepid(void)
{
  int pid, pidexists = 0;
  struct proc *p;

  // Check for the usual case - a thread is created and the current max pid + 1 is free.
  pid = ++currmaxpid;

  if(pid == MAX_PID)
    currmaxpid = MIN_PID;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pid == pid)
      pidexists = 1;

  if(!pidexists)
    return pid;

  // If not, we need to find "holes", otherwise there is no space left.
  for(pid = MIN_PID; pid <= MAX_PID; pid++){
    pidexists = 0;
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pid == pid){
        pidexists = 1;
        break;
      }
    }

    if(!pidexists)
      return pid;
  }

  return -1;
}

// Assumes the ptable is locked
static int
getfreetid(int pid)
{
  int tid, tidexists = 0;
  struct proc *p;

  // Check for the usual case - a thread is created and the current max tid + 1 is free.
  tid = ++currmaxtid[pid - MIN_PID];

  if(tid == MAX_TID)
    currmaxtid[pid - MIN_PID] = MIN_TID;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pid == pid && tid == p->tid)
      tidexists = 1;

  if(!tidexists)
    return tid;

  // If not, we need to find "holes", otherwise there is no space left.
  for(tid = MIN_TID; tid <= MAX_TID; tid++){
    tidexists = 0;
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pid == pid && tid == p->tid){
        tidexists = 1;
        break;
      }
    }

    if(!tidexists)
      return tid;
  }

  return -1;
}

// Assumes the ptable is locked.
int
setupkstack(struct proc *p)
{
  char *sp;

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->pid = 0;
    p->tid = 0;
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return 1;
}

// Assumes the ptable is locked.
struct proc*
getunusedproc(void)
{
  struct proc *p;

    // Find an available spot in the table
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      return p;

  return 0;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  int pid;

  acquire(&ptable.lock);

  if((p = getunusedproc()) == 0){
    release(&ptable.lock);
    return 0;
  }

  if((pid = getfreepid()) == -1){
    release(&ptable.lock);
    return 0;
  }

  p->state = EMBRYO;
  p->pid = pid;
  p->tid = MIN_TID;

  if(setupkstack(p) == 0){
    // The pid,tid and state cleanup happens in `setupkstack`
    release(&ptable.lock);
    return 0;
  }

  if ((p->cwd = (struct inode**)kalloc()) == 0){
    freeproc(p);
    release(&ptable.lock);
    return 0;
  }

  // Create and setup a new process file table.
  if ((p->procfiletable = (struct procfiletable*)kalloc()) == 0){
    freeproc(p);
    release(&ptable.lock);
    return 0;
  }
  memset(p->procfiletable->ofile, 0, sizeof(p->procfiletable->ofile));

  release(&ptable.lock);

  return p;
}

static struct proc*
allocthread(void)
{
  struct proc *p;
  int tid, pid;

  acquire(&ptable.lock);

  if((p = getunusedproc()) == 0){
    release(&ptable.lock);
    return 0;
  }

  pid = myproc()->pid;

  if((tid = getfreetid(pid)) == -1){
    release(&ptable.lock);
    return 0;
  }

  p->state = EMBRYO;
  p->pid = pid;
  p->tid = tid;
  
  if(setupkstack(p) == 0){
    // The pid,tid and state cleanup happens in `setupkstack`
    release(&ptable.lock);
    return 0;
  }

  p->procfiletable = myproc()->procfiletable;

  release(&ptable.lock);

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  *p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
// Assumes the process lock is locked.
int
growproc(int n)
{
  uint sz;
  struct proc *p, *curproc = myproc();

  acquire(&ptable.lock);

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  }
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pid == curproc->pid)
      p->sz = sz;

  switchuvm(curproc);

  release(&ptable.lock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  proclock();

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    procrelease();
    acquire(&ptable.lock);
    freeproc(np);
    release(&ptable.lock);
    return -1;
  }

  np->sz = curproc->sz;
  // Handle open files.
  for(i = 0; i < NOFILE; i++)
    if(curproc->procfiletable->ofile[i])
        np->procfiletable->ofile[i] = filedup(curproc->procfiletable->ofile[i]);
  *np->cwd = idup(*curproc->cwd);

  procrelease();

  *np->tf = *curproc->tf;
  np->ppid = curproc->pid;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;
  np->joined = 0;
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  // If called from thread_create then the tid is expected, else if it was called from
  // fork, the pid is expected.
  return pid;
}

int
thread_create(void (*func) (void), void *tstack, int stacksize)
{
  int tid;
  struct proc *newthread;
  struct proc *curproc = myproc();

  if(!stacksize)
    return -1;

  // Allocate thread.
  if((newthread = allocthread()) == 0)
    return -1;

  // Copy thread state from proc.
  proclock();
  newthread->pgdir = curproc->pgdir;
  newthread->sz = curproc->sz;
  newthread->cwd = curproc->cwd;
  procrelease();

  *newthread->tf = *curproc->tf;
  
  newthread->ppid = curproc->ppid;

  // Set it to jump to `func`.
  newthread->tf->eip = (uint)func;
  newthread->tf->esp = (uint)tstack + stacksize;

  // The return address. The userspace will actually get a page-fault because it will try to access
  // a kernel-space address, but as we do `thread_exit` on a page-fault anyway, it will function the same.
  // See trap.c
  newthread->tf->esp -= 4;
  *(uint*)(newthread->tf->esp) = (uint)thread_exit;

  newthread->joined = 0;

  safestrcpy(newthread->name, curproc->name, sizeof(curproc->name));

  tid = newthread->tid;

  acquire(&ptable.lock);

  newthread->state = RUNNABLE;

  release(&ptable.lock);

  return tid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd, islastofpid = 1;

  if(curproc == initproc)
    panic("init exiting");

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pid == curproc->pid && p != curproc)
      islastofpid = 0;

  // Only if this is the last thread to exit we free resources shared by threads.
  if(islastofpid){
    release(&ptable.lock);
    // Close all open files.
    for(fd = 0; fd < NOFILE; fd++){
      if(curproc->procfiletable->ofile[fd]){
        fileclose(curproc->procfiletable->ofile[fd]);
        curproc->procfiletable->ofile[fd] = 0;
      }
    }

    // Clean cwd
    begin_op();
    iput(*curproc->cwd);
    end_op();

    kfree((char*)curproc->cwd);
    curproc->cwd = 0;

    // Clean the process file table
    kfree((char*)curproc->procfiletable);
    curproc->procfiletable = 0;

    acquire(&ptable.lock);
  }

  // Parent or other threads might be sleeping in wait().
  wakeup1(&processlocks[curproc->ppid - MIN_PID]);
  wakeup1(&processlocks[curproc->pid - MIN_PID]);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->ppid == curproc->pid){
      p->ppid = initproc->pid;
      if(p->state == ZOMBIE)
        wakeup1(&processlocks[initproc->pid - MIN_PID]);
    } else if(p->pid == curproc->pid && p != curproc)
      p->killed = PROC_KILLED;
  }

  curproc->state = ZOMBIE;
  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

void thread_exit(void *retval)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int islastofpid = 1;

  acquire(&ptable.lock);

  // Check if this is the last thread of the group.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p != curproc && p->pid == curproc->pid){
      islastofpid = 0;
      break;
    }
  }

  release(&ptable.lock);

  if(islastofpid){
    exit();
    panic("continued after exit");
  }

  curproc->retval = retval;

  acquire(&ptable.lock);

  // Another thread might be sleeping in thread_join().
  if(curproc->joined)
    wakeup1(curproc->joined);

  // Jump into the scheduler, never to return.
  curproc->state = THREAD_ZOMBIE;
  sched();
  panic("zombie exit");
}

// Assumes the ptable is locked.
void
freeproc(struct proc *p)
{
  kfree(p->kstack);
  p->kstack = 0;
  p->pid = 0;
  p->tid = 0;
  p->joined = 0;
  p->ppid = 0;
  p->name[0] = 0;
  p->killed = NOT_KILLED;

  // It is freed in wait, only once as it's a shared resource across threads.
  p->pgdir = 0;

  p->state = UNUSED;
}

// Assumes the ptable is not already locked.
int
killotherthreads(void)
{
  struct proc *p, *curproc = myproc();
  int n, tids[NTHREADS - 1];

  acquire(&ptable.lock);

  for(p = ptable.proc, n = 0; p < &ptable.proc[NPROC]; p++){
    if(p->pid == curproc->pid && p != curproc){
      tids[n++] = p->tid;
      p->killed = THREAD_KILLED;
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
    }
  }

  release(&ptable.lock);

  // Could any more threads be created from this point on (in this process, until this function ends)?
  // No, as the ptable is locked when storing them, and `killed` is checked
  // after and before every syscall.

  // Join them all.
  for(; n > 0; n--)
    if(thread_join(tids[n - 1], 0) != tids[n - 1])
      return -1;

  return 0;  
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p, *proc, *lasttoclean = 0;
  int towaitfor, havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    towaitfor = 0;
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      // If we didn't find a process that has a zombie yet, skip if p is not a child of curproc.
      // If we already found an exited process, skip all others, even if they are children
      // in order for `wait` to be wait only for a single child.
      if((!lasttoclean && p->ppid != curproc->pid) || (lasttoclean && p->pid != lasttoclean->pid))
        continue;

      havekids = 1;

      if (lasttoclean && lasttoclean == p)
        continue;

      // If it's a THREAD_ZOMBIE, we shouldn't necessarily wait for the other threads to exit,
      // because we have no way to know how soon that would happen, and other children could exit
      // before that. Therefore, only handle thread zombies once we have another thread from the process
      // that exitted, and when finding one check previous entires in the ptable for thread zombies
      // we missed.
      if(p->state == ZOMBIE || (lasttoclean && p->state == THREAD_ZOMBIE)){
        // We need to continue to search for other threads of that process,
        // so keep one of them to free last and free the rest of them (while not freeing shared resources).
        if(lasttoclean)
          freeproc(p);
        else{
          lasttoclean = p;
          
          // Check for missed zombies in the current loop.
          for(proc = ptable.proc; proc < p; proc++)
            if(proc->state == THREAD_ZOMBIE && proc->pid == lasttoclean->pid)
              freeproc(proc);
        }
      } else
        towaitfor++;
    }

    // Only one zombie means that we can now clean the thread we kept for last, as well as the process' shared resources.
    if(lasttoclean && towaitfor == 0){
      pid = lasttoclean->pid;
      freevm(lasttoclean->pgdir);
      freeproc(lasttoclean);

      release(&ptable.lock);
      return pid;
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(&processlocks[curproc->pid - MIN_PID], &ptable.lock);  //DOC: wait-sleep
  }
}

int
thread_join(int tid, void **retval)
{
  struct proc *p, *thread = 0;
  struct proc *curproc = myproc();

  if(curproc->tid == tid)
    return -1;

  acquire(&ptable.lock);

  // Scan through the table looking for the desired thread
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == curproc->pid && p->tid == tid){
      if(p->joined){
        // The target thread is already joined
        release(&ptable.lock);
        return -2;
      }

      p->joined = curproc;

      thread = p;
      break;
    }
  }

  // No point waiting if another thread doesn't exist
  if(!thread){
    release(&ptable.lock);
    return -1;
  }

  for(;;){
    if(thread->state == THREAD_ZOMBIE){
      if(retval)
        *retval = thread->retval;
      freeproc(thread);
      release(&ptable.lock);
      return tid;
    }

    if(curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for the target thread to do thread_exit.  (See wakeup1 call in thread_exit.)
    sleep(curproc, &ptable.lock);
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
// Can handle the ptable already being locked by this cpu.
void
wakeup(void *chan)
{
  int needtolock = !holding(&ptable.lock);

  if(needtolock)
    acquire(&ptable.lock);

  wakeup1(chan);

  if(needtolock)
    release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  int found = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      found = 1;
      p->killed = PROC_KILLED;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
    }
  }
  release(&ptable.lock);

  return found ? 0 : -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// This function and the matching one bellow are utility functions
// used to lock/unlock the lock which is specific to the current process - shared by threads.
// It is meant to guard resources shared by threads (sz, pgdir etc.).
void
proclock(void)
{
  acquire(&processlocks[myproc()->pid - MIN_PID]);
}

void
procrelease(void)
{
  release(&processlocks[myproc()->pid - MIN_PID]);
}

// Assumes the process lock is locked.
int
validaddr(void* addr, int size)
{
  struct proc *curproc = myproc();

  return !(size < 0 || (uint)addr >= curproc->sz || (uint)addr+size > curproc->sz);
}
