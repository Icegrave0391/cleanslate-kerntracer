#!/usr/bin/env bash

set -e -x

target_prog=$1
file_prefix=$2

start_time=$SECONDS

cwd=`pwd`

cmd_prefix=

euid=`id -u`
if [ "$euid" -ne 0 ]; then  # not root
    cmd_prefix='sudo '
fi

${cmd_prefix}rm -f $file_prefix-unshare-out.log target.pid target.pgid ns.pid

if [ -d "./tmp" ]; then
    ${cmd_prefix}rm -rf ./tmp
fi
mkdir tmp/
cd tmp/
mkdir target newtarget

pushd target
cp "$target_prog" .
cp "$target_prog" ../newtarget/
popd

${cmd_prefix}unshare --propagation private -m bash -c "mount --bind $PWD/target ./newtarget; ./newtarget/a.out" > $cwd/$file_prefix-unshare-out.log 2>&1 &
orig_child_pid=$!

target_pid=

for i in {1..300}; do
    sleep 0.01
    #pstree "$orig_child_pid" |grep a.out|tr '\---' "\n" || ( exit 0 )
    line=`pstree -p "$orig_child_pid"|grep 'a\.out' || ( exit 0 )`
    if [ -n "$line" ]; then
        target_pid=`echo $line|sed 's/---/\n/g'|grep 'a\.out'|sed 's/^a\.out(\|)$//g'`
        break
    fi
done

if [ -z "$target_pid" ]; then
    ps aux|grep bash
    echo "No target pid found for a.out" > /dev/stderr
    exit 1
fi

echo -n $target_pid > $cwd/target.pid
echo -n $target_pid > $cwd/ns.pid

echo "Elapsed $((SECONDS - start_time)) seconds."
time wait $orig_child_pid

