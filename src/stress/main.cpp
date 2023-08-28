#include "stress.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <functional>
#include <getopt.h>
#include <random>

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
	double total_duration_secs = 1.0;
	double period_secs = 0.02;
	uint32_t task_weight = 250u;
	double period_variance = 0.5;
	double weight_variance = 0.5;

	static struct option long_options[] = {
		{"period",          required_argument, 0, 'p'},
		{"weight",          required_argument, 0, 'w'},
		{"duration",        required_argument, 0, 'd'},
		{"period-variance", required_argument, 0, 'P'},
		{"weight-variance", required_argument, 0, 'W'},
	};

	int c;
	int option_index = 0;
	int rc = 0;
	while(!rc) {
		c = getopt_long(argc, argv, "t:p:w:d:P:W:", long_options, &option_index);

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
			case 'P':
				rc = parse_double(optarg, period_variance);
				break;
			case 'W':
				rc = parse_double(optarg, weight_variance);
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
		fprintf(stderr, "Usage: stress [OPTION]...\n");
		fprintf(stderr, "Run bogops at a fixed, randomized arrival rate.\n");
		fprintf(stderr, "\t-p <period>\t\tThe period of arrival. Default 20ms.\n");
		fprintf(stderr, "\t-w <weight>\t\tThe weight of the task, in number of bogops. The execution speed is machine specific and needs calibration. Default 250.\n");
		fprintf(stderr, "\t-d <duration>\t\tThe total duration of the test. Default 1s.\n");
		fprintf(stderr, "\t-P <period variance>\tThe variance of the periods of arrival. Default 0.5.\n");
		fprintf(stderr, "\t-W <weight variance>\tThe variance of the weights. Default 0.5.\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "Model:\n");
		fprintf(stderr, "The stressor is modeled as a constant task arrival rate system.\n");
		fprintf(stderr, "A task is comprised of W bogops operaion, where W corresponds to the task weight\n");
		fprintf(stderr, "The task arrival times are computed in wall clock time *independently* of task executions.\n");
		fprintf(stderr, "If the next task arrival time is ahead of the current wall clock time, the process will sleep accordingly.\n");
		fprintf(stderr, "On the other hand, if the next task arrival time is behind the current wall clock time, the task is executed immediately.\n");
		fprintf(stderr, "This means depending on the input configuration and the CPU contention the process can morph into a busy loop if the execution of tasks falls behind arrival rates.\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "Variance:\n");
		fprintf(stderr, "* The variance is a +/- random factor applied to period and arrival.\n");
		fprintf(stderr, "* A period/weight variance of 0.5 means that the next task arrival/weight will be of the specified config value +/- 50%%.\n");
		fprintf(stderr, "* Negative values are left as it is for periods (as if the execution falls behind).\n");
		fprintf(stderr, "* Negative and zero values for weights are defaulted to 1.\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "Calibration:\n");
		fprintf(stderr, "* The bogops rate in cpu time (the clock that only advances when the process is running) should be constant regardless of the configuration.\n");
		fprintf(stderr, "* Using the bogops rate one can compute the theoretical completion time of a task (weight / bogops_rate).\n");
		fprintf(stderr, "* A sleep time of zero or near zero is indicative of the stressor running as busy loop (task execution falls behind task arrival rate).\n");
		fprintf(stderr, "* The bogops count is indicative of the quantity of work performed by the stressor.\n");
		fprintf(stderr, "\n\n");

		exit(rc);
		exit(rc);
	}

	printf("Stress config\n");
	printf("Total duration [s] = %.3f\n", total_duration_secs);
	printf("Period [s]         = %.3f (+/- %.3f)\n", period_secs, period_secs * period_variance);
	printf("Task weight        = %u (+/- %u)\n", task_weight, int(task_weight * weight_variance));
	printf("\n");

	// using hi res as seed to avoid collisions if stress is started multiple times in a short interval
	auto now = std::chrono::high_resolution_clock::now();
	auto timestamp = now.time_since_epoch().count();
	std::mt19937_64 rng(timestamp);
	std::uniform_real_distribution<double> distribution(-1, 1);

	struct stress_cfg stress_cfg;
	stress_cfg.execute_task_item = default_bogops;
	stress_cfg.next_task_arrival_secs = [&] { return period_secs * (1 + period_variance * distribution(rng)); };
	stress_cfg.next_task_weight = [&] { return std::max(task_weight * (1 + weight_variance * distribution(rng)), 1.0); };
	stress_cfg.total_duration_secs = total_duration_secs;

	struct stress_result stress_result = stress(stress_cfg);
	print_stress_result(stdout, stress_result);

	return 0;
}
