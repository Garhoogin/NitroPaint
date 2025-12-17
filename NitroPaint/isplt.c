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

#define TRUE 1
#define FALSE 0

#define RX_LARGE_NUMBER             1e32 // constant to represent large color difference
#define RX_SLAB_SIZE            0x100000 // slab size of allocator
#define INV_512    0.0019531250000000000 // 1.0/512.0
#define INV_511    0.0019569471624266144 // 1.0/511.0
#define INV_255    0.0039215686274509800 // 1.0/255.0
#define INV_3      0.3333333333333333333 // 1.0/  3.0
#define TWO_THIRDS 0.6666666666666666667 // 2.0/  3.0

//struct for internal processing of color leaves
typedef struct {
	double y;
	double i;
	double q;
	double a;
	double weightL;
} RxiClusterSum;

static int RxiPaletteFindClosestColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, const RxYiqColor *col, double *outDiff);


// ----- memory allocation wrappers for SIMD use

#ifdef RX_SIMD

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


static COLOR32 RxMaskColorToDS15(COLOR32 c) {
	//DS mode masking: round color channels to 5-bit values, and force alpha=0xff
	return ColorRoundToDS15(c) | 0xFF000000;
}

static COLOR32 RxMaskColorDummy(COLOR32 c) {
	//no-mask dummy: pass colors, forcing RGB to 0 when alpha=0
	if ((c >> 24) == 0) c = 0;
	return c;
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

void RxSetBalance(RxReduction *reduction, int balance, int colorBalance, int enhanceColors) {
	reduction->yWeight = 60 - balance;       // high balance -> lower Y weight
	reduction->iWeight = colorBalance;       // high color balance -> high I weight
	reduction->qWeight = 40 - colorBalance;  // high color balance -> low Q weight

	reduction->yWeight2 = reduction->yWeight * reduction->yWeight; // Y weight squared
	reduction->iWeight2 = reduction->iWeight * reduction->iWeight; // I weight squared
	reduction->qWeight2 = reduction->qWeight * reduction->qWeight; // Q weight squared

	//color difference is computed by taking the average squared difference between colors composited onto
	//a background color. This calculation requires the first and second moments of the color space, and we
	//assume uniformly distributed RGB colors to compute this.
	reduction->aWeight2 = 0.893465463 * reduction->yWeight2 + 0.179131315 * reduction->iWeight2 + 0.138788427 * reduction->qWeight2;
	reduction->aWeight = sqrt(reduction->aWeight2);

	reduction->enhanceColors = enhanceColors;

	//when using SIMD, set vector weights
#ifdef RX_SIMD
	__m128 weightAQ = _mm_cvtpd_ps(reduction->qaWeight2);
	__m128 weightIY = _mm_cvtpd_ps(reduction->yiWeight2);
	__m128 weight2 = _mm_shuffle_ps(_mm_shuffle_ps(weightIY, weightIY, _MM_SHUFFLE(1, 0, 1, 0)), weightAQ, _MM_SHUFFLE(1, 0, 3, 2));
	reduction->yiqaWeight2 = weight2;
#endif
}

void RxInit(RxReduction *reduction, int balance, int colorBalance, int enhanceColors, unsigned int nColors) {
	memset(reduction, 0, sizeof(RxReduction));
	RxSetBalance(reduction, balance, colorBalance, enhanceColors);

	reduction->nReclusters = RECLUSTER_DEFAULT;
	reduction->nPaletteColors = nColors;
	reduction->gamma = 1.27;
	reduction->maskColors = RxMaskColorToDS15;
	reduction->alphaMode = RX_ALPHA_NONE; // default: no alpha processing
	reduction->alphaThreshold = 0x80;     // default: alpha threshold =128

	for (int i = 0; i < 512; i++) {
		reduction->lumaTable[i] = pow((double) i * INV_511, 1.27) * 511.0;
	}
	reduction->status = RX_STATUS_OK;
}

RxReduction *RxNew(int balance, int colorBalance, int enhanceColors, unsigned int nColors) {
	RxReduction *reduction = (RxReduction *) RxMemCalloc(1, sizeof(RxReduction));
	if (reduction == NULL) return NULL;

	RxInit(reduction, balance, colorBalance, enhanceColors, nColors);
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

static void RxiSlabFree(RxSlab *allocator) {
	if (allocator->allocation != NULL) RxMemFree(allocator->allocation);
	allocator->allocation = NULL;

	if (allocator->next != NULL) {
		RxiSlabFree(allocator->next);
		allocator->next = NULL;
	}
}

static inline unsigned int RxiHistHashColor(const RxYiqColor *yiq) {
#ifndef RX_SIMD
	int yi = (int) yiq->y, ii = (int) yiq->i, qi = (int) yiq->q, ai = (int) yiq->a;
	return (qi + (yi * 64 + ii) * 4 + ai) & 0x1FFFF;
#else
	__m128i yiqI = _mm_cvtps_epi32(_mm_mul_ps(yiq->yiq, _mm_set_ps(1.0f, 1.0f, 4.0f, 256.0f)));
	__m128i sum = _mm_add_epi32(yiqI, _mm_shuffle_epi32(yiqI, _MM_SHUFFLE(2, 3, 0, 1)));
	sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, _MM_SHUFFLE(0, 1, 2, 3)));
	return _mm_cvtsi128_si32(sum) & 0x1FFFF;
#endif
}

static void RxHistAddColor(RxReduction *reduction, const RxYiqColor *col, double weight) {
	RxHistogram *histogram = reduction->histogram;
	if (reduction->status != RX_STATUS_OK) return;

	RxYiqColor yiq;
	memcpy(&yiq, col, sizeof(RxYiqColor));

	//process the alpha value.
	switch (reduction->alphaMode) {
		case RX_ALPHA_NONE:
		case RX_ALPHA_RESERVE:
			//we use tha alpha threshold here since these alpha modes imply binary alpha.
			if (yiq.a < (float) reduction->alphaThreshold) return;
			yiq.a = 255.0f;
			break;

		case RX_ALPHA_PIXEL:
			//we'll discard alpha=0 since it doesn't need to appear in the palette.
			if (yiq.a == 0.0f) return;
			weight *= yiq.a * INV_255;
			yiq.a = 255.0f;
			break;

		case RX_ALPHA_PALETTE:
			//we explicitly must pass all alpha values.
			if (yiq.a == 0.0f) {
				//for alpha=0, we'll set YIQ to 0 so we only have one transparent value.
				yiq.y = yiq.i = yiq.q = 0.0f;
			}
			break;
	}

	int slotIndex = RxiHistHashColor(&yiq);
	if (slotIndex < histogram->firstSlot) histogram->firstSlot = slotIndex;

	//find a slot with the same YIQA, or create a new one if none exists.
	RxHistEntry **ppslot = &histogram->entries[slotIndex];
	while (*ppslot != NULL) {
		RxHistEntry *slot = *ppslot;

		//matching slot? add weight
		if (memcmp(&slot->color, &yiq, sizeof(RxYiqColor)) == 0) {
			slot->weight += weight;
			return;
		}

		ppslot = &slot->next;
	}

	RxHistEntry *slot = (RxHistEntry *) RxiSlabAlloc(&histogram->allocator, sizeof(RxHistEntry));;
	if (slot == NULL) {
		reduction->status = RX_STATUS_NOMEM;
		return;
	}

	//put new color
	*ppslot = slot;
	memcpy(&slot->color, &yiq, sizeof(yiq));
	slot->weight = weight;
	slot->next = NULL;
	slot->value = 0.0;
	histogram->nEntries++;
	histogram->totalWeight += weight;
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
	yiq->y = (float) y; //    0 - 511
	yiq->i = (float) i; // -320 - 319
	yiq->q = (float) q; // -270 - 269
	yiq->a = (float) ((rgb >> 24) & 0xFF);
#else
	//vectorized implementation
	__m128i rgbVeci = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(rgb), _mm_setzero_si128()), _mm_setzero_si128());
	__m128 rgbVec = _mm_cvtepi32_ps(rgbVeci);
	
	__m128 yVec = _mm_mul_ps(rgbVec, _mm_set_ps(0.0f,  0.22800f,  1.17400f, 0.59800f));
	__m128 iVec = _mm_mul_ps(rgbVec, _mm_set_ps(0.0f, -0.64406f, -0.54804f, 1.19208f));
	__m128 qVec = _mm_mul_ps(rgbVec, _mm_set_ps(0.0f,  0.62206f, -1.04408f, 0.42204f));

	//do three horizontal sums
	yVec = _mm_add_ps(yVec, _mm_shuffle_ps(yVec, yVec, _MM_SHUFFLE(2, 3, 0, 1)));
	iVec = _mm_add_ps(iVec, _mm_shuffle_ps(iVec, iVec, _MM_SHUFFLE(0, 1, 2, 3)));
	qVec = _mm_add_ps(qVec, _mm_shuffle_ps(qVec, qVec, _MM_SHUFFLE(0, 1, 2, 3)));

	__m128 iqVec = _mm_shuffle_ps(iVec, qVec, _MM_SHUFFLE(0, 1, 0, 1)); // lo: half I sum, hi: half Q sum, halves reversed
	yVec = _mm_add_ss(yVec, _mm_movehl_ps(yVec, yVec));
	iqVec = _mm_add_ps(iqVec, _mm_shuffle_ps(iqVec, iqVec, _MM_SHUFFLE(2, 3, 0, 1)));

	//sums distributed horizontally, so we mask them out
	__m128 iqMask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, 0));
	__m128 aMask = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
	__m128 yiqVec = _mm_move_ss(_mm_and_ps(iqVec, iqMask), yVec);
	
	//apply soft clamping by I>245, Q<-215 by 2/3
	__m128 excess = _mm_sub_ps(yiqVec, _mm_set_ps(0.0f, -215.0f, 245.0f, 0.0f));     // I excess of 245, Q excess of -215
	excess = _mm_mul_ps(excess, _mm_set_ps(0.0f, 0.33333333f, -0.33333333f, 0.0f));  // Correction factor of 1/3
	excess = _mm_min_ps(excess, _mm_setzero_ps());                                   // Clamp to non-positives
	excess = _mm_mul_ps(excess, _mm_set_ps(0.0f, -1.0f, 1.0f, 0.0f));                // Correct sign of Q bias
	yiqVec = _mm_add_ps(yiqVec, excess);

	//soft clamp on Q-I difference
	__m128 iqDiff = _mm_sub_ss(_mm_movehl_ps(yiqVec, yiqVec), _mm_shuffle_ps(yiqVec, yiqVec, _MM_SHUFFLE(1, 1, 1, 1)));
	__m128 diq = _mm_sub_ss(iqDiff, _mm_set_ss(265.0f));

	// if (diq >= 0.0)
	{
		diq = _mm_max_ss(diq, _mm_setzero_ps());                      // if (diq < 0.0) diq = 0.0
		diq = _mm_shuffle_ps(diq, diq, _MM_SHUFFLE(0, 0, 0, 0));      // distribute across vector register
		diq = _mm_mul_ps(diq, _mm_set_ps(0.0f, -0.25f, 0.25f, 0.0f)); // scale by 0.25 and make Q difference negative
		yiqVec = _mm_add_ps(yiqVec, diq);
	}

	iVec = _mm_shuffle_ps(yiqVec, yiqVec, _MM_SHUFFLE(1, 1, 1, 1));
	qVec = _mm_movehl_ps(yiqVec, yiqVec);
	// if (i < 0.0 && q > 0.0)
	{
		__m128 sub = _mm_mul_ss(_mm_mul_ss(_mm_min_ss(iVec, _mm_setzero_ps()), _mm_max_ss(qVec, _mm_setzero_ps())), _mm_set_ss((float) INV_512));
		yiqVec = _mm_sub_ss(yiqVec, sub);
	}

	//insert alpha channel to the output vector
	yiq->yiq = _mm_add_ps(yiqVec, _mm_and_ps(rgbVec, aMask));
#endif
}

COLOR32 RxConvertYiqToRgb(const RxYiqColor *yiq) {
	double i = (double) yiq->i;
	double q = (double) yiq->q;
	double y = (double) yiq->y;
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
	int a = (int) (yiq->a + 0.5);

	//pack clamped color
	r = min(max(r, 0), 255);
	g = min(max(g, 0), 255);
	b = min(max(b, 0), 255);
	a = min(max(a, 0), 255);
	return r | (g << 8) | (b << 16) | (a << 24);
}

static inline double RxiDelinearizeLuma(RxReduction *reduction, double luma) {
	return 511.0 * pow(luma * INV_511, 1.0 / reduction->gamma);
}

static inline double RxiLinearizeLuma(RxReduction *reduction, double luma) {
	RX_ASSUME(luma >= 0 && luma < 512);
#ifndef RX_SIMD
	return reduction->lumaTable[(int) (luma + 0.5)];
#else
	return reduction->lumaTable[_mm_cvtsd_si32(_mm_set_sd(luma))];
#endif
}

static inline COLOR32 RxiMaskYiqToRgb(RxReduction *reduction, const RxYiqColor *yiq) {
	return reduction->maskColors(RxConvertYiqToRgb(yiq));
}

static inline double RxiComputeColorDifference(RxReduction *reduction, const RxYiqColor *yiq1, const RxYiqColor *yiq2) {
	if (yiq1->a == yiq2->a) {
		//equal alpha comparison. Because each color is scaled by the same alpha, we can pull it out by
		//multiplying YIQ squared difference by squared alpha.
#ifndef RX_SIMD
		double yw2 = reduction->yWeight2;
		double iw2 = reduction->iWeight2;
		double qw2 = reduction->qWeight2;
		double aw2 = reduction->aWeight2;

		double dy = RxiLinearizeLuma(reduction, yiq1->y) - RxiLinearizeLuma(reduction, yiq2->y);
		double di = yiq1->i - yiq2->i;
		double dq = yiq1->q - yiq2->q;
		double d2 = yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq;

		if (yiq1->a != 255.0f) {
			//translucent scale factor
			double a = yiq1->a * INV_255;
			d2 *= a * a;
		}
		return d2;
#else // RX_SIMD
		__m128 v1 = yiq1->yiq, v2 = yiq2->yiq;
		
		__m128 yMask = _mm_castsi128_ps(_mm_set_epi32(-1, -1, -1, 0));
		__m128d y1 = _mm_load_sd(&reduction->lumaTable[_mm_cvt_ss2si(v1)]); // delinearlize luma
		__m128d y2 = _mm_load_sd(&reduction->lumaTable[_mm_cvt_ss2si(v2)]);
		__m128 dy = _mm_cvtsd_ss(_mm_setzero_ps(), _mm_sub_sd(y1, y2));

		__m128 diff = _mm_sub_ps(v1, v2);
		diff = _mm_or_ps(_mm_and_ps(diff, yMask), dy);

		__m128 diff2 = _mm_mul_ps(_mm_mul_ps(diff, diff), reduction->yiqaWeight2);

		__m128 d2Temp = _mm_shuffle_ps(diff2, diff2, _MM_SHUFFLE(2, 3, 0, 1));
		diff2 = _mm_add_ps(diff2, d2Temp);
		d2Temp = _mm_shuffle_ps(diff2, diff2, _MM_SHUFFLE(0, 1, 2, 3));
		diff2 = _mm_add_ps(diff2, d2Temp);

		__m128 a2 = _mm_set_ss((float) yiq1->a);
		a2 = _mm_mul_ss(a2, _mm_set_ss((float) INV_255));
		a2 = _mm_mul_ss(a2, a2);
		diff2 = _mm_mul_ss(diff2, a2);
		return (double) diff2.m128_f32[0];
#endif
	} else {
		double yw2 = reduction->yWeight2;
		double iw2 = reduction->iWeight2;
		double qw2 = reduction->qWeight2;
		double aw2 = reduction->aWeight2;

		//color difference with alpha.
		double a1 = yiq1->a * INV_255, a2 = yiq2->a * INV_255;

		//scale color by alpha for comparison
		double y1 = a1 * RxiLinearizeLuma(reduction, yiq1->y), y2 = a2 * RxiLinearizeLuma(reduction, yiq2->y);
		double i1 = a1 * yiq1->i, i2 = a2 * yiq2->i;
		double q1 = a1 * yiq1->q, q2 = a2 * yiq2->q;
		double dy = y1 - y2, di = i1 - i2, dq = q1 - q2;
		double da = yiq1->a - yiq2->a;

		//coefficients below taken from first moment of YIQ space given uniform RGB distribution
		return yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq + aw2 * da * da
			- 2.0 * da * (0.857138537 * yw2 * dy - 0.0000496078431 * iw2 * di + 0.0000369686275 * qw2 * dq);
	}
}

double RxComputeColorDifference(RxReduction *reduction, const RxYiqColor *yiq1, const RxYiqColor *yiq2) {
	return RxiComputeColorDifference(reduction, yiq1, yiq2);
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

	for (int i = reduction->histogram->firstSlot; i < 0x20000; i++) {
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
		reduction->histogram = (RxHistogram *) calloc(1, sizeof(RxHistogram));
		if (reduction->histogram == NULL) return RX_STATUS_NOMEM;

		reduction->histogram->firstSlot = 0x20000;
	}
	
	if (width == 0 || height == 0) return reduction->status;

	for (unsigned int y = 0; y < height; y++) {
		//track block of 3 pixels
		RxYiqColor rowBlock[3];
		RxConvertRgbToYiq(img[y * width + 0], &rowBlock[0]);     // left
		memcpy(&rowBlock[1], &rowBlock[0], sizeof(RxYiqColor));  // center
		memcpy(&rowBlock[2], &rowBlock[1], sizeof(RxYiqColor));  // right

		for (unsigned int x = 0; x < width; x++) {
			//fill right pixel
			if ((x + 1) < width) {
				RxConvertRgbToYiq(img[y * width + x + 1], &rowBlock[2]);
			}

			//top and bottom pixel
			RxYiqColor top, bottom;
			if (y > 0) RxConvertRgbToYiq(img[(y - 1) * width + x], &top);
			else memcpy(&top, &rowBlock[1], sizeof(RxYiqColor));
			if ((y + 1) < height) RxConvertRgbToYiq(img[(y + 1) * width + x], &bottom);
			else memcpy(&bottom, &rowBlock[1], sizeof(RxYiqColor));

			//compute weight
			double yInter = 0.25 * (RxiLinearizeLuma(reduction, rowBlock[0].y) + RxiLinearizeLuma(reduction, rowBlock[1].y)
				+ RxiLinearizeLuma(reduction, top.y) + RxiLinearizeLuma(reduction, bottom.y));
			double yCenter = RxiLinearizeLuma(reduction, rowBlock[1].y);
			double yDiff = fabs(yCenter - yInter);
			double weight = 16.0 - fabs(16.0 - yDiff) / 8.0;
			if (weight < 1.0) weight = 1.0;
			RxHistAddColor(reduction, &rowBlock[1], weight);

			//slide row
			memmove(&rowBlock[0], &rowBlock[1], 2 * sizeof(RxYiqColor));
		}
	}
	return reduction->status;
}

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

static void RxiHistComputePrincipal(RxReduction *reduction, int startIndex, int endIndex, double *axis) {
	double mtx[4][4] = { 0 };
	double x0bar = 0.0, x1bar = 0.0, x2bar = 0.0, x3bar = 0.0;
	double sumWeight = 0.0;

	//compute the covariance matrix for the input range of colors.
	for (int i = startIndex; i < endIndex; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];

		double x0 = reduction->yWeight * RxiLinearizeLuma(reduction, entry->color.y);
		double x1 = reduction->iWeight * entry->color.i;
		double x2 = reduction->qWeight * entry->color.q;
		double x3 = reduction->aWeight * entry->color.a;
		double weight = entry->weight;

		//covariances (upper diagonal)
		mtx[0][0] += weight * x0 * x0;
		mtx[0][1] += weight * x0 * x1;
		mtx[0][2] += weight * x0 * x2;
		mtx[0][3] += weight * x0 * x3;
		mtx[1][1] += weight * x1 * x1;
		mtx[1][2] += weight * x1 * x2;
		mtx[1][3] += weight * x1 * x3;
		mtx[2][2] += weight * x2 * x2;
		mtx[2][3] += weight * x2 * x3;
		mtx[3][3] += weight * x3 * x3;

		x0bar += weight * x0;
		x1bar += weight * x1;
		x2bar += weight * x2;
		x3bar += weight * x3;
		sumWeight += weight;
	}
	x0bar /= sumWeight;
	x1bar /= sumWeight;
	x2bar /= sumWeight;
	x3bar /= sumWeight;

	mtx[0][0] = mtx[0][0] / sumWeight - x0bar * x0bar;
	mtx[0][1] = mtx[0][1] / sumWeight - x0bar * x1bar;
	mtx[0][2] = mtx[0][2] / sumWeight - x0bar * x2bar;
	mtx[0][3] = mtx[0][3] / sumWeight - x0bar * x3bar;
	mtx[1][0] = mtx[0][1];
	mtx[1][1] = mtx[1][1] / sumWeight - x1bar * x1bar;
	mtx[1][2] = mtx[1][2] / sumWeight - x1bar * x2bar;
	mtx[1][3] = mtx[1][3] / sumWeight - x1bar * x3bar;
	mtx[2][0] = mtx[0][2];
	mtx[2][1] = mtx[1][2];
	mtx[2][2] = mtx[2][2] / sumWeight - x2bar * x2bar;
	mtx[2][3] = mtx[2][3] / sumWeight - x2bar * x3bar;
	mtx[3][0] = mtx[0][3];
	mtx[3][1] = mtx[1][3];
	mtx[3][2] = mtx[2][3];
	mtx[3][3] = mtx[3][3] / sumWeight - x3bar * x3bar;

	//fill identity
	double E[4][4] = { 0 };
	for (int i = 0; i < 4; i++) E[i][i] = 1.0;

	double z[4] = { 0 };
	double e[4] = { 0 };
	double b[4] = { 0 };
	for (int i = 0; i < 4; i++) {
		e[i] = mtx[i][i];
		b[i] = e[i];
	}
	
	for (int iter = 0; iter < 1000; iter++) {
		//sum above upper diagonal
		double sum = 0.0;
		for (int k = 0; k < 3; k++) {
			for (int l = k + 1; l < 4; l++) {
				sum += fabs(mtx[k][l]);
			}
		}
		if (sum == 0.0) break;

		double th = 0.0;
		if (iter < 4) th = 0.0125 * sum;

		for (int k = 0; k < 3; k++) {
			for (int l = k + 1; l < 4; l++) {
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
				for (int i = 0; i < k; i++) {
					double Sik = mtx[i][k], Sil = mtx[i][l];
					mtx[i][k] = c * Sik - s * Sil;
					mtx[i][l] = s * Sik + c * Sil;
				}
				for (int i = k + 1; i < l; i++) {
					double Ski = mtx[k][i], Sil = mtx[i][l];
					mtx[k][i] = c * Ski - s * Sil;
					mtx[i][l] = s * Ski + c * Sil;
				}
				for (int i = l + 1; i < 4; i++) {
					double Ski = mtx[k][i], Sli = mtx[l][i];
					mtx[k][i] = c * Ski - s * Sli;
					mtx[l][i] = s * Ski + c * Sli;
				}

				for (int i = 0; i < 4; i++) {
					double Eik = E[i][k], Eil = E[i][l];
					E[i][k] = c * Eik - s * Eil;
					E[i][l] = s * Eik + c * Eil;
				}
			}
		}
		for (int k = 0; k < 4; k++) {
			b[k] += z[k];
			z[k] = 0;
			e[k] = b[k];
		}
	}

	//e now holds the eigenvalues. Negate negative one to compare magnitudes.
	if (e[0] < 0.0) e[0] = -e[0];
	if (e[1] < 0.0) e[1] = -e[1];
	if (e[2] < 0.0) e[2] = -e[2];
	if (e[3] < 0.0) e[3] = -e[3];

	//select the eigenvector with the greatest absolute eigenvalue.
	int eigenNo = 0;
	if (e[1] > e[eigenNo]) eigenNo = 1;
	if (e[2] > e[eigenNo]) eigenNo = 2;
	if (e[3] > e[eigenNo]) eigenNo = 3;

	axis[0] = E[0][eigenNo];
	axis[1] = E[1][eigenNo];
	axis[2] = E[2][eigenNo];
	axis[3] = E[3][eigenNo];
}

static int RxiHistEntryComparator(const void *p1, const void *p2) {
	const RxHistEntry *e1 = *(const RxHistEntry **) p1;
	const RxHistEntry *e2 = *(const RxHistEntry **) p2;

	double d = e1->value - e2->value;
	if (d < 0.0) return -1;
	if (d > 0.0) return 1;
	return 0;
}

static inline double RxiVec4Mag(double x, double y, double z, double w) {
	return x * x + y * y + z * z + w * w;
}

void RxHistSort(RxReduction *reduction, int startIndex, int endIndex) {
	double principal[4];
	int nColors = endIndex - startIndex;
	RxHistEntry **thisHistogram = &reduction->histogramFlat[startIndex];
	RxiHistComputePrincipal(reduction, startIndex, endIndex, principal);

	//check principal component, make sure principal[0] >= 0
	if (principal[0] < 0) {
		principal[0] = -principal[0];
		principal[1] = -principal[1];
		principal[2] = -principal[2];
		principal[3] = -principal[3];
	}

	double yWeight = principal[0] * reduction->yWeight;
	double iWeight = principal[1] * reduction->iWeight;
	double qWeight = principal[2] * reduction->qWeight;
	double aWeight = principal[3] * reduction->aWeight;

	for (int i = startIndex; i < endIndex; i++) {
		RxHistEntry *histEntry = reduction->histogramFlat[i];
		double value = RxiLinearizeLuma(reduction, histEntry->color.y) * yWeight
			+ histEntry->color.i * iWeight
			+ histEntry->color.q * qWeight
			+ histEntry->color.a * aWeight;

		histEntry->value = value;
	}

	//sort colors by dot product with the vector
	qsort(thisHistogram, nColors, sizeof(RxHistEntry *), RxiHistEntryComparator);
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

	double principal[4];
	RxiHistComputePrincipal(reduction, node->startIndex, node->endIndex, principal);

	double yWeight = principal[0] * reduction->yWeight;
	double iWeight = principal[1] * reduction->iWeight;
	double qWeight = principal[2] * reduction->qWeight;
	double aWeight = principal[3] * reduction->aWeight;

	double projMax = -RX_LARGE_NUMBER;
	double projMin = RX_LARGE_NUMBER;
	for (int i = node->startIndex; i < node->endIndex; i++) {
		RxHistEntry *histEntry = reduction->histogramFlat[i];
		double proj = RxiLinearizeLuma(reduction, histEntry->color.y) * yWeight
			+ histEntry->color.i * iWeight
			+ histEntry->color.q * qWeight
			+ histEntry->color.a * aWeight;
			
		histEntry->value = proj;
		if (proj > projMax) projMax = proj;
		if (proj < projMin) projMin = proj;
	}

	if (projMin == projMax) {
		node->canSplit = FALSE;
		return;
	}

	RxiClusterSum *splits = (RxiClusterSum *) calloc(nColors, sizeof(RxiClusterSum));
	if (splits == NULL) {
		reduction->status = RX_STATUS_NOMEM;
		return;
	}

	//sort colors by dot product with the vector
	RxHistEntry **thisHistogram = reduction->histogramFlat + node->startIndex;
	qsort(thisHistogram, nColors, sizeof(RxHistEntry *), RxiHistEntryComparator);

	//gather statistics for splitting
	double totalWeight = 0.0, totalY = 0.0, sumSq = 0.0, totalI = 0.0, totalQ = 0.0, totalA = 0.0;
	for (int i = 0; i < nColors; i++) {
		RxHistEntry *entry = thisHistogram[i];
		double weight = entry->weight;
		double cy = reduction->yWeight * RxiLinearizeLuma(reduction, entry->color.y);
		double ci = reduction->iWeight * entry->color.i;
		double cq = reduction->qWeight * entry->color.q;
		double ca = reduction->aWeight * entry->color.a;
		sumSq += weight * RxiVec4Mag(cy, ci, cq, ca);

		RxiClusterSum *split = &splits[i];
		split->y = (totalY += weight * cy);       // accumulate color
		split->i = (totalI += weight * ci);       // accumulate
		split->q = (totalQ += weight * cq);       // accumulate
		split->a = (totalA += weight * ca);       // accumulate
		split->weightL = (totalWeight += weight); // accumulate total weight
	}
	node->weight = totalWeight;

	//computing representative color
	if (reduction->alphaMode == RX_ALPHA_PALETTE) {
		//compute average color, with color values weighted by their alpha
		double sumAlpha = 0.0;
		double initY = 0.0, initI = 0.0, initQ = 0.0, initA = 0.0;
		for (int i = 0; i < nColors; i++) {
			RxHistEntry *ent = thisHistogram[i];

			double weightA = ent->color.a * ent->weight;
			initY += RxiLinearizeLuma(reduction, ent->color.y) * weightA;
			initI += ent->color.i * weightA;
			initQ += ent->color.q * weightA;
			initA += ent->color.a * ent->weight;
			sumAlpha += weightA;
		}

		//compute average color, weighting color by alpha
		node->color.y = (float) RxiDelinearizeLuma(reduction, initY / sumAlpha);
		node->color.i = (float) (initI / sumAlpha);
		node->color.q = (float) (initQ / sumAlpha);
		node->color.a = (float) (initA / totalWeight);
	} else {
		//compute average color, treating alpha as an independent channel
		node->color.y = (float) RxiDelinearizeLuma(reduction, totalY / (totalWeight * reduction->yWeight));
		node->color.i = (float) (totalI / (totalWeight * reduction->iWeight));
		node->color.q = (float) (totalQ / (totalWeight * reduction->qWeight));
		node->color.a = (float) (totalA / (totalWeight * reduction->aWeight));
	}

	//determine pivot index based on the split that yields the best total WSS. This represents total
	//squared quantization error
	int pivotIndex = 1;
	double wssBest = RX_LARGE_NUMBER;
	for (int i = 0; i < (nColors - 1); i++) {
		RxiClusterSum *entry = &splits[i];
		
		double weightR = totalWeight - entry->weightL;

		double sumSqL = RxiVec4Mag(entry->y, entry->i, entry->q, entry->a) / entry->weightL;
		double sumSqR = RxiVec4Mag(totalY - entry->y, totalI - entry->i, totalQ - entry->q, totalA - entry->a) / weightR;
		double wss = sumSq - sumSqL - sumSqR;

		//better sum of squares
		if (wss < wssBest) {

			//we'll check the mean left and mean right. They should be different with masking.
			RxYiqColor yiqL, yiqR;
			yiqL.y = (float) RxiDelinearizeLuma(reduction, entry->y / (entry->weightL * reduction->yWeight));
			yiqR.y = (float) RxiDelinearizeLuma(reduction, (totalY - entry->y) / (weightR * reduction->yWeight));
			yiqL.i = (float) (entry->i / (entry->weightL * reduction->iWeight));
			yiqR.i = (float) ((totalI - entry->i) / (weightR * reduction->iWeight));
			yiqL.q = (float) (entry->q / (entry->weightL * reduction->qWeight));
			yiqR.q = (float) ((totalQ - entry->q) / (weightR * reduction->qWeight));
			yiqL.a = (float) (entry->a / (entry->weightL * reduction->aWeight));
			yiqR.a = (float) ((totalA - entry->a) / (weightR * reduction->aWeight));

			COLOR32 maskL = RxiMaskYiqToRgb(reduction, &yiqL);
			COLOR32 maskR = RxiMaskYiqToRgb(reduction, &yiqR);
			if (maskL == maskR) continue; // discard this split (centroids mask to the same color)

			wssBest = wss;
			pivotIndex = i + 1;
		}
	}
	free(splits);
	
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
	double wssInitial = (sumSq - RxiVec4Mag(totalY, totalI, totalQ, totalA) / totalWeight);
	double wssReduction = wssInitial - wssBest;

	node->priority = wssReduction;
	if (!reduction->enhanceColors) {
		//moderate penalty for popular cluster
		node->priority /= sqrt(totalWeight);
	}
}

static void RxiTreeSplitNode(RxReduction *reduction, RxColorNode *node) {
	if (!node->canSplit) return; // did not split
	node->canSplit = FALSE;

	RX_ASSUME(node->left == NULL && node->right == NULL);
	RX_ASSUME(node->pivotIndex > node->startIndex && node->pivotIndex < node->endIndex);

	RxColorNode *lNode = (RxColorNode *) RxMemCalloc(1, sizeof(RxColorNode));
	RxColorNode *rNode = (RxColorNode *) RxMemCalloc(1, sizeof(RxColorNode));
	if (lNode == NULL || rNode == NULL) {
		free(lNode);
		free(rNode);
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
	COLOR32 compare = RxiMaskYiqToRgb(reduction, &src->color);
	COLOR32 thisRgb = RxiMaskYiqToRgb(reduction, &treeHead->color);
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

static void RxiPaletteWrite(RxReduction *reduction) {
	if (reduction->colorTreeHead == NULL) return;

	//convert to RGB
	RxColorNode **colorBlockPtr = reduction->colorBlocks;
	for (int i = 0; i < reduction->nUsedColors; i++) {
		RX_ASSUME(colorBlockPtr[i] != NULL);
		
		COLOR32 rgb32 = RxiMaskYiqToRgb(reduction, &colorBlockPtr[i]->color);

		//write RGB
		reduction->paletteRgb[i] = rgb32;

		//write YIQ (with any loss of information to RGB)
		RxConvertRgbToYiq(rgb32, &reduction->paletteYiq[i]);
	}
}

static void RxiVoronoiAccumulateClusters(RxReduction *reduction) {
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	memset(totalsBuffer, 0, sizeof(reduction->blockTotals));

	//remap histogram points to palette colors, and accumulate the error
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];

		double bestDistance;
		int bestIndex = RxiPaletteFindClosestColor(reduction, reduction->paletteYiqCopy, reduction->nUsedColors, &entry->color, &bestDistance);

		//add to total. YIQ colors scaled by alpha to be unscaled later.
		double weight = entry->weight;
		double a1 = weight * entry->color.a;
		totalsBuffer[bestIndex].weight += weight;
		totalsBuffer[bestIndex].error += weight * bestDistance;
		totalsBuffer[bestIndex].y += a1 * RxiLinearizeLuma(reduction, entry->color.y);
		totalsBuffer[bestIndex].i += a1 * entry->color.i;
		totalsBuffer[bestIndex].q += a1 * entry->color.q;
		totalsBuffer[bestIndex].a += a1;
		totalsBuffer[bestIndex].count++;
		entry->entry = bestIndex;
	}
}

static void RxiVoronoiMoveToCluster(RxReduction *reduction, RxHistEntry *entry, int idxTo, double newDifference, double oldDifference) {
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	int idxFrom = entry->entry;

	double aWeight = entry->weight * entry->color.a;
	double y = aWeight * RxiLinearizeLuma(reduction, entry->color.y);
	double i = aWeight * entry->color.i;
	double q = aWeight * entry->color.q;
	double a = aWeight;

	//add weight to "to" cluster
	totalsBuffer[idxTo].y += y;
	totalsBuffer[idxTo].i += i;
	totalsBuffer[idxTo].q += q;
	totalsBuffer[idxTo].a += a;
	totalsBuffer[idxTo].weight += entry->weight;
	totalsBuffer[idxTo].error += newDifference;
	totalsBuffer[idxTo].count++;

	//remove weight from "from" cluster
	totalsBuffer[idxFrom].y -= y;
	totalsBuffer[idxFrom].i -= i;
	totalsBuffer[idxFrom].q -= q;
	totalsBuffer[idxFrom].a -= a;
	totalsBuffer[idxFrom].weight -= entry->weight;
	totalsBuffer[idxFrom].error -= oldDifference;
	totalsBuffer[idxFrom].count--;

	entry->entry = idxTo;
}

static int RxiVoronoiIterate(RxReduction *reduction) {
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;

	//map histogram colors to existing clusters and accumulate error.
	RxiVoronoiAccumulateClusters(reduction);

	//new centroid indexes, when created
	int newCentroidIdxs[RX_PALETTE_MAX_SIZE];
	int nNewCentroids = 0;

	//check that every palette entry has some weight assigned to it from the previous step.
	//if any palette color would have zero weight, we assign it a color with the highest
	//squared deviation from its palette color (scaled by weight).
	//when we do this, we recompute the cluster bounds.
	int nHistEntries = reduction->histogram->nEntries;
	for (int i = 0; i < reduction->nUsedColors; i++) {
		if (totalsBuffer[i].weight > 0.0) continue;

		//find the color farthest from this center
		double largestDifference = 0.0;
		int farthestIndex = 0;
		for (int j = 0; j < nHistEntries; j++) {
			RxHistEntry *entry = reduction->histogramFlat[j];            // histogram color
			RxYiqColor *yiq1 = &reduction->paletteYiqCopy[entry->entry]; // ceontroid of the cluster the color belongs to

			//do not move a cluster with only one member
			if (totalsBuffer[entry->entry].count <= 1) continue;

			//if we mask colors, check this entry against the palette with clamping. If they compare equal,
			//then we say that this color is as close as it will be to a palette color and we won't include
			//this in our search candidates.
			COLOR32 histMasked = RxiMaskYiqToRgb(reduction, &entry->color);
			COLOR32 palMasked = RxiMaskYiqToRgb(reduction, yiq1);
			if (histMasked == palMasked) continue; // this difference can't be reconciled

			double diff = RxiComputeColorDifference(reduction, yiq1, &entry->color) * entry->weight;
			if (diff > largestDifference) {

				//we additionally want to check that the new cluster may accurately represent the color.
				//with masking, we may end up with a color further away than the current centroid!
				RxYiqColor yiqNewCentroid;
				RxConvertRgbToYiq(histMasked, &yiqNewCentroid);

				double newDifference = RxiComputeColorDifference(reduction, &entry->color, &yiqNewCentroid) * entry->weight;
				if (diff >= newDifference) {

					//lastly, since an earlier cluster reassignment may have produced a cluster matching
					//what would be this entry's new centroid, we'll check the existing centroids and assign
					//to an existing one if it exists.
					int found = 0;
					for (int k = 0; k < nNewCentroids; k++) {
						int idx = newCentroidIdxs[k];
						if (reduction->paletteRgbCopy[idx] == histMasked) {
							//remap
							RxiVoronoiMoveToCluster(reduction, entry, idx, newDifference, diff);
							found = 1;
							break;
						}
					}

					if (!found) {
						largestDifference = diff;
						farthestIndex = j;
					}
				}
			}
		}

		//get RGB of new point (will be used when checking identical remapped colors)
		RxHistEntry *entry = reduction->histogramFlat[farthestIndex];
		reduction->paletteRgbCopy[i] = RxiMaskYiqToRgb(reduction, &entry->color);
		RxConvertRgbToYiq(reduction->paletteRgbCopy[i], &reduction->paletteYiqCopy[i]);

		//move centroid
		double newDifference = RxiComputeColorDifference(reduction, &entry->color, &reduction->paletteYiqCopy[i]) * entry->weight;
		RxiVoronoiMoveToCluster(reduction, entry, i, newDifference, largestDifference);
		newCentroidIdxs[nNewCentroids++] = i;
	}

	//after recomputing bounds, now let's see if we're wasting any slots.
	for (int i = 0; i < reduction->nUsedColors; i++) {
		if (totalsBuffer[i].weight <= 0.0) return 0; // stop
	}

	//average out the colors in the new partitions
	for (int i = 0; i < reduction->nUsedColors; i++) {
		RxYiqColor yiq;
		double invAWeight = 1.0 / totalsBuffer[i].a;
		yiq.y = (float) RxiDelinearizeLuma(reduction, totalsBuffer[i].y * invAWeight);
		yiq.i = (float) (totalsBuffer[i].i * invAWeight);
		yiq.q = (float) (totalsBuffer[i].q * invAWeight);
		yiq.a = (float) (totalsBuffer[i].a / totalsBuffer[i].weight);

		//mask color
		COLOR32 as32 = RxiMaskYiqToRgb(reduction, &yiq);
		RxConvertRgbToYiq(as32, &yiq);

		//when color masking is used, it is possible that the new computed centroid may drift
		//from optimal placement. We will select either the new centroid or the old one, based
		//on which achieves the least error. If the old centroid achieves a better error, then
		//we do not update the centroid.
		//this ensures that the total error is at least monotonically decreasing.
		double errNewCluster = 0.0;
		for (int j = 0; j < reduction->histogram->nEntries; j++) {
			if (reduction->histogramFlat[j]->entry != i) continue;

			RxHistEntry *hist = reduction->histogramFlat[j];
			errNewCluster += hist->weight * RxiComputeColorDifference(reduction, &hist->color, &yiq);
		}

		//if the new cluster is an improvement over the old cluster
		if (errNewCluster < totalsBuffer[i].error) {
			reduction->paletteRgbCopy[i] = as32;
			memcpy(&reduction->paletteYiqCopy[i], &yiq, sizeof(yiq));
		}
	}

	//compute new error
	double error = 0.0;
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxHistEntry *hist = reduction->histogramFlat[i];
		
		double err;
		hist->entry = RxiPaletteFindClosestColor(reduction, reduction->paletteYiqCopy, reduction->nUsedColors, &hist->color, &err);
		error += err * hist->weight;
	}

	//if the error is no longer decreasing, stop iteration
	if (error >= reduction->lastSSE) return 0; // stop

	//error check succeeded, copy this palette to the main palette.
	memcpy(reduction->paletteYiq, reduction->paletteYiqCopy, sizeof(reduction->paletteYiqCopy));
	memcpy(reduction->paletteRgb, reduction->paletteRgbCopy, sizeof(reduction->paletteRgbCopy));
	reduction->lastSSE = error;

	//if this is the last iteration, stop iterating
	if (++reduction->reclusterIteration >= reduction->nReclusters) return 0;
	return 1; // continue
}

static void RxiPaletteRecluster(RxReduction *reduction) {
	//simple termination conditions
	if (reduction->nReclusters <= 0) return;

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
		RxYiqColor *histColor = &reduction->histogramFlat[i]->color;

		//find nearest, add to total
		int bestIndex = RxiPaletteFindClosestColor(reduction, reduction->paletteYiq, reduction->nUsedColors, histColor, NULL);
		totalsBuffer[bestIndex].weight += reduction->histogramFlat[i]->weight;
	}

	//weight==0 => delete
	int nRemoved = 0;
	for (int i = 0; i < reduction->nUsedColors; i++) {
		if (totalsBuffer[i].weight > 0) continue;

		//delete
		memmove(&reduction->paletteRgb[i], &reduction->paletteRgb[i + 1], (reduction->nUsedColors - i - 1) * sizeof(reduction->paletteRgb[0]));
		memmove(&reduction->paletteYiq[i], &reduction->paletteYiq[i + 1], (reduction->nUsedColors - i - 1) * sizeof(reduction->paletteYiq[0]));
		memmove(&totalsBuffer[i], &totalsBuffer[i + 1], (reduction->nUsedColors - i - 1) * sizeof(RxTotalBuffer));
		reduction->nUsedColors--;
		i--;
		nRemoved++;
	}

	memset(reduction->paletteRgb + reduction->nUsedColors, 0, nRemoved * sizeof(reduction->paletteRgb[0]));
	memset(reduction->paletteYiq + reduction->nUsedColors, 0, nRemoved * sizeof(reduction->paletteYiq[0]));
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
	for (int i = 0; i < reduction->nUsedColors; i++) {
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

RxStatus RxComputePalette(RxReduction *reduction) {
	if (reduction->histogramFlat == NULL || reduction->histogram->nEntries == 0) {
		reduction->nUsedColors = 0;
		return reduction->status;
	}

	//do it
	RxColorNode *treeHead = (RxColorNode *) RxMemCalloc(1, sizeof(RxColorNode));
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

		//no more nodes to split?
		if (node == NULL || reduction->status != RX_STATUS_OK) break;
	}

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
		RxiSlabFree(&reduction->histogram->allocator);
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
		RxiSlabFree(&reduction->histogram->allocator);
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
	RxReduction *reduction = RxNew(balance, colorBalance, enhanceColors, nColors);
	if (reduction == NULL) return RX_STATUS_NOMEM;

	RxApplyFlags(reduction, flag);

	RxHistAdd(reduction, img, width, height);
	RxHistFinalize(reduction);
	RxComputePalette(reduction);

	//copy palette out
	memcpy(pal, reduction->paletteRgb, nColors * sizeof(COLOR32));

	RxStatus status = reduction->status;
	int nProduced = reduction->nUsedColors;
	RxFree(reduction);

	if (flag & RX_FLAG_SORT_ONLY_USED) {
		qsort(pal, nProduced, sizeof(COLOR32), RxColorLightnessComparator);
	} else {
		qsort(pal, nColors, sizeof(COLOR32), RxColorLightnessComparator);
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

static int RxiPaletteFindClosestRgbColor(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, COLOR32 col, double *outDiff) {
	RxYiqColor yiq;
	RxConvertRgbToYiq(col, &yiq);

	return RxiPaletteFindClosestColor(reduction, palette, nColors, &yiq, outDiff);
}

int RxPaletteFindCloestColorYiq(RxReduction *reduction, const RxYiqColor *color, const RxYiqColor *palette, unsigned int nColors) {
	return RxiPaletteFindClosestColor(reduction, palette, nColors, color, NULL);
}

double RxHistComputePaletteErrorYiq(RxReduction *reduction, const RxYiqColor *palette, unsigned int nColors, double maxError) {
	double error = 0.0;

	//sum total weighted squared differences
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];
		
		double diff = 0.0;
		(void) RxiPaletteFindClosestColor(reduction, palette, nColors, &entry->color, &diff);
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

void RxCreateMultiplePalettes(const COLOR32 *px, unsigned int tilesX, unsigned int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							int paletteSize, int nColsPerPalette, int paletteOffset, int *progress) {
	RxCreateMultiplePalettesEx(px, tilesX, tilesY, dest, paletteBase, nPalettes, paletteSize, nColsPerPalette, 
							 paletteOffset, BALANCE_DEFAULT, BALANCE_DEFAULT, 0, progress);
}

void RxCreateMultiplePalettesEx(const COLOR32 *imgBits, unsigned int tilesX, unsigned int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							  int paletteSize, int nColsPerPalette, int paletteOffset, int balance, 
							  int colorBalance, int enhanceColors, int *progress) {
	if (nPalettes == 0) return;

	//in the case of one palette, call to the faster single-palette routines.
	if (nPalettes == 1) {
		if (paletteOffset > 0) {
			RxCreatePaletteEx(
				imgBits,
				tilesX * 8,
				tilesY * 8,
				dest + (paletteBase * paletteSize) + paletteOffset,
				nColsPerPalette,
				balance,
				colorBalance,
				enhanceColors,
				RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE,
				NULL
			);
		} else {
			//reserve the first color entry
			RxCreatePaletteEx(
				imgBits,
				tilesX * 8,
				tilesY * 8,
				dest + (paletteBase * paletteSize) + paletteOffset + 1,
				nColsPerPalette - 1,
				balance,
				colorBalance,
				enhanceColors,
				RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE,
				NULL
			);
			dest[(paletteBase * paletteSize) + paletteOffset] = 0xFF00FF; //transparent fill
		}
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
	RxReduction *reduction = RxNew(balance, colorBalance, enhanceColors, nColsPerPalette);

	for (unsigned int y = 0; y < tilesY; y++) {
		for (unsigned int x = 0; x < tilesX; x++) {
			RxiTile *tile = &tiles[x + (y * tilesX)];
			const COLOR32 *pxOrigin = imgBits + x * 8 + (y * 8 * tilesX * 8);
			RxiTileCopy(tile, pxOrigin, tilesX * 8);

			RxHistClear(reduction);
			RxHistAdd(reduction, tile->rgb, 8, 8);
			RxHistFinalize(reduction);
			RxComputePalette(reduction);
			for (int i = 0; i < RX_TILE_PALETTE_MAX; i++) {
				COLOR32 col = reduction->paletteRgb[i];
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
		if (cost > 0.0 && (tiles[index1].nUsedColors + tiles[index2].nUsedColors) > (unsigned int) reduction->nPaletteColors) {
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
		RxComputePalette(reduction);

		//write over the palette of the tile
		RxiTile *palTile = &tiles[index1];
		for (int i = 0; i < RX_TILE_PALETTE_MAX - 1; i++) {
			RxConvertRgbToYiq(reduction->paletteRgb[i], &palTile->palette[i]);
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
		RxComputePalette(reduction);
		
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
			RxComputePalette(reduction);

			//write back
			memcpy(palettes + i * RX_TILE_PALETTE_MAX, reduction->paletteRgb, nColsPerPalette * sizeof(COLOR32));
		}
	}
	RxMemFree(yiqPalette);

	//write palettes in the correct size
	reduction->nPaletteColors = nFinalColsPerPalette;
	for (int i = 0; i < nPalettes; i++) {
		//recreate palette so that it can be output in its correct size
		if (nFinalColsPerPalette != nColsPerPalette) {

			//palette does need to be created again
			RxHistClear(reduction);
			for (unsigned int j = 0; j < nTiles; j++) {
				if (bestPalettes[j] != i) continue;
				RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
			RxHistFinalize(reduction);
			RxComputePalette(reduction);

			//write and sort
			COLOR32 *thisPalDest = dest + paletteSize * (i + paletteBase) + outputOffs;
			memcpy(thisPalDest, reduction->paletteRgb, nFinalColsPerPalette * sizeof(COLOR32));
			qsort(thisPalDest, nFinalColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
		} else {
			//already the correct size; simply sort and copy
			qsort(palettes + i * RX_TILE_PALETTE_MAX, nColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
			memcpy(dest + paletteSize * (i + paletteBase) + outputOffs, palettes + i * RX_TILE_PALETTE_MAX, nColsPerPalette * sizeof(COLOR32));
		}

		if (paletteOffset == 0) dest[(i + paletteBase) * paletteSize] = 0xFF00FF;
	}

	free(palettes);
	free(bestPalettes);
	RxFree(reduction);
	RxMemFree(tiles);
	free(diffBuff);
}

static double RxiDiffuseCurveY(double x) {
	if (x < 0.0) return -RxiDiffuseCurveY(-x);
	if (x <= 8.0) return x;
	return 8.0 + pow(x - 8.0, 0.9) * 0.94140625;
}

static double RxiDiffuseCurveI(double x) {
	if (x < 0.0) return -RxiDiffuseCurveI(-x);
	if (x <= 8.0) return x;
	return 8.0 + pow(x - 8.0, 0.85) * 0.98828125;
}

static double RxiDiffuseCurveQ(double x) {
	if (x < 0.0) return -RxiDiffuseCurveQ(-x);
	if (x <= 8.0) return x;
	return 8.0 + pow(x - 8.0, 0.85) * 0.89453125;
}

RxStatus RxReduceImage(COLOR32 *px, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int nColors, float diffuse) {
	return RxReduceImageEx(px, NULL, width, height, palette, nColors, RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_PRESERVE_ALPHA,
		diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

RxStatus RxReduceImageEx(COLOR32 *img, int *indices, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int nColors, RxFlag flag, float diffuse, int balance, int colorBalance, int enhanceColors) {
	RxReduction *reduction = RxNew(balance, colorBalance, enhanceColors, nColors);
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

	//load palette into context
	RxStatus status = RxPaletteLoad(reduction, palette, nColors);
	if (status != RX_STATUS_OK) return status;

	RxYiqColor *rowbuf = (RxYiqColor *) RxMemCalloc(4 * (width + 2), sizeof(RxYiqColor));
	if (rowbuf == NULL) {
		//no memory
		RxPaletteFree(reduction);
		free(rowbuf);
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

			float colorY = (thisRow[x + 1].y * 3 + thisRow[x + 2].y * 3 + thisRow[x].y * 3 + lastRow[x + 1].y * 3
						  + lastRow[x].y * 2 + lastRow[x + 2].y * 2) * 0.0625f;
			float colorI = (thisRow[x + 1].i * 3 + thisRow[x + 2].i * 3 + thisRow[x].i * 3 + lastRow[x + 1].i * 3
						  + lastRow[x].i * 2 + lastRow[x + 2].i * 2) * 0.0625f;
			float colorQ = (thisRow[x + 1].q * 3 + thisRow[x + 2].q * 3 + thisRow[x].q * 3 + lastRow[x + 1].q * 3
						  + lastRow[x].q * 2 + lastRow[x + 2].q * 2) * 0.0625f;
			float colorA = thisRow[x + 1].a;

			//match it to a palette color. We'll measure distance to it as well.
			RxYiqColor colorYiq = { colorY, colorI, colorQ, colorA };
			double paletteDistance = 0.0;
			int matched = RxPaletteFindClosestColorYiq(reduction, &colorYiq, &paletteDistance);

			//now measure distance from the actual color to its average surroundings
			RxYiqColor centerYiq;
			memcpy(&centerYiq, &thisRow[x + 1], sizeof(RxYiqColor));
			double centerDistance = RxiComputeColorDifference(reduction, &centerYiq, &colorYiq);

			//now test: Should we dither?
			double yw2 = reduction->yWeight2;
			if (centerDistance < 110.0 * yw2 && paletteDistance >  2.0 * yw2 && diffuse > 0.0f) {
				//Yes, we should dither :)

				//correct for Floyd-Steinberg coefficients (scale by diffusion amount)
				double diffuseY = thisDiffuse[x + 1].y * diffuse;
				double diffuseI = thisDiffuse[x + 1].i * diffuse;
				double diffuseQ = thisDiffuse[x + 1].q * diffuse;
				double diffuseA = thisDiffuse[x + 1].a * diffuse;

				if (binaryAlpha) diffuseA = 0.0; //don't diffuse alpha if no alpha channel, or we're told not to

				colorY += (float) RxiDiffuseCurveY(diffuseY);
				colorI += (float) RxiDiffuseCurveI(diffuseI);
				colorQ += (float) RxiDiffuseCurveQ(diffuseQ);
				colorA += (float) diffuseA;
				if (colorY < 0.0f) { //clamp just in case
					colorY = 0.0f;
					colorI = 0.0f;
					colorQ = 0.0f;
				} else if (colorY > 511.0f) {
					colorY = 511.0f;
					colorI = 0.0f;
					colorQ = 0.0f;
				}

				if (colorA < 0.0f) colorA = 0.0f;
				else if (colorA > 255.0f) colorA = 255.0f;

				//match to palette color
				RxYiqColor diffusedYiq = { colorY, colorI, colorQ, colorA };
				matched = RxPaletteFindClosestColorYiq(reduction, &diffusedYiq, NULL);

				if (!(flag & RX_FLAG_NO_WRITEBACK)) {
					COLOR32 chosen = palette[matched];
					if (touchAlpha) img[x + y * width] = chosen;
					else img[x + y * width] = (chosen & 0x00FFFFFF) | (img[x + y * width] & 0xFF000000);
				}
				if (indices != NULL) indices[x + y * width] = matched;

				//RxYiqColor *chosenYiq = &yiqPalette[matched];
				RxYiqColor *chosenYiq = &reduction->accel.plttLarge[matched];
				float chosenA = (float) (chosenYiq->a * INV_255);
				float offY = (colorY - chosenYiq->y) * chosenA;
				float offI = (colorI - chosenYiq->i) * chosenA;
				float offQ = (colorQ - chosenYiq->q) * chosenA;
				float offA = (colorA - chosenYiq->a);
				if (flag & RX_FLAG_NO_ALPHA_DITHER) offA = 0.0f;

				//now diffuse to neighbors
				RxYiqColor *diffNextPixel = &thisDiffuse[x + 1 + hDirection];
				RxYiqColor *diffDownPixel = &nextDiffuse[x + 1];
				RxYiqColor *diffNextDownPixel = &nextDiffuse[x + 1 + hDirection];
				RxYiqColor *diffBackDownPixel = &nextDiffuse[x + 1 - hDirection];

				if (colorA >= 128.0f || !binaryAlpha) { //don't dither if there's no alpha channel and this is transparent!
					diffNextPixel->y += offY * 0.4375f; // 7/16
					diffNextPixel->i += offI * 0.4375f;
					diffNextPixel->q += offQ * 0.4375f;
					diffNextPixel->a += offA * 0.4375f;
					diffDownPixel->y += offY * 0.3125f; // 5/16
					diffDownPixel->i += offI * 0.3125f;
					diffDownPixel->q += offQ * 0.3125f;
					diffDownPixel->a += offA * 0.3125f;
					diffBackDownPixel->y += offY * 0.1875f; // 3/16
					diffBackDownPixel->i += offI * 0.1875f;
					diffBackDownPixel->q += offQ * 0.1875f;
					diffBackDownPixel->a += offA * 0.1875f;
					diffNextDownPixel->y += offY * 0.0625f; // 1/16
					diffNextDownPixel->i += offI * 0.0625f;
					diffNextDownPixel->q += offQ * 0.0625f;
					diffNextDownPixel->a += offA * 0.0625f;
				}

			} else {
				//anomaly in the picture, just match the original color. Don't diffuse, it'll cause issues.
				//That or the color is pretty homogeneous here, so dithering is bad anyway.
				if (binaryAlpha) {
					if (centerYiq.a < 128.0f) {
						centerYiq.y = 0.0f;
						centerYiq.i = 0.0f;
						centerYiq.q = 0.0f;
						centerYiq.a = 0.0f;
					} else {
						centerYiq.a = 255.0f;
					}
				}

				matched = RxPaletteFindClosestColorYiq(reduction, &centerYiq, NULL);
				if (!(flag & RX_FLAG_NO_WRITEBACK)) {
					COLOR32 chosen = palette[matched];
					if (touchAlpha) img[x + y * width] = chosen;
					else img[x + y * width] = (chosen & 0x00FFFFFF) | (img[x + y * width] & 0xFF000000);
				}
				if (indices != NULL) indices[x + y * width] = matched;
			}

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
	}

	RxPaletteFree(reduction);
	RxMemFree(rowbuf);
	return RX_STATUS_OK;
}

static double RxiAccelGetChannelN(RxReduction *reduction, const RxYiqColor *color, unsigned int n) {
	switch (n) {
		case 0: return reduction->yWeight * RxiLinearizeLuma(reduction, color->y);
		case 1: return reduction->iWeight * color->i;
		case 2: return reduction->qWeight * color->q;
		case 3: return reduction->aWeight * color->a;
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
		accel->splitVal = RxiAccelGetChannelN(reduction, &accel->mid->color, nextSplit);
		return nodebuf;
	}

	//sort by
	for (unsigned int i = 0; i < 4; i++) {
		//test sort
		for (unsigned int i = 0; i < accel->nCol; i++) {
			pltt[i].sortVal = RxiAccelGetChannelN(reduction, &pltt[i].color, nextSplit);
		}
		qsort(pltt, accel->nCol, sizeof(RxPaletteMapEntry), RxiAccelSortPalette);

		//split test
		double f1 = pltt[0].sortVal;
		double f2 = pltt[accel->nCol - 1].sortVal;
		if (f1 != f2) break;

		//else
		nextSplit = (nextSplit + 1) % 4;
		if (i == 3) return nodebuf; // not split (all axes degenerate)
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
		nodebuf = RxiAccelSplit(reduction, childL, nodebuf, plttFull, (nextSplit + 1) % 4);
	}

	if (nRight > 0) {
		RxPaletteAccelNode *childR = nodebuf++;
		childR->start = accel->start + iSplit + 1;
		childR->nCol = nRight;
		childR->parent = accel;
		accel->pRight = childR;
		nodebuf = RxiAccelSplit(reduction, childR, nodebuf, plttFull, (nextSplit + 1) % 4);
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

		double diff = RxiComputeColorDifference(reduction, color, &accel->mid->color);
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
		double diff = RxiComputeColorDifference(reduction, color, &accel->mid->color);
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
		RxYiqColor *nodeCol = &nodep->mid->color;

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
	double bestDiff = RxiComputeColorDifference(reduction, color, &nodep->mid->color);

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

	RxYiqColor cpy;
	memcpy(&cpy, color, sizeof(RxYiqColor));

	//processing for alpha mode
	unsigned int plttStart = 0;
	switch (reduction->alphaMode) {
		case RX_ALPHA_PIXEL:
			cpy.a = 255.0;
			break;
		case RX_ALPHA_RESERVE:
			plttStart = 1;
			if (cpy.a < (float) reduction->alphaThreshold) {
				if (outDiff != NULL) *outDiff = 0.0;
				return 0; // transparent reserve
			}
			cpy.a = 255.0f;
			break;
		default:
			break;
	}

	if (accel->useAccelerator) {
		//accelerated search
		return RxiPaletteFindClosestColorAccelerated(reduction, &cpy, outDiff) + plttStart;
	} else {
		//slow search
		return RxiPaletteFindClosestColor(reduction, accel->plttLarge + plttStart, accel->nPltt - plttStart, &cpy, outDiff) + plttStart;
	}
}

static RxStatus RxiPaletteAlloc(RxReduction *reduction, unsigned int nCol) {
	RxPaletteAccelerator *accel = &reduction->accel;
	RX_ASSUME(accel->plttLarge == NULL);

	if (nCol > sizeof(accel->plttSmall) / sizeof(accel->plttSmall[0])) {
		//above small threshold --> allocate on the heap
		accel->plttLarge = (RxYiqColor *) RxMemCalloc(nCol, sizeof(RxYiqColor));
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
			if (a < 255.0f) return RX_STATUS_INVALID;
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
			accel->pltt[i].color.a = 255.0f;
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

	for (unsigned int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(pltt[i], &reduction->accel.plttLarge[i]);
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

void RxPaletteFree(RxReduction *reduction) {
	if (!reduction->accel.initialized) return;

	RxMemFree(reduction->accel.pltt);
	if (reduction->accel.plttLarge != reduction->accel.plttSmall) RxMemFree(reduction->accel.plttLarge);
	free(reduction->accel.nodebuf);
	reduction->accel.pltt = NULL;
	reduction->accel.nodebuf = NULL;
	reduction->accel.plttLarge = NULL;
	reduction->accel.initialized = 0;
	reduction->accel.nPltt = 0;
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
		if (a < reduction->alphaThreshold) continue;
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
