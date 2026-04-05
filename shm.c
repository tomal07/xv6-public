#include "param.h"
#include "types.h"
#include "defs.h"
#include "mmu.h"
#include "proc.h"
#include "memlayout.h"
#include "spinlock.h"

#define KEYS_AMOUNT (MAX_SHM_KEY - MIN_SHM_KEY + 1)

struct virtpage {
  int used;
  // TOOD: I now rely on both the pgdir (see freepage) and on the pid (see getat).
  // This is kind of weird, ideally it would be one or the other (unified api).
  // One possibility is removing the virtpages from the shmentry struct,
  // and using only pgdir (walkpgdir in search of phypage).
  // It might be fine, need to further see. The real instructions may help.
  // Another idea I had is to put the shmkey in the pgdir entry perm, but I might be
  // overdoing it already by having SHM there as a perm, kinda weird.
  pde_t *pgdir;
  char *addr;
};

struct shmentry {
  struct spinlock lock;
  int num_pages;
  char *phypages[MAX_SHM_PAGES];
  int refs;
  struct virtpage virtpages[NPROCGRPS];
};

struct shmentry shmtable[KEYS_AMOUNT];

void
cleanentry(struct shmentry *entry)
{
  entry->num_pages = 0;
  memset(entry->phypages, 0, sizeof(entry->phypages));

  entry->refs = 0;
  memset(entry->virtpages, 0, sizeof(entry->virtpages));
}

void
shminit(void)
{
  struct shmentry *entry;

  for(entry = shmtable; entry < &shmtable[KEYS_AMOUNT]; entry++){
    initlock(&entry->lock, "shmentry");
    cleanentry(entry);
  }
}

// Assumes the process specific lock is locked.
void
shmfreepage(pde_t *pgdir, char *addr)
{
  struct shmentry *entry;
  struct virtpage *vpage;
  int i;

  for(entry = shmtable; entry < &shmtable[KEYS_AMOUNT]; entry++){
    acquire(&entry->lock);

    for(vpage = entry->virtpages; vpage < &entry->virtpages[NPROCGRPS]; vpage++){
      if(vpage->used && vpage->pgdir == pgdir && vpage->addr == addr){
        vpage->used = 0;
        vpage->pgdir = 0;
        vpage->addr = 0;
  
        // TODO: do I fear it already being 0?
        if((--entry->refs) == 0){
          for(i = 0; i < entry->num_pages; i++)
            kfree(entry->phypages[i]);
          cleanentry(entry);
        }
  
        release(&entry->lock);
        return;
      }
    }

    release(&entry->lock);
  }
}

void
shmcopieduvm(pde_t *newpgdir, int newpid)
{
  struct shmentry *entry;
  int curpid = myproc()->pid;

  for(entry = shmtable; entry < &shmtable[KEYS_AMOUNT]; entry++){
    acquire(&entry->lock);

    if(entry->virtpages[curpid - MIN_PID].used){
      entry->refs++;
      entry->virtpages[newpid - MIN_PID].used = 1;
      entry->virtpages[newpid - MIN_PID].pgdir = newpgdir;
      entry->virtpages[newpid - MIN_PID].addr = entry->virtpages[curpid - MIN_PID].addr;
    }

    release(&entry->lock);
  }
}

// TODO: int? void*? If void*, what should the error value be?
int
shmgetat(int key, int num_pages)
{
  int i, j, pidindex, newsz;
  uint addr;
  struct shmentry *entry;
  struct proc *curproc = myproc();

  if(key < MIN_SHM_KEY || key > MAX_SHM_KEY)
    return -1;

  entry = &shmtable[key - MIN_SHM_KEY];
  acquire(&entry->lock);

  // Check if the process already mapped with this key.
  pidindex = curproc->pid - MIN_PID;
  if(entry->virtpages[pidindex].used){
    release(&entry->lock);
    return (int)entry->virtpages[pidindex].addr;
  }

  // Checking it here as when a process recalls shmgetat with the same key, any value of
  // num_pages is acceptable.
  if(num_pages < MIN_SHM_PAGES || num_pages > MAX_SHM_PAGES){
    release(&entry->lock);
    return -1;
  }

  proclock();

  newsz = curproc->sz + num_pages * PGSIZE;
  if(newsz >= KERNBASE){
    procrelease();
    release(&entry->lock);
    return -1;
  }

  if(entry->refs == 0){
    entry->num_pages = num_pages;

    // New shared memory - need to allocate and initialize the pages.
    for(i = 0; i < num_pages; i++){
      if((entry->phypages[i] = kalloc()) == 0){
        procrelease();

        for(j = 0; j < i; j++)
          kfree(entry->phypages[j]);

        release(&entry->lock);
        return -1;
      }
      memset(entry->phypages[i], 0, PGSIZE);
    }
  }

  entry->refs++;

  addr = PGROUNDUP(curproc->sz);

  // Mark the virtual address of this process for future use.
  entry->virtpages[pidindex].used = 1;
  entry->virtpages[pidindex].pgdir = curproc->pgdir;
  entry->virtpages[pidindex].addr = (char*)addr;

  
  // Map pages to the process.
  for(i = 0; i < num_pages; i++){
    if(mappages(curproc->pgdir, (void*)addr, PGSIZE, V2P(entry->phypages[i]), PTE_W|PTE_U|PTE_SHM) < 0){
      deallocuvm(curproc->pgdir, curproc->sz + i * PGSIZE, curproc->sz);
      procrelease();
      release(&entry->lock);
      return -1;
    }

    addr += PGSIZE;
  }

  // TOOD: do we need ptable lock for this?
  changesz(newsz);

  procrelease();
  release(&entry->lock);

  return (int)entry->virtpages[pidindex].addr;
}

int
shm_refcount(int key)
{
  struct shmentry *entry;
  int refs;

  if(key < MIN_SHM_KEY || key > MAX_SHM_KEY)
    return -1;

  entry = &shmtable[key - MIN_SHM_KEY];

  acquire(&entry->lock);
  refs = entry->refs;
  release(&entry->lock);

  return refs;
}
