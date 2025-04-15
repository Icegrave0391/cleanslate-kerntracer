#!/bin/bash

sudo rm ../functionsInSyscallRaw
sudo rm ../kallsyms
make
sudo insmod cfprofile.ko

echo n | phoronix-test-suite run pts/compress-7zip

sudo rmmod cfprofile
make clean

sudo cp /proc/kallsyms ../kallsyms
