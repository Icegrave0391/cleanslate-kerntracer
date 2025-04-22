#!/bin/bash

CPUS=$(nproc --all)

if [ $# -ne 2 ]; then
    echo "Usage: $0 <trace-file.dat> <program-name>"
    exit
fi

DAT_FILE=$1
PROG=$2

# extract input .dat file name
echo "reporting $DAT_FILE..."
sudo trace-cmd report \
    -i $DAT_FILE \
    > report.txt

# differentiate report into different threads
rm report-cpu*.txt
python3 ./report-to-pid.py $PROG

# delete raw report.txt file
rm report.txt