#!/bin/bash

sudo rm ../functionsInSyscallRaw
sudo rm ../kallsyms
make
sudo insmod cfprofile.ko

echo n | phoronix-test-suite run system/octave-benchmark-1.0.1

sudo rmmod cfprofile
make clean

sudo cp /proc/kallsyms ../kallsyms
