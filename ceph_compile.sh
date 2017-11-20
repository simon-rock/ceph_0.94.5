#!/bin/sh
cd ceph-10.2.7
./configure --prefix=/home/yu/workspace/githome/ceph_sp/out \
        --with-nss \
        --without-cryptopp \
        --with-debug \
                --without-tcmalloc \
                --with-librocksdb-static=check \
                --with-radosgw \
            --without-lttng \
                --without-babeltrace
#               --with-jemalloc
cd ..
