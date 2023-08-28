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

export results_dir=results

args=()
# Iterate through each argument
while [[ $# -gt 0 ]]; do
    # Check if the argument contains spaces
    if [[ "$1" == *" "* ]]; then
        # Preserve double quotes if present
        if [[ "$1" == \"*\" ]]; then
            args+=("${1#\"}"  # Remove leading double quote
                   "${1%\"}") # Remove trailing double quote
        else
            args+=("$1")
        fi
    else
        args+=($1)
    fi
    shift
done
args+=(-g $schtest_cg -d $results_dir)
perf sched record -k raw -- ./bin/schtest "${args[@]}" > schtest.out.txt

perf script --script perf-script-schtest.py
