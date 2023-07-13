#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

struct task_spec {
	char *cmd;
	char out_file[32];
	struct task_spec *next;
};

int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"task", required_argument, 0, 't'},
	};

	struct task_spec *next = NULL, *prev = NULL, *head = NULL;
	int c;
	int option_index = 0;
	int rc = 0;
	int task_count = 0;
	while(!rc) {
		c = getopt_long(argc, argv, "t:", long_options, &option_index);

		if (c == -1)
			break;

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
				sprintf(next->out_file, "fork_%d.txt", task_count);
				strcpy(next->cmd, optarg);
				task_count++;

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
	for (struct task_spec *curr = head; curr != NULL; curr = curr->next) {
		printf("\t%s > %s 2>&1\n", curr->cmd, curr->out_file);
	}

	int *children_pids = (int*)malloc(task_count * sizeof(int));
	bool is_child = false;
	int task_idx = 0;
	for (struct task_spec *curr = head; curr != NULL; curr = curr->next) {
		pid_t c_pid = fork();
		if (c_pid == -1) {
			fprintf(stderr, "Fork failure for task %d\n", task_idx);
			while (task_idx > 0) {
				killpg(--task_idx, SIGKILL);
			}
			exit(1);
		}
		else if (c_pid == 0) {
			is_child = true;

			// arg max refers to the total argument size, not argc, just over allocate here to keep it simple
			// we don't have to check for overflows because the input args are derived from this program's input args which is also subject to the same limit
			long arg_max = sysconf(_SC_ARG_MAX);
			char **newargv = (char**)malloc(arg_max * sizeof(char*));
			int newargc = 0;
			const char *s = " ";
			char *token;
			token = strtok(curr->cmd, s);
			newargv[newargc++] = token;
			if (newargv[0] == NULL) {
				fprintf(stderr, "Null executable for cmd '%s', aborting fork\n", curr->cmd);
				exit(1);
			}
			while ((token = strtok(NULL, s)) != NULL) {
				newargv[newargc++] = token;
			}
			newargv[newargc] = NULL;
			char *newenviron[] = { NULL };

			int fd = open(curr->out_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
			if (fd == -1) {
				fprintf(stderr, "Unable to redirect stdout/stderr of fork %d with cmd %s at file %s\n", task_idx, curr->cmd, curr->out_file);
			} else {
				rc = dup2(fd, 1); // stdout
				if (rc == -1)
					fprintf(stderr, "Unable to redirect stdout of fork %d with cmd %s at file %s, errno = %d\n", task_idx, curr->cmd, curr->out_file, errno);
				rc = dup2(fd, 2); // stderr
				if (rc == -1)
					fprintf(stderr, "Unable to redirect stderr of fork %d with cmd %s at file %s, errno = %d\n", task_idx, curr->cmd, curr->out_file, errno);
				close(fd);
			}

			execve(newargv[0], newargv, newenviron);
			fprintf(stderr, "execve of failed for %s with errno %d\n", curr->cmd, errno);
			exit(0);
		} else {
			children_pids[task_idx++] = c_pid;
		}
	}

	if (!is_child) {
		printf("Forked children, waiting for completion...\n");
		pid_t wpid;
		int status = 0;
		while ((wpid = wait(&status)) > 0);
		printf("Done\n");
	}

	return 0;
}
