#include <dbc.h>
#include <string.h>
#include "pfifo.h"

#include "thread.h"


void init_pfifo(PriorityFIFO* pfifo)
{
   require (pfifo != NULL, "NULL pointer to FIFO");   // a false value indicates a program error

   memset(pfifo->array, 0, sizeof(pfifo->array));
   pfifo->inp = pfifo->out = pfifo->cnt = 0;
   pfifo->mut = PTHREAD_MUTEX_INITIALIZER;
   pfifo->isNotEmpty = PTHREAD_COND_INITIALIZER;
   pfifo->isNotFull = PTHREAD_COND_INITIALIZER;
}

/* --------------------------------------- */

int empty_pfifo(PriorityFIFO* pfifo)
{
   require (pfifo != NULL, "NULL pointer to FIFO");   // a false value indicates a program error

   return pfifo->cnt == 0;
}

/* --------------------------------------- */

int full_pfifo(PriorityFIFO* pfifo)
{
   require (pfifo != NULL, "NULL pointer to FIFO");   // a false value indicates a program error

   return pfifo->cnt == FIFO_MAXSIZE;
}

/* --------------------------------------- */

void insert_pfifo(PriorityFIFO* pfifo, uint32_t id, uint32_t priority)
{
   mutex_lock(&pfifo->mut);

   require (pfifo != NULL, "NULL pointer to FIFO");   // a false value indicates a program error
   require (id <= MAX_ID, "invalid id");              // a false value indicates a program error
   require (priority > 0 && priority <= MAX_PRIORITY, "invalid priority value");  // a false value indicates a program error
   
   while(full_pfifo(pfifo))
   {
   		cond_wait(&pfifo->isNotFull, &pfifo->mut);
   }

   uint32_t idx = pfifo->inp;
   uint32_t prev = (idx + FIFO_MAXSIZE - 1) % FIFO_MAXSIZE;
   
   while((idx != pfifo->out) && (pfifo->array[prev].priority > priority))
   {
      //printf("[insert_pfifo] idx=%d, prev=%d\n", idx, prev);
      pfifo->array[idx] = pfifo->array[prev];
      idx = prev;
      prev = (idx + FIFO_MAXSIZE - 1) % FIFO_MAXSIZE;
   }

   pfifo->array[idx].id = id;
   pfifo->array[idx].priority = priority;
   pfifo->inp = (pfifo->inp + 1) % FIFO_MAXSIZE;
   pfifo->cnt++;

   cond_broadcast(&pfifo->isNotEmpty);

   mutex_unlock(&pfifo->mut);
}

/* --------------------------------------- */

uint32_t retrieve_pfifo(PriorityFIFO* pfifo)
{
   mutex_lock(&pfifo->mut);
   
   require (pfifo != NULL, "NULL pointer to FIFO");   // a false value indicates a program error

   while(empty_pfifo(pfifo))
   {
   		cond_wait(&pfifo->isNotEmpty, &pfifo->mut);
   }

   check_valid_id(pfifo->array[pfifo->out].id);
   check_valid_priority(pfifo->array[pfifo->out].priority);

   uint32_t result = pfifo->array[pfifo->out].id;
   pfifo->array[pfifo->out].id = INVALID_ID;
   pfifo->array[pfifo->out].priority = INVALID_PRIORITY;
   pfifo->out = (pfifo->out + 1) % FIFO_MAXSIZE;
   pfifo->cnt--;

   // update priority of all remaing elements (increase priority by one)
   uint32_t idx = pfifo->out;
   for(uint32_t i = 1; i <= pfifo->cnt; i++)
   {
      if (pfifo->array[idx].priority > 1 && pfifo->array[idx].priority != INVALID_PRIORITY)
         pfifo->array[idx].priority--;
      idx = (idx + 1) % FIFO_MAXSIZE;
   }

   cond_broadcast(&pfifo->isNotFull);

   mutex_unlock(&pfifo->mut);

   return result;
}

/* --------------------------------------- */

void print_pfifo(PriorityFIFO* pfifo)
{
   require (pfifo != NULL, "NULL pointer to FIFO");   // a false value indicates a program error

   mutex_lock(&pfifo->mut);

   uint32_t idx = pfifo->out;
   for(uint32_t i = 1; i <= pfifo->cnt; i++)
   {
      check_valid_id(pfifo->array[pfifo->out].id);
      check_valid_priority(pfifo->array[pfifo->out].priority);
      printf("[%02d] value = %d, priority = %d\n", i, pfifo->array[idx].id, pfifo->array[idx].priority);
      idx = (idx + 1) % FIFO_MAXSIZE;
   }

   mutex_unlock(&pfifo->mut);
}

