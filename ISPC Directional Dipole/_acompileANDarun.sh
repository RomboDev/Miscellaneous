#!/bin/sh

# cd into this directory and then 
# ./_compileANDrun.sh

# ISPC compilation
echo "$(tput setaf 3)Compiling ISPC .. "
ispc --wno-perf --woff --pic file_dirpole.ispc -h file_dirpole.ispc.h -o file_dirpole.ispc.o

# only OPENMP C++ code implementation ...
#c++ -std=c++11 -O3 file_dirpole.cpp -fopenmp -o app_dirpole

# put the whole together .. ie. access ISPC from C++ apz
c++ -c -std=c++11 -O3 -fopenmp file_dirpole.cpp tasksys.cpp
c++ file_dirpole.o file_dirpole.ispc.o tasksys.o -lpthread -fopenmp -o app_dirpole

echo "and C++ .. done ! $(tput sgr 0)"

# start application
./app_dirpole
