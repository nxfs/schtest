#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "time.h"

uint64_t clock_get_time_nsecs() {
	struct timespec ts;

	errno = 0;
	// must align with the clock used by perf, i.e `perf -k raw`
	int rc = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

	if (rc) {
		fprintf(stderr, "FATAL: clock_gettime returned %d, errno %d\n\n", rc, errno);
		exit(1);
	}

	return ((uint64_t)ts.tv_sec)*1000000000 + (uint64_t)ts.tv_nsec;
}

void sleep_nsecs(uint64_t nsecs) {
       struct timespec t, trem;

	t.tv_sec = nsecs / 1000000000;
	t.tv_nsec = nsecs % 1000000000;

	errno = 0;
	int rc = nanosleep(&t, &trem) < 0;
	if (rc) {
		if (errno == EINTR) {
			fprintf(stderr, "nanosleep was interrupted\n");
		} else {
			fprintf(stderr, "nanosleep returned %d, errno %d\n\n", rc, errno);
			exit(1);
		}
	}
}
