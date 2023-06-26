#include "load.hpp"

int main() {
	struct stress_cfg stress_cfg;
	stress_cfg.execute_task_item = default_bogops;
	stress_cfg.next_task_arrival_secs = [] { return 0.020; };
	stress_cfg.next_task_weight = [] { return 250u; };
	stress_cfg.total_duration_secs = 10.0;

	struct stress_result stress_result = stress(stress_cfg);
	print_stress_result(stdout, stress_result);

	return 0;
}
