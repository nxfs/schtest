#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cookie.h"
#include "cpuset.h"
#include "schtest.h"
#include "smt.h"
#include "time.h"

#define MAX(a,b) (((a)>(b))?(a):(b))

static const char *OUT_FILE = "out.txt";

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
		fscanf(f, "%llu %llu %llu", &schedstat.cpu_time, &schedstat.runq_wait_time, &schedstat.timeslices);
		fclose(f);
	} else {
		fprintf(stderr, "Failed to read schedstat at %s\n", path);
	}
	return schedstat;
}

static struct proc_pid_sched fetch_proc_pid_sched(int pid) {
	struct proc_pid_sched sched = {0};
	char path[PATH_MAX];
	sprintf(path, "/proc/%d/sched", pid);
	bool found = false;
	FILE *f = fopen(path, "r");
	if (f != NULL) {
		ssize_t read;
		float value;
		char *line = NULL;
		size_t len = 0;
		while((read = getline(&line, &len, f)) != -1 ) {
			if (sscanf(line, "core_forceidle_sum%*[ \t]:%*[ \t]%f", &value) == 1) {
				sched.core_forceidle_sum = value;
				found = true;
				break;
			}
		}
		fclose(f);
		if (line)
			free(line);
	} else {
		fprintf(stderr, "Failed to read schedstat at %s\n", path);
	}
	if (!found) {
		fprintf(stderr, "Force idle not found for %d\n", pid);
	}
	return sched;
}

static int enable_sched_schedstats() {
	char path[] = "/proc/sys/kernel/sched_schedstats";
	FILE *f = fopen(path, "w");
	if (f != NULL) {
		if (fprintf(f, "1") < 0) {
			fprintf(stderr, "Could not write %s, %d\n", path, errno);
			return errno;
		};
		if (fclose(f)) {
			fprintf(stderr, "Could not close %s, %d\n", path, errno);
			return errno;
		};
	} else {
		fprintf(stderr, "Could not open %s, %d\n", path, errno);
		return errno;
	}

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
	uint64_t timeout_ns = clock_get_time_nsecs() + (uint64_t)(duration) * 1000000000lu;
	bool killed = false;
	int waiting_count = task_count;
	while (waiting_count) {
		siginfo_t info;
		uint64_t now_ns = clock_get_time_nsecs();
		if (now_ns > timeout_ns)
			timeout_ns = now_ns;
		struct timespec timeout;
		timeout.tv_sec = (timeout_ns - now_ns) / 1000000000lu;
		timeout.tv_nsec = (timeout_ns - now_ns) % 1000000000lu;
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
					printf("Task %d completed, status=%d, cpu_time_s=%.3f, runq_wait_time_s=%.3f, core_forceidle_sum=%.9f\n",
							terminated_pid,
							status,
							task_info[task_idx].schedstat.cpu_time / 1000000000.0,
							task_info[task_idx].schedstat.runq_wait_time / 1000000000.0,
							task_info[task_idx].sched.core_forceidle_sum / 1000.0);
					if (fprintf(out_f, "%d %d %llu %lu %d %llu %llu %f\n",
								task_idx,
								task_info[task_idx].pid,
								task_info[task_idx].cookie,
								clock_get_time_nsecs(),
								status,
								task_info[task_idx].schedstat.cpu_time,
								task_info[task_idx].schedstat.runq_wait_time,
								task_info[task_idx].sched.core_forceidle_sum) < 0) {
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
						task_info[task_idx].sched = fetch_proc_pid_sched(pid);
						kill(pid, SIGINT);
					}
				}
				killed = true;
				timeout_ns += 5000000000;
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

	printf("Enabling sched schedstats ...\n");
	enable_sched_schedstats();

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
