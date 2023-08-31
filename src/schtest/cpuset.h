#ifndef __CPUSET_H_
#define __CPUSET_H__

#include "schtest.h"

int setup_cgroup_cpu_set(char *cgroup, char *cpu_set);
int move_tasks_to_cgroup(char* cgroup, struct task_info* task_info, int task_count);

#endif
