#!/bin/bash

set -euxo pipefail

cd ..
KERNEL_REPO=/linux
SCRIPT="$@"
CPU_COUNT=$(nproc)
MODE=exit
/schedulerutils/qemu/host/run-qemu.sh $KERNEL_REPO $SCRIPT $CPU_COUNT $MODE
cp -r /tmp/share/out .
cd /out
/linux/tools/perf/perf sched latency

set +x
echo "Welcome to the container"
echo "The data collected from this run is in the perf.data file of this /out folder"
echo "Provided that you have mounted the /out folder as docker volume, any data that you write here will be available to the host after the run"
/bin/bash

