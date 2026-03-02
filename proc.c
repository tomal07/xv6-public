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

static struct proc *initproc;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
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
  int pid, foundpid = 0;
  struct proc *p;

  // Loop until we find an available pid
  for(pid = MIN_PID; pid <= MAX_PID; pid++){
    foundpid = 1;
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != UNUSED && p->pid == pid){
        foundpid = 0;
        break;
      }
    }

    if(foundpid)
      return pid;
  }

  if(!foundpid)
    return -1;

  return pid;
}

// Assumes the ptable is locked
static int
getfreetid(int pid)
{
  int tid, foundtid = 0;
  struct proc *p;

  // Loop until we find an available tid
  for(tid = MIN_TID; tid <= MAX_TID; tid++){
    foundtid = 1;
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != UNUSED && p->pid == pid && tid == p->tid){
        foundtid = 0;
        break;
      }
    }

    if(foundtid)
      return tid;
  }

  if(!foundtid)
    return -1;

  return tid;
}

// Returns the amount of existing process groups.
// Assumes the number didn't exceed the max (NPROCGRPS), as this is the function used to avoid exceeding that.
// Assumes the ptable is locked.
static int
procgrpsamount(void)
{
  int i, found, amount = 0;
  int grpsfound[NPROCGRPS];
  struct proc *p;

  memset(grpsfound, 0, sizeof(grpsfound));

  for(p = ptable.proc; p < &ptable.proc[NPROC] && amount < NPROCGRPS; p++){
    if(p->state != UNUSED){
      // Check if we already counted that pid
      found = 0;
      for(i = 0; i < amount && !found; i++)
        if(grpsfound[i] == p->pid)
          found = 1;
      
      // If we didn't, add it to the array
      if(!found)
        grpsfound[amount++] = p->pid;
    }
  }

  return amount;
}

// Returns the amount of threads in the given (by the pid) process group.
// Assumes the number didn't exceed the max (NTHREADS), as this is the function used to avoid exceeding that.
// Assumes the ptable is locked.
static int
threadamount(int pid)
{
  int amount = 0;
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC] && amount < NTHREADS; p++)
    if(p->state != UNUSED && p->pid == pid)
      amount++;

  return amount;
}

static struct proc*
getunusedproc(int isthread)
{
  struct proc *p, *availableproc = 0;
  int tid, pid;

  acquire(&ptable.lock);

  // Threads share pids, while non-threads (or more accurately new processes that currently have 1 thread)
  // get a new pid and can arbitrarily choose a tid.
  if(isthread){
    pid = myproc()->pid;

    if(threadamount(pid) == NTHREADS)
      goto error;

    if((tid = getfreetid(pid)) == -1)
      goto error;
  } else{
    if(procgrpsamount() == NPROCGRPS)
      goto error;

    if((pid = getfreepid()) == -1)
      goto error;

    tid = MIN_TID;
  }

  // Find an available spot in the table
  for(p = ptable.proc; p < &ptable.proc[NPROC] && !availableproc; p++)
    if(p->state == UNUSED)
      availableproc = p;

  if(!availableproc)
    goto error;

  availableproc->state = EMBRYO;
  availableproc->pid = pid;
  availableproc->tid = tid;

  release(&ptable.lock);
  return availableproc;

error:
  release(&ptable.lock);
  return 0;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
newproc(int isthread)
{
  struct proc *p;
  char *sp;

  p = getunusedproc(isthread);
  if(p == 0)
    return 0;

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


  if(isthread)
    p->ft = myproc()->ft;
  else{
    if ((p->ft = (struct ftlock*)kalloc()) == 0){
      freeproc(p);
      return 0;
    }

    memset(p->ft->ofile, 0, sizeof(p->ft->ofile));

    initlock(&p->ft->lock, "processfiletable");
  }

  return p;
}

static struct proc*
allocproc(void)
{
  return newproc(0);
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
  p->cwd = namei("/");

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

// Creates a new process, with all the necessary setup.
// If a thread is requested, returns the tid, else the pid.
int
clone(int isthread, void (*func) (void), void *tstack, int stacksize, void (*wrapper) (uint))
{
  int i, pid, tid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = newproc(isthread)) == 0)
    return -1;

  // Copy process state from proc.
  if(isthread)
    np->pgdir = curproc->pgdir;
  else{
    if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
      kfree(np->kstack);
      np->kstack = 0;
      np->pid = 0;
      np->tid = 0;
      np->state = UNUSED;
      return -1;
    }
  }
  np->sz = curproc->sz;
  *np->tf = *curproc->tf;
  
  if(isthread){
    np->parent = curproc->parent;

    // Set it to jump to `wrapper` with `func` as it's argument when returning to the user-space.
    np->tf->eip = (uint)wrapper;
    np->tf->esp = (uint)tstack + stacksize;

    np->tf->esp -= 4;
    *(uint*)(np->tf->esp) = (uint)func;

    // Space for the return address for the wrapper.
    // Subtracting 4 bytes is critical for the wrapper function to get `func` as it's argument, but the actual value is irrelevant
    // as the wrapper end by doing `thread_exit`. For good measure putting 0 there.
    np->tf->esp -= 4;
    *(uint*)(np->tf->esp) = 0;
  }
  else{
    np->parent = curproc;

    // Clear %eax so that fork returns 0 in the child.
    np->tf->eax = 0;
  }

  np->joined = 0;

  // Handle open files.
  if(!isthread){
    acquire(&curproc->ft->lock);

    for(i = 0; i < NOFILE; i++)
      if(curproc->ft->ofile[i])
          np->ft->ofile[i] = filedup(curproc->ft->ofile[i]);

    release(&curproc->ft->lock);
  }

  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  if(isthread)
    tid = np->tid;
  else
    pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  // If called from thread_create then the tid is expected, else if it was called from
  // fork, the pid is expected.
  return isthread ? tid : pid;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  // Clone as a non-thread (no user provided function, wrapper or stack)
  return clone(0, 0, 0, 0, 0);
}

int
thread_create(void (*func) (void), void *tstack, int stacksize, void (*wrapper) (uint))
{
  if(!func || !tstack || !stacksize || !wrapper)
    return -1;

  return clone(1, func, tstack, stacksize, wrapper);
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent->pid == curproc->pid){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    } else if(p->pid == curproc->pid)
      p->state = ZOMBIE;
  }

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ft->ofile[fd]){
      fileclose(curproc->ft->ofile[fd]);
      curproc->ft->ofile[fd] = 0;
    }
  }

  kfree((char*)curproc->ft);
  curproc->ft = 0;

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

void
freeproc(struct proc *p)
{
  kfree(p->kstack);
  p->kstack = 0;
  p->pid = 0;
  p->tid = 0;
  p->joined = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->state = UNUSED;
}

// Does two operations that need to be inside the same ptable lock.
// Assumes the ptable is not already locked.
pde_t*
kill_other_threads_and_switch_pgdir(pde_t *newpgdir)
{
  pde_t *oldpgdir;
  struct proc *p, *curproc = myproc();

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->pid == curproc->pid && p != curproc)
      freeproc(p);

  oldpgdir = curproc->pgdir;
  curproc->pgdir = newpgdir;

  release(&ptable.lock);

  return oldpgdir;  
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid = 0;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      // If we didn't find a pid yet, skip if p is not a child of curproc.
      // If we already found a child pid, skip all others, even if they are children
      // in order for `wait` to be wait only for a single child.
      if((!pid && p->parent->pid != curproc->pid) || (pid && p->pid != pid))
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        // We need to continue to search for other procs with the same pid (aka threads)
        // so if this is the first time free the address space as it is shared across threads.
        if(!pid){
          pid = p->pid;
          freevm(p->pgdir);
        }
        freeproc(p);
      }
    }

    if(pid){
      release(&ptable.lock);
      return pid;
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
thread_join(int tid, void **retval)
{
  struct proc *p, *thread = 0;
  struct proc *curproc = myproc();

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

  // No point waiting if the thread doesn't exist
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
      p->killed = 1;
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
