#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "color.h"
#include "palette.h"

#define TRUE 1
#define FALSE 0

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

void RxInit(RxReduction *reduction, int balance, int colorBalance, int optimization, int enhanceColors, unsigned int nColors) {
	memset(reduction, 0, sizeof(RxReduction));
	reduction->yWeight = 60 - balance;
	reduction->iWeight = colorBalance;
	reduction->optimization = optimization;
	reduction->qWeight = 40 - colorBalance;
	reduction->enhanceColors = enhanceColors;
	reduction->nReclusters = RECLUSTER_DEFAULT;// nColors <= 32 ? RECLUSTER_DEFAULT : 0;
	reduction->nPaletteColors = nColors;
	reduction->gamma = 1.27;
	reduction->maskColors = TRUE;

	for (int i = 0; i < 512; i++) {
		reduction->lumaTable[i] = pow((double) i / 511.0, 1.27) * 511.0;
	}
}

static void *RxiSlabAlloc(RxSlab *allocator, int size) {
	if (allocator->allocation == NULL) {
		allocator->allocation = calloc(0x100000, 1);
		allocator->nextEntryOffset = 0;
	}
	while (allocator->nextEntryOffset + size > 0x100000) {
		if (allocator->next == NULL) {
			RxSlab *next = calloc(1, sizeof(RxSlab));
			next->allocation = calloc(0x100000, 1);
			next->nextEntryOffset = 0;
			allocator->next = next;
		}
		allocator = allocator->next;
	}
	void *res = (void *) (((uintptr_t) allocator->allocation) + allocator->nextEntryOffset);
	allocator->nextEntryOffset += size;
	return res;
}

void RxHistAddColor(RxHistogram *histogram, int y, int i, int q, int a, double weight) {
	if (a == 0) return;
	int slotIndex = (q + (y * 64 + i) * 4 + 0x60E + a) & 0x1FFFF;
	if (slotIndex < histogram->firstSlot) histogram->firstSlot = slotIndex;

	RxHistEntry *slot = histogram->entries[slotIndex];

	//find a slot with the same YIQA, or create a new one if none exists.
	if (slot == NULL) {
		slot = (RxHistEntry *) RxiSlabAlloc(&histogram->allocator, sizeof(RxHistEntry));
		slot->color.y = y;
		slot->color.i = i;
		slot->color.q = q;
		slot->color.a = a;
		slot->weight = weight;
		slot->next = NULL;
		slot->value = 0.0;
		histogram->entries[slotIndex] = slot;
		histogram->nEntries++;
		return;
	}
	while (1) {
		if (slot->color.y == y && slot->color.i == i && slot->color.q == q && slot->color.a == a) {
			slot->weight += weight;
			return;
		}

		if (slot->next == NULL) {
			slot->next = (RxHistEntry *) RxiSlabAlloc(&histogram->allocator, sizeof(RxHistEntry));
			slot = slot->next;

			slot->color.y = y;
			slot->color.i = i;
			slot->color.q = q;
			slot->color.a = a;
			slot->weight = weight;
			slot->next = NULL;
			slot->value = 0.0;
			histogram->nEntries++;
			return;
		}
		slot = slot->next;
	}
}

void RxConvertRgbToYiq(COLOR32 rgb, RxYiqColor *yiq) {
	double doubleR = (double) (rgb & 0xFF);
	double doubleG = (double) ((rgb >> 8) & 0xFF);
	double doubleB = (double) ((rgb >> 16) & 0xFF);

	double y = 2.0 * (doubleR * 0.29900 + doubleG * 0.58700 + doubleB * 0.11400);
	double i = 2.0 * (doubleR * 0.59604 - doubleG * 0.27402 - doubleB * 0.32203);
	double q = 2.0 * (doubleR * 0.21102 - doubleG * 0.52204 + doubleB * 0.31103);
	double iCopy = i;

	if(iCopy > 245.0) {
		iCopy = 2 * (iCopy - 245.0) * 0.3333333 + 245.0;
	}

	if(q < -215.0) {
		q = 2 * (q + 215.0) * 0.3333333 - 215.0;
	}

	double iqDiff = q - iCopy;
	if(iqDiff > 265.0){
		double iqDiffShifted = (iqDiff - 265.0) * 0.25;
		iCopy += iqDiffShifted;
		q -= iqDiffShifted;
	}

	double iqProd;
	if(iCopy >= 0.0 || q <= 0.0) {
		iqProd = y;
	} else {
		iqProd = -(q * iCopy) * 0.00195313 + y;
	}

	//round to integers
	int yInt = (int) (iqProd + 0.5);
	int iInt = (int) (i + (i < 0.0 ? -0.5 : 0.5));
	int qInt = (int) (q + (q < 0.0 ? -0.5 : 0.5));

	//clamp variables to good ranges
	yInt = min(max(yInt, 0), 511);
	iInt = min(max(iInt, -320), 319);
	qInt = min(max(qInt, -270), 269);

	//write output
	yiq->y = yInt;
	yiq->i = iInt;
	yiq->q = qInt;
	yiq->a = (rgb >> 24) & 0xFF;
}

void RxConvertYiqToRgb(RxRgbColor *rgb, RxYiqColor *yiq) {
	double i = (double) yiq->i;
	double q = (double) yiq->q;
	double y;
	if(i >= 0.0 || q <= 0.0) {
		y = (double) yiq->y;
	} else {
		y = ((double) yiq->y) + (q * i) * 0.00195313;
	}
	if(y >= 0.0) {
		if(y > 511.0) {
			y = 511.0;
		}
	} else {
		y = 0.0;
	}

	double iqDiff = q - i;
	if(iqDiff > 265.0) {
		iqDiff = (iqDiff - 265.0) * 0.5;
		i -= iqDiff;
		q += iqDiff;
	}

	if(q < -215.0) {
		q = (q + 215.0) * 3.0 * 0.5 - 215.0;
	}

	int r = (int) (y * 0.5 + i * 0.477791 + q * 0.311426 + 0.5);
	int g = (int) (y * 0.5 - i * 0.136066 - q * 0.324141 + 0.5);
	int b = (int) (y * 0.5 - i * 0.552535 + q * 0.852230 + 0.5);

	r = min(max(r, 0), 255);
	g = min(max(g, 0), 255);
	b = min(max(b, 0), 255);

	rgb->r = r;
	rgb->g = g;
	rgb->b = b;
	rgb->a = yiq->a;
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

void RxHistAdd(RxReduction *reduction, COLOR32 *img, int width, int height) {
	int iMask = 0xFFFFFFFF, qMask = 0xFFFFFFFF;
	if (reduction->optimization < 5) {
		qMask = 0xFFFFFFFE;
		if (reduction->optimization < 2) {
			iMask = 0xFFFFFFFE;
		}
	}

	if (reduction->histogram == NULL) {
		reduction->histogram = (RxHistogram *) calloc(1, sizeof(RxHistogram));
		reduction->histogram->firstSlot = 0x20000;
	}

	for (int y = 0; y < height; y++) {
		RxYiqColor yiqLeft;
		RxConvertRgbToYiq(img[y * width], &yiqLeft);
		int yLeft = yiqLeft.y;

		for (int x = 0; x < width; x++) {
			RxYiqColor yiq;
			RxConvertRgbToYiq(img[x + y * width], &yiq);

			int dy = yiq.y - yLeft;
			double weight = (double) (16 - abs(16 - abs(dy)) / 8);
			if (weight < 1.0) weight = 1.0;

			RxHistAddColor(reduction->histogram, yiq.y, yiq.i & iMask, yiq.q & qMask, yiq.a, weight);
			yLeft = yiq.y;
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

static int RxiTreeCountLeaves(RxColorNode *tree) {
	int count = 0;
	if (tree->left == NULL && tree->right == NULL) return 1; //doesn't necessarily have to be a leaf

	if (tree->left != NULL) {
		count = RxiTreeCountLeaves(tree->left);
	}
	if (tree->right != NULL) {
		count += RxiTreeCountLeaves(tree->right);
	}
	return count;
}

static void RxiTreeSplitNodeAtPivot(RxColorNode *tree, int pivotIndex) {
	if (tree->left == NULL && tree->right == NULL) {
		if (pivotIndex > tree->startIndex && pivotIndex >= tree->endIndex) {
			pivotIndex = tree->endIndex - 1;
		}
		if (pivotIndex <= tree->startIndex) {
			pivotIndex = tree->startIndex + 1;
		}

		if (pivotIndex > tree->startIndex && pivotIndex < tree->endIndex) {
			RxColorNode *newNode = (RxColorNode *) calloc(1, sizeof(RxColorNode));
			
			newNode->color.a = 0xFF;
			newNode->isLeaf = TRUE;
			newNode->startIndex = tree->startIndex;
			newNode->endIndex = pivotIndex;
			tree->left = newNode;

			newNode = (RxColorNode *) calloc(1, sizeof(RxColorNode));
			newNode->color.a = 0xFF;
			newNode->isLeaf = TRUE;
			newNode->startIndex = pivotIndex;
			newNode->endIndex = tree->endIndex;
			tree->right = newNode;
		}
	}
	tree->isLeaf = FALSE;
}

static RxColorNode *RxiTreeFindHighPriorityLeaf(RxColorNode *tree) {
	if (tree->left == NULL && tree->right == NULL) {
		if (tree->isLeaf) return tree;
		return NULL;
	}
	RxColorNode *leafLeft = NULL, *leafRight = NULL;
	if (tree->left != NULL) {
		leafLeft = RxiTreeFindHighPriorityLeaf(tree->left);
	}
	if (tree->right != NULL) {
		leafRight = RxiTreeFindHighPriorityLeaf(tree->right);
	}
	
	if (leafLeft != NULL && leafRight == NULL) return leafLeft;
	if (leafRight != NULL && leafLeft == NULL) return leafRight;
	if (leafLeft == NULL && leafRight == NULL) return NULL;

	if (leafRight->priority >= leafLeft->priority) return leafRight;
	return leafLeft;

}

static double RxiHistComputePrincipal(RxReduction *reduction, int startIndex, int endIndex, double *axis) {
	double mtx[4][4] = { 0 };
	double averageY = 0.0, averageI = 0.0, averageQ = 0.0, averageA = 0.0;
	double sumWeight = 0.0;

	for (int i = startIndex; i < endIndex; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];

		double scaledA = entry->color.a * 40;
		double scaledY = reduction->yWeight * reduction->lumaTable[entry->color.y];
		double scaledI = reduction->iWeight * entry->color.i;
		double scaledQ = reduction->qWeight * entry->color.q;
		double weight = entry->weight;

		mtx[0][0] += weight * scaledY * scaledY;
		mtx[0][1] += weight * scaledY * scaledI;
		mtx[0][2] += weight * scaledY * scaledQ;
		mtx[0][3] += weight * scaledY * scaledA;
		mtx[1][1] += weight * scaledI * scaledI;
		mtx[1][2] += weight * scaledI * scaledQ;
		mtx[1][3] += weight * scaledI * scaledA;
		mtx[2][2] += weight * scaledQ * scaledQ;
		mtx[2][3] += weight * scaledQ * scaledA;
		mtx[3][3] += weight * scaledA * scaledA;

		averageY += weight * scaledY;
		averageI += weight * scaledI;
		averageQ += weight * scaledQ;
		averageA += weight * scaledA;
		sumWeight += weight;
	}
	averageY /= sumWeight;
	averageI /= sumWeight;
	averageQ /= sumWeight;
	averageA /= sumWeight;

	mtx[0][0] = mtx[0][0] / sumWeight - averageY * averageY;
	mtx[0][1] = mtx[0][1] / sumWeight - averageY * averageI;
	mtx[0][2] = mtx[0][2] / sumWeight - averageY * averageQ;
	mtx[0][3] = mtx[0][3] / sumWeight - averageY * averageA;
	mtx[1][0] = mtx[0][1];
	mtx[1][1] = mtx[1][1] / sumWeight - averageI * averageI;
	mtx[1][2] = mtx[1][2] / sumWeight - averageI * averageQ;
	mtx[1][3] = mtx[1][3] / sumWeight - averageI * averageA;
	mtx[2][0] = mtx[0][2];
	mtx[2][1] = mtx[1][2];
	mtx[2][2] = mtx[2][2] / sumWeight - averageQ * averageQ;
	mtx[2][3] = mtx[2][3] / sumWeight - averageQ * averageA;
	mtx[3][0] = mtx[0][3];
	mtx[3][1] = mtx[1][3];
	mtx[3][2] = mtx[2][3];
	mtx[3][3] = mtx[3][3] / sumWeight - averageA * averageA;

	double eigen[4][4];
	double diag[4];
	double vec[4];
	for (int i = 1; i <= 4; i++) {
		//setup identity matrix in local_88
		eigen[i - 1][i - 1] = 1.0;
		if (i < 4) {
			for (int j = 0; j < 4 - i; j++) {
				eigen[i - 1][i + j] = 0.0;
				eigen[i + j][i - 1] = 0.0;
			}
		}

		//set up diag with the diagonal
		diag[i - 1] = mtx[i - 1][i - 1];

		//setup vec
		vec[i - 1] = mtx[i % 4][(i + 1) % 4];
	}

	for (int i = 0; i < 1000; i++) {
		if (vec[0] == 0.0 && vec[1] == 0.0 && vec[2] == 0.0 && vec[3] == 0.0) break;

		for (int col0 = 3; col0 >= 0; col0--) {
			int col1 = (col0 + 1) % 4;
			int col2 = (col1 + 1) % 4;
			double absVecComp0 = fabs(vec[col0]);
			if (absVecComp0 > 0.0) {
				double diff = diag[col2] - diag[col1];
				if (absVecComp0 * 100.0 + fabs(diff) == fabs(diff)) { //?
					diff = vec[col0] / diff;
				} else {
					absVecComp0 = (diff * 0.5) / vec[col0];
					diff = 1.0 / (fabs(absVecComp0) + sqrt(absVecComp0 * absVecComp0 + 1.0));
				}

				double vecComp0 = vec[col0];
				double vecComp1 = vec[col1];
				double vecComp2 = vec[col2];

				diag[col1] -= diff * vecComp0;
				diag[col2] += diff * vecComp0;

				double recip = 1.0 / sqrt(diff * diff + 1.0);
				double f2 = recip * diff;
				double f1 = 1.0 - f2 * f2 / (recip + 1.0);
				vec[col2] = vecComp2 * f1 - vecComp1 * f2;
				vec[col1] = vecComp1 * f1 + vecComp2 * f2;
				vec[col0] = 0.0;

				for(int k = 0; k < 4; k++){
					double val1 = eigen[3 - k][col1];
					double val2 = eigen[3 - k][col2];
					eigen[3 - k][col1] = val1 * f1 - val2 * f2;
					eigen[3 - k][col2] = val2 * f1 + val1 * f2;
				}

			}
		}
	}

	if (diag[0] < 0.0) diag[0] = -diag[0];
	if (diag[1] < 0.0) diag[1] = -diag[1];
	if (diag[2] < 0.0) diag[2] = -diag[2];
	if (diag[3] < 0.0) diag[3] = -diag[3];

	int col1 = (diag[1] > diag[0]);
	if (diag[2] > diag[col1]) {
		col1 = 2;
	}
	if (diag[3] > diag[col1]) {
		col1 = 3;
	}

	axis[0] = eigen[0][col1];
	axis[1] = eigen[1][col1];
	axis[2] = eigen[2][col1];
	axis[3] = eigen[3][col1];
	return 1e30;
}

static int RxiHistEntryComparator(const void *p1, const void *p2) {
	RxHistEntry *e1 = *(RxHistEntry **) p1;
	RxHistEntry *e2 = *(RxHistEntry **) p2;
	double d = e1->value - e2->value;
	if (d < 0.0) return -1;
	if (d > 0.0) return 1;
	return 0;
}

static double __inline RxiVec3Mag(double x, double y, double z) {
	return x * x + y * y + z * z;
}

void RxHistSort(RxReduction *reduction, int startIndex, int endIndex) {
	double principal[4];
	int nColors = endIndex - startIndex;
	RxHistEntry **thisHistogram = reduction->histogramFlat + startIndex;
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
	double aWeight = principal[3] * 40.0;

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
	if (colorBlock->endIndex - colorBlock->startIndex < 2) {
		RxHistEntry *entry = reduction->histogramFlat[colorBlock->startIndex];
		memcpy(&colorBlock->color, &entry->color, sizeof(RxYiqColor));
		colorBlock->weight = entry->weight;
		colorBlock->isLeaf = FALSE;
		return;
	}

	double greatestValue = -1e30;
	double leastValue = 1e30;

	double principal[4];
	RxiHistComputePrincipal(reduction, colorBlock->startIndex, colorBlock->endIndex, principal);

	double yWeight = principal[0] * reduction->yWeight;
	double iWeight = principal[1] * reduction->iWeight;
	double qWeight = principal[2] * reduction->qWeight;
	double aWeight = principal[3] * 40.0;

	for (int i = colorBlock->startIndex; i < colorBlock->endIndex; i++) {
		RxHistEntry *histEntry = reduction->histogramFlat[i];
		double value = reduction->lumaTable[histEntry->color.y] * yWeight
			+ histEntry->color.i * iWeight
			+ histEntry->color.q * qWeight
			+ histEntry->color.a * aWeight;
			
		histEntry->value = value;
		if (value >= greatestValue) {
			greatestValue = value;
		}
		if (value < leastValue) {
			leastValue = value;
		}
	}

	double valueRange = greatestValue - leastValue;
	if (valueRange < 0.0) {
		colorBlock->isLeaf = FALSE;
		return;
	}

	int nColors = colorBlock->endIndex - colorBlock->startIndex;

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

		colorInfo[i].y = weight * cy;
		colorInfo[i].i = weight * ci;
		colorInfo[i].q = weight * cq;
		colorInfo[i].a = weight * 40 * entry->color.a;
		colorInfo[i].weightedSquares = weight * RxiVec3Mag(cy, ci, cq);
		colorInfo[i].weight = weight;
	}
	
	//gather statistics
	double totalWeight = 0.0, totalY = 0.0, sumWeightedSquares = 0.0, totalI = 0.0, totalQ = 0.0, totalA = 0.0;
	for (int i = 0; i < nColors; i++) {
		COLOR_INFO *entry = colorInfo + i;
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
	double avgY = totalY / totalWeight;
	avgY = pow((avgY / (double) reduction->yWeight) * 0.00195695, 1.0 / reduction->gamma);

	//compute average color
	int initY = (int) (avgY * 511.0 + 0.5);
	int initI = (int) ((totalI / totalWeight) / reduction->iWeight + 0.5);
	int initQ = (int) ((totalQ / totalWeight) / reduction->qWeight + 0.5);
	int initA = (int) ((totalA / totalWeight) / 40.0 + 0.5);

	colorBlock->color.y = initY;
	colorBlock->color.i = initI;
	colorBlock->color.q = initQ;
	colorBlock->color.a = initA;
	colorBlock->weight = totalWeight;

	double adjustedWeight = 1.0;
	if (!reduction->enhanceColors) {
		adjustedWeight = sqrt(totalWeight);
	}

	//determine pivot index
	int pivotIndex = 0;
	double leastVariance = 1e30;
	for (int i = 0; i < nColors; i++) {
		COLOR_INFO *entry = colorInfo + i;
		if (entry->weight > 0.0) {
			double weightAfter = totalWeight - entry->partialSumWeights;
			if (weightAfter <= 0.0) {
				weightAfter = 0.0001;
			}
			double averageLeftSquared = RxiVec3Mag(entry->y, entry->i, entry->q) / entry->partialSumWeights;
			double averageRightSquared = RxiVec3Mag(totalY - entry->y, totalI - entry->i, totalQ - entry->q) / weightAfter;
			double varianceTotal = sumWeightedSquares - averageLeftSquared - averageRightSquared;

			if (varianceTotal <= leastVariance) {
				leastVariance = varianceTotal;
				pivotIndex = i + 1;
			}
		}
	}

	//double check pivot index
	if (pivotIndex == 0) pivotIndex = 1;
	else if (pivotIndex >= nColors) pivotIndex = nColors - 1;
	pivotIndex = pivotIndex + colorBlock->startIndex;
	colorBlock->pivotIndex = pivotIndex;

	double averageSquares = RxiVec3Mag(totalY, totalI, totalQ) / totalWeight;
	colorBlock->priority = (sumWeightedSquares - averageSquares - leastVariance) / adjustedWeight;
	free(colorInfo);
}

static double RxiTreeAddNodePriorities(RxColorNode *n1, RxColorNode *n2, RxReduction *reduction) {
	double p1 = n1->priority, p2 = n2->priority;
	double adjustedWeight = 1.0;
	if (!reduction->enhanceColors) {
		p1 *= sqrt(n1->weight);
		p2 *= sqrt(n2->weight);
		adjustedWeight = sqrt(n1->weight + n2->weight);
	}
	return (p1 + p2) / adjustedWeight;
}

static RxColorNode *RxiTreeFindNodeByColor(RxColorNode *treeHead, RxColorNode *src, int maskColors) {
	if (treeHead == NULL || treeHead == src) return NULL;

	if (treeHead->left != NULL || treeHead->right != NULL) {
		RxColorNode *foundLeft = RxiTreeFindNodeByColor(treeHead->left, src, maskColors);
		if (foundLeft != NULL) return foundLeft;

		RxColorNode *foundRight = RxiTreeFindNodeByColor(treeHead->right, src, maskColors);
		return foundRight;
	}

	//is leaf, does this match?
	RxRgbColor rgb;
	RxConvertYiqToRgb(&rgb, &src->color);
	COLOR32 compare = rgb.r | (rgb.g << 8) | (rgb.b << 16);
	if (maskColors) compare = ColorRoundToDS15(compare);

	RxConvertYiqToRgb(&rgb, &treeHead->color);
	COLOR32 thisRgb = rgb.r | (rgb.g << 8) | (rgb.b << 16);
	if (maskColors) thisRgb = ColorRoundToDS15(thisRgb);

	return (compare == thisRgb) ? treeHead : NULL;
}

static RxColorNode *RxiTreeFindNodeByChild(RxColorNode *treeHead, RxColorNode *child) {
	if (treeHead == NULL) return NULL;
	if (treeHead->left == child || treeHead->right == child) return treeHead;

	RxColorNode *foundLeft = RxiTreeFindNodeByChild(treeHead->left, child);
	if (foundLeft != NULL) return foundLeft;

	return RxiTreeFindNodeByChild(treeHead->right, child);
}

static RxColorNode *RxiTreeFindNodeByIndex(RxColorNode *treeHead, int index) {
	if (treeHead == NULL) return NULL;
	if (treeHead->left == NULL && treeHead->right == NULL) return treeHead;
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

static RxColorNode **RxiAddTreeToList(RxColorNode *colorBlock, RxColorNode **colorBlockList) {
	if (colorBlock->left == NULL && colorBlock->right == NULL) {
		*colorBlockList = colorBlock;
		return colorBlockList + 1;
	}
	if (colorBlock->left != NULL) {
		colorBlockList = RxiAddTreeToList(colorBlock->left, colorBlockList);
	}
	if (colorBlock->right != NULL) {
		colorBlockList = RxiAddTreeToList(colorBlock->right, colorBlockList);
	}
	return colorBlockList;
}

static void RxiPaletteWrite(RxReduction *reduction) {
	if (reduction->colorTreeHead == NULL) return;

	//convert to RGB
	int ofs = 0;
	RxColorNode **colorBlockPtr = reduction->colorBlocks;
	for (int i = 0; i < reduction->nPaletteColors; i++) {
		if (colorBlockPtr[i] != NULL) {
			RxColorNode *block = colorBlockPtr[i];
			RxRgbColor rgb;
			RxConvertYiqToRgb(&rgb, &block->color);

			COLOR32 rgb32 = rgb.r | (rgb.g << 8) | (rgb.b << 16);
			if (reduction->maskColors) rgb32 = ColorRoundToDS15(rgb32);

			//write RGB
			reduction->paletteRgb[ofs][0] = rgb32 & 0xFF;
			reduction->paletteRgb[ofs][1] = (rgb32 >> 8) & 0xFF;
			reduction->paletteRgb[ofs][2] = (rgb32 >> 16) & 0xFF;

			//write YIQ (with any loss of information to RGB)
			RxConvertRgbToYiq(rgb32, &reduction->paletteYiq[ofs]);
			ofs++;
		}
	}
}

static double __inline RxiComputeColorDifference(RxReduction *reduction, RxYiqColor *yiq1, RxYiqColor *yiq2) {
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	double dy = reduction->lumaTable[yiq1->y] - reduction->lumaTable[yiq2->y];
	double di = yiq1->i - yiq2->i;
	double dq = yiq1->q - yiq2->q;
	return yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq;
}

static void RxiPaletteRecluster(RxReduction *reduction) {
	//simple termination conditions
	int nIterations = reduction->nReclusters;
	if (nIterations <= 0) return;
	if (reduction->nUsedColors < reduction->nPaletteColors) return;
	if (reduction->nUsedColors >= reduction->histogram->nEntries) return;

	int nHistEntries = reduction->histogram->nEntries;
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	//keep track of error. Used to abort if we mess up the palette
	double error = 0.0, lastError = 1e32;

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

			double bestDistance = 1e30;
			int bestIndex = 0;
			for (int j = 0; j < reduction->nUsedColors; j++) {
				RxYiqColor *pyiq = &reduction->paletteYiqCopy[j];

				double dy = hy - pyiq->y;
				double di = hi - pyiq->i;
				double dq = hq - pyiq->q;
				double diff = yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq;
				if (diff < bestDistance) {
					bestDistance = diff;
					bestIndex = j;
				}
			}

			//add to total
			totalsBuffer[bestIndex].weight += weight;
			totalsBuffer[bestIndex].y += reduction->lumaTable[hy] * weight;
			totalsBuffer[bestIndex].i += hi * weight;
			totalsBuffer[bestIndex].q += hq * weight;
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
					if (reduction->maskColors) {
						RxRgbColor histRgb, palRgb;
						RxConvertYiqToRgb(&histRgb, &entry->color);
						RxConvertYiqToRgb(&palRgb, yiq1);

						COLOR32 histMasked = ColorRoundToDS15(histRgb.r | (histRgb.g << 8) | (histRgb.b << 16));
						COLOR32 palMasked = ColorRoundToDS15(palRgb.r | (palRgb.g << 8) | (palRgb.b << 16));
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
				RxRgbColor rgb = { 0 };
				RxConvertYiqToRgb(&rgb, &entry->color);
				COLOR32 as32 = rgb.r | (rgb.g << 8) | (rgb.b << 16) | 0xFF000000;
				if (reduction->maskColors) as32 = ColorRoundToDS15(as32) | 0xFF000000;
				
				//set this node's center to the point
				RxConvertRgbToYiq(as32, &reduction->paletteYiqCopy[i]);
				reduction->paletteRgbCopy[i][0] = (as32 >> 0) & 0xFF;
				reduction->paletteRgbCopy[i][1] = (as32 >> 8) & 0xFF;
				reduction->paletteRgbCopy[i][2] = (as32 >> 16) & 0xFF;
				
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
			if (totalsBuffer[i].weight <= 0.0) return;
		}

		//also check palette error; if we've started rising, we passed our locally optimal palette
		if (error > lastError) {
			return;
		}

		//check: is the palette the same after this iteration as lst?
		if (error == lastError)
			if (memcmp(reduction->paletteRgb, reduction->paletteRgbCopy, sizeof(reduction->paletteRgb)) == 0)
				return;

		//weight check succeeded, copy this palette to the main palette.
		memcpy(reduction->paletteYiq, reduction->paletteYiqCopy, sizeof(reduction->paletteYiqCopy));
		memcpy(reduction->paletteRgb, reduction->paletteRgbCopy, sizeof(reduction->paletteRgbCopy));

		//if this is the last iteration, skip the new block totals since they won't affect anything
		if (k == nIterations - 1) break;

		//average out the colors in the new partitions
		for (int i = 0; i < reduction->nUsedColors; i++) {
			double weight = totalsBuffer[i].weight;
			double avgY = totalsBuffer[i].y / weight;
			double avgI = totalsBuffer[i].i / weight;
			double avgQ = totalsBuffer[i].q / weight;
			double avgA = totalsBuffer[i].a / weight;

			//delinearize Y
			avgY = 511.0 * pow(avgY * 0.00195695, 1.0 / reduction->gamma);

			//convert to integer YIQ
			int iy = (int) (avgY + 0.5);
			int ii = (int) (avgI + (avgI < 0 ? -0.5 : 0.5));
			int iq = (int) (avgQ + (avgQ < 0 ? -0.5 : 0.5));
			int ia = (int) (avgA + 0.5);

			//to RGB
			RxRgbColor rgb;
			RxYiqColor yiq = { iy, ii, iq, ia };
			RxConvertYiqToRgb(&rgb, &yiq);
			COLOR32 as32 = rgb.r | (rgb.g << 8) | (rgb.b << 16);
			if (reduction->maskColors) as32 = ColorRoundToDS15(as32);

			reduction->paletteRgbCopy[i][0] = as32 & 0xFF;
			reduction->paletteRgbCopy[i][1] = (as32 >> 8) & 0xFF;
			reduction->paletteRgbCopy[i][2] = (as32 >> 16) & 0xFF;
			RxConvertRgbToYiq(as32, &reduction->paletteYiqCopy[i]);
		}

		lastError = error;
		error = 0.0;
	}
}

void RxComputePalette(RxReduction *reduction) {
	if (reduction->histogramFlat == NULL) {
		reduction->nUsedColors = 0;
		return;
	}

	//do it
	RxColorNode *treeHead = (RxColorNode *) calloc(1, sizeof(RxColorNode));
	treeHead->isLeaf = TRUE;
	treeHead->color.a = 0xFF;
	treeHead->endIndex = reduction->histogram->nEntries;

	reduction->colorTreeHead = treeHead;
	if (reduction->histogram->nEntries == 0) {
		reduction->nUsedColors = 0;
		return;
	}
	RxiTreeNodeInit(reduction, treeHead);

	int numberOfTreeElements = 0;
	if (treeHead->left == NULL && treeHead->right == NULL) {
		numberOfTreeElements = 1;
	} else {
		if (treeHead->left != NULL) {
			numberOfTreeElements = RxiTreeCountLeaves(treeHead->left);
		}
		if (treeHead->right != NULL) {
			numberOfTreeElements = RxiTreeCountLeaves(treeHead->right);
		}
	}

	reduction->nUsedColors = 1;
	if (numberOfTreeElements < reduction->nPaletteColors) {
		RxColorNode *colorBlock;
		while ((colorBlock = RxiTreeFindHighPriorityLeaf(treeHead)) != NULL) {
			RxiTreeSplitNodeAtPivot(colorBlock, colorBlock->pivotIndex);

			RxColorNode *leftBlock = colorBlock->left, *rightBlock = colorBlock->right;
			if (leftBlock != NULL) {
				RxiTreeNodeInit(reduction, leftBlock);

				//destroy this node?
				if (leftBlock->weight < 1.0) {
					if (leftBlock->left != NULL) {
						RxiTreeFree(leftBlock->left, TRUE);
						leftBlock->left = NULL;
					}
					if (leftBlock->right != NULL) {
						RxiTreeFree(leftBlock->right, TRUE);
						leftBlock->right = NULL;
					}
					colorBlock->left = NULL;
				}
			}

			if (rightBlock != NULL) {
				RxiTreeNodeInit(reduction, rightBlock);

				//destroy this node?
				if (rightBlock->weight < 1.0) {
					if (rightBlock->left != NULL) {
						RxiTreeFree(rightBlock->left, TRUE);
						rightBlock->left = NULL;
					}
					if (rightBlock->right != NULL) {
						RxiTreeFree(rightBlock->right, TRUE);
						rightBlock->right = NULL;
					}
					colorBlock->right = NULL;
				}
			}
			//it is possible to end up with a branch with no leaves, if they all die :(

			if (colorBlock->left != NULL && colorBlock->right != NULL) {
				RxRgbColor decodedLeft, decodedRight;
				RxConvertYiqToRgb(&decodedLeft, &colorBlock->left->color);
				RxConvertYiqToRgb(&decodedRight, &colorBlock->right->color);

				int leftAlpha = colorBlock->left->color.a;
				int rightAlpha = colorBlock->right->color.a;
				COLOR32 leftRgb = decodedLeft.r | (decodedLeft.g << 8) | (decodedLeft.b << 16);
				COLOR32 rightRgb = decodedRight.r | (decodedRight.g << 8) | (decodedRight.b << 16);
				if (reduction->maskColors && numberOfTreeElements > 2) { //don't prune too quickly
					leftRgb = ColorRoundToDS15(leftRgb);
					rightRgb = ColorRoundToDS15(rightRgb);
				}

				if (leftRgb == rightRgb && leftAlpha == rightAlpha) {
					leftBlock = colorBlock->left, rightBlock = colorBlock->right;

					//prune left
					if (leftBlock != NULL) {
						if (leftBlock->left != NULL) {
							RxiTreeFree(leftBlock->left, TRUE);
							leftBlock->left = NULL;
						}
						if (leftBlock->right != NULL) {
							RxiTreeFree(leftBlock->right, TRUE);
							leftBlock->right = NULL;
						}
						free(leftBlock);
						leftBlock = NULL;
					}
					colorBlock->left = NULL;

					//prune right
					if (rightBlock != NULL) {
						if (rightBlock->left != NULL) {
							RxiTreeFree(rightBlock->left, TRUE);
							rightBlock->left = NULL;
						}
						if (rightBlock->right != NULL) {
							RxiTreeFree(rightBlock->right, TRUE);
							rightBlock->right = NULL;
						}
						free(rightBlock);
						rightBlock = NULL;
					}
					colorBlock->right = NULL;
				}
			}

			//count number of leaves
			numberOfTreeElements = 0;
			if (treeHead->left == NULL && treeHead->right == NULL) {
				numberOfTreeElements = 1;
			} else {
				if (treeHead->left != NULL) {
					numberOfTreeElements += RxiTreeCountLeaves(treeHead->left);
				}
				if (treeHead->right != NULL) {
					numberOfTreeElements += RxiTreeCountLeaves(treeHead->right);
				}
			}

			if (numberOfTreeElements >= reduction->nPaletteColors) {

				//duplicate color test
				int prunedNode = 0;
				for (int i = 0; i < numberOfTreeElements; i++) {
					RxColorNode *node = RxiTreeFindNodeByIndex(treeHead, i);
					RxColorNode *dup = RxiTreeFindNodeByColor(treeHead, node, reduction->maskColors);
					if (dup != NULL) {
						double p1 = node->priority, p2 = dup->priority;
						double total = RxiTreeAddNodePriorities(node, dup, reduction);
						RxColorNode *toDelete = dup, *toKeep = node, *parent = NULL;

						//find which node should keep, which to remove using priority
						if (p1 < p2) {
							toDelete = node;
							toKeep = dup;
						}
						parent = RxiTreeFindNodeByChild(treeHead, toDelete);
						toKeep->priority = total;

						//remove node from existence
						RxiTreeFree(toDelete, TRUE);
						if (parent->left == toDelete) parent->left = NULL;
						if (parent->right == toDelete) parent->right = NULL;
						if (parent->right == NULL && parent->left == NULL) RxiTreeCleanEmptyNode(treeHead, parent);
						numberOfTreeElements--;
						prunedNode = 1;
						break;
					}
				}

				if(!prunedNode) break;
			}
		}
	}

	reduction->nUsedColors = numberOfTreeElements;

	//flatten
	RxColorNode **colorBlockPtr = reduction->colorBlocks;
	memset(colorBlockPtr, 0, sizeof(reduction->colorBlocks));
	RxiAddTreeToList(reduction->colorTreeHead, colorBlockPtr);

	//to array
	RxiPaletteWrite(reduction);

	//perform voronoi iteration
	RxiPaletteRecluster(reduction);
}

static void RxiSlabFree(RxSlab *allocator) {
	if (allocator->allocation != NULL) free(allocator->allocation);
	allocator->allocation = NULL;
	if (allocator->next != NULL) {
		RxiSlabFree(allocator->next);
		allocator->next = NULL;
	}
}

void RxDestroy(RxReduction *reduction) {
	if(reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	if (reduction->histogram != NULL) {
		RxiSlabFree(&reduction->histogram->allocator);
		free(reduction->histogram);
	}
	if(reduction->colorTreeHead != NULL) RxiTreeFree(reduction->colorTreeHead, FALSE);
	free(reduction->colorTreeHead);
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

extern int RxColorLightnessComparator(const void *d1, const void *d2);

int RxCreatePalette(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors) {
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, 20, 20, 15, FALSE, nColors);
	RxHistAdd(reduction, img, width, height);
	RxHistFinalize(reduction);
	RxComputePalette(reduction);
	
	for (unsigned int i = 0; i < nColors; i++) {
		uint8_t r = reduction->paletteRgb[i][0];
		uint8_t g = reduction->paletteRgb[i][1];
		uint8_t b = reduction->paletteRgb[i][2];
		pal[i] = r | (g << 8) | (b << 16);
	}

	RxDestroy(reduction);
	free(reduction);
	qsort(pal, nColors, 4, RxColorLightnessComparator);
	return 0;
}

typedef struct RxiTile_ {
	COLOR32 rgb[64];
	uint8_t indices[64];
	RxYiqColor palette[16]; //YIQ
	int useCounts[16];
	unsigned short palIndex; //points to the index of the tile that is maintaining the palette this tile uses
	unsigned short nUsedColors; //number of filled slots
	unsigned short nSwallowed;
} RxiTile;

static void RxiTileCopy(RxiTile *dest, COLOR32 *pxOrigin, int width) {
	for (int y = 0; y < 8; y++) {
		memcpy(dest->rgb + y * 8, pxOrigin + y * width, 32);
	}
}

static int RxiPaletteFindClosestRgbColorYiqPaletteSimple(RxReduction *reduction, RxYiqColor *palette, int nColors, COLOR32 col, double *outDiff) {
	RxYiqColor yiq;
	RxConvertRgbToYiq(col, &yiq);

	double leastDiff = 1e32;
	int leastIndex = 0;
	for (int i = 0; i < nColors; i++) {
		RxYiqColor *yiq2 = palette + i;

		double diff = RxiComputeColorDifference(reduction, &yiq, yiq2);
		if (diff < leastDiff) {
			leastDiff = diff;
			leastIndex = i;
		}
	}
	if (outDiff != NULL) *outDiff = leastDiff;
	return leastIndex;
}

double RxHistComputePaletteErrorYiq(RxReduction *reduction, RxYiqColor *palette, int nColors, double maxError) {
	double error = 0.0;

	//sum total weighted squared differences
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		RxHistEntry *entry = reduction->histogramFlat[i];
		
		int closest = RxPaletteFindCloestColorYiq(reduction, &entry->color, palette, nColors);
		RxYiqColor *closestYiq = palette + closest;
		double dy = reduction->lumaTable[entry->color.y] - reduction->lumaTable[closestYiq->y];
		int di = entry->color.i - closestYiq->i;
		int dq = entry->color.q - closestYiq->q;
		error += (yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq) * entry->weight;

		if (error >= maxError) return maxError;
	}
	return error;
}

double RxHistComputePaletteError(RxReduction *reduction, COLOR32 *palette, int nColors, double maxError) {
	RxYiqColor yiqPaletteStack[16];
	RxYiqColor *yiqPalette = yiqPaletteStack;
	if (nColors > 16) {
		yiqPalette = (RxYiqColor *) calloc(nColors, sizeof(RxYiqColor));
	}

	//convert palette colors
	for (int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(palette[i], yiqPalette + i);
	}

	double error = RxHistComputePaletteErrorYiq(reduction, yiqPalette, nColors, maxError);

	if (yiqPalette != yiqPaletteStack) free(yiqPalette);
	return error;
}

static int RxiPaletteFindClosestColorYiqSimple(RxReduction *reduction, RxYiqColor *palette, int nColors, RxYiqColor *col, double *outDiff) {
	RxRgbColor rgb;
	RxConvertYiqToRgb(&rgb, col);
	return RxiPaletteFindClosestRgbColorYiqPaletteSimple(reduction, palette, nColors, rgb.r | (rgb.g << 8) | (rgb.b << 16), outDiff);
}

static double RxiTileComputePaletteDifference(RxReduction *reduction, RxiTile *tile1, RxiTile *tile2) {
	//are the palettes identical?
	if (tile1->nUsedColors == tile2->nUsedColors && memcmp(tile1->palette, tile2->palette, tile1->nUsedColors * sizeof(tile1->palette[0])) == 0) return 0;

	//map each color from tile2 to one of tile1
	double totalDiff = 0.0;
	for (int i = 0; i < tile2->nUsedColors; i++) {
		RxYiqColor *yiq = &tile2->palette[i];
		double diff = 0.0;
		int closest = RxiPaletteFindClosestColorYiqSimple(reduction, &tile1->palette[0], tile1->nUsedColors, yiq, &diff);

		if (diff > 0) {
			totalDiff += diff * tile2->useCounts[i];
		}

	}
	if (totalDiff > 0) totalDiff = sqrt(totalDiff);
	if (totalDiff == 0.0) return 0;

	if ((tile1->nUsedColors + tile2->nUsedColors) <= reduction->nPaletteColors) {
		if (tile2->nUsedColors <= reduction->nPaletteColors / 2) {
			//totalDiff = totalDiff * (double) tile2->nUsedColors / reduction->nPaletteColors;
		}
	} else {
		totalDiff += 2.0 * (tile1->nUsedColors + tile2->nUsedColors - reduction->nPaletteColors);
	}

	totalDiff += 15.0 * sqrt(tile1->nSwallowed * tile2->nSwallowed);
	return totalDiff;
}

static double RxiTileFindSimilarTiles(RxiTile *tiles, double *similarities, int nTiles, int *i1, int *i2) {
	//find a pair of tiles. Both must be representative tiles.

	double leastDiff = 1e32;
	int best1 = 0, best2 = 1;
	for (int i = 0; i < nTiles; i++) {
		RxiTile *tile1 = tiles + i;
		if (tile1->palIndex != i) continue;

		for (int j = 0; j < nTiles; j++) {
			RxiTile *tile2 = tiles + j;

			if (tile2->palIndex != j) continue;
			if (i == j) continue;

			//test difference
			if (similarities[i * nTiles + j] <= leastDiff) {
				leastDiff = similarities[i * nTiles + j];
				best1 = i;
				best2 = j;
				if (!leastDiff) goto done;
			}
		}
	}

done:
	*i1 = best1;
	*i2 = best2;
	return leastDiff;
}

void RxCreateMultiplePalettes(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							int paletteSize, int nColsPerPalette, int paletteOffset, int *progress) {
	RxCreateMultiplePalettesEx(imgBits, tilesX, tilesY, dest, paletteBase, nPalettes, paletteSize, nColsPerPalette, 
							 paletteOffset, BALANCE_DEFAULT, BALANCE_DEFAULT, 0, progress);
}

void RxCreateMultiplePalettesEx(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							  int paletteSize, int nColsPerPalette, int paletteOffset, int balance, 
							  int colorBalance, int enhanceColors, int *progress) {
	if (nPalettes == 0) return;
	if (nPalettes == 1) {
		if (paletteOffset) {
			RxCreatePaletteEx(imgBits, tilesX * 8, tilesY * 8, dest + (paletteBase * paletteSize) + paletteOffset, nColsPerPalette, balance, colorBalance, enhanceColors, 0);
		} else {
			RxCreatePaletteEx(imgBits, tilesX * 8, tilesY * 8, dest + (paletteBase * paletteSize) + paletteOffset + 1, nColsPerPalette - 1, balance, colorBalance, enhanceColors, 0);
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
	if (nColsPerPalette >= 16) nColsPerPalette = 15;
	
	//3 stage algorithm:
	//	1 - split into tiles
	//	2 - map similarities
	//	3 - palette merging

	//------------STAGE 1
	int nTiles = tilesX * tilesY;
	RxiTile *tiles = (RxiTile *) calloc(nTiles, sizeof(RxiTile));
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, 15, enhanceColors, nColsPerPalette);
	reduction->maskColors = FALSE;
	COLOR32 palBuf[16];
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			RxiTile *tile = tiles + x + (y * tilesX);
			COLOR32 *pxOrigin = imgBits + x * 8 + (y * 8 * tilesX * 8);
			RxiTileCopy(tile, pxOrigin, tilesX * 8);

			//RxCreatePalette(tile->rgb, 8, 8, palBuf + 1, 15);
			RxHistClear(reduction);
			RxHistAdd(reduction, tile->rgb, 8, 8);
			RxHistFinalize(reduction);
			RxComputePalette(reduction);
			for (int i = 0; i < 16; i++) {
				uint8_t *col = &reduction->paletteRgb[i][0];
				palBuf[i] = col[0] | (col[1] << 8) | (col[2] << 16);
			}

			for (int i = 0; i < 16; i++) {
				RxConvertRgbToYiq(palBuf[i], &tile->palette[i]);
			}
			tile->nUsedColors = reduction->nUsedColors;

			//match pixels to palette indices
			for (int i = 0; i < 64; i++) {
				int index = RxiPaletteFindClosestRgbColorYiqPaletteSimple(reduction, &tile->palette[0], tile->nUsedColors, tile->rgb[i], NULL);
				if ((tile->rgb[i] >> 24) == 0) index = 15;
				tile->indices[i] = index;
				tile->useCounts[index]++;
			}
			tile->palIndex = x + y * tilesX;
			tile->nSwallowed = 1;
		}
	}

	//-------------STAGE 2
	double *diffBuff = (double *) calloc(nTiles * nTiles, sizeof(double));
	for (int i = 0; i < nTiles; i++) {
		RxiTile *tile1 = tiles + i;
		for (int j = 0; j < nTiles; j++) {
			RxiTile *tile2 = tiles + j;

			//write difference
			if (i == j) diffBuff[i + j * nTiles] = 0;
			else {
				diffBuff[i + j * nTiles] = RxiTileComputePaletteDifference(reduction, tile1, tile2);
			}
		}
		(*progress)++;
	}

	//-----------STAGE 3
	int nCurrentPalettes = nTiles;
	while (nCurrentPalettes > nPalettes) {

		int index1, index2;
		double diff = RxiTileFindSimilarTiles(tiles, diffBuff, nTiles, &index1, &index2);

		//find all  instances of index2, replace with index1
		int nSwitched = 0;
		for (int i = 0; i < nTiles; i++) {
			if (tiles[i].palIndex == index2) {
				tiles[i].palIndex = index1;
				nSwitched++;
			}
		}

		//build new palette
		RxHistClear(reduction);
		for (int i = 0; i < nTiles; i++) {
			if (tiles[i].palIndex == index1) {
				RxHistAdd(reduction, tiles[i].rgb, 8, 8);
			}
		}
		RxHistFinalize(reduction);
		RxComputePalette(reduction);

		//write over the palette of the tile
		RxiTile *palTile = tiles + index1;
		COLOR32 palBuf[16];
		for (int i = 0; i < 15; i++) {
			RxYiqColor *yiqDest = &palTile->palette[i];
			uint8_t *srcRgb = &reduction->paletteRgb[i][0];
			COLOR32 rgb = srcRgb[0] | (srcRgb[1] << 8) | (srcRgb[2] << 16);
			palBuf[i] = rgb;
			RxConvertRgbToYiq(rgb, yiqDest);
		}
		palTile->nUsedColors = reduction->nUsedColors;
		palTile->nSwallowed += nSwitched;

		//get new use count
		RxiTile *rep = tiles + index1;
		memset(rep->useCounts, 0, sizeof(rep->useCounts));
		for (int i = 0; i < nTiles; i++) {
			RxiTile *tile = tiles + i;
			if (tile->palIndex != index1) continue;

			for (int j = 0; j < 64; j++) {
				COLOR32 col = tile->rgb[j];
				int index = RxiPaletteFindClosestRgbColorYiqPaletteSimple(reduction, tile->palette, tile->nUsedColors, tile->rgb[j], NULL);
				if ((col >> 24) == 0) index = 15;
				tile->indices[j] = index;
				rep->useCounts[index]++;
			}
		}

		//recompute differences for index1 and representative tiles
		for (int i = 0; i < nTiles; i++) {
			RxiTile *t = tiles + i;
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
	COLOR32 palettes[16 * 16] = { 0 };
	int paletteIndices[16] = { 0 };
	reduction->maskColors = TRUE;
	for (int i = 0; i < nTiles; i++) {
		RxiTile *t = tiles + i;
		if (t->palIndex != i) continue;

		//rebuild palette but with masking enabled
		RxHistClear(reduction);
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].palIndex == t->palIndex) {
				RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
		}
		RxHistFinalize(reduction);
		RxComputePalette(reduction);
		
		for (int j = 0; j < 15; j++) {
			uint8_t *rgb = &reduction->paletteRgb[j][0];
			palettes[j + nPalettesWritten * 16] = ColorRoundToDS15(rgb[0] | (rgb[1] << 8) | (rgb[2] << 16));
		}
		paletteIndices[nPalettesWritten] = i;
		nPalettesWritten++;
		(*progress)++;
	}

	//palette refinement
	int nRefinements = 4;
	int *bestPalettes = (int *) calloc(nTiles, sizeof(int));
	RxYiqColor *yiqPalette = (RxYiqColor *) calloc(nPalettes, 16 * sizeof(RxYiqColor));
	for (int k = 0; k < nRefinements; k++) {
		//palette to YIQ
		for (int i = 0; i < nPalettes; i++) {
			for (int j = 0; j < nColsPerPalette; j++) {
				RxConvertRgbToYiq(palettes[i * 16 + j], yiqPalette + (i * 16 + j));
			}
		}

		//find best palette for each tile again
		for (int i = 0; i < nTiles; i++) {
			RxiTile *t = tiles + i;
			COLOR32 *px = t->rgb;
			int best = 0;
			double bestError = 1e32;

			//compute histogram for the tile
			RxHistClear(reduction);
			RxHistAdd(reduction, px, 8, 8);
			RxHistFinalize(reduction);

			//determine which palette is best for this tile for remap
			for (int j = 0; j < nPalettes; j++) {
				double error = RxHistComputePaletteErrorYiq(reduction, yiqPalette + (j * 16), nColsPerPalette, bestError);
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
			for (int j = 0; j < nTiles; j++) {
				if (bestPalettes[j] != i) continue;
				RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
			RxHistFinalize(reduction);
			RxComputePalette(reduction);

			//write back
			for (int j = 0; j < nColsPerPalette; j++) {
				uint8_t *rgb = &reduction->paletteRgb[j][0];
				palettes[j + i * 16] = ColorRoundToDS15(rgb[0] | (rgb[1] << 8) | (rgb[2] << 16));
			}
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
			for (int j = 0; j < nTiles; j++) {
				if (bestPalettes[j] != i) continue;
				RxHistAdd(reduction, tiles[j].rgb, 8, 8);
			}
			RxHistFinalize(reduction);
			RxComputePalette(reduction);

			//write and sort
			COLOR32 *thisPalDest = dest + paletteSize * (i + paletteBase) + outputOffs;
			for (int j = 0; j < nFinalColsPerPalette; j++) {
				uint8_t *rgb = &reduction->paletteRgb[j][0];
				thisPalDest[j] = ColorRoundToDS15(rgb[0] | (rgb[1] << 8) | (rgb[2] << 16));
			}
			qsort(thisPalDest, nFinalColsPerPalette, sizeof(COLOR32), RxColorLightnessComparator);
		} else {
			//already the correct size; simply sort and copy
			qsort(palettes + i * 16, nColsPerPalette, 4, RxColorLightnessComparator);
			memcpy(dest + paletteSize * (i + paletteBase) + outputOffs, palettes + i * 16, nColsPerPalette * sizeof(COLOR32));
		}

		if (paletteOffset == 0) dest[(i + paletteBase) * paletteSize] = 0xFF00FF;
	}

	free(bestPalettes);
	RxDestroy(reduction);
	free(reduction);
	free(tiles);
	free(diffBuff);
}

int RxCreatePaletteEx(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors, int balance, int colorBalance, int enhanceColors, int sortOnlyUsed) {
	RxReduction *reduction = (RxReduction *) calloc(sizeof(RxReduction), 1);

	RxInit(reduction, balance, colorBalance, 15, enhanceColors, nColors);
	RxHistAdd(reduction, img, width, height);
	RxHistFinalize(reduction);
	RxComputePalette(reduction);

	for (unsigned int i = 0; i < nColors; i++) {
		uint8_t r = reduction->paletteRgb[i][0];
		uint8_t g = reduction->paletteRgb[i][1];
		uint8_t b = reduction->paletteRgb[i][2];
		pal[i] = r | (g << 8) | (b << 16);
	}
	RxDestroy(reduction);

	int nProduced = reduction->nUsedColors;
	free(reduction);

	if (sortOnlyUsed) {
		qsort(pal, nProduced, sizeof(COLOR32), RxColorLightnessComparator);
	} else {
		qsort(pal, nColors, sizeof(COLOR32), RxColorLightnessComparator);
	}

	return nProduced;
}

static int RxiDiffuseCurveY(int x) {
	if (x < 0) return -RxiDiffuseCurveY(-x);
	if (x <= 8) return x;
	return (int) (8.5f + pow(x - 8, 0.9) * 0.94140625);
}

static int RxiDiffuseCurveI(int x) {
	if (x < 0) return -RxiDiffuseCurveI(-x);
	if (x <= 8) return x;
	return (int) (8.5f + pow(x - 8, 0.85) * 0.98828125);
}

static int RxiDiffuseCurveQ(int x) {
	if (x < 0) return -RxiDiffuseCurveQ(-x);
	if (x <= 8) return x;
	return (int) (8.5f + pow(x - 8, 0.85) * 0.89453125);
}

int RxPaletteFindCloestColorYiq(RxReduction *reduction, RxYiqColor *color, RxYiqColor *palette, int nColors) {
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	double minDistance = 1e32;
	int minIndex = 0;
	for (int i = 0; i < nColors; i++) {
		RxYiqColor *yiq = palette + i;

		double dy = reduction->lumaTable[yiq->y] - reduction->lumaTable[color->y];
		double di = yiq->i - color->i;
		double dq = yiq->q - color->q;
		double dst = dy * dy * yw2 + di * di * iw2 + dq * dq * qw2;
		if (dst < minDistance) {
			minDistance = dst;
			minIndex = i;
			if (minDistance == 0.0) return i;
		}
	}

	return minIndex;
}

void RxReduceImage(COLOR32 *img, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse) {
	RxReduceImageEx(img, NULL, width, height, palette, nColors, touchAlpha, binaryAlpha, c0xp, diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

void RxReduceImageEx(COLOR32 *img, int *indices, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse, int balance, int colorBalance, int enhanceColors) {
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, 15, enhanceColors, nColors);

	//convert palette to YIQ
	RxYiqColor *yiqPalette = (RxYiqColor *) calloc(nColors, sizeof(RxYiqColor));
	for (int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(palette[i], yiqPalette + i);
	}

	//allocate row buffers for color and diffuse.
	RxYiqColor *thisRow = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));
	RxYiqColor *lastRow = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));
	RxYiqColor *thisDiffuse = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));
	RxYiqColor *nextDiffuse = (RxYiqColor *) calloc(width + 2, sizeof(RxYiqColor));

	//fill the last row with the first row, just to make sure we don't run out of bounds
	for (int i = 0; i < width; i++) {
		RxConvertRgbToYiq(img[i], lastRow + (i + 1));
	}
	memcpy(lastRow, lastRow + 1, sizeof(RxYiqColor));
	memcpy(lastRow + (width + 1), lastRow + width, sizeof(RxYiqColor));

	//start dithering, do so in a serpentine path.
	for (int y = 0; y < height; y++) {

		//which direction?
		int hDirection = (y & 1) ? -1 : 1;
		COLOR32 *rgbRow = img + y * width;
		for (int x = 0; x < width; x++) {
			RxConvertRgbToYiq(rgbRow[x], thisRow + (x + 1));
		}
		memcpy(thisRow, thisRow + 1, sizeof(RxYiqColor));
		memcpy(thisRow + (width + 1), thisRow + width, sizeof(RxYiqColor));

		//scan across
		int startPos = (hDirection == 1) ? 0 : (width - 1);
		int x = startPos;
		for (int xPx = 0; xPx < width; xPx++) {
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
			int matched = c0xp + RxPaletteFindCloestColorYiq(reduction, &colorYiq, yiqPalette + c0xp, nColors - c0xp);
			if (colorA == 0 && c0xp) matched = 0;

			//measure distance. From middle color to sampled color, and from palette color to sampled color.
			RxYiqColor *matchedYiq = yiqPalette + matched;
			double paletteDy = reduction->lumaTable[matchedYiq->y] - reduction->lumaTable[colorY];
			int paletteDi = matchedYiq->i - colorI;
			int paletteDq = matchedYiq->q - colorQ;
			double paletteDistance = paletteDy * paletteDy * reduction->yWeight * reduction->yWeight +
				paletteDi * paletteDi * reduction->iWeight * reduction->iWeight +
				paletteDq * paletteDq * reduction->qWeight * reduction->qWeight;

			//now measure distance from the actual color to its average surroundings
			RxYiqColor centerYiq;
			memcpy(&centerYiq, thisRow + (x + 1), sizeof(RxYiqColor));

			double centerDy = reduction->lumaTable[centerYiq.y] - reduction->lumaTable[colorY];
			int centerDi = centerYiq.i - colorI;
			int centerDq = centerYiq.q - colorQ;
			double centerDistance = centerDy * centerDy * reduction->yWeight * reduction->yWeight +
				centerDi * centerDi * reduction->iWeight * reduction->iWeight +
				centerDq * centerDq * reduction->qWeight * reduction->qWeight;

			//now test: Should we dither?
			double balanceSquare = reduction->yWeight * reduction->yWeight;
			if (centerDistance < 110.0 * balanceSquare && paletteDistance >  2.0 * balanceSquare && diffuse > 0.0f) {
				//Yes, we should dither :)

				int diffuseY = (int) (thisDiffuse[x + 1].y * diffuse / 16); //correct for Floyd-Steinberg coefficients
				int diffuseI = (int) (thisDiffuse[x + 1].i * diffuse / 16);
				int diffuseQ = (int) (thisDiffuse[x + 1].q * diffuse / 16);
				int diffuseA = (int) (thisDiffuse[x + 1].a * diffuse / 16);

				if (!touchAlpha || binaryAlpha) diffuseA = 0; //don't diffuse alpha if no alpha channel, or we're told not to

				colorY += RxiDiffuseCurveY(diffuseY);
				colorI += RxiDiffuseCurveI(diffuseI);
				colorQ += RxiDiffuseCurveQ(diffuseQ);
				colorA += diffuseA;
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
				matched = c0xp + RxPaletteFindCloestColorYiq(reduction, &diffusedYiq, yiqPalette + c0xp, nColors - c0xp);
				if (diffusedYiq.a < 128 && c0xp) matched = 0;
				COLOR32 chosen = (palette[matched] & 0xFFFFFF) | (colorA << 24);
				img[x + y * width] = chosen;
				if (indices != NULL) indices[x + y * width] = matched;

				RxYiqColor *chosenYiq = yiqPalette + matched;
				int offY = colorY - chosenYiq->y;
				int offI = colorI - chosenYiq->i;
				int offQ = colorQ - chosenYiq->q;
				int offA = colorA - chosenYiq->a;

				//now diffuse to neighbors
				RxYiqColor *diffNextPixel = thisDiffuse + (x + 1 + hDirection);
				RxYiqColor *diffDownPixel = nextDiffuse + (x + 1);
				RxYiqColor *diffNextDownPixel = nextDiffuse + (x + 1 + hDirection);
				RxYiqColor *diffBackDownPixel = nextDiffuse + (x + 1 - hDirection);

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

				matched = c0xp + RxPaletteFindCloestColorYiq(reduction, &centerYiq, yiqPalette + c0xp, nColors - c0xp);
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

double RxComputePaletteError(RxReduction *reduction, COLOR32 *px, int nPx, COLOR32 *pal, int nColors, int alphaThreshold, double nMaxError) {
	if (nMaxError == 0) nMaxError = 1e32;
	double error = 0;

	RxYiqColor paletteYiqStack[16]; //small palettes
	RxYiqColor *paletteYiq = paletteYiqStack;
	if (nColors > 16) {
		paletteYiq = (RxYiqColor *) calloc(nColors, sizeof(RxYiqColor));
	}

	//palette to YIQ
	for (int i = 0; i < nColors; i++) {
		RxConvertRgbToYiq(pal[i], paletteYiq + i);
	}

	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;
	for (int i = 0; i < nPx; i++) {
		COLOR32 p = px[i];
		int a = (p >> 24) & 0xFF;
		if (a < alphaThreshold) continue;

		RxYiqColor yiq;
		RxConvertRgbToYiq(px[i], &yiq);
		int best = RxPaletteFindCloestColorYiq(reduction, &yiq, paletteYiq, nColors);
		RxYiqColor *chosen = paletteYiq + best;

		double dy = reduction->lumaTable[yiq.y] - reduction->lumaTable[chosen->y];
		double di = yiq.i - chosen->i;
		double dq = yiq.q - chosen->q;

		error += dy * dy * yw2;
		if (error >= nMaxError) {
			if (paletteYiq != paletteYiqStack) free(paletteYiq);
			return nMaxError;
		}
		error += di * di * iw2 + dq * dq * qw2;
		if (error >= nMaxError) {
			if (paletteYiq != paletteYiqStack) free(paletteYiq);
			return nMaxError;
		}
	}

	if (paletteYiq != paletteYiqStack) free(paletteYiq);
	return error;
}
