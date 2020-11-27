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

EXTERN_C DLLEXPORT int RandomRealCMJ (WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) 
{
  	mint I0;
  	mreal I1;
  	I0 = MArgument_getInteger(Args[0]);
    
	mint x = 10, y = 10;
	uint p = hash_int_2d<uint>(x,y);
    
    mint N = next_pow2(I0)+1;
    I1 = cmj_sample_1D(I0, N, p);

  	MArgument_setReal(Res, I1);
  	return LIBRARY_NO_ERROR;
}

EXTERN_C DLLEXPORT int RandomRealCMJ2D (WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) 
{
	// error
    int err = LIBRARY_NO_ERROR;

	// input
  	int s = MArgument_getInteger(Args[0]);
  	int N = MArgument_getInteger(Args[1]);
	if(N<s) N = next_pow2(s)+1;
	else 	N = next_pow2(N)+1;

	// setup sampler
	int x = 10, y = 10;
	uint p = hash_int_2d<uint>(x,y);

	// rig output
	MTensor out_MT;
	const mint out_dim[]={2};
	mint out_rank = 1;

	// setup data
	err = libData->MTensor_new(MType_Real, out_rank, out_dim, &out_MT);
	mreal *out_cpointer = libData->MTensor_getRealData(out_MT);

	// do calculations
	cmj_sample_2D(s, N, p, &out_cpointer[0], &out_cpointer[1]);

	// return with a list of two pointsZ
  	MArgument_setMTensor(Res, out_MT);
  	return err;
}