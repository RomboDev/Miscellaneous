Hachisuka-Directional Dipole ported to ISPC.

The ISPC version is around 5x faster than the plain C++ version.
One just needs to download the ispc executable (https://ispc.github.io/downloads.html) and put it in a sys folder path where it can be reached, then run the build sh script to first compile the ispc producing an .h and an .o files that will be used then by C++ to invoke the ispc fnc and by the linker to link the binariy code.
Once compiled the program will first run the C++ version parallelized by OPENMP and eventually the ISPC version that is both parallel across the simd lanes of one processing core and while executing that across multiple cores (tasksys.cpp).
Two images (hopefully identical) will be written down as .pfm.

![cover](https://raw.githubusercontent.com/RomboDev/Miscellaneous/master/ISPC%20Directional%20Dipole/image_dirpole.png)
