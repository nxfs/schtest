#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "schtest.h"
#include "smt.h"

#define MAX(a,b) (((a)>(b))?(a):(b))

static const char *OUT_FILE = "out.txt";

struct task_shm {
	atomic_int cookie_ready_sem;
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

static void get_task_out_filename(char *results_dir, char *out_filename, int task_idx) {
	sprintf(out_filename, "%s/fork_%03d.txt", results_dir, task_idx);
}

static void exec_task(struct task_spec *spec, int task_idx, char *results_dir) {
	// arg max refers to the total argument size, not argc, just over allocate here to keep it simple
	// we don't have to check for overflows because the input args are derived from this program's input args which is also subject to the same limit
	long arg_max = sysconf(_SC_ARG_MAX);
	char **newargv = (char**)malloc(arg_max * sizeof(char*));
	int newargc = 0;
	const char *s = " ";
	char *token;
	token = strtok(spec->cmd, s);
	newargv[newargc++] = token;
	if (newargv[0] == NULL) {
		fprintf(stderr, "Null executable for cmd '%s', aborting fork\n", spec->cmd);
		exit(1);
	}
	while ((token = strtok(NULL, s)) != NULL) {
		newargv[newargc++] = token;
	}
	newargv[newargc] = NULL;
	char *newenviron[] = { NULL };
	char out_filename[PATH_MAX];
	get_task_out_filename(results_dir, out_filename, task_idx);
	int fd = open(out_filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		fprintf(stderr, "Unable to redirect stdout/stderr of fork %d with cmd %s at file %s, errno = %d\n", task_idx, spec->cmd, out_filename, errno);
		exit(1);
	} else {
		int rc;
		rc = dup2(fd, STDOUT_FILENO);
		if (rc == -1) {
			fprintf(stderr, "Unable to redirect stdout of fork %d with cmd %s at file %s, errno = %d\n", task_idx, spec->cmd, out_filename, errno);
			exit(1);
		}
		rc = dup2(fd, STDERR_FILENO);
		if (rc == -1) {
			fprintf(stderr, "Unable to redirect stderr of fork %d with cmd %s at file %s, errno = %d\n", task_idx, spec->cmd, out_filename, errno);
			exit(1);
		}
		close(fd);
	}

	execve(newargv[0], newargv, newenviron); // doesn't return if successful
	fprintf(stderr, "execve of failed for %s with errno %d\n", spec->cmd, errno);
	exit(1);
}

static int setup_cgroup_cpu_set(char *cgroup, char *cpu_set) {
	char cgroup_cpu_set[PATH_MAX];
	sprintf(cgroup_cpu_set, "%s/cpuset.cpus", cgroup);

	if (access(cgroup_cpu_set, F_OK) == 0) {
		printf("Setting up cgroup %s cpu set <%s> ...\n", cgroup, cpu_set[0] == '\0' ? "empty" : cpu_set);

		FILE *f = fopen(cgroup_cpu_set, "a");
		if (!f) {
			fprintf(stderr, "Could not open cpuset.cpus at %s, errno = %d\n", cgroup_cpu_set, errno);
			return errno;
		}
		if (fprintf(f, "%s", cpu_set) < 0) {
			fprintf(stderr, "Could not write cpu sets <%s> to cpuset.cpus at %s, errno = %d\n", cpu_set, cgroup_cpu_set, errno);
			return errno;
		}
		if (fclose(f)) {
			fprintf(stderr, "Could not close cpuset.cpus file at %s after writing cpu sets <%s>, errno = %d\n", cgroup_cpu_set, cpu_set, errno);
			return errno;
		}
	} else if (*cpu_set != '\0') {
		fprintf(stderr, "Cannot access cpu set file at %s\n", cgroup_cpu_set);
		return -EACCES;
	}

	return 0;
}

static int get_cookie(int task_idx, int pid, unsigned long long *cookie) {
	int rc = prctl(PR_SCHED_CORE, PR_SCHED_CORE_GET, pid, PR_SCHED_CORE_SCOPE_THREAD, (unsigned long)cookie);
	if (rc)
		fprintf(stderr, "Could not get cookie for task %d with pid %d, rc = %d, errno = %d\n", task_idx, pid, rc, errno);
	return rc;
}

static int create_cookie(int cookie_count, int task_idx, int task_pid, struct task_shm *task_shm, unsigned long long *cookie) {
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

static int copy_cookie(int cookie_count, int task_idx, int donor_task_pid, struct task_shm *task_shm) {
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

static int move_tasks_to_cgroup(char* cgroup, struct task_info* task_info, int task_count) {
	printf("Moving tasks to cgroup %s ...\n", cgroup);
	char cgroup_procs[PATH_MAX];
	sprintf(cgroup_procs, "%s/cgroup.procs", cgroup);
	for (int i = 0; i < task_count; i++) {
		FILE *f = fopen(cgroup_procs, "a");
		if (!f) {
			fprintf(stderr, "Could not open cgroup.procs at %s, errno = %d\n", cgroup_procs, errno);
			return errno;
		}
		if (fprintf(f, "%d", task_info[i].pid) < 0) {
			fprintf(stderr, "Could not write pid %d of task %d to cgroup.procs at %s, errno = %d\n", task_info[i].pid, i, cgroup_procs, errno);
			return errno;
		}
		if (fclose(f)) {
			fprintf(stderr, "Could not close cgroup.procs file at %s, errno = %d\n", cgroup_procs, errno);
			return errno;
		}
	}
	return 0;
}

static int shm_init(struct task_shm **task_shm) {
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

static uint64_t clock_get_time_nsecs() {
	struct timespec ts;

	errno = 0;
	// must align with the clock used by perf, i.e `perf -k raw`
	int rc = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

	if (rc) {
		fprintf(stderr, "FATAL: clock_gettime returned %d, errno %d\n\n", rc, errno);
		exit(1);
	}

	return ((uint64_t)ts.tv_sec)*1000000000 + (uint64_t)ts.tv_nsec;
}

static void sleep_nsecs(uint64_t nsecs) {
       struct timespec t, trem;

	t.tv_sec = nsecs / 1000000000;
	t.tv_nsec = nsecs % 1000000000;

	errno = 0;
	int rc = nanosleep(&t, &trem) < 0;
	if (rc) {
		if (errno == EINTR) {
			fprintf(stderr, "nanosleep was interrupted\n");
		} else {
			fprintf(stderr, "nanosleep returned %d, errno %d\n\n", rc, errno);
			exit(1);
		}
	}
}

static int open_out_file(char *results_dir, FILE **f) {
	char path[PATH_MAX];
	sprintf(path, "%s/%s", results_dir, OUT_FILE);
	*f = fopen(path, "w");
	if (*f == NULL) {
		fprintf(stderr, "Could not open out timestamp file '%s', errno = %d\n", path, errno);
		return errno;
	}
	return 0;
}

static struct proc_pid_schedstat fetch_proc_pid_schedstat(int pid) {
	struct proc_pid_schedstat schedstat = {0};
	char path[PATH_MAX];
	sprintf(path, "/proc/%d/schedstat", pid);
	FILE *f = fopen(path, "r");
	if (f != NULL) {
		fscanf(f, "%lu %lu %lu", &schedstat.cpu_time, &schedstat.runq_wait_time, &schedstat.timeslices);
		fclose(f);
	} else {
		fprintf(stderr, "Failed to read schedstat at %s\n", path);
	}
	return schedstat;
}

static void wait_for_tasks(int task_count, uint32_t duration, struct task_info *task_info, FILE *out_f) {
	int rc;
	int task_idx;

	printf("Waiting for tasks completion ...\n");
	sigset_t mask, orig_mask;
	sigemptyset (&mask);
	sigaddset (&mask, SIGCHLD);
	rc = sigprocmask(SIG_BLOCK, &mask, &orig_mask);
	if (rc) {
		fprintf(stderr, "Failed to add SIGCHILD to sigprocmask, rc = %d\n", rc);
		exit(1);
	}
	int waiting_count = task_count;
	time_t timeout_epoch = time(NULL) + duration + 1;
	bool killed = false;
	while (waiting_count) {
		siginfo_t info;
		struct timespec timeout;
		timeout.tv_sec = MAX(timeout_epoch - time(NULL), 1);
		rc = sigtimedwait(&mask, &info, duration > 0 || killed ? &timeout : NULL);
		if (rc > 0) {
			if (info.si_signo == SIGCHLD) {
				int status;
				pid_t terminated_pid = waitpid(info.si_pid, &status, 0);
				if (terminated_pid != -1) {
					for (task_idx = 0; task_idx < task_count; task_idx++)
						if (task_info[task_idx].pid == terminated_pid)
							break;
					if (task_idx == task_count) {
						fprintf(stderr, "Unknown child process with pid %d terminated\n", terminated_pid);
						exit(1);
					}
					task_info[task_idx].running = false;
					printf("Task %d completed, status=%d, cpu_time_s=%.3f, runq_wait_time_s=%.3f\n",
							terminated_pid,
							status,
							task_info[task_idx].schedstat.cpu_time / 1000000000.0,
							task_info[task_idx].schedstat.runq_wait_time / 1000000000.0);
					if (fprintf(out_f, "%d %d %llu %lu %d %lu %lu\n",
								task_idx,
								task_info[task_idx].pid,
								task_info[task_idx].cookie,
								clock_get_time_nsecs(),
								status,
								task_info[task_idx].schedstat.cpu_time,
								task_info[task_idx].schedstat.runq_wait_time) < 0) {
						fprintf(stderr, "Could not write task info for task with pid %d, errno = %d\n", task_info[task_idx].pid, errno);
						exit(1);
					}
					waiting_count--;
				} else {
					fprintf(stderr, "Waitpid error for %d, errno = %d\n", task_info[task_idx].pid, errno);
					exit(1);
				}

			} else {
				fprintf(stderr, "Received unexpected signal %d\n", info.si_signo);
				exit(1);
			}
		} else if (errno == EINTR) {
			fprintf(stderr, "Interrupted while waiting for children\n");
			exit(1);
		} else if (errno == EAGAIN) {
			if (killed) {
				fprintf(stderr, "Timed out while waiting for children\n");
				exit(1);
			} else {
				fprintf(stdout, "Test duration elapsed, sending SIGINT to remaining children...\n");
				for (task_idx = 0; task_idx < task_count; task_idx++) {
					if (task_info[task_idx].running) {
						int pid = task_info[task_idx].pid;
						task_info[task_idx].schedstat = fetch_proc_pid_schedstat(pid);
						kill(pid, SIGINT);
					}
				}
				killed = true;
			}
		} else {
			fprintf(stderr, "Error %d while waiting for children\n", errno);
			exit(1);
		}
	}
}

int run(struct task_spec *head, int duration, int cookie_count, bool fake_cookies, char *cgroup, char *cpu_set, char *results_dir) {
	printf("Tasks\n");
	int task_count = 0;
	for (struct task_spec *curr = head; curr != NULL; curr = curr->next) {
		char out_filename[PATH_MAX];
		for (int i = 0; i < curr->count; i++) {
			get_task_out_filename(results_dir, out_filename, task_count);
			printf("\t%3d: %s > %s 2>&1\n", task_count++, curr->cmd, out_filename);
		}
	}
	printf("\n");

	if (cookie_count > task_count) {
		fprintf(stderr, "Cannot use more cookies (%d) than tasks (%d)\n", cookie_count, task_count);
		return -EINVAL;
	}

	printf("Fetching cpu topography ...\n");
	struct cpu_group *siblings = NULL;
	if (fetch_cpu_topography(cpu_set, &siblings)) {
		fprintf(stderr, "Failed to fetch cpu topography of cpu set <%s>\n", cpu_set);
		exit(1);
	}

	struct task_shm *task_shm = NULL;
	if (cookie_count > 0) {
		printf("Creating shared memory ...\n");
		if (shm_init(&task_shm)) {
			fprintf(stderr, "Shared memory creation failure\n");
			exit(1);
		}
		task_shm->cookie_ready_sem = task_count;
	}

	printf("Forking tasks ...\n");
	struct task_info *task_info = (struct task_info*)malloc(task_count * sizeof(*task_info));
	int task_idx = 0;
	for (struct task_spec *curr = head; curr != NULL; curr = curr->next) {
		for (int i = 0; i < curr->count; i++) {
			pid_t c_pid = fork();
			if (c_pid == -1) {
				fprintf(stderr, "Fork failure for task %d\n", task_idx);
				while (task_idx > 0) {
					killpg(--task_idx, SIGKILL);
				}
				exit(1);
			} else if (c_pid == 0) {
				if (fake_cookies && cookie_count > 0) {
					task_shm->cookie_ready_sem--;
				}
				else if (copy_cookie(cookie_count, task_idx, cookie_count ? task_info[task_idx % cookie_count].pid : -1, task_shm)) {
					fprintf(stderr, "Failed to copy cookies for task %d with pid %d\n", task_idx, c_pid);
					exit(1);
				}
				exec_task(curr, task_idx, results_dir); // doesn't return
			} else {
				task_info[task_idx].pid = c_pid;
				task_info[task_idx].running = true;
				if (cookie_count) {
					if (fake_cookies) {
						task_info[task_idx].cookie = (task_idx % cookie_count) + 1;
					} else {
						if (task_idx < cookie_count) {
							if (create_cookie(cookie_count, task_idx, c_pid, task_shm, &task_info[task_idx].cookie)) {
								fprintf(stderr, "Failed to create cookie for task %d with pid %d\n", task_idx, c_pid);
								exit(1);
							}
						} else {
							task_info[task_idx].cookie = task_info[task_idx % cookie_count].cookie;
						}
					}
				}
				task_idx++;
			}
		}
	}

	if (*cgroup != '\0') {
		if (setup_cgroup_cpu_set(cgroup, cpu_set)) {
			fprintf(stderr, "Failed to setup cgroup cpu set\n");
			exit(1);
		}
		if (move_tasks_to_cgroup(cgroup, task_info, task_count)) {
			fprintf(stderr, "Failed to move tasks to cgroup\n");
			exit(1);
		}
	}

	uint64_t ready_timestamp = clock_get_time_nsecs();
	if (cookie_count > 0) {
		printf("Waiting for cookies ...\n");
		uint64_t timeout = ready_timestamp + 1000000000;
		while (task_shm->cookie_ready_sem > 0) {
			if (clock_get_time_nsecs() > timeout) {
				fprintf(stderr, "Timeout waiting for cookie setup, missing %d/%d tasks\n", task_shm->cookie_ready_sem, task_count);
				exit(1);
			}
			sleep_nsecs(1000000);
		}
		ready_timestamp = clock_get_time_nsecs();
		printf("Cookies setup completed at %lu\n", ready_timestamp);
	}


	FILE * out_f;
	if (open_out_file(results_dir, &out_f)) {
		fprintf(stderr, "Could not open output file\n");
		exit(1);
	}
	fprintf(out_f, "%s\n", *cpu_set == '\0' ? "empty" : cpu_set);
	print_cpu_topography(out_f, siblings);
	fprintf(out_f, "%d\n", task_count);

	wait_for_tasks(task_count, duration, task_info, out_f);

	if (fprintf(out_f, "%lu %lu\n", ready_timestamp, clock_get_time_nsecs()) < 0) {
		fprintf(stderr, "Could not write execution timestamp %lu, errno = %d\n", ready_timestamp, errno);
		exit(1);
	}

	fclose(out_f);
	printf("Done\n");

	return 0;
}
