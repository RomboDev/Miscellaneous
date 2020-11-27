
This is the LibraryLink.
Better compile it from within Mathematica.
Of course Mathematica is using the default system compiler (tested with GCC 7.3 Ubuntu18.04)
(the shared objects will be here : /home/max/.Mathematica/SystemFiles/LibraryResources/Linux-x86-64)

Needs["CCompilerDriver`"]
randomRealHaltonLib = CreateLibrary[{"yourpath/randomRealHalton.cpp"}, "randomRealHalton", "CompileOptions" -> "-O3"]
RandomRealHalton = LibraryFunctionLoad[randomRealHaltonLib, "RandomRealHalton", {Integer}, Real]

Then use it as usual :
RandomRealHalton[123688]

The cool thing here is that it works great with parallel stuff and performances are almost C/C++ and sometimes better then Mathematica buildin RandomReal (RandomRealHalton, R2 for example). You can check performances with the provided Mathematica notebook : ../../CompiledCC++_SamplersTest.nb

Take care samplers.h from parent folder is needed for compilation.


