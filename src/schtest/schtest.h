#ifndef __SCHTEST_H__
#define __SCHTEST_H__

#include <stdbool.h>

#define CPU_SET_LENGTH 32

struct task_spec {
	char *cmd;
	int count;
	struct task_spec *next;
};

struct proc_pid_schedstat {
	unsigned long int cpu_time;
	unsigned long int runq_wait_time;
	unsigned long int timeslices;
};

struct task_info {
	int pid;
	unsigned long long cookie;
	bool running;
	struct proc_pid_schedstat schedstat;
};

int run(struct task_spec *head, int duration, int cookie_count, bool fake_cookies, char *cgroup, char *cpu_set, char *results_dir);

#endif
