#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <unistd.h>

#include "cpuset.h"

int setup_cgroup_cpu_set(char *cgroup, char *cpu_set) {
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

int move_tasks_to_cgroup(char* cgroup, struct task_info* task_info, int task_count) {
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
