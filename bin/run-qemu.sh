#!/bin/bash

set -euxo pipefail

KERNEL_REPO=$1
KERNEL_IMAGE=$KERNEL_REPO/arch/x86/boot/bzImage
PERF=$KERNEL_REPO/tools/perf/perf
CPU_COUNT=8
MEMORY=16G
INITRAMFS=initramfs.cpio.gz
SHARED_DIR=/tmp/share
mkdir -p $SHARED_DIR
rm -f $INITRAMFS
$(dirname ${BASH_SOURCE[0]})/make-initramfs.sh $INITRAMFS $SHARED_DIR $PERF
qemu-system-x86_64 \
	-nographic \
	-enable-kvm \
	-cpu host,+vmx \
	-smp $CPU_COUNT,threads=2 \
	-m $MEMORY \
	-name test,debug-threads=on \
	-kernel $KERNEL_IMAGE \
	-initrd $INITRAMFS \
	-append "console=ttyS0" \
	-serial mon:stdio \
	-virtfs local,path=$SHARED_DIR,mount_tag=host_share,security_model=none
rm -f $INITRAMFS
cp -a $SHARED_DIR/out/. ./
export results_dir=results
perf script --script perf-script-schtest.py
