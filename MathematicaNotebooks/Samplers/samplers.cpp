#include <stddef.h>
#include <bitset>
#include "utils.hpp"
#include <math.h>
#include <assert.h>

typedef unsigned int uint;


/////////////////////////////
// Correlated Multi-Jitter //
/////////////////////////////
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

template<typename T>
static inline T hash_int_2d(const uint kx, const uint ky)
{
	T a, b, c;

	a = b = c = 0xdeadbeef + (2 << 2) + 13;
	a += kx;
	b += ky;
  
	;
	c ^= b; 
	T cc =rot(b,14);
	c -= cc;
	a ^= c; a -= rot(c,11);
	b ^= a; b -= rot(a,25);
	c ^= b; c -= rot(b,16);
	a ^= c; 
	T aa = rot(c,4);
	a -= aa;
	b ^= a; b -= rot(a,14);
	c ^= b; c -= rot(b,24);

	return (T)c;
};

static inline
int float_to_int(float f){ return (int)f; }

static inline 
int cmj_isqrt(int value){ float_to_int(sqrtf(value) + 1e-6f); }

static inline 
int cmj_fast_mod_pow2(int a, int b){ return (a & (b - 1)); }

static inline 
uint cmj_hash(uint i, uint p)
{
	i ^= p;
	i ^= i >> 17;
	i ^= i >> 10;
	i *= 0xb36534e5;
	i ^= i >> 12;
	i ^= i >> 21;
	i *= 0x93fc4795;
	i ^= 0xdf6e307f;
	i ^= i >> 17;
	i *= 1 | p >> 18;

	return i;
}

static inline 
uint cmj_hash_simple(uint i, uint p)
{
	i = (i ^ 61) ^ p;
	i += i << 3;
	i ^= i >> 4;
	i *= 0x27d4eb2d;
	return i;
}

static inline 
float cmj_randfloat(uint i, uint p)
{
	return cmj_hash(i, p) * (1.0f / 4294967808.0f);
}

static inline 
bool cmj_is_pow2(int i)
{
	return (i > 1) && ((i & (i - 1)) == 0);
}

static inline 
uint cmj_w_mask(uint w)
{	
	w |= w >> 1;
	w |= w >> 2;
	w |= w >> 4;
	w |= w >> 8;
	w |= w >> 16;

	return w;
	//return ((1 << (32 - __builtin_clz(w))) - 1);
}

//
static inline uint cmj_permute(uint i, uint l, uint p)
{
	uint w = l - 1;

	if((l & w) == 0) {
		/* l is a power of two (fast) */
		i ^= p;
		i *= 0xe170893d;
		i ^= p >> 16;
		i ^= (i & w) >> 4;
		i ^= p >> 8;
		i *= 0x0929eb3f;
		i ^= p >> 23;
		i ^= (i & w) >> 1;
		i *= 1 | p >> 27;
		i *= 0x6935fa69;
		i ^= (i & w) >> 11;
		i *= 0x74dcb303;
		i ^= (i & w) >> 2;
		i *= 0x9e501cc3;
		i ^= (i & w) >> 2;
		i *= 0xc860a3df;
		i &= w;
		i ^= i >> 5;

		const uint ipw=(i + p) & w;
		return ipw;
	}
	else {
		/* l is not a power of two (slow) */
		w = cmj_w_mask(w);
		do {
			i ^= p;
			i *= 0xe170893d;
			i ^= p >> 16;
			i ^= (i & w) >> 4;
			i ^= p >> 8;
			i *= 0x0929eb3f;
			i ^= p >> 23;
			i ^= (i & w) >> 1;
			i *= 1 | p >> 27;
			i *= 0x6935fa69;
			i ^= (i & w) >> 11;
			i *= 0x74dcb303;
			i ^= (i & w) >> 2;
			i *= 0x9e501cc3;
			i ^= (i & w) >> 2;
			i *= 0xc860a3df;
			i &= w;
			i ^= i >> 5;
		} while(i >= l);

		const uint ipl=(i + p) % l;
		return (i + p) % l;
	}
}

static inline
void cmj_sample_2D(int s, int N, int p, float *fx, float *fy)
{
	assert(s < N);

	int m = cmj_isqrt(N);
	int n = (N - 1)/m + 1;
	float invN = 1.0f/N;
	float invm = 1.0f/m;
	float invn = 1.0f/n;

	uint ps = p * 0x51633e2d;
	s = cmj_permute(s, N, ps);

	int sdivm, smodm;

	if(cmj_is_pow2(m)) {
		//sdivm = cmj_fast_div_pow2(s, m);
		sdivm = s/m;
		smodm = cmj_fast_mod_pow2(s, m);
	}
	else {
		// Doing s*inmv gives precision issues here
		sdivm = s / m;
		smodm = s - sdivm*m;
	}

	uint psx = p * 0x68bc21eb;
	uint psy = p * 0x02e5be93;
	uint sx = cmj_permute(smodm, m, psx);
	uint sy = cmj_permute(sdivm, n, psy);

	uint pjx = p * 0x967a889b;
	uint pjy = p * 0x368cc8b7;
	float jx = cmj_randfloat(s, pjx);
	float jy = cmj_randfloat(s, pjy);

	*fx = (sx + (sy + jx)*invn)*invm;
	*fy = (s + jy)*invN;
}

// const int p = Sampling::CSOBOL::hash_int_2d(state->x, state->y) ^ (state->si + state->bounces + offset);
// Sampling::CMJ::cmj_sample_2D(state->si, totsamples, p, &para[0], &para[1]);
//                               ^^^^^^     ^^^^^^^    ^
//                                 s           N       p



////////////
// Halton //
////////////
typedef float Double;
inline unsigned int next_rand(unsigned int current) {return current * 3125u + 49u;}
const Double rand_range = 4294967296.0;

// Halton sequence with reverse permutation
const int primes[20]={2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71};
inline int rev(const int i, const int p) {return (i == 0) ?  i : p - i;}
inline Double hal(const int b, int j) 
{
	const int p = primes[b]; 
	Double h = 0.0;
	Double f = 1.0 / (Double)p;
	Double fct = f;
	while (j > 0) {
		h += rev(j % p, p) * fct; 
		j /= p; 
		fct *= f;
	} 
	return h;
}
// Computes the entry in the base 2 Halton sequence at the specified index.
// Based on PBRT at https://github.com/mmp/pbrt-v3/blob/master/src/core/lowdiscrepancy.h.
inline float halton2(uint32_t index)
{
    index = (index << 16) | (index >> 16);
    index = ((index & 0x00ff00ff) << 8) | ((index & 0xff00ff00) >> 8);
    index = ((index & 0x0f0f0f0f) << 4) | ((index & 0xf0f0f0f0) >> 4);
    index = ((index & 0x33333333) << 2) | ((index & 0xcccccccc) >> 2);
    index = ((index & 0x55555555) << 1) | ((index & 0xaaaaaaaa) >> 1);

	const uint64_t ull=(1ull << 32);
    return index * 1.0f / ull;
}
// Computes the entry in the base 3 Halton sequence at the specified index.
inline float halton3(uint32_t index)
{
    float result = 0.0f;
    float scale = 1.0f;
    while (index != 0)
    {
        scale /= 3;
		u_int32_t xx = index%3;
        result += xx * scale;
        index /= 3;
    }

    return result;
}
//This looks nicer than the Halton based on perms and it 3x faster 
inline void getRandomHalton2D(float& u1, float& u2, uint32_t index)
{
    u1 = halton2(index);
    u2 = halton3(index);
}
// unsigned int xi = next_rand(index);
// if ((xi / rand_range) < accept_prob) 
// sample(i, Vec(hal(2, index), hal(3, index), 0.0), sp, sn);



/////////////////
// Jittered R2 //
/////////////////
const float phi = 1.324717957244746;
const float pi = 3.14159265359;
const float delta0 = 0.76;
const float i0 = 0.700;
const float alpha0 = 1.0/phi;
const float alpha1 = 1.0/phi/phi;

float hash( uint x, uint y)
{
    uint qx = 1103515245U * ( (x>>1U) ^ (x) );
    uint qy = 1103515245U * ( (y>>1U) ^ (y) );
    uint  n = 1103515245U * ( (qx  ) ^ (qy>>3U) );
	const float hsh = float(n) * (1.0/float(0xffffffffU));
    return hsh;
}
float fmod(float x)
{
 	return x - floor(x);   
}
void getSample2D(int i, float lambda, float&r0, float&r1)
{
    float u0 = hash(i, 0);
	float u1 = hash(i, 1)-0.5;

	r0 = fmod(alpha0 * float(i) + lambda * delta0 * sqrt(pi) / (4.0 * sqrt(float(i) - i0)) * u0);
	r1 = fmod(alpha1 * float(i) + lambda * delta0 * sqrt(pi) / (4.0 * sqrt(float(i) - i0)) * u1);
}



//////////////////////////
// Owen Scrambled Sobol //
//////////////////////////
void Sobol(uint n, uint&px, uint&py) {
    px = 0u;
    py = 0u;
    uint dx = 0x80000000u;
    uint dy = 0x80000000u;

    /*for(; n != 0u; n >>= 1u) {
        if((n & 1u) != 0u){
            px ^= dx;
			py ^= dy;
		}
        dx >>= 1u; 		// 1st dimension Sobol matrix, same as base 2 Van der Corput
        dy ^= dy >> 1u;	// 2nd dimension Sobol matrix
		std::cout << "nnngn" << std::endl;
    }*/
	while(n != 0u){
		uint cc= (n & 1u);
        if(cc != 0u){
            px ^= dx;
			py ^= dy;
		}
        dx >>= 1u; 		// 1st dimension Sobol matrix, same as base 2 Van der Corput
        dy ^= dy >> 1u;	// 2nd dimension Sobol matrix

		n >>= 1u;		//update n
    }
}

uint LCG1(uint n) {
    return (n * 1664525u) + 1013904223u; 	// from Numerical Recipes
}
uint LCG2(uint n) {
    return (n * 1103515245u) + 12345u; 		// from glibc
}

uint OwenScramble(uint p, uint s) {
    s = LCG2(s);
    
    for(uint i = 1u << 31u; i > 0u; i >>= 1u) {
        if(s > 0x80000000u)
            p ^= i;
        
        if((p & i) == 0u)
            s = LCG1(s);
        else
            s = LCG2(s);
    }
    
    return p;
}



//////////////////
// Hashing      //
//////////////////
uint32_t wangHash(uint32_t x)
{
    x = (x ^ 61) ^ (x >> 16);
    x *= 9;
    x ^= x >> 4;
    x *= 0x27d4eb2dU;
    x ^= x >> 15;

    return x;
}
uint32_t lowBias32Hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;

    return x;
}



//
#include <array>
#include"lutDBN.h"
typedef std::array<double, 2> Point;
unsigned mirror[256];
inline double phix(const unsigned &i)
{
  const unsigned ONE = 0x1000000;                                             // 24 bits is considered sufficient
  const double scl = 1.0 / ONE;
  return scl * (
                mirror[(i >> 16) & 255] +
                (mirror[(i >> 8) & 255] << 8) +
                (mirror[i & 255] << 16)
                );
}
void initSamplers()
{
  for (unsigned i = 0; i < 256; i++) {
    mirror[i] = (i >> 7) + ((i >> 5) & 2) + ((i >> 3) & 4) + ((i >> 1) & 8)
    + ((i << 1) & 16) + ((i << 3) & 32) + ((i << 5) & 64) + ((i << 7) & 128);
  }
}
void ldbnBNOT(const unsigned int nbPts,
              std::vector<Point> &samples)
{
  samples.clear();
  auto n = std::floor(std::sqrt((double)nbPts));
  Point p;

  double inv = 1.0 / n;
  unsigned mask = 128 - 1;                                                      // t should be a power of 2.
  unsigned shift = std::log2(128);

  for (unsigned Y = 0; Y < n; Y++) {
    for (unsigned X = 0; X < n; X++) {
      unsigned index = ((Y & mask) << shift) + (X & mask);

      double u = phix( (Y & 0xfffffff0) + (lutLDBN_BNOT[index] & 0xf) );
      double v = phix( (X & 0xfffffff0) + (lutLDBN_BNOT[index] >> 4) );
      p[0] = inv * (X + u);
      p[1] = inv * (Y + v);
	  
      samples.push_back(p);
    }
  }
}



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

	/*
	// Correlated Multi-Jitter
	int x = 10, y = 10;
	uint p = hash_int_2d<uint>(x,y);
	std::cout << "hashed_uint_p: " << p << std::endl;

	float fx,fy;
	int s=11, N=2048;
	cmj_sample_2D(s, N, p, &fx, &fy);
	std::cout << "cmj_fx= " << fx << ", cmj_fy=" << fy << std::endl; 
	std::cout << "-------------" << std::endl;
	*/

	// Halton
	//std::cout << hal(2,10000) << std::endl;
	//std::cout << halton2(1234) << std::endl; //slow
	// test halton perm based vs pbrt based
	/*
	aux::Timer timer_whole(true);
	float u1,u2,res;
	for(int i = 0; i<10000000;i++){
		//getRandomHalton2D(u1,u2,i);
		//res+=u1+u2;
		res += hal(1,i)+hal(2,i);
	}
	auto elapsed = timer_whole.Elapsed(); //end time recording
	std::cout << res << ", " << (elapsed.count()*0.001) << " seconds" << std::endl;
	*/

	// Jittered R3
	/*float f0,f1;
	getSample2D(12345, 1.f, f0, f1);
	std::cout << "r2_r0= " << f0 << ", r2_f1= " << f1 << std::endl;*/

	// Owen Scrambled Sobol
	uint ipx,ipy;
	Sobol(2, ipx, ipy);
	std::cout << "s_ipx= " << ipx << ", s_ipy=" << ipy << std::endl;
	std::cout << "oss_ipx= " << OwenScramble(ipx,42u) / float(0xFFFFFFFFu) << 
				 ", oss_ipy= " << OwenScramble(ipy,666u) / float(0xFFFFFFFFu) << std::endl;
	

	//std::cout << lowBias32Hash(12345) << std::endl;

	return 0;
}