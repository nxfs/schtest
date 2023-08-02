#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define CPU_SET_LENGTH 32

struct task_spec {
	char *cmd;
	int count;
	struct task_spec *next;
};

void get_out_filename(char *out_filename, int task_idx) {
	sprintf(out_filename, "fork_%d.txt", task_idx);
}

static uint32_t parse_uint32_t(const char *str, uint32_t *val) {
	if (!str)
		return -EINVAL;

	char *endptr;

	errno = 0;
	long int parsed;
	parsed = strtol(str, &endptr, 10);

	if (errno != 0)
		return errno;

	if (endptr == NULL || '\0' != *endptr)
		return -EINVAL;

	if (parsed < 0 || parsed > UINT32_MAX)
		return -EINVAL;

	*val = (uint32_t)parsed;

	return 0;
}

static void exec_task(struct task_spec *spec, int task_idx) {
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
	get_out_filename(out_filename, task_idx);
	int fd = open(out_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		fprintf(stderr, "Unable to redirect stdout/stderr of fork %d with cmd %s at file %s\n", task_idx, spec->cmd, out_filename);
	} else {
		int rc;
		rc = dup2(fd, 1); // stdout
		if (rc == -1)
			fprintf(stderr, "Unable to redirect stdout of fork %d with cmd %s at file %s, errno = %d\n", task_idx, spec->cmd, out_filename, errno);
		rc = dup2(fd, 2); // stderr
		if (rc == -1)
			fprintf(stderr, "Unable to redirect stderr of fork %d with cmd %s at file %s, errno = %d\n", task_idx, spec->cmd, out_filename, errno);
		close(fd);
	}

	execve(newargv[0], newargv, newenviron); // doesn't return if successful
	fprintf(stderr, "execve of failed for %s with errno %d\n", spec->cmd, errno);
	exit(1);
}

static pid_t fork_task(struct task_spec *spec, int task_idx) {
	pid_t c_pid = fork();
	if (c_pid == 0) {
		exec_task(spec, task_idx); // doesn't return
	}
	return c_pid;
}

int setup_cgroup_cpu_set(char* cgroup, char* cpu_set) {
	char cgroup_cpu_set[PATH_MAX];
	sprintf(cgroup_cpu_set, "%s/cpuset.cpus", cgroup);

	if (access(cgroup_cpu_set, F_OK) == 0) {
		printf("Setting up cgroup %s cpu set <%s> ...\n", cgroup, cpu_set);

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

int print_cookie(int task_idx, int pid) {
	unsigned long long cookie;
	int rc = prctl(PR_SCHED_CORE, PR_SCHED_CORE_GET, pid, PR_SCHED_CORE_SCOPE_THREAD, (unsigned long)&cookie);
	if (rc)
		fprintf(stderr, "Could not get cookie for task %d with pid %d, rc = %d, errno = %d\n", task_idx, pid, rc, errno);
	else
		fprintf(stdout, "Cookie for task %d with pid %d is %llx\n", task_idx, pid, cookie);
	return rc;
}

int create_cookie(int cookie_count, int task_idx, int task_pid) {
	if (!cookie_count || task_idx >= cookie_count)
		return 0;

	int rc = prctl(PR_SCHED_CORE, PR_SCHED_CORE_CREATE, task_pid, PR_SCHED_CORE_SCOPE_THREAD, 0);
	if (rc) {
		fprintf(stderr, "Could not create cookie for task %d with pid %d, rc = %d, errno = %d\n", task_idx, task_pid, rc, errno);
	} else {
		rc = print_cookie(task_idx, task_pid);
	}

	return rc;
}

int copy_cookie(int cookie_count, int task_idx, int donor_task_pid) {
	if (!cookie_count || task_idx < cookie_count)
		return 0;

	int rc = prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_FROM, donor_task_pid, PR_SCHED_CORE_SCOPE_THREAD, 0);
	if (rc) {
		fprintf(stderr, "Task %d could not share cookie from task with pid %d, rc = %d, errno = %d\n", getpid(), donor_task_pid, rc, errno);
	} else {
		rc = print_cookie(task_idx, getpid());
	}

	return rc;
}

int move_tasks_to_cgroup(char* cgroup, int* task_pids, int task_count) {
	printf("Moving tasks to cgroup %s ...\n", cgroup);
	char cgroup_procs[PATH_MAX];
	sprintf(cgroup_procs, "%s/cgroup.procs", cgroup);
	for (int i = 0; i < task_count; i++) {
		FILE *f = fopen(cgroup_procs, "a");
		if (!f) {
			fprintf(stderr, "Could not open cgroup.procs at %s, errno = %d\n", cgroup_procs, errno);
			return errno;
		}
		if (fprintf(f, "%d", task_pids[i]) < 0) {
			fprintf(stderr, "Could not write pid %d of task %d to cgroup.procs at %s, errno = %d\n", task_pids[i], i, cgroup_procs, errno);
			return errno;
		}
		if (fclose(f)) {
			fprintf(stderr, "Could not close cgroup.procs file at %s, errno = %d\n", cgroup_procs, errno);
			return errno;
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"task",       required_argument, 0, 't'},
		{"task_count", required_argument, 0, 'n'},
		{"cgroup",     required_argument, 0, 'g'},
		{"cpu_set",    required_argument, 0, 's'},
		{"cookies",    required_argument, 0, 'c'},
	};

	struct task_spec *next = NULL, *prev = NULL, *head = NULL;
	char cgroup[PATH_MAX / 2] = "";
	char cpu_set[CPU_SET_LENGTH] = "";
	int cookie_count = 0;
	int c;
	int option_index = 0;
	int rc = 0;
	while(!rc) {
		c = getopt_long(argc, argv, "t:n:g:s:c:", long_options, &option_index);

		if (c == -1)
			break;

		if (c == 'c' && head == NULL) {
			fprintf(stderr, "Option %c applies to task, specify a task with -t first", c);
			rc = -EINVAL;
			break;
		}

		switch(c) {
			case 't':
				prev = next;
				next = (struct task_spec*)malloc(sizeof(*next));
				if (head == NULL) {
					head = next;
				} else {
					prev->next = next;
				}
				next->next = NULL;
				next->cmd = (char*)malloc(strlen(optarg) * sizeof(char));
				next->count = 1;
				strcpy(next->cmd, optarg);
				break;
			case 'n':
				rc = parse_uint32_t(optarg, &next->count);
				break;
			case 'g':
				if (strlen(optarg) >= PATH_MAX / 2) {
					fprintf(stderr, "cgroup filename %s is too long\n", optarg);
					rc = -EINVAL;
				} else {
					strcpy(cgroup, optarg);
				}
				break;
			case 's':
				if (strlen(optarg) >= CPU_SET_LENGTH) {
					fprintf(stderr, "cpu_set %s is too long\n", optarg);
					rc = -EINVAL;
				} else {
					strcpy(cpu_set, optarg);
				}
				break;
			case 'c':
				rc = parse_uint32_t(optarg, &cookie_count);
				break;
			case '?':
				rc = -EINVAL;
				break;
			default:
				fprintf(stderr, "BUG - option with character code 0x%2x is not implemented\n", c);
				rc = -EINVAL;
				break;
		}
	}

	if (rc) {
		fprintf(stderr, "Parsing error for option %c at index %d with error code %d (errno = %d)\n\n", c, option_index, rc, errno);
	}

	if (head == NULL) {
		fprintf(stderr, "At least one task must be specified\n");
		rc = -EINVAL;
	}

	if (cgroup[0] == '\0' && cpu_set[0] != '\0') {
		fprintf(stderr, "Cannot use cpu set %s without specifying a cgroup\n", cpu_set);
		rc = -EINVAL;
	}

	if (rc) {
		fprintf(stderr, "Usage: schtest [TASKS]... [OPTION]...\n");
		fprintf(stderr, "Executes tasks\n\n");
		fprintf(stderr, "\t-t <cmd>\tAdds a task group. Can be used multiple times. Each task in the group is executed by the given <cmd>.\n");
		fprintf(stderr, "\t-n <N>\t\tThe number of tasks in the group. Each task is executed with the given <cmd> passed with the -t argument.\n");
		fprintf(stderr, "\t-g <cgroup>\tThe cgroup path. Each task is moved in the corresponding cgroup.\n");
		fprintf(stderr, "\t-s <cpuset>\tThe cpuset. The cgroup is bound to the given cpuset.\n");
		fprintf(stderr, "\t-c <N>\t\tThe number of core scheduling cookies. If non-zero, each task is assigned the cookie corresponding to its index modulo the cookie count.\n\n");
		fprintf(stderr, "Examples:\n\n");
		fprintf(stderr, "\tschtest -t \"bin/stress\"");
		fprintf(stderr, "\n\t\t* Launches bin/stress");
		fprintf(stderr, "\n\n");
		fprintf(stderr, "\tschtest -t \"bin/stress -d 20\" -t \"bin/stress -d 10\" -n 3 -g \"cgroup2/schtest\" -s \"1-12\" -c 2");
		fprintf(stderr, "\n\t\t* Launches 1 x \"bin/stress -d 20\" and 3 x \"bin/stress -d 10\"");
		fprintf(stderr, "\n\t\t* Move launched processes to the cgroup cgroup2/schtest");
		fprintf(stderr, "\n\t\t* Assign the cpuset \"1-12\" to the cgroup");
		fprintf(stderr, "\n\t\t* Create two cookies and assign them to the four launched processes, using round robin assignment");
		fprintf(stderr, "\n\n");

		exit(rc);
	}

	printf("Tasks\n");
	int task_count = 0;
	for (struct task_spec *curr = head; curr != NULL; curr = curr->next) {
		char out_filename[PATH_MAX];
		for (int i = 0; i < curr->count; i++) {
			get_out_filename(out_filename, task_count);
			printf("\t%3d: %s > %s 2>&1\n", task_count++, curr->cmd, out_filename);
		}
	}

	if (cookie_count > task_count) {
		fprintf(stderr, "Cannot use more cookies (%d) than tasks (%d)\n", cookie_count, task_count);
		return -EINVAL;
	}

	printf("\nForking tasks...\n");
	int *children_pids = (int*)malloc(task_count * sizeof(int));
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
				if (copy_cookie(cookie_count, task_idx, cookie_count ? children_pids[task_idx % cookie_count] : -1)) {
					fprintf(stderr, "Failed to copy cookies for task %d with pid %d\n", task_idx, c_pid);
					exit(1);
				}
				exec_task(curr, task_idx); // doesn't return
			} else {
				children_pids[task_idx] = c_pid;
				if (create_cookie(cookie_count, task_idx, c_pid)) {
					fprintf(stderr, "Failed to create cookie for task %d with pid %d\n", task_idx, c_pid);
					exit(1);
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
		if (move_tasks_to_cgroup(cgroup, children_pids, task_count)) {
			fprintf(stderr, "Failed to move tasks to cgroup\n");
			exit(1);
		}
	}


	printf("Waiting for tasks completion...\n");
	pid_t wpid;
	int status = 0;
	while ((wpid = wait(&status)) > 0) {
		printf("Task %d completed with status %d\n", wpid, status);
	}
	printf("Done\n");

	return 0;
}
