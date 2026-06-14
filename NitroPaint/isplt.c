// -----------------------------------------------------------------------------------------------
// Copyright (c) 2020, Garhoogin
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// -----------------------------------------------------------------------------------------------
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "color.h"
#include "palette.h"

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

#define RX_LARGE_NUMBER             1e32 // constant to represent large color difference
#define RX_SLAB_SIZE            0x100000 // slab size of allocator
#define INV_512    0.0019531250000000000 // 1.0/512.0
#define INV_511    0.0019569471624266144 // 1.0/511.0
#define INV_255    0.0039215686274509800 // 1.0/255.0
#define INV_3      0.3333333333333333333 // 1.0/  3.0
#define TWO_THIRDS 0.6666666666666666667 // 2.0/  3.0

//default parameters for alpha processing (describes the distribution of colors)
#define MEAN_Y    217.7410308381821300000  // mean of Y
#define MEAN_I     -0.0000041426875938555  // mean of I
#define MEAN_Q     -0.0000076075821411337  // mean of Q
#define MEAN_Y2 58357.1112772430790000000  // mean of Y^2
#define MEAN_I2  6772.8810438603696000000  // mean of I^2
#define MEAN_Q2 15832.5709062345540000000  // mean of Q^2


typedef struct RxPaletteMapEntry_ {
	RxYiqColor color[RX_PALETTE_MAX_COUNT];
	unsigned int index;
	double sortVal;
} RxiPaletteMapEntry;

typedef struct RxPaletteAccelNode_ {
	struct RxPaletteAccelNode_ *pLeft;    // left pointer
	struct RxPaletteAccelNode_ *pRight;   // right pointer
	struct RxPaletteAccelNode_ *parent;   // parent node
	RxiPaletteMapEntry *mid;               // mid color
	double splitVal;                      // value of split
	unsigned int nCol;                    // number of colors this node
	unsigned int start;                   // start index of color
	unsigned int splitDir;                // split direction (Y,I,Q,A)
} RxiPaletteAccelNode;

struct RxPalette_ {
	RxBool useAccelerator;                            // marks that the loaded palette is using the accelerator
	RxiPaletteAccelNode root;                          // the root node of the accelerator
	RxiPaletteMapEntry *pltt;                          // palette mapping entries used by the accelerator
	RxiPaletteAccelNode *nodebuf;                      // accelerator working memory

	RxYiqColor plttSmall[16 * RX_PALETTE_MAX_COUNT];  // palette buffer used for small palettes
	RxYiqColor *plttLarge;                            // pointer to palette buffer (heap allocated or pointer to small)
	unsigned int nPltt;                               // number of palette colors loaded
	RxAlphaMode alphaMode;                            // alpha processing mode used by the accelerator
};


static int RxiPaletteFindClosestColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, const RxYiqColor *col, double *outDiff);
static RxStatus RxiPaletteLoadYiq(RxReduction *reduction, const RxYiqColor *pltt, unsigned int srcPitch, unsigned int nColors, RxBool overrideMode);



// ----- memory allocation wrappers for SIMD use

#if defined(RX_SIMD) && !defined(_M_X64)

//alignment of memory to be allocated by RxMem* routines
#define ALLOC_ALIGN sizeof(__m128)

void *RxMemAlloc(size_t size) {
	unsigned char *req = malloc((size + sizeof(void *) + ALLOC_ALIGN - 1) & ~(ALLOC_ALIGN - 1));
	if (req == NULL) return NULL;

	unsigned char *aligned = (unsigned char *) ((((uintptr_t) req) + sizeof(void *) + ALLOC_ALIGN - 1) & ~(uintptr_t) (ALLOC_ALIGN - 1));
	if (aligned != NULL) ((void **) aligned)[-1] = req;

	return aligned;
}

void *RxMemCalloc(size_t nMemb, size_t size) {
	unsigned char *req = calloc((size * nMemb + sizeof(void *) + ALLOC_ALIGN - 1) & ~(ALLOC_ALIGN - 1), 1);
	if (req == NULL) return NULL;

	unsigned char *aligned = (unsigned char *) ((((uintptr_t) req) + sizeof(void *) + ALLOC_ALIGN - 1) & ~(uintptr_t) (ALLOC_ALIGN - 1));
	if (aligned != NULL) ((void **) aligned)[-1] = req;

	return aligned;
}

void RxMemFree(void *p) {
	if (p != NULL) free(((void **) p)[-1]);
}

#endif


// ----- routines for operating on colors

void RX_API RxConvertRgbToYiq(COLOR32 rgb, RxYiqColor *yiq) {
	//implementations using scalar and vector arithmetic
#ifndef RX_SIMD
	float r = (float) ((rgb >>  0) & 0xFF);
	float g = (float) ((rgb >>  8) & 0xFF);
	float b = (float) ((rgb >> 16) & 0xFF);
	float a = (float) ((rgb >> 24) & 0xFF) / 255.0f;

	//this is no longer true YIQ anymore
	float y =  0.5146329f * r + 1.2303905f * g + 0.2588982f * b;
	float i = -0.5885085f * r - 0.3060195f * g + 0.8945280f * b;
	float q =  0.7227111f * r - 1.3898515f * g + 0.6671403f * b;

	//write rounded color
	yiq->y = a * y;
	yiq->i = a * i;
	yiq->q = a * q;
	yiq->a = a;
#else
	//vectorized implementation
	__m128i rgbVeci = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(rgb), _mm_setzero_si128()), _mm_setzero_si128());
	__m128 rgbVec = _mm_cvtepi32_ps(rgbVeci);

	//matrix transform
	__m128 rVec = _mm_shuffle_ps(rgbVec, rgbVec, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 gVec = _mm_shuffle_ps(rgbVec, rgbVec, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 bVec = _mm_shuffle_ps(rgbVec, rgbVec, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 aVec = _mm_div_ps(_mm_shuffle_ps(rgbVec, rgbVec, _MM_SHUFFLE(3, 3, 3, 3)), _mm_set1_ps(255.0f));

	__m128 row0 = _mm_mul_ps(rVec, _mm_setr_ps(0.5146329f, -0.5885085f,  0.7227111f, 0.0f));
	__m128 row1 = _mm_mul_ps(gVec, _mm_setr_ps(1.2303905f, -0.3060195f, -1.3898515f, 0.0f));
	__m128 row2 = _mm_mul_ps(bVec, _mm_setr_ps(0.2588982f,  0.8945280f,  0.6671403f, 0.0f));
	__m128 row3 = _mm_mul_ps(aVec, _mm_setr_ps(      0.0f,        0.0f,        0.0f, 1.0f));
	__m128 yiqa = _mm_add_ps(_mm_add_ps(row0, row1), row2);
		
	//alpha premultiplication and insertion
	yiqa = _mm_mul_ps(yiqa, aVec);
	yiqa = _mm_add_ps(yiqa, row3);

	yiq->yiq = yiqa;
#endif
}

COLOR32 RX_API RxConvertYiqToRgb(const RxYiqColor *yiq) {
	//scalar and SIMD versions
#ifndef RX_SIMD
	float y = 0.0f, i = 0.0f, q = 0.0f;
	if (yiq->a > 0.0f) {
		y = yiq->y / yiq->a;
		i = yiq->i / yiq->a;
		q = yiq->q / yiq->a;
	}

	float r = 0.4990215f * y - 0.56700944f * i + 0.5666126f * q;
	float g = 0.4990215f * y + 0.07502532f * i - 0.2942529f * q;
	float b = 0.4990215f * y + 0.77053964f * i + 0.2721091f * q;
	float a = yiq->a * 255.0f;

	//clamp color
	r = min(max(r, 0.0f), (255.0f));
	g = min(max(g, 0.0f), (255.0f));
	b = min(max(b, 0.0f), (255.0f));
	a = min(max(a, 0.0f), (255.0f));

	int iR = (int) (r + 0.5f);
	int iG = (int) (g + 0.5f);
	int iB = (int) (b + 0.5f);
	int iA = (int) (a + 0.5f);
	return iR | (iG << 8) | (iB << 16) | (iA << 24);
#else
	__m128 yiqa = yiq->yiq;
	__m128 aVec = _mm_shuffle_ps(yiqa, yiqa, _MM_SHUFFLE(3, 3, 3, 3));

	// if a > 0
	if (_mm_ucomigt_ss(aVec, _mm_setzero_ps())) {
		yiqa = _mm_div_ps(yiqa, aVec);
	} else {
		yiqa = _mm_setzero_ps();
	}

	__m128 yVec = _mm_shuffle_ps(yiqa, yiqa, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 iVec = _mm_shuffle_ps(yiqa, yiqa, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 qVec = _mm_shuffle_ps(yiqa, yiqa, _MM_SHUFFLE(2, 2, 2, 2));

	__m128 row0 = _mm_mul_ps(yVec, _mm_setr_ps( 0.49902150f,  0.49902150f, 0.49902150f,   0.0f));
	__m128 row1 = _mm_mul_ps(iVec, _mm_setr_ps(-0.56700944f,  0.07502532f, 0.77053964f,   0.0f));
	__m128 row2 = _mm_mul_ps(qVec, _mm_setr_ps(  0.5666126f, -0.29425290f, 0.27210910f,   0.0f));
	__m128 row3 = _mm_mul_ps(aVec, _mm_setr_ps(        0.0f,         0.0f,        0.0f, 255.0f));
	__m128 rgbaF = _mm_add_ps(_mm_add_ps(row0, row1), _mm_add_ps(row2, row3));

	//clamping
	rgbaF = _mm_max_ps(rgbaF, _mm_setr_ps(  0.0f,   0.0f,   0.0f,   0.0f));
	rgbaF = _mm_min_ps(rgbaF, _mm_setr_ps(255.0f, 255.0f, 255.0f, 255.0f));

	//rounding to integer
	__m128i rgbaI = _mm_cvtps_epi32(rgbaF);

	//clamp and pack to 8-bit by s32->s16->u8 with saturation
	rgbaI = _mm_packs_epi32(rgbaI, _mm_setzero_si128());
	rgbaI = _mm_packus_epi16(rgbaI, _mm_setzero_si128());
	return _mm_cvtsi128_si32(rgbaI);
#endif
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

double RX_API RxComputeColorDifference(RxReduction *reduction, const RxYiqColor *yiq1, const RxYiqColor *yiq2) {
	return RxiComputeColorDifference(reduction, yiq1, yiq2);
}

static COLOR32 RxiMaskColorToDS15(COLOR32 c) {
	//DS mode masking: round color channels to 5-bit values, and force alpha=0xff
	return ColorRoundToDS15(c) | 0xFF000000;
}

static COLOR32 RxiMaskColorDummy(COLOR32 c) {
	//no-mask dummy: pass colors
	return c;
}

static inline COLOR32 RxiMaskYiqToRgb(RxReduction *reduction, const RxYiqColor *yiq) {
	return reduction->maskColors(RxConvertYiqToRgb(yiq));
}

static inline void RxiMaskYiq(RxReduction *reduction, const RxYiqColor *yiq, RxYiqColor *out) {
	COLOR32 rgb = RxiMaskYiqToRgb(reduction, yiq);
	RxConvertRgbToYiq(rgb, out);
}

static inline RxBool RxiColorEqual(const RxYiqColor *a, const RxYiqColor *b) {
#ifndef RX_SIMD
	return memcmp(a, b, sizeof(RxYiqColor)) == 0;
#else
	__m128 eq = _mm_cmpneq_ps(a->yiq, b->yiq);
	return _mm_movemask_ps(eq) == 0;  // all equality results
#endif
}

static inline RxBool RxiColorVecEqual(const RxYiqColor *a, const RxYiqColor *b, unsigned int n) {
	RX_ASSUME(n > 0);

#ifndef RX_SIMD
	return memcmp(a, b, n * sizeof(RxYiqColor)) == 0;
#else
	do {
		if (!RxiColorEqual(a++, b++)) return 0;
	} while (--n);

	return 1;
#endif
}

static inline void RxiColorCopy(RxYiqColor *dest, const RxYiqColor *src) {
#ifndef RX_SIMD
	memcpy(dest, src, sizeof(RxYiqColor));
#else
	dest->yiq = src->yiq;
#endif
}

static inline void RxiColorVecCopy(RxYiqColor *dest, const RxYiqColor *src, unsigned int n) {
	RX_ASSUME(n > 0);

#ifndef RX_SIMD
	memcpy(dest, src, n * sizeof(RxYiqColor));
#else
	do {
		RxiColorCopy(dest++, src++);
	} while (--n);
#endif
}

static inline void RxiColorSwap(RxYiqColor *col1, RxYiqColor *col2) {
	RxYiqColor tmp;
	RxiColorCopy(&tmp, col1);
	RxiColorCopy(col1, col2);
	RxiColorCopy(col2, &tmp);
}

static inline void RxiColorVecSwap(RxYiqColor *vec1, RxYiqColor *vec2, unsigned int n) {
	RX_ASSUME(n > 0);

	do {
		RxiColorSwap(vec1++, vec2++);
	} while (--n);
}

static inline void RxiColorSubtract(RxYiqColor *result, RxYiqColor *a, RxYiqColor *b) {
#ifndef RX_SIMD
	result->y = a->y - b->y;
	result->i = a->i - b->i;
	result->q = a->q - b->q;
	result->a = a->a - b->a;
#else
	result->yiq = _mm_sub_ps(a->yiq, b->yiq);
#endif
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

static inline void RxiColorMakeBlack(RxYiqColor *yiq) {
	//set YIQ channels to 0, leaving alpha unchanged
#ifndef RX_SIMD
	yiq->y = 0.0f;
	yiq->i = 0.0f;
	yiq->q = 0.0f;
#else
	yiq->yiq = _mm_and_ps(yiq->yiq, _mm_castsi128_ps(_mm_setr_epi32(0, 0, 0, -1)));
#endif
}

static inline void RxiColorMakeWhite(RxYiqColor *yiq) {
	//set Y channel to 511 (scaled by alpha), and IQ channels to 0
#ifndef RX_SIMD
	yiq->y = 511.0f * yiq->a;
	yiq->i = 0.0f;
	yiq->q = 0.0f;
#else
	__m128 yiqVec = yiq->yiq;
	__m128 aVec = _mm_shuffle_ps(yiqVec, yiqVec, _MM_SHUFFLE(3, 3, 3, 3));
	yiq->yiq = _mm_mul_ps(_mm_setr_ps(511.0f, 0.0f, 0.0f, 1.0f), aVec);
#endif
}

static inline void RxiColorScale(RxYiqColor *yiq, float scale) {
#ifndef RX_SIMD
	yiq->y *= scale;
	yiq->i *= scale;
	yiq->q *= scale;
	yiq->a *= scale;
#else
	__m128 vScale = _mm_set1_ps(scale);
	yiq->yiq = _mm_mul_ps(yiq->yiq, vScale);
#endif
}

static void RxiUnweightLongColor(RxYiqColor *out, const RxLongColor *cLong, double weight) {
	RX_ASSUME(weight > 0.0);

#ifndef RX_SIMD
	double invWeight = 1.0 / weight;
	out->y = (float) (cLong->y * invWeight);
	out->i = (float) (cLong->i * invWeight);
	out->q = (float) (cLong->q * invWeight);
	out->a = (float) (cLong->a * invWeight);
#else
	__m128d vWeight = _mm_set1_pd(weight);
	__m128 yi = _mm_cvtpd_ps(_mm_div_pd(cLong->yi, vWeight));
	__m128 qa = _mm_cvtpd_ps(_mm_div_pd(cLong->qa, vWeight));

	//interleave yi, qa into packed singles
	out->yiq = _mm_shuffle_ps(yi, qa, _MM_SHUFFLE(1, 0, 1, 0));
#endif
}

static void RxiAddWeightedLongColor(RxLongColor *cLong, const RxYiqColor *yiq, double weight) {
#ifndef RX_SIMD
	cLong->y += weight * yiq->y;
	cLong->i += weight * yiq->i;
	cLong->q += weight * yiq->q;
	cLong->a += weight * yiq->a;
#else
	__m128 col = yiq->yiq;
	__m128d vWeight = _mm_set1_pd(weight);
	__m128d yi = _mm_cvtps_pd(col);
	__m128d qa = _mm_cvtps_pd(_mm_movehl_ps(_mm_setzero_ps(), col));

	cLong->yi = _mm_add_pd(cLong->yi, _mm_mul_pd(yi, vWeight));
	cLong->qa = _mm_add_pd(cLong->qa, _mm_mul_pd(qa, vWeight));
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

void RX_API RxGetDefaultBalance(RxBalanceSetting *balance) {
	balance->balance = RX_BALANCE_DEFAULT;           // lightness-color balance
	balance->colorBalance = RX_COLORBALANCE_DEFAULT; // IQ balance
	balance->enhanceColors = RX_TRUE;                // enhance largely used colors
}

void RX_API RxSetBalance(RxReduction *reduction, const RxBalanceSetting *balance) {

	RxBalanceSetting effBalance;
	if (balance != NULL) {
		//use the user-specified parameters
		effBalance.balance = balance->balance;
		effBalance.colorBalance = balance->colorBalance;
		effBalance.enhanceColors = balance->enhanceColors;
	} else {
		//use the default balance parameters
		RxGetDefaultBalance(&effBalance);
	}

	reduction->yWeight = 40 - effBalance.balance;       // high balance -> lower Y weight
	reduction->iWeight = effBalance.colorBalance;       // high color balance -> high I weight
	reduction->qWeight = 40 - effBalance.colorBalance;  // high color balance -> low Q weight

	reduction->yWeight2 = reduction->yWeight * reduction->yWeight; // Y weight squared
	reduction->iWeight2 = reduction->iWeight * reduction->iWeight; // I weight squared
	reduction->qWeight2 = reduction->qWeight * reduction->qWeight; // Q weight squared

	//compute alpha weights and interactions
	RxiComputeAlphaInteraction(reduction);

	reduction->enhanceColors = effBalance.enhanceColors;
}

RxStatus RX_API RxSetPaletteLayers(RxReduction *reduction, unsigned int nLayers) {
	//check valid layer count
	if (nLayers == 0 || nLayers > RX_PALETTE_MAX_COUNT) return RX_STATUS_INVALID;

	//the context must not have any histogram colors
	if (reduction->histogram != NULL && reduction->histogram->nEntries > 0) return RX_STATUS_INCORRECT_STATE;

	reduction->paletteLayers = nLayers;
	return RX_STATUS_OK;
}

static void RxiInit(RxReduction *reduction, const RxBalanceSetting *balance) {
	memset(reduction, 0, sizeof(RxReduction));

	//default color space moments, precalculated assuming a uniform distribution of RGB colors
	reduction->meanY = MEAN_Y;
	reduction->meanI = MEAN_I;
	reduction->meanQ = MEAN_Q;
	reduction->meanY2 = MEAN_Y2;
	reduction->meanI2 = MEAN_I2;
	reduction->meanQ2 = MEAN_Q2;

	RxSetBalance(reduction, balance);
	RxSetPaletteLayers(reduction, 1);

	reduction->nReclusters = RECLUSTER_DEFAULT;
	reduction->nPaletteColors = RX_PALETTE_MAX_SIZE;
	reduction->maskColors = RxiMaskColorToDS15;
	reduction->alphaMode = RX_ALPHA_NONE; // default: no alpha processing
	reduction->fAlphaThreshold = (float) (0x80 * INV_255);
	reduction->status = RX_STATUS_OK;
}

void RX_API RxAssumeCompositingDistribution(RxReduction *reduction, const COLOR32 *cols, unsigned int nCols) {
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

RxReduction *RX_API RxNew(const RxBalanceSetting *balance) {
	RxReduction *reduction = (RxReduction *) RxMemCalloc(1, sizeof(RxReduction));
	if (reduction == NULL) return NULL;

	RxiInit(reduction, balance);
	return reduction;
}

void RX_API RxApplyFlags(RxReduction *reduction, RxFlag flag) {
	//set alpha mode
	switch (flag & RX_FLAG_ALPHA_MODE_MASK) {
		case RX_FLAG_ALPHA_MODE_NONE: reduction->alphaMode = RX_ALPHA_NONE; break;
		case RX_FLAG_ALPHA_MODE_RESERVE: reduction->alphaMode = RX_ALPHA_RESERVE; break;
		case RX_FLAG_ALPHA_MODE_PIXEL: reduction->alphaMode = RX_ALPHA_PIXEL; break;
		case RX_FLAG_ALPHA_MODE_PALETTE: reduction->alphaMode = RX_ALPHA_PALETTE; break;
	}
	
	//clean-up previous mask bits mode
	//TODO

	//setting the mask bits mode
	switch (flag & RX_FLAG_MASK_BITS_TYPE_MASK) {
		case RX_FLAG_NO_MASK_BITS:
			reduction->maskColors = RxiMaskColorDummy;
			break;
		case RX_FLAG_MASK_BITS:
			reduction->maskColors = RxiMaskColorToDS15;
			break;
	}
}

void RX_API RxSetProgressCallback(RxReduction *reduction, RxProgressCallback callback, void *userData) {
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

static int RxiYiqComparator(const void *d1, const void *d2) {
	const RxYiqColor *yiq1 = (const RxYiqColor *) d1;
	const RxYiqColor *yiq2 = (const RxYiqColor *) d2;

	if (yiq1->y < yiq2->y) return -1;
	if (yiq1->y > yiq2->y) return  1;
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

void RX_API RxHistAddColor(RxReduction *reduction, const RxYiqColor *col, double weight) {
	RxHistogram *histogram = reduction->histogram;
	if (reduction->status != RX_STATUS_OK) return;

	unsigned int nLayer = reduction->paletteLayers;

	//update the first slot index with hash of new color
	int slotIndex = RxiHistHashColor(col);
	if (slotIndex < histogram->firstSlot) histogram->firstSlot = slotIndex;

	//find a slot with the same YIQA, or create a new one if none exists.
	RxHistEntry **ppslot = &histogram->entries[slotIndex];
	int slotUsed = *ppslot != NULL;
	while (*ppslot != NULL) {
		RxHistEntry *slot = *ppslot;

		//matching slot? add weight
		if (RxiColorVecEqual(slot->color, col, nLayer)) {
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
	RxiColorVecCopy(slot->color, col, nLayer);
	slot->weight = weight;
	slot->next = NULL;
	slot->value = 0.0;
	histogram->nEntries++;
	histogram->totalWeight += weight;

	if (!slotUsed) {
		if (reduction->histogram->nSlotsUsed < RX_HISTOGRAM_SMALL) {
			reduction->histogram->slotIndices[reduction->histogram->nSlotsUsed] = slotIndex;
		}
		reduction->histogram->nSlotsUsed++;
	}
}

RxStatus RX_API RxHistFinalize(RxReduction *reduction) {
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

	if (reduction->histogram->nSlotsUsed > RX_HISTOGRAM_SMALL) {
		//check the histogram's slots in order
		for (int i = reduction->histogram->firstSlot; i < RX_HISTOGRAM_SIZE; i++) {
			RxHistEntry *entry = reduction->histogram->entries[i];

			while (entry != NULL) {
				*(pos++) = entry;
				entry = entry->next;
			}
		}
	} else {
		//check only slots in the small histogram list
		for (int i = 0; i < reduction->histogram->nSlotsUsed; i++) {
			RxHistEntry *entry = reduction->histogram->entries[reduction->histogram->slotIndices[i]];

			while (entry != NULL) {
				*(pos++) = entry;
				entry = entry->next;
			}
		}
	}

	return RX_STATUS_OK;
}

RxStatus RX_API RxHistAdd(RxReduction *reduction, const COLOR32 *img, unsigned int width, unsigned int height) {
	if (reduction->histogram == NULL) {
		RxStatus status = RxHistInit(reduction);
		if (status != RX_STATUS_OK) return reduction->status = status;
	}
	
	if (width == 0 || height == 0) return reduction->status;

	//create YIQ data buffer, 1px overhang in all directions where pixels are duplicated
	unsigned int padWidth = width + 2, padHeight = height + 2, nLayer = reduction->paletteLayers;
	unsigned int nPxSrc = width * height;
	
	unsigned int bufferSize = padWidth * padHeight * nLayer;
	RxYiqColor *yiqbuf = reduction->imgBuffer;
	if (bufferSize > RX_TEMP_IMG_BUF_SIZE) {
		//allocate buffer on the heap if large enough
		yiqbuf = (RxYiqColor *) RxMemAlloc(bufferSize * sizeof(RxYiqColor));
	}

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

		RxiColorVecCopy(&row[0], &row[1 * nLayer], nLayer);
		RxiColorVecCopy(&row[(width + 1) * nLayer], &row[width * nLayer], nLayer);
	}
	RxiColorVecCopy(&yiqbuf[0 * padWidth * nLayer], &yiqbuf[padWidth * nLayer], padWidth * nLayer);
	RxiColorVecCopy(&yiqbuf[(height + 1) * padWidth * nLayer], &yiqbuf[height * padWidth * nLayer], padWidth * nLayer);


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
			RxiColorVecCopy(col, center, nLayer);

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

	if (yiqbuf != reduction->imgBuffer) RxMemFree(yiqbuf);

	return reduction->status;
}

double RX_API RxHistComputePaletteErrorYiq(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, double maxError) {
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

double RX_API RxHistComputePaletteError(RxReduction *reduction, const COLOR32 *palette, unsigned int nColors, double maxError) {
	RxYiqColor *yiqPalette = reduction->imgBuffer;
	if (nColors > RX_TEMP_IMG_BUF_SIZE) {
		yiqPalette = (RxYiqColor *) RxMemCalloc(nColors, sizeof(RxYiqColor));
	}

	//convert palette colors
	for (unsigned int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(palette[i], &yiqPalette[i]);
	}

	double error = RxHistComputePaletteErrorYiq(reduction, yiqPalette, nColors, maxError);

	if (yiqPalette != reduction->imgBuffer) RxMemFree(yiqPalette);
	return error;
}


// ----- routines for searching palettes (unaccelerated)

static int RxiPaletteFindClosestColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, const RxYiqColor *col, double *outDiff) {
	double leastDiff = RX_LARGE_NUMBER;
	int leastIndex = 0;
	for (unsigned int i = 0; i < nColors; i++) {
		const RxYiqColor *yiq2 = &palette[i * reduction->paletteLayers];

		double diff = RxiComputeLayeredColorDifference(reduction, col, yiq2);
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
	//TODO: col: scalar color?
	RxYiqColor yiq;
	RxConvertRgbToYiq(col, &yiq);

	return RxiPaletteFindClosestColor(reduction, palette, nColors, &yiq, outDiff);
}



// ----- clustering code

static void RxiColorNodeFree(RxColorNode *node) {
	RxMemFree(node);
}

static void RxiColorNodeFreeAll(RxReduction *reduction) {
	//free all nodes
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RxiColorNodeFree(reduction->colorNodes[i]);
	}

	memset(reduction->colorNodes, 0, sizeof(reduction->colorNodes));
}

static RxColorNode *RxiFindSplittableColorNode(RxReduction *reduction) {
	RxColorNode *found = NULL;

	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RxColorNode *node = reduction->colorNodes[i];
		
		//if the node is splittable, choose it at a higher priority
		if (node->canSplit && (found == NULL || node->priority > found->priority)) {
			found = node;
		}
	}
	return found;
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

static double RxiComputePcScore(RxReduction *reduction, const RxYiqColor *col, const double *principal) {
	double dot = 0.0;
	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		dot += col[j].y * reduction->yWeight * principal[j * 4 + 0]
			+ col[j].i * reduction->iWeight * principal[j * 4 + 1]
			+ col[j].q * reduction->qWeight * principal[j * 4 + 2]
			+ col[j].a * reduction->aWeight * principal[j * 4 + 3];
	}
	return dot;
}

void RX_API RxHistSort(RxReduction *reduction, int startIndex, int endIndex) {
	double principal[4 * RX_PALETTE_MAX_COUNT];
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
		histEntry->value = RxiComputePcScore(reduction, histEntry->color, principal);
	}

	//sort colors by dot product with the vector
	int nColors = endIndex - startIndex;
	qsort(thisHistogram, nColors, sizeof(RxHistEntry *), RxiHistEntryComparator);
}

unsigned int RX_API RxHistGetTopN(RxReduction *reduction, unsigned int n, RxYiqColor *cols, double *weights) {
	if (reduction->histogram == NULL) return 0; // no histogram

	//sort histogram
	qsort(reduction->histogramFlat, reduction->histogram->nEntries, sizeof(RxHistEntry *), RxiHistEntryWeightComparator);

	//get top N items
	unsigned int nGet = n;
	if (nGet > (unsigned int) reduction->histogram->nEntries) nGet = reduction->histogram->nEntries;

	for (unsigned int i = 0; i < nGet; i++) {
		if (weights != NULL) weights[i] = reduction->histogramFlat[i]->weight;
		RxiColorCopy(&cols[i], reduction->histogramFlat[i]->color);
	}
	return nGet;
}

static RxColorNode *RxiTreeNodeAlloc(RxReduction *reduction) {
	//allocate the node structure plus enough color entries for the number of palette layers
	RxColorNode *node = (RxColorNode *) RxMemCalloc(1, sizeof(RxColorNode) + reduction->paletteLayers * sizeof(RxYiqColor));
	return node;
}

static void RxiColorNodeInit(RxReduction *reduction, RxColorNode *node, int startIndex, int endIndex) {
	node->startIndex = startIndex;
	node->endIndex = endIndex;
	node->canSplit = RX_TRUE;

	//calculate the pivot index, as well as average YIQA values.
	int nColors = node->endIndex - node->startIndex;
	if (nColors < 2) {
		//1 color: set leaf color to the single histogram color and its weight
		RxHistEntry *entry = reduction->histogramFlat[node->startIndex];
		RxiColorVecCopy(node->color, entry->color, reduction->paletteLayers);
		node->weight = entry->weight;
		node->canSplit = RX_FALSE;
		return;
	}

	//compute the split axis for this cluster
	double *principal = reduction->splitAxis;
	RxiHistChooseSplitAxis(reduction, node->startIndex, node->endIndex, principal);

	double projMax = -RX_LARGE_NUMBER;
	double projMin = RX_LARGE_NUMBER;
	for (int i = node->startIndex; i < node->endIndex; i++) {
		RxHistEntry *histEntry = reduction->histogramFlat[i];
		double proj = RxiComputePcScore(reduction, histEntry->color, principal);

		histEntry->value = proj;
		if (proj > projMax) projMax = proj;
		if (proj < projMin) projMin = proj;
	}

	if (projMin == projMax) {
		node->canSplit = RX_FALSE;
		return;
	}

	RxLongColor *splits = (RxLongColor *) RxMemCalloc(nColors * reduction->paletteLayers, sizeof(RxLongColor));
	double *splitWeightL = (double *) calloc(nColors, sizeof(double));
	if (splits == NULL || splitWeightL == NULL) {
		RxMemFree(splits);
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
	RxLongColor total[RX_PALETTE_MAX_COUNT] = { 0 };   // total straight
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
		RxLongColor *split = &splits[i * reduction->paletteLayers];
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			RxiAddWeightedLongColor(&total[j], &entry->color[j], weight);  // accumulate YIQA

			memcpy(&split[j], &total[j], sizeof(RxLongColor));
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
		RxiUnweightLongColor(&node->color[i], &total[i], totalWeight);
	}

	//initial WSS value, which we use to calculate the WSS reduction from split
	double wssInitial = sumSq;
	for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
		wssInitial -= RxiVec4Mag(total[i].y * reduction->yWeight, total[i].i * reduction->iWeight,
			total[i].q * reduction->qWeight, total[i].a * reduction->aWeight) * invWeight;
	}

	//in alpha processing mode, we must apply the interaction terms to WSS.
	if (reduction->alphaMode == RX_ALPHA_PALETTE) {
		for (unsigned int i = 0; i < reduction->paletteLayers; i++) {
			double meanY = node->color[i].y, meanI = node->color[i].i, meanQ = node->color[i].q, meanA = node->color[i].a;
			wssInitial -= (
				  reduction->interactionY * (totalA[3 * i + 0] - (meanY * meanA) * totalWeight)
				+ reduction->interactionI * (totalA[3 * i + 1] - (meanI * meanA) * totalWeight)
				+ reduction->interactionQ * (totalA[3 * i + 2] - (meanQ * meanA) * totalWeight)
			);
		}
	}

	//determine pivot index based on the split that yields the best total WSS. This represents total
	//squared quantization error
	int pivotIndex = 1;
	double wssBest = RX_LARGE_NUMBER;
	for (int i = 0; i < (nColors - 1); i++) {
		RxLongColor *SL = &splits[i * reduction->paletteLayers];
		
		double weightL = splitWeightL[i];
		double weightR = totalWeight - weightL;
		double invWeightL = 1.0 / weightL;
		double invWeightR = 1.0 / weightR;

		RxLongColor SR[RX_PALETTE_MAX_COUNT];
		double sumSqL = 0.0, sumSqR = 0.0;

		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			SR[j].y = total[j].y - SL[j].y;
			SR[j].i = total[j].i - SL[j].i;
			SR[j].q = total[j].q - SL[j].q;
			SR[j].a = total[j].a - SL[j].a;

			sumSqL += RxiVec4Mag(SL[j].y * reduction->yWeight, SL[j].i * reduction->iWeight, SL[j].q * reduction->qWeight, SL[j].a * reduction->aWeight);
			sumSqR += RxiVec4Mag(SR[j].y * reduction->yWeight, SR[j].i * reduction->iWeight, SR[j].q * reduction->qWeight, SR[j].a * reduction->aWeight);
		}

		double wss = sumSq - sumSqL * invWeightL - sumSqR * invWeightR;

		//in alpha processing mode, we must apply the intercation terms to WSS.
		if (reduction->alphaMode == RX_ALPHA_PALETTE) {
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
				wss -= (
					  reduction->interactionY * (totalA[3 * j + 0] - (SL[j].y * SL[j].a * invWeightL + SR[j].y * SR[j].a * invWeightR))
					+ reduction->interactionI * (totalA[3 * j + 1] - (SL[j].i * SL[j].a * invWeightL + SR[j].i * SR[j].a * invWeightR))
					+ reduction->interactionQ * (totalA[3 * j + 2] - (SL[j].q * SL[j].a * invWeightL + SR[j].q * SR[j].a * invWeightR))
				);
			}
		}

		//better sum of squares
		if (wss < wssBest) {
			//we'll check the mean left and mean right. They should be different with masking.

			RxBool same = RX_TRUE;
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {

				RxYiqColor yiqL, yiqR;
				RxiUnweightLongColor(&yiqL, &SL[j], weightL);
				RxiUnweightLongColor(&yiqR, &SR[j], weightR);

				COLOR32 maskL = RxiMaskYiqToRgb(reduction, &yiqL);
				COLOR32 maskR = RxiMaskYiqToRgb(reduction, &yiqR);
				if (maskL != maskR) {
					//centroids differ in at least one color
					same = RX_FALSE;
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
	RxMemFree(splits);
	free(splitWeightL);
	
	if (wssBest == RX_LARGE_NUMBER) {
		//any split must necessarily reduce the WSS, except for when color masking is used. If no split may be
		//made, then we mark the node as unsplittable.
		node->canSplit = RX_FALSE;
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

static void RxiColorNodeSplit(RxReduction *reduction, RxColorNode *node) {
	RX_ASSUME(node->canSplit);
	RX_ASSUME(node->pivotIndex > node->startIndex && node->pivotIndex < node->endIndex);

	//allocate a new right node, the left node will replace the old node
	RxColorNode *lNode = node;
	RxColorNode *rNode = RxiTreeNodeAlloc(reduction);
	if (rNode == NULL) {
		reduction->status = RX_STATUS_NOMEM;
		return;
	}

	//slot new node into the array
	RX_ASSUME(reduction->nUsedColors < RX_PALETTE_MAX_SIZE);
	reduction->colorNodes[reduction->nUsedColors++] = rNode;

	//init left and right nodes
	RxiColorNodeInit(reduction, rNode, node->pivotIndex, node->endIndex);
	RxiColorNodeInit(reduction, lNode, node->startIndex, node->pivotIndex);
}

static RxColorNode *RxiColorNodeFindByColor(RxReduction *reduction, const RxColorNode *src, unsigned int *pIndex) {
	
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RxColorNode *node = reduction->colorNodes[i];
		if (node == src) continue; // do not return the query node

		//check the color matches
		RxBool differ = 0;
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			COLOR32 compare = RxiMaskYiqToRgb(reduction, &src->color[j]);
			COLOR32 thisRgb = RxiMaskYiqToRgb(reduction, &node->color[j]);

			if (compare != thisRgb) {
				//not the node we're looking for
				differ = RX_TRUE;
				break;
			}
		}

		//found macthing node
		if (!differ) {
			*pIndex = i;
			return node;
		}
	}

	//no nodes found
	return NULL;
}

static void RxiColorNodeDeleteByIndex(RxReduction *reduction, unsigned int iNode) {
	RX_ASSUME(iNode < reduction->nUsedColors);

	//first free the node structure
	RxiColorNodeFree(reduction->colorNodes[iNode]);

	//move nodes
	unsigned int nMove = reduction->nUsedColors - iNode - 1;
	if (nMove > 0) {
		memmove(&reduction->colorNodes[iNode], &reduction->colorNodes[iNode + 1], nMove * sizeof(RxColorNode *));
	}
	reduction->nUsedColors--;
}

static void RxiCreatePaletteUpdateProgress(RxReduction *reduction) {
	//weight # reclusters 64x to one palette color
	unsigned int progressMax = reduction->nPaletteColors + 64 * reduction->nReclusters;
	unsigned int progress = reduction->nUsedColors + 64 * reduction->reclusterIteration;

	RxiUpdateProgress(reduction, progress, progressMax);
}

static void RxiPaletteWriteMasked(RxReduction *reduction) {
	//convert to RGB
	RxColorNode **colorBlockPtr = reduction->colorNodes;
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RX_ASSUME(colorBlockPtr[i] != NULL);
		
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			//write YIQ (with any loss of information to RGB)
			RxiMaskYiq(reduction, &colorBlockPtr[i]->color[j], &reduction->paletteYiq[i][j]);
		}
	}
}

static void RxiPaletteToRgb(RxReduction *reduction) {
	//convert all colors to final RGB output
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			reduction->paletteRgb[i][j] = RxConvertYiqToRgb(&reduction->paletteYiq[i][j]);
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
		int bestIndex = RxPaletteFindClosestColorYiq(reduction, entry->color, &bestDistance);

		//add to total. YIQ colors scaled by alpha to be unscaled later.
		double weight = entry->weight;
		totalsBuffer[bestIndex].weight += weight;
		totalsBuffer[bestIndex].error += weight * bestDistance;
		totalsBuffer[bestIndex].count++;

		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			RxiAddWeightedLongColor(&totalsBuffer[bestIndex].sum[j], &entry->color[j], weight);
		}
		entry->entry = bestIndex;
	}
}

static void RxiVoronoiMoveToCluster(RxReduction *reduction, RxHistEntry *entry, int idxTo, double newDifference, double oldDifference) {
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	int idxFrom = entry->entry;

	double weight = entry->weight;
	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		//add weight to "to" cluster
		RxiAddWeightedLongColor(&totalsBuffer[idxTo].sum[j], &entry->color[j], weight);

		//remove weight from "from" cluster
		RxiAddWeightedLongColor(&totalsBuffer[idxFrom].sum[j], &entry->color[j], -weight);
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
	unsigned int nLayers = reduction->paletteLayers;

	//load the palette into the acceleration structure
	RxiPaletteLoadYiq(reduction, &reduction->paletteYiq[0][0], RX_PALETTE_MAX_COUNT, reduction->nUsedColors, RX_TRUE);

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
			RxHistEntry *entry = reduction->histogramFlat[j];       // histogram color
			RxYiqColor *yiq1 = reduction->paletteYiq[entry->entry]; // ceontroid of the cluster the color belongs to

			//do not move a cluster with only one member
			if (totalsBuffer[entry->entry].count <= 1) continue;

			//calculate the masked histogram color
			RxYiqColor *yiqNewCentroid = reduction->tempLayeredColor;

			//if we mask colors, check this entry against the palette with clamping. If they compare equal,
			//then we say that this color is as close as it will be to a palette color and we won't include
			//this in our search candidates.
			RxBool same = RX_TRUE;
			for (unsigned int k = 0; k < nLayers; k++) {
				COLOR32 palMasked = RxiMaskYiqToRgb(reduction, &yiq1[k]);
				COLOR32 histMasked = RxiMaskYiqToRgb(reduction, &entry->color[k]);
				RxConvertRgbToYiq(histMasked, &yiqNewCentroid[k]);

				//check if the histogram and palette still differ after masking
				if (histMasked != palMasked) {
					//colors differ
					same = RX_FALSE;
				}
			}
			if (same) continue; // this difference can't be reconciled (mask to the same color)

			//calculate the difference between the histogram color and its currently assigned best centroid.
			double diff = RxiComputeColorDifference(reduction, yiq1, &entry->color[0]) * entry->weight;

			//we subtract the difference to the new centroid to calcualate the reduction in the error sum of
			//squares. The highest reduction is desired.
			double newDifference = RxiComputeLayeredColorDifference(reduction, entry->color, yiqNewCentroid) * entry->weight;
			
			double diffReduction = diff - newDifference;
			if (diffReduction > 0.0 && diffReduction > largestDifferenceReduction) {
				//lastly, since an earlier cluster reassignment may have produced a cluster matching
				//what would be this entry's new centroid, we'll check the existing centroids and assign
				//to an existing one if it exists.
				RxBool found = RX_FALSE;
				for (unsigned int k = 0; k < nNewCentroids; k++) {
					unsigned int idx = newCentroidIdxs[k];
					//check that all layers of the colors match
					if (RxiColorVecEqual(reduction->paletteYiq[idx], yiqNewCentroid, nLayers)) {
						//remap to the existing centroid
						RxiVoronoiMoveToCluster(reduction, entry, idx, newDifference, diff);
						found = RX_TRUE;
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
			for (unsigned int j = 0; j < nLayers; j++) {
				RxiMaskYiq(reduction, &entry->color[j], &reduction->paletteYiq[i][j]);
			}

			//move centroid
			double newDifference = RxiComputeLayeredColorDifference(reduction, entry->color, reduction->paletteYiq[i]) * entry->weight;
			RxiVoronoiMoveToCluster(reduction, entry, i, newDifference, largestDifference);
			newCentroidIdxs[nNewCentroids++] = i;
		} else {
			//no best point was found for replacement.
			return 0; // stop
		}
	}

	//average out the colors in the new partitions
	unsigned int nMovedClusters = 0;
	for (unsigned int i = reduction->nPinnedClusters; i < reduction->nUsedColors; i++) {
		//compute the final new masked color for the cluster
		RxYiqColor *yiq = reduction->tempLayeredColor;

		for (unsigned int j = 0; j < nLayers; j++) {
			RxiUnweightLongColor(&yiq[j], &totalsBuffer[i].sum[j], totalsBuffer[i].weight);

			//mask color
			RxiMaskYiq(reduction, &yiq[j], &yiq[j]);
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
			RxiColorVecCopy(reduction->paletteYiq[i], yiq, nLayers);
			nMovedClusters++;
		}
	}

	//nMovedClusters indicates the number of centroids that were not degenerate that were moved.
	//nNewCentroids indicates the number of centroids that were degenerate but resolved.
	//if both of these are zero, this indicates no change was made to the palette, meaning we have
	//reached a stable clustering and don't need to proced with the error calculation.
	if (nMovedClusters == 0 && nNewCentroids == 0) return 0;

	RxiCreatePaletteUpdateProgress(reduction);

	//if this is the last iteration, stop iterating
	if (++reduction->reclusterIteration >= reduction->nReclusters) return 0;
	return 1; // continue
}

static void RxiVoronoiRecluster(RxReduction *reduction) {
	//simple termination conditions
	if (reduction->nReclusters <= 0 || reduction->nPinnedClusters >= reduction->nUsedColors) return;

	//voronoi iteration
	reduction->reclusterIteration = 0;
	while (RxiVoronoiIterate(reduction));

	//load palette accelerator
	RxiPaletteLoadYiq(reduction, &reduction->paletteYiq[0][0], RX_PALETTE_MAX_COUNT, reduction->nUsedColors, RX_TRUE);

	//delete any entries we couldn't use and shrink the palette size.
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	memset(totalsBuffer, 0, sizeof(reduction->blockTotals));
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxYiqColor *histColor = reduction->histogramFlat[i]->color;

		//find nearest, add to total
		int bestIndex = RxPaletteFindClosestColorYiq(reduction, histColor, NULL);
		totalsBuffer[bestIndex].weight += reduction->histogramFlat[i]->weight;
	}

	//weight==0 => delete
	unsigned int nRemoved = 0;
	for (unsigned int i = reduction->nPinnedClusters; i < reduction->nUsedColors; i++) {
		if (totalsBuffer[i].weight > 0) continue;

		//delete
		memmove(reduction->paletteYiq[i], reduction->paletteYiq[i + 1], (reduction->nUsedColors - i - 1) * sizeof(reduction->paletteYiq[0]));
		memmove(&totalsBuffer[i], &totalsBuffer[i + 1], (reduction->nUsedColors - i - 1) * sizeof(RxTotalBuffer));
		reduction->nUsedColors--;
		i--;
		nRemoved++;
	}

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

static void RxiAdjustHistogramIndices(RxReduction *reduction, int cutStart, int nCut) {
	//adjust all nodes
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RxColorNode *node = reduction->colorNodes[i];

		//adjust this node
		if (node->startIndex > cutStart) {
			//when startIndex==cutStart, this node is referencing the space being deleted. Do not adjust.
			node->startIndex -= nCut;
			node->endIndex -= nCut;
			node->pivotIndex -= nCut;
		}
	}
}

static int RxiMergeColorNodes(RxReduction *reduction) {
	if (reduction->status != RX_STATUS_OK) return 0;
	if (reduction->nUsedColors < 2) return 0; // no merge possible

	//duplicate color test
	for (unsigned int i = 0; i < reduction->nUsedColors; i++) {
		RxColorNode *node = reduction->colorNodes[i];

		unsigned int nDupMerge = 0;
		while (1) {
			//find duplicate nodes until no duplicates are found
			unsigned int iDup;
			RxColorNode *dup = RxiColorNodeFindByColor(reduction, node, &iDup);
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

			//delete the duplicate node.
			RxiColorNodeDeleteByIndex(reduction, iDup);

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
			RxiAdjustHistogramIndices(reduction, start1, nCols1);
			RxiAdjustHistogramIndices(reduction, start2 - nCols1, nCols2); // adjust starting index by amount we cut above

			//we recalculate the new combined node.
			RxiColorNodeInit(reduction, node, loc3, loc4);
			if (reduction->status != RX_STATUS_OK) return 0; // early exit

			node->canSplit = 0; // HACK

			//remove node from existence
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

RxStatus RX_API RxComputePalette(RxReduction *reduction, unsigned int nColors) {
	reduction->nPaletteColors = nColors;
	reduction->reclusterIteration = 0;
	reduction->nPinnedClusters = 0;
	reduction->nUsedColors = 0;
	RxiCreatePaletteUpdateProgress(reduction);

	if (reduction->histogramFlat == NULL || reduction->histogram->nEntries == 0) {
		return reduction->status;
	}

	//max palette size check
	if (nColors > RX_PALETTE_MAX_SIZE) return RX_STATUS_INVALID;
	
	//create the root cluster holding all colors
	RxColorNode *head = RxiTreeNodeAlloc(reduction);
	RxiColorNodeInit(reduction, head, 0, reduction->histogram->nEntries);
	reduction->colorNodes[reduction->nUsedColors++] = head;

	//main color reduction loop
	while (reduction->nUsedColors < reduction->nPaletteColors) {
		//split and initialize children for the found node.
		RxColorNode *node = RxiFindSplittableColorNode(reduction);
		if (node != NULL) RxiColorNodeSplit(reduction, node); // split node

		//when we would reach a termination condition, check first if any colors would be duplicates of each other.
		//this may especially happen when color masking is used, since the masked colors are not yet known.
		if (reduction->nUsedColors >= reduction->nPaletteColors || node == NULL) {
			//merge loop
			while (RxiMergeColorNodes(reduction));
		}
		RxiCreatePaletteUpdateProgress(reduction);

		//no more nodes to split?
		if (node == NULL || reduction->status != RX_STATUS_OK) break;
	}
	RxiCreatePaletteUpdateProgress(reduction);

	//mask cluster centroids to palette colors
	RxiPaletteWriteMasked(reduction);

	//perform voronoi iteration
	RxiVoronoiRecluster(reduction);

	//cleanup
	RxiColorNodeFreeAll(reduction);

	//palette to RGB
	RxiPaletteToRgb(reduction);

	//load the palette into the accelerator.
	RxiPaletteLoadYiq(reduction, &reduction->paletteYiq[0][0], RX_PALETTE_MAX_COUNT, reduction->nUsedColors, RX_FALSE);

	return reduction->status;
}

static int RxiIsColorVectorAllEqual(const RxYiqColor *yiq, unsigned int n) {
	//check colors 1...n-1
	for (unsigned int i = 1; i < n; i++) {
		if (!RxiColorEqual(&yiq[i], &yiq[0])) return 0;
	}
	return 1;
}

RxStatus RX_API RxSortPalette(RxReduction *reduction, RxFlag flag) {
	unsigned int nSort = reduction->nPaletteColors;
	if (flag & RX_FLAG_SORT_ONLY_USED) nSort = reduction->nUsedColors;

	if (nSort > reduction->nUsedColors) {
		//when the user requests to sort more palette colors than are generated, we
		//pad space by filling with opaque black slots. When these extra slots are
		//included in sorting, we increase the number of used colors to account for
		//the fact that now the expanded range is being used.
		for (unsigned int i = reduction->nUsedColors; i < nSort; i++) {
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
				RxiColorMakeBlack(&reduction->paletteYiq[i][j]);
			}
		}
		reduction->nUsedColors = nSort;
	}

	//if the flag to move differing colors to the end was specified, we use the comparator that
	//checks for shared colors.
	if (flag & RX_FLAG_SORT_END_DIFFER) {
		//sort by differ check, then Y of 1st palette
		unsigned int nSame = 0;  // number of colors moved to the beginning of the palette
		for (unsigned int i = 0; i < nSort; i++) {
			RxYiqColor *yiq = reduction->paletteYiq[i];

			if (RxiIsColorVectorAllEqual(yiq, reduction->paletteLayers)) {
				//all components are the same: move to the beginning of the palette by swapping it out with
				//whatever color was there
				if (i > nSame) {
					//swap colors
					RxiColorVecSwap(reduction->paletteYiq[i], reduction->paletteYiq[nSame], reduction->paletteLayers);
				}

				nSame++;
			} else {
				//not all same, do nothing
			}
		}

		//sort both halves of the palette
		qsort(&reduction->paletteYiq[0], nSame, sizeof(reduction->paletteYiq[0]), RxiYiqComparator);
		if (nSame < nSort) {
			qsort(&reduction->paletteYiq[nSame], nSort - nSame, sizeof(reduction->paletteYiq[0]), RxiYiqComparator);
		}

	} else {
		//sort only by Y channel of 1st palette
		qsort(reduction->paletteYiq, nSort, sizeof(reduction->paletteYiq[0]), RxiYiqComparator);
	}

	//remake the RGB colors
	for (unsigned int i = 0; i < nSort; i++) {
		for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
			reduction->paletteRgb[i][j] = RxConvertYiqToRgb(&reduction->paletteYiq[i][j]);
		}
	}

	//load into the accelerator.
	RxiPaletteLoadYiq(reduction, &reduction->paletteYiq[0][0], RX_PALETTE_MAX_COUNT, reduction->nUsedColors, RX_FALSE);

	return RX_STATUS_OK;
}

RxStatus RX_API RxGetPalette(RxReduction *reduction, COLOR32 *pltt, unsigned int iPltt) {
	if (iPltt >= reduction->paletteLayers) return RX_STATUS_INVALID;

	//when the alpha mode is "reserve", we write out a placeholder transparent black color in that slot.
	unsigned int iStart = 0;
	if (reduction->alphaMode == RX_ALPHA_RESERVE) {
		iStart = 1;
		pltt[0] = 0x00000000;
	}

	//we write all nPaletteColors: If the palette was sorted prior, only outputting nUsedColors would cause
	//some colors to be left out of the palette! This is also the cleanest contract for the caller, ensuring
	//that they need not check the nUsedColors field to know how many colors were written.
	for (unsigned int i = 0; i < reduction->nPaletteColors; i++) {
		pltt[i + iStart] = reduction->paletteRgb[i][iPltt];
	}

	return RX_STATUS_OK;
}

RxStatus RX_API RxHistClear(RxReduction *reduction) {
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	reduction->histogramFlat = NULL;

	if (reduction->histogram != NULL) {
		RxiSlabFreeAll(&reduction->histogram->allocator);
		free(reduction->histogram);
		reduction->histogram = NULL;
	}

	reduction->nUsedColors = 0;
	memset(reduction->paletteRgb, 0, sizeof(reduction->paletteRgb));
	return reduction->status = RX_STATUS_OK;
}

static void RxiDestroy(RxReduction *reduction) {
	RxPaletteFree(reduction);
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	if (reduction->histogram != NULL) {
		RxiSlabFreeAll(&reduction->histogram->allocator);
		free(reduction->histogram);
	}
}

void RX_API RxFree(RxReduction *reduction) {
	RxiDestroy(reduction);
	RxMemFree(reduction);
}

RxStatus RX_API RxGlbCreatePalette(const COLOR32 *img, unsigned int width, unsigned int height, COLOR32 *pal, unsigned int nColors, const RxBalanceSetting *balance, RxFlag flag, unsigned int *pOutCols) {
	RxReduction *reduction = RxNew(balance);
	if (reduction == NULL) return RX_STATUS_NOMEM;

	RxApplyFlags(reduction, flag);

	RxStatus status = RxCreatePalette(reduction, img, width, height, pal, nColors, flag, pOutCols);
	RxFree(reduction);
	return status;
}

RxStatus RX_API RxCreatePalette(RxReduction *reduction, const COLOR32 *px, unsigned int width, unsigned int height, COLOR32 *pal, unsigned int nColors, RxFlag flag, unsigned int *pOutCols) {
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


// ----- character map color reduction routines

#define RX_TILE_PALETTE_COUNT_MAX 16 // max palettes produced

typedef struct RxiTile_ {
	COLOR32 rgb[64];                         // RGBA 8x8 block color
	uint8_t indices[64];                     // indices into color palette per 8x8 pixels
	RxYiqColor palette[RX_PALETTE_MAX_SIZE]; // YIQ color palette
	int useCounts[RX_PALETTE_MAX_SIZE];
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
	if (tile1->nUsedColors == 0 || tile2->nUsedColors == 0) return 0.0;

	//are the palettes identical?
	if (tile1->nUsedColors == tile2->nUsedColors) {
		//if (memcmp(tile1->palette, tile2->palette, tile1->nUsedColors * sizeof(tile1->palette[0])) == 0) return 0.0;
		if (RxiColorVecEqual(tile1->palette, tile2->palette, tile1->nUsedColors)) return 0.0;
	}

	//map each color from tile2 to one of tile1
	double totalDiff = 0.0;
	for (unsigned int i = 0; i < tile2->nUsedColors; i++) {
		double diff = 0.0;
		(void) RxiPaletteFindClosestColor(reduction, tile1->palette, tile1->nUsedColors, &tile2->palette[i], &diff);

		totalDiff += diff * tile2->useCounts[i];
	}
	
	//if all colors match perfectly, return 0
	if (totalDiff == 0.0) return 0.0;

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

static void RxiGetPalette0Rgb(RxReduction *reduction, COLOR32 *dest, unsigned int nCols) {
	for (unsigned int i = 0; i < nCols; i++) dest[i] = reduction->paletteRgb[i][0];
}

static int RxiPaletteLightnessComparator(const void *e1, const void *e2) {
	const COLOR32 *p1 = (const COLOR32 *) e1;
	const COLOR32 *p2 = (const COLOR32 *) e2;

	double y1 = 0.0, y2 = 0.0, a1 = 0.0, a2 = 0.0;

	//average lightness per palette
	for (unsigned int i = 0; i < RX_PALETTE_MAX_SIZE; i++) {
		RxYiqColor yiq1, yiq2;
		RxConvertRgbToYiq(p1[i], &yiq1);
		RxConvertRgbToYiq(p2[i], &yiq2);

		y1 += yiq1.y; a1 += yiq1.a;
		y2 += yiq2.y; a2 += yiq2.a;
	}

	if (a2 == 0.0 && a1 == 0.0) return 0; // equivalent
	if (a2 == 0.0) return -1;
	if (a1 == 0.0) return  1;

	y1 /= a1;
	y2 /= a2;

	if (y1 < y2) return -1;
	if (y1 > y2) return  1;
	return 0;
}

void RX_API RxCreateMultiplePalettes(
	const COLOR32          *imgBits,
	unsigned int            tilesX,
	unsigned int            tilesY,
	COLOR32                *dest,
	int                     paletteBase,
	int                     nPalettes,
	int                     paletteSize,
	int                     nColsPerPalette,
	int                     paletteOffset,
	RxBool                  useColor0,
	const RxBalanceSetting *balance,
	volatile int           *progress
) {
	if (nPalettes == 0) return;

	//in the case of one palette, call to the faster single-palette routines.
	if (nPalettes == 1) {
		//create just one palette
		unsigned int effectivePaletteOffset = paletteOffset, effectivePaletteSize = nColsPerPalette;
		if (paletteOffset == 0 && !useColor0) {
			effectivePaletteOffset++;
			effectivePaletteSize--;
		}

		RxGlbCreatePalette(
			imgBits,
			tilesX * 8,
			tilesY * 8,
			dest + (paletteBase * paletteSize) + effectivePaletteOffset,
			effectivePaletteSize,
			balance,
			RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE,
			NULL
		);
		if (paletteOffset == 0 && !useColor0) dest[(paletteBase * paletteSize)] = 0xFF00FF; // transparent fill
		return;
	}

	if (paletteOffset == 0) nColsPerPalette--;
	
	//3 stage algorithm:
	//	1 - split into tiles
	//	2 - map similarities
	//	3 - palette merging

	// ----- STAGE 1: create tile  data

	unsigned int nTiles = tilesX * tilesY;
	RxiTile *tiles = (RxiTile *) RxMemCalloc(nTiles, sizeof(RxiTile));
	RxReduction *reduction = RxNew(balance);

	for (unsigned int y = 0; y < tilesY; y++) {
		for (unsigned int x = 0; x < tilesX; x++) {
			RxiTile *tile = &tiles[x + (y * tilesX)];
			const COLOR32 *pxOrigin = imgBits + x * 8 + (y * 8 * tilesX * 8);
			RxiTileCopy(tile, pxOrigin, tilesX * 8);

			RxHistClear(reduction);
			RxHistAdd(reduction, tile->rgb, 8, 8);
			RxHistFinalize(reduction);
			RxComputePalette(reduction, nColsPerPalette);
			for (unsigned int i = 0; i < RX_PALETTE_MAX_SIZE; i++) {
				RxiColorCopy(&tile->palette[i], &reduction->paletteYiq[i][0]);
			}

			tile->nUsedColors = reduction->nUsedColors;

			//match pixels to palette indices
			for (unsigned int i = 0; i < 64; i++) {
				unsigned int index = RxiPaletteFindClosestRgbColor(reduction, &tile->palette[0], tile->nUsedColors, tile->rgb[i], NULL);
				if ((tile->rgb[i] >> 24) == 0) index = RX_PALETTE_MAX_SIZE - 1;
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
		for (int i = 0; i < RX_PALETTE_MAX_SIZE - 1; i++) {
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
				if ((col >> 24) == 0) index = RX_PALETTE_MAX_SIZE - 1;
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
	COLOR32 *palettes = (COLOR32 *) calloc(RX_TILE_PALETTE_COUNT_MAX * RX_PALETTE_MAX_SIZE, sizeof(COLOR32));

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
		
		RxiGetPalette0Rgb(reduction, palettes + nPalettesWritten * RX_PALETTE_MAX_SIZE, RX_PALETTE_MAX_SIZE - 1);
		nPalettesWritten++;
		(*progress)++;
	}

	//palette refinement
	int nRefinements = 8;
	int *bestPalettes = (int *) calloc(nTiles, sizeof(int));
	RxYiqColor *yiqPalette = (RxYiqColor *) RxMemCalloc(nPalettes, RX_PALETTE_MAX_SIZE * sizeof(RxYiqColor));
	for (int k = 0; k < nRefinements; k++) {
		//palette to YIQ
		for (int i = 0; i < nPalettes; i++) {
			for (int j = 0; j < nColsPerPalette; j++) {
				RxConvertRgbToYiq(palettes[i * RX_PALETTE_MAX_SIZE + j], &yiqPalette[i * RX_PALETTE_MAX_SIZE + j]);
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
				double error = RxHistComputePaletteErrorYiq(reduction, yiqPalette + (j * RX_PALETTE_MAX_SIZE), nColsPerPalette, bestError);
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
			RxiGetPalette0Rgb(reduction, palettes + i * RX_PALETTE_MAX_SIZE, nColsPerPalette);
		}
	}
	RxMemFree(yiqPalette);

	//a second histogram for accumulating per-color error
	RxReduction *errHist = RxNew(balance);
	RxHistInit(errHist);

	//sort palettes by lightness
	qsort(palettes, nPalettes, RX_PALETTE_MAX_SIZE * sizeof(COLOR32), RxiPaletteLightnessComparator);

	//write palettes in the correct size
	for (int i = 0; i < nPalettes; i++) {
		//recreate palette so that it can be output in its correct size
		COLOR32 *thisPalDest = dest + paletteSize * (i + paletteBase) + outputOffs;
		qsort(palettes + i * RX_PALETTE_MAX_SIZE, nColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
		memcpy(thisPalDest, palettes + i * RX_PALETTE_MAX_SIZE, nColsPerPalette * sizeof(COLOR32));

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
			RxiVoronoiLoad(reduction, pltI, nColsPerPalette + 1);  // load palette
			RxiVoronoiPinRange(reduction, 1);                      // pin first color
			RxiVoronoiRecluster(reduction);
			RxiVoronoiUnpin(reduction);

			RxiGetPalette0Rgb(reduction, pltI, nColsPerPalette + 1);
			qsort(pltI + 1, nColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
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

RxStatus RX_API RxGlbReduceImage(
	COLOR32                *img,
	int                    *indices,
	unsigned int            width,
	unsigned int            height,
	const COLOR32          *palette,
	unsigned int            nColors,
	RxFlag                  flag,
	float                   diffuse,
	const RxBalanceSetting *balance
) {
	RxReduction *reduction = RxNew(balance);
	if (reduction == NULL) return RX_STATUS_NOMEM;
	
	RxStatus status;
	RxApplyFlags(reduction, flag);

	//load palette into context
	status = RxPaletteLoad(reduction, palette, nColors);

	if (status == RX_STATUS_OK) {
		//reduce image
		status = RxReduceImage(reduction, img, indices, width, height, flag, diffuse);
	}

	RxFree(reduction);
	return status;
}

RxStatus RX_API RxReduceImage(
	RxReduction *reduction,
	COLOR32     *img,
	int         *indices,
	unsigned int width,
	unsigned int height,
	RxFlag       flag,
	float        diffuse
) {
	//decode flags
	int touchAlpha = (flag & RX_FLAG_NO_PRESERVE_ALPHA);
	int adaptive = !(flag & RX_FLAG_NO_ADAPTIVE_DIFFUSE);

	//the context must have an active palette.
	if (reduction->accel == NULL) return RX_STATUS_INCORRECT_STATE;

	//initial progress
	RxiUpdateProgress(reduction, 0, height);

	//the rest of the color reduction routine requires at least one row for the row buffer, but
	//a 0-line bitmap may be trivially indexed.
	if (height == 0) return RX_STATUS_OK;

	unsigned int nLayers = reduction->paletteLayers;
	unsigned int nPxSrc = width * height;

	//allocate the 4 row buffers
	unsigned int linebufSize = 4 * (width + 2) * nLayers;

	RxYiqColor *rowbuf = reduction->imgBuffer;
	if (linebufSize > RX_TEMP_IMG_BUF_SIZE) {
		//allocate a new row buffer on the heap
		rowbuf = (RxYiqColor *) RxMemCalloc(linebufSize, sizeof(RxYiqColor));
		if (rowbuf == NULL) {
			//no memory
			RxMemFree(rowbuf);
			return RX_STATUS_NOMEM;
		}
	} else {
		//clear the row buffer
		memset(rowbuf, 0, linebufSize * sizeof(RxYiqColor));
	}

	//each of the four row buffers:
	RxYiqColor *thisRow = rowbuf;                                   // the color vector for the current scanline
	RxYiqColor *lastRow = thisRow + (width + 2) * nLayers;          // the color vector for the previous scanline
	RxYiqColor *thisDiffuse = lastRow + (width + 2) * nLayers;      // the diffuse vector for the current scanline
	RxYiqColor *nextDiffuse = thisDiffuse + (width + 2) * nLayers;  // the diffuse vector for the next scanline

	//fill the previous-row buffer with the first row, to make sure we don't run out of bounds
	for (unsigned int i = 0; i < nLayers; i++) {
		COLOR32 *rgbRow = img + i * nPxSrc + 0 * width;

		for (unsigned int x = 0; x < width; x++) {
			RxConvertRgbToYiq(rgbRow[x], &lastRow[nLayers * (x + 1) + i]);
		}
	}
	RxiColorVecCopy(&lastRow[nLayers * (0)], &lastRow[nLayers * 1], nLayers);
	RxiColorVecCopy(&lastRow[nLayers * (width + 1)], &lastRow[nLayers * width], nLayers);

	//start dithering, do so in a serpentine path.
	for (unsigned int y = 0; y < height; y++) {

		//which direction?
		int hDirection = (y & 1) ? -1 : 1;

		for (unsigned int i = 0; i < nLayers; i++) {
			COLOR32 *rgbRow = img + i * nPxSrc + y * width;

			for (unsigned int x = 0; x < width; x++) {
				RxConvertRgbToYiq(rgbRow[x], &thisRow[nLayers * (x + 1) + i]);
			}
		}
		RxiColorVecCopy(&thisRow[nLayers * (0)], &thisRow[nLayers * 1], nLayers);
		RxiColorVecCopy(&thisRow[nLayers * (width + 1)], &thisRow[nLayers * width], nLayers);

		//scan across
		unsigned int startPos = (hDirection == 1) ? 0 : (width - 1);
		unsigned int x = startPos;
		for (unsigned int xPx = 0; xPx < width; xPx++) {
			//take a sample of pixels nearby. This will be a gauge of variance around this pixel, and help
			//determine if dithering should happen. Weight the sampled pixels with respect to distance from center.

			RxYiqColor colorYiq[RX_PALETTE_MAX_COUNT];
			if (adaptive) {
				for (unsigned int i = 0; i < nLayers; i++) {
#ifndef RX_SIMD
					colorYiq[i].y = (thisRow[nLayers * (x + 1) + i].y + thisRow[nLayers * (x + 2) + i].y + thisRow[nLayers * x + i].y + lastRow[nLayers * (x + 1) + i].y)
						* 0.1875f + (lastRow[nLayers * (x + 0) + i].y + lastRow[nLayers * (x + 2) + i].y) * 0.125f;
					colorYiq[i].i = (thisRow[nLayers * (x + 1) + i].i + thisRow[nLayers * (x + 2) + i].i + thisRow[nLayers * x + i].i + lastRow[nLayers * (x + 1) + i].i)
						* 0.1875f + (lastRow[nLayers * (x + 0) + i].i + lastRow[nLayers * (x + 2) + i].i) * 0.125f;
					colorYiq[i].q = (thisRow[nLayers * (x + 1) + i].q + thisRow[nLayers * (x + 2) + i].q + thisRow[nLayers * x + i].q + lastRow[nLayers * (x + 1) + i].q)
						* 0.1875f + (lastRow[nLayers * (x + 0) + i].q + lastRow[nLayers * (x + 2) + i].q) * 0.125f;
					colorYiq[i].a = (thisRow[nLayers * (x + 1) + i].a + thisRow[nLayers * (x + 2) + i].a + thisRow[nLayers * x + i].a + lastRow[nLayers * (x + 1) + i].a)
						* 0.1875f + (lastRow[nLayers * (x + 0) + i].a + lastRow[nLayers * (x + 2) + i].a) * 0.125f;
#else
					__m128 vec1 = _mm_add_ps(_mm_add_ps(thisRow[nLayers * (x + 1) + i].yiq, thisRow[nLayers * (x + 2) + i].yiq),
						_mm_add_ps(thisRow[nLayers * x + i].yiq, lastRow[nLayers * (x + 1) + i].yiq));
					__m128 vec2 = _mm_add_ps(lastRow[nLayers * x + i].yiq, lastRow[nLayers * (x + 2) + i].yiq);

					colorYiq[i].yiq = _mm_add_ps(_mm_mul_ps(vec1, _mm_set1_ps(0.1875f)), _mm_mul_ps(vec2, _mm_set1_ps(0.125f)));
#endif
				}
			} else {
				//no adaptive diffuse -> no local noise checking
				RxiColorVecCopy(colorYiq, &thisRow[nLayers * (x + 1)], nLayers);
			}

			//match it to a palette color. We'll measure distance to it as well.
			double paletteDistance = 0.0;
			unsigned int matched = RxPaletteFindClosestColorYiq(reduction, colorYiq, &paletteDistance);

			//now measure distance from the actual color to its average surroundings
			RxYiqColor *centerYiq = &thisRow[nLayers * (x + 1)];
			double centerDistance = RxiComputeLayeredColorDifference(reduction, centerYiq, colorYiq) / nLayers;

			//now test: Should we dither?
			double yw2 = reduction->yWeight2;
			if (diffuse > 0.0f && (!adaptive || (centerDistance < 110.0 * yw2 && paletteDistance >  2.0 * yw2))) {
				RxYiqColor diffuseVec[RX_PALETTE_MAX_COUNT];
				RxiColorVecCopy(diffuseVec, &thisDiffuse[nLayers * (x + 1)], nLayers);

				for (unsigned int i = 0; i < nLayers; i++) {
					RxiColorScale(&diffuseVec[i], diffuse);

					//in adaptive diffusion mode, we apply a tapering curve to the diffusion amount. This has the effect
					//of reducing extreme noise that may result from dithering. The curves limit both the immediate
					//intensity of diffusion, as well as the distance the diffusion travels. In cases where this would
					//appear, it's usually unsightly anyways. Adaptive diffusion does not work well when the palette is not
					//well-fit to the image data however, and color reduction error tends to be larger.
					if (adaptive) {
						diffuseVec[i].y = (float) RxiDiffuseCurveY(diffuseVec[i].y);
						diffuseVec[i].i = (float) RxiDiffuseCurveI(diffuseVec[i].i);
						diffuseVec[i].q = (float) RxiDiffuseCurveQ(diffuseVec[i].q);
						diffuseVec[i].a = (float) RxiDiffuseCurveA(diffuseVec[i].a);
					}

					if (flag & RX_FLAG_NO_ALPHA_DITHER) {
						//diffuse into the current color. We must unmultiply and remultiply by alpha. Doing this scales the
						//error diffused by the alpha value of the source pixel (i.e. more transparent pixels diffuse less
						//error to their neighbors), and the alpha diffusion is canceled.
						if (colorYiq[i].a != 0.0f) {
							float aFactor = 1.0f + diffuseVec[i].a / colorYiq[i].a;
							colorYiq[i].y *= aFactor;
							colorYiq[i].i *= aFactor;
							colorYiq[i].q *= aFactor;
						}

						RxiColorScale(&diffuseVec[i], colorYiq[i].a + diffuseVec[i].a);
						diffuseVec[i].a = 0.0f;
					} else {
						//alpha dithering is enabled, so we diffuse directly without adjustment.
					}

					//apply the diffusion
#ifndef RX_SIMD
					colorYiq[i].y += diffuseVec[i].y;
					colorYiq[i].i += diffuseVec[i].i;
					colorYiq[i].q += diffuseVec[i].q;
					colorYiq[i].a += diffuseVec[i].a;
#else
					colorYiq[i].yiq = _mm_add_ps(colorYiq[i].yiq, diffuseVec[i].yiq);
#endif

					if (colorYiq[i].a < 0.0f) {
						//normalize to alpha=0
						RxiColorMakeTransparent(&colorYiq[i]);
					} else {
						//clamp Y channel
						if (colorYiq[i].y < 0.0f) {
							RxiColorMakeBlack(&colorYiq[i]);
						} else if (colorYiq[i].y > 511.0f * colorYiq[i].a) {
							RxiColorMakeWhite(&colorYiq[i]);
						}

						if (colorYiq[i].a > 1.0f) {
							//normalize to alpha=1
							RxiColorMakeOpaque(&colorYiq[i]);
						}
					}
				}

				//match to palette color
				matched = RxPaletteFindClosestColorYiq(reduction, colorYiq, NULL);
				RxYiqColor *chosenYiq = &reduction->accel->plttLarge[matched * nLayers];

				//now diffuse to neighbors (mirrored with the scan direction):
				//        X  7/16
				// 3/16 5/16 1/16
				RxYiqColor *diffuse21 = &thisDiffuse[nLayers * (x + 1 + hDirection)];
				RxYiqColor *diffuse12 = &nextDiffuse[nLayers * (x + 1)];
				RxYiqColor *diffuse22 = &nextDiffuse[nLayers * (x + 1 + hDirection)];
				RxYiqColor *diffuse02 = &nextDiffuse[nLayers * (x + 1 - hDirection)];

				for (unsigned int i = 0; i < nLayers; i++) {
					RxYiqColor off;

					if (flag & RX_FLAG_NO_ALPHA_DITHER) {
						//alpha is not dithered, so we un-premultiply the colors and scale to palette alpha.
						if (colorYiq[i].a > 0.0f) {
							float chosenA = chosenYiq[i].a;
							off.y = colorYiq[i].y * chosenA / colorYiq[i].a - chosenYiq[i].y;
							off.i = colorYiq[i].i * chosenA / colorYiq[i].a - chosenYiq[i].i;
							off.q = colorYiq[i].q * chosenA / colorYiq[i].a - chosenYiq[i].q;
							off.a = 0.0f;
						} else {
							//zero alpha, no color information to dither.
							RxiColorMakeTransparent(&off);
						}
					} else {
						//alpha is dithered, so we take the straight preultiplied difference to diffuse
						//signal intensity.
						RxiColorSubtract(&off, &colorYiq[i], &chosenYiq[i]);
					}

#ifndef RX_SIMD
					diffuse21[i].y += off.y * 0.4375f; // 7/16
					diffuse21[i].i += off.i * 0.4375f;
					diffuse21[i].q += off.q * 0.4375f;
					diffuse21[i].a += off.a * 0.4375f;
					diffuse12[i].y += off.y * 0.3125f; // 5/16
					diffuse12[i].i += off.i * 0.3125f;
					diffuse12[i].q += off.q * 0.3125f;
					diffuse12[i].a += off.a * 0.3125f;
					diffuse02[i].y += off.y * 0.1875f; // 3/16
					diffuse02[i].i += off.i * 0.1875f;
					diffuse02[i].q += off.q * 0.1875f;
					diffuse02[i].a += off.a * 0.1875f;
					diffuse22[i].y += off.y * 0.0625f; // 1/16
					diffuse22[i].i += off.i * 0.0625f;
					diffuse22[i].q += off.q * 0.0625f;
					diffuse22[i].a += off.a * 0.0625f;
#else
					diffuse21[i].yiq = _mm_add_ps(diffuse21[i].yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.4375f))); // 7/16
					diffuse12[i].yiq = _mm_add_ps(diffuse12[i].yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.3125f))); // 5/16
					diffuse02[i].yiq = _mm_add_ps(diffuse02[i].yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.1875f))); // 3/16
					diffuse22[i].yiq = _mm_add_ps(diffuse22[i].yiq, _mm_mul_ps(off.yiq, _mm_set1_ps(0.0625f))); // 1/16
#endif
				}
			} else {
				//high noise area or dithering disabled, do not diffuse
				matched = RxPaletteFindClosestColorYiq(reduction, centerYiq, NULL);
			}

			//put pixel
			if (!(flag & RX_FLAG_NO_WRITEBACK)) {
				for (unsigned int i = 0; i < nLayers; i++) {
					COLOR32 chosen = RxPaletteGetColor(reduction, i, matched);

					COLOR32 *imgI = img + i * nPxSrc;
					if (touchAlpha) imgI[x + y * width] = chosen;
					else imgI[x + y * width] = (chosen & 0x00FFFFFF) | (imgI[x + y * width] & 0xFF000000);
				}
			}

			//put palette index
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
		memset(nextDiffuse, 0, nLayers * (width + 2) * sizeof(RxYiqColor));
		RxiUpdateProgress(reduction, y + 1, height);
	}

	if (rowbuf != reduction->imgBuffer) RxMemFree(rowbuf);
	return RX_STATUS_OK;
}


// ----- palette accelerator routines

static inline double RxiAccelGetChannelN(RxReduction *reduction, const RxYiqColor *color, unsigned int n) {
	RX_ASSUME(n < 4 * reduction->paletteLayers);

	double ch = color[n / 4].vec[n % 4];
	switch (n % 4) {
		case 0: return ch * reduction->yWeight;
		case 1: return ch * reduction->iWeight;
		case 2: return ch * reduction->qWeight;
		case 3: return ch * reduction->aWeight;
		default: RX_ASSUME(0); // does not reach here
	}

	//does not reach here
	return 0.0;
}

static int RxiAccelSortPalette(const void *p1, const void *p2) {
	const RxiPaletteMapEntry *e1 = (const RxiPaletteMapEntry *) p1;
	const RxiPaletteMapEntry *e2 = (const RxiPaletteMapEntry *) p2;
	if (e1->sortVal < e2->sortVal) return -1;
	if (e1->sortVal > e2->sortVal) return 1;
	return 0;
}

static RxiPaletteAccelNode *RxiAccelSplit(
	RxReduction         *reduction,
	RxiPaletteAccelNode *accel,
	RxiPaletteAccelNode *nodebuf,
	RxiPaletteMapEntry  *plttFull,
	unsigned int         nextSplit
) {
	RX_ASSUME(accel->nCol > 0);

	RxiPaletteMapEntry *pltt = plttFull + accel->start;

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
		qsort(pltt, accel->nCol, sizeof(RxiPaletteMapEntry), RxiAccelSortPalette);

		//split test
		double f1 = pltt[0].sortVal;
		double f2 = pltt[accel->nCol - 1].sortVal;
		if (f1 != f2) break;

		//else
		nextSplit = (nextSplit + 1) % nChannel;
		if (i == nChannel - 2) return nodebuf; // not split (all axes degenerate)
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
		RxiPaletteAccelNode *childL = nodebuf++;
		childL->start = accel->start;
		childL->nCol = nLeft;
		childL->parent = accel;
		accel->pLeft = childL;
		nodebuf = RxiAccelSplit(reduction, childL, nodebuf, plttFull, (nextSplit + 1) % nChannel);
	}

	if (nRight > 0) {
		RxiPaletteAccelNode *childR = nodebuf++;
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

static void RxiAccelRecurseTreeInternal(
	RxReduction         *reduction,
	RxiPaletteAccelNode *accel,
	const RxYiqColor    *color,
	double              *pBestDiff,
	unsigned int        *piBest
) {
	//distance of color to the root node
	RxBool intersectPlane = RX_FALSE;
	double projColor = RxiAccelGetChannelN(reduction, color, accel->splitDir);
	double diffFromSplit = accel->splitVal - projColor;
	diffFromSplit *= diffFromSplit;

	if (diffFromSplit < *pBestDiff) {
		intersectPlane = RX_TRUE;

		double diff = RxiComputeLayeredColorDifference(reduction, color, accel->mid->color);
		if (diff < *pBestDiff) {
			*pBestDiff = diff;
			*piBest = accel->mid->index;
		}
	}

	//based on the difference from the split, we may only need to search one child.
	//left/right nodes, if they exist, and are within the search space
	RxiPaletteAccelNode *nodeL = accel->pLeft;
	if (nodeL != NULL) {
		//left node: lesser values (search only if the splitting plane is greater)
		if (intersectPlane || projColor <= accel->splitVal) {
			RxiAccelRecurseTreeInternal(reduction, nodeL, color, pBestDiff, piBest);
		}
	}

	RxiPaletteAccelNode *nodeR = accel->pRight;
	if (nodeR != NULL) {
		//right node: greater values (seeach only if the splitting plane is lesser)
		if (intersectPlane || projColor >= accel->splitVal) {
			RxiAccelRecurseTreeInternal(reduction, nodeR, color, pBestDiff, piBest);
		}
	}
}

static void RxiAccelRecurseTree(
	RxReduction         *reduction,
	RxiPaletteAccelNode *accel,
	const RxYiqColor    *color,
	double              *pBestDiff,
	unsigned int        *piBest,
	int                  lrbit
) {
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
		RxiPaletteAccelNode *sub = lrbit ? accel->pLeft : accel->pRight;
		if (sub != NULL) RxiAccelRecurseTreeInternal(reduction, sub, color, pBestDiff, piBest);
	}
}

static unsigned int RxiPaletteFindClosestColorAccelerated(
	RxReduction      *reduction,
	RxPalette        *accel,
	const RxYiqColor *color,
	double           *outDiff
) {
	unsigned int nLayer = reduction->paletteLayers;

	//traverse down
	RxiPaletteAccelNode *nodep = &accel->root;
	while (1) {
		double split = nodep->splitVal;
		double val = RxiAccelGetChannelN(reduction, color, nodep->splitDir);
		RxYiqColor *nodeCol = nodep->mid->color;

		//if this is a leaf node or the split value matches, check for a matching color
		if ((nodep->pLeft == NULL && nodep->pRight == NULL) || (split == val)) {
			//compare color
			if (RxiColorVecEqual(nodeCol, color, nLayer)) {
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

static unsigned int RxiPaletteFindClosestColorOnAccel(
	RxReduction      *reduction,
	RxPalette        *accel,
	const RxYiqColor *color,
	double           *outDiff
) {
	RX_ASSUME(accel != NULL);

	RxYiqColor *cpy = reduction->tempLayeredColor;
	RxiColorVecCopy(cpy, color, reduction->paletteLayers);

	//processing for alpha mode
	unsigned int plttStart = 0;
	switch (reduction->accel->alphaMode) {
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
		return RxiPaletteFindClosestColorAccelerated(reduction, accel, cpy, outDiff) + plttStart;
	} else {
		//slow search
		RxYiqColor *pltt = &accel->plttLarge[plttStart * reduction->paletteLayers];
		return RxiPaletteFindClosestColor(reduction, pltt, accel->nPltt - plttStart, cpy, outDiff) + plttStart;
	}
}

unsigned int RX_API RxPaletteFindClosestColorYiq(RxReduction *reduction, const RxYiqColor *color, double *outDiff) {
	RxPalette *accel = reduction->accel;
	if (accel == NULL) {
		//not initialized
		if (outDiff != NULL) *outDiff = RX_LARGE_NUMBER;
		return 0;
	}

	return RxiPaletteFindClosestColorOnAccel(reduction, accel, color, outDiff);
}

unsigned int RX_API RxPaletteFindClosestColor(RxReduction *reduction, COLOR32 color, double *outDiff) {
	RxYiqColor yiq;
	RxConvertRgbToYiq(color, &yiq);
	return RxPaletteFindClosestColorYiq(reduction, &yiq, outDiff);
}

static RxStatus RxiPaletteAllocCurrent(RxReduction *reduction) {
	RxPalette *accel = (RxPalette *) RxMemCalloc(1, sizeof(RxPalette));
	if (accel == NULL) return reduction->status = RX_STATUS_NOMEM;

	reduction->accel = accel;
	return RX_STATUS_OK;
}

static RxStatus RxiPaletteAllocColors(RxReduction *reduction, unsigned int nCol) {
	RxPalette *accel = reduction->accel;
	RX_ASSUME(accel != NULL);

	if (nCol * reduction->paletteLayers > sizeof(accel->plttSmall) / sizeof(accel->plttSmall[0])) {
		//above small threshold --> allocate on the heap
		accel->plttLarge = (RxYiqColor *) RxMemCalloc(nCol * reduction->paletteLayers, sizeof(RxYiqColor));
	} else {
		//within small threshold --> use small buffer
		accel->plttLarge = accel->plttSmall;
	}

	if (accel->plttLarge == NULL) {
		reduction->status = RX_STATUS_NOMEM;
	} else {
		accel->nPltt = nCol;
	}
	return reduction->status;
}

static RxStatus RxiPaletteLoadAccelerated(RxReduction *reduction) {
	//the K-D tree is incompatible with the palette with palette alpha.
	RxAlphaMode alphaMode = reduction->accel->alphaMode;
	if (alphaMode == RX_ALPHA_PALETTE) return RX_STATUS_INVALID;

	unsigned int iStart = 0;
	if (alphaMode == RX_ALPHA_RESERVE) iStart = 1; // skip 1st color in reserve mode

	RxPalette *accel = reduction->accel;
	RxYiqColor *pltt = accel->plttLarge;
	unsigned int nColors = accel->nPltt;
	if (nColors > 0) {
		nColors -= iStart;
		pltt += iStart * reduction->paletteLayers;
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
	accel->pltt = (RxiPaletteMapEntry *) RxMemCalloc(nColors, sizeof(RxiPaletteMapEntry));
	accel->nodebuf = (RxiPaletteAccelNode *) calloc(nColors, sizeof(RxiPaletteAccelNode));

	if (accel->pltt == NULL || accel->nodebuf == NULL) {
		//no memory
		RxMemFree(accel->pltt);
		free(accel->nodebuf);
		return reduction->status = RX_STATUS_NOMEM;
	}

	for (unsigned int i = 0; i < nColors; i++) {
		accel->pltt[i].index = i;
		RxiColorVecCopy(accel->pltt[i].color, &pltt[i * reduction->paletteLayers], reduction->paletteLayers);

		if (alphaMode == RX_ALPHA_PIXEL) {
			for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
				RxiColorMakeOpaque(&accel->pltt[i].color[j]);
			}
		}
	}

	accel->useAccelerator = RX_TRUE;
	accel->root.parent = NULL;
	accel->root.start = 0;
	accel->root.nCol = nColors;
	accel->root.pLeft = NULL;
	accel->root.pRight = NULL;
	RxiAccelSplit(reduction, &accel->root, accel->nodebuf, accel->pltt, 0);

	return RX_STATUS_OK;
}

static RxStatus RxiPaletteLoadUnaccelerated(RxReduction *reduction, const COLOR32 *pltt, unsigned int nColors) {
	RxStatus status = RxiPaletteAllocColors(reduction, nColors);
	if (status != RX_STATUS_OK) return reduction->status = status;

	for (unsigned int j = 0; j < reduction->paletteLayers; j++) {
		for (unsigned int i = 0; i < nColors; i++) {
			RxConvertRgbToYiq(pltt[j * nColors + i], &reduction->accel->plttLarge[i * reduction->paletteLayers + j]);
		}
	}

	return reduction->status;
}

static RxStatus RxiPaletteLoadYiqUnaccelerated(RxReduction *reduction, const RxYiqColor *pltt, unsigned int nColors, unsigned int srcPitch) {
	//this is for loading palettes from internal representation, where transparent color-0
	//is not reserved. We adjust the indexes here so that our palette accelerator does not
	//have to care.
	unsigned int iStart = 0;
	if (reduction->accel->alphaMode == RX_ALPHA_RESERVE) {
		iStart++;
	}

	RxStatus status = RxiPaletteAllocColors(reduction, nColors + iStart);
	if (status != RX_STATUS_OK) return status;

	unsigned int nLayer = reduction->paletteLayers;
	for (unsigned int i = 0; i < nColors; i++) {
		RxiColorVecCopy(&reduction->accel->plttLarge[(i + iStart) * nLayer], &pltt[i * srcPitch], nLayer);
	}

	return RX_STATUS_OK;
}

RxStatus RX_API RxPaletteLoad(RxReduction *reduction, const COLOR32 *pltt, unsigned int nColors) {
	//if an accelerator is loaded already, unload it.
	RxPaletteFree(reduction);

	RxStatus status = RxiPaletteAllocCurrent(reduction);
	if (status != RX_STATUS_OK) return status;

	//set alpha mode
	RxPalette *accel = reduction->accel;
	accel->alphaMode = reduction->alphaMode;

	//in all cases, we load without the accelerator first
	status = RxiPaletteLoadUnaccelerated(reduction, pltt, nColors);
	if (status != RX_STATUS_OK) return reduction->status = status;

	if (nColors > 16) {
		//number of colors is high enough to benefit from acceleration
		RxiPaletteLoadAccelerated(reduction);
	}

	return reduction->status;
}

static RxStatus RxiPaletteLoadYiq(RxReduction *reduction, const RxYiqColor *pltt, unsigned int srcPitch, unsigned int nColors, RxBool overrideMode) {
	//if an accelerator is loaded already, unload it.
	RxPaletteFree(reduction);

	RxStatus status = RxiPaletteAllocCurrent(reduction);
	if (status != RX_STATUS_OK) return reduction->status = status;

	//set alpha mode
	RxPalette *accel = reduction->accel;
	accel->alphaMode = reduction->alphaMode;

	if (overrideMode) {
		//for the internal YIQ palette load, we do not use transparency reserve mode. The internal palette
		//does not keep the reserved color for transparency, and transparent colors will not be mapped to
		//the palette. Thus, we change the alpha mode to "none" to disable these behaviors.
		if (accel->alphaMode == RX_ALPHA_RESERVE) accel->alphaMode = RX_ALPHA_NONE;
	}

	//in all cases, we load without the accelerator first
	status = RxiPaletteLoadYiqUnaccelerated(reduction, pltt, nColors, srcPitch);
	if (status != RX_STATUS_OK) return reduction->status = status;

	if (nColors > 16) {
		//number of colors is high enough to benefit from acceleration
		RxiPaletteLoadAccelerated(reduction);
	}

	return reduction->status;
}

COLOR32 RX_API RxPaletteGetColor(RxReduction *reduction, unsigned int iLayer, unsigned int iColor) {
	RX_ASSUME(reduction->accel != NULL);
	RX_ASSUME(iLayer < reduction->paletteLayers);
	RX_ASSUME(iColor < reduction->accel->nPltt);

	//get the color at the index
	const RxYiqColor *yiq = &reduction->accel->plttLarge[iColor * reduction->paletteLayers + iLayer];
	return RxConvertYiqToRgb(yiq);
}

RxStatus RX_API RxPaletteGetColorYiq(RxReduction *reduction, unsigned int iLayer, unsigned int iColor, RxYiqColor *col) {
	if (reduction->accel == NULL) return RX_STATUS_INCORRECT_STATE;
	if (iLayer >= reduction->paletteLayers || iColor >= reduction->accel->nPltt) return RX_STATUS_INVALID;

	//get the color at the index
	const RxYiqColor *yiq = &reduction->accel->plttLarge[iColor * reduction->paletteLayers + iLayer];
	RxiColorCopy(col, yiq);
	return RX_STATUS_OK;
}

static void RxiPaletteFree(RxPalette *palette) {
	if (palette->plttLarge != palette->plttSmall) RxMemFree(palette->plttLarge);
	RxMemFree(palette->pltt);
	free(palette->nodebuf);
	RxMemFree(palette);
}

void RX_API RxPaletteFree(RxReduction *reduction) {
	if (reduction->accel == NULL) return;

	RxiPaletteFree(reduction->accel);
	reduction->accel = NULL;
}

double RX_API RxComputePaletteError(RxReduction *reduction, const COLOR32 *px, unsigned int width, unsigned int height, const COLOR32 *pal, unsigned int nColors, double nMaxError) {
	if (nMaxError == 0) nMaxError = RX_LARGE_NUMBER;
	double error = 0;

	RxYiqColor *paletteYiq = reduction->imgBuffer;
	if (nColors > RX_TEMP_IMG_BUF_SIZE) {
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
		RxConvertRgbToYiq(p, &yiq);
		double bestDiff;
		(void) RxiPaletteFindClosestColor(reduction, paletteYiq, nColors, &yiq, &bestDiff);

		error += bestDiff;
		if (error >= nMaxError) {
			error = nMaxError;
			break;
		}
	}

	if (paletteYiq != reduction->imgBuffer) RxMemFree(paletteYiq);
	return error;
}
