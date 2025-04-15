#!/bin/bash -e

# Get environment variables
source .env

# Initial information
echo "----------------------------------"
echo "Function: This script restarts a stopped virtual machine"
echo ""
echo "  1. Mapping CTRL-C to CTRL-]"
echo "  2. Press CTRL-] to stop the VM"
echo "----------------------------------"
echo ""

# Disable huge pages
sudo sh -c "echo never > /sys/kernel/mm/transparent_hugepage/enabled"
sudo sh -c "echo never > /sys/kernel/mm/transparent_hugepage/defrag"

if test -d "vmhome"; then
	./copy-to-vm.sh
	sleep 0.5
fi

# Map CTRL-C to CTRL-]
stty intr ^]

# launch the QEMU VM
# qemu-system-x86_64 -s -cpu host,intel-pt=on -enable-kvm -smp 1,maxcpus=24\
# taskset -c 3 nice -n -19 ../local-qemu/qemu-8.1.0/build/qemu-system-x86_64 -s -cpu host,intel-pt=on -enable-kvm -smp 1,maxcpus=24\
# qemu-system-x86_64 -s -cpu host,intel-pt=on -enable-kvm -smp 1,maxcpus=24 -m 4096M -no-reboot -netdev user,id=vmnic,hostfwd=tcp::8000-:22,hostfwd=tcp::9000-:80,hostfwd=tcp::10000-:11211,hostfwd=tcp::11000-:6379 -device e1000,netdev=vmnic,romfile= -drive file=$VMDISK,if=none,id=disk0,format=qcow2 -device virtio-scsi-pci,id=scsi0,disable-legacy=on,iommu_platform=true \
# qemu-system-x86_64 -s -cpu host,intel-pt=on -enable-kvm -smp 2,maxcpus=24 -m 16384M -no-reboot -netdev user,id=vmnic,hostfwd=tcp::8000-:22,hostfwd=tcp::9000-:80,hostfwd=tcp::10000-:11211,hostfwd=tcp::11000-:6379 -device e1000,netdev=vmnic,romfile= -drive file=$VMDISK,if=none,id=disk0,format=qcow2 -device virtio-scsi-pci,id=scsi0,disable-legacy=on,iommu_platform=true \
# qemu-system-x86_64 -s -cpu host,intel-pt=on -enable-kvm -smp 1,maxcpus=24 -m 32768M -no-reboot -netdev user,id=vmnic,hostfwd=tcp::8000-:22,hostfwd=tcp::9000-:80,hostfwd=tcp::10000-:11211,hostfwd=tcp::11000-:6379 -device e1000,netdev=vmnic,romfile= -drive file=$VMDISK,if=none,id=disk0,format=qcow2 -device virtio-scsi-pci,id=scsi0,disable-legacy=on,iommu_platform=true \

QEMU="../local-qemu/qemu-8.1.0/build/qemu-system-x86_64"
# taskset -c 0,1,2,3 $QEMU -s -cpu host,intel-pt=on -enable-kvm -smp 1,maxcpus=24 -m 4096M -mem-prealloc -no-reboot -netdev user,id=vmnic,hostfwd=tcp::8000-:22,hostfwd=tcp::9000-:80,hostfwd=tcp::10000-:11211,hostfwd=tcp::11000-:6379 -device e1000,netdev=vmnic,romfile= -drive file=$VMDISK,if=none,id=disk0,format=qcow2 -device virtio-scsi-pci,id=scsi0,disable-legacy=on,iommu_platform=true \
$QEMU -s -cpu host,intel-pt=on -enable-kvm -smp 8,maxcpus=24 -m 8000M -mem-prealloc -no-reboot -netdev user,id=vmnic,hostfwd=tcp::8000-:22,hostfwd=tcp::9000-:80,hostfwd=tcp::10000-:11211,hostfwd=tcp::11000-:6379 -device e1000,netdev=vmnic,romfile= -drive file=$VMDISK,if=none,id=disk0,format=qcow2 -device virtio-scsi-pci,id=scsi0,disable-legacy=on,iommu_platform=true \
	-device scsi-hd,drive=disk0 -nographic -monitor pty -monitor unix:monitor,server,nowait

# restore the mapping
stty intr ^c

sleep 0.5
./copy-from-vm.sh
