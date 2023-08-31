#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "schtest.h"
#include "smt.h"

static struct cpu_group* create_and_populate_cpu_group(struct cpu_group *prev_group, int cpu) {
	struct cpu_group *g = (struct cpu_group*)malloc(sizeof(*g));
	g->next = NULL;
	if (prev_group)
		prev_group->next = g;
	g->cpu_list = NULL;

	char thread_siblings_list_file[PATH_MAX];
	sprintf(thread_siblings_list_file, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
	FILE *f = fopen(thread_siblings_list_file, "r");
	if (f == NULL) {
		fprintf(stderr, "Unable to open thread sibling list at %s, errno = %d\n", thread_siblings_list_file, errno);
		return NULL;
	}
	char cpus[CPU_SET_LENGTH];
	if (fgets(cpus, CPU_SET_LENGTH, f) == NULL) {
		fprintf(stderr, "Unable to read thread sibling list at %s, errno = %d\n", thread_siblings_list_file, errno);
		return NULL;
	}
	fclose(f);

	struct cpu_node *prev_node = NULL, *curr_node = NULL;
	char *cpus_p = cpus;
	char *ptr_comma;
	while(1) {
		char *cpu = strtok_r(cpus_p, ",", &ptr_comma);
		cpus_p = NULL;
		if (cpu == NULL)
			break;

		curr_node = (struct cpu_node*)malloc(sizeof(*(g->cpu_list)));
		if (prev_node) {
			prev_node->next = curr_node;
		}
		else {
			g->cpu_list = curr_node;
		}

		curr_node->next = NULL;
		curr_node->cpu = atoi(cpu);
		prev_node = curr_node;
	}

	return g;
}

static int find_or_add_cpu(struct cpu_group **siblings, int cpu) {
	if (!*siblings) {
		*siblings = create_and_populate_cpu_group(NULL, cpu);
		return *siblings ? 0 : -1;
	}

	for(struct cpu_group *g = *siblings;; g = g->next) {
		for (struct cpu_node *n = g->cpu_list; n = n->next; n != NULL) {
			if (n->cpu == cpu)
				return 0;
		}
		if (g->next == NULL) {
			return create_and_populate_cpu_group(g, cpu) ? 0 : -1;
		}
	}
}

int fetch_cpu_topography(char *cpu_set_in, struct cpu_group **siblings) {
	char cpu_set[CPU_SET_LENGTH];
	strcpy(cpu_set, cpu_set_in);
	if (cpu_set == NULL) {
		int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
		for (int i = 0; i < cpu_count; i++)
			if (find_or_add_cpu(siblings, i))
				return -1;
	} else {
		char *ptr_comma;
		char *c = cpu_set;
		while(1) {
			char *cpu_range = strtok_r(c, ",", &ptr_comma);
			c = NULL;
			if (cpu_range == NULL)
				break;
			else {
				char *ptr_dash;
				char *cpu_range_t = cpu_range;
				char *first_cpu_s = strtok_r(cpu_range_t, "-", &ptr_dash);
				int first_cpu = atoi(first_cpu_s);
				char *second_cpu_s = strtok_r(NULL, "-", &ptr_dash);
				if (second_cpu_s == NULL) {
					if (find_or_add_cpu(siblings, first_cpu))
						return -1;
				}
				else {
					int second_cpu = atoi(second_cpu_s);
					for (; first_cpu <= second_cpu; first_cpu++)
						if (find_or_add_cpu(siblings, first_cpu))
							return -1;
				}
			}
		}
	}
	return 0;
}

void print_cpu_topography(FILE *out_f, struct cpu_group *siblings) {
	int siblings_count = 0;
	if (siblings) {
		for (struct cpu_group *curr = siblings; curr != NULL; curr = curr->next)
			siblings_count++;
	}
	fprintf(out_f, "%d\n", siblings_count);
	if (siblings) {
		for (struct cpu_group *curr_g = siblings; curr_g != NULL; curr_g = curr_g->next) {
			for (struct cpu_node *curr_n = curr_g->cpu_list; curr_n != NULL; curr_n = curr_n->next) {
				fprintf(out_f, "%d%s", curr_n->cpu, curr_n->next == NULL ? "\n" : " ");
			}
		}
	}
}
