#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "mmu.h"
#include "proc.h"

struct sem {
  struct spinlock lock;
  int used;
  int counter;
};

struct sem binarytable[NSEM];
struct sem countingtable[NSEM];

void
seminit(void)
{
  int i;

  for(i = 0; i < NSEM; i++){
    binarytable[i].used = 0;
    binarytable[i].counter = 0;
    initlock(&binarytable[i].lock, "binary_sem_lock");

    countingtable[i].used = 0;
    countingtable[i].counter = 0;
    initlock(&countingtable[i].lock, "counting_sem_lock");
  }
}

int
sem_destroy(struct sem *s)
{
  acquire(&s->lock);

  if(!s->used){
    release(&s->lock);
    return -1;
  }

  s->used = 0;
  s->counter = 0;

  release(&s->lock);
  return 0;
}

int
sem_wait(struct sem *s)
{
  acquire(&s->lock);

  if(!s->used){
    release(&s->lock);
    return -1;
  }

  for(;;){
    if(myproc()->killed){
      release(&s->lock);
      return -1;
    }

    if(s->counter > 0){
      s->counter--;
      release(&s->lock);
      return 0;
    }

    // sleep reaquires the given lock when returning.
    // Will be woken by post, or when the process/thread is killed
    sleep(s, &s->lock);
  }

  release(&s->lock);
  return 0;
}

int
counting_sem_init(int counter)
{
  int i;

  if (counter < 0 || counter > MAX_COUNTING_SEM_COUNT)
    return -1;

  for(i = 0; i < NSEM; i++){
    acquire(&countingtable[i].lock);

    if(!countingtable[i].used){
      // Found a free spot
      countingtable[i].used = 1;
      countingtable[i].counter = counter;

      release(&countingtable[i].lock);
      return i;
    }

    release(&countingtable[i].lock);
  }

  // No free spot was found
  return -1;
}

int
counting_sem_wait(int sid)
{
  if (sid < 0 || sid >= NELEM(countingtable))
    return -1;

  return sem_wait(&countingtable[sid]);
}

int
counting_sem_post(int sid)
{
  if (sid < 0 || sid >= NELEM(countingtable))
    return -1;

  acquire(&countingtable[sid].lock);

  if(!countingtable[sid].used ||
      countingtable[sid].counter == MAX_COUNTING_SEM_COUNT){
    release(&countingtable[sid].lock);
    return -1;
  }

  countingtable[sid].counter++;
  wakeup(&countingtable[sid]);

  release(&countingtable[sid].lock);
  return 0;
}

int
counting_sem_destroy(int sid)
{
  if (sid < 0 || sid >= NELEM(countingtable))
    return -1;

  return sem_destroy(&countingtable[sid]);
}

int
binary_sem_init(int counter)
{
  int i;

  if (counter < 0 || counter > 1)
    return -1;

  for(i = 0; i < NSEM; i++){
    acquire(&binarytable[i].lock);

    if(!binarytable[i].used){
      // Found a free spot
      binarytable[i].used = 1;
      binarytable[i].counter = counter;

      release(&binarytable[i].lock);
      return i;
    }

    release(&binarytable[i].lock);
  }

  // No free spot was found
  return -1;
}

int
binary_sem_wait(int sid)
{
  if (sid < 0 || sid >= NELEM(binarytable))
    return -1;

  return sem_wait(&binarytable[sid]);
}

int
binary_sem_post(int sid)
{
  if (sid < 0 || sid >= NELEM(binarytable))
    return -1;

  acquire(&binarytable[sid].lock);

  if(!binarytable[sid].used){
    release(&binarytable[sid].lock);
    return -1;
  }

  if(binarytable[sid].counter == 1){
    // Binary Semaphore "absorbs" extra posts
    release(&binarytable[sid].lock);
    return 0;
  }

  binarytable[sid].counter = 1;
  wakeup(&binarytable[sid]);

  release(&binarytable[sid].lock);
  return 0;
}

int
binary_sem_destroy(int sid)
{
  if (sid < 0 || sid >= NELEM(binarytable))
    return -1;

  return sem_destroy(&binarytable[sid]);
}
