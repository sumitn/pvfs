#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "pvfs2-debug.h"
#include "gossip.h"
#include "dbpf-checksum-threads.h"

/* Thread function. Keeps a watch on the queue. If no requests remaining,
 * all threads go to sleep. They will be woken by broadcast when any 
 * new request is submitted.
 */
static void * s_thread_function(void *ptr)
{
   int ret = 0;
   struct s_thread_work *stw = NULL;
   struct qlist_head *next = NULL;
   struct s_thread *st = NULL;
   thread_fn func;

   stw = (struct s_thread_work *)ptr;
   st = stw->st;
   st->active++;

   if(stw->ptr == NULL)
   {
      free(stw);
   }

   while(1)
   {
      if(next)
      {      
         stw = qlist_entry(next, struct s_thread_work, link);
         func = stw->st->func;
         func(stw->ptr);
         stw->status = TH_COMPLETE;
         stw->st->count--;
         if(stw->st->count == 0 && stw->st->active == -1)
            break;
         
         if(next->next != next)
         {
            pthread_mutex_lock(&st->lock);
            next = qlist_pop(&st->link);
            pthread_mutex_unlock(&st->lock);
         }
         else
            next = NULL;
      }
      else
      {
         if(st->active == -1)
         {
            st->count--;
            break;
         }
         pthread_mutex_lock(&st->lock);
         st->active--;

	 gossip_debug(GOSSIP_CKSUM_DEBUG, "[s_thread_function]: Going to "
		      "sleep.\n");
         ret = pthread_cond_wait(&st->cond, &st->lock);
	 gossip_debug(GOSSIP_CKSUM_DEBUG, "[s_thread_function]: Thread woken "
		      "up by broadcast.\n");
         if(ret != 0)
            gossip_lerr("[s_thread_function]: Error with cond_wait %d\n", ret);

         next = qlist_pop(&st->link);
         st->active++;
         pthread_mutex_unlock(&st->lock);
      }
   }

   if(stw)
      free(stw);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[s_thread_function]: Exit.\n");
   return NULL;
}

/* Initialize all the threads */
int s_thread_init(struct s_thread *st, int count, thread_fn func)
{
   int i;

   if(st)
   {
      st->status = malloc(sizeof(int)*count);
      st->thread = malloc(sizeof(pthread_t)*count);
      st->func = func;
      st->active = 0;
      st->count = count;
      pthread_cond_init(&st->cond, NULL);
      pthread_mutex_init(&st->lock, NULL);
      st->link.next = &(st->link);
      st->link.prev = &(st->link);

      /* Start the threads */
      for(i=0; i<count; i++)
      {
         struct s_thread_work *stw;
         stw = malloc(sizeof(struct s_thread_work));
         stw->ptr = NULL;
         stw->st = st;
         pthread_create(&st->thread[i], NULL, s_thread_function, stw);
         st->status[i] = TH_SLEEPING;
      }
   }
   else 
      return -1;

   return 0;
}

/* Function to append the submitted request to the queue. Wake the
 * threads if necessary.
 */
void * s_thread_work_append(struct s_thread *st, void *ptr)
{
   int ret = 0;
   struct s_thread_work *stw = malloc(sizeof(struct s_thread_work));

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[s_thread_work_append]: Adding new "
		"work.\n");
   stw->ptr = ptr;
   stw->st = st;
   stw->status = TH_PENDING;
   pthread_mutex_lock(&stw->st->lock);
   qlist_add_tail(&stw->link, &st->link);
   st->count++;
   pthread_mutex_unlock(&stw->st->lock);

   if(st->active == 0)
      ret = pthread_cond_broadcast(&st->cond);

   ret = pthread_cond_signal(&st->cond);
   if(ret != 0)
      gossip_lerr("error %d, \n", ret);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[s_thread_work_append]: Work "
		"submitted.\n");
   return (void *)stw;
}

int s_thread_terminate(struct s_thread *st)
{
   st->active = -1;
   while(st->count != 0)
      pthread_cond_broadcast(&st->cond);

   free(st->thread);
   free(st->status);
   pthread_cond_destroy(&st->cond);
   pthread_mutex_destroy(&st->lock);

   return 0;
}

/* Returns the number of active threads */
int s_thread_pending(struct s_thread *st)
{
   return st->count - st->active;
}

/* Retrieve the job that was submitted. */
void * s_thread_retrieve(struct s_thread *st, void *ptr)
{
   struct s_thread_work *stw = (struct s_thread_work *)ptr;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[s_thread_retrieve]: Enter.\n");

  check:
   if(stw->status == TH_COMPLETE)
      return NULL;

   goto check;

   while(stw->status != TH_COMPLETE);

   qlist_del(&stw->link);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[s_thread_retrieve]: Exit.\n");

   return NULL;
}
