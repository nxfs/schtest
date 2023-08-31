#include <errno.h>
#include <getopt.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "schtest.h"

static void print_usage(FILE * out) {
	fprintf(out, "Usage: schtest [TASKS]... [OPTION]...\n");
	fprintf(out, "Executes tasks\n");
	fprintf(out, "\t-t <cmd>\tAdds a task group. Can be used multiple times. Each task in the group is executed by the given <cmd>.\n");
	fprintf(out, "\t-n <N>\t\tThe number of tasks in the group. Each task is executed with the given <cmd> passed with the -t argument.\n");
	fprintf(out, "\t-g <cgroup>\tThe cgroup path. Each task is moved in the corresponding cgroup.\n");
	fprintf(out, "\t-s <cpuset>\tThe cpuset. The cgroup is bound to the given cpuset.\n");
	fprintf(out, "\t-c <N>\t\tThe number of core scheduling cookies. If non-zero, each task is assigned the cookie corresponding to its index modulo the cookie count.\n");
	fprintf(out, "\t-D <dir>\tThe results directory. Defaults to the working directory.\n");
	fprintf(out, "\t-f \t\tUse fake cookies. Cookies won't be set but the generated report will be as if cookies were set. This is useful for a/b testing.\n");
	fprintf(out, "\t-d <duration>\t\tThe total duration of the test in seconds. Default is 0, in which case it waits for all tasks to exit.\n");
	fprintf(out, "\nResult directory\n");
	fprintf(out, "\tout.txt contains test results in the following format, line by line:\n");
	fprintf(out, "\t\t[cpu set]\n");
	fprintf(out, "\t\t[cpu sibling count]\n");
	fprintf(out, "\t\tFor each cpu sibling group: [cpu_0], [cpu_1], ...\n");
	fprintf(out, "\t\t[process count]\n");
	fprintf(out, "\t\tFor each process: [task_idx] [pid] [cookie] [stop_ns] [exit_code] [cpu_time_ns] [runq_wait_time_ns]\n");
	fprintf(out, "\t\t\tcpu_time and runq_wait time will only be available if schtest interrupts the task; in other words the task must run for longer than the test duration\n");
	fprintf(out, "\t\t[start_ns] [stop_ns]\n");
	fprintf(out, "\tfork_[task_idx].txt contains the stdout and stderr of each executed task\n");
	fprintf(out, "\nExamples of usage\n");
	fprintf(out, "\tschtest -t \"bin/stress\"");
	fprintf(out, "\n\t\t* Launches bin/stress");
	fprintf(out, "\n\n");
	fprintf(out, "\tschtest -t \"bin/stress -d 20\" -t \"bin/stress -d 10\" -n 3 -g \"cgroup2/schtest\" -s \"1-12\" -c 2");
	fprintf(out, "\n\t\t* Launches 1 x \"bin/stress -d 20\" and 3 x \"bin/stress -d 10\"");
	fprintf(out, "\n\t\t* Move launched processes to the cgroup cgroup2/schtest");
	fprintf(out, "\n\t\t* Assign the cpuset \"1-12\" to the cgroup");
	fprintf(out, "\n\t\t* Create two cookies and assign them to the four launched processes, using round robin assignment");
	fprintf(out, "\n\t\t  Each even numbered task will get the first cookie and each odd numbered task will get the second cookie");
	fprintf(out, "\n\n");
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

static int parse_string(const char *str, int max_length, char *out) {
	if (strlen(str) >= max_length) {
		fprintf(stderr, "string %s is too long\n", str);
		return -EINVAL;
	}
	strcpy(out, str);
	return 0;
}

static int make_results_dir(char *results_dir) {
	struct stat st = {0};
	if (stat(results_dir, &st) == -1) {
		if (mkdir(results_dir, 0600) == -1) {
			fprintf(stderr, "Could not make results dir at %s, errno = %d\n", results_dir, errno);
			return errno;
		}
	} else {
		if (!S_ISDIR(st.st_mode)) {
			fprintf(stderr, "Not a valid results directory: %s\n", results_dir);
			return -ENOTDIR;
		}
		if (!st.st_mode & S_IWRITE) {
			fprintf(stderr, "Not a writeable results directory: %s\n", results_dir);
			return -EPERM;
		}
	}
	return 0;
}


int main(int argc, char *argv[]) {
	static struct option long_options[] = {
		{"task",       	 required_argument, 0, 't'},
		{"task_count", 	 required_argument, 0, 'n'},
		{"cgroup",     	 required_argument, 0, 'g'},
		{"cpu_set",    	 required_argument, 0, 's'},
		{"cookies",    	 required_argument, 0, 'c'},
		{"dir",        	 required_argument, 0, 'd'},
		{"fake_cookies", no_argument,       0, 'f'},
		{"duration",     required_argument, 0, 'd'},
	};

	struct task_spec *next = NULL, *prev = NULL, *head = NULL;
	char cgroup[PATH_MAX / 2] = "";
	char results_dir[PATH_MAX / 2] = ".";
	char cpu_set[CPU_SET_LENGTH] = "";
	int cookie_count = 0;
	int c;
	int option_index = 0;
	int rc = 0;
	bool fake_cookies = false;
	uint32_t duration = 0;
	while(!rc) {
		c = getopt_long(argc, argv, "t:n:g:s:c:D:fd:", long_options, &option_index);

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
				rc = parse_string(optarg, PATH_MAX / 2, cgroup);
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
			case 'D':
				rc = parse_string(optarg, PATH_MAX / 2, results_dir);
				break;
			case 'f':
				fake_cookies = true;
				break;
			case 'd':
				rc = parse_uint32_t(optarg, &duration);
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
		print_usage(stderr);
		exit(rc);
	}

	if (make_results_dir(results_dir)) {
		fprintf(stderr, "Could not initialize results directory\n");
		exit(1);
	}

	return run(head, duration, cookie_count, fake_cookies, cgroup, cpu_set, results_dir);
}
