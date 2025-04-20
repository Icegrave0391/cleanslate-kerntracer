#!/bin/bash

# 1. Grab your nginx PIDs
PIDS=$(pgrep -x nginx | tr '\n' ',' | sed 's/,$//')

BUFSIZE_KB=16384

# 2. Record a combined trace:
sudo trace-cmd record \
  -p function_graph \
  -P $PIDS \
  -e syscalls:sys_enter_* \
  -e syscalls:sys_exit_* \
  -b $BUFSIZE_KB \
  -o nginx-syscall-fns.dat

# …wait until you’ve generated enough workload, then press Ctrl‑C to finish…
