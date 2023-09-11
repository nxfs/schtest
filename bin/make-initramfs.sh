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
install_exec ${DIR}/perf-schtest/perf-schtest-common.sh ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec bash ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec mkdir ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec mount ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec rm ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec dirname ${SHARED_DIR} ${INITRAMFS_DIR}
install_exec $PERF ${SHARED_DIR} ${INITRAMFS_DIR}

# Copy busybox into the right place.
cp -a ${BUSYBOX} ${INITRAMFS_DIR}/bin/busybox

# Create the init script
cp ${DIR}/qemu/init ${INITRAMFS_DIR}/init
chmod +x ${INITRAMFS_DIR}/init

# Create the cpio archive and cleanup
cd ${INITRAMFS_DIR}
find . | cpio -o -H newc | gzip > ../$1
cd -
rm -rf ${INITRAMFS_DIR}
