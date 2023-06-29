#include "stress.hpp"
#include <cerrno>
#include <cstdio>
#include <getopt.h>
#include <functional>
#include <stdlib.h>

static int parse_double(const char *str, double &val) {
	if (!str)
		return -EINVAL;

	char *endptr;

	errno = 0;
	double parsed;
	parsed = strtod(str, &endptr);

	if (errno != 0)
		return errno;

	if (endptr == NULL || '\0' != *endptr)
		return -EINVAL;

	val = parsed;

	return 0;
}

static uint32_t parse_uint32_t(const char *str, uint32_t &val) {
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

	val = (uint32_t)parsed;

	return 0;
}

int main(int argc, char *argv[]) {
	double period_secs = 0.02;
	uint32_t task_weight = 250u;
	double total_duration_secs = 1.0;

	static struct option long_options[] = {
		{"period",   required_argument, 0, 'p'},
		{"weight",   required_argument, 0, 'w'},
		{"duration", required_argument, 0, 'd'},
	};

	int c;
	int option_index = 0;
	int rc = 0;
	while(!rc) {
		c = getopt_long(argc, argv, "p:w:d:", long_options, &option_index);

		if (c == -1)
			break;

		switch(c) {
			case 'p':
				rc = parse_double(optarg, period_secs);
				break;
			case 'w':
				rc = parse_uint32_t(optarg, task_weight);
				break;
			case 'd':
				rc = parse_double(optarg, total_duration_secs);
				break;
			case '?':
				rc = -EINVAL;
				break;
			default:
				fprintf(stderr, "BUG - option with character code 0x%2x is not implemented", c);
				rc = -EINVAL;
				break;
		}
	}

	if (rc) {
		fprintf(stderr, "Parsing error for option %c at index %d with error code %d (errno = %d)", c, option_index, rc, errno);
		exit(rc);
	}

	printf("Stress config\n");
	printf("Period [s]         = %.3f\n", period_secs);
	printf("Task weight        = %u\n", task_weight);
	printf("Total duration [s] = %.3f\n\n", total_duration_secs);

	struct stress_cfg stress_cfg;
	stress_cfg.execute_task_item = default_bogops;
	stress_cfg.next_task_arrival_secs = [=] { return period_secs; };
	stress_cfg.next_task_weight = [=] { return task_weight; };
	stress_cfg.total_duration_secs = total_duration_secs;

	struct stress_result stress_result = stress(stress_cfg);
	print_stress_result(stdout, stress_result);

	return 0;
}
