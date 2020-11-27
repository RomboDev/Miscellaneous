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


EXTERN_C DLLEXPORT int RandomRealLDBN(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) 
{
	const mint nsamples = MArgument_getInteger(Args[0]);

	MTensor out_MT;
	const mint out_dim[]={nsamples};
	mint out_rank=1;

	int err = libData->MTensor_new(MType_Real, out_rank, out_dim, &out_MT);
	mreal *out_cpointer = libData->MTensor_getRealData(out_MT);

    // Low discrepancy Blue noise
	/*std::vector<Point> samples;
	initSamplers();
	ldbnBNOT(2048, samples);

	for(auto &s: samples)
    	std::cout<< s[0]<<" "<< s[1]<<std::endl;*/

    for(int i=0;i<nsamples;i++)
	    out_cpointer[i] = 0.001;

	MArgument_setMTensor(Res,out_MT);
	return LIBRARY_NO_ERROR;
}


/*EXTERN_C DLLEXPORT int RandomRealLDBN(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) 
{
	const mint nsamples = MArgument_getInteger(Args[0]);

	MTensor out_MT;
	const mint out_dim[]={2};
	mint out_rank=1;

	int err = libData->MTensor_new(MType_Real, out_rank, out_dim, &out_MT);
	mreal *out_cpointer = libData->MTensor_getRealData(out_MT);

    for(int i=0;i<2;i++)
	    out_cpointer[i] = 0.001;

	MArgument_setMTensor(Res,out_MT);
	return LIBRARY_NO_ERROR;
}*/