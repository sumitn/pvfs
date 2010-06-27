#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "gen-locks.h"
#include "quicklist.h"

#define TH_RUNNING  1
#define TH_SLEEPING 2
#define TH_DEAD     3

#define TH_COMPLETE 0
#define TH_PENDING  1

typedef void * (*thread_fn)(void *ptr);

struct s_thread_work;

typedef struct s_thread
{
   pthread_t *thread;
   pthread_cond_t cond;
   pthread_mutex_t lock;
   int *status; /* TH_RUNNING, TH_SLEEPING, TH_DEAD */
   struct qlist_head link;
   thread_fn func;
   int volatile count;
   int volatile active;
} s_thread ;

typedef struct s_thread_work
{
   void *ptr;
   struct qlist_head link;
   struct s_thread *st;
   int volatile status;
} s_thread_work;

int s_thread_init(struct s_thread *st, int count, thread_fn func);
void * s_thread_work_append(struct s_thread *st, void *ptr);
int s_thread_terminate(struct s_thread *st);
int s_thread_pending(struct s_thread *st);
void * s_thread_retrieve(struct s_thread *st, void *ptr);
