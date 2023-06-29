#/bin/bash

set -euxo pipefail

mount -o remount,mode=755 /sys/kernel/debug/tracing/
if ! perf sched record -- ./schtest "$@"; then
	echo "perf command failed; try run the script as root once to initialize tracing?"
fi

perf script --script perf-script.py
