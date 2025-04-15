#!/bin/bash -e

# https://blog.programster.org/create-ubuntu-20-kvm-guest-from-cloud-image

echo "--------------------------------------------------------------------------------"
echo "Please install pre-requisites before running this script."
echo "--------------------------------------------------------------------------------"

sudo apt install cloud-image-utils qemu-kvm libvirt-daemon-system libvirt-clients bridge-utils virtinst

# Get environment variables
source .env

# Get a cloud image (ubuntu 22.04 LTS) from Ubuntu servers
if [ ! -f $VMDISK ]; then 
	wget https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img

	# Move the cloud image into a new folder (for clean setup)
	mkdir -p $VMDISKFOLDER
	mv jammy-server-cloudimg-amd64.img $VMDISK

	# Resize the qcow2 disk to 50G (just to have enough space)
	sudo qemu-img resize $VMDISK 50G
fi

# Generate a custom configuration file
sudo echo "#cloud-config
system_info:
  default_user:
    name: $USERNAME
    home: /home/$USERNAME

password: $PASSWORD
chpasswd: { expire: False }
hostname: $VMNAME

# configure sshd to allow users logging in using password
# rather than just keys
ssh_pwauth: True
" | sudo tee ubuntu-vm-init.cfg

# Create an ISO file from the configuration file
sudo cloud-localds ubuntu-vm-init.iso ubuntu-vm-init.cfg

# Destroy domain and name if exists (for later reinstalls)
sudo virsh destroy $VMNAME || true
sudo virsh undefine $VMNAME || true

# Install the image file
sudo virt-install \
  --name $VMNAME \
  --memory 1024 \
  --disk $VMDISK,device=disk,bus=virtio \
  --disk ubuntu-vm-init.iso,device=cdrom \
  --os-type linux \
  --os-variant ubuntu20.04 \
  --virt-type kvm \
  --graphics none \
  --network network=default,model=virtio \
  --import

# Destroy domain and name if exists
sudo virsh destroy $VMNAME || true
sudo virsh undefine $VMNAME || true

# Cleanup and remove everything that is not needed
rm -rf *.iso *.cfg *.img*

# Run bootstrap commands
./bootstrap.sh
