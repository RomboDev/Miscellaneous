
Original idea from Martin Roberts ...
http://extremelearning.com.au/isotropic-blue-noise-point-sets/

Reference Java implementation from Tommy Ettinger ...
https://github.com/tommyettinger/sarong/blob/master/src/test/java/sarong/PermutationEtc.java


There're three versions of the permutation code:
- C++11, one can switch between using Lambdas vs goto and between C-arrays vs STD-arrays
- C version for prototyping the following ISPC code
- ISPC code based on the C code. It's actually 3x slower as I didn't spend time optimizing it.

C/C++ code is around 30% faster than the Java implementation. 
I've added clocks (C++ version) to time single sqs, average and max time spent permutating.

On Linux simply run.. _acompileANDarun.sh
For ISPC simply download the ISPC exec put it in a sys path and then run.. ispc_compileANDarun.sh

All three version of the executable are directly available eventually : 
- appcpp_perm
- appc_perm
- appispc_perm
