///////////////////////////////////////////////////////////////////////////////////////////////////
// ISPC version of Directional Dipole by T. Hachisuka and J. R. Frisvad                          //
///////////////////////////////////////////////////////////////////////////////////////////////////
// compile it with : ispc file_dirpole.ispc --pic -h file_dirpole.ispc.h -o file_dirpole.ispc.o  //
// we'll get back a 'file_dirpole.ispc.h' to be used as header to call ISPC stuff from C++       //
// and an object file to be used at linking time ... see _acompileANDarun.sh script              //
// --------------------------------------------------------------------------------------------- //
// Without specifing a target the ISA used will be the one on the compiling machine.	         //
// Addressing instead if not specified as compilation entry will always be default 32bit         //
// Eventually download ISPC Compiler Language Extension for Visual Studio Code                   //
///////////////////////////////////////////////////////////////////////////////////////////////////

typedef float Double;	// naively switch between float/double (do it also in the C++)
			// ie. put 'double' where you see 'float' ....................

// Halton sequence with reverse permutation
const uniform int primes[20]={2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71};
static inline int rev(const int i, const int p) {return (i == 0) ?  i : p - i;}
static inline Double hal(const int b, int j) 
{
	const int p = primes[b]; 
	Double h = 0.0;
	Double f = 1.0 / (Double)p;
	Double fct = f;
	#pragma ignore warning(perf)
	while (j > 0) { h += rev(j % p, p) * fct; j /= p; fct *= f;	};
	return h;
}

// Vector class
typedef Double<3> Vec; //from ISPC
static inline Vec Cross(const Vec& l, const Vec& r) 
{ const Vec cross = { l.y*r.z - l.z*r.y, l.z*r.x - l.x*r.z, l.x*r.y - l.y*r.x }; return cross; }
static inline Double Dot(const Vec a, const Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Double SquaredLength(const Vec& v){ return Dot(v, v); }
static inline Double Length(const Vec& v){ return sqrt(SquaredLength(v)); }
static inline Vec Sqrt(const Vec& v){ const Vec s = { sqrt(v.x), sqrt(v.y), sqrt(v.z) }; return s; }
static inline Vec Normalized(const Vec& v){ return v / Length(v); }

// Ray
struct Ray { Vec o,d; };

// Sphere
#define sphereRAD 25
const uniform struct Sphere
{
	const Double rad; 
	const Vec p;

} sph = {sphereRAD, {50, 35, 60}}; //C-style declare, define, initialize
static inline Double sphereIntersect(const uniform Sphere &s, const Ray &r)
{
	// ray-sphere intersection ...
	const Vec op = s.p - r.o;
	Double t;
	Double b = Dot(op, r.d);
	Double det = b * b - Dot(op, op) + s.rad * s.rad;
	if (det < 0) return 1e20;
	else det = sqrt(det);

	// returns the distance
	return (t = b - det) > 1e-4 ? t : ((t = b + det) > 1e-4 ? t : 1e20);
}

// Find the closest intersection
static inline bool intersect(const Ray &r,Double &t)
{
	Double d, inf = 1e20;
	t = inf;
	d = sphereIntersect(sph, r);
	if (d < t) t = d; 
	return t < inf;
}

// Uniformly sample a point on the sphere
const uniform Double samplePDF = 1.0 / (4.0 * PI * sphereRAD * sphereRAD);
static inline void sample(const int i, const Vec offset, Vec &sp, Vec &sn) 
{
	Double r0 = hal(0, i) + offset.x;
	Double r1 = hal(1, i) + offset.y;
	if (r0 > 1.0) r0 -= 1.0;
	if (r1 > 1.0) r1 -= 1.0;
	const Double p = 2.0 * PI * r0, t = 2.0 * acos(sqrt(1.0 - r1));
	const Vec d = {cos(p) * sin(t), cos(t), sin(p) * sin(t)};

	sp = sph.p + d * sph.rad;
	sn = d;
}

// Helper fncs
static inline Double min3(const Double x, const Double y, const Double z) 
{
	const Double r = x < y ? x : y; return r < z ? r : z;
}

/// Apz constants
struct constants
{
	const int bucketSize;
	const Double eta;
	const Vec sigma_t;
	const Vec sigma_sp;
	const Vec sigma_tp;
	const Vec albedo_p;
	const Vec D;
	const Vec sigma_tr;
	const Vec de;
	const Double Cp_norm;
	const Double Cp;
	const Double Ce;
	const Double A;
	const Double min_sigma_tr;
};

// Directional dipole
// --------------------------------
static inline Double Sp_d(const Vec x, const Vec w,
			  const Double r, const Vec n,
			  const int j,
			  const uniform constants &Constants)
{
	// evaluate the profile
	const Double s_tr_r = Constants.sigma_tr[j] * r;
	const Double s_tr_r_one = 1.0 + s_tr_r;
	const Double x_dot_w = Dot(x,w);
	const Double r_sqr = r * r;

	const Double t0 = Constants.Cp_norm * (1.0 / (4.0 * PI * PI)) * exp(-s_tr_r) / (r * r_sqr);
	const Double t1 = r_sqr / Constants.D[j] + 3.0 * s_tr_r_one * x_dot_w;
	const Double t2 = 3.0 * Constants.D[j] * s_tr_r_one * Dot(w, n);
	const Double t3 = (s_tr_r_one + 3.0 * Constants.D[j] * (3.0 * s_tr_r_one + s_tr_r * s_tr_r) / r_sqr * x_dot_w) * Dot(x,n);

	return t0 * (Constants.Cp * t1 - Constants.Ce * (t2 - t3));
}
// BSSRDF routine
// yep .. 'inline' aggressively, it's good for ISPC !!
static inline Double bssrdf(const Vec xi, const Vec ni, 
			    const Vec wi, const Vec xo, 
			    const Vec no, const Vec wo, 
			    const int j, 
			    const uniform constants &Constants) 
{
	// distance
	const Vec xoxi = xo - xi;
	const Double r = Length(xoxi);

	// modified normal
	const Vec ni_s = Cross(Normalized(xoxi), Normalized(Cross(ni,xoxi)));
	
	// directions of ray sources
	const Double nnt = 1.0 / Constants.eta, ddn = -Dot(wi, ni);
	const Vec wr = Normalized(wi * -nnt - ni * (ddn * nnt + sqrt(1.0 - nnt * nnt * (1.0 - ddn * ddn))));
	const Vec wv = wr - ni_s * (2.0 * Dot(wr, ni_s));

	// distance to real sources
	const Double cos_beta = -sqrt((r * r - Dot(xoxi, wr) * Dot(xoxi,wr)) / (r * r + Constants.de[j] * Constants.de[j]));
	Double dr;
	const Double mu0 = -Dot(no, wr);
	if (mu0 > 0.0) {
		dr = sqrt((Constants.D[j] * mu0) * ((Constants.D[j] * mu0) - Constants.de[j] * cos_beta * 2.0) + r * r);
	} else {
		dr = sqrt(1.0 / (3.0 * Constants.sigma_t[j] * 3.0 * Constants.sigma_t[j]) + r * r);
	}

	// distance to virtual source
	const Vec xoxv = xo - (xi + ni_s * (2.0 * Constants.A * Constants.de[j]));
	const Double dv = Length(xoxv);

	// BSSRDF
	const Double result = Sp_d(xoxi, wr, dr, no, j, Constants) - Sp_d(xoxv, wv, dv, no, j, Constants);

	// clamping to zero
	return (result < 0.0) ? 0.0 : result;
}
// --------------------------------

// Fresnel transmittance
static inline Double fresnel(const Double cos_theta, const uniform Double eta) 
{
	const Double sin_theta_t_sqr = 1.0 / (eta * eta) * (1.0 - cos_theta * cos_theta);
	if (sin_theta_t_sqr >= 1.0) return 1.0;
	const Double cos_theta_t = sqrt(1.0 - sin_theta_t_sqr);
	const Double r_s = (cos_theta - eta * cos_theta_t) / (cos_theta + eta * cos_theta_t);
	const Double r_p = (eta * cos_theta - cos_theta_t) / (eta * cos_theta + cos_theta_t);
	return (r_s * r_s + r_p * r_p) * 0.5;
}

// Tracing routine
static Vec trace(const Ray &r, const int index, const uniform int num_samps, const uniform constants &Constants)
{
	// ray-sphere intersection
	Double t;
	if (!intersect(r, t)){
		const Vec v0 = {0,0,0};
		return v0;
	}
	// compute the intersection data
	const Vec x = r.o + r.d * t;
	const Vec n = Normalized(x - sph.p);
	const Vec w = -r.d;

	// compute Fresnel transmittance at point of emergence
	const Double T21 = 1.0 - fresnel(Dot(w, n), Constants.eta);

	// light is fixed
	Vec sw = {1, 1, -0.5}; sw = Normalized(sw);
	const uniform Double intensity = 1.5;

	// integration of the BSSRDF over the surface
	Vec result = {0,0,0};
	for (uniform int i = 0; i < num_samps; i++) 
	{
		// generate a sample
		Vec sp = {0,0,0};
		Vec sn = {0,0,0};
		const Vec vhal = {hal(2, index), hal(3, index), 0.0};
		sample(i, vhal, sp, sn);

		// directional light source
		const Double cos_wi_n = Dot(sn, sw);
		if (cos_wi_n > 0.0) 
		{
			// Russian roulette (note that accept_prob can be any value in (0, 1))
			const Double accept_prob = exp(-Length(sp - x) * Constants.min_sigma_tr);
			if (hal(4, index+i) < accept_prob) 
			{
				const Double T12 = 1.0 - fresnel(cos_wi_n, Constants.eta);
				const Double Li_cos = T12 * (4.0 * PI) * intensity * cos_wi_n / (samplePDF * accept_prob);

				// BSSRDF
				for (uniform int j = 0; j < 3; j++) 
					result[j] += bssrdf(sp, sn, sw, x, n, w, j, Constants) * Li_cos;
			}
		}
	}
	return T21 * result / (Double)num_samps;
}


// /////////////////////////////////////////////////////////////////
static void raytrace_tile(const uniform int x0, const uniform int x1,
			  const uniform int y0, const uniform int y1,
			  const uniform int width, const uniform int height,
			  const uniform constants &Constants,
			  const uniform Ray &cam, 
			  const uniform Vec &cx, const uniform Vec &cy,
			  const uniform int samples,
			  uniform Vec pixelbuffer[])
{
    foreach_tiled (y = y0 ... y1, x = x0 ... x1)
	{
		const Vec d = cx * ((x + 0.5) / width - 0.5) + cy * (-(y + 0.5) / height + 0.5) + cam.d;
        Ray ray;
		ray.o = cam.o+d*140;
		ray.d = Normalized(d);

		const int offset = x + y * width;
		#pragma ignore warning(perf)
		pixelbuffer[offset] = trace(ray, offset, samples, Constants);
		//          ^^^^^^ yep scatter-ed access
    }
}

// task routine
task void raytrace_tile_task(	const uniform int width, const uniform int height,
				 const uniform constants &Constants,
				 const uniform Ray &cam, 
				 const uniform Vec &cx, const uniform Vec &cy,
				 const uniform int samples,
				 uniform Vec pixelbuffer[] )
{
    const uniform int bucketSize = Constants.bucketSize;
    const uniform int xBuckets = (width + (bucketSize-1)) / bucketSize;
    const uniform int x0 = (taskIndex % xBuckets) * bucketSize;
    const uniform int x1 = min(x0 + bucketSize, width);
    const uniform int y0 = (taskIndex / xBuckets) * bucketSize;
    const uniform int y1 = min(y0 + bucketSize, height);

    raytrace_tile(x0, x1, y0, y1, width, height, Constants, cam, cx, cy, samples, pixelbuffer);
	print("\r %", taskIndex); //oh well ... :)
}

// ISPC callable routine 
// (ie. exported as "extern C" in the file_dirpole.ispc.h file )
export void raytrace_ispc_tasks(const uniform int width, const uniform int height,
				const uniform constants &Constants,
				const uniform Ray &cam, 
				const uniform Vec &cx, const uniform Vec &cy,
				const uniform int samples,
				uniform Vec pixelbuffer[])
{
    const uniform int bucketSize = Constants.bucketSize;
    const uniform int xBuckets = (width + (bucketSize-1)) / bucketSize;
    const uniform int yBuckets = (height + (bucketSize-1)) / bucketSize;
    const uniform int nTasks = xBuckets * yBuckets;

    launch[nTasks] raytrace_tile_task(width, height, Constants, cam, cx, cy, samples, pixelbuffer);
}
