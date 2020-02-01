Hachisuka-Directional Dipole (for Subsurface Scattering) implemented with ISPC.

The ISPC (Intel® SPMD Program Compiler) version is around 5x faster than the plain C++ version.
One just needs to download the ISPC executable (https://ispc.github.io/downloads.html) and put it in a sys folder path, then run the sh build script to first compile the ISPC producing an .h and an .o files that will be used then by C++ to invoke the ISPC fnc and by the linker to link the binary code.
Once compiled the program will first run the C++ version parallelized by OPENMP and eventually the ISPC version that is both parallel across the SIMD lanes of one processing core while executing that across multiple cores (tasksys.cpp).
Two images (hopefully identical) will be written down as .pfm.

![cover](https://raw.githubusercontent.com/RomboDev/Miscellaneous/master/ISPC%20Directional%20Dipole/image_dirpole.png)
