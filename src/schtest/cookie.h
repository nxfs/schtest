#ifndef __COOKIE_H__
#define __COOKIE_H__

#include <stdatomic.h>

struct task_shm {
	atomic_int cookie_ready_sem;
};

int shm_init(struct task_shm **task_shm);
int create_cookie(int cookie_count, int task_idx, int task_pid, struct task_shm *task_shm, unsigned long long *cookie);
int copy_cookie(int cookie_count, int task_idx, int donor_task_pid, struct task_shm *task_shm);

#endif
