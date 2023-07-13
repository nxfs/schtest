#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define OUT_FILENAME_LENGTH 32

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
	char out_filename[OUT_FILENAME_LENGTH];
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

int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"task",       required_argument, 0, 't'},
		{"task_count", required_argument, 0, 'c'},
	};

	struct task_spec *next = NULL, *prev = NULL, *head = NULL;
	int c;
	int option_index = 0;
	int rc = 0;
	while(!rc) {
		c = getopt_long(argc, argv, "t:c:", long_options, &option_index);

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
			case 'c':
				rc = parse_uint32_t(optarg, &next->count);
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

	if (head == NULL) {
		fprintf(stderr, "At least one task must be specified\n");
		rc = -EINVAL;
	}

	if (rc) {
		fprintf(stderr, "Parsing error for option %c at index %d with error code %d (errno = %d)\n", c, option_index, rc, errno);
		exit(rc);
	}

	printf("Tasks\n");
	int task_idx = 0;
	for (struct task_spec *curr = head; curr != NULL; curr = curr->next) {
		char out_filename[OUT_FILENAME_LENGTH];
		for (int i = 0; i < curr->count; i++) {
			get_out_filename(out_filename, task_idx);
			printf("\t%3d: %s > %s 2>&1\n", task_idx++, curr->cmd, out_filename);
		}
	}

	int *children_pids = (int*)malloc(task_idx * sizeof(int));
	bool is_child = false;
	task_idx = 0;
	for (struct task_spec *curr = head; curr != NULL; curr = curr->next) {
		for (int i = 0; i < curr->count; i++) {
			pid_t c_pid = fork_task(curr, task_idx);
			if (c_pid == -1) {
				fprintf(stderr, "Fork failure for task %d\n", task_idx);
				while (task_idx > 0) {
					killpg(--task_idx, SIGKILL);
				}
				exit(1);
			} else {
				children_pids[task_idx++] = c_pid;
			}
		}
	}

	if (!is_child) {
		printf("Forked children, waiting for completion...\n");
		pid_t wpid;
		int status = 0;
		while ((wpid = wait(&status)) > 0) {
			printf("Task %d completed with status %d\n", wpid, status);
		}
		printf("Done\n");
	}

	return 0;
}
