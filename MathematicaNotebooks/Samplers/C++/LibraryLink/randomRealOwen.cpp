#include "WolframLibrary.h"

EXTERN_C DLLEXPORT mint WolframLibrary_getVersion(){return WolframLibraryVersion;}
EXTERN_C DLLEXPORT int WolframLibrary_initialize( WolframLibraryData libData) {return 0;}
EXTERN_C DLLEXPORT void WolframLibrary_uninitialize( WolframLibraryData libData) {}
EXTERN_C DLLEXPORT int constantzero(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res)
{
     MArgument_setReal(Res, 0.0);
     return LIBRARY_NO_ERROR;
}


#include "../samplers.h"
using namespace samplers;

EXTERN_C DLLEXPORT int RandomRealOwen (WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) 
{
  	mint I0;
  	mreal I1;
  	I0 = MArgument_getInteger(Args[0]);
    
  	uint ipx,ipy;
	Sobol(I0, ipx, ipy);
    I1 = OwenScramble(ipx,42u) 	/ float(0xFFFFFFFFu);

  	MArgument_setReal(Res, I1);
  	return LIBRARY_NO_ERROR;
}

EXTERN_C DLLEXPORT int RandomRealOwen2D (WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) 
{
	// error
    int err = LIBRARY_NO_ERROR;

	// input
  	int s = MArgument_getInteger(Args[0]);

	// rig output
	MTensor out_MT;
	const mint out_dim[]={2};
	mint out_rank = 1;

	// setup data
	err = libData->MTensor_new(MType_Real, out_rank, out_dim, &out_MT);
	mreal *out_cpointer = libData->MTensor_getRealData(out_MT);

	// sampler
	uint ipx,ipy;
	Sobol(s, ipx, ipy);
	out_cpointer[0] = OwenScramble(ipx,42u)  / (double)0xFFFFFFFFu;
	out_cpointer[1] = OwenScramble(ipy,666u) / (double)0xFFFFFFFFu;
    
	// return with a list of two pointsZ
  	MArgument_setMTensor(Res, out_MT);
  	return LIBRARY_NO_ERROR;
}
