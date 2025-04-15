# source environment variables
pushd ../ && source .env && popd

# load the vm image (if not currently loaded)
./load-vmdisk.sh 

# guest Linux version
GUEST_LINUX_VER=$KERNELVERSION

# unlink the build directory (if exists)
sudo unlink $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build || true

# copy the updated build source
sudo mkdir -p $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build
sudo cp -ur linux/* $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build/

# unload the vm image
./unload-vmdisk.sh