#!/bin/bash

sudo rm ../functionsInSyscallRaw
sudo rm ../kallsyms
make
sudo insmod cfprofile.ko
sudo systemctl restart memcached
for i in $(seq 1 10); do
	memcslap -s 127.0.0.1:11211
done
sudo rmmod cfprofile
make clean

sudo cp /proc/kallsyms ../kallsyms
