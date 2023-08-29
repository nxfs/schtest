#pragma once

#include <functional>
#include <stdint.h>
#include <stdio.h>

struct stress_cfg {
	std::function<void(void)> execute_task_item;
	std::function<double(void)> next_task_arrival_secs;
	std::function<uint32_t(void)> next_task_weight;
	double total_duration_secs;
};

struct stress_result {
	uint64_t task_item_count;
	uint64_t task_count;
	double total_wall_time_secs;
	double total_cpu_time_secs;
	double total_sleep_time_secs;
	double total_lost_time_secs;
};

void default_bogops();
void print_stress_result(FILE *stream, const struct stress_result &stress_result);
struct stress_result stress(const struct stress_cfg &stress_cfg, bool &interrupted);
