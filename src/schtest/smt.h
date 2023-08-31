#ifndef __SMT_H__
#define __SMT_H__

struct cpu_node {
	int cpu;
	struct cpu_node *next;
};

struct cpu_group {
	struct cpu_node *cpu_list;
	struct cpu_group *next;
};

int fetch_cpu_topography(char *cpu_set_in, struct cpu_group **siblings);
void print_cpu_topography(FILE *out_f, struct cpu_group *siblings);

#endif
