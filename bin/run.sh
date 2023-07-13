#/bin/bash

is_mounted() {
	cd "$1"
	path=$(pwd)
	cd -
	mount | awk -v DIR="$path" '{if ($3 == DIR) { exit 0}} ENDFILE{exit -1}'
}

set -euxo pipefail

cg_mnt=./cgroup2
mkdir -p $cg_mnt
if ! is_mounted "$cg_mnt"; then
	mount -t cgroup2 none $cg_mnt
fi
schtest_cg=$cg_mnt/schtest
mkdir -p $schtest_cg
stc=$schtest_cg/cgroup.subtree_control
echo "+cpu +cpuset" > $stc
pid=$$
> $schtest_cg/cgroup.procs
echo $pid > $schtest_cg/cgroup.procs

mount -o remount,mode=755 /sys/kernel/debug/tracing/

perf sched record -- ./bin/schtest "$@ -g $schtest_cg" > schtest.out.txt

perf script --script perf-script-schtest.py
