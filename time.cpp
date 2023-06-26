#include "time.hpp"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

uint64_t clock_get_time_nsecs(clockid_t clk_id) {
	struct timespec ts;

	errno = 0;
	int rc = clock_gettime(clk_id, &ts);

	if (rc) {
		fprintf(stderr, "FATAL: clock_gettime returned %d, errno %d\n\n", rc, errno);
		exit(1);
	}

	return ((uint64_t)ts.tv_sec)*1000000000 + (uint64_t)ts.tv_nsec;
}

uint64_t clock_get_real_time_nsecs() {
	return clock_get_time_nsecs(CLOCK_REALTIME);
}

uint64_t clock_get_cpu_time_nsecs() {
	return clock_get_time_nsecs(CLOCK_PROCESS_CPUTIME_ID);
}

void sleep_nsecs(uint64_t nsecs) {
       struct timespec t, trem;

	t.tv_sec = nsecs / 1000000000;
	t.tv_nsec = nsecs % 1000000000;

	errno = 0;
	int rc = nanosleep(&t, &trem) < 0;
	if (rc) {
		if (errno == EINTR) {
			fprintf(stderr, "WARNING: nanosleep was interrupted\n");
		} else {
			fprintf(stderr, "FATAL: nanosleep returned %d, errno %d\n\n", rc, errno);
			exit(1);
		}
	}
}

