#include "WolframLibrary.h"
#include "WolframRTL.h"

EXTERN_C DLLEXPORT mint WolframLibrary_getVersion(){return WolframLibraryVersion;}
EXTERN_C DLLEXPORT int WolframLibrary_initialize( WolframLibraryData libData) {return 0;}
EXTERN_C DLLEXPORT void WolframLibrary_uninitialize( WolframLibraryData libData) {}
EXTERN_C DLLEXPORT int constantzero(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res)
{
     MArgument_setReal(Res, 0.0);
     return LIBRARY_NO_ERROR;
}

#include <string.h> // memcpy
#include "../samplers.h"
using namespace samplers;


EXTERN_C DLLEXPORT int RandomRealLDBN(	WolframLibraryData libData, 
										mint Argc, MArgument *Args, MArgument Res) 
{
	// Error
    int err = LIBRARY_NO_ERROR;
	
	// Args
	const mint isamples = MArgument_getInteger(Args[0]);

	// Setup nb of samples
	const mint samplerow = std::floor(std::sqrt((double)isamples));
	const mint nsamples = pow(samplerow,2);
	const mint nrealslots = 2;

	// Tensor to hold nsamples slots of 2 reals
	MTensor oT;
	const mint oDims[2] = {nsamples, nrealslots};
	const mint oRank = 2;
	err = libData->MTensor_new(MType_Real, oRank, oDims, &oT);
	if(err) return err;

    // Low-Discrepancy BlueNoise
	std::vector<Point> samples;
	initSamplers();
	ldbnBNOT(samplerow, samples);

	// Get the starting address of the data to be filled in
	mreal *datastartingaddress = libData->MTensor_getRealData(oT);

	// Fill in samples data
	memcpy(datastartingaddress, &samples[0], sizeof(double)*nsamples*nrealslots);

	// Same as above
	/*for(int i=0; i < nsamples; i++){
		Point s = samples[i];
		datastartingaddress[i*nrealslots +0] = s[0];
		datastartingaddress[i*nrealslots +1] = s[1];
	}*/

	//[...]

	// Get back to WM
	MArgument_setMTensor(Res, oT);
	return err;
}

/*
Ok, great. I also removed the question about the data pointer as it's already highlighted here with your comments. Will try LTemplate ! Thanks for all. */

/*
// this is the same as above but does it with LibraryLink MTensor_setMTensor stuff
EXTERN_C DLLEXPORT int RandomRealLDBN(	WolframLibraryData libData, 
										mint Argc, MArgument *Args, MArgument Res) 
{
	// Error
    int err = LIBRARY_NO_ERROR;
	
	// Args
	const mint isamples = MArgument_getInteger(Args[0]);

	// Setup nb of samples
	const mint nsamples = std::floor(std::sqrt((double)isamples)) *2;
	const mint nrealslots = 2;

	// Tensor to hold nsamples slots of 2 reals
	MTensor oT;
	mint oDims[2];
	oDims[0] = nsamples;
	oDims[1] = nrealslots;
	const mint oRank = 2;
	err = libData->MTensor_new(MType_Real, oRank, oDims, &oT);
	if(err) return err;

    // Low-Discrepancy BlueNoise
	std::vector<Point> samples;
	initSamplers();
	ldbnBNOT(nsamples, samples);

	// Setup subtensor to hold sample data
	MTensor iT;
	mint iDims[] = {nrealslots};
	const mint iRank = 1;
	const mint nPos = 1;
	mint pos[1];

	// Yep, MTensor is a 1-based array as in WM
	for(int i=1; i<=nsamples; i++)
	{
		Point s = samples[i-1];

		// allocate a tensor to hold the sample
		err = libData->MTensor_new(MType_Real, iRank, iDims, &iT);
		if(err) return err;

		// fill in the sample data
		mreal *sample = libData->MTensor_getRealData(iT);
		sample[0] = s[0]; //copy sample data
		sample[1] = s[1];

		// update index
		pos[0] = i;

		//insert subtensor in our main tensor list at pos
		err = libData->MTensor_setMTensor(oT, iT, pos, nPos);
		if(err) return err;
	}

	// Get back to WM
	MArgument_setMTensor(Res, oT);
	return err;
}
*/

/*
// this is _NOT_ working but if it would that would be the way to do it with WolframCompileLibrary_Functions
EXTERN_C DLLEXPORT int RandomRealLDBN(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) 
{
	// Error
    int err = LIBRARY_NO_ERROR;
	
	// Args
	const mint isamples = MArgument_getInteger(Args[0]);

	// Setup nb of samples
	const mint nsamples = std::floor(std::sqrt((double)isamples)) *2;
	const mint nrealslots = 2;
	const mint iotype = MType_Real;


	static WolframCompileLibrary_Functions structCompile;
	structCompile = libData->compileLibraryFunctions;

    MTensorInitializationData Tinit;
	Tinit = structCompile->GetInitializedMTensors(libData, 2);

	// main tensor list to hold nsamples slots of 2 reals
	MTensor* oT;
    oT = MTensorInitializationData_getTensor(Tinit, 0);
	mint oDims[2];
	oDims[0] = nsamples;
	oDims[1] = nrealslots;
	const mint orank = 2;
	err = structCompile->MTensor_allocate(oT, iotype, orank, oDims);
	if(err) return err;

    // low discrepancy Blue noise
	std::vector<Point> samples;
	initSamplers();
	ldbnBNOT(nsamples, samples);


	//for(auto &s: samples)
	for(int i=1; i<=nsamples; i++)
	{
		Point s = samples[i];

		// allocate a tensor to hold the sample
		MTensor* iT;
    	iT = MTensorInitializationData_getTensor(Tinit, 1);
		mint iDims[1] = {2};
		const mint irank = 2;
		err = structCompile->MTensor_allocate(iT, iotype, irank, iDims);
		// fill *it with sample data
		mreal *sample = MTensor_getRealDataMacro(*iT);
		sample[0] = s[0];
		sample[1] = s[1];

		//insert it in our main tensor list
		err = structCompile->MTensor_insertMTensor(*oT, *iT, oDims); 	// inconsistent dims or exceeding array bounds !
		if(err) return err;												// not sure if oDims is the right thing there
		oT++; // advance to the next slot
	}


	// just for test //
	MTensor out_MT;
	const mint out_dim[]={nsamples};
	mint out_rank=1;

	err = libData->MTensor_new(MType_Real, out_rank, out_dim, &out_MT);
	mreal *out_cpointer = libData->MTensor_getRealData(out_MT);

    for(int i=0;i<nsamples;i++)
	    out_cpointer[i] = 0.001;

	MArgument_setMTensor(Res,out_MT);
	return err;
}
*/