#include <stddef.h>
#include "samplers.h"
#include "utils.hpp"

using namespace samplers;

//////////
int main()
{
	/* // test Mathematica forloop vs C/C++
	aux::Timer timer_whole(true);
	auto sm = 0;
	for (int i = 0; i < 1000000; i++)
	sm+=1;
	std::cout << sm << std::endl;
	auto elapsed = timer_whole.Elapsed();
	std::cout << "\nForLoop took " << (elapsed.count()*0.001) << " seconds" << std::endl;
	*/

	
	// Correlated Multi-Jitter
	/*
	int x = 10, y = 10;
	uint p = hash_int_2d<uint>(x,y);
	std::cout << "hashed_uint_p: " << p << std::endl;

	float fx,fy;
	float rarr[2];
	int s=12, N=8193;
	cmj_sample_2D(s, N, p, &rarr[0], &rarr[1]);
	//fx=cmj_sample_1D(s, N, p); fy=0.f;
	std::cout << "cmj_fx= " << rarr[0] << ", cmj_fy=" << rarr[1] << std::endl;
	//std::cout << "cmj_fx= " << fx << ", cmj_fy=" << fy << std::endl;
	std::cout << "-------------" << std::endl;
	*/

	//std::cout << "nextpow:" << next_pow2(8192) << std::endl;
	//std::cout << lowBias32Hash(12345) << std::endl;


	// Halton
	//std::cout << hal(2,10000) << std::endl;
	//std::cout << halton2(1234) << std::endl; //slow
	// test halton perm based vs pbrt based
	/*
	aux::Timer timer_whole(true);
	float u1,u2,res;
	for(int i = 0; i<10000000;i++){
		//getRandomHalton2D(i,u1,u2);
		//res+=u1+u2;
		res += hal(1,i)+hal(2,i);
	}
	auto elapsed = timer_whole.Elapsed(); //end time recording
	std::cout << res << ", " << (elapsed.count()*0.001) << " seconds" << std::endl;
	*/


	// Jittered R2
	/*
	float f0,f1;
	getR2Sample2D(12345, 1.f, &f0, &f1);
	std::cout << "r2_r0= " << f0 << ", r2_f1= " << f1 << std::endl;
	std::cout << getR2Sample(12345) << std::endl;
	*/


	// Owen Scrambled Sobol
	/*
	uint ipx,ipy;
	Sobol(12, ipx, ipy);
	std::cout << "oss_ipx= " << OwenScramble(ipx,42u)/ (double)0xFFFFFFFFu << std::endl;
	std::cout << "oss_ipy= " << OwenScramble(ipy,666u)/ (double)0xFFFFFFFFu << std::endl;
	*/


	// Low discrepancy Blue noise
	/*
	// this is how WMathematica LibraryLink may work internally and instead it does not :)
	// which in turn it means we have to copy data instead to just let it point to 
	struct st_MDataArray {
		double prec;
		int *dims;
		int rank;
		int nelems;
		void *data;
	};

	// Low discrepancy Blue noise
	const int nrealslots = 2;
	const int nsamples = 4*4;
	const int samplerow = std::floor(std::sqrt((double) nsamples));

	std::vector<Point> samples;
	initSamplers();
	ldbnBNOT(samplerow, samples);

	// Point our samples to data
	st_MDataArray dSamples;
	dSamples.data = &samples[0];

	// double *hsamples;
	// hsamples = (double*) &samples[0];

	for (size_t i = 0; i < nsamples*nrealslots; i++){
		std::cout<< ((double*)(dSamples.data))[i]<<" "<< ((double*)(dSamples.data))[++i]<<std::endl;
		//std::cout<< hsamples[i]<<" "<< hsamples[++i]<<std::endl;
	}
	*/

	const int nrealslots = 2;
	const int nsamples = 4*4;
	const int samplerow = std::floor(std::sqrt((double) nsamples));

	std::vector<Point> samples;
	initSamplers();
	ldbnBNOT(samplerow, samples);

	std::cout<<"---------"<<std::endl;
	for(auto &s: samples)
    	std::cout<< s[0]<<" "<< s[1]<<std::endl;

	return 0;
}