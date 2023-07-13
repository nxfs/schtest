#include <stdint.h>
#include <sys/time.h>
#include <time.h>

uint64_t clock_get_time_nsecs(clockid_t clk_id);
uint64_t clock_get_real_time_nsecs();
uint64_t clock_get_cpu_time_nsecs();
void sleep_nsecs(uint64_t nsecs);
