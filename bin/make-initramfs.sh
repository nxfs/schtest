#!/bin/bash
# Create a busybox based initramfs. You must provide the path to a
# statically build busybox exe.

# copy executable to host/guest shared dir and copy all dynamic libraries
install_exec() {
	f=$1
	if [ ! -f $f ]; then
		f=$(which $f)
	fi
	mkdir -p $2/bin
	cp $f $2/bin
	for lib in $(ldd $f | cut -d '(' -f 1 | cut -d '>' -f 2 | grep -v vdso); do
		LIBDIR=$(dirname $lib)
		mkdir -p $3/${LIBDIR}
		cp $lib $3/$lib
	done
}

set -eux

INITRAMFS=$1
SHARED_DIR=$2
PERF=$3

BUSYBOX=$(which busybox)
INITRAMFS_DIR=initramfs

# Create a directoy tree for the initramfs. Make sure we start from a
# fresh state.

rm -rf ${INITRAMFS_DIR}
mkdir -p ${INITRAMFS_DIR}/{bin,sbin,etc,proc,sys,dev}
touch ${INITRAMFS_DIR}/etc/mdev.conf

DIR=$(dirname ${BASH_SOURCE[0]})
install_exec ${DIR}/stress ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec ${DIR}/schtest ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec ${DIR}/run-common.sh ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec bash ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec mkdir ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec mount ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec rm ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec dirname ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec $PERF ${SHARED_DIR} ${INITRAMFS_DIR}

# Copy busybox into the right place.
cp -a ${BUSYBOX} ${INITRAMFS_DIR}/bin/busybox

# Create the init script
cat > ${INITRAMFS_DIR}/init << EOF
#!/bin/busybox sh

echo "Running busybox initramfs..."

# Mount the /proc and /sys filesystems.
/bin/busybox mount -t proc none /proc
/bin/busybox mount -t sysfs none /sys
/bin/busybox mount -t debugfs none /sys/kernel/debug
/bin/busybox mount -t devtmpfs none /dev

/bin/busybox mkdir -p /mnt/share
/bin/busybox mount -t 9p -o trans=virtio host_share /mnt/share -oversion=9p2000.L

export PATH=$PATH:/mnt/share/bin
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64

echo "Running run.sh"
bash /mnt/share/bin/run-common.sh -t /mnt/share/bin/stress -d 1 || echo "run.sh failed"

rm -rf /mnt/share/out
mkdir -p /mnt/share/out
mv results /mnt/share/out
mv perf.data /mnt/share/out
mv schtest.out.txt /mnt/share/out

# Drop into the shell
# echo "Entering busybox shell..."
# exec /bin/busybox sh
poweroff -f
EOF

# Make the init file executable
chmod +x ${INITRAMFS_DIR}/init

# Create the cpio archive and cleanup
cd ${INITRAMFS_DIR}
find . | cpio -o -H newc | gzip > ../$1
cd -
rm -rf ${INITRAMFS_DIR}
