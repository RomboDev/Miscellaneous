
//////////////////////////////////////////////////////////
// Directional dipole by T. Hachisuka and J. R. Frisvad //
// Usage: ./dirpole 100000 && xv image.ppm		//
//              	^^^^^^: number of spp		//
//////////////////////////////////////////////////////////


#include <math.h>
#include <stdlib.h>
#include <stdio.h>  
#include <iostream>

// this is generated from ISPC compilation
#include "file_dirpole.ispc.h"

#include <chrono>
#include <ostream>
namespace aux 
{
	// Timer class (c++11 high_resolution_clock)
    class Timer 
	{
        typedef std::chrono::high_resolution_clock high_resolution_clock;
        typedef std::chrono::milliseconds milliseconds;
    public:
        explicit Timer(bool run = false)
        {
            if (run) Reset();
        }
        void Reset()
        {
            _start = high_resolution_clock::now();
        }
        milliseconds Elapsed() const
        {
            return std::chrono::duration_cast<milliseconds>(high_resolution_clock::now() - _start);
        }
        template <typename T, typename Traits>
        friend std::basic_ostream<T, Traits>& operator<<(std::basic_ostream<T, Traits>& out, const Timer& timer)
        {
            return out << timer.Elapsed().count();
        }
    private:
        high_resolution_clock::time_point _start;
    };

	// Console output coloring
    enum Code {
        FG_RED      = 31,
        FG_GREEN    = 32,
        FG_BLUE     = 34,
        FG_DEFAULT  = 39,
        BG_RED      = 41,
        BG_GREEN    = 42,
        BG_BLUE     = 44,
        BG_DEFAULT  = 49
    };
    class Modifier {
        Code code;
    public:
        Modifier(Code pCode) : code(pCode) {}
        friend std::ostream&
        operator<<(std::ostream& os, const Modifier& mod) {
            return os << "\033[" << mod.code << "m";
        }
    };
}

// Computation type
typedef float Double;	// naively switch between float/double (do it in ISPC too)
			// ie. put 'double' here where you see 'float' ...........
			// change vector class alignment and eventually ..........
			// change also float3 to double3 for ISPC calling fnc ....

// Compact vector class
#ifdef _MSC_VER
__declspec(align(16))
#endif
struct Vec 
{
	Double x,y,z;
	Double pad;
/*
	union {	
		struct{Double x, y, z;}; 
		struct{Double r, g, b;};
	}; // vector: position, also color (r,g,b)
*/
	explicit Vec(Double x_=0, Double y_=0, Double z_=0) {x = x_; y = y_; z = z_;}
	//Vec(Double x_=0) {x = x_; y = x_; z = x_;}
	inline Double& operator[](const int i) {return *(&x + i);}
	inline const Double& operator[](const int i) const {return *(&x + i);}
	inline Vec operator-() const {return Vec(-x, -y, -z);}
	inline Vec operator+(const Vec &b) const {return Vec(x + b.x, y + b.y, z + b.z);}
	inline Vec operator-(const Vec &b) const {return Vec(x - b.x, y - b.y, z - b.z);}
	inline Vec operator*(const Vec &b) const {return Vec(x * b.x, y * b.y, z * b.z);}
	inline Vec operator/(const Vec &b) const {return Vec(x / b.x, y / b.y, z / b.z);}
	inline Vec operator+(Double b) const {return Vec(x + b, y + b, z + b);}
	inline Vec operator-(Double b) const {return Vec(x - b, y - b, z - b);}
	inline Vec operator*(Double b) const {return Vec(x * b, y * b, z * b);}
	inline Vec operator/(Double b) const {return Vec(x / b, y / b, z / b);}
	inline friend Vec operator+(Double b, const Vec &v) {return Vec(b + v.x, b + v.y, b + v.z);}
	inline friend Vec operator-(Double b, const Vec &v) {return Vec(b - v.x, b - v.y, b - v.z);}
	inline friend Vec operator*(Double b, const Vec &v) {return Vec(b * v.x, b * v.y, b * v.z);}
	inline friend Vec operator/(Double b, const Vec &v) {return Vec(b / v.x, b / v.y, b / v.z);}
	inline Double len() const {return sqrt(x * x + y * y + z * z);}
	inline Vec normalized() const {return (*this) / this->len();}
	inline friend Vec sqrt(const Vec&b) {return Vec(sqrt(b.x), sqrt(b.y), sqrt(b.z));}
	inline friend Vec exp(const Vec&b) {return Vec(exp(b.x), exp(b.y), exp(b.z));}
	inline Double dot(const Vec &b) const {return x * b.x + y * b.y + z * b.z;}
	inline Vec operator%(const Vec&b) const {return Vec(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x);}
}
#ifndef _MSC_VER
__attribute__((aligned(16)))
#endif
;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// @TastyTexel - Twitter
#ifdef BETTERNOTUSETHESE
static inline Double dot(const Vec a, const Vec b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
typedef Vec vec3;

struct vec2
{
	Double x,y;
	vec2(Double x_, Double y_ ) {x = x_; y = y_;}
	vec2(Double x_) {x = x_; y = x_;}
	inline vec2 operator+(const vec2 &b) const {return vec2(x + b.x, y + b.y);}
};
static inline Double dot(const vec2 a, const vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static inline Double fract (Double x_)
{
	//Double jeez = 0;
	//return modf (x_ , &jeez);
	return x_-floor(x_);
}
static inline vec3 fract (vec3 x_){
	return vec3 (fract (x_.x), fract (x_.y), fract (x_.z));
}
// "Hash without Sine" by David Hoskins - Use this for integer stepped ranges, ie Value-Noise/Perlin noise functions.
#define HASHSCALE1 0.1031
float Hash12I(vec2  p )
{
	vec3 p3 = fract(vec3(p.x,p.y,p.x ) * 0.1031);
	Double xx = dot(p3, vec3(p3.y,p3.z,p3.x)  + 19.19);
	p3 = p3 + vec3(xx,xx,xx);
	return fract((p3.x + p3.y) * p3.z);
}
// shoulder of the ground truth s-curve
inline float SCurveU_Sh(float x)
{
    float a = x < 0.25 ?   0.0        :
              x < 0.5  ? - 1.0 / 60.0 :
              x < 0.75 ?  47.0 / 60.0 :
                         -49.0 / 15.0 ;
    float b = x < 0.25 ?   2.0        :
              x < 0.5  ?   7.0 /  3.0 :
              x < 0.75 ? -17.0 /  3.0 :
                          64.0 /  3.0 ;
    float c = x < 0.25 ?   0.0        :
              x < 0.5  ? - 8.0 /  3.0 :
              x < 0.75 ?  88.0 /  3.0 :
                         -128.0/  3.0 ;
    float d = x < 0.25 ?   0.0        :
              x < 0.5  ?  32.0 /  3.0 :
              x < 0.75 ? -160.0/  3.0 :
                          128.0/  3.0 ;
    float e = x < 0.25 ?   0.0        :
              x < 0.5  ? -64.0 /  3.0 :
              x < 0.75 ?  128.0/  3.0 :
                         -64.0 /  3.0 ;
    float f = x < 0.25 ? -64.0 / 15.0 :
              x < 0.5  ?  64.0 /  5.0 :
              x < 0.75 ? -64.0 /  5.0 :
                          64.0 / 15.0 ;  
    
    float r = a + x*(b + x*(c + x*(d + x*(e + x*f))));
    return r;
}
// ground truth s-curve [-1..1]
inline float SCurveU(float x)
{
   float s = x < 0.0 ? -1.0 : 1.0;
   return SCurveU_Sh(abs(x)) * s;
}
// ground truth s-curve [0..1]
inline float SCurveU01(float x)
{
    return SCurveU(x * 2.0 - 1.0) * 0.5 + 0.5;
}
// high-pass filtered white noise with remapping
float halXXX(const int b, int j)
{
	vec2 uv ((float)b,(float)j);
    float v = Hash12I(uv);
    
    float v0 = Hash12I(uv + vec2(-1.0, 0.0));
    float v1 = Hash12I(uv + vec2( 1.0, 0.0));
    float v2 = Hash12I(uv + vec2( 0.0,-1.0));
    float v3 = Hash12I(uv + vec2( 0.0, 1.0));
    
    //float vf = v*0.5 - (v0+v1+v2+v3)*0.125 + 0.5;    
    float vf = ((v0+v1+v2+v3) * -0.25 + v) * 0.5 + 0.5;    

    #if 0
    return SCurveU01(abs(mod(uv.x + mod(uv.y, 2.0), 2.0) - ((v0+v1+v2+v3) * 0.25 + v) * 0.5));    
    #else
    //return mod(uv.x + mod(uv.y, 2.0), 2.0);
    return SCurveU01(vf);  
    #endif
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// @Reedbeta - Twitter
uint WangHash(uint seed)
{
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}

uint Xorshift(uint seed)
{
	// Xorshift algorithm from George Marsaglia's paper
	seed ^= (seed << 13);
	seed ^= (seed >> 17);
	seed ^= (seed << 5);
	return seed;
}

float halZZZ(const int b, uint seed)
{
	seed += b;
	seed = WangHash(seed);
	return float(Xorshift(seed)) * (1.f / 4294967296.f);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Above generators have poor sequences with high dimensions - they are there just for test. Use the below Halton generator for optimal stuff ///
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linear congruential PRNG
// un-comment below to use this for russian-rouletting .......
// when not, we use indexed halton sequence (a lot few noise!)
//#define LGPRNG

inline unsigned int next_rand(unsigned int current) {return current * 3125u + 49u;}
const Double rand_range = 4294967296.0;

// Halton sequence with reverse permutation
const int primes[20]={2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71};
inline int rev(const int i, const int p) {return (i == 0) ?  i : p - i;}
inline Double hal(const int b, int j) 
{
	const int p = primes[b]; Double h = 0.0, f = 1.0 / (Double)p, fct = f;
	while (j > 0) {h += rev(j % p, p) * fct; j /= p; fct *= f;} return h;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Ray
struct Ray 
{
	Vec o, d; 
	Ray():o(0),d(0){}; 
	Ray(Vec o_, Vec d_) : o(o_), d(d_) {}
};

// Sphere
const struct Sphere 
{
	Double rad; Vec p;
	Sphere(Double r_,Vec p_) : rad(r_),p(p_){}
	inline Double intersect(const Ray &r) const 
	{
		// ray-sphere intersection returns the distance
		const Vec op = p - r.o;
		Double t, b = op.dot(r.d), det = b * b - op.dot(op) + rad * rad;
		if (det < 0) return 1e20;
		else det = sqrt(det);

		return (t = b - det) > 1e-4 ? t : ((t = b + det) > 1e-4 ? t : 1e20);
	}
} sph = Sphere(25, Vec(50, 35, 60));

// Find the closest intersection
inline bool intersect(const Ray &r,Double &t) 
{
	Double d, inf = 1e20;
	t = inf;
	d = sph.intersect(r);
	if (d < t) t = d; 
	return t < inf;
}

// Uniformly sample a point on the sphere
#define PI 3.14159265358979
const Double samplePDF = 1.0 / (4.0 * PI * sph.rad * sph.rad);
inline void sample(const int i, const Vec offset, Vec &sp, Vec &sn) 
{
	Double r0 = hal(0, i) + offset.x;
	Double r1 = hal(1, i) + offset.y;
	if (r0 > 1.0) r0 -= 1.0;
	if (r1 > 1.0) r1 -= 1.0;
	const Double p = 2.0 * PI * r0, t = 2.0 * acos(sqrt(1.0 - r1));
	const Vec d = Vec(cos(p) * sin(t), cos(t), sin(p) * sin(t));
	sp = sph.p + d * sph.rad;
	sn = d;
}

// Material parameters
// potato
//const Vec sigma_s = Vec(0.68, 0.70, 0.55);
//const Vec sigma_a = Vec(0.0024, 0.0090, 0.12);
//const Vec g = Vec(0.0, 0.0, 0.0);
// white grapefruit
const Vec sigma_s = Vec(0.3513, 0.3669, 0.5237);
const Vec sigma_a = Vec(0.3609, 0.3800, 0.5632) - sigma_s;
const Vec g = Vec(0.548, 0.545, 0.565);

// Helper fncs
inline Double min3(const Double x, const Double y, const Double z) 
{
	const Double r = x < y ? x : y; return r < z ? r : z;
}
inline Double C1(const Double n) 
{
	Double r;
	if (n > 1.0) {
		r = -9.23372 + n * (22.2272 + n * (-20.9292 + n * (10.2291 + n * (-2.54396 + 0.254913 * n))));
	} else {
		r = 0.919317 + n * (-3.4793 + n * (6.75335 + n *  (-7.80989 + n *(4.98554 - 1.36881 * n))));
	}
	return r / 2.0;
}
inline Double C2(const Double n) 
{
	Double r = -1641.1 + n * (1213.67 + n * (-568.556 + n * (164.798 + n * (-27.0181 + 1.91826 * n))));
	r += (((135.926 / n) - 656.175) / n + 1376.53) / n;
	return r / 3.0;
}

// Setup some constants
struct constants
{
	const int bucketSize = 16;
	const Double eta = 1.3; //IOR
	const Vec sigma_t = sigma_s + sigma_a;
	const Vec sigma_sp = sigma_s * (1.0 - g);
	const Vec sigma_tp = sigma_sp + sigma_a;
	const Vec albedo_p = sigma_sp / sigma_tp;
	const Vec D = 1.0 / (3.0 * sigma_tp);
	const Vec sigma_tr = sqrt(sigma_a / D);
	const Vec de = 2.131 * D / sqrt(albedo_p);
	const Double Cp_norm = 1.0 / (1.0 - 2.0 * C1(1.0 / eta));
	const Double Cp = (1.0 - 2.0 * C1(eta)) / 4.0;
	const Double Ce = (1.0 - 3.0 * C2(eta)) / 2.0;
	const Double A = (1.0 - Ce) / (2.0 * Cp);
	const Double min_sigma_tr = min3(sigma_tr.x, sigma_tr.y, sigma_tr.z);
} Constants;

// Directional dipole /////////////
// --------------------------------
inline Double Sp_d(const Vec x, const Vec w, const Double r, const Vec n, const int j) 
{
	// evaluate the profile
	const Double s_tr_r = Constants.sigma_tr[j] * r;
	const Double s_tr_r_one = 1.0 + s_tr_r;
	const Double x_dot_w = x.dot(w);
	const Double r_sqr = r * r;

	const Double t0 = Constants.Cp_norm * (1.0 / (4.0 * PI * PI)) * exp(-s_tr_r) / (r * r_sqr);
	const Double t1 = r_sqr / Constants.D[j] + 3.0 * s_tr_r_one * x_dot_w;
	const Double t2 = 3.0 * Constants.D[j] * s_tr_r_one * w.dot(n);
	const Double t3 = (s_tr_r_one + 3.0 * Constants.D[j] * (3.0 * s_tr_r_one + s_tr_r * s_tr_r) / r_sqr * x_dot_w) * x.dot(n);

	return t0 * (Constants.Cp * t1 - Constants.Ce * (t2 - t3));
}
inline Double bssrdf(const Vec xi, const Vec ni, const Vec wi, const Vec xo, const Vec no, const Vec wo, const int j) 
{
	// distance
	const Vec xoxi = xo - xi;
	const Double r = xoxi.len();

	// modified normal
	const Vec ni_s = (xoxi.normalized()) % ((ni % xoxi).normalized());

	// directions of ray sources
	const Double nnt = 1.0 / Constants.eta, ddn = -wi.dot(ni);
	const Vec wr = (wi * -nnt - ni * (ddn * nnt + sqrt(1.0 - nnt * nnt * (1.0 - ddn * ddn)))).normalized();
	const Vec wv = wr - ni_s * (2.0 * wr.dot(ni_s));

	// distance to real sources
	const Double cos_beta = -sqrt((r * r - xoxi.dot(wr) * xoxi.dot(wr)) / (r * r + Constants.de[j] * Constants.de[j]));
	Double dr;
	const Double mu0 = -no.dot(wr);
	if (mu0 > 0.0) {
		dr = sqrt((Constants.D[j] * mu0) * ((Constants.D[j] * mu0) - Constants.de[j] * cos_beta * 2.0) + r * r);
	} else {
		dr = sqrt(1.0 / (3.0 * Constants.sigma_t[j] * 3.0 * Constants.sigma_t[j]) + r * r);
	}

	// distance to virtual source
	const Vec xoxv = xo - (xi + ni_s * (2.0 * Constants.A * Constants.de[j]));
	const Double dv = xoxv.len();

	// BSSRDF
	const Double result = Sp_d(xoxi, wr, dr, no, j) - Sp_d(xoxv, wv, dv, no, j);

	// clamping to zero
	return (result < 0.0) ? 0.0 : result;
}
// --------------------------------

inline Double fresnel(const Double cos_theta, const Double eta) 
{
	const Double sin_theta_t_sqr = 1.0 / (eta * eta) * (1.0 - cos_theta * cos_theta);
	if (sin_theta_t_sqr >= 1.0) return 1.0;
	const Double cos_theta_t = sqrt(1.0 - sin_theta_t_sqr);
	const Double r_s = (cos_theta - eta * cos_theta_t) / (cos_theta + eta * cos_theta_t);
	const Double r_p = (eta * cos_theta - cos_theta_t) / (eta * cos_theta + cos_theta_t);
	return (r_s * r_s + r_p * r_p) * 0.5;
}

Vec trace(const Ray &r, const int index, const int num_samps) 
{
	// ray-sphere intersection
	Double t;
	if (!intersect(r, t)) return Vec(0);

	// compute the intersection data
	const Vec x = r.o + r.d * t, n = (x - sph.p).normalized(), w = -r.d;

	// compute Fresnel transmittance at point of emergence
	const Double T21 = 1.0 - fresnel(w.dot(n), Constants.eta);

	// integration of the BSSRDF over the surface
#ifdef LGPRNG
	unsigned int xi = next_rand(index);
#endif
	Vec result(0);
	for (int i = 0; i < num_samps; i++) 
	{
		// generate a sample
		Vec sp(0), sn(0), sw(0);
		sample(i, Vec(hal(2, index), hal(3, index), 0.0), sp, sn);
		sw = Vec(1, 1, -0.5).normalized();

		// directional light source
		const Double intensity = 1.5;
		const Double cos_wi_n = sn.dot(sw);
		if (cos_wi_n > 0.0) 
		{
			// Russian roulette (note that accept_prob can be any value in (0, 1))
			const Double accept_prob = exp(-(sp - x).len() * Constants.min_sigma_tr);
#ifdef LGPRNG
			if ((xi / rand_range) < accept_prob) 
#else
			if (hal(4, index+i) < accept_prob) 
#endif
			{
				const Double T12 = 1.0 - fresnel(cos_wi_n, Constants.eta);
				const Double Li_cos = T12 * (4.0 * PI) * intensity * cos_wi_n / (samplePDF * accept_prob);

				// BSSRDF
				for (int j = 0; j < 3; j++) result[j] += bssrdf(sp, sn, sw, x, n, w, j) * Li_cos;

				// reciprocal evaluation with the reciprocity hack
				//for (int j = 0; j < 3; j++) 
				//	result[j] += 0.5 * (bssrdf(sp, sn, sw, x, n, w, j) + bssrdf(x, n, w, sp, sn, sw, j)) * Li_cos;
			}
#ifdef LGPRNG
			xi = next_rand(xi);
#endif
		}
	}
	return T21 * result / (Double)num_samps;
}


////////////////////////////////
// /////////////////////////////
int main(int argc, char *argv[]) 
{
	const int w = 512, h = 512, samps = (argc == 2) ? atoi(argv[1]) : 1 << 13;

	// Initialize main rendering process
	const Ray cam(Vec(50, 45, 290), Vec(0, -0.042612, -1).normalized());
	const Vec cx = Vec(w * 0.25 / h), cy = (cx % cam.d).normalized() * 0.25;
	Vec *stored_pixels = new Vec[w * h]; //pixel buffer//
	
#if 1
{
	// Render with std C++ and OpenMP //
	// ---------------------------------
	std::cout << "\033[31mRendering with OpenMP ... \033[31m" << std::endl;
	aux::Timer timer(true);

	#pragma omp parallel for schedule(dynamic, 1)
	for (int y = 0; y < h; ++y) 
	{
		fprintf(stderr, "\r %5.2f%%", 100.0 * y / (h - 1));
		for (int x = 0; x < w; ++x) 
		{
			const Vec d = cx * ((x + 0.5) / w - 0.5) + cy * (-(y + 0.5) / h + 0.5) + cam.d;
			const int i = x + y * w;
			stored_pixels[i] = trace(Ray(cam.o + d * 140, d.normalized()), i, samps);
		}
	}

	auto elapsed = timer.Elapsed(); //end time recording
	std::cout <<  "\ntook: " << std::fixed << elapsed.count() << "ms"  << std::endl;

	// Save the HDR image //
	float *cf = new float[w * h * 3];
	for (int i = 0; i < w * h; i++) {
		for (int j = 0; j < 3; j++) cf[i * 3 + j] = stored_pixels[i][j];
	}
	FILE *f = fopen("image_dirpole.pfm", "wb"); 
	fprintf(f, "PF\n%d %d\n%6.6f\n", w, h, -1.0);
	fwrite(cf, w * h * 3, sizeof(float), f);
	// ---------------------------------
}
#endif

#if 1
{
	// Render with ISPC 
	// Effectively once we've setup ISPC we've parallelism across 
	// the SIMD lanes of one processing core, for execution across 
	// multiple cores we can still use OPENMP, C++threads or ISPC
	// own task-parallelism as we choose to do in the .ispc file.
	// -------------------------------------------------------------
	std::cout << aux::Modifier(aux::FG_GREEN) << "Rendering with ISPC ... " << std::endl;
	aux::Timer timer(true);

	ispc::raytrace_ispc_tasks(w, h,
				  reinterpret_cast<const ispc::constants&>(Constants), reinterpret_cast<const ispc::Ray&>(cam), 
				  reinterpret_cast<const ispc::float3&>(cx), reinterpret_cast<const ispc::float3&>(cy), 
				  samps, 
				  reinterpret_cast<ispc::float3*>(stored_pixels));

	auto elapsed = timer.Elapsed(); //end time recording
	std::cout <<  "\ntook: " << std::fixed << elapsed.count() << "ms" << aux::Modifier(aux::FG_DEFAULT) << std::endl;

	// Save the HDR image //
	float *cf = new float[w * h * 3];
	for (int i = 0; i < w * h; i++) {
		for (int j = 0; j < 3; j++) cf[i * 3 + j] = stored_pixels[i][j];
	}
	FILE *f = fopen("image_dirpole.ispc.pfm", "wb"); 
	fprintf(f, "PF\n%d %d\n%6.6f\n", w, h, -1.0);
	fwrite(cf, w * h * 3, sizeof(float), f);
	// -------------------------------------------------------------
}
#endif

	return 1;
}

