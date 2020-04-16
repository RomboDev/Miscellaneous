#!/bin/sh

# cd into this directory and then 
# ./_compileANDrun.sh

# Compilation
#c++ -c -std=c++11 -O3 MRperm.cpp
#c++ MRperm.o -o appcpp_perm
c++ -std=c++11 -O3 MRperm.cpp -o appcpp_perm

#echo "$(tput setaf 3)Compilatin done ! "
echo "Compilatin done ! "

# start application
./appcpp_perm

# C program
#cc MRperm.c -o appc_perm
