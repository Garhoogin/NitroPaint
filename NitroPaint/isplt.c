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

#define RX_LARGE_NUMBER          1e32 // constant to represent large color difference
#define RX_SLAB_SIZE         0x100000 // slab size of allocator
#define INV_512 0.0019531250000000000 // 1.0/512.0
#define INV_511 0.0019569471624266144 // 1.0/511.0
#define INV_255 0.0039215686274509800 // 1.0/255.0
#define INV_3   0.3333333333333333333 // 1.0/  3.0

//struct for internal processing of color leaves
typedef struct {
	double y;
	double i;
	double q;
	double a;
	double partialSumWeights;
	double weightedSquares;
	double weight;
} COLOR_INFO; 

static COLOR32 RxMaskColorToDS15(COLOR32 c) {
	//DS mode masking: round color channels to 5-bit values, and force alpha=0xff
	return ColorRoundToDS15(c) | 0xFF000000;
}

static COLOR32 RxMaskColorDummy(COLOR32 c) {
	//no-mask dummy: pass colors, forcing RGB to 0 when alpha=0
	if ((c >> 24) == 0) c = 0;
	return c;
}

void RxInit(RxReduction *reduction, int balance, int colorBalance, int enhanceColors, unsigned int nColors) {
	memset(reduction, 0, sizeof(RxReduction));
	reduction->yWeight = 60 - balance;
	reduction->iWeight = colorBalance;
	reduction->qWeight = 40 - colorBalance;
	reduction->aWeight = 40.0;
	reduction->yWeight2 = reduction->yWeight * reduction->yWeight; // Y weight squared
	reduction->iWeight2 = reduction->iWeight * reduction->iWeight; // I weight squared
	reduction->qWeight2 = reduction->qWeight * reduction->qWeight; // Q weight squared
	reduction->aWeight2 = reduction->aWeight * reduction->aWeight; // A weight squared

	reduction->enhanceColors = enhanceColors;
	reduction->nReclusters = RECLUSTER_DEFAULT;// nColors <= 32 ? RECLUSTER_DEFAULT : 0;
	reduction->nPaletteColors = nColors;
	reduction->gamma = 1.27;
	reduction->maskColors = RxMaskColorToDS15;
	reduction->alphaMode = RX_ALPHA_NONE; // default: no alpha processing
	reduction->alphaThreshold = 0x80;     // default: alpha threshold =128

	for (int i = 0; i < 512; i++) {
		reduction->lumaTable[i] = pow((double) i * INV_511, 1.27) * 511.0;
	}
}

static void *RxiSlabAlloc(RxSlab *allocator, unsigned int size) {
	RX_ASSUME(size <= RX_SLAB_SIZE);

	//if no slab is allocated, allocate one.
	if (allocator->allocation == NULL) {
		allocator->allocation = calloc(RX_SLAB_SIZE, 1);
		allocator->pos = 0;
	}

	//search for a slab with a suitable size.
	while ((allocator->pos + size) > RX_SLAB_SIZE) {
		if (allocator->next == NULL) {
			RxSlab *next = calloc(1, sizeof(RxSlab));
			next->allocation = calloc(RX_SLAB_SIZE, 1);
			next->pos = 0;
			allocator->next = next;
		}
		allocator = allocator->next;
	}

	void *res = (void *) (((uintptr_t) allocator->allocation) + allocator->pos);
	allocator->pos += size;
	return res;
}

static void RxiSlabFree(RxSlab *allocator) {
	if (allocator->allocation != NULL) free(allocator->allocation);
	allocator->allocation = NULL;

	if (allocator->next != NULL) {
		RxiSlabFree(allocator->next);
		allocator->next = NULL;
	}
}

static void RxHistAddColor(RxReduction *reduction, int y, int i, int q, int a, double weight) {
	RxHistogram *histogram = reduction->histogram;

	//process the alpha value.
	switch (reduction->alphaMode) {
		case RX_ALPHA_NONE:
		case RX_ALPHA_RESERVE:
			//we use tha alpha threshold here since these alpha modes imply binary alpha.
			if (a < (int) reduction->alphaThreshold) return;
			a = 255;
			break;

		case RX_ALPHA_PIXEL:
			//we'll discard alpha=0 since it doesn't need to appear in the palette.
			if (a == 0) return;
			weight *= ((double) a) * INV_255;
			a = 255;
			break;

		case RX_ALPHA_PALETTE:
			//we explicitly must pass all alpha values.
			if (a == 0) {
				//for alpha=0, we'll set YIQ to 0 so we only have one transparent value.
				y = i = q = 0;
			}
			break;
	}

	int slotIndex = (q + (y * 64 + i) * 4 + a) & 0x1FFFF;
	if (slotIndex < histogram->firstSlot) histogram->firstSlot = slotIndex;

	//find a slot with the same YIQA, or create a new one if none exists.
	RxHistEntry **ppslot = &histogram->entries[slotIndex];
	while (*ppslot != NULL) {
		RxHistEntry *slot = *ppslot;

		//matching slot? add weight
		if (slot->color.y == y && slot->color.i == i && slot->color.q == q && slot->color.a == a) {
			slot->weight += weight;
			return;
		}

		ppslot = &slot->next;
	}
	RxHistEntry *slot = (RxHistEntry *) RxiSlabAlloc(&histogram->allocator, sizeof(RxHistEntry));;
	*ppslot = slot;

	//put new color
	slot->color.y = y;
	slot->color.i = i;
	slot->color.q = q;
	slot->color.a = a;
	slot->weight = weight;
	slot->next = NULL;
	slot->value = 0.0;
	histogram->nEntries++;
}

void RxConvertRgbToYiq(COLOR32 rgb, RxYiqColor *yiq) {
	double doubleR = (double) ((rgb >>  0) & 0xFF);
	double doubleG = (double) ((rgb >>  8) & 0xFF);
	double doubleB = (double) ((rgb >> 16) & 0xFF);

	double y = 2.0 * (doubleR * 0.29900 + doubleG * 0.58700 + doubleB * 0.11400);
	double i = 2.0 * (doubleR * 0.59604 - doubleG * 0.27402 - doubleB * 0.32203);
	double q = 2.0 * (doubleR * 0.21102 - doubleG * 0.52204 + doubleB * 0.31103);
	double iCopy = i;

	if (iCopy > 245.0) {
		iCopy = 2 * (iCopy - 245.0) * INV_3 + 245.0;
	}

	if (q < -215.0) {
		q = 2 * (q + 215.0) * INV_3 - 215.0;
	}

	double iqDiff = q - iCopy;
	if (iqDiff > 265.0) {
		double iqDiffShifted = (iqDiff - 265.0) * 0.25;
		iCopy += iqDiffShifted;
		q -= iqDiffShifted;
	}

	if (iCopy < 0.0 && q > 0.0) y -= (q * iCopy) * INV_512;

	//round to integers
	int yInt = (int) (y + 0.5);
	int iInt = (int) (i + (i < 0.0 ? -0.5 : 0.5));
	int qInt = (int) (q + (q < 0.0 ? -0.5 : 0.5));

	//write clamped color
	yiq->y = min(max(yInt,    0), 511);
	yiq->i = min(max(iInt, -320), 319);
	yiq->q = min(max(qInt, -270), 269);
	yiq->a = (rgb >> 24) & 0xFF;
}

void RxConvertYiqToRgb(RxRgbColor *rgb, const RxYiqColor *yiq) {
	double i = (double) yiq->i;
	double q = (double) yiq->q;
	double y = (double) yiq->y;
	if (i < 0.0 && q > 0.0) y += (q * i) * INV_512;

	if (y < 0.0) y = 0.0;
	else if (y > 511.0) y = 511.0;	

	double iqDiff = q - i;
	if (iqDiff > 265.0) {
		iqDiff = (iqDiff - 265.0) * 0.5;
		i -= iqDiff;
		q += iqDiff;
	}

	if (q < -215.0) {
		q = (q + 215.0) * 3.0 * 0.5 - 215.0;
	}

	int r = (int) (y * 0.5 + i * 0.477791 + q * 0.311426 + 0.5);
	int g = (int) (y * 0.5 - i * 0.136066 - q * 0.324141 + 0.5);
	int b = (int) (y * 0.5 - i * 0.552535 + q * 0.852230 + 0.5);

	//write clamped color
	rgb->r = min(max(r, 0), 255);
	rgb->g = min(max(g, 0), 255);
	rgb->b = min(max(b, 0), 255);
	rgb->a = yiq->a;
}

static inline double RxiDelinearizeLuma(RxReduction *reduction, double luma) {
	return 511.0 * pow(luma * INV_511, 1.0 / reduction->gamma);
}

static inline COLOR32 RxiMaskYiqToRgb(RxReduction *reduction, const RxYiqColor *yiq) {
	RxRgbColor rgb;
	RxConvertYiqToRgb(&rgb, yiq);
	return reduction->maskColors(rgb.r | (rgb.g << 8) | (rgb.b << 16) | (rgb.a << 24));
}

static inline double RxiComputeColorDifference(RxReduction *reduction, const RxYiqColor *yiq1, const RxYiqColor *yiq2) {
	double yw2 = reduction->yWeight2;
	double iw2 = reduction->iWeight2;
	double qw2 = reduction->qWeight2;
	double aw2 = reduction->aWeight2;

	if (yiq1->a == yiq2->a) {
		//equal alpha comparison. Because each color is scaled by the same alpha, we can pull it out by
		//multiplying YIQ squared difference by squared alpha.
		double dy = (double) (reduction->lumaTable[yiq1->y] - reduction->lumaTable[yiq2->y]);
		double di = (double) (yiq1->i - yiq2->i);
		double dq = (double) (yiq1->q - yiq2->q);
		double d2 = yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq;

		if (yiq1->a != 255) {
			//translucent scale factor
			double a = yiq1->a * INV_255;
			d2 *= a * a;
		}
		return d2;
	} else {
		//color difference with alpha.
		double a1 = yiq1->a * INV_255, a2 = yiq2->a * INV_255;

		//scale color by alpha for comparison
		double y1 = a1 * reduction->lumaTable[yiq1->y], y2 = a2 * reduction->lumaTable[yiq2->y];
		double i1 = a1 * yiq1->i, i2 = a2 * yiq2->i;
		double q1 = a1 * yiq1->q, q2 = a2 * yiq2->q;
		double dy = y1 - y2, di = i1 - i2, dq = q1 - q2;
		double da = yiq1->a - yiq1->a;

		return yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq
			- da * (
				reduction->yWeight * (y1 - y2)
				+ reduction->iWeight * (i1 - i2)
				+ reduction->qWeight * (q1 - q2)
			) + aw2 * da * da;
	}
}

void RxHistFinalize(RxReduction *reduction) {
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);

	if (reduction->histogram == NULL) {
		reduction->histogramFlat = NULL;
		return;
	}

	reduction->histogramFlat = (RxHistEntry **) calloc(reduction->histogram->nEntries, sizeof(RxHistEntry *));
	RxHistEntry **pos = reduction->histogramFlat;

	for (int i = reduction->histogram->firstSlot; i < 0x20000; i++) {
		RxHistEntry *entry = reduction->histogram->entries[i];

		while (entry != NULL) {
			*(pos++) = entry;
			entry = entry->next;
		}
	}
}

void RxHistAdd(RxReduction *reduction, const COLOR32 *img, unsigned int width, unsigned int height) {
	if (reduction->histogram == NULL) {
		reduction->histogram = (RxHistogram *) calloc(1, sizeof(RxHistogram));
		reduction->histogram->firstSlot = 0x20000;
	}

	for (unsigned int y = 0; y < height; y++) {
		RxYiqColor yiqLeft;
		RxConvertRgbToYiq(img[y * width], &yiqLeft);
		int yLeft = yiqLeft.y, aLeft = yiqLeft.a;

		for (unsigned int x = 0; x < width; x++) {
			RxYiqColor yiq;
			RxConvertRgbToYiq(img[x + y * width], &yiq);

			//when the left pixel is transparent, treat it as same Y value.
			if (aLeft == 0) yLeft = yiq.y;

			int dy = yiq.y - yLeft;
			double weight = (double) (16 - abs(16 - abs(dy)) / 8);
			if (weight < 1.0) weight = 1.0;

			RxHistAddColor(reduction, yiq.y, yiq.i, yiq.q, yiq.a, weight);
			yLeft = yiq.y;
			aLeft = yiq.a;
		}
	}
}

void RxiTreeFree(RxColorNode *colorBlock, int freeThis) {
	if (colorBlock->left != NULL) {
		RxiTreeFree(colorBlock->left, TRUE);
		colorBlock->left = NULL;
	}
	if (colorBlock->right != NULL) {
		RxiTreeFree(colorBlock->right, TRUE);
		colorBlock->right = NULL;
	}
	if (freeThis) {
		free(colorBlock);
	}
}

static int RxiTreeCountLeaves(const RxColorNode *tree) {
	if (tree->left == NULL && tree->right == NULL) return 1;

	int count = 0;
	if (tree->left != NULL) count += RxiTreeCountLeaves(tree->left);
	if (tree->right != NULL) count += RxiTreeCountLeaves(tree->right);
	return count;
}

static int RxiTreeSplitNode(RxColorNode *node) {
	if (!node->canSplit) return 0; // did not split
	node->canSplit = FALSE;

	if (node->left == NULL && node->right == NULL) {
		if (node->pivotIndex > node->startIndex && node->pivotIndex < node->endIndex) {
			RxColorNode *newNode = (RxColorNode *) calloc(1, sizeof(RxColorNode));
			newNode->canSplit = TRUE;
			newNode->startIndex = node->startIndex;
			newNode->endIndex = node->pivotIndex;
			node->left = newNode;

			newNode = (RxColorNode *) calloc(1, sizeof(RxColorNode));
			newNode->canSplit = TRUE;
			newNode->startIndex = node->pivotIndex;
			newNode->endIndex = node->endIndex;
			node->right = newNode;
			return 1; // did split
		}
	}
	return 0; // did not split
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

		double x0 = reduction->yWeight * reduction->lumaTable[entry->color.y];
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
		double value = reduction->lumaTable[histEntry->color.y] * yWeight
			+ histEntry->color.i * iWeight
			+ histEntry->color.q * qWeight
			+ histEntry->color.a * aWeight;

		histEntry->value = value;
	}

	//sort colors by dot product with the vector
	qsort(thisHistogram, nColors, sizeof(RxHistEntry *), RxiHistEntryComparator);
}

static void RxiTreeNodeInit(RxReduction *reduction, RxColorNode *colorBlock) {
	//calculate the pivot index, as well as average YIQA values.
	int nColors = colorBlock->endIndex - colorBlock->startIndex;
	if (nColors < 2) {
		//1 color: set leaf color to the single histogram color and its weight
		RxHistEntry *entry = reduction->histogramFlat[colorBlock->startIndex];
		memcpy(&colorBlock->color, &entry->color, sizeof(RxYiqColor));
		colorBlock->weight = entry->weight;
		colorBlock->canSplit = FALSE;
		return;
	}

	double projMax = -RX_LARGE_NUMBER;
	double projMin = RX_LARGE_NUMBER;

	double principal[4];
	RxiHistComputePrincipal(reduction, colorBlock->startIndex, colorBlock->endIndex, principal);

	double yWeight = principal[0] * reduction->yWeight;
	double iWeight = principal[1] * reduction->iWeight;
	double qWeight = principal[2] * reduction->qWeight;
	double aWeight = principal[3] * reduction->aWeight;

	for (int i = colorBlock->startIndex; i < colorBlock->endIndex; i++) {
		RxHistEntry *histEntry = reduction->histogramFlat[i];
		double proj = reduction->lumaTable[histEntry->color.y] * yWeight
			+ histEntry->color.i * iWeight
			+ histEntry->color.q * qWeight
			+ histEntry->color.a * aWeight;
			
		histEntry->value = proj;
		if (proj > projMax) projMax = proj;
		if (proj < projMin) projMin = proj;
	}

	double valueRange = projMax - projMin;
	if (valueRange <= 0.0) {
		colorBlock->canSplit = FALSE;
		return;
	}

	COLOR_INFO *colorInfo = (COLOR_INFO *) calloc(nColors, sizeof(COLOR_INFO));

	//sort colors by dot product with the vector
	RxHistEntry **thisHistogram = reduction->histogramFlat + colorBlock->startIndex;
	qsort(thisHistogram, nColors, sizeof(RxHistEntry *), RxiHistEntryComparator);

	//fill out color information in colorInfo
	for (int i = 0; i < nColors; i++) {
		RxHistEntry *entry = thisHistogram[i];
		double weight = entry->weight;
		double cy = reduction->yWeight * reduction->lumaTable[entry->color.y];
		double ci = reduction->iWeight * entry->color.i;
		double cq = reduction->qWeight * entry->color.q;
		double ca = reduction->aWeight * entry->color.a;

		colorInfo[i].y = weight * cy;
		colorInfo[i].i = weight * ci;
		colorInfo[i].q = weight * cq;
		colorInfo[i].a = weight * ca;
		colorInfo[i].weightedSquares = weight * RxiVec4Mag(cy, ci, cq, ca);
		colorInfo[i].weight = weight;
	}
	
	//gather statistics
	double totalWeight = 0.0, totalY = 0.0, sumWeightedSquares = 0.0, totalI = 0.0, totalQ = 0.0, totalA = 0.0;
	for (int i = 0; i < nColors; i++) {
		COLOR_INFO *entry = &colorInfo[i];
		totalWeight += entry->weight;
		sumWeightedSquares += entry->weightedSquares;
		totalY += entry->y;
		totalI += entry->i;
		totalQ += entry->q;
		totalA += entry->a;
		entry->partialSumWeights = totalWeight;
		entry->y = totalY;
		entry->i = totalI;
		entry->q = totalQ;
		entry->a = totalA;
	}

	//computing representative color
	double initY = 0.0, initI = 0.0, initQ = 0.0, initA = 0.0;
	if (reduction->alphaMode == RX_ALPHA_PALETTE) {
		//compute average color, with color values weighted by their alpha
		double sumAlpha = 0.0;
		for (int i = 0; i < nColors; i++) {
			RxHistEntry *ent = thisHistogram[i];

			double weightA = ent->color.a * INV_255;
			initY += reduction->lumaTable[ent->color.y] * ent->weight * weightA;
			initI += ent->color.i * ent->weight * weightA;
			initQ += ent->color.q * ent->weight * weightA;
			initA += ent->color.a * ent->weight;
			sumAlpha += weightA * ent->weight;
		}

		initY = RxiDelinearizeLuma(reduction, initY / sumAlpha);
		initI = initI / sumAlpha;
		initQ = initQ / sumAlpha;
		initA = initA / totalWeight;
	} else {
		//compute average color, treating alpha as an independent channel
		initY = RxiDelinearizeLuma(reduction, (totalY / totalWeight) / reduction->yWeight);
		initI = (totalI / totalWeight) / reduction->iWeight;
		initQ = (totalQ / totalWeight) / reduction->qWeight;
		initA = (totalA / totalWeight) / reduction->aWeight;
	}

	colorBlock->color.y = (int) (initY + 0.5);
	colorBlock->color.i = (int) (initI + (initI < 0.0 ? -0.5 : 0.5));
	colorBlock->color.q = (int) (initQ + (initQ < 0.0 ? -0.5 : 0.5));
	colorBlock->color.a = (int) (initA + 0.5);
	colorBlock->weight = totalWeight;

	//determine pivot index
	int pivotIndex = 0;
	double leastVariance = RX_LARGE_NUMBER;
	for (int i = 0; i < (nColors - 1); i++) {
		COLOR_INFO *entry = &colorInfo[i];
		if (entry->weight > 0.0) {
			double weightR = totalWeight - entry->partialSumWeights;
			if (weightR <= 0.0) weightR = 0.0001;

			double sumSqL = RxiVec4Mag(entry->y, entry->i, entry->q, entry->a) / entry->partialSumWeights;
			double sumSqR = RxiVec4Mag(totalY - entry->y, totalI - entry->i, totalQ - entry->q, totalA - entry->a) / weightR;
			double varianceTotal = sumWeightedSquares - sumSqL - sumSqR;

			//sum variance lower
			if (varianceTotal <= leastVariance) {

				//we'll check the mean left and mean right. They should be different with masking.
				if (reduction->maskColors != RxMaskColorDummy) {
					double yMeanL = entry->y / (entry->partialSumWeights * reduction->yWeight), yMeanR = (totalY - entry->y) / (weightR * reduction->yWeight);
					double iMeanL = entry->i / (entry->partialSumWeights * reduction->iWeight), iMeanR = (totalI - entry->i) / (weightR * reduction->iWeight);
					double qMeanL = entry->q / (entry->partialSumWeights * reduction->qWeight), qMeanR = (totalQ - entry->q) / (weightR * reduction->qWeight);
					double aMeanL = entry->a / (entry->partialSumWeights * reduction->aWeight), aMeanR = (totalA - entry->a) / (weightR * reduction->aWeight);

					int yL = (int) (RxiDelinearizeLuma(reduction, yMeanL) + 0.5), aL = (int) (aMeanL + 0.5);
					int yR = (int) (RxiDelinearizeLuma(reduction, yMeanR) + 0.5), aR = (int) (aMeanR + 0.5);
					int iL = (int) (iMeanL + (iMeanL < 0.0 ? -0.5 : 0.5)), qL = (int) (qMeanL + (qMeanL < 0.0 ? -0.5 : 0.5));
					int iR = (int) (iMeanR + (iMeanR < 0.0 ? -0.5 : 0.5)), qR = (int) (qMeanR + (qMeanR < 0.0 ? -0.5 : 0.5));

					RxYiqColor yiqL = { yL, iL, qL, aL }, yiqR = { yR, iR, qR, aR };
					COLOR32 maskL = RxiMaskYiqToRgb(reduction, &yiqL);
					COLOR32 maskR = RxiMaskYiqToRgb(reduction, &yiqR);
					if (maskL == maskR) continue; // discard this split (centroids mask to the same color)
				}

				leastVariance = varianceTotal;
				pivotIndex = i + 1;
			}
		}
	}
	if (leastVariance == RX_LARGE_NUMBER) {
		colorBlock->canSplit = FALSE;
		free(colorInfo);
		return;
	}

	//double check pivot index
	if (pivotIndex == 0) pivotIndex = 1;
	else if (pivotIndex >= nColors) pivotIndex = nColors - 1;
	colorBlock->pivotIndex = colorBlock->startIndex + pivotIndex;

	double adjustedWeight = 1.0;
	if (!reduction->enhanceColors) {
		adjustedWeight = sqrt(totalWeight);
	}

	double averageSquares = RxiVec4Mag(totalY, totalI, totalQ, totalA) / totalWeight;
	colorBlock->priority = (sumWeightedSquares - averageSquares - leastVariance) / adjustedWeight;
	free(colorInfo);
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

static void RxiTreeAdjustOneChildNodes(RxColorNode *tree) {
	if (tree->left == NULL && tree->right == NULL) return;

	//children
	if (tree->left != NULL) RxiTreeAdjustOneChildNodes(tree->left);
	if (tree->right != NULL) RxiTreeAdjustOneChildNodes(tree->right);

	//scenarios where one child node exists
	RxColorNode *pTake = NULL;
	if (tree->left != NULL && tree->right == NULL) pTake = tree->left;
	if (tree->right != NULL && tree->left == NULL) pTake = tree->right;

	if (pTake != NULL) {
		//copy info from taken node
		memcpy(tree, pTake, sizeof(RxColorNode));
		free(pTake); // do NOT RxiTreeFree. We still want its children!
	}
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
	int ofs = 0;
	RxColorNode **colorBlockPtr = reduction->colorBlocks;
	for (int i = 0; i < reduction->nPaletteColors; i++) {
		if (colorBlockPtr[i] != NULL) {
			COLOR32 rgb32 = RxiMaskYiqToRgb(reduction, &colorBlockPtr[i]->color);

			//write RGB
			reduction->paletteRgb[ofs] = rgb32;

			//write YIQ (with any loss of information to RGB)
			RxConvertRgbToYiq(rgb32, &reduction->paletteYiq[ofs]);
			ofs++;
		}
	}
}

static void RxiPaletteRecluster(RxReduction *reduction) {
	//simple termination conditions
	int nIterations = reduction->nReclusters;
	if (nIterations <= 0) return;

	int nHistEntries = reduction->histogram->nEntries;

	//keep track of error. Used to abort if we mess up the palette
	double error = 0.0, lastError = RX_LARGE_NUMBER;

	//copy main palette to palette copy
	memcpy(reduction->paletteYiqCopy, reduction->paletteYiq, sizeof(reduction->paletteYiq));
	memcpy(reduction->paletteRgbCopy, reduction->paletteRgb, sizeof(reduction->paletteRgb));

	//iterate up to n times
	int nRecomputes = 0;
	RxTotalBuffer *totalsBuffer = reduction->blockTotals;
	for (int k = 0; k < nIterations; k++) {
		//reset block totals
		memset(totalsBuffer, 0, sizeof(reduction->blockTotals));

		//voronoi iteration
		for (int i = 0; i < nHistEntries; i++) {
			RxHistEntry *entry = reduction->histogramFlat[i];
			double weight = entry->weight;
			int hy = entry->color.y, hi = entry->color.i, hq = entry->color.q, ha = entry->color.a;
			double a1 = ha * INV_255;

			double bestDistance = RX_LARGE_NUMBER;
			int bestIndex = 0;
			for (int j = 0; j < reduction->nUsedColors; j++) {
				const RxYiqColor *pyiq = &reduction->paletteYiqCopy[j];

				double diff = RxiComputeColorDifference(reduction, &entry->color, pyiq);
				if (diff < bestDistance) {
					bestDistance = diff;
					bestIndex = j;
				}
			}

			//add to total. YIQ colors scaled by alpha to be unscaled later.
			totalsBuffer[bestIndex].weight += weight;
			totalsBuffer[bestIndex].y += a1 * reduction->lumaTable[hy] * weight;
			totalsBuffer[bestIndex].i += a1 * hi * weight;
			totalsBuffer[bestIndex].q += a1 * hq * weight;
			totalsBuffer[bestIndex].a += ha * weight;
			totalsBuffer[bestIndex].error += bestDistance * weight;
			entry->entry = bestIndex;

			error += bestDistance * weight;
		}

		//quick sanity check of bucket weights (if any are 0, find another color for it.)
		int doRecompute = 0;
		for (int i = 0; i < reduction->nUsedColors; i++) {
			if (totalsBuffer[i].weight <= 0.0) {
				//find the color farthest from this center
				double largestDifference = 0.0;
				int farthestIndex = 0;
				for (int j = 0; j < nHistEntries; j++) {
					RxHistEntry *entry = reduction->histogramFlat[j];
					RxYiqColor *yiq1 = &reduction->paletteYiqCopy[entry->entry];

					//if we mask colors, check this entry against the palette with clamping.
					if (reduction->maskColors != RxMaskColorDummy) {
						COLOR32 histMasked = RxiMaskYiqToRgb(reduction, &entry->color);
						COLOR32 palMasked = RxiMaskYiqToRgb(reduction, yiq1);
						if (histMasked == palMasked) continue; //this difference can't be reconciled
					}

					double diff = RxiComputeColorDifference(reduction, yiq1, &entry->color) * entry->weight;
					if (diff > largestDifference) {
						largestDifference = diff;
						farthestIndex = j;
					}
				}

				//get RGB of new point
				RxHistEntry *entry = reduction->histogramFlat[farthestIndex];
				COLOR32 as32 = RxiMaskYiqToRgb(reduction, &entry->color);
				
				//set this node's center to the point
				reduction->paletteRgbCopy[i] = as32;
				RxConvertRgbToYiq(as32, &reduction->paletteYiqCopy[i]);
				
				//now that we've changed the palette copy, we need to recompute boundaries.
				doRecompute = 1;
				break;
			}
		}

		//if we need to recompute boundaries, do so now. Be careful!! Doing this dumbly has a
		//risk of looping forever! For now just limit the number of recomputations
		if (doRecompute && nRecomputes < 2) {
			nRecomputes++;
			k--;
			continue;
		}
		nRecomputes = 0;

		//after recomputing bounds, now let's see if we're wasting any slots.
		for (int i = 0; i < reduction->nUsedColors; i++) {
			if (totalsBuffer[i].weight <= 0.0) goto finalize;
		}

		//also check palette error; if we've started rising, we passed our locally optimal palette
		if (error > lastError) {
			goto finalize;
		}

		//check: is the palette the same after this iteration as lst?
		if (error == lastError)
			if (memcmp(reduction->paletteRgb, reduction->paletteRgbCopy, sizeof(reduction->paletteRgb)) == 0)
				goto finalize;

		//weight check succeeded, copy this palette to the main palette.
		memcpy(reduction->paletteYiq, reduction->paletteYiqCopy, sizeof(reduction->paletteYiqCopy));
		memcpy(reduction->paletteRgb, reduction->paletteRgbCopy, sizeof(reduction->paletteRgbCopy));

		//if this is the last iteration, skip the new block totals since they won't affect anything
		if (k == nIterations - 1) break;

		//average out the colors in the new partitions
		for (int i = 0; i < reduction->nUsedColors; i++) {
			double weight = totalsBuffer[i].weight;
			double avgA = totalsBuffer[i].a / weight;
			double avgY = totalsBuffer[i].y / (totalsBuffer[i].a * INV_255);
			double avgI = totalsBuffer[i].i / (totalsBuffer[i].a * INV_255);
			double avgQ = totalsBuffer[i].q / (totalsBuffer[i].a * INV_255);

			//delinearize Y
			avgY = RxiDelinearizeLuma(reduction, avgY);

			//convert to integer YIQ
			int iy = (int) (avgY + 0.5);
			int ii = (int) (avgI + (avgI < 0 ? -0.5 : 0.5));
			int iq = (int) (avgQ + (avgQ < 0 ? -0.5 : 0.5));
			int ia = (int) (avgA + 0.5);

			//to RGB
			RxYiqColor yiq = { iy, ii, iq, ia };
			COLOR32 as32 = RxiMaskYiqToRgb(reduction, &yiq);

			reduction->paletteRgbCopy[i] = as32;
			RxConvertRgbToYiq(as32, &reduction->paletteYiqCopy[i]);
		}

		lastError = error;
		error = 0.0;
	}

finalize:
	//delete any entries we couldn't use and shrink the palette size.
	memset(totalsBuffer, 0, sizeof(reduction->blockTotals));
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxYiqColor *histColor = &reduction->histogramFlat[i]->color;

		//find nearest
		double bestDistance = RX_LARGE_NUMBER;
		int bestIndex = 0;
		for (int j = 0; j < reduction->nUsedColors; j++) {
			const RxYiqColor *pyiq = &reduction->paletteYiq[j];

			double diff = RxiComputeColorDifference(reduction, histColor, pyiq);
			if (diff < bestDistance) {
				bestDistance = diff;
				bestIndex = j;
			}
		}

		//add to total
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
			node->startIndex = loc3;
			node->endIndex = loc4;
			RxiTreeNodeInit(reduction, node);

			//HACK
			node->canSplit = 0;

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

void RxComputePalette(RxReduction *reduction) {
	if (reduction->histogramFlat == NULL || reduction->histogram->nEntries == 0) {
		reduction->nUsedColors = 0;
		return;
	}

	//do it
	RxColorNode *treeHead = (RxColorNode *) calloc(1, sizeof(RxColorNode));
	treeHead->canSplit = TRUE;
	treeHead->startIndex = 0;
	treeHead->endIndex = reduction->histogram->nEntries;
	RxiTreeNodeInit(reduction, treeHead);

	reduction->colorTreeHead = treeHead;

	//main color reduction loop
	reduction->nUsedColors = 1;
	while (reduction->nUsedColors < reduction->nPaletteColors) {
		//split and initialize children for the found node.
		RxColorNode *node = RxiTreeFindSplittableNode(treeHead);
		if (node != NULL) {
			//split node
			if (RxiTreeSplitNode(node)) {
				RxiTreeNodeInit(reduction, node->left);
				RxiTreeNodeInit(reduction, node->right);

				reduction->nUsedColors++;
			}
		}

		//when we would reach a termination condition, check first if any colors would be duplicates of each other.
		//this may especially happen when color masking is used, since the masked colors are not yet known.
		if (reduction->nUsedColors >= reduction->nPaletteColors || node == NULL) {
			//merge loop
			while (RxiMergeTreeNodes(reduction, treeHead));
		}

		//no more nodes to split?
		if (node == NULL) break;
	}

	//flatten
	RxColorNode **nodep = reduction->colorBlocks;
	memset(nodep, 0, sizeof(reduction->colorBlocks));
	RxiAddTreeToList(reduction->colorTreeHead, nodep);

	//to array
	RxiPaletteWrite(reduction);

	//perform voronoi iteration
	RxiPaletteRecluster(reduction);
}

void RxHistClear(RxReduction *reduction) {
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	reduction->histogramFlat = NULL;

	if (reduction->histogram != NULL) {
		RxiSlabFree(&reduction->histogram->allocator);
		free(reduction->histogram);
		reduction->histogram = NULL;
	}

	if (reduction->colorTreeHead != NULL) RxiTreeFree(reduction->colorTreeHead, FALSE);
	free(reduction->colorTreeHead);

	reduction->colorTreeHead = NULL;
	reduction->nUsedColors = 0;
	memset(reduction->paletteRgb, 0, sizeof(reduction->paletteRgb));
}

void RxDestroy(RxReduction *reduction) {
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	if (reduction->histogram != NULL) {
		RxiSlabFree(&reduction->histogram->allocator);
		free(reduction->histogram);
	}
	if (reduction->colorTreeHead != NULL) RxiTreeFree(reduction->colorTreeHead, FALSE);
	free(reduction->colorTreeHead);
}

int RxCreatePalette(const COLOR32 *img, unsigned int width, unsigned int height, COLOR32 *pal, unsigned int nColors) {
	return RxCreatePaletteEx(img, width, height, pal, nColors, BALANCE_DEFAULT, BALANCE_DEFAULT, 0, 
		RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_SORT_ALL | RX_FLAG_MASK_BITS);
}

int RxCreatePaletteEx(const COLOR32 *img, unsigned int width, unsigned int height, COLOR32 *pal, unsigned int nColors, int balance, int colorBalance, int enhanceColors, RxFlag flag) {
	RxReduction *reduction = (RxReduction *) calloc(sizeof(RxReduction), 1);
	RxInit(reduction, balance, colorBalance, enhanceColors, nColors);

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
	}

	RxHistAdd(reduction, img, width, height);
	RxHistFinalize(reduction);
	RxComputePalette(reduction);

	//copy palette out
	memcpy(pal, reduction->paletteRgb, nColors * sizeof(COLOR32));
	RxDestroy(reduction);

	int nProduced = reduction->nUsedColors;
	free(reduction);

	if (flag & RX_FLAG_SORT_ONLY_USED) {
		qsort(pal, nProduced, sizeof(COLOR32), RxColorLightnessComparator);
	} else {
		qsort(pal, nColors, sizeof(COLOR32), RxColorLightnessComparator);
	}

	return nProduced;
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
		yiqPalette = (RxYiqColor *) calloc(nColors, sizeof(RxYiqColor));
	}

	//convert palette colors
	for (unsigned int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(palette[i], &yiqPalette[i]);
	}

	double error = RxHistComputePaletteErrorYiq(reduction, yiqPalette, nColors, maxError);

	if (yiqPalette != yiqPaletteStack) free(yiqPalette);
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
				RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE
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
				RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE
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

	//------------STAGE 1

	unsigned int nTiles = tilesX * tilesY;
	RxiTile *tiles = (RxiTile *) calloc(nTiles, sizeof(RxiTile));
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, enhanceColors, nColsPerPalette);

	for (unsigned int y = 0; y < tilesY; y++) {
		for (unsigned int x = 0; x < tilesX; x++) {
			RxiTile *tile = &tiles[x + (y * tilesX)];
			const COLOR32 *pxOrigin = imgBits + x * 8 + (y * 8 * tilesX * 8);
			RxiTileCopy(tile, pxOrigin, tilesX * 8);

			//RxCreatePalette(tile->rgb, 8, 8, palBuf + 1, 15);
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

	//-------------STAGE 2
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

	//-----------STAGE 3
	int nCurrentPalettes = nTiles;
	while (nCurrentPalettes > nPalettes) {

		unsigned int index1, index2;
		RxiTileFindSimilarTiles(tiles, diffBuff, nTiles, &index1, &index2);

		//find all  instances of index2, replace with index1
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
			RxYiqColor *yiqDest = &palTile->palette[i];
			RxConvertRgbToYiq(reduction->paletteRgb[i], yiqDest);
		}
		palTile->nUsedColors = reduction->nUsedColors;
		palTile->nSwallowed += nSwitched;

		//get new use count
		RxiTile *rep = &tiles[index1];
		memset(rep->useCounts, 0, sizeof(rep->useCounts));
		for (unsigned int i = 0; i < nTiles; i++) {
			RxiTile *tile = tiles + i;
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
	int paletteIndices[RX_TILE_PALETTE_COUNT_MAX] = { 0 };

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
		paletteIndices[nPalettesWritten++] = i;
		(*progress)++;
	}

	//palette refinement
	int nRefinements = 4;
	int *bestPalettes = (int *) calloc(nTiles, sizeof(int));
	RxYiqColor *yiqPalette = (RxYiqColor *) calloc(nPalettes, RX_TILE_PALETTE_MAX * sizeof(RxYiqColor));
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
	free(yiqPalette);

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
			qsort(palettes + i * RX_TILE_PALETTE_MAX, nColsPerPalette, 4, RxColorLightnessComparator);
			memcpy(dest + paletteSize * (i + paletteBase) + outputOffs, palettes + i * RX_TILE_PALETTE_MAX, nColsPerPalette * sizeof(COLOR32));
		}

		if (paletteOffset == 0) dest[(i + paletteBase) * paletteSize] = 0xFF00FF;
	}

	free(palettes);
	free(bestPalettes);
	RxDestroy(reduction);
	free(reduction);
	free(tiles);
	free(diffBuff);
}

static int RxiDiffuseCurveY(double x) {
	if (x < 0.0) return -RxiDiffuseCurveY(-x);
	if (x <= 8.0) return (int) (x + 0.5);
	return (int) (8.0 + pow(x - 8.0, 0.9) * 0.94140625 + 0.5);
}

static int RxiDiffuseCurveI(double x) {
	if (x < 0.0) return -RxiDiffuseCurveI(-x);
	if (x <= 8.0) return (int) (x + 0.5);
	return (int) (8.0 + pow(x - 8.0, 0.85) * 0.98828125 + 0.5);
}

static int RxiDiffuseCurveQ(double x) {
	if (x < 0.0) return -RxiDiffuseCurveQ(-x);
	if (x <= 8.0) return (int) (x + 0.5);
	return (int) (8.0 + pow(x - 8.0, 0.85) * 0.89453125 + 0.5);
}

void RxReduceImage(COLOR32 *px, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse) {
	RxReduceImageEx(px, NULL, width, height, palette, nColors, touchAlpha, binaryAlpha, c0xp, diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

void RxReduceImageEx(COLOR32 *img, int *indices, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse, int balance, int colorBalance, int enhanceColors) {
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, enhanceColors, nColors);

	//convert palette to YIQ
	RxYiqColor *yiqPalette = (RxYiqColor *) calloc(nColors, sizeof(RxYiqColor));
	for (unsigned int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(palette[i], &yiqPalette[i]);
	}

	//allocate row buffers for color and diffuse.
	RxYiqColor *thisRow = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));
	RxYiqColor *lastRow = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));
	RxYiqColor *thisDiffuse = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));
	RxYiqColor *nextDiffuse = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));

	//fill the last row with the first row, just to make sure we don't run out of bounds
	for (unsigned int i = 0; i < width; i++) {
		RxConvertRgbToYiq(img[i], lastRow + (i + 1));
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

			int colorY = (thisRow[x + 1].y * 3 + thisRow[x + 2].y * 3 + thisRow[x].y * 3 + lastRow[x + 1].y * 3
						  + lastRow[x].y * 2 + lastRow[x + 2].y * 2) / 16;
			int colorI = (thisRow[x + 1].i * 3 + thisRow[x + 2].i * 3 + thisRow[x].i * 3 + lastRow[x + 1].i * 3
						  + lastRow[x].i * 2 + lastRow[x + 2].i * 2) / 16;
			int colorQ = (thisRow[x + 1].q * 3 + thisRow[x + 2].q * 3 + thisRow[x].q * 3 + lastRow[x + 1].q * 3
						  + lastRow[x].q * 2 + lastRow[x + 2].q * 2) / 16;
			int colorA = thisRow[x + 1].a;

			if (touchAlpha && binaryAlpha) {
				if (colorA < 128) {
					colorY = 0;
					colorI = 0;
					colorQ = 0;
					colorA = 0;
				}
			}

			//match it to a palette color. We'll measure distance to it as well.
			RxYiqColor colorYiq = { colorY, colorI, colorQ, colorA };
			int matched = c0xp + RxiPaletteFindClosestColor(reduction, yiqPalette + c0xp, nColors - c0xp, &colorYiq, NULL);
			if (colorA == 0 && c0xp) matched = 0;

			//measure distance. From middle color to sampled color, and from palette color to sampled color.
			RxYiqColor *matchedYiq = yiqPalette + matched;
			double paletteDy = reduction->lumaTable[matchedYiq->y] - reduction->lumaTable[colorY];
			int paletteDi = matchedYiq->i - colorI;
			int paletteDq = matchedYiq->q - colorQ;
			double paletteDistance = paletteDy * paletteDy * reduction->yWeight2 +
				paletteDi * paletteDi * reduction->iWeight2 +
				paletteDq * paletteDq * reduction->qWeight2;

			//now measure distance from the actual color to its average surroundings
			RxYiqColor centerYiq;
			memcpy(&centerYiq, &thisRow[x + 1], sizeof(RxYiqColor));

			double centerDy = reduction->lumaTable[centerYiq.y] - reduction->lumaTable[colorY];
			int centerDi = centerYiq.i - colorI;
			int centerDq = centerYiq.q - colorQ;
			double centerDistance = centerDy * centerDy * reduction->yWeight2 +
				centerDi * centerDi * reduction->iWeight2 +
				centerDq * centerDq * reduction->qWeight2;

			//now test: Should we dither?
			double yw2 = reduction->yWeight2;
			if (centerDistance < 110.0 * yw2 && paletteDistance >  2.0 * yw2 && diffuse > 0.0f) {
				//Yes, we should dither :)

				//correct for Floyd-Steinberg coefficients by dividing by 16 (and scale by diffusion amount)
				double diffuseY = thisDiffuse[x + 1].y * diffuse * 0.0625;
				double diffuseI = thisDiffuse[x + 1].i * diffuse * 0.0625;
				double diffuseQ = thisDiffuse[x + 1].q * diffuse * 0.0625;
				double diffuseA = thisDiffuse[x + 1].a * diffuse * 0.0625;

				if (!touchAlpha || binaryAlpha) diffuseA = 0.0; //don't diffuse alpha if no alpha channel, or we're told not to

				colorY += RxiDiffuseCurveY(diffuseY);
				colorI += RxiDiffuseCurveI(diffuseI);
				colorQ += RxiDiffuseCurveQ(diffuseQ);
				colorA += (int) diffuseA;
				if (colorY < 0) { //clamp just in case
					colorY = 0;
					colorI = 0;
					colorQ = 0;
				} else if (colorY > 511) {
					colorY = 511;
					colorI = 0;
					colorQ = 0;
				}

				if (colorA < 0) colorA = 0;
				else if (colorA > 255) colorA = 255;

				//match to palette color
				RxYiqColor diffusedYiq = { colorY, colorI, colorQ, colorA };
				matched = c0xp + RxiPaletteFindClosestColor(reduction, yiqPalette + c0xp, nColors - c0xp, &diffusedYiq, NULL);
				if (diffusedYiq.a < 128 && c0xp) matched = 0;
				COLOR32 chosen = (palette[matched] & 0xFFFFFF) | (colorA << 24);
				img[x + y * width] = chosen;
				if (indices != NULL) indices[x + y * width] = matched;

				RxYiqColor *chosenYiq = &yiqPalette[matched];
				int offY = colorY - chosenYiq->y;
				int offI = colorI - chosenYiq->i;
				int offQ = colorQ - chosenYiq->q;
				int offA = colorA - chosenYiq->a;

				//now diffuse to neighbors
				RxYiqColor *diffNextPixel = &thisDiffuse[x + 1 + hDirection];
				RxYiqColor *diffDownPixel = &nextDiffuse[x + 1];
				RxYiqColor *diffNextDownPixel = &nextDiffuse[x + 1 + hDirection];
				RxYiqColor *diffBackDownPixel = &nextDiffuse[x + 1 - hDirection];

				if (colorA >= 128 || !binaryAlpha) { //don't dither if there's no alpha channel and this is transparent!
					diffNextPixel->y += offY * 7;
					diffNextPixel->i += offI * 7;
					diffNextPixel->q += offQ * 7;
					diffNextPixel->a += offA * 7;
					diffDownPixel->y += offY * 5;
					diffDownPixel->i += offI * 5;
					diffDownPixel->q += offQ * 5;
					diffDownPixel->a += offA * 5;
					diffBackDownPixel->y += offY * 3;
					diffBackDownPixel->i += offI * 3;
					diffBackDownPixel->q += offQ * 3;
					diffBackDownPixel->a += offA * 3;
					diffNextDownPixel->y += offY * 1;
					diffNextDownPixel->i += offI * 1;
					diffNextDownPixel->q += offQ * 1;
					diffNextDownPixel->a += offA * 1;
				}

			} else {
				//anomaly in the picture, just match the original color. Don't diffuse, it'll cause issues.
				//That or the color is pretty homogeneous here, so dithering is bad anyway.
				if (c0xp && touchAlpha) {
					if (centerYiq.a < 128) {
						centerYiq.y = 0;
						centerYiq.i = 0;
						centerYiq.q = 0;
						centerYiq.a = 0;
					}
				}

				matched = c0xp + RxiPaletteFindClosestColor(reduction, yiqPalette + c0xp, nColors - c0xp, &centerYiq, NULL);
				if (c0xp && centerYiq.a < 128) matched = 0;
				COLOR32 chosen = (palette[matched] & 0xFFFFFF) | (centerYiq.a << 24);
				img[x + y * width] = chosen;
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

	free(yiqPalette);
	free(thisRow);
	free(lastRow);
	free(thisDiffuse);
	free(nextDiffuse);

	RxDestroy(reduction);
	free(reduction);
}

double RxComputePaletteError(RxReduction *reduction, const COLOR32 *px, unsigned int nPx, const COLOR32 *pal, unsigned int nColors, int alphaThreshold, double nMaxError) {
	if (nMaxError == 0) nMaxError = RX_LARGE_NUMBER;
	double error = 0;

	RxYiqColor paletteYiqStack[16]; //small palettes
	RxYiqColor *paletteYiq = paletteYiqStack;
	if (nColors > 16) {
		paletteYiq = (RxYiqColor *) calloc(nColors, sizeof(RxYiqColor));
	}

	//palette to YIQ
	for (unsigned int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(pal[i], paletteYiq + i);
	}

	for (unsigned int i = 0; i < nPx; i++) {
		COLOR32 p = px[i];
		int a = (p >> 24) & 0xFF;
		if (a < alphaThreshold) continue;

		RxYiqColor yiq;
		RxConvertRgbToYiq(px[i], &yiq);
		double bestDiff;
		(void) RxiPaletteFindClosestColor(reduction, paletteYiq, nColors, &yiq, &bestDiff);

		error += bestDiff;
		if (error >= nMaxError) {
			if (paletteYiq != paletteYiqStack) free(paletteYiq);
			return nMaxError;
		}
	}

	if (paletteYiq != paletteYiqStack) free(paletteYiq);
	return error;
}
