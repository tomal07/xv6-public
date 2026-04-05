#include "types.h"
#include "defs.h"

int
sys_shmgetat(void)
{
  int key, num_pages;

  if(argint(0, &key) < 0 ||
     argint(1, &num_pages) < 0)
    return -1;
  
  return shmgetat(key, num_pages);
}

int
sys_shm_refcount(void)
{
  int key;

  if(argint(0, &key) < 0)
    return -1;
  
  return shm_refcount(key);
}
