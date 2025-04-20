#!/bin/bash

CPUS=$(nproc --all)

if [ $# -ne 1 ]; then
    echo "Usage: $0 <trace-file.dat>"
    exit
fi

for cpu in $(seq 0 $((CPUS - 1))); do
    sudo trace-cmd report \
        -i $1 \
        --cpu $cpu \
        > report-cpu${cpu}.txt
done
