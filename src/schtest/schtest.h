#ifndef __SCHTEST_H__
#define __SCHTEST_H__

#define CPU_SET_LENGTH 32

struct task_spec {
	char *cmd;
	int count;
	struct task_spec *next;
};


int run(struct task_spec *head, int duration, int cookie_count, bool fake_cookies, char *cgroup, char *cpu_set, char *results_dir);

#endif
