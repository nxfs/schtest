#include "stress.hpp"
#include "time.hpp"

#include <iostream>
#include <sys/time.h>

void default_bogops() {
	int a = rand();
	int b = rand();
	int c1 = rand();
	int c2 = rand();
	int c3 = rand();

	for (int i = 0; i < 1000; i++) {
		b ^= a;
		a >>= 1;
		b <<= 2;
		b -= a;
		a ^= (int)~0;
		b ^= ~(c1);
		a *= 3;
		b *= 7;
		a += 2;
		b -= 3;
		a /= 77;
		b /= 3;
		a <<= 1;
		b <<= 2;
		a |= 1;
		b |= 3;
		a *= rand();
		b ^= rand();
		a += rand();
		b -= rand();
		a /= 7;
		b /= 9;
		a |= (c2);
		b &= (c3);
	}
}

struct stress_result stress(const struct stress_cfg &stress_cfg, bool &interrupted) {
	uint64_t task_item_count = 0;
	uint64_t task_count = 0;
	uint64_t total_wall_time_nsecs = 0;
	uint64_t total_cpu_time_nsecs = 0;
	uint64_t total_sleep_time_nsecs = 0;
	uint64_t total_lost_time_nsecs = 0;

	uint64_t start_time_nsecs = clock_get_real_time_nsecs();
	uint64_t start_time_cpu_nsecs = clock_get_cpu_time_nsecs();
	uint64_t end_time_nsecs = start_time_nsecs + stress_cfg.total_duration_secs * 1000000000;
	uint64_t next_task_arrival_nsecs = start_time_nsecs;

	do {
		uint32_t next_task_weight = stress_cfg.next_task_weight();
		for (uint32_t i = 0; i < next_task_weight; i++) {
			stress_cfg.execute_task_item();
		}
		task_item_count += next_task_weight;
		task_count++;

		next_task_arrival_nsecs += stress_cfg.next_task_arrival_secs() * 1000000000;

		if (next_task_arrival_nsecs > end_time_nsecs)
			next_task_arrival_nsecs = end_time_nsecs;

		uint64_t now_nsecs = clock_get_real_time_nsecs();

		if (interrupted || (stress_cfg.total_duration_secs > 0 && now_nsecs > end_time_nsecs))
			break;

		if (next_task_arrival_nsecs <= now_nsecs) {
			total_lost_time_nsecs += now_nsecs - next_task_arrival_nsecs;
		} else {
			uint64_t sleep_time_nsecs = next_task_arrival_nsecs - now_nsecs;
			total_sleep_time_nsecs += sleep_time_nsecs;
			sleep_nsecs(sleep_time_nsecs);
		}
	} while(!interrupted && (stress_cfg.total_duration_secs == 0 || next_task_arrival_nsecs < end_time_nsecs));

	uint64_t now_nsecs = clock_get_real_time_nsecs();
	uint64_t now_cpu_nsecs = clock_get_cpu_time_nsecs();

	struct stress_result stress_result;
	stress_result.task_item_count = task_item_count;
	stress_result.task_count = task_count;
	stress_result.total_wall_time_secs = (double)(now_nsecs - start_time_nsecs) / 1000000000.0;
	stress_result.total_cpu_time_secs = (double)(now_cpu_nsecs - start_time_cpu_nsecs) / 1000000000.0;
	stress_result.total_sleep_time_secs = (double)(total_sleep_time_nsecs) / 1000000000.0;
	stress_result.total_lost_time_secs = (double)(total_lost_time_nsecs) / 1000000000.0;
	return stress_result;
}

void print_stress_result(FILE *stream, const struct stress_result &stress_result) {
	fprintf(stream, "Stress result\n");
	fprintf(stream, "Task count                      = %lu\n", stress_result.task_count);
	fprintf(stream, "Task avg duration [ms]          = %.3f\n", stress_result.total_cpu_time_secs * 1000.0 / (double)(stress_result.task_count));
	fprintf(stream, "Task rate (cpu time) [/s]       = %.3f\n", (double)(stress_result.task_count) / stress_result.total_cpu_time_secs);
	fprintf(stream, "Task rate (wall time) [/s]      = %.3f\n", (double)(stress_result.task_count) / stress_result.total_wall_time_secs);
	fprintf(stream, "Bogops count                    = %lu\n", stress_result.task_item_count);
	fprintf(stream, "Bogops avg duration [ms]        = %.3f\n", stress_result.total_cpu_time_secs * 1000.0 / (double)(stress_result.task_item_count));
	fprintf(stream, "Bogops rate (cpu time) [/s]     = %.3f\n", (double)(stress_result.task_item_count) / stress_result.total_cpu_time_secs);
	fprintf(stream, "Bogops rate (wall time) [/s]    = %.3f\n", (double)(stress_result.task_item_count) / stress_result.total_wall_time_secs);
	fprintf(stream, "Total wall time [s]             = %.3f\n", stress_result.total_wall_time_secs);
	fprintf(stream, "Total cpu time [s]              = %.3f\n", stress_result.total_cpu_time_secs);
	fprintf(stream, "Total sleep time [s]            = %.3f\n", stress_result.total_sleep_time_secs);
	fprintf(stream, "Total lost time [s]             = %.3f\n", stress_result.total_lost_time_secs);
	fprintf(stream, "Active ratio                    = %.3f\n", stress_result.total_cpu_time_secs / stress_result.total_wall_time_secs);
}
