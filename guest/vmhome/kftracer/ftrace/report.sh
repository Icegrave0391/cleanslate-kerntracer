#!/bin/bash

CPUS=$(nproc --all)

if [ $# -ne 1 ]; then
    echo "Usage: $0 <trace-file.dat>"
    exit
fi

# extract input .dat file name
echo "reporting $1..."
sudo trace-cmd report \
    -i $1 \
    > report.txt
