#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "color.h"
#include "palette.h"

//TODO: delete this
#define COV_DIM 4

//optimize for speed rather than size
#ifndef _DEBUG
#ifdef _MSC_VER
#pragma optimize("t", on)
#endif
#endif

//assumption+assertion macros
#ifdef NDEBUG
#ifdef _MSC_VER
#define RX_ASSUME(x)    __assume(x)
#else
#define RX_ASSUME(x)    if(!(x)) __builtin_unreachable()
#endif
#else
#define RX_ASSUME(x)    if(!(x)) __debugbreak()
#endif

//inline and restrict for MSVC
#ifdef _MSC_VER
#define inline   __inline
#define restrict __restrict
#endif

#define TRUE 1
#define FALSE 0

#define RX_LARGE_NUMBER             1e32 // constant to represent large color difference
#define RX_SLAB_SIZE            0x100000 // slab size of allocator
#define INV_512    0.0019531250000000000 // 1.0/512.0
#define INV_511    0.0019569471624266144 // 1.0/511.0
#define INV_255    0.0039215686274509800 // 1.0/255.0
#define INV_3      0.3333333333333333333 // 1.0/  3.0
#define TWO_THIRDS 0.6666666666666666667 // 2.0/  3.0

//default parameters for alpha processing (describes the distribution of colors)
#define MEAN_Y    218.5703266931449500000  // mean of Y
#define MEAN_I     -0.0126499310411609110  // mean of I
#define MEAN_Q      0.0094274570675025038  // mean of Q
#define MEAN_Y2 58097.5917021356730000000  // mean of Y^2
#define MEAN_I2 11648.0137399607650000000  // mean of I^2
#define MEAN_Q2  9024.7174610226702000000  // mean of Q^2


//struct for internal processing of color leaves
typedef struct {
	double SyU;       // sum (y)
	double SiU;       // sum (i)
	double SqU;       // sum (q)
	double SaU;       // sum (a)
} RxiLongColor;

static int RxiPaletteFindClosestColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, const RxYiqColor *col, double *outDiff);
static RxStatus RxPaletteLoadYiq(RxReduction *reduction, const RxYiqColor *pltt, unsigned int srcPitch, unsigned int nColors);


//luma table: sLumaTable[i] = 511.0 * pow(i / 511.0, RX_GAMMA)
static const float sLumaTable[] = {
	  0.000000f,   0.185663f,   0.447749f,   0.749325f,   1.079798f,   1.433568f,   1.807084f,   2.197864f,
	  2.604058f,   3.024227f,   3.457215f,   3.902070f,   4.357993f,   4.824301f,   5.300404f,   5.785785f,
	  6.279987f,   6.782604f,   7.293272f,   7.811662f,   8.337473f,   8.870434f,   9.410293f,   9.956821f,
	 10.509804f,  11.069045f,  11.634360f,  12.205578f,  12.782537f,  13.365088f,  13.953089f,  14.546406f,
	 15.144915f,  15.748495f,  16.357035f,  16.970427f,  17.588570f,  18.211367f,  18.838726f,  19.470558f,
	 20.106781f,  20.747313f,  21.392077f,  22.041000f,  22.694011f,  23.351041f,  24.012026f,  24.676902f,
	 25.345609f,  26.018088f,  26.694283f,  27.374140f,  28.057605f,  28.744629f,  29.435162f,  30.129156f,
	 30.826566f,  31.527347f,  32.231455f,  32.938848f,  33.649487f,  34.363330f,  35.080341f,  35.800480f,
	 36.523713f,  37.250004f,  37.979317f,  38.711621f,  39.446882f,  40.185068f,  40.926148f,  41.670092f,
	 42.416871f,  43.166456f,  43.918818f,  44.673930f,  45.431766f,  46.192299f,  46.955504f,  47.721355f,
	 48.489828f,  49.260899f,  50.034544f,  50.810742f,  51.589468f,  52.370702f,  53.154421f,  53.940604f,
	 54.729232f,  55.520282f,  56.313736f,  57.109575f,  57.907778f,  58.708327f,  59.511203f,  60.316389f,
	 61.123867f,  61.933619f,  62.745628f,  63.559878f,  64.376351f,  65.195032f,  66.015904f,  66.838952f,
	 67.664160f,  68.491514f,  69.320998f,  70.152597f,  70.986298f,  71.822085f,  72.659945f,  73.499865f,
	 74.341830f,  75.185827f,  76.031843f,  76.879865f,  77.729881f,  78.581878f,  79.435843f,  80.291764f,
	 81.149629f,  82.009427f,  82.871145f,  83.734773f,  84.600299f,  85.467711f,  86.336999f,  87.208152f,
	 88.081159f,  88.956009f,  89.832692f,  90.711198f,  91.591516f,  92.473637f,  93.357551f,  94.243248f,
	 95.130717f,  96.019950f,  96.910938f,  97.803670f,  98.698139f,  99.594334f, 100.492246f, 101.391868f,
	102.293190f, 103.196203f, 104.100900f, 105.007271f, 105.915308f, 106.825004f, 107.736349f, 108.649337f,
	109.563958f, 110.480206f, 111.398071f, 112.317548f, 113.238627f, 114.161303f, 115.085566f, 116.011410f,
	116.938827f, 117.867811f, 118.798355f, 119.730450f, 120.664091f, 121.599270f, 122.535980f, 123.474215f,
	124.413969f, 125.355234f, 126.298004f, 127.242273f, 128.188033f, 129.135280f, 130.084006f, 131.034205f,
	131.985872f, 132.938999f, 133.893582f, 134.849614f, 135.807088f, 136.766001f, 137.726344f, 138.688114f,
	139.651303f, 140.615907f, 141.581920f, 142.549336f, 143.518149f, 144.488356f, 145.459949f, 146.432924f,
	147.407275f, 148.382997f, 149.360086f, 150.338535f, 151.318340f, 152.299495f, 153.281997f, 154.265838f,
	155.251016f, 156.237525f, 157.225359f, 158.214515f, 159.204988f, 160.196772f, 161.189863f, 162.184257f,
	163.179949f, 164.176934f, 165.175208f, 166.174766f, 167.175604f, 168.177717f, 169.181102f, 170.185753f,
	171.191667f, 172.198839f, 173.207265f, 174.216941f, 175.227862f, 176.240025f, 177.253425f, 178.268058f,
	179.283920f, 180.301008f, 181.319317f, 182.338843f, 183.359583f, 184.381532f, 185.404687f, 186.429044f,
	187.454598f, 188.481347f, 189.509286f, 190.538412f, 191.568721f, 192.600210f, 193.632874f, 194.666711f,
	195.701716f, 196.737886f, 197.775218f, 198.813708f, 199.853352f, 200.894147f, 201.936090f, 202.979177f,
	204.023405f, 205.068771f, 206.115270f, 207.162901f, 208.211659f, 209.261541f, 210.312544f, 211.364665f,
	212.417901f, 213.472248f, 214.527703f, 215.584264f, 216.641926f, 217.700688f, 218.760545f, 219.821495f,
	220.883535f, 221.946662f, 223.010872f, 224.076163f, 225.142532f, 226.209975f, 227.278491f, 228.348076f,
	229.418727f, 230.490441f, 231.563216f, 232.637048f, 233.711935f, 234.787874f, 235.864863f, 236.942898f,
	238.021976f, 239.102096f, 240.183254f, 241.265448f, 242.348675f, 243.432932f, 244.518216f, 245.604526f,
	246.691858f, 247.780210f, 248.869580f, 249.959964f, 251.051360f, 252.143766f, 253.237179f, 254.331596f,
	255.427016f, 256.523436f, 257.620852f, 258.719263f, 259.818667f, 260.919061f, 262.020442f, 263.122808f,
	264.226157f, 265.330486f, 266.435794f, 267.542077f, 268.649333f, 269.757561f, 270.866757f, 271.976920f,
	273.088047f, 274.200136f, 275.313185f, 276.427191f, 277.542152f, 278.658067f, 279.774932f, 280.892746f,
	282.011507f, 283.131212f, 284.251859f, 285.373447f, 286.495972f, 287.619433f, 288.743827f, 289.869154f,
	290.995410f, 292.122593f, 293.250702f, 294.379734f, 295.509688f, 296.640561f, 297.772351f, 298.905056f,
	300.038675f, 301.173205f, 302.308645f, 303.444992f, 304.582244f, 305.720399f, 306.859457f, 307.999413f,
	309.140268f, 310.282018f, 311.424662f, 312.568199f, 313.712625f, 314.857940f, 316.004141f, 317.151226f,
	318.299195f, 319.448044f, 320.597772f, 321.748377f, 322.899858f, 324.052213f, 325.205439f, 326.359536f,
	327.514501f, 328.670332f, 329.827028f, 330.984587f, 332.143008f, 333.302288f, 334.462426f, 335.623420f,
	336.785269f, 337.947970f, 339.111522f, 340.275924f, 341.441174f, 342.607269f, 343.774209f, 344.941992f,
	346.110616f, 347.280079f, 348.450380f, 349.621518f, 350.793490f, 351.966295f, 353.139931f, 354.314397f,
	355.489692f, 356.665813f, 357.842759f, 359.020529f, 360.199121f, 361.378533f, 362.558764f, 363.739813f,
	364.921677f, 366.104356f, 367.287847f, 368.472150f, 369.657263f, 370.843183f, 372.029911f, 373.217444f,
	374.405781f, 375.594920f, 376.784861f, 377.975600f, 379.167138f, 380.359473f, 381.552602f, 382.746525f,
	383.941241f, 385.136747f, 386.333043f, 387.530127f, 388.727998f, 389.926654f, 391.126093f, 392.326316f,
	393.527319f, 394.729102f, 395.931664f, 397.135002f, 398.339116f, 399.544005f, 400.749667f, 401.956100f,
	403.163303f, 404.371276f, 405.580016f, 406.789522f, 407.999794f, 409.210829f, 410.422627f, 411.635186f,
	412.848504f, 414.062581f, 415.277416f, 416.493006f, 417.709352f, 418.926450f, 420.144301f, 421.362903f,
	422.582255f, 423.802355f, 425.023202f, 426.244796f, 427.467134f, 428.690215f, 429.914039f, 431.138604f,
	432.363909f, 433.589953f, 434.816734f, 436.044251f, 437.272504f, 438.501490f, 439.731209f, 440.961660f,
	442.192841f, 443.424751f, 444.657390f, 445.890755f, 447.124846f, 448.359661f, 449.595200f, 450.831461f,
	452.068443f, 453.306146f, 454.544567f, 455.783706f, 457.023562f, 458.264133f, 459.505418f, 460.747417f,
	461.990128f, 463.233550f, 464.477682f, 465.722523f, 466.968071f, 468.214327f, 469.461288f, 470.708953f,
	471.957322f, 473.206394f, 474.456166f, 475.706640f, 476.957812f, 478.209682f, 479.462250f, 480.715513f,
	481.969472f, 483.224125f, 484.479470f, 485.735508f, 486.992236f, 488.249654f, 489.507761f, 490.766556f,
	492.026038f, 493.286205f, 494.547058f, 495.808594f, 497.070812f, 498.333713f, 499.597294f, 500.861556f,
	502.126496f, 503.392113f, 504.658408f, 505.925379f, 507.193024f, 508.461343f, 509.730336f, 511.000000f
};



// ----- memory allocation wrappers for SIMD use

#if defined(RX_SIMD) && !defined(_M_X64)

//alignment of memory to be allocated by RxMem* routines
#define ALLOC_ALIGN sizeof(__m128)

void *RxMemAlloc(size_t size) {
	unsigned char *req = malloc((size + sizeof(void *) + ALLOC_ALIGN - 1) & ~(ALLOC_ALIGN - 1));
	unsigned char *aligned = (unsigned char *) ((((uintptr_t) req) + sizeof(void *) + ALLOC_ALIGN - 1) & ~(uintptr_t) (ALLOC_ALIGN - 1));
	if (aligned != NULL) ((void **) aligned)[-1] = req;

	return aligned;
}

void *RxMemCalloc(size_t nMemb, size_t size) {
	unsigned char *req = calloc((size * nMemb + sizeof(void *) + ALLOC_ALIGN - 1) & ~(ALLOC_ALIGN - 1), 1);
	unsigned char *aligned = (unsigned char *) ((((uintptr_t) req) + sizeof(void *) + ALLOC_ALIGN - 1) & ~(uintptr_t) (ALLOC_ALIGN - 1));
	if (aligned != NULL) ((void **) aligned)[-1] = req;

	return aligned;
}

void RxMemFree(void *p) {
	if (p != NULL) free(((void **) p)[-1]);
}

#endif


// ----- routines for operating on colors


static inline double RxiDelinearizeLuma(float luma) {
	return 511.0 * pow(luma * INV_511, 1.0 / RX_GAMMA);
}

static inline float RxiLinearizeLuma(float luma) {
	RX_ASSUME(luma >= 0 && luma < 512);
#ifndef RX_SIMD
	return sLumaTable[(int) (luma + 0.5)];
#else
	return sLumaTable[_mm_cvtss_si32(_mm_set_ss(luma))];
#endif
}

void RxConvertRgbToYiq(COLOR32 rgb, RxYiqColor *yiq) {
	//implementations using scalar and vector arithmetic
#ifndef RX_SIMD
	double r = (double) ((rgb >>  0) & 0xFF);
	double g = (double) ((rgb >>  8) & 0xFF);
	double b = (double) ((rgb >> 16) & 0xFF);

	//twice the standard RGB->YIQ matrix (doubles output components)
	double y = r * 0.59800 + g * 1.17400 + b * 0.22800;
	double i = r * 1.19208 - g * 0.54804 - b * 0.64406;
	double q = r * 0.42204 - g * 1.04408 + b * 0.62206;

	if (i >  245.0) i = (i - 245.0) * TWO_THIRDS + 245.0;
	if (q < -215.0) q = (q + 215.0) * TWO_THIRDS - 215.0;

	double iqDiff = q - i;
	if (iqDiff > 265.0) {
		double diq = (iqDiff - 265.0) * 0.25;
		i += diq;
		q -= diq;
	}

	if (i < 0.0 && q > 0.0) y -= (q * i) * INV_512;

	//write rounded color
	float a = ((rgb >> 24) & 0xFF) / 255.0f;
	yiq->y = a * RxiLinearizeLuma((float) y); //    0 - 511
	yiq->i = a * (float) i;                   // -320 - 319
	yiq->q = a * (float) q;                   // -270 - 269
	yiq->a = a;
#else
	//vectorized implementation
	__m128i rgbVeci = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(rgb), _mm_setzero_si128()), _mm_setzero_si128());
	__m128 rgbVec = _mm_cvtepi32_ps(rgbVeci);

	__m128 yVec = _mm_mul_ps(rgbVec, _mm_setr_ps(0.59800f,  1.17400f,  0.22800f, 0.0f));
	__m128 iVec = _mm_mul_ps(rgbVec, _mm_setr_ps(1.19208f, -0.54804f, -0.64406f, 0.0f));
	__m128 qVec = _mm_mul_ps(rgbVec, _mm_setr_ps(0.42204f, -1.04408f,  0.62206f, 0.0f));

	//do three horizontal sums
	yVec = _mm_add_ps(yVec, _mm_shuffle_ps(yVec, yVec, _MM_SHUFFLE(2, 3, 0, 1)));
	iVec = _mm_add_ps(iVec, _mm_shuffle_ps(iVec, iVec, _MM_SHUFFLE(0, 1, 2, 3)));
	qVec = _mm_add_ps(qVec, _mm_shuffle_ps(qVec, qVec, _MM_SHUFFLE(0, 1, 2, 3)));

	__m128 iqVec = _mm_shuffle_ps(iVec, qVec, _MM_SHUFFLE(0, 1, 0, 1)); // lo: half I sum, hi: half Q sum, halves reversed
	iqVec = _mm_add_ps(iqVec, _mm_shuffle_ps(iqVec, iqVec, _MM_SHUFFLE(2, 3, 0, 1)));

	//place components into low parts of vector registers
	__m128 y = _mm_add_ss(yVec, _mm_movehl_ps(yVec, yVec));
	__m128 i = iqVec;
	__m128 q = _mm_movehl_ps(iqVec, iqVec);

	//apply soft clamping by I>245, Q<-215 by 2/3
	__m128 twoThirds = _mm_set_ss((float) TWO_THIRDS);
	if (_mm_ucomigt_ss(i, _mm_set_ss( 245.0f))) i = _mm_add_ss(_mm_mul_ss(i, twoThirds), _mm_set_ss((float) (245.0 - 245.0 * TWO_THIRDS)));
	if (_mm_ucomilt_ss(q, _mm_set_ss(-215.0f))) q = _mm_add_ss(_mm_mul_ss(q, twoThirds), _mm_set_ss((float) (215.0 * TWO_THIRDS - 215.0)));

	__m128 iqDiff = _mm_sub_ss(q, i);
	if (_mm_ucomigt_ss(iqDiff, _mm_set_ss(265.0f))) {
		__m128 diq = _mm_mul_ss(_mm_sub_ss(iqDiff, _mm_set_ss(265.0f)), _mm_set_ss(0.25f));
		i = _mm_add_ss(i, diq);
		q = _mm_sub_ss(q, diq);
	}

	__m128 zero = _mm_setzero_ps();
	if (_mm_ucomilt_ss(i, zero) && _mm_ucomigt_ss(q, zero)) y = _mm_sub_ss(y, _mm_mul_ss(_mm_mul_ss(q, i), _mm_set_ss((float) INV_512)));

	//horizontal sum
	__m128 yiqa = _mm_movelh_ps(_mm_unpacklo_ps(y, i), _mm_unpacklo_ps(q, zero));

	//linearize luma
	yiqa = _mm_move_ss(yiqa, _mm_load_ss(&sLumaTable[_mm_cvt_ss2si(yiqa)]));

	//insert alpha channel to the output vector and premultiply
	__m128 aVec = _mm_div_ps(_mm_shuffle_ps(rgbVec, rgbVec, _MM_SHUFFLE(3, 3, 3, 3)), _mm_set1_ps(255.0f));
	yiqa = _mm_mul_ps(yiqa, aVec);
	yiqa = _mm_or_ps(yiqa, _mm_and_ps(_mm_castsi128_ps(_mm_setr_epi32(0, 0, 0, -1)), aVec));

	yiq->yiq = yiqa;
#endif
}

COLOR32 RxConvertYiqToRgb(const RxYiqColor *yiq) {
	double da = yiq->a;
	double y = 0.0, i = 0.0, q = 0.0;
	if (da > 0.0) {
		y = RxiDelinearizeLuma(yiq->y) / da;
		i = yiq->i / da;
		q = yiq->q / da;
	}

	if (i < 0.0 && q > 0.0) y += (q * i) * INV_512;

	if (y < 0.0) y = 0.0;
	else if (y > 511.0) y = 511.0;	

	double iqDiff = q - i;
	if (iqDiff > 265.0) {
		double diq = (iqDiff - 265.0) * 0.5;
		i -= diq;
		q += diq;
	}

	if (q < -215.0) q = (q + 215.0) * 1.5 - 215.0;
	if (i >  245.0) i = (i - 245.0) * 1.5 + 245.0;

	int r = (int) (y * 0.5 + i * 0.477791 + q * 0.311426 + 0.5);
	int g = (int) (y * 0.5 - i * 0.136066 - q * 0.324141 + 0.5);
	int b = (int) (y * 0.5 - i * 0.552535 + q * 0.852230 + 0.5);
	int a = (int) (yiq->a * 255.0 + 0.5);

	//pack clamped color
	r = min(max(r, 0), 255);
	g = min(max(g, 0), 255);
	b = min(max(b, 0), 255);
	a = min(max(a, 0), 255);
	return r | (g << 8) | (b << 16) | (a << 24);
}

static inline double RxiComputeColorDifference(RxReduction *reduction, const RxYiqColor *yiq1, const RxYiqColor *yiq2) {
#ifndef RX_SIMD
	double yw2 = reduction->yWeight2;
	double iw2 = reduction->iWeight2;
	double qw2 = reduction->qWeight2;
	double aw2 = reduction->aWeight2;

	double dy = yiq1->y - yiq2->y;
	double di = yiq1->i - yiq2->i;
	double dq = yiq1->q - yiq2->q;
	double da = yiq1->a - yiq2->a;

	double d2 = yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq;

	if (da != 0.0) {
		//The color difference with alpha. We define the difference to be the average squared difference
		//between the two input colors composited onto a random background color, where the background
		//color is drawn from a uniform distribution in RGB space. The below interaction term results from
		//evaluating this integral.
		d2 += aw2 * da * da - da * (reduction->interactionY * dy + reduction->interactionI * di + reduction->interactionQ * dq);
	}
	return d2;
#else // RX_SIMD
	//color difference vector
	__m128 d = _mm_sub_ps(yiq1->yiq, yiq2->yiq);
	__m128 da = _mm_shuffle_ps(d, d, _MM_SHUFFLE(3, 3, 3, 3));

	//squared components, minus alpha interaction
	__m128 d2 = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(d, reduction->yiqaWeight2), _mm_mul_ps(da, reduction->interactionYIQA)), d);

	//final horizontal sum
	d2 = _mm_add_ps(d2, _mm_shuffle_ps(d2, d2, _MM_SHUFFLE(2, 3, 0, 1)));
	d2 = _mm_add_ss(d2, _mm_movehl_ps(d2, d2));
	return (double) _mm_cvtss_f32(d2);
#endif
}

static inline double RxiComputeLayeredColorDifference(RxReduction *reduction, const RxYiqColor *yiq1, const RxYiqColor *yiq2) {
	double diff = 0.0;

	//add multiple diffs
	for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
		diff += RxiComputeColorDifference(reduction, &yiq1[i], &yiq2[i]);
	}

	return diff;
}

double RxComputeColorDifference(RxReduction *reduction, const RxYiqColor *yiq1, const RxYiqColor *yiq2) {
	return RxiComputeColorDifference(reduction, yiq1, yiq2);
}

static COLOR32 RxMaskColorToDS15(COLOR32 c) {
	//DS mode masking: round color channels to 5-bit values, and force alpha=0xff
	return ColorRoundToDS15(c) | 0xFF000000;
}

static COLOR32 RxMaskColorDummy(COLOR32 c) {
	//no-mask dummy: pass colors
	return c;
}

static inline COLOR32 RxiMaskYiqToRgb(RxReduction *reduction, const RxYiqColor *yiq) {
	return reduction->maskColors(RxConvertYiqToRgb(yiq));
}

static inline void RxiColorMakeOpaque(RxYiqColor *yiq) {
	RX_ASSUME(yiq->a != 0.0f);  // a fully transparent color has no meaningful color information

#ifndef RX_SIMD
	yiq->y /= yiq->a;
	yiq->i /= yiq->a;
	yiq->q /= yiq->a;
	yiq->a = 1.0f;
#else
	__m128 vec = yiq->yiq;
	__m128 a = _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3, 3, 3, 3));
	yiq->yiq = _mm_div_ps(vec, a);
#endif
}

static inline void RxiColorMakeTransparent(RxYiqColor *yiq) {
#ifndef RX_SIMD
	yiq->y = 0.0f;
	yiq->i = 0.0f;
	yiq->q = 0.0f;
	yiq->a = 0.0f;
#else
	yiq->yiq = _mm_setzero_ps();
#endif
}



// ----- reduction context initialization/setup routines

static void RxiComputeAlphaInteraction(RxReduction *reduction) {
	//color difference is computed by taking the average squared difference between colors composited onto
	//a background color. This calculation requires the first and second moments of the color space, which
	//are given in the reduction context prior. These may be set by the user to assume any distribution of
	//background colors for compositing.
	reduction->aWeight2 = reduction->meanY2 * reduction->yWeight2 + reduction->meanI2 * reduction->iWeight2 + reduction->meanQ2 * reduction->qWeight2;
	reduction->aWeight = sqrt(reduction->aWeight2);

	reduction->interactionY = (2.0 * reduction->meanY) * reduction->yWeight2;
	reduction->interactionI = (2.0 * reduction->meanI) * reduction->iWeight2;
	reduction->interactionQ = (2.0 * reduction->meanQ) * reduction->qWeight2;
	reduction->interactionA = 0.0;

	//when using SIMD, set vector weights
#ifdef RX_SIMD
	__m128 weightAQ = _mm_cvtpd_ps(reduction->qaWeight2);
	__m128 weightIY = _mm_cvtpd_ps(reduction->yiWeight2);
	__m128 weight2 = _mm_shuffle_ps(_mm_shuffle_ps(weightIY, weightIY, _MM_SHUFFLE(1, 0, 1, 0)), weightAQ, _MM_SHUFFLE(1, 0, 3, 2));
	reduction->yiqaWeight2 = weight2;

	__m128 interactionYI = _mm_cvtpd_ps(reduction->interactionYI);
	__m128 interactionQA = _mm_cvtpd_ps(reduction->interactionQA);
	__m128 interaction = _mm_shuffle_ps(_mm_shuffle_ps(interactionYI, interactionYI, _MM_SHUFFLE(1, 0, 1, 0)), interactionQA, _MM_SHUFFLE(1, 0, 3, 2));
	reduction->interactionYIQA = interaction;
#endif
}

void RxSetBalance(RxReduction *reduction, int balance, int colorBalance, int enhanceColors) {
	reduction->yWeight = 60 - balance;       // high balance -> lower Y weight
	reduction->iWeight = colorBalance;       // high color balance -> high I weight
	reduction->qWeight = 40 - colorBalance;  // high color balance -> low Q weight

	reduction->yWeight2 = reduction->yWeight * reduction->yWeight; // Y weight squared
	reduction->iWeight2 = reduction->iWeight * reduction->iWeight; // I weight squared
	reduction->qWeight2 = reduction->qWeight * reduction->qWeight; // Q weight squared

	//compute alpha weights and interactions
	RxiComputeAlphaInteraction(reduction);

	reduction->enhanceColors = enhanceColors;
}

RxStatus RxSetPaletteLayers(RxReduction *reduction, unsigned int nLayers) {
	//check valid layer count
	if (nLayers == 0 || nLayers > RX_PALETTE_MAX_COUNT) return RX_STATUS_INVALID;

	//the context must not have any histogram colors
	if (reduction->histogram != NULL && reduction->histogram->nEntries > 0) return RX_STATUS_INCORRECT_STATE;

	reduction->paletteLayers = nLayers;
	return RX_STATUS_OK;
}

void RxInit(RxReduction *reduction, int balance, int colorBalance, int enhanceColors) {
	memset(reduction, 0, sizeof(RxReduction));

	//default color space moments, precalculated assuming a uniform distribution of RGB colors
	reduction->meanY = MEAN_Y;
	reduction->meanI = MEAN_I;
	reduction->meanQ = MEAN_Q;
	reduction->meanY2 = MEAN_Y2;
	reduction->meanI2 = MEAN_I2;
	reduction->meanQ2 = MEAN_Q2;

	RxSetBalance(reduction, balance, colorBalance, enhanceColors);
	RxSetPaletteLayers(reduction, 1);

	reduction->nReclusters = RECLUSTER_DEFAULT;
	reduction->nPaletteColors = RX_PALETTE_MAX_SIZE;
	reduction->maskColors = RxMaskColorToDS15;
	reduction->alphaMode = RX_ALPHA_NONE; // default: no alpha processing
	reduction->fAlphaThreshold = (float) (0x80 * INV_255);
	reduction->status = RX_STATUS_OK;
}

void RxAssumeCompositingDistribution(RxReduction *reduction, const COLOR32 *cols, unsigned int nCols) {
	if (nCols == 0) {
		//defaults (uniform distribution)
		reduction->meanY = MEAN_Y;
		reduction->meanI = MEAN_I;
		reduction->meanQ = MEAN_Q;
		reduction->meanY2 = MEAN_Y2;
		reduction->meanI2 = MEAN_I2;
		reduction->meanQ2 = MEAN_Q2;
	} else {
		//calculate the first and second moments of colors given in the input. We take each color to have the
		//same weight, since we are not modeling perception of colors in the input, but taking it as a probability
		//distribution.
		double sumY = 0.0, sumI = 0.0, sumQ = 0.0, sumY2 = 0.0, sumI2 = 0.0, sumQ2 = 0.0;

		for (unsigned int i = 0; i < nCols; i++) {
			RxYiqColor yiq;
			RxConvertRgbToYiq(cols[i], &yiq);

			sumY += yiq.y;
			sumI += yiq.i;
			sumQ += yiq.q;
			sumY2 += yiq.y * yiq.y;
			sumI2 += yiq.i * yiq.i;
			sumQ2 += yiq.q * yiq.q;
		}

		double invCols = 1.0 / (double) nCols;
		reduction->meanY = sumY * invCols;
		reduction->meanI = sumI * invCols;
		reduction->meanQ = sumQ * invCols;
		reduction->meanY2 = sumY2 * invCols;
		reduction->meanI2 = sumI2 * invCols;
		reduction->meanQ2 = sumQ2 * invCols;
	}

	//recompute weights and interactions
	RxiComputeAlphaInteraction(reduction);
}

RxReduction *RxNew(int balance, int colorBalance, int enhanceColors) {
	RxReduction *reduction = (RxReduction *) RxMemCalloc(1, sizeof(RxReduction));
	if (reduction == NULL) return NULL;

	RxInit(reduction, balance, colorBalance, enhanceColors);
	return reduction;
}

void RxApplyFlags(RxReduction *reduction, RxFlag flag) {
	//set alpha mode
	switch (flag & RX_FLAG_ALPHA_MODE_MASK) {
		case RX_FLAG_ALPHA_MODE_NONE: reduction->alphaMode = RX_ALPHA_NONE; break;
		case RX_FLAG_ALPHA_MODE_RESERVE: reduction->alphaMode = RX_ALPHA_RESERVE; break;
		case RX_FLAG_ALPHA_MODE_PIXEL: reduction->alphaMode = RX_ALPHA_PIXEL; break;
		case RX_FLAG_ALPHA_MODE_PALETTE: reduction->alphaMode = RX_ALPHA_PALETTE; break;
	}

	if (flag & RX_FLAG_NO_MASK_BITS) {
		//no color masking -> use dummy color mask callback
		reduction->maskColors = RxMaskColorDummy;
	} else {
		//with color masking
		reduction->maskColors = RxMaskColorToDS15;
	}
}

void RxSetProgressCallback(RxReduction *reduction, RxProgressCallback callback, void *userData) {
	reduction->progressCallback = callback;
	reduction->progressCallbackData = userData;
}

static void RxiUpdateProgress(RxReduction *reduction, unsigned int progress, unsigned int progressMax) {
	if (reduction->progressCallback != NULL) {
		reduction->progressCallback(reduction, progress, progressMax, reduction->progressCallbackData);
	}
}

int RxColorLightnessComparator(const void *d1, const void *d2) {
	COLOR32 c1 = *(COLOR32 *) d1;
	COLOR32 c2 = *(COLOR32 *) d2;
	if (c1 == c2) return 0;

	//sort by ascending alpha value first, pushing fully opaque colors to the end of the palette.
	//this allows more efficient palette alpha representation in formats like PNG.
	int a1 = c1 >> 24, a2 = c2 >> 24;
	if (a1 != a2) return a1 - a2;

	RxYiqColor yiq1, yiq2;
	RxConvertRgbToYiq(c1, &yiq1);
	RxConvertRgbToYiq(c2, &yiq2);

	if (yiq1.y < yiq2.y) return -1;
	if (yiq1.y > yiq2.y) return 1;
	return 0;
}


// ----- internal allocator routines

static void *RxiSlabAlloc(RxSlab *allocator, unsigned int size) {
	RX_ASSUME(size <= RX_SLAB_SIZE);

	//if no slab is allocated, allocate one.
	if (allocator->allocation == NULL) {
		allocator->allocation = RxMemCalloc(RX_SLAB_SIZE, 1);
		allocator->pos = 0;
		if (allocator->allocation == NULL) return NULL;
	}

	//search for a slab with a suitable size.
	while ((allocator->pos + size) > RX_SLAB_SIZE) {
		if (allocator->next == NULL) {
			RxSlab *next = calloc(1, sizeof(RxSlab));
			if (next == NULL) return NULL;

			next->allocation = RxMemCalloc(RX_SLAB_SIZE, 1);
			next->pos = 0;
			allocator->next = next;
			if (next->allocation == NULL) return NULL;
		}
		allocator = allocator->next;
	}

	void *res = (void *) (((uintptr_t) allocator->allocation) + allocator->pos);
	allocator->pos += size;
	return res;
}

static void RxiSlabFreeAll(RxSlab *allocator) {
	if (allocator->allocation != NULL) RxMemFree(allocator->allocation);
	allocator->allocation = NULL;

	if (allocator->next != NULL) {
		RxiSlabFreeAll(allocator->next);
		allocator->next = NULL;
	}
}



// ----- histogram routines

//hash a color for use in the histogram
static inline unsigned int RxiHistHashColor(const RxYiqColor *yiq) {
#ifndef RX_SIMD
	double yi = yiq->y * 256.0, ii = yiq->i * 1.0, qi = yiq->q * 4.0, ai = 255.0f * yiq->a;
	return ((int) (yi + ii + qi + ai + 0.5)) & (RX_HISTOGRAM_SIZE - 1);
#else
	__m128i yiqI = _mm_cvtps_epi32(_mm_mul_ps(yiq->yiq, _mm_set_ps(255.0f, 1.0f, 4.0f, 256.0f)));
	__m128i sum = _mm_add_epi32(yiqI, _mm_shuffle_epi32(yiqI, _MM_SHUFFLE(2, 3, 0, 1)));
	sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, _MM_SHUFFLE(0, 1, 2, 3)));
	return _mm_cvtsi128_si32(sum) & (RX_HISTOGRAM_SIZE - 1);
#endif
}

RxStatus RxHistInit(RxReduction *reduction) {
	if (reduction->histogram != NULL) RxHistClear(reduction);

	reduction->histogram = (RxHistogram *) calloc(1, sizeof(RxHistogram));
	if (reduction->histogram == NULL) return RX_STATUS_NOMEM;

	reduction->histogram->firstSlot = RX_HISTOGRAM_SIZE;
	return RX_STATUS_OK;
}

void RxHistAddColor(RxReduction *reduction, const RxYiqColor *col, double weight) {
	RxHistogram *histogram = reduction->histogram;
	if (reduction->status != RX_STATUS_OK) return;

	unsigned int nLayer = reduction->paletteLayers;

	//update the first slot index with hash of new color
	int slotIndex = RxiHistHashColor(col);
	if (slotIndex < histogram->firstSlot) histogram->firstSlot = slotIndex;

	//find a slot with the same YIQA, or create a new one if none exists.
	RxHistEntry **ppslot = &histogram->entries[slotIndex];
	while (*ppslot != NULL) {
		RxHistEntry *slot = *ppslot;

		//matching slot? add weight
		if (memcmp(slot->color, col, nLayer * sizeof(RxYiqColor)) == 0) {
			slot->weight += weight;
			return;
		}

		ppslot = &slot->next;
	}

	RxHistEntry *slot = (RxHistEntry *) RxiSlabAlloc(&histogram->allocator, sizeof(RxHistEntry) + nLayer * sizeof(RxYiqColor));
	if (slot == NULL) {
		reduction->status = RX_STATUS_NOMEM;
		return;
	}

	//put new color
	*ppslot = slot;
	memcpy(slot->color, col, nLayer * sizeof(RxYiqColor));
	slot->weight = weight;
	slot->next = NULL;
	slot->value = 0.0;
	histogram->nEntries++;
	histogram->totalWeight += weight;
}

RxStatus RxHistFinalize(RxReduction *reduction) {
	if (reduction->status != RX_STATUS_OK) return reduction->status;
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);

	if (reduction->histogram == NULL) {
		reduction->histogramFlat = NULL;
		return RX_STATUS_OK;
	}

	reduction->histogramFlat = (RxHistEntry **) calloc(reduction->histogram->nEntries, sizeof(RxHistEntry *));
	if (reduction->histogramFlat == NULL) {
		return reduction->status = RX_STATUS_NOMEM;
	}

	RxHistEntry **pos = reduction->histogramFlat;

	for (int i = reduction->histogram->firstSlot; i < RX_HISTOGRAM_SIZE; i++) {
		RxHistEntry *entry = reduction->histogram->entries[i];

		while (entry != NULL) {
			*(pos++) = entry;
			entry = entry->next;
		}
	}
	return RX_STATUS_OK;
}

RxStatus RxHistAdd(RxReduction *reduction, const COLOR32 *img, unsigned int width, unsigned int height) {
	if (reduction->histogram == NULL) {
		RxStatus status = RxHistInit(reduction);
		if (status != RX_STATUS_OK) return reduction->status = status;
	}
	
	if (width == 0 || height == 0) return reduction->status;

	//create YIQ data buffer, 1px overhang in all directions where pixels are duplicated
	unsigned int padWidth = width + 2, padHeight = height + 2, nLayer = reduction->paletteLayers;
	unsigned int nPxSrc = width * height;
	
	RxYiqColor *yiqbuf = (RxYiqColor *) RxMemAlloc(padWidth * padHeight * nLayer * sizeof(RxYiqColor));
	if (yiqbuf == NULL) {
		return reduction->status = RX_STATUS_NOMEM;
	}

	//convert input data into YIQ space. We rearrange the data to being indexed as
	//[layer][y][x], to [y][x][layer].
	for (unsigned int i = 0; i < nLayer; i++) {
		const COLOR32 *imgI = img + i * nPxSrc;

		for (unsigned int y = 0; y < height; y++) {
			for (unsigned int x = 0; x < width; x++) {
				RxConvertRgbToYiq(imgI[x + y * width], &yiqbuf[((x + 1) + (y + 1) * padWidth) * nLayer + i]);
			}
		}
	}

	//copy pixels into the overhang areas
	for (unsigned int y = 0; y < height; y++) {
		RxYiqColor *row = &yiqbuf[(y + 1) * padWidth * nLayer];

		memcpy(&row[0], &row[1 * nLayer], nLayer * sizeof(RxYiqColor));
		memcpy(&row[(width + 1) * nLayer], &row[width * nLayer], nLayer * sizeof(RxYiqColor));
	}
	memcpy(&yiqbuf[0 * padWidth * nLayer], &yiqbuf[padWidth * nLayer], padWidth * nLayer * sizeof(RxYiqColor));
	memcpy(&yiqbuf[(height + 1) * padWidth * nLayer], &yiqbuf[height * padWidth * nLayer], padWidth * nLayer * sizeof(RxYiqColor));


	for (unsigned int y = 0; y < height; y++) {
		
		RxYiqColor *row0 = &yiqbuf[(y + 0) * padWidth * nLayer];
		RxYiqColor *row1 = &yiqbuf[(y + 1) * padWidth * nLayer];
		RxYiqColor *row2 = &yiqbuf[(y + 2) * padWidth + nLayer];

		for (unsigned int x = 0; x < width; x++) {

			RxYiqColor *top = &row0[(x + 1) * nLayer];
			RxYiqColor *bottom = &row2[(x + 1) * nLayer];
			RxYiqColor *left = &row1[(x + 0) * nLayer];
			RxYiqColor *right = &row1[(x + 2) * nLayer];
			RxYiqColor *center = &row1[(x + 1) * nLayer];

			//copy the center color to a temporary location as we may modify the color based on the alpha
			//mode, and do not want this to affect the weighting calculations of other pixels.
			RxYiqColor *col = reduction->tempLayeredColor;
			memcpy(col, center, nLayer * sizeof(RxYiqColor));

			//when we calculate the weight of multiple colors, we take the weight to be the sum of weights
			//of individual colors. This allows a color where there exist completely transparent pixels
			//but receive a nonzero weight since one color may be opaque. Only when all individual colors
			//have a zero weight do we discard an input pixel.

			//compute weight, accumulated per layer
			double totalWeight = 0.0;
			for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
				double yInter = 0.25 * (left[i].y + right[i].y + top[i].y + bottom[i].y);
				double yCenter = center->y;
				double yDiff = fabs(yCenter - yInter);
				double weight = 16.0 - fabs(16.0 - yDiff) / 8.0;
				if (weight < 1.0) weight = 1.0;

				//process the alpha value.
				switch (reduction->alphaMode) {
					case RX_ALPHA_NONE:
					case RX_ALPHA_RESERVE:
						//we use tha alpha threshold here since these alpha modes imply binary alpha.
						if (col[i].a < reduction->fAlphaThreshold) {
							RxiColorMakeTransparent(&col[i]);
							weight = 0.0;
						} else {
							RxiColorMakeOpaque(&col[i]);
						}
						break;

					case RX_ALPHA_PIXEL:
						//we'll discard alpha=0 since it doesn't need to appear in the palette.
						weight *= col[i].a;
						if (col[i].a > 0.0f) RxiColorMakeOpaque(&col[i]);
						break;

					case RX_ALPHA_PALETTE:
						//we explicitly must pass all alpha values.
						break;

					default:
						//must not reach here
						RX_ASSUME(0);
				}

				totalWeight += weight;
			}

			//add the color to the histogram only if its total weight was nonzero.
			if (totalWeight > 0.0) {
				RxHistAddColor(reduction, col, totalWeight);
			}
		}
	}

	RxMemFree(yiqbuf);

	return reduction->status;
}

double RxHistComputePaletteErrorYiq(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, double maxError) {
	double error = 0.0;

	//sum total weighted squared differences
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];

		double diff = 0.0;
		(void) RxiPaletteFindClosestColor(reduction, palette, nColors, &entry->color[0], &diff);
		error += diff * entry->weight;

		if (error >= maxError) return maxError;
	}
	return error;
}

double RxHistComputePaletteError(RxReduction *reduction, const COLOR32 *palette, unsigned int nColors, double maxError) {
	RxYiqColor yiqPaletteStack[16];
	RxYiqColor *yiqPalette = yiqPaletteStack;
	if (nColors > 16) {
		yiqPalette = (RxYiqColor *) RxMemCalloc(nColors, sizeof(RxYiqColor));
	}

	//convert palette colors
	for (unsigned int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(palette[i], &yiqPalette[i]);
	}

	double error = RxHistComputePaletteErrorYiq(reduction, yiqPalette, nColors, maxError);

	if (yiqPalette != yiqPaletteStack) RxMemFree(yiqPalette);
	return error;
}


// ----- routines for searching palettes (unaccelerated)

static int RxiPaletteFindClosestColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, const RxYiqColor *col, double *outDiff) {
	double leastDiff = RX_LARGE_NUMBER;
	int leastIndex = 0;
	for (unsigned int i = 0; i < nColors; i++) {
		const RxYiqColor *yiq2 = &palette[i];

		double diff = RxiComputeColorDifference(reduction, col, yiq2);
		if (diff < leastDiff) {
			leastDiff = diff;
			leastIndex = i;
			if (diff == 0.0) break;
		}
	}
	if (outDiff != NULL) *outDiff = leastDiff;
	return leastIndex;
}

static int RxiPaletteFindClosestLayeredColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int palettePitch, unsigned int nColors, const RxYiqColor *col, double *outDiff) {
	RX_ASSUME(reduction->paletteLayers < palettePitch);

	double leastDiff = RX_LARGE_NUMBER;
	int leastIndex = 0;
	for (unsigned int i = 0; i < nColors; i++) {
		const RxYiqColor *yiq2 = &palette[i * palettePitch];

		double diff = 0.0;
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			diff += RxiComputeColorDifference(reduction, &col[j], &yiq2[j]);
			if (diff >= leastDiff) break;
		}

		if (diff < leastDiff) {
			leastDiff = diff;
			leastIndex = i;
			if (diff == 0.0) break;
		}
	}
	if (outDiff != NULL) *outDiff = leastDiff;
	return leastIndex;
}

static int RxiPaletteFindClosestRgbColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, COLOR32 col, double *outDiff) {
	RxYiqColor yiq;
	RxConvertRgbToYiq(col, &yiq);

	return RxiPaletteFindClosestColor(reduction, palette, nColors, &yiq, outDiff);
}



// ----- clustering code

void RxiTreeFree(RxColorNode *node, int freeThis) {
	if (node->left != NULL) {
		RxiTreeFree(node->left, TRUE);
		node->left = NULL;
	}
	if (node->right != NULL) {
		RxiTreeFree(node->right, TRUE);
		node->right = NULL;
	}
	if (freeThis) {
		RxMemFree(node);
	}
}

static int RxiTreeCountLeaves(const RxColorNode *tree) {
	if (tree->left == NULL && tree->right == NULL) return 1;

	int count = 0;
	if (tree->left != NULL) count += RxiTreeCountLeaves(tree->left);
	if (tree->right != NULL) count += RxiTreeCountLeaves(tree->right);
	return count;
}

static RxColorNode *RxiTreeFindSplittableNode(const RxColorNode *tree) {
	if (tree->left == NULL && tree->right == NULL) {
		if (tree->canSplit) return (RxColorNode *) tree;
		return NULL;
	}

	RxColorNode *leafLeft = NULL, *leafRight = NULL;
	if (tree->left != NULL) leafLeft = RxiTreeFindSplittableNode(tree->left);
	if (tree->right != NULL) leafRight = RxiTreeFindSplittableNode(tree->right);
	
	//in the cases of one or no found nodes
	if (leafLeft != NULL && leafRight == NULL) return leafLeft;
	if (leafRight != NULL && leafLeft == NULL) return leafRight;
	if (leafLeft == NULL && leafRight == NULL) return NULL;

	if (leafRight->priority >= leafLeft->priority) return leafRight;
	return leafLeft;

}

static void RxiHistComputePrincipal(RxReduction *reduction, int startIndex, int endIndex, double *axis, double *pVar) {
	//clear work
	memset(&reduction->pcaWork, 0, sizeof(reduction->pcaWork));

	double (*mtx)[4 * RX_PALETTE_MAX_COUNT] = reduction->pcaWork.cov;
	double *means = reduction->pcaWork.means;   // vector: mean of each dimension
	double *x = reduction->pcaWork.x;           // vector: temporary storage for each input vector
	double sumWeight = 0.0;

	//dimension of vectors is 4 * [number of palettes] (YIQA for each input layer)
	unsigned int dim = 4 * reduction->paletteLayers;

	//compute the covariance matrix for the input range of colors.
	for (int i = startIndex; i < endIndex; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];

		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			x[j * 4 + 0] = reduction->yWeight * entry->color[j].y;
			x[j * 4 + 1] = reduction->iWeight * entry->color[j].i;
			x[j * 4 + 2] = reduction->qWeight * entry->color[j].q;
			x[j * 4 + 3] = reduction->aWeight * entry->color[j].a;
		}

		double weight = entry->weight;

		if (reduction->alphaMode == RX_ALPHA_PALETTE) {
			//in palette alpha mode, we ignore the alpha channel when running PCA, and scale the
			//color weights by the alpha value (they did not receive this treatment during the
			//creation of the histogram).
			//TODO: how best to handle in the case where multiple palette layers make this no
			//longer mathematically valid?
			if (entry->color[0].a > 0.0f) {
				double invA = 1.0 / entry->color[0].a;
				x[0] *= invA;
				x[1] *= invA;
				x[2] *= invA;
			}
			weight *= entry->color[0].a;
			x[3] = 0.0;
		}

		//means and covariances update
		for (unsigned int j = 0; j < dim; j++) {
			//update means
			means[j] += weight * x[j];

			//update covariances (upper diagonal elements)
			for (unsigned int k = j; k < dim; k++) {
				mtx[j][k] += weight * x[j] * x[k];
			}
		}

		sumWeight += weight;
	}

	//divide sums by total weight to get means of each component
	for (unsigned int i = 0; i < dim; i++) {
		means[i] /= sumWeight;
	}

	for (unsigned int i = 0; i < dim; i++) {
		for (unsigned int j = 0; j < dim; j++) {
			if (i > j) {
				//below diagonal: mirror elements on above diagonal
				mtx[i][j] = mtx[j][i];
			} else {
				//finalize covariance calculation
				mtx[i][j] = mtx[i][j] / sumWeight - means[i] * means[j];
			}
		}
	}

	// ----- Jacobi eigenvalue calculation on the covariance matrix

	//fill identity
	double (*E)[4 * RX_PALETTE_MAX_COUNT] = reduction->pcaWork.E;
	for (unsigned int i = 0; i < dim; i++) E[i][i] = 1.0;

	double *z = reduction->pcaWork.z, *e = reduction->pcaWork.e, *b = reduction->pcaWork.b;
	memset(z, 0, dim * sizeof(double));

	for (unsigned int i = 0; i < dim; i++) {
		e[i] = mtx[i][i];
		b[i] = e[i];
	}
	
	for (unsigned int iter = 0; iter < 1000; iter++) {
		//sum above upper diagonal
		double sum = 0.0;
		for (unsigned int k = 0; k < dim - 1; k++) {
			for (unsigned int l = k + 1; l < dim; l++) {
				sum += fabs(mtx[k][l]);
			}
		}
		if (sum == 0.0) break;

		double th = 0.0;
		if (iter < 4) th = sum / (dim * dim * 5.0);

		for (unsigned int k = 0; k < dim - 1; k++) {
			for (unsigned int l = k + 1; l < dim; l++) {
				double g = 100.0 * fabs(mtx[k][l]);
				if (iter > 4 && (fabs(e[k]) + g) == fabs(e[k]) && (fabs(e[l]) + g) == fabs(e[l])) {
					mtx[k][l] = 0.0;
					continue;
				}

				//"small" values
				if (fabs(mtx[k][l]) <= th) continue;

				double d, f, h = e[l] - e[k];
				if ((fabs(h) + g) == fabs(h)) {
					d = mtx[k][l] / h;
					f = d * mtx[k][l];
				} else {
					double y = 0.5 * h / mtx[k][l];
					d = 1.0 / (fabs(y) + sqrt(1.0 + y * y));
					if (y < 0) d = -d;
					f = d * mtx[k][l];
				}

				double c2 = 1.0 / sqrt(1.0 + d * d);
				double s = d * c2;
				double c = 1.0 - s * s / (1.0 + c2);
				z[k] -= f;
				z[l] += f;
				e[k] -= f;
				e[l] += f;
				mtx[k][l] = 0.0;

				//rotations
				for (unsigned int i = 0; i < k; i++) {
					double Sik = mtx[i][k], Sil = mtx[i][l];
					mtx[i][k] = c * Sik - s * Sil;
					mtx[i][l] = s * Sik + c * Sil;
				}
				for (unsigned int i = k + 1; i < l; i++) {
					double Ski = mtx[k][i], Sil = mtx[i][l];
					mtx[k][i] = c * Ski - s * Sil;
					mtx[i][l] = s * Ski + c * Sil;
				}
				for (unsigned int i = l + 1; i < dim; i++) {
					double Ski = mtx[k][i], Sli = mtx[l][i];
					mtx[k][i] = c * Ski - s * Sli;
					mtx[l][i] = s * Ski + c * Sli;
				}

				for (unsigned int i = 0; i < dim; i++) {
					double Eik = E[i][k], Eil = E[i][l];
					E[i][k] = c * Eik - s * Eil;
					E[i][l] = s * Eik + c * Eil;
				}
			}
		}
		for (unsigned int k = 0; k < dim; k++) {
			b[k] += z[k];
			z[k] = 0.0;
			e[k] = b[k];
		}
	}

	//e now holds the eigenvalues. Negate negative one to compare magnitudes.
	for (unsigned int i = 0; i < dim; i++) {
		if (e[i] < 0.0) e[i] = -e[i];
	}

	//select the eigenvector with the greatest absolute eigenvalue.
	unsigned int eigenNo = 0;
	for (unsigned int i = 1; i < dim; i++) {
		if (e[i] > e[eigenNo]) eigenNo = i;
	}

	//return the loadings and variance of PC1
	for (unsigned int i = 0; i < dim; i++) {
		axis[i] = E[i][eigenNo];
	}
	*pVar = e[eigenNo];
}

static void RxiHistChooseSplitAxis(RxReduction *reduction, int startIndex, int endIndex, double *axis) {
	double varColor;
	RxiHistComputePrincipal(reduction, startIndex, endIndex, axis, &varColor);
	
	//if not in the palette alpha mode, do not separately consider alpha.
	if (reduction->alphaMode != RX_ALPHA_PALETTE) return;

	//compute variance of the alpha channels
	//TODO: check joint distribution of alphas
	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		//accumulate sum and sum squares
		double Sa = 0.0, Saa = 0.0, totalWeight = 0.0;
		for (int i = startIndex; i < endIndex; i++) {
			RxHistEntry *entry = reduction->histogramFlat[i];
			double a = entry->color[j].a * reduction->aWeight;

			Sa += entry->weight * a;
			Saa += entry->weight * a * a;
			totalWeight += entry->weight;
		}

		if (totalWeight > 0.0) {
			double varA = (Saa - Sa * Sa / totalWeight) / totalWeight;
			if (varA > varColor) {
				//variance of alpha dominates, so we choose to split on alpha.
				memset(axis, 0, reduction->paletteLayers * 4 * sizeof(double));
				axis[j * 4 + 3] = 1.0;  // alpha channel of layer j
			}
		}
	}
}

static int RxiHistEntryComparator(const void *p1, const void *p2) {
	const RxHistEntry *e1 = *(const RxHistEntry **) p1;
	const RxHistEntry *e2 = *(const RxHistEntry **) p2;

	double d = e1->value - e2->value;
	if (d < 0.0) return -1;
	if (d > 0.0) return 1;
	return 0;
}

static int RxiHistEntryWeightComparator(const void *p1, const void *p2) {
	const RxHistEntry *e1 = *(const RxHistEntry **) p1;
	const RxHistEntry *e2 = *(const RxHistEntry **) p2;

	double d = e2->weight - e1->weight; // descending
	if (d < 0.0) return -1;
	if (d > 0.0) return 1;
	return 0;
}

static inline double RxiVec4Mag(double x, double y, double z, double w) {
	return x * x + y * y + z * z + w * w;
}

void RxHistSort(RxReduction *reduction, int startIndex, int endIndex) {
	double principal[COV_DIM];
	RxHistEntry **thisHistogram = &reduction->histogramFlat[startIndex];
	RxiHistChooseSplitAxis(reduction, startIndex, endIndex, principal);

	//check principal component, make sure principal[0] >= 0
	if (principal[0] < 0) {
		//reversing the direction of the whole vector
		for (unsigned int i = 0; i < 4 * reduction->paletteLayers; i++) {
			principal[i] = -principal[i];
		}
	}

	//compute dot products with the split axis.
	for (int i = startIndex; i < endIndex; i++) {
		RxHistEntry *histEntry = reduction->histogramFlat[i];

		double dot = 0.0;
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			dot += histEntry->color[j].y * reduction->yWeight * principal[j * 4 + 0]
				+ histEntry->color[j].i * reduction->iWeight * principal[j * 4 + 1]
				+ histEntry->color[j].q * reduction->qWeight * principal[j * 4 + 2]
				+ histEntry->color[j].a * reduction->aWeight * principal[j * 4 + 3];
		}

		histEntry->value = dot;
	}

	//sort colors by dot product with the vector
	int nColors = endIndex - startIndex;
	qsort(thisHistogram, nColors, sizeof(RxHistEntry *), RxiHistEntryComparator);
}

unsigned int RxHistGetTopN(RxReduction *reduction, unsigned int n, RxYiqColor *cols, double *weights) {
	if (reduction->histogram == NULL) return 0; // no histogram

	//sort histogram
	qsort(reduction->histogramFlat, reduction->histogram->nEntries, sizeof(RxHistEntry *), RxiHistEntryWeightComparator);

	//get top N items
	unsigned int nGet = n;
	if (nGet > (unsigned int) reduction->histogram->nEntries) nGet = reduction->histogram->nEntries;

	for (unsigned int i = 0; i < nGet; i++) {
		if (weights != NULL) weights[i] = reduction->histogramFlat[i]->weight;
		memcpy(&cols[i], &reduction->histogramFlat[i]->color, sizeof(RxYiqColor));
	}
	return nGet;
}

static RxColorNode *RxiTreeNodeAlloc(RxReduction *reduction) {
	//allocate the node structure plus enough color entries for the number of palette layers
	RxColorNode *node = (RxColorNode *) RxMemCalloc(1, sizeof(RxColorNode) + reduction->paletteLayers * sizeof(RxYiqColor));
	return node;
}

static void RxiTreeNodeInit(RxReduction *reduction, RxColorNode *node, int startIndex, int endIndex) {
	node->startIndex = startIndex;
	node->endIndex = endIndex;
	node->canSplit = TRUE;

	//calculate the pivot index, as well as average YIQA values.
	int nColors = node->endIndex - node->startIndex;
	if (nColors < 2) {
		//1 color: set leaf color to the single histogram color and its weight
		RxHistEntry *entry = reduction->histogramFlat[node->startIndex];
		memcpy(&node->color, &entry->color, sizeof(RxYiqColor));
		node->weight = entry->weight;
		node->canSplit = FALSE;
		return;
	}

	//compute the split axis for this cluster
	double *principal = reduction->splitAxis;
	RxiHistChooseSplitAxis(reduction, node->startIndex, node->endIndex, principal);

	double projMax = -RX_LARGE_NUMBER;
	double projMin = RX_LARGE_NUMBER;
	for (int i = node->startIndex; i < node->endIndex; i++) {
		RxHistEntry *histEntry = reduction->histogramFlat[i];

		double proj = 0.0;
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			proj += histEntry->color[j].y * principal[j * 4 + 0] * reduction->yWeight
				+ histEntry->color[j].i * principal[j * 4 + 1] * reduction->iWeight
				+ histEntry->color[j].q * principal[j * 4 + 2] * reduction->qWeight
				+ histEntry->color[j].a * principal[j * 4 + 3] * reduction->aWeight;
		}
			
		histEntry->value = proj;
		if (proj > projMax) projMax = proj;
		if (proj < projMin) projMin = proj;
	}

	if (projMin == projMax) {
		node->canSplit = FALSE;
		return;
	}

	RxiLongColor *splits = (RxiLongColor *) calloc(nColors * reduction->paletteLayers, sizeof(RxiLongColor));
	double *splitWeightL = (double *) calloc(nColors, sizeof(double));
	if (splits == NULL || splitWeightL == NULL) {
		free(splits);
		free(splitWeightL);

		reduction->status = RX_STATUS_NOMEM;
		return;
	}

	//sort colors by dot product with the vector
	RxHistEntry **thisHistogram = reduction->histogramFlat + node->startIndex;
	qsort(thisHistogram, nColors, sizeof(RxHistEntry *), RxiHistEntryComparator);

	//gather statistics for splitting
	double totalWeight = 0.0;
	double sumSq = 0.0;

	//vector (sum of histogram weighted YIQA colors)
	double total[4 * RX_PALETTE_MAX_COUNT] = { 0 };   // total straight
	double totalA[3 * RX_PALETTE_MAX_COUNT] = { 0 };  // total alpha interactions

	for (int i = 0; i < nColors; i++) {
		RxHistEntry *entry = thisHistogram[i];
		double weight = entry->weight;

		//accumulate sum of squares
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			double cy = reduction->yWeight * entry->color[j].y;
			double ci = reduction->iWeight * entry->color[j].i;
			double cq = reduction->qWeight * entry->color[j].q;
			double ca = reduction->aWeight * entry->color[j].a;
			sumSq += weight * RxiVec4Mag(cy, ci, cq, ca);
		}

		//accumulate means
		RxiLongColor *split = &splits[i * reduction->paletteLayers];
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			split[j].SyU = (total[4 * j + 0] += weight * entry->color[j].y);  // accumulate Y
			split[j].SiU = (total[4 * j + 1] += weight * entry->color[j].i);  // accumulate I
			split[j].SqU = (total[4 * j + 2] += weight * entry->color[j].q);  // accumulate Q
			split[j].SaU = (total[4 * j + 3] += weight * entry->color[j].a);  // accumulate A
		}

		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			double aWeight = entry->weight * entry->color[j].a;
			totalA[j * 3 + 0] += aWeight * entry->color[j].y;
			totalA[j * 3 + 1] += aWeight * entry->color[j].i;
			totalA[j * 3 + 2] += aWeight * entry->color[j].q;
		}

		//accumulate total weight
		splitWeightL[i] = (totalWeight += weight);
	}
	node->weight = totalWeight;

	//computing representative color
	double invWeight = 1.0 / totalWeight;
	for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
		node->color[i].y = (float) (total[4 * i + 0] * invWeight);
		node->color[i].i = (float) (total[4 * i + 1] * invWeight);
		node->color[i].q = (float) (total[4 * i + 2] * invWeight);
		node->color[i].a = (float) (total[4 * i + 3] * invWeight);
	}

	//initial WSS value, which we use to calculate the WSS reduction from split
	double wssInitial = sumSq;
	for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
		wssInitial -= RxiVec4Mag(total[i * 4 + 0] * reduction->yWeight, total[i * 4 + 1] * reduction->iWeight,
			total[i * 4 + 2] * reduction->qWeight, total[i * 4 + 3] * reduction->aWeight) * invWeight;
	}

	//in alpha processing mode, we must apply the interaction terms to WSS.
	if (reduction->alphaMode == RX_ALPHA_PALETTE) {
		for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
			double meanY = node->color[i].y, meanI = node->color[i].i, meanQ = node->color[i].q, meanA = node->color[i].a;
			wssInitial -= (
				  reduction->interactionY * (totalA[3 * i + 0] - (meanY * meanA) * invWeight)
				+ reduction->interactionI * (totalA[3 * i + 1] - (meanI * meanA) * invWeight)
				+ reduction->interactionQ * (totalA[3 * i + 2] - (meanQ * meanA) * invWeight)
			);
		}
	}

	//determine pivot index based on the split that yields the best total WSS. This represents total
	//squared quantization error
	int pivotIndex = 1;
	double wssBest = RX_LARGE_NUMBER;
	for (int i = 0; i < (nColors - 1); i++) {
		RxiLongColor *entry = &splits[i * reduction->paletteLayers];
		
		double weightL = splitWeightL[i];
		double weightR = totalWeight - weightL;
		double invWeightL = 1.0 / weightL;
		double invWeightR = 1.0 / weightR;

		double SyL[RX_PALETTE_MAX_COUNT], SiL[RX_PALETTE_MAX_COUNT], SqL[RX_PALETTE_MAX_COUNT], SaL[RX_PALETTE_MAX_COUNT];
		double SyR[RX_PALETTE_MAX_COUNT], SiR[RX_PALETTE_MAX_COUNT], SqR[RX_PALETTE_MAX_COUNT], SaR[RX_PALETTE_MAX_COUNT];
		double sumSqL = 0.0, sumSqR = 0.0;
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			SyL[j] = entry[j].SyU, SyR[j] = total[4 * j + 0] - entry[j].SyU;
			SiL[j] = entry[j].SiU, SiR[j] = total[4 * j + 1] - entry[j].SiU;
			SqL[j] = entry[j].SqU, SqR[j] = total[4 * j + 2] - entry[j].SqU;
			SaL[j] = entry[j].SaU, SaR[j] = total[4 * j + 3] - entry[j].SaU;

			sumSqL += RxiVec4Mag(SyL[j] * reduction->yWeight, SiL[j] * reduction->iWeight, SqL[j] * reduction->qWeight, SaL[j] * reduction->aWeight);
			sumSqR += RxiVec4Mag(SyR[j] * reduction->yWeight, SiR[j] * reduction->iWeight, SqR[j] * reduction->qWeight, SaR[j] * reduction->aWeight);
		}

		double wss = sumSq - sumSqL * invWeightL - sumSqR * invWeightR;

		//in alpha processing mode, we must apply the intercation terms to WSS.
		if (reduction->alphaMode == RX_ALPHA_PALETTE) {
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
				wss -= (
					  reduction->interactionY * (totalA[3 * j + 0] - (SyL[j] * SaL[j] * invWeightL + SyR[j] * SaR[j] * invWeightR))
					+ reduction->interactionI * (totalA[3 * j + 1] - (SiL[j] * SaL[j] * invWeightL + SiR[j] * SaR[j] * invWeightR))
					+ reduction->interactionQ * (totalA[3 * j + 2] - (SqL[j] * SaL[j] * invWeightL + SqR[j] * SaR[j] * invWeightR))
				);
			}
		}

		//better sum of squares
		if (wss < wssBest) {
			//we'll check the mean left and mean right. They should be different with masking.

			int same = 1;
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {

				RxYiqColor yiqL, yiqR;
				yiqL.y = (float) (SyL[j] * invWeightL);
				yiqL.i = (float) (SiL[j] * invWeightL);
				yiqL.q = (float) (SqL[j] * invWeightL);
				yiqL.a = (float) (SaL[j] * invWeightL);
				yiqR.y = (float) (SyR[j] * invWeightR);
				yiqR.i = (float) (SiR[j] * invWeightR);
				yiqR.q = (float) (SqR[j] * invWeightR);
				yiqR.a = (float) (SaR[j] * invWeightR);

				COLOR32 maskL = RxiMaskYiqToRgb(reduction, &yiqL);
				COLOR32 maskR = RxiMaskYiqToRgb(reduction, &yiqR);
				if (maskL != maskR) {
					//centroids differ in at least one color
					same = 0;
					break;
				}
			}

			if (!same) {
				//accept the split only if the centroids mask two different colors (at least one color must
				//be different)
				wssBest = wss;
				pivotIndex = i + 1;
			}
		}
	}
	free(splits);
	free(splitWeightL);
	
	if (wssBest == RX_LARGE_NUMBER) {
		//any split must necessarily reduce the WSS, except for when color masking is used. If no split may be
		//made, then we mark the node as unsplittable.
		node->canSplit = FALSE;
		return;
	}

	//set pivot index
	RX_ASSUME(pivotIndex > 0 && pivotIndex < nColors);
	node->pivotIndex = node->startIndex + pivotIndex;

	//set node priority based on within-cluster sum squares reduction
	double wssReduction = wssInitial - wssBest;

	node->priority = wssReduction;
	if (!reduction->enhanceColors) {
		//moderate penalty for popular cluster
		node->priority *= sqrt(invWeight);
	}
}

static void RxiTreeSplitNode(RxReduction *reduction, RxColorNode *node) {
	if (!node->canSplit) return; // did not split
	node->canSplit = FALSE;

	RX_ASSUME(node->left == NULL && node->right == NULL);
	RX_ASSUME(node->pivotIndex > node->startIndex && node->pivotIndex < node->endIndex);

	RxColorNode *lNode = RxiTreeNodeAlloc(reduction);
	RxColorNode *rNode = RxiTreeNodeAlloc(reduction);
	if (lNode == NULL || rNode == NULL) {
		RxMemFree(lNode);
		RxMemFree(rNode);
		reduction->status = RX_STATUS_NOMEM;
		return;
	}

	//init left node
	node->left = lNode;
	RxiTreeNodeInit(reduction, lNode, node->startIndex, node->pivotIndex);

	//init right node
	node->right = rNode;
	RxiTreeNodeInit(reduction, rNode, node->pivotIndex, node->endIndex);

	reduction->nUsedColors++;
}

static RxColorNode *RxiTreeFindNodeByColor(RxReduction *reduction, const RxColorNode *treeHead, const RxColorNode *src) {
	if (treeHead == NULL || treeHead == src) return NULL;

	if (treeHead->left != NULL || treeHead->right != NULL) {
		RxColorNode *foundLeft = RxiTreeFindNodeByColor(reduction, treeHead->left, src);
		if (foundLeft != NULL) return foundLeft;

		RxColorNode *foundRight = RxiTreeFindNodeByColor(reduction, treeHead->right, src);
		return foundRight;
	}

	//is leaf, does this match?
	COLOR32 compare = RxiMaskYiqToRgb(reduction, &src->color[0]);
	COLOR32 thisRgb = RxiMaskYiqToRgb(reduction, &treeHead->color[0]);
	return (compare == thisRgb) ? (RxColorNode *) treeHead : NULL;
}

static RxColorNode *RxiTreeFindNodeByChild(const RxColorNode *treeHead, const RxColorNode *child) {
	if (treeHead == NULL) return NULL;
	if (treeHead->left == child || treeHead->right == child) return (RxColorNode *) treeHead;

	RxColorNode *foundLeft = RxiTreeFindNodeByChild(treeHead->left, child);
	if (foundLeft != NULL) return foundLeft;

	return RxiTreeFindNodeByChild(treeHead->right, child);
}

static RxColorNode *RxiTreeFindNodeByIndex(const RxColorNode *treeHead, int index) {
	if (treeHead == NULL) return NULL;
	if (treeHead->left == NULL && treeHead->right == NULL) return (RxColorNode *) treeHead;
	if (treeHead->left == NULL) return RxiTreeFindNodeByIndex(treeHead->right, index);
	if (treeHead->right == NULL) return RxiTreeFindNodeByIndex(treeHead->left, index);

	//count nodes left. If greater than index, search left, else search right
	int nodesLeft = RxiTreeCountLeaves(treeHead->left);
	if (nodesLeft > index) return RxiTreeFindNodeByIndex(treeHead->left, index);
	return RxiTreeFindNodeByIndex(treeHead->right, index - nodesLeft);
}

static void RxiTreeCleanEmptyNode(RxColorNode *treeHead, RxColorNode *node) {
	//trace up the tree clearing out empty non-leaf nodes
	RxColorNode *parent = RxiTreeFindNodeByChild(treeHead, node);
	if (parent != NULL) {
		if (parent->left == node) parent->left = NULL;
		if (parent->right == node) parent->right = NULL;
		if (parent->left == NULL && parent->right == NULL) RxiTreeCleanEmptyNode(treeHead, parent);
	}
	RxiTreeFree(node, TRUE);
}

static RxColorNode **RxiAddTreeToList(const RxColorNode *node, RxColorNode **list) {
	if (node->left == NULL && node->right == NULL) {
		//leaf node
		*(list++) = (RxColorNode *) node;
		return list;
	}

	if (node->left != NULL) list = RxiAddTreeToList(node->left, list);
	if (node->right != NULL) list = RxiAddTreeToList(node->right, list);
	return list;
}

static void RxiCreatePaletteUpdateProgress(RxReduction *reduction) {
	//weight # reclusters 64x to one palette color
	unsigned int progressMax = reduction->nPaletteColors + 64 * reduction->nReclusters;
	unsigned int progress = reduction->nUsedColors + 64 * reduction->reclusterIteration;

	RxiUpdateProgress(reduction, progress, progressMax);
}

static void RxiPaletteWrite(RxReduction *reduction) {
	if (reduction->colorTreeHead == NULL) return;

	//convert to RGB
	RxColorNode **colorBlockPtr = reduction->colorBlocks;
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RX_ASSUME(colorBlockPtr[i] != NULL);
		
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			COLOR32 rgb32 = RxiMaskYiqToRgb(reduction, &colorBlockPtr[i]->color[j]);

			//write RGB
			reduction->paletteRgb[i][j] = rgb32;

			//write YIQ (with any loss of information to RGB)
			RxConvertRgbToYiq(rgb32, &reduction->paletteYiq[i][j]);
		}
	}
}

static void RxiVoronoiAccumulateClusters(RxReduction *reduction) {
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	memset(totalsBuffer, 0, sizeof(reduction->blockTotals));

	//remap histogram points to palette colors, and accumulate the error
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];

		double bestDistance;
		int bestIndex = RxPaletteFindClosestColorYiq(reduction, &entry->color[0], &bestDistance);

		//add to total. YIQ colors scaled by alpha to be unscaled later.
		double weight = entry->weight;
		totalsBuffer[bestIndex].weight += weight;
		totalsBuffer[bestIndex].error += weight * bestDistance;
		totalsBuffer[bestIndex].count++;

		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			totalsBuffer[bestIndex].y[j] += weight * entry->color[j].y;
			totalsBuffer[bestIndex].i[j] += weight * entry->color[j].i;
			totalsBuffer[bestIndex].q[j] += weight * entry->color[j].q;
			totalsBuffer[bestIndex].a[j] += weight * entry->color[j].a;
		}
		entry->entry = bestIndex;
	}
}

static void RxiVoronoiMoveToCluster(RxReduction *reduction, RxHistEntry *entry, int idxTo, double newDifference, double oldDifference) {
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	int idxFrom = entry->entry;

	double weight = entry->weight;
	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		double y = weight * entry->color[j].y;
		double i = weight * entry->color[j].i;
		double q = weight * entry->color[j].q;
		double a = weight * entry->color[j].a;

		//add weight to "to" cluster
		totalsBuffer[idxTo].y[j] += y;
		totalsBuffer[idxTo].i[j] += i;
		totalsBuffer[idxTo].q[j] += q;
		totalsBuffer[idxTo].a[j] += a;

		//remove weight from "from" cluster
		totalsBuffer[idxFrom].y[j] -= y;
		totalsBuffer[idxFrom].i[j] -= i;
		totalsBuffer[idxFrom].q[j] -= q;
		totalsBuffer[idxFrom].a[j] -= a;
	}

	totalsBuffer[idxTo].weight += entry->weight;
	totalsBuffer[idxTo].error += newDifference;
	totalsBuffer[idxTo].count++;

	totalsBuffer[idxFrom].weight -= entry->weight;
	totalsBuffer[idxFrom].error -= oldDifference;
	totalsBuffer[idxFrom].count--;

	entry->entry = idxTo;
}

static int RxiVoronoiIterate(RxReduction *reduction) {
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;

	//load the palette into the acceleration structure
	RxPaletteLoadYiq(reduction, &reduction->paletteYiqCopy[0][0], RX_PALETTE_MAX_COUNT, reduction->nUsedColors);

	//map histogram colors to existing clusters and accumulate error.
	RxiVoronoiAccumulateClusters(reduction);

	//new centroid indexes, when created
	unsigned int *newCentroidIdxs = reduction->newCentroids;
	unsigned int nNewCentroids = 0;

	//check that every palette entry has some weight assigned to it from the previous step.
	//if any palette color would have zero weight, we assign it a color with the highest
	//squared deviation from its palette color (scaled by weight).
	//when we do this, we recompute the cluster bounds.
	int nHistEntries = reduction->histogram->nEntries;
	for (unsigned int i = reduction->nPinnedClusters; i < reduction->nUsedColors; i++) {
		if (totalsBuffer[i].weight > 0.0) continue;

		//find the color furthest from its center and create a centroid for it in place of this one.
		double largestDifference = 0.0, largestDifferenceReduction = 0.0;
		int farthestIndex = -1;
		for (int j = 0; j < nHistEntries; j++) {
			RxHistEntry *entry = reduction->histogramFlat[j];               // histogram color
			RxYiqColor *yiq1 = &reduction->paletteYiqCopy[entry->entry][0]; // ceontroid of the cluster the color belongs to

			//do not move a cluster with only one member
			if (totalsBuffer[entry->entry].count <= 1) continue;

			//if we mask colors, check this entry against the palette with clamping. If they compare equal,
			//then we say that this color is as close as it will be to a palette color and we won't include
			//this in our search candidates.
			COLOR32 histMasked = RxiMaskYiqToRgb(reduction, &entry->color[0]);
			COLOR32 palMasked = RxiMaskYiqToRgb(reduction, yiq1);
			if (histMasked == palMasked) continue; // this difference can't be reconciled

			//calculate the difference between the histogram color and its currently assigned best centroid.
			double diff = RxiComputeColorDifference(reduction, yiq1, &entry->color[0]) * entry->weight;

			//we subtract the difference to the new centroid to calcualate the reduction in the error sum of
			//squares. The highest reduction is desired.
			RxYiqColor yiqNewCentroid;
			RxConvertRgbToYiq(histMasked, &yiqNewCentroid);
			double newDifference = RxiComputeColorDifference(reduction, &entry->color[0], &yiqNewCentroid) * entry->weight;
			
			double diffReduction = diff - newDifference;
			if (diffReduction > 0.0 && diffReduction > largestDifferenceReduction) {
				//lastly, since an earlier cluster reassignment may have produced a cluster matching
				//what would be this entry's new centroid, we'll check the existing centroids and assign
				//to an existing one if it exists.
				int found = 0;
				for (unsigned int k = 0; k < nNewCentroids; k++) {
					unsigned int idx = newCentroidIdxs[k];
					//TODO: check all palette layers
					if (reduction->paletteRgbCopy[idx][0] == histMasked) {
						//remap to the existing centroid
						RxiVoronoiMoveToCluster(reduction, entry, idx, newDifference, diff);
						found = 1;
						break;
					}
				}

				//the color was not found in the reassigned centroid list, so we'll note it as a candiate for the
				//new centroid creation.
				if (!found) {
					largestDifferenceReduction = diffReduction;
					largestDifference = diff;
					farthestIndex = j;
				}
			}
		}

		if (farthestIndex != -1) {
			//get RGB of new point (will be used when checking identical remapped colors)
			RxHistEntry *entry = reduction->histogramFlat[farthestIndex];
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
				reduction->paletteRgbCopy[i][j] = RxiMaskYiqToRgb(reduction, &entry->color[j]);
				RxConvertRgbToYiq(reduction->paletteRgbCopy[i][j], &reduction->paletteYiqCopy[i][j]);
			}

			//move centroid
			double newDifference = RxiComputeLayeredColorDifference(reduction, entry->color, reduction->paletteYiqCopy[i]) * entry->weight;
			RxiVoronoiMoveToCluster(reduction, entry, i, newDifference, largestDifference);
			newCentroidIdxs[nNewCentroids++] = i;
		} else {
			//no best point was found for replacement.
			return 0; // stop
		}
	}

	//average out the colors in the new partitions
	for (unsigned int i = reduction->nPinnedClusters; i < reduction->nUsedColors; i++) {
		RxYiqColor yiq[RX_PALETTE_MAX_COUNT];
		COLOR32 as32[RX_PALETTE_MAX_COUNT];

		double invWeight = 1.0 / totalsBuffer[i].weight;
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			yiq[j].y = (float) (totalsBuffer[i].y[j] * invWeight);
			yiq[j].i = (float) (totalsBuffer[i].i[j] * invWeight);
			yiq[j].q = (float) (totalsBuffer[i].q[j] * invWeight);
			yiq[j].a = (float) (totalsBuffer[i].a[j] * invWeight);

			//mask color
			as32[j] = RxiMaskYiqToRgb(reduction, &yiq[j]);
			RxConvertRgbToYiq(as32[j], &yiq[j]);
		}

		//when color masking is used, it is possible that the new computed centroid may drift
		//from optimal placement. We will select either the new centroid or the old one, based
		//on which achieves the least error. If the old centroid achieves a better error, then
		//we do not update the centroid.
		//this ensures that the total error is at least monotonically decreasing.
		double errNewCluster = 0.0;
		for (int j = 0; j < reduction->histogram->nEntries; j++) {
			if (reduction->histogramFlat[j]->entry != i) continue;

			RxHistEntry *hist = reduction->histogramFlat[j];
			errNewCluster += hist->weight * RxiComputeLayeredColorDifference(reduction, hist->color, yiq);
		}

		//if the new cluster is an improvement over the old cluster
		if (errNewCluster < totalsBuffer[i].error) {
			memcpy(&reduction->paletteRgbCopy[i][0], as32, reduction->paletteLayers * sizeof(COLOR32));
			memcpy(&reduction->paletteYiqCopy[i][0], yiq, reduction->paletteLayers * sizeof(RxYiqColor));
		}
	}

	//load the new palette data into the accelerator
	RxPaletteLoadYiq(reduction, &reduction->paletteYiqCopy[0][0], RX_PALETTE_MAX_COUNT, reduction->nUsedColors);

	//compute new error
	double error = 0.0;
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxHistEntry *hist = reduction->histogramFlat[i];
		
		double err;
		hist->entry = RxPaletteFindClosestColorYiq(reduction, hist->color, &err);
		error += err * hist->weight;
	}

	//if the error is no longer decreasing, stop iteration
	if (error >= reduction->lastSSE) return 0; // stop

	//error check succeeded, copy this palette to the main palette.
	memcpy(reduction->paletteYiq, reduction->paletteYiqCopy, sizeof(reduction->paletteYiqCopy));
	memcpy(reduction->paletteRgb, reduction->paletteRgbCopy, sizeof(reduction->paletteRgbCopy));
	reduction->lastSSE = error;

	RxiCreatePaletteUpdateProgress(reduction);

	//if this is the last iteration, stop iterating
	if (++reduction->reclusterIteration >= reduction->nReclusters) return 0;
	return 1; // continue
}

static void RxiPaletteRecluster(RxReduction *reduction) {
	//simple termination conditions
	if (reduction->nReclusters <= 0 || reduction->nPinnedClusters >= reduction->nUsedColors) return;

	//copy main palette to palette copy
	memcpy(reduction->paletteYiqCopy, reduction->paletteYiq, sizeof(reduction->paletteYiq));
	memcpy(reduction->paletteRgbCopy, reduction->paletteRgb, sizeof(reduction->paletteRgb));

	//voronoi iteration
	reduction->reclusterIteration = 0;
	reduction->lastSSE = RX_LARGE_NUMBER;
	while (RxiVoronoiIterate(reduction));

	//delete any entries we couldn't use and shrink the palette size.
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	memset(totalsBuffer, 0, sizeof(reduction->blockTotals));
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxYiqColor *histColor = &reduction->histogramFlat[i]->color[0];

		//find nearest, add to total
		int bestIndex = RxiPaletteFindClosestLayeredColor(reduction, &reduction->paletteYiq[0][0], RX_PALETTE_MAX_COUNT,
			reduction->nUsedColors, histColor, NULL);
		totalsBuffer[bestIndex].weight += reduction->histogramFlat[i]->weight;
	}

	//weight==0 => delete
	unsigned int nRemoved = 0;
	for (unsigned int i = reduction->nPinnedClusters; i < reduction->nUsedColors; i++) {
		if (totalsBuffer[i].weight > 0) continue;

		//delete
		memmove(reduction->paletteRgb[i], reduction->paletteRgb[i + 1], (reduction->nUsedColors - i - 1) * sizeof(reduction->paletteRgb[0]));
		memmove(reduction->paletteYiq[i], reduction->paletteYiq[i + 1], (reduction->nUsedColors - i - 1) * sizeof(reduction->paletteYiq[0]));
		memmove(&totalsBuffer[i], &totalsBuffer[i + 1], (reduction->nUsedColors - i - 1) * sizeof(RxTotalBuffer));
		reduction->nUsedColors--;
		i--;
		nRemoved++;
	}

	memset(reduction->paletteRgb[reduction->nUsedColors], 0, nRemoved * sizeof(reduction->paletteRgb[0]));
	memset(reduction->paletteYiq[reduction->nUsedColors], 0, nRemoved * sizeof(reduction->paletteYiq[0]));
	RxiCreatePaletteUpdateProgress(reduction);
}

static void RxiVoronoiPinRange(RxReduction *reduction, unsigned int nCols) {
	reduction->nPinnedClusters = nCols;
}

static void RxiVoronoiUnpin(RxReduction *reduction) {
	reduction->nPinnedClusters = 0;
}

static void RxiVoronoiLoad(RxReduction *reduction, const COLOR32 *pltt, unsigned int nColors) {
	RX_ASSUME(nColors <= RX_PALETTE_MAX_SIZE);

	reduction->nPaletteColors = nColors;
	reduction->nUsedColors = nColors;

	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		const COLOR32 *thisPltt = pltt + nColors * j;

		memcpy(&reduction->paletteRgb[j * reduction->nUsedColors], thisPltt, nColors * sizeof(COLOR32));
		for (unsigned int i = 0; i < nColors; i++) RxConvertRgbToYiq(thisPltt[i], &reduction->paletteYiq[i][j]);
	}
}

static void RxiAdjustHistogramIndices(RxColorNode *tree, int cutStart, int nCut) {
	//adjust this node
	if (tree->startIndex > cutStart) {
		//when startIndex==cutStart, this node is referencing the space being deleted. Do not adjust.
		tree->startIndex -= nCut;
		tree->endIndex -= nCut;
		tree->pivotIndex -= nCut;
	}

	//adjust all children
	if (tree->left != NULL) RxiAdjustHistogramIndices(tree->left, cutStart, nCut);
	if (tree->right != NULL) RxiAdjustHistogramIndices(tree->right, cutStart, nCut);
}

static int RxiMergeTreeNodes(RxReduction *reduction, RxColorNode *treeHead) {
	if (reduction->status != RX_STATUS_OK) return 0;
	if (reduction->nUsedColors < 2) return 0; // no merge possible

	//duplicate color test
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RxColorNode *node = RxiTreeFindNodeByIndex(treeHead, i);

		int nDupMerge = 0;
		while (1) {
			//find duplicate nodes until no duplicates are found
			RxColorNode *dup = RxiTreeFindNodeByColor(reduction, treeHead, node);
			if (dup == NULL) break;

			//we should combine these two nodes into one node of combined weight.
			int start1, end1, start2, end2;
			if (node->startIndex < dup->startIndex) {
				start1 = node->startIndex, end1 = node->endIndex;
				start2 = dup->startIndex, end2 = dup->endIndex;
			} else {
				start1 = dup->startIndex, end1 = dup->endIndex;
				start2 = node->startIndex, end2 = node->endIndex;
			}

			int nCols1 = end1 - start1, nCols2 = end2 - start2;
			int nColsMove = nCols1 + nCols2;
			int nHist = reduction->histogram->nEntries;

			//free one node. If the node's parent then has no children, we clean the tree upwards.
			RxColorNode *parent = RxiTreeFindNodeByChild(treeHead, dup);
			RxiTreeFree(dup, TRUE);
			if (parent->left == dup) parent->left = NULL;
			if (parent->right == dup) parent->right = NULL;
			if (parent->right == NULL && parent->left == NULL) RxiTreeCleanEmptyNode(treeHead, parent);

			//we'll combine the histogram entries from both nodes into one. To accomplish this, we'll need to rearrange
			//the histogram array.
			int loc1 = start1, loc2 = start1 + start2 - end1, loc3 = start1 + start2 - end1 + nHist - end2, loc4 = nHist;
			RxHistEntry **tempbuf = (RxHistEntry **) calloc(nColsMove, sizeof(RxHistEntry *));
			memcpy(&tempbuf[0], &reduction->histogramFlat[start1], nCols1 * sizeof(RxHistEntry *));
			memcpy(&tempbuf[nCols1], &reduction->histogramFlat[start2], nCols2 * sizeof(RxHistEntry *));

			memmove(&reduction->histogramFlat[loc1], &reduction->histogramFlat[end1], (start2 - end1) * sizeof(RxHistEntry *));
			memmove(&reduction->histogramFlat[loc2], &reduction->histogramFlat[end2], (nHist - end2) * sizeof(RxHistEntry *));
			memcpy(&reduction->histogramFlat[loc3], tempbuf, nColsMove * sizeof(RxHistEntry *));
			free(tempbuf);

			//adjust the histogram indices of tree nodes
			RxiAdjustHistogramIndices(treeHead, start1, nCols1);
			RxiAdjustHistogramIndices(treeHead, start2 - nCols1, nCols2); // adjust starting index by amount we cut above

			//we recalculate the new combined node.
			RxiTreeNodeInit(reduction, node, loc3, loc4);
			if (reduction->status != RX_STATUS_OK) return 0; // early exit

			node->canSplit = 0; // HACK

			//fix leaves with one child to prevent the tree depth from spiraling
			//RxiTreeAdjustOneChildNodes(treeHead);

			//remove node from existence
			reduction->nUsedColors--;
			nDupMerge++;
			continue;
		}

		if (nDupMerge > 0) {
			return 1; // may be more to merge
		}
	}

	//fall-through: there was nothing to merge
	return 0;
}

RxStatus RxComputePalette(RxReduction *reduction, unsigned int nColors) {
	reduction->nPaletteColors = nColors;
	reduction->reclusterIteration = 0;
	reduction->nPinnedClusters = 0;
	RxiCreatePaletteUpdateProgress(reduction);

	if (reduction->histogramFlat == NULL || reduction->histogram->nEntries == 0) {
		reduction->nUsedColors = 0;
		return reduction->status;
	}

	//do it
	RxColorNode *treeHead = RxiTreeNodeAlloc(reduction);
	RxiTreeNodeInit(reduction, treeHead, 0, reduction->histogram->nEntries);

	reduction->colorTreeHead = treeHead;

	//main color reduction loop
	reduction->nUsedColors = 1;
	while (reduction->nUsedColors < reduction->nPaletteColors) {
		//split and initialize children for the found node.
		RxColorNode *node = RxiTreeFindSplittableNode(treeHead);
		if (node != NULL) RxiTreeSplitNode(reduction, node); // split node

		//when we would reach a termination condition, check first if any colors would be duplicates of each other.
		//this may especially happen when color masking is used, since the masked colors are not yet known.
		if (reduction->nUsedColors >= reduction->nPaletteColors || node == NULL) {
			//merge loop
			while (RxiMergeTreeNodes(reduction, treeHead));
		}
		RxiCreatePaletteUpdateProgress(reduction);

		//no more nodes to split?
		if (node == NULL || reduction->status != RX_STATUS_OK) break;
	}
	RxiCreatePaletteUpdateProgress(reduction);

	//flatten
	RxColorNode **nodep = reduction->colorBlocks;
	memset(nodep, 0, sizeof(reduction->colorBlocks));
	RxiAddTreeToList(reduction->colorTreeHead, nodep);

	//to array
	RxiPaletteWrite(reduction);

	//perform voronoi iteration
	RxiPaletteRecluster(reduction);
	return reduction->status;
}

RxStatus RxHistClear(RxReduction *reduction) {
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	reduction->histogramFlat = NULL;

	if (reduction->histogram != NULL) {
		RxiSlabFreeAll(&reduction->histogram->allocator);
		free(reduction->histogram);
		reduction->histogram = NULL;
	}

	if (reduction->colorTreeHead != NULL) RxiTreeFree(reduction->colorTreeHead, FALSE);
	RxMemFree(reduction->colorTreeHead);

	reduction->colorTreeHead = NULL;
	reduction->nUsedColors = 0;
	memset(reduction->paletteRgb, 0, sizeof(reduction->paletteRgb));
	return reduction->status = RX_STATUS_OK;
}

void RxDestroy(RxReduction *reduction) {
	RxPaletteFree(reduction);
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	if (reduction->histogram != NULL) {
		RxiSlabFreeAll(&reduction->histogram->allocator);
		free(reduction->histogram);
	}
	if (reduction->colorTreeHead != NULL) RxiTreeFree(reduction->colorTreeHead, FALSE);
	RxMemFree(reduction->colorTreeHead);
}

void RxFree(RxReduction *reduction) {
	RxDestroy(reduction);
	RxMemFree(reduction);
}

RxStatus RxCreatePalette(const COLOR32 *img, unsigned int width, unsigned int height, COLOR32 *pal, unsigned int nColors) {
	return RxCreatePaletteEx(img, width, height, pal, nColors, BALANCE_DEFAULT, BALANCE_DEFAULT, 0, 
		RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_SORT_ALL | RX_FLAG_MASK_BITS, NULL);
}

RxStatus RxCreatePaletteEx(const COLOR32 *img, unsigned int width, unsigned int height, COLOR32 *pal, unsigned int nColors, int balance, int colorBalance, int enhanceColors, RxFlag flag, unsigned int *pOutCols) {
	RxReduction *reduction = RxNew(balance, colorBalance, enhanceColors);
	if (reduction == NULL) return RX_STATUS_NOMEM;

	RxApplyFlags(reduction, flag);

	RxStatus status = RxCreatePaletteWithContext(reduction, img, width, height, pal, nColors, flag, pOutCols);
	RxFree(reduction);
	return status;
}

RxStatus RxCreatePaletteWithContext(RxReduction *reduction, const COLOR32 *px, unsigned int width, unsigned int height, COLOR32 *pal, unsigned int nColors, RxFlag flag, unsigned int *pOutCols) {
	RxHistAdd(reduction, px, width, height);
	RxHistFinalize(reduction);
	RxComputePalette(reduction, nColors);

	//copy palettes out
	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		COLOR32 *thisPltt = pal + nColors * j;

		//palette output ordering is [layer][i]
		for (unsigned int i = 0; i < nColors; i++) {
			thisPltt[i] = reduction->paletteRgb[i][j];
		}
	}

	RxStatus status = reduction->status;
	int nProduced = reduction->nUsedColors;

	if (reduction->paletteLayers < 2) {
		//TODO: sorting for higher order palettes (need to be jointly sorted)
		if (flag & RX_FLAG_SORT_ONLY_USED) {
			qsort(pal, nProduced, sizeof(COLOR32), RxColorLightnessComparator);
		} else {
			qsort(pal, nColors, sizeof(COLOR32), RxColorLightnessComparator);
		}
	}

	if (pOutCols != NULL) *pOutCols = nProduced;
	return status;
}

#define RX_TILE_PALETTE_MAX       32 //max colors in the internal work buffer
#define RX_TILE_PALETTE_COUNT_MAX 16 //max palettes produced

typedef struct RxiTile_ {
	COLOR32 rgb[64];                         // RGBA 8x8 block color
	uint8_t indices[64];                     // indices into color palette per 8x8 pixels
	RxYiqColor palette[RX_TILE_PALETTE_MAX]; // YIQ color palette
	int useCounts[RX_TILE_PALETTE_MAX];
	unsigned int palIndex;                   // points to the index of the tile that is maintaining the palette this tile uses
	unsigned int nUsedColors;                // number of filled slots
	unsigned int nSwallowed;
} RxiTile;

static void RxiTileCopy(RxiTile *dest, const COLOR32 *pxOrigin, unsigned int width) {
	for (int y = 0; y < 8; y++) {
		memcpy(dest->rgb + y * 8, pxOrigin + y * width, 8 * sizeof(COLOR32));
	}
}

static double RxiTileComputePaletteDifference(RxReduction *reduction, const RxiTile *tile1, const RxiTile *tile2) {
	//if either palette has 0 colors, return 0 (perfect fit)
	if (tile1->nUsedColors == 0 || tile2->nUsedColors == 0) return 0;

	//are the palettes identical?
	if (tile1->nUsedColors == tile2->nUsedColors && memcmp(tile1->palette, tile2->palette, tile1->nUsedColors * sizeof(tile1->palette[0])) == 0) return 0;

	//map each color from tile2 to one of tile1
	double totalDiff = 0.0;
	for (unsigned int i = 0; i < tile2->nUsedColors; i++) {
		double diff = 0.0;
		(void) RxiPaletteFindClosestColor(reduction, tile1->palette, tile1->nUsedColors, &tile2->palette[i], &diff);

		totalDiff += diff * tile2->useCounts[i];
	}
	
	//if all colors match perfectly, return 0
	if (totalDiff == 0) return 0;

	//imperfect fit. 
	unsigned int fullSize = 2 * reduction->nPaletteColors;
	if (tile1->nUsedColors + tile2->nUsedColors < fullSize) {
		//one or two palettes not full. Scale down
		totalDiff *= sqrt(((double) (tile1->nUsedColors + tile2->nUsedColors)) / fullSize);
	} else {
		//both palettes full.
	}

	//scale difference by frequency (resistance to change)
	totalDiff *= sqrt(tile1->nSwallowed + tile2->nSwallowed);

	return totalDiff;
}

static double RxiTileFindSimilarTiles(const RxiTile *tiles, const double *similarities, unsigned int nTiles, unsigned int *i1, unsigned int *i2) {
	//find a pair of tiles. Both must be representative tiles.

	double leastDiff = RX_LARGE_NUMBER;
	unsigned int best1 = 0, best2 = 1;

	for (unsigned int i = 0; i < nTiles; i++) {
		const RxiTile *tile1 = &tiles[i];
		if (tile1->palIndex != i) continue;

		for (unsigned int j = 0; j < nTiles; j++) {
			const RxiTile *tile2 = &tiles[j];

			if (tile2->palIndex != j) continue;
			if (i == j) continue;

			//test difference
			if (similarities[i * nTiles + j] <= leastDiff) {
				leastDiff = similarities[i * nTiles + j];
				best1 = i;
				best2 = j;
				if (!leastDiff) goto Done;
			}
		}
	}

Done:
	*i1 = best1;
	*i2 = best2;
	return leastDiff;
}

static COLOR32 RxiChooseMultiPaletteColor0(RxReduction *reduction) {
	RxHistFinalize(reduction);

	//get histogram colors
	RxYiqColor *histCols = (RxYiqColor *) RxMemCalloc(reduction->histogram->nEntries, sizeof(RxYiqColor));
	double *weights = (double *) calloc(reduction->histogram->nEntries, sizeof(double));
	unsigned int nCol = RxHistGetTopN(reduction, reduction->histogram->nEntries, histCols, weights);

	//weighted masked colors (bin by masking and accumulate weights)
	RxHistClear(reduction);
	RxHistInit(reduction);
	for (unsigned int i = 0; i < nCol; i++) {
		RxYiqColor maskYiq;
		RxConvertRgbToYiq(RxiMaskYiqToRgb(reduction, &histCols[i]), &maskYiq);
		RxHistAddColor(reduction, &maskYiq, weights[i]);
	}
	RxHistFinalize(reduction);

	free(weights);
	RxMemFree(histCols);

	//get binned weights
	RxYiqColor worst;
	nCol = RxHistGetTopN(reduction, 1, &worst, NULL);

	return RxiMaskYiqToRgb(reduction, &worst) | 0xFF000000;
}

void RxCreateMultiplePalettes(const COLOR32 *px, unsigned int tilesX, unsigned int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							int paletteSize, int nColsPerPalette, int paletteOffset, int *progress) {
	RxCreateMultiplePalettesEx(px, tilesX, tilesY, dest, paletteBase, nPalettes, paletteSize, nColsPerPalette, 
							 paletteOffset, 0, BALANCE_DEFAULT, BALANCE_DEFAULT, 0, progress);
}

void RxCreateMultiplePalettesEx(const COLOR32 *imgBits, unsigned int tilesX, unsigned int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							  int paletteSize, int nColsPerPalette, int paletteOffset, int useColor0,
							  int balance, int colorBalance, int enhanceColors, int *progress) {
	if (nPalettes == 0) return;

	//in the case of one palette, call to the faster single-palette routines.
	if (nPalettes == 1) {
		//create just one palette
		unsigned int effectivePaletteOffset = paletteOffset, effectivePaletteSize = nColsPerPalette;
		if (paletteOffset == 0 && !useColor0) {
			effectivePaletteOffset++;
			effectivePaletteSize--;
		}

		RxCreatePaletteEx(
			imgBits,
			tilesX * 8,
			tilesY * 8,
			dest + (paletteBase * paletteSize) + effectivePaletteOffset,
			effectivePaletteSize,
			balance,
			colorBalance,
			enhanceColors,
			RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE,
			NULL
		);
		if (paletteOffset == 0 && !useColor0) dest[(paletteBase * paletteSize)] = 0xFF00FF; // transparent fill
		return;
	}

	//if creating a palette with >= 15 colors per palette, final output colors per palette
	int nFinalColsPerPalette = nColsPerPalette;
	if (paletteOffset == 0) nFinalColsPerPalette--;
	if (nFinalColsPerPalette > paletteSize - 1) nFinalColsPerPalette = paletteSize - 1;

	//for palette sizes > 15, this algorithm will process the clustering in 15 colors only.
	if (paletteOffset == 0) nColsPerPalette--;
	if (nColsPerPalette >= RX_TILE_PALETTE_MAX) nColsPerPalette = RX_TILE_PALETTE_MAX - 1;
	
	//3 stage algorithm:
	//	1 - split into tiles
	//	2 - map similarities
	//	3 - palette merging

	// ----- STAGE 1: create tile  data

	unsigned int nTiles = tilesX * tilesY;
	RxiTile *tiles = (RxiTile *) RxMemCalloc(nTiles, sizeof(RxiTile));
	RxReduction *reduction = RxNew(balance, colorBalance, enhanceColors);

	for (unsigned int y = 0; y < tilesY; y++) {
		for (unsigned int x = 0; x < tilesX; x++) {
			RxiTile *tile = &tiles[x + (y * tilesX)];
			const COLOR32 *pxOrigin = imgBits + x * 8 + (y * 8 * tilesX * 8);
			RxiTileCopy(tile, pxOrigin, tilesX * 8);

			RxHistClear(reduction);
			RxHistAdd(reduction, tile->rgb, 8, 8);
			RxHistFinalize(reduction);
			RxComputePalette(reduction, nColsPerPalette);
			for (int i = 0; i < RX_TILE_PALETTE_MAX; i++) {
				COLOR32 col = reduction->paletteRgb[i][0];
				RxConvertRgbToYiq(col, &tile->palette[i]);
			}

			tile->nUsedColors = reduction->nUsedColors;

			//match pixels to palette indices
			for (int i = 0; i < 64; i++) {
				int index = RxiPaletteFindClosestRgbColor(reduction, &tile->palette[0], tile->nUsedColors, tile->rgb[i], NULL);
				if ((tile->rgb[i] >> 24) == 0) index = RX_TILE_PALETTE_MAX - 1;
				tile->indices[i] = (uint8_t) index;
				tile->useCounts[index]++;
			}
			tile->palIndex = x + y * tilesX;
			tile->nSwallowed = 1;
		}
	}

	// ----- STAGE 2: create difference map
	// We'll determine candidacy for palette merges using an nxn matrix of differences. Each entry
	// in the diagonal is necessarily 0 since any palette is merged with itself without cost. The
	// matrix is not symmetric, however, representing the different directions in which this relation
	// is calculated.
	double *diffBuff = (double *) calloc(nTiles * nTiles, sizeof(double));
	for (unsigned int i = 0; i < nTiles; i++) {
		RxiTile *tile1 = &tiles[i];
		for (unsigned int j = 0; j < nTiles; j++) {
			RxiTile *tile2 = &tiles[j];

			//write difference
			if (i == j) {
				diffBuff[i + j * nTiles] = 0.0;
			} else {
				diffBuff[i + j * nTiles] = RxiTileComputePaletteDifference(reduction, tile1, tile2);
			}
		}
		(*progress)++;
	}

	// ----- STAGE 3: merge palettes
	// We'll select the most highly mergeable two palettes and merge them by creating a new palette
	// using the combined histograms of represented tiles.
	int nCurrentPalettes = nTiles;
	while (nCurrentPalettes > 1) {
		//find two best palettes to merge
		unsigned int index1, index2;
		double cost = RxiTileFindSimilarTiles(tiles, diffBuff, nTiles, &index1, &index2);

		//we will continue to merge palettes even when we have are at or below the target count when
		//we may merge more palettes at 0 cost, or when there exist palettes which may be merged losslessly
		//to avoid palette waste.
		if (cost > 0.0 && (tiles[index1].nUsedColors + tiles[index2].nUsedColors) > (unsigned int) nColsPerPalette) {
			if (nCurrentPalettes <= nPalettes) break;
		}

		//find all instances of index2, replace with index1
		int nSwitched = 0;
		for (unsigned int i = 0; i < nTiles; i++) {
			if (tiles[i].palIndex == index2) {
				tiles[i].palIndex = index1;
				nSwitched++;
			}
		}

		//build new palette
		RxHistClear(reduction);
		for (unsigned int i = 0; i < nTiles; i++) {
			if (tiles[i].palIndex == index1) {
				RxHistAdd(reduction, tiles[i].rgb, 8, 8);
			}
		}
		RxHistFinalize(reduction);
		RxComputePalette(reduction, nColsPerPalette);

		//write over the palette of the tile
		RxiTile *palTile = &tiles[index1];
		for (int i = 0; i < RX_TILE_PALETTE_MAX - 1; i++) {
			RxConvertRgbToYiq(reduction->paletteRgb[i][0], &palTile->palette[i]);
		}
		palTile->nUsedColors = reduction->nUsedColors;
		palTile->nSwallowed += nSwitched;

		//get new use count
		RxiTile *rep = &tiles[index1];
		memset(rep->useCounts, 0, sizeof(rep->useCounts));
		for (unsigned int i = 0; i < nTiles; i++) {
			RxiTile *tile = &tiles[i];
			if (tile->palIndex != index1) continue;

			for (int j = 0; j < 64; j++) {
				COLOR32 col = tile->rgb[j];
				int index = RxiPaletteFindClosestRgbColor(reduction, tile->palette, tile->nUsedColors, tile->rgb[j], NULL);
				if ((col >> 24) == 0) index = RX_TILE_PALETTE_MAX - 1;
				tile->indices[j] = (uint8_t) index;
				rep->useCounts[index]++;
			}
		}

		//recompute differences for index1 and representative tiles
		for (unsigned int i = 0; i < nTiles; i++) {
			RxiTile *t = &tiles[i];
			if (t->palIndex != i) continue;

			double diff1 = RxiTileComputePaletteDifference(reduction, t, rep);
			double diff2 = RxiTileComputePaletteDifference(reduction, rep, t);
			diffBuff[i + index1 * nTiles] = diff1;
			diffBuff[index1 + i * nTiles] = diff2;
		}

		nCurrentPalettes--;
		(*progress)++;
	}

	//get palette output from previous step
	int nPalettesWritten = 0;
	int outputOffs = max(paletteOffset, 1);
	COLOR32 *palettes = (COLOR32 *) calloc(RX_TILE_PALETTE_COUNT_MAX * RX_TILE_PALETTE_MAX, sizeof(COLOR32));

	for (unsigned int i = 0; i < nTiles; i++) {
		RxiTile *t = &tiles[i];
		if (t->palIndex != i) continue;

		//rebuild palette but with masking enabled
		RxHistClear(reduction);
		for (unsigned int j = 0; j < nTiles; j++) {
			if (tiles[j].palIndex == t->palIndex) {
				RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
		}
		RxHistFinalize(reduction);
		RxComputePalette(reduction, nColsPerPalette);
		
		memcpy(palettes + nPalettesWritten * RX_TILE_PALETTE_MAX, reduction->paletteRgb, (RX_TILE_PALETTE_MAX - 1) * sizeof(COLOR32));
		nPalettesWritten++;
		(*progress)++;
	}

	//palette refinement
	int nRefinements = 8;
	int *bestPalettes = (int *) calloc(nTiles, sizeof(int));
	RxYiqColor *yiqPalette = (RxYiqColor *) RxMemCalloc(nPalettes, RX_TILE_PALETTE_MAX * sizeof(RxYiqColor));
	for (int k = 0; k < nRefinements; k++) {
		//palette to YIQ
		for (int i = 0; i < nPalettes; i++) {
			for (int j = 0; j < nColsPerPalette; j++) {
				RxConvertRgbToYiq(palettes[i * RX_TILE_PALETTE_MAX + j], &yiqPalette[i * RX_TILE_PALETTE_MAX + j]);
			}
		}

		//find best palette for each tile again
		for (unsigned int i = 0; i < nTiles; i++) {
			RxiTile *t = &tiles[i];
			COLOR32 *px = t->rgb;
			int best = 0;
			double bestError = RX_LARGE_NUMBER;

			//compute histogram for the tile
			RxHistClear(reduction);
			RxHistAdd(reduction, px, 8, 8);
			RxHistFinalize(reduction);

			//determine which palette is best for this tile for remap
			for (int j = 0; j < nPalettes; j++) {
				double error = RxHistComputePaletteErrorYiq(reduction, yiqPalette + (j * RX_TILE_PALETTE_MAX), nColsPerPalette, bestError);
				if (error < bestError) {
					bestError = error;
					best = j;
				}
			}
			bestPalettes[i] = best;
		}

		//now that we have the new best palette indices, begin regenerating the palettes
		//in a way pretty similar to before
		for (int i = 0; i < nPalettes; i++) {
			RxHistClear(reduction);
			for (unsigned int j = 0; j < nTiles; j++) {
				if (bestPalettes[j] != i) continue;
				RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
			RxHistFinalize(reduction);
			RxComputePalette(reduction, nColsPerPalette);

			//write back
			memcpy(palettes + i * RX_TILE_PALETTE_MAX, reduction->paletteRgb, nColsPerPalette * sizeof(COLOR32));
		}
	}
	RxMemFree(yiqPalette);

	//a second histogram for accumulating per-color error
	RxReduction *errHist = RxNew(balance, colorBalance, enhanceColors);
	RxHistInit(errHist);

	//write palettes in the correct size
	for (int i = 0; i < nPalettes; i++) {
		//recreate palette so that it can be output in its correct size
		COLOR32 *thisPalDest = dest + paletteSize * (i + paletteBase) + outputOffs;
		if (nFinalColsPerPalette != nColsPerPalette) {

			//palette does need to be created again
			RxHistClear(reduction);
			for (unsigned int j = 0; j < nTiles; j++) {
				if (bestPalettes[j] != i) continue;
				RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
			RxHistFinalize(reduction);
			RxComputePalette(reduction, nFinalColsPerPalette);

			//write and sort
			memcpy(thisPalDest, reduction->paletteRgb, nFinalColsPerPalette * sizeof(COLOR32));
			qsort(thisPalDest, nFinalColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
		} else {
			//already the correct size; simply sort and copy
			qsort(palettes + i * RX_TILE_PALETTE_MAX, nColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
			memcpy(thisPalDest, palettes + i * RX_TILE_PALETTE_MAX, nColsPerPalette * sizeof(COLOR32));
		}

		//accumulate error statistics
		if (useColor0) {
			//load the current palette for fast lookup
			RxPaletteLoad(errHist, thisPalDest, nColsPerPalette);
			for (unsigned int j = 0; j < nTiles; j++) {
				if (bestPalettes[j] != i) continue;

				//error for each color in the block
				for (unsigned int k = 0; k < 64; k++) {
					double diff = 0.0;
					RxPaletteFindClosestColor(errHist, tiles[j].rgb[k], &diff);

					//want only the error in excess of what may be achieved by masking
					RxYiqColor yiqCol, yiqMask;
					RxConvertRgbToYiq(tiles[j].rgb[k], &yiqCol);
					RxConvertRgbToYiq(RxiMaskYiqToRgb(reduction, &yiqCol), &yiqMask);
					diff -= RxiComputeColorDifference(reduction, &yiqCol, &yiqMask);
					if (diff < 0.0) diff = 0.0;

					//accumulate error
					RxHistAddColor(errHist, &yiqCol, diff);
				}
			}
			RxPaletteFree(errHist);
		}

		if (paletteOffset == 0) dest[(i + paletteBase) * paletteSize] = 0xFF00FF;
	}

	//if color 0 is used, select a color to be used.
	if (paletteOffset == 0 && useColor0) {
		COLOR32 col0 = RxiChooseMultiPaletteColor0(errHist);
		for (int i = 0; i < nPalettes; i++) dest[(i + paletteBase) * paletteSize] = col0;

		//with the color 0 calculated, run one last round of Voronoi reclustering on the palettes.
		for (int i = 0; i < nPalettes; i++) {
			COLOR32 *pltI = dest + (i + paletteBase) * paletteSize;
			
			//build histogram
			RxHistClear(reduction);
			for (unsigned int j = 0; j < nTiles; j++) {
				if (bestPalettes[j] == i) RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
			RxHistFinalize(reduction);

			//run reclustering on the current palette, with color 0 pinned
			RxiVoronoiLoad(reduction, pltI, nFinalColsPerPalette + 1);  // load palette
			RxiVoronoiPinRange(reduction, 1);                           // pin first color
			RxiPaletteRecluster(reduction);
			RxiVoronoiUnpin(reduction);

			memcpy(pltI, reduction->paletteRgb, (nFinalColsPerPalette + 1) * sizeof(COLOR32));
			qsort(pltI + 1, nFinalColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
		}
	}

	free(palettes);
	free(bestPalettes);
	RxFree(errHist);
	RxFree(reduction);
	RxMemFree(tiles);
	free(diffBuff);
}

static inline double RxiDiffuseCurveY(double x) {
	if (x < 0.0) return -RxiDiffuseCurveY(-x);
	if (x <= 8.0) return x;
	return 8.0 + pow(x - 8.0, 0.9) * 0.94140625;
}

static inline double RxiDiffuseCurveI(double x) {
	if (x < 0.0) return -RxiDiffuseCurveI(-x);
	if (x <= 8.0) return x;
	return 8.0 + pow(x - 8.0, 0.85) * 0.98828125;
}

static inline double RxiDiffuseCurveQ(double x) {
	if (x < 0.0) return -RxiDiffuseCurveQ(-x);
	if (x <= 8.0) return x;
	return 8.0 + pow(x - 8.0, 0.85) * 0.89453125;
}

static inline double RxiDiffuseCurveA(double x) {
	return RxiDiffuseCurveY(x * 511.0) * INV_511;
}

RxStatus RxReduceImage(COLOR32 *px, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int nColors, float diffuse) {
	return RxReduceImageEx(px, NULL, width, height, palette, nColors, RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_PRESERVE_ALPHA,
		diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

RxStatus RxReduceImageEx(COLOR32 *img, int *indices, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int nColors, RxFlag flag, float diffuse, int balance, int colorBalance, int enhanceColors) {
	RxReduction *reduction = RxNew(balance, colorBalance, enhanceColors);
	if (reduction == NULL) return RX_STATUS_NOMEM;
	
	RxApplyFlags(reduction, flag);

	RxStatus status = RxReduceImageWithContext(reduction, img, indices, width, height, palette, nColors, flag, diffuse);
	RxFree(reduction);

	return status;
}

RxStatus RxReduceImageWithContext(RxReduction *reduction, COLOR32 *img, int *indices, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int nColors, RxFlag flag, float diffuse) {
	//decode flags
	RxFlag alphaMode = flag & RX_FLAG_ALPHA_MODE_MASK;
	int binaryAlpha = (alphaMode == RX_FLAG_ALPHA_MODE_NONE) || (alphaMode == RX_FLAG_ALPHA_MODE_RESERVE);
	int touchAlpha = (flag & RX_FLAG_NO_PRESERVE_ALPHA);
	int adaptive = !(flag & RX_FLAG_NO_ADAPTIVE_DIFFUSE);

	//initial progress
	RxiUpdateProgress(reduction, 0, height);

	//load palette into context
	RxStatus status = RxPaletteLoad(reduction, palette, nColors);
	if (status != RX_STATUS_OK) return status;

	RxYiqColor *rowbuf = (RxYiqColor *) RxMemCalloc(4 * (width + 2), sizeof(RxYiqColor));
	if (rowbuf == NULL) {
		//no memory
		RxPaletteFree(reduction);
		RxMemFree(rowbuf);
		return RX_STATUS_NOMEM;
	}

	//allocate row buffers for color and diffuse.
	RxYiqColor *thisRow = rowbuf;
	RxYiqColor *lastRow = thisRow + (width + 2);
	RxYiqColor *thisDiffuse = lastRow + (width + 2);
	RxYiqColor *nextDiffuse = thisDiffuse + (width + 2);

	//fill the last row with the first row, just to make sure we don't run out of bounds
	for (unsigned int i = 0; i < width; i++) {
		RxConvertRgbToYiq(img[i], &lastRow[i + 1]);
	}
	memcpy(lastRow, lastRow + 1, sizeof(RxYiqColor));
	memcpy(lastRow + (width + 1), lastRow + width, sizeof(RxYiqColor));

	//start dithering, do so in a serpentine path.
	for (unsigned int y = 0; y < height; y++) {

		//which direction?
		int hDirection = (y & 1) ? -1 : 1;
		COLOR32 *rgbRow = img + y * width;
		for (unsigned int x = 0; x < width; x++) {
			RxConvertRgbToYiq(rgbRow[x], &thisRow[x + 1]);
		}
		memcpy(&thisRow[0], &thisRow[1], sizeof(RxYiqColor));
		memcpy(&thisRow[width + 1], &thisRow[width], sizeof(RxYiqColor));

		//scan across
		unsigned int startPos = (hDirection == 1) ? 0 : (width - 1);
		unsigned int x = startPos;
		for (unsigned int xPx = 0; xPx < width; xPx++) {
			//take a sample of pixels nearby. This will be a gauge of variance around this pixel, and help
			//determine if dithering should happen. Weight the sampled pixels with respect to distance from center.

			RxYiqColor colorYiq;
			if (adaptive) {
#ifndef RX_SIMD
				colorYiq.y = (thisRow[x + 1].y + thisRow[x + 2].y + thisRow[x].y + lastRow[x + 1].y) * 0.1875f + (lastRow[x].y + lastRow[x + 2].y) * 0.125f;
				colorYiq.i = (thisRow[x + 1].i + thisRow[x + 2].i + thisRow[x].i + lastRow[x + 1].i) * 0.1875f + (lastRow[x].i + lastRow[x + 2].i) * 0.125f;
				colorYiq.q = (thisRow[x + 1].q + thisRow[x + 2].q + thisRow[x].q + lastRow[x + 1].q) * 0.1875f + (lastRow[x].q + lastRow[x + 2].q) * 0.125f;
				colorYiq.a = (thisRow[x + 1].a + thisRow[x + 2].a + thisRow[x].a + lastRow[x + 1].a) * 0.1875f + (lastRow[x].a + lastRow[x + 2].a) * 0.125f;
#else
				__m128 vec1 = _mm_add_ps(_mm_add_ps(thisRow[x + 1].yiq, thisRow[x + 2].yiq), _mm_add_ps(thisRow[x].yiq, lastRow[x + 1].yiq));
				__m128 vec2 = _mm_add_ps(lastRow[x].yiq, lastRow[x + 2].yiq);

				colorYiq.yiq = _mm_add_ps(_mm_mul_ps(vec1, _mm_set1_ps(0.1875f)), _mm_mul_ps(vec2, _mm_set1_ps(0.125f)));
#endif
			} else {
				//no adaptive diffuse -> no local noise checking
				memcpy(&colorYiq, &thisRow[x + 1], sizeof(RxYiqColor));
			}

			//match it to a palette color. We'll measure distance to it as well.
			double paletteDistance = 0.0;
			int matched = RxPaletteFindClosestColorYiq(reduction, &colorYiq, &paletteDistance);

			//now measure distance from the actual color to its average surroundings
			RxYiqColor *centerYiq = &thisRow[x + 1];
			double centerDistance = RxiComputeColorDifference(reduction, centerYiq, &colorYiq);

			//now test: Should we dither?
			double yw2 = reduction->yWeight2;
			if (diffuse > 0.0f && (!adaptive || (centerDistance < 110.0 * yw2 && paletteDistance >  2.0 * yw2))) {

				RxYiqColor diffuseVec;
				diffuseVec.y = thisDiffuse[x + 1].y * diffuse;
				diffuseVec.i = thisDiffuse[x + 1].i * diffuse;
				diffuseVec.q = thisDiffuse[x + 1].q * diffuse;
				diffuseVec.a = thisDiffuse[x + 1].a * diffuse;
				if (adaptive) {
					diffuseVec.y = (float) RxiDiffuseCurveY(diffuseVec.y);
					diffuseVec.i = (float) RxiDiffuseCurveI(diffuseVec.i);
					diffuseVec.q = (float) RxiDiffuseCurveQ(diffuseVec.q);
					diffuseVec.a = (float) RxiDiffuseCurveA(diffuseVec.a);
				}

				if (flag & RX_FLAG_NO_ALPHA_DITHER) {
					//diffuse into the current color. We must unmultiply and remultiply by alpha.
					if (colorYiq.a != 0.0f) {
						float aFactor = 1.0f + diffuseVec.a / colorYiq.a;
						colorYiq.y *= aFactor;
						colorYiq.i *= aFactor;
						colorYiq.q *= aFactor;
					}

					float colorA2 = colorYiq.a + diffuseVec.a;
					diffuseVec.y *= colorA2;
					diffuseVec.i *= colorA2;
					diffuseVec.q *= colorA2;
					diffuseVec.a = 0.0f;
				} else {
					//alpha dithering is enabled, so we diffuse directly without adjustment.
				}

				//apply the diffusion
#ifndef RX_SIMD
				colorYiq.y += diffuseVec.y;
				colorYiq.i += diffuseVec.i;
				colorYiq.q += diffuseVec.q;
				colorYiq.a += diffuseVec.a;
#else
				colorYiq.yiq = _mm_add_ps(colorYiq.yiq, diffuseVec.yiq);
#endif

				if (colorYiq.a < 0.0f) {
					//normalize to alpha=0
					RxiColorMakeTransparent(&colorYiq);
				} else {
					//clamp Y channel
					if (colorYiq.y < 0.0f) {
						colorYiq.y = 0.0f;
						colorYiq.i = 0.0f;
						colorYiq.q = 0.0f;
					} else if (colorYiq.y > 511.0f * colorYiq.a) {
						colorYiq.y = 511.0f * colorYiq.a;
						colorYiq.i = 0.0f;
						colorYiq.q = 0.0f;
					}

					if (colorYiq.a > 1.0f) {
						//normalize to alpha=1
						RxiColorMakeOpaque(&colorYiq);
					}
				}

				//match to palette color
				matched = RxPaletteFindClosestColorYiq(reduction, &colorYiq, NULL);
				RxYiqColor *chosenYiq = &reduction->accel.plttLarge[matched];

				//now diffuse to neighbors (mirrored with the scan direction):
				//        X  7/16
				// 3/16 5/16 1/16
				RxYiqColor *diffuse21 = &thisDiffuse[x + 1 + hDirection];
				RxYiqColor *diffuse12 = &nextDiffuse[x + 1];
				RxYiqColor *diffuse22 = &nextDiffuse[x + 1 + hDirection];
				RxYiqColor *diffuse02 = &nextDiffuse[x + 1 - hDirection];

				RxYiqColor off;
				if (flag & RX_FLAG_NO_ALPHA_DITHER) {
					//alpha is not dithered, so we un-premultiply the colors and scale to palette alpha.
					if (colorYiq.a > 0.0f) {
						float chosenA = chosenYiq->a;
						off.y = colorYiq.y * chosenA / colorYiq.a - chosenYiq->y;
						off.i = colorYiq.i * chosenA / colorYiq.a - chosenYiq->i;
						off.q = colorYiq.q * chosenA / colorYiq.a - chosenYiq->q;
						off.a = 0.0f;
					} else {
						//zero alpha, no color information to dither.
						RxiColorMakeTransparent(&off);
					}
				} else {
					//alpha is dithered, so we take the straight preultiplied difference to diffuse
					//signal intensity.
					off.y = colorYiq.y - chosenYiq->y;
					off.i = colorYiq.i - chosenYiq->i;
					off.q = colorYiq.q - chosenYiq->q;
					off.a = colorYiq.a - chosenYiq->a;
				}

#ifndef RX_SIMD
				diffuse21->y += off.y * 0.4375f; // 7/16
				diffuse21->i += off.i * 0.4375f;
				diffuse21->q += off.q * 0.4375f;
				diffuse21->a += off.a * 0.4375f;
				diffuse12->y += off.y * 0.3125f; // 5/16
				diffuse12->i += off.i * 0.3125f;
				diffuse12->q += off.q * 0.3125f;
				diffuse12->a += off.a * 0.3125f;
				diffuse02->y += off.y * 0.1875f; // 3/16
				diffuse02->i += off.i * 0.1875f;
				diffuse02->q += off.q * 0.1875f;
				diffuse02->a += off.a * 0.1875f;
				diffuse22->y += off.y * 0.0625f; // 1/16
				diffuse22->i += off.i * 0.0625f;
				diffuse22->q += off.q * 0.0625f;
				diffuse22->a += off.a * 0.0625f;
#else
				diffuse21->yiq = _mm_add_ps(diffuse21->yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.4375f))); // 7/16
				diffuse12->yiq = _mm_add_ps(diffuse12->yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.3125f))); // 5/16
				diffuse02->yiq = _mm_add_ps(diffuse02->yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.1875f))); // 3/16
				diffuse22->yiq = _mm_add_ps(diffuse22->yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.0625f))); // 1/16
#endif
			} else {
				//high noise area or dithering disabled, do not diffuse
				matched = RxPaletteFindClosestColorYiq(reduction, centerYiq, NULL);
			}

			//put pixel
			if (!(flag & RX_FLAG_NO_WRITEBACK)) {
				COLOR32 chosen = palette[matched];
				if (touchAlpha) img[x + y * width] = chosen;
				else img[x + y * width] = (chosen & 0x00FFFFFF) | (img[x + y * width] & 0xFF000000);
			}
			if (indices != NULL) indices[x + y * width] = matched;

			x += hDirection;
		}

		//swap row buffers
		RxYiqColor *temp = thisRow;
		thisRow = lastRow;
		lastRow = temp;
		temp = nextDiffuse;
		nextDiffuse = thisDiffuse;
		thisDiffuse = temp;
		memset(nextDiffuse, 0, (width + 2) * sizeof(RxYiqColor));
		RxiUpdateProgress(reduction, y + 1, height);
	}

	RxPaletteFree(reduction);
	RxMemFree(rowbuf);
	return RX_STATUS_OK;
}

static inline double RxiAccelGetChannelN(RxReduction *reduction, const RxYiqColor *color, unsigned int n) {
	RX_ASSUME(n < 4 * reduction->paletteLayers);

	switch (n % 4) {
		case 0: return reduction->yWeight * color[n / 4].y;
		case 1: return reduction->iWeight * color[n / 4].i;
		case 2: return reduction->qWeight * color[n / 4].q;
		case 3: return reduction->aWeight * color[n / 4].a;
	}
	return 0.0;
}

static int RxiAccelSortPalette(const void *p1, const void *p2) {
	const RxPaletteMapEntry *e1 = (const RxPaletteMapEntry *) p1;
	const RxPaletteMapEntry *e2 = (const RxPaletteMapEntry *) p2;
	if (e1->sortVal < e2->sortVal) return -1;
	if (e1->sortVal > e2->sortVal) return 1;
	return 0;
}

static RxPaletteAccelNode *RxiAccelSplit(RxReduction *reduction, RxPaletteAccelNode *accel, RxPaletteAccelNode *nodebuf, RxPaletteMapEntry *plttFull, unsigned int nextSplit) {
	RX_ASSUME(accel->nCol > 0);

	RxPaletteMapEntry *pltt = plttFull + accel->start;

	//we split the node if it has more than one node.
	accel->mid = &pltt[0]; // set to first color in the trvial/degenerate case
	if (accel->nCol <= 1) {
		//put split values for the node
		accel->splitDir = nextSplit;
		accel->splitVal = RxiAccelGetChannelN(reduction, accel->mid->color, nextSplit);
		return nodebuf;
	}

	//sort by
	unsigned int nChannel = 4 * reduction->paletteLayers;
	for (unsigned int i = 0; i < nChannel; i++) {
		//skip alpha channels (cannot be used in the K-D tree)
		if ((i % 4) == 3) continue;

		//test sort
		for (unsigned int i = 0; i < accel->nCol; i++) {
			pltt[i].sortVal = RxiAccelGetChannelN(reduction, pltt[i].color, nextSplit);
		}
		qsort(pltt, accel->nCol, sizeof(RxPaletteMapEntry), RxiAccelSortPalette);

		//split test
		double f1 = pltt[0].sortVal;
		double f2 = pltt[accel->nCol - 1].sortVal;
		if (f1 != f2) break;

		//else
		nextSplit = (nextSplit + 1) % nChannel;
		if (i == 2) return nodebuf; // not split (all axes degenerate)
	}

	//find split (subtract 1 for the median color)
	unsigned int iSplit = (accel->nCol - 1) / 2;

	//given the chance of identical sorting values at the median point, we bucket those values
	//equal to the median in the greater bucket. This allows exact matches to be searched
	//more quickly.
	double medVal = pltt[iSplit].sortVal;
	for (unsigned int i = 0; i < accel->nCol; i++) {
		if (pltt[i].sortVal == medVal) {
			iSplit = i;
			break;
		}
	}

	unsigned int nLeft = iSplit;
	unsigned int nRight = accel->nCol - nLeft - 1;
	accel->mid = pltt + iSplit;

	if (nLeft > 0) {
		RxPaletteAccelNode *childL = nodebuf++;
		childL->start = accel->start;
		childL->nCol = nLeft;
		childL->parent = accel;
		accel->pLeft = childL;
		nodebuf = RxiAccelSplit(reduction, childL, nodebuf, plttFull, (nextSplit + 1) % nChannel);
	}

	if (nRight > 0) {
		RxPaletteAccelNode *childR = nodebuf++;
		childR->start = accel->start + iSplit + 1;
		childR->nCol = nRight;
		childR->parent = accel;
		accel->pRight = childR;
		nodebuf = RxiAccelSplit(reduction, childR, nodebuf, plttFull, (nextSplit + 1) % nChannel);
	}

	//put split
	accel->splitDir = nextSplit;
	accel->splitVal = medVal;
	return nodebuf;
}

static void RxiAccelRecurseTreeInternal(RxReduction *reduction, RxPaletteAccelNode *accel, const RxYiqColor *color, double *pBestDiff, unsigned int *piBest) {
	//distance of color to the root node
	int intersectPlane = 0;
	double projColor = RxiAccelGetChannelN(reduction, color, accel->splitDir);
	double diffFromSplit = accel->splitVal - projColor;
	diffFromSplit *= diffFromSplit;

	if (diffFromSplit < *pBestDiff) {
		intersectPlane = 1;

		double diff = RxiComputeLayeredColorDifference(reduction, color, accel->mid->color);
		if (diff < *pBestDiff) {
			*pBestDiff = diff;
			*piBest = accel->mid->index;
		}
	}

	//based on the difference from the split, we may only need to search one child.
	//left/right nodes, if they exist, and are within the search space
	RxPaletteAccelNode *nodeL = accel->pLeft;
	if (nodeL != NULL) {
		//left node: lesser values (search only if the splitting plane is greater)
		if (intersectPlane || projColor <= accel->splitVal) {
			RxiAccelRecurseTreeInternal(reduction, nodeL, color, pBestDiff, piBest);
		}
	}

	RxPaletteAccelNode *nodeR = accel->pRight;
	if (nodeR != NULL) {
		//right node: greater values (seeach only if the splitting plane is lesser)
		if (intersectPlane || projColor >= accel->splitVal) {
			RxiAccelRecurseTreeInternal(reduction, nodeR, color, pBestDiff, piBest);
		}
	}
}

static void RxiAccelRecurseTree(RxReduction *reduction, RxPaletteAccelNode *accel, const RxYiqColor *color, double *pBestDiff, unsigned int *piBest, int lrbit) {
	//test the root and then recurse down (only the half not explored)
	double diffFromSplit = accel->splitVal - RxiAccelGetChannelN(reduction, color, accel->splitDir);
	diffFromSplit *= diffFromSplit;

	//the axis-aligned difference to split (i.e. difference to cutting plane) is within the search
	//radius, thus we must search the other sub-tree.
	if (diffFromSplit < *pBestDiff) {
		//because the plane is within the search radius, its point must be checked too.
		double diff = RxiComputeLayeredColorDifference(reduction, color, accel->mid->color);
		if (diff < *pBestDiff) {
			*pBestDiff = diff;
			*piBest = accel->mid->index;
		}

		//choose the sub tree opposite the way we came, search down
		RxPaletteAccelNode *sub = lrbit ? accel->pLeft : accel->pRight;
		if (sub != NULL) RxiAccelRecurseTreeInternal(reduction, sub, color, pBestDiff, piBest);
	}
}

static unsigned int RxiPaletteFindClosestColorAccelerated(RxReduction *reduction, const RxYiqColor *color, double *outDiff) {
	RxPaletteAccelerator *accel = &reduction->accel;
	if (!accel->initialized) {
		//not initialized
		if (outDiff != NULL) *outDiff = RX_LARGE_NUMBER;
		return 0;
	}

	//traverse down
	RxPaletteAccelNode *nodep = &accel->root;
	while (1) {
		double split = nodep->splitVal;
		double val = RxiAccelGetChannelN(reduction, color, nodep->splitDir);
		RxYiqColor *nodeCol = nodep->mid->color;

		//if this is a leaf node or the split value matches, check for a matching color
		if ((nodep->pLeft == NULL && nodep->pRight == NULL) || (split == val)) {
			//compare color
			if (memcmp(nodeCol, color, sizeof(RxYiqColor)) == 0) {
				if (outDiff != NULL) *outDiff = 0.0; // identical match
				return nodep->mid->index;
			}
		}

		//no child nodes, no check split
		if (nodep->pLeft == NULL && nodep->pRight == NULL) break;

		//choose left/right. We choose the only available path if only one (to ensure we reach the tree's bottom)
		if (nodep->pLeft == NULL) nodep = nodep->pRight;
		else if (nodep->pRight == NULL) nodep = nodep->pLeft;
		else if (val < split) nodep = nodep->pLeft;
		else nodep = nodep->pRight;
	}

	//we're at the bottom of the tree, and there were no exact matches. We recurse the tree
	//to find closer candidate points.
	unsigned int iBest = nodep->mid->index;
	double bestDiff = RxiComputeLayeredColorDifference(reduction, color, nodep->mid->color);

	//traverse up
	while (nodep->parent != NULL) {
		int lrbit = (nodep == nodep->parent->pRight); // index of child, to avoid recursing the tree we've already checked
		nodep = nodep->parent;
		RxiAccelRecurseTree(reduction, nodep, color, &bestDiff, &iBest, lrbit);
	}

	//best index
	if (outDiff != NULL) *outDiff = bestDiff;
	return iBest;
}

unsigned int RxPaletteFindClosestColor(RxReduction *reduction, COLOR32 color, double *outDiff) {
	RxYiqColor yiq;
	RxConvertRgbToYiq(color, &yiq);
	return RxPaletteFindClosestColorYiq(reduction, &yiq, outDiff);
}

unsigned int RxPaletteFindClosestColorYiq(RxReduction *reduction, const RxYiqColor *color, double *outDiff) {
	RxPaletteAccelerator *accel = &reduction->accel;
	if (!accel->initialized) {
		//not initialized
		if (outDiff != NULL) *outDiff = RX_LARGE_NUMBER;
		return 0;
	}

	RxYiqColor cpy[RX_PALETTE_MAX_COUNT];
	memcpy(cpy, color, reduction->paletteLayers * sizeof(RxYiqColor));

	//processing for alpha mode
	unsigned int plttStart = 0;
	switch (reduction->alphaMode) {
		case RX_ALPHA_PIXEL:
		{
			for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
				//pixel-mode alpha: do not consider alpha in calculation, so rescale all colors
				//to full alpha
				RxiColorMakeOpaque(&cpy[i]);
			}
			break;
		}
		case RX_ALPHA_RESERVE:
		{
			plttStart = 1;

			for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
				//alpha-reserve mode cannot cleanly support multiple alpha, but we will consider
				//any color below the threshold to be transparent.
				if (cpy[i].a < reduction->fAlphaThreshold) {
					if (outDiff != NULL) *outDiff = 0.0;
					return 0; // transparent reserve
				}
				RxiColorMakeOpaque(&cpy[i]);
			}
			break;
		}
		default:
			break;
	}

	if (accel->useAccelerator) {
		//accelerated search
		return RxiPaletteFindClosestColorAccelerated(reduction, cpy, outDiff) + plttStart;
	} else {
		//slow search
		return RxiPaletteFindClosestColor(reduction, accel->plttLarge + plttStart, accel->nPltt - plttStart, cpy, outDiff) + plttStart;
	}
}

static RxStatus RxiPaletteAlloc(RxReduction *reduction, unsigned int nCol) {
	RxPaletteAccelerator *accel = &reduction->accel;
	RX_ASSUME(accel->plttLarge == NULL);

	if (nCol > sizeof(accel->plttSmall) / sizeof(accel->plttSmall[0])) {
		//above small threshold --> allocate on the heap
		accel->plttLarge = (RxYiqColor *) RxMemCalloc(nCol * reduction->paletteLayers, sizeof(RxYiqColor));
	} else {
		//within small threshold --> use small buffer
		accel->plttLarge = accel->plttSmall;
	}

	if (accel->plttLarge == NULL) reduction->status = RX_STATUS_NOMEM;
	else accel->nPltt = nCol;
	return reduction->status;
}

static RxStatus RxiPaletteLoadAccelerated(RxReduction *reduction) {
	//the K-D tree is incompatible with the palette with palette alpha.
	RxAlphaMode alphaMode = reduction->alphaMode;
	if (alphaMode == RX_ALPHA_PALETTE) return RX_STATUS_INVALID;

	unsigned int iStart = 0;
	if (alphaMode == RX_ALPHA_RESERVE) iStart = 1; // skip 1st color in reserve mode

	RxPaletteAccelerator *accel = &reduction->accel;
	RxYiqColor *pltt = accel->plttLarge;
	unsigned int nColors = accel->nPltt;
	if (nColors > 0) {
		nColors -= iStart;
		pltt += iStart;
	}

	if (nColors == 0) return RX_STATUS_INVALID; // empty palette

	if (alphaMode != RX_ALPHA_PIXEL) {
		//in the per-pixel alpha mode, we'll force all palette alpha values to full. Otherwise, we use
		//the alpha from the palette and must check it for validity.
		for (unsigned int i = 0; i < nColors; i++) {
			float a = pltt[i].a;
			if (a < 1.0f) return RX_STATUS_INVALID;
		}
	}

	//working memory for accelerator
	accel->pltt = (RxPaletteMapEntry *) RxMemCalloc(nColors, sizeof(RxPaletteMapEntry));
	accel->nodebuf = (RxPaletteAccelNode *) calloc(nColors, sizeof(RxPaletteAccelNode));

	if (accel->pltt == NULL || accel->nodebuf == NULL) {
		//no memory
		free(accel->pltt);
		free(accel->nodebuf);
		return reduction->status = RX_STATUS_NOMEM;
	}

	for (unsigned int i = 0; i < nColors; i++) {
		memcpy(&accel->pltt[i].color, &pltt[i], sizeof(RxYiqColor));
		accel->pltt[i].index = i;

		if (alphaMode == RX_ALPHA_PIXEL) {
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
				RxiColorMakeOpaque(&accel->pltt[i].color[j]);
			}
		}
	}

	accel->useAccelerator = 1;
	accel->root.parent = NULL;
	accel->root.start = 0;
	accel->root.nCol = nColors;
	accel->root.pLeft = NULL;
	accel->root.pRight = NULL;
	RxiAccelSplit(reduction, &accel->root, accel->nodebuf, accel->pltt, 0);

	return RX_STATUS_OK;
}

static RxStatus RxiPaletteLoadUnaccelerated(RxReduction *reduction, const COLOR32 *pltt, unsigned int nColors) {
	RxStatus status = RxiPaletteAlloc(reduction, nColors);
	if (status != RX_STATUS_OK) return reduction->status = status;

	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		for (unsigned int i = 0; i < nColors; i++) {
			RxConvertRgbToYiq(pltt[j * nColors + i], &reduction->accel.plttLarge[i * reduction->paletteLayers + j]);
		}
	}

	return reduction->status;
}

static RxStatus RxiPaletteLoadYiqUnaccelerated(RxReduction *reduction, const RxYiqColor *pltt, unsigned int nColors, unsigned int srcPitch) {
	RxStatus status = RxiPaletteAlloc(reduction, nColors);
	if (status != RX_STATUS_OK) return reduction->status = status;

	unsigned int nLayer = reduction->paletteLayers;
	for (unsigned int i = 0; i < nColors; i++) {
		memcpy(&reduction->accel.plttLarge[i * nLayer], &pltt[i * srcPitch], nLayer * sizeof(RxYiqColor));
	}

	return reduction->status;
}

RxStatus RxPaletteLoad(RxReduction *reduction, const COLOR32 *pltt, unsigned int nColors) {
	RxPaletteAccelerator *accel = &reduction->accel;

	//if an accelerator is loaded already, unload it.
	RxPaletteFree(reduction);

	//in all cases, we load without the accelerator first
	RxStatus status = RxiPaletteLoadUnaccelerated(reduction, pltt, nColors);
	if (status != RX_STATUS_OK) return reduction->status = status;

	if (nColors > 16) {
		//number of colors is high enough to benefit from acceleration
		RxiPaletteLoadAccelerated(reduction);
	}

	accel->initialized = 1;
	return reduction->status;
}

static RxStatus RxPaletteLoadYiq(RxReduction *reduction, const RxYiqColor *pltt, unsigned int srcPitch, unsigned int nColors) {
	RxPaletteAccelerator *accel = &reduction->accel;

	//if an accelerator is loaded already, unload it.
	RxPaletteFree(reduction);

	//in all cases, we load without the accelerator first
	RxStatus status = RxiPaletteLoadYiqUnaccelerated(reduction, pltt, nColors, srcPitch);
	if (status != RX_STATUS_OK) return reduction->status = status;

	if (nColors > 16) {
		//number of colors is high enough to benefit from acceleration
		RxiPaletteLoadAccelerated(reduction);
	}

	accel->initialized = 1;
	return reduction->status;
}

void RxPaletteFree(RxReduction *reduction) {
	if (!reduction->accel.initialized) return;

	RxMemFree(reduction->accel.pltt);
	if (reduction->accel.plttLarge != reduction->accel.plttSmall) RxMemFree(reduction->accel.plttLarge);
	free(reduction->accel.nodebuf);
	memset(&reduction->accel, 0, sizeof(reduction->accel));
	reduction->accel.initialized = 0;
}

double RxComputePaletteError(RxReduction *reduction, const COLOR32 *px, unsigned int width, unsigned int height, const COLOR32 *pal, unsigned int nColors, double nMaxError) {
	if (nMaxError == 0) nMaxError = RX_LARGE_NUMBER;
	double error = 0;

	RxYiqColor paletteYiqStack[16]; //small palettes
	RxYiqColor *paletteYiq = paletteYiqStack;
	if (nColors > 16) {
		paletteYiq = (RxYiqColor *) RxMemCalloc(nColors, sizeof(RxYiqColor));
	}

	//palette to YIQ
	for (unsigned int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(pal[i], &paletteYiq[i]);
	}

	for (unsigned int i = 0; i < (width * height); i++) {
		COLOR32 p = px[i];
		unsigned int a = (p >> 24) & 0xFF;
		if (a < 0x80) continue;
		p |= 0xFF000000;

		RxYiqColor yiq;
		RxConvertRgbToYiq(px[i], &yiq);
		double bestDiff;
		(void) RxiPaletteFindClosestColor(reduction, paletteYiq, nColors, &yiq, &bestDiff);

		error += bestDiff;
		if (error >= nMaxError) {
			if (paletteYiq != paletteYiqStack) RxMemFree(paletteYiq);
			return nMaxError;
		}
	}

	if (paletteYiq != paletteYiqStack) RxMemFree(paletteYiq);
	return error;
}
