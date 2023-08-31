#include <errno.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <unistd.h>

#include "cookie.h"

int shm_init(struct task_shm **task_shm) {
	int shm_id = shmget(IPC_PRIVATE, sizeof(struct task_shm), IPC_CREAT | IPC_EXCL | 0666);
	if (shm_id == -1) {
		fprintf(stderr, "Could not create shared memory: errno = %d\n", errno);
		return errno;
	}
	void *p = shmat(shm_id, NULL, 0);
	if (p == (void *)-1) {
		fprintf(stderr, "Could not attach shared memory: errno = %d\n", errno);
		return errno;
	}
	*task_shm = p;
	return 0;
}


static int get_cookie(int task_idx, int pid, unsigned long long *cookie) {
	int rc = prctl(PR_SCHED_CORE, PR_SCHED_CORE_GET, pid, PR_SCHED_CORE_SCOPE_THREAD, (unsigned long)cookie);
	if (rc)
		fprintf(stderr, "Could not get cookie for task %d with pid %d, rc = %d, errno = %d\n", task_idx, pid, rc, errno);
	return rc;
}

int create_cookie(int cookie_count, int task_idx, int task_pid, struct task_shm *task_shm, unsigned long long *cookie) {
	if (!cookie_count || task_idx >= cookie_count) {
		return 0;
	}

	int rc = prctl(PR_SCHED_CORE, PR_SCHED_CORE_CREATE, task_pid, PR_SCHED_CORE_SCOPE_THREAD, 0);
	if (rc) {
		fprintf(stderr, "Could not create cookie for task %d with pid %d, rc = %d, errno = %d\n", task_idx, task_pid, rc, errno);
	} else {
		rc = get_cookie(task_idx, task_pid, cookie);
		if (!rc) {
			fprintf(stdout, "Cookie for task %d with pid %d is %llx\n", task_idx, task_pid, *cookie);
			task_shm->cookie_ready_sem--;
		}
	}

	return rc;
}

int copy_cookie(int cookie_count, int task_idx, int donor_task_pid, struct task_shm *task_shm) {
	if (!cookie_count || task_idx < cookie_count)
		return 0;

	int rc = prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_FROM, donor_task_pid, PR_SCHED_CORE_SCOPE_THREAD, 0);
	if (rc) {
		fprintf(stderr, "Task %d could not share cookie from task with pid %d, rc = %d, errno = %d\n", getpid(), donor_task_pid, rc, errno);
	} else {
		unsigned long long cookie;
		int pid = getpid();
		rc = get_cookie(task_idx, pid, &cookie);
		if (!rc) {
			fprintf(stdout, "Cookie for task %d with pid %d is %llx\n", task_idx, pid, cookie);
			task_shm->cookie_ready_sem--;
		}
	}

	return rc;
}
