#!/bin/bash

set -exo pipefail

KERNEL_REPO=$1
SCRIPT=$2
MODE=$3

KERNEL_IMAGE=$KERNEL_REPO/arch/x86/boot/bzImage
PERF=$KERNEL_REPO/tools/perf/perf
CPU_COUNT=8
MEMORY=16G
INITRAMFS=initramfs.cpio.gz

SHARED_DIR=/tmp/share
mkdir -p $SHARED_DIR/bin
rm -f $INITRAMFS
USER_SCRIPT=$SHARED_DIR/bin/userscript
rm -rf $USER_SCRIPT
echo "#!bin/bash" >> $USER_SCRIPT
SCRIPT="bash ${SCRIPT}"
echo $SCRIPT >> $USER_SCRIPT
chmod u+x $USER_SCRIPT

MAIN_SCRIPT=$SHARED_DIR/bin/mainscript
cat > ${MAIN_SCRIPT} << EOF
#!/bin/busybox sh

bash /mnt/share/bin/userscript || echo "run.sh failed"

rm -rf /mnt/share/out
mkdir -p /mnt/share/out
mv results /mnt/share/out
mv perf.data /mnt/share/out
mv schtest.out.txt /mnt/share/out
EOF
chmod u+x $MAIN_SCRIPT

case $MODE in
	exit)
		echo "poweroff -f" >> $MAIN_SCRIPT
		;;
	*)
		echo "/bin/busybox sh" >> $MAIN_SCRIPT
		;;
esac

DIR=$(dirname ${BASH_SOURCE[0]})
$DIR/make-initramfs.sh $INITRAMFS $SHARED_DIR $PERF
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
