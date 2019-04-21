#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <uapi/linux/sched/types.h>

#include "h1.h"

#define numCores 4
#define maxUtil 50

#define absus(x) x > 0 ? x : -1

typedef struct core {
  struct mutex lock;
  int num;
  int util;
  subtask_t *head;
  struct task_struct *ts;
  struct sched_param par;
} core_t;

struct mutex lock2;
struct mutex lock1;
static core_t cores[numCores];
static char *todo = "calibrate";
static char *arg = "nothing";

module_param(arg, charp, 0);

void removeNode(core_t *core, struct subtask *node) {
  if (core->head == NULL) {
    return;
  }
  if (node->prev == NULL && node->next == NULL) {
    core->head = NULL;
  } else if (node->prev == NULL && node->next != NULL) {
    core->head = node->next;
    core->head->prev = NULL;
  } else if (node->next == NULL && node->prev != NULL) {
    node->prev->next = NULL;
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;
  }
}

void addNode(core_t *core, struct subtask *newNode) {
  if (core->head == NULL) {
    core->head = newNode;
    newNode->prev = NULL;
    newNode->next = NULL;
  } else {
    newNode->next = core->head;
    newNode->prev = NULL;
    newNode->next->prev = newNode;
    core->head = newNode;
  }
}

void printLL(void) {
  int i;
  struct subtask *temp;
  for (i = 0; i < numCores; i++) {
    printk("core[%d]=\n", i);
    temp = cores[i].head;
    while (temp != NULL) {
      printk("%d,%d, me= %p, prev=%p, next=%p\n", temp->myindex, temp->util,
             (void *)temp, (void *)temp->prev, (void *)temp->next);
      temp = temp->next;
    }
  }
}

int subRunner(void *data) {
  subtask_t *subt = (subtask_t *)data;
  int i, iter;
  printk("running sub[%d]\n", subt->myindex);
  iter = subt->loop_iterations_count;

  for (i = 0; i < iter; i++) {
    ktime_get();
  }

  printk("finished running sub[%d]\n", subt->myindex);
  return 0;
}

void scheduleSub(core_t *core, subtask_t *subt) {
  printk("scheduling sub[%d] on core %d\n", subt->myindex, core->num);
  addNode(core, subt);
  subt->assigned = 1;
  subt->ts = kthread_create(subRunner, (void *)subt, "%d-subtask[%d]",
                            subt->pindex, subt->myindex);
  kthread_bind(subt->ts, core->num);
  subt->par.sched_priority = subt->priority;
  sched_setscheduler(subt->ts, SCHED_FIFO, &subt->par);
  wake_up_process(subt->ts);
}

int findCore(int tUtil, subtask_t subt) {
  int i;

  for (i = 0; i < numCores; i++) {
    mutex_lock(&cores[i].lock);
    if (cores[i].util + tUtil <= maxUtil) {
      scheduleSub(&cores[i], &subt);
      mutex_unlock(&cores[i].lock);
      return 1;
    }
    mutex_unlock(&cores[i].lock);
  }
  return 0;
}

int taskScheduler(void *data) {
  task_t *task = (task_t *)data;
  int i;
  int again = 1;
  while (again) {
    again = 0;
    for (i = maxSubTasks; i >= 0; i--) {
      if (!task->sub[i].assigned) {
        if (findCore(task->sub[i].util, task->sub[i])) {
        } else {
          again = 1;
        }
      }
    }
    usleep_range(0, 100000);
  }
  return 0;
}

int calibrate(void *data) {
  core_t *core = (core_t *)data;
  subtask_t *sub = core->head;
  unsigned long t1, t2;
  long t, imin, imax, tester;
  printk("core[%d] test\n", core->num);
  set_current_state(TASK_INTERRUPTIBLE);
  schedule();
  mutex_lock(&lock2);
  mutex_unlock(&lock1);

  printk("started\n");
  while (sub) {
    printk("outloop\n");
    imin = 0;
    imax = sub->loop_iterations_count;
    while (1) {
      printk("inner loop sub[%d] t=%lu\n", sub->myindex, sub->time);
      t1 = ktime_get();
      core->par.sched_priority = sub->priority;
      sched_setscheduler(core->ts, SCHED_FIFO, &core->par);
      subRunner((void *)sub);
      t2 = ktime_get();
      t = sub->time - ((t2 - t1) / 1000000);
      tester = t;
      if (t < 0) {
        tester = -t;
      }
      if (tester <= ((sub->time) / 20)) {
        printk("subtask[%d], time[%lu] vs %lu, loop[%lu]", sub->myindex,
               sub->time, ((t2 - t1) / 1000000), sub->loop_iterations_count);
        sub = sub->next;
        break;
      }
      if (t > 0) {
        imin = sub->loop_iterations_count + 1;
        if (imin > imax) imax = sub->loop_iterations_count * 2;
      } else {
        imax = sub->loop_iterations_count - 1;
      }
      sub->loop_iterations_count = (imin + imax) / 2;
    }
  }
  mutex_unlock(&lock2);
  printk("done time=%lu", (unsigned long)t2);
  printk("done time=%lu", (unsigned long)(t2 - t1));
  return 0;
}

subtask_t *sublookup(struct hrtimer *t) { return (subtask_t *)t; }

static enum hrtimer_restart restartSub(struct hrtimer *tmr) {
  subtask_t *subt;
  subt = sublookup(tmr);
  wake_up_process(subt->ts);
  return HRTIMER_RESTART;
}
int simple_init(void) {
  int i, j;
  struct tast_struct *ts;
  struct core_t *core;
  if (arg[0] == 'r' && arg[1] == 'u' && arg[2] == 'n') {
    todo = arg;
  }
  initHeader();
  mutex_init(&lock2);
  for (i = 0; i < numCores; i++) {
    mutex_init(&cores[i].lock);
    cores[i].num = i;
    cores[i].util = 0;
    cores[i].head = NULL;
  }
  for (i = 0; i < amt; i++) {
    for (j = 0; j < maxSubTasks; j++) {
      addNode(&cores[(i * (j + 1)) % numCores], &t[i].sub[j]);
    }
  }
  printk("arg=%s\n", arg);
  printk("todo=%s\n", todo);

  if (!strcmp(todo, "calibrate")) {
    for (i = 0; i < numCores; i++) {
      cores[i].ts = kthread_create(calibrate, (void *)(&cores[i]),
                                   "calibrate core %d", cores[i].num);
      kthread_bind(cores[i].ts, cores[i].num);
      wake_up_process(cores[i].ts);
    }
    usleep_range(0, 10000);
    mutex_lock(&lock1);
    for (i = 0; i < numCores; i++) {
      mutex_lock(&lock2);
      mutex_unlock(&lock2);
      wake_up_process(cores[i].ts);
      mutex_lock(&lock1);
    }

  }

  else if (!strcmp(todo, "run")) {
    printk("running!\n");
    for (i = 0; i < amt; i++) {
      for (j = 0; j < maxSubTasks; j++) {
        ts = kthread_create(run_thread, (void *)(&t[i].sub[j]), "%d-%d", i, j);
        core = &(cores[t[i].sub[j].core])kthread_bind(ts, t[i].sub[j].core);
        core->par.sched_priority = sub->priority;
        sched_setscheduler(ts, SCHED_FIFO, &cores[t[i].sub[j].core].par);
      }
    }
  }

  printk("done\n");

  return 0;
}

static void simple_exit(void) {
  printk("module removed!\n");
  return;
}

module_init(simple_init);
module_exit(simple_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LKD Chapter 17");
MODULE_DESCRIPTION("Simple CSE 422 Module Template");
MODULE_INFO(intree, "Y");