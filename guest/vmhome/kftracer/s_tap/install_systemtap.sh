#!/bin/bash

PREFIX=/usr/local/bin/

pushd systemtap
    make clean
    ./configure --prefix=$PREFIX --disable-nls
    make -j$(nproc)
    sudo make install
popd
