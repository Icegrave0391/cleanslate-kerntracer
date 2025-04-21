#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 <server-program-name> <output-file.dat>"
    exit
fi

BUFSIZE_KB=16384

# 1. Grab your nginx PIDs
PROC_NAME=$1
PIDS=$(pgrep -x $PROC_NAME | tr '\n' ',' | sed 's/,$//')

# 2. Record a combined trace:
sudo trace-cmd record \
  -p function_graph \
  -P $PIDS \
  -e syscalls:sys_enter_* \
  -e syscalls:sys_exit_* \
  -b $BUFSIZE_KB \
  -O function_fork \
  -o $2

# …wait until you’ve generated enough workload, then press Ctrl‑C to finish…
