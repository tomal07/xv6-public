#include "types.h"

typedef struct {
    int sem_queueing_positions;
    int sem_empty_positions;
    int sem_buffer_manipulation;
    int *buffer;
    uint size;
    uint next_index;
} sem_bounded_buffer_t;

/**
 * Providing a sem_bounded_buffer_t modified by things which are not the
 * following functions would result in returning -1 at best, and possible
 * undesired side effects at worst
 */

/**
 * Initilize the bounded buffer.
 * returns 0 on success, -1 on failure.
 */
int bounded_buffer_init(sem_bounded_buffer_t*, uint);

/**
 * Push to the bounded buffer.
 * returns 0 on success, -1 on failure.
 */
int bounded_buffer_push(sem_bounded_buffer_t*, int);

/**
 * Pop from the bounded buffer.
 * returns 0 on success, -1 on failure.
 */
int bounded_buffer_pop(sem_bounded_buffer_t*, int*);

/**
 * Destroy the bounded buffer.
 * returns 0 on success, -1 on failure.
 */
int bounded_buffer_destroy(sem_bounded_buffer_t*);
