#!/bin/sh

# cd into this directory and then 
# ./_compileANDrun.sh

# ISPC compilation
#ispc --wno-perf --woff --pic --addressing=32 -O3 ispc_perm.ispc -h ispc_perm.ispc.h -o ispc_perm.ispc.o
ispc --target=avx2-i32x8 --addressing=32 -O3 --pic ispc_perm.ispc -h ispc_perm.ispc.h -o ispc_perm.ispc.o
c++ -c -std=c++11 -O3 ispc_run.cpp
c++ ispc_run.o ispc_perm.ispc.o -o appispc_perm

# start application
./appispc_perm


#cc MRperm.c -o appc_perm
