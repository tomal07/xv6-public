#include "types.h"
#include "defs.h"

int
sys_counting_sem_init(void)
{
  int counter;

  if(argint(0, &counter) < 0)
    return -1;
  return counting_sem_init(counter);
}

int
sys_counting_sem_wait(void)
{
  int sid;

  if(argint(0, &sid) < 0)
    return -1;
  return counting_sem_wait(sid);
}

int
sys_counting_sem_post(void)
{
  int sid;

  if(argint(0, &sid) < 0)
    return -1;
  return counting_sem_post(sid);
}

int
sys_counting_sem_destroy(void)
{
  int sid;

  if(argint(0, &sid) < 0)
    return -1;
  return counting_sem_destroy(sid);
}

int
sys_binary_sem_init(void)
{
  int counter;

  if(argint(0, &counter) < 0)
    return -1;
  return binary_sem_init(counter);
}

int
sys_binary_sem_wait(void)
{
  int sid;

  if(argint(0, &sid) < 0)
    return -1;
  return binary_sem_wait(sid);
}

int
sys_binary_sem_post(void)
{
  int sid;

  if(argint(0, &sid) < 0)
    return -1;
  return binary_sem_post(sid);
}

int
sys_binary_sem_destroy(void)
{
  int sid;

  if(argint(0, &sid) < 0)
    return -1;
  return binary_sem_destroy(sid);
}
