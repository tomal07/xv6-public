#include "types.h"
#include "user.h"
#include "boundedbuffer.h"

int
bounded_buffer_init(sem_bounded_buffer_t *sem_out, uint size)
{
  int sem_queueing_positions = -1;
  int sem_empty_positions = -1;
  int sem_buffer_manipulation = -1;
  int *buffer = 0;

  sem_out->buffer = 0;
  
  if(size == 0)
    goto error;

  if((sem_queueing_positions = counting_sem_init(0)) == -1)
    goto error;

  if((sem_empty_positions = counting_sem_init(size)) == -1)
    goto error;

  if((sem_buffer_manipulation = binary_sem_init(1)) == -1)
    goto error;

  if((buffer = malloc(size)) == 0)
    goto error;

  sem_out->sem_queueing_positions = sem_queueing_positions;
  sem_out->sem_empty_positions = sem_empty_positions;
  sem_out->sem_buffer_manipulation = sem_buffer_manipulation;
  sem_out->buffer = buffer;
  sem_out->size = size;
  sem_out->next_index = 0;

  return 0;

error:
  if(sem_queueing_positions != -1)
    counting_sem_destroy(sem_queueing_positions);

  if(sem_empty_positions != -1)
    counting_sem_destroy(sem_empty_positions);

  if(sem_buffer_manipulation != -1)
    binary_sem_destroy(sem_buffer_manipulation);

  if(buffer != 0)
    free(buffer);

  return -1;
}

int
bounded_buffer_push(sem_bounded_buffer_t *sem, int value)
{
  if(counting_sem_wait(sem->sem_empty_positions) != 0 ||
      binary_sem_wait(sem->sem_buffer_manipulation) != 0)
    return -1;

  if(sem->next_index == sem->size){
    // Buffer is full
    counting_sem_post(sem->sem_empty_positions);
    binary_sem_post(sem->sem_buffer_manipulation);
    return -1;
  }

  sem->buffer[sem->next_index++] = value;

  if(binary_sem_post(sem->sem_buffer_manipulation) != 0 || 
      counting_sem_post(sem->sem_queueing_positions) != 0)
    return -1;

  return 0;
}

int
bounded_buffer_pop(sem_bounded_buffer_t *sem, int *value)
{
  if(counting_sem_wait(sem->sem_queueing_positions) != 0 ||
      binary_sem_wait(sem->sem_buffer_manipulation) != 0)
    return -1;

  if(sem->next_index == 0){
    // Buffer is empty
    counting_sem_post(sem->sem_queueing_positions);
    binary_sem_post(sem->sem_buffer_manipulation);
    return -1;
  }

  *value = sem->buffer[--sem->next_index];

  if(binary_sem_post(sem->sem_buffer_manipulation) != 0 ||
      counting_sem_post(sem->sem_empty_positions) != 0)
    return -1;

  return 0;
}


int
bounded_buffer_destroy(sem_bounded_buffer_t *sem)
{
  int ret = 0;

  if(counting_sem_destroy(sem->sem_queueing_positions) != 0 ||
      counting_sem_destroy(sem->sem_empty_positions) != 0 ||
      binary_sem_destroy(sem->sem_buffer_manipulation) != 0)
    ret = -1;

  free(sem->buffer);
  sem->buffer = 0;
  sem->size = 0;
  sem->next_index = 0;

  return ret;
}
