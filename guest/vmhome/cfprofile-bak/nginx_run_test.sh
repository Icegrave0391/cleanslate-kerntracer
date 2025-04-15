#!/bin/bash

sudo rm ../functionsInSyscallRaw
sudo rm ../kallsyms
make
sudo insmod cfprofile.ko
# redis-benchmark
# wrk -t10 -c100 -d30s http://127.0.0.1
sudo systemctl restart nginx
for i in $(seq 1 10); do
	ab -k -n 10000 -c 32 http://localhost:80/
done
sudo systemctl restart nginx
for i in $(seq 1 10); do
	ab -n 10000 -c 12 http://127.0.0.1:80/10K.html
done
sudo systemctl restart nginx
sudo rmmod cfprofile
make clean

sudo cp /proc/kallsyms ../kallsyms
