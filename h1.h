#define amt 16
#define maxSubTasks 25

typedef struct subtask {
  struct hrtimer timer;
  int myindex;
  int priority;
  int pindex;
  int first;
  int last;

  ktime_t last_release_time;
  unsigned long loop_iterations_count;
  int cumulative_execution_time;
  unsigned long time;
  int util;
  int assigned;
  int core;
  struct task_struct *ts;
  struct sched_param par;
  struct subtask *next;
  struct subtask *prev;

} subtask_t;
typedef struct task {
  unsigned long period;
  uint execution_time;
  subtask_t sub[maxSubTasks];
} task_t;

task_t t[amt];

void combine(subtask_t *arr, int start, int finish) {

  subtask_t *temp = (subtask_t *)kmalloc(
      (finish - start) * sizeof(struct subtask), GFP_KERNEL);

  int l = start;
  int r = (finish + start) / 2 + 1;
  int i;
  for (i = 0; i < finish - start; i++) {
    temp[i].util = 0;
  }

  for (i = 0; i <= finish - start; i++) {
    if (l > (finish + start) / 2) {
      temp[i].util = arr[r].util;
      r++;
    } else if (r > (finish)) {
      temp[i].util = arr[l].util;
      l++;
    } else if (arr[l].util < arr[r].util) {
      temp[i].util = arr[l].util;
      l++;
    } else {
      temp[i].util = arr[r].util;
      r++;
    }
  }
  for (i = 0; i <= finish - start; i++) {
    arr[i + start].util = temp[i].util;
  }
  kfree(temp);
}

void breaker(subtask_t *arr, int start, int finish) {
  int mid;
  if (start > finish) {
    return;
  }
  mid = (finish + start) / 2;
  if (finish - start >= 1) {

    breaker(arr, start, mid);
    breaker(arr, mid + 1, finish);

    combine(arr, start, finish);
  }
}

void doMS(subtask_t *arr, int size) {
  int i, next2 = 1;
  subtask_t *arr2;
  while (size > next2) {
    next2 = next2 << 1;
  }
  arr2 = (subtask_t *)kmalloc(next2 * sizeof(struct subtask), GFP_KERNEL);
  for (i = 0; i < size; i++) {
    arr2[i].util = arr[i].util;
  }
  for (i = size; i < next2; i++) {
    arr2[i].util = ~(1 << 31);
  }
  breaker(arr2, 0, next2 - 1);
  for (i = 0; i < size; i++) {
    arr[i].util = arr2[i].util;
  }

  kfree(arr2);
}

void initHeader(void) {
  int i;
  int j;
  int sum = 0;

  int period[amt] = {0};
  for (i = 0; i < amt; i++) {
    period[i] = 1000;
    t[i].period = period[i];

    for (j = 0; j < maxSubTasks; j++) {

      t[i].sub[j].pindex = i;
      t[i].sub[j].myindex = j;
      t[i].sub[j].first = !j;
      t[i].sub[j].last = (j == (maxSubTasks - 1));
      ;
      t[i].sub[j].priority = ((i + 13) * (j + 17)) % 75;
      t[i].sub[j].myindex = j;
      t[i].sub[j].core = 0;
      t[i].sub[j].last_release_time = 0;
      t[i].sub[j].time = 25;
      sum += t[i].sub[j].time;
      t[i].sub[j].cumulative_execution_time = sum;

      t[i].sub[j].loop_iterations_count = 9618 * t[i].sub[j].time;
      t[i].sub[j].assigned = 0;
      t[i].sub[j].util = ((100 * t[i].sub[j].time) / (t[i].period));
    }

    t[i].execution_time = sum;
  }
}
void printHeader(void) {
  int i, j;
  for (i = 0; i < amt; i++) {
    printk("period[%d]=%lu\n", i, t[i].period);
    for (j = 0; j < maxSubTasks; j++) {
      printk("       time[%d]=%lu\n", j, t[i].sub[j].time);
    }
    printk("       sorted order\n");
    printk("       ");
    for (j = 0; j < maxSubTasks; j++) {
      printk("[%d]=%d\n", i, t[i].sub[j].util);
    }
  }
}