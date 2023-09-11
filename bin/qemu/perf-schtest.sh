#!/bin/busybox sh

rm -rf /mnt/share/out
mkdir -p /mnt/share/out
mv results /mnt/share/out
mv perf.data /mnt/share/out
mv schtest.out.txt /mnt/share/out

./post_init || echo "post init script failed"
echo "Entering busybox shell..."
exec /bin/busybox sh
