#include <Windows.h>
#include <math.h>

#include "color.h"
#include "palette.h"
#include "analysis.h"

//histogram linked list entry as secondary sorting
typedef struct HIST_ENTRY_ {
	int y;
	int i;
	int q;
	int a;
	struct HIST_ENTRY_ *next;
	double weight;
	double value;
} HIST_ENTRY;

//structure for a node in the color tree
typedef struct COLOR_NODE_ {
	BOOL isLeaf;
	double weight;
	double priority;
	int y;
	int i;
	int q;
	int a;
	int pivotIndex;
	int startIndex;
	int endIndex;
	struct COLOR_NODE_ *left;
	struct COLOR_NODE_ *right;
} COLOR_NODE;

//allocator for allocating the linked lists
typedef struct ALLOCATOR_ {
	void *allocation;
	int nextEntryOffset;
	struct ALLOCATOR_ *next;
} ALLOCATOR;

//histogram structure
typedef struct HISTOGRAM_ {
	ALLOCATOR allocator;
	HIST_ENTRY *entries[0x20000];
	int nEntries;
} HISTOGRAM;

//reduction workspace structure
typedef struct REDUCTION_ {
	int nPaletteColors;
	int balance;
	int colorBalance;
	int shiftColorBalance;
	BOOL enhanceColors;
	int optimization;
	HISTOGRAM *histogram;
	HIST_ENTRY **histogramFlat;
	COLOR_NODE *colorTreeHead;
	COLOR_NODE *colorBlocks[0x2000];
	BYTE paletteRgb[256][3];
	double lumaTable[512];
	double gamma;
} REDUCTION;

//struct for internal processing of color leaves
typedef struct {
	double y;
	double i;
	double q;
	double a;
	double partialSumWeights;
	double weightedSquares;
	double weight;
	double unused;
} COLOR_INFO; 

void initReduction(REDUCTION *reduction, int balance, int colorBalance, int optimization, BOOL enhanceColors, unsigned int nColors) {
	memset(reduction, 0, sizeof(REDUCTION));
	reduction->balance = 60 - balance;
	reduction->colorBalance = colorBalance;
	reduction->optimization = optimization;
	reduction->shiftColorBalance = 40 - colorBalance;
	reduction->enhanceColors = enhanceColors;
	reduction->nPaletteColors = nColors;
	reduction->gamma = 1.27;

	for (int i = 0; i < 512; i++) {
		reduction->lumaTable[i] = pow((double) i / 511.0, 1.27) * 511.0;
	}
}

void *allocateEntry(ALLOCATOR *allocator, int size) {
	if (allocator->allocation == NULL) {
		allocator->allocation = calloc(0x100000, 1);
		allocator->nextEntryOffset = 0;
	}
	while (allocator->nextEntryOffset + size > 0x100000) {
		if (allocator->next == NULL) {
			ALLOCATOR *next = calloc(1, sizeof(ALLOCATOR));
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

void histogramAddColor(HISTOGRAM *histogram, int y, int i, int q, int a, double weight) {
	int slotIndex = (q + (y * 64 + i) * 4 + 0x60E + a) & 0x1FFFF;

	HIST_ENTRY *slot = histogram->entries[slotIndex];

	//find a slot with the same YIQA, or create a new one if none exists.
	if (slot == NULL) {
		slot = (HIST_ENTRY *) allocateEntry(&histogram->allocator, sizeof(HIST_ENTRY));
		slot->y = y;
		slot->i = i;
		slot->q = q;
		slot->a = a;
		slot->weight = weight;
		slot->next = NULL;
		slot->value = 0.0;
		histogram->entries[slotIndex] = slot;
		histogram->nEntries++;
		return;
	}
	while (1) {
		if (slot->y == y && slot->i == i && slot->q == q && slot->a == a) {
			slot->weight += weight;
			return;
		}

		if (slot->next == NULL) {
			slot->next = (HIST_ENTRY *) allocateEntry(&histogram->allocator, sizeof(HIST_ENTRY));
			slot = slot->next;

			slot->y = y;
			slot->i = i;
			slot->q = q;
			slot->a = a;
			slot->weight = weight;
			slot->next = NULL;
			slot->value = 0.0;
			histogram->nEntries++;
			return;
		}
		slot = slot->next;
	}
}

void encodeColor(DWORD rgb, int *yiq) {
	double doubleR = (double) ((rgb & 0xFF) << 1);
	double doubleG = (double) (((rgb >> 8) & 0xFF) << 1);
	double doubleB = (double) (((rgb >> 16) & 0xFF) << 1);

	double y = (doubleR * 19595 + doubleG * 38470 + doubleB * 7471) * 0.00001526;
	double i = (doubleR * 39059 - doubleG * 17957 - doubleB * 21103) * 0.00001526;
	double q = (doubleR * 13828 - doubleG * 34210 + doubleB * 20382) * 0.00001526;
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
	int iqInt = (int) (iqProd + 0.5);
	int iInt = (int) (i + (i < 0.0 ? -0.5 : 0.5));
	int qInt = (int) (q + (q < 0.0 ? -0.5 : 0.5));

	//clamp variables to good ranges
	iqInt = min(max(iqInt, 0), 511);
	iInt = min(max(iInt, -320), 319);
	qInt = min(max(qInt, -270), 269);

	//write output
	yiq[0] = iqInt;
	yiq[1] = iInt;
	yiq[2] = qInt;
	yiq[3] = 0xFF;
}

void decodeColor(int *rgb, int *out) {
	double i = (double) out[1];
	double q = (double) out[2];
	double y;
	if(i >= 0.0 || q <= 0.0) {
		y = (double) out[0];
	} else {
		y = ((double) out[0]) + (q * i) * 0.00195313;
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

	int r = (int) ((65536 * y + 62629 * i + 40822 * q) * 0.00000763 + 0.5);
	int g = (int) ((65536 * y - 17836 * i - 42489 * q) * 0.00000763 + 0.5);
	int b = (int) ((65536 * y - 72428 * i + 111714 * q) * 0.00000763 + 0.5);

	r = min(max(r, 0), 255);
	g = min(max(g, 0), 255);
	b = min(max(b, 0), 255);

	rgb[0] = r;
	rgb[1] = g;
	rgb[2] = b;
}

void computeHistogram(REDUCTION *reduction, DWORD *img, int width, int height) {
	int iMask = 0xFFFFFFFF, qMask = 0xFFFFFFFF;
	if (reduction->optimization < 5) {
		qMask = 0xFFFFFFFE;
		if (reduction->optimization < 2) {
			iMask = 0xFFFFFFFE;
		}
	}

	reduction->histogram = (HISTOGRAM *) calloc(1, sizeof(HISTOGRAM));

	for (int y = 0; y < height; y++) {
		int yiqLeft[4];
		encodeColor(img[y * width], yiqLeft);
		int yLeft = yiqLeft[0];

		for (int x = 0; x < width; x++) {
			int yiq[4];
			encodeColor(img[x + y * width], yiq);

			int dy = yiq[0] - yLeft;
			double weight = (double) (16 - abs(16 - abs(dy)) / 8);
			if (weight < 1.0) weight = 1.0;

			histogramAddColor(reduction->histogram, yiq[0], yiq[1] & iMask, yiq[2] & qMask, yiq[3], weight);
			yLeft = yiq[0];
		}
	}

	//"flatten" the histogram
	reduction->histogramFlat = (HIST_ENTRY **) calloc(reduction->histogram->nEntries, sizeof(HIST_ENTRY *));
	HIST_ENTRY **pos = reduction->histogramFlat;

	for (int i = 0; i < 0x20000; i++) {
		HIST_ENTRY *entry = reduction->histogram->entries[i];

		while (entry != NULL) {
			*(pos++) = entry;
			entry = entry->next;
		}

	}
}

void freeColorTree(COLOR_NODE *colorBlock, BOOL freeThis) {
	if (colorBlock->left != NULL) {
		freeColorTree(colorBlock->left, TRUE);
		colorBlock->left = NULL;
	}
	if (colorBlock->right != NULL) {
		freeColorTree(colorBlock->right, TRUE);
		colorBlock->right = NULL;
	}
	if (freeThis) {
		free(colorBlock);
	}
}

int getNumberOfTreeLeaves(COLOR_NODE *tree) {
	int count = 0;
	if (tree->left == NULL && tree->right == NULL) return 1; //doesn't necessarily have to be a leaf

	if (tree->left != NULL) {
		count = getNumberOfTreeLeaves(tree->left);
	}
	if (tree->right != NULL) {
		count += getNumberOfTreeLeaves(tree->right);
	}
	return count;
}

void createLeaves(COLOR_NODE *tree, int pivotIndex) {
	if (tree->left == NULL && tree->right == NULL) {
		if (pivotIndex > tree->startIndex && pivotIndex >= tree->endIndex) {
			pivotIndex = tree->endIndex - 1;
		}
		if (pivotIndex <= tree->startIndex) {
			pivotIndex = tree->startIndex + 1;
		}

		if (pivotIndex > tree->startIndex && pivotIndex < tree->endIndex) {
			COLOR_NODE *newNode = (COLOR_NODE *) calloc(1, sizeof(COLOR_NODE));
			
			newNode->a = 0xFF;
			newNode->isLeaf = TRUE;
			newNode->startIndex = tree->startIndex;
			newNode->endIndex = pivotIndex;
			tree->left = newNode;

			newNode = (COLOR_NODE *) calloc(1, sizeof(COLOR_NODE));
			newNode->a = 0xFF;
			newNode->isLeaf = TRUE;
			newNode->startIndex = pivotIndex;
			newNode->endIndex = tree->endIndex;
			tree->right = newNode;
		}
	}
	tree->isLeaf = FALSE;
}

COLOR_NODE *getLeaf(COLOR_NODE *tree) {
	if (tree->left == NULL && tree->right == NULL) {
		if (tree->isLeaf) return tree;
		return NULL;
	}
	COLOR_NODE *leafLeft = NULL, *leafRight = NULL;
	if (tree->left != NULL) {
		leafLeft = getLeaf(tree->left);
	}
	if (tree->right != NULL) {
		leafRight = getLeaf(tree->right);
	}
	
	if (leafLeft != NULL && leafRight == NULL) return leafLeft;
	if (leafRight != NULL && leafLeft == NULL) return leafRight;
	if (leafLeft == NULL && leafRight == NULL) return NULL;

	if (leafRight->priority >= leafLeft->priority) return leafRight;
	return leafLeft;

}

double approximatePrincipalComponent(REDUCTION *reduction, int startIndex, int endIndex, double *axis) {
	double mtx[4][4] = { 0 };
	double averageY = 0.0, averageI = 0.0, averageQ = 0.0, averageA = 0.0;
	double sumWeight = 0.0;

	for (int i = startIndex; i < endIndex; i++) {
		HIST_ENTRY *entry = reduction->histogramFlat[i];

		double scaledA = entry->a * 40;
		double scaledY = reduction->balance * reduction->lumaTable[entry->y];
		double scaledI = reduction->colorBalance * entry->i;
		double scaledQ = reduction->shiftColorBalance * entry->q;
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

int histEntryComparator(const void *p1, const void *p2) {
	HIST_ENTRY *e1 = *(HIST_ENTRY **) p1;
	HIST_ENTRY *e2 = *(HIST_ENTRY **) p2;
	double d = e1->value - e2->value;
	if (d < 0.0) return -1;
	if (d > 0.0) return 1;
	return 0;
}

double lengthSquared(double x, double y, double z) {
	return x * x + y * y + z * z;
}

void setupLeaf(REDUCTION *reduction, COLOR_NODE *colorBlock) {
	//calculate the pivot index, as well as average YIQA values.
	if (colorBlock->endIndex - colorBlock->startIndex < 2) {
		HIST_ENTRY *entry = reduction->histogramFlat[colorBlock->startIndex];
		colorBlock->y = entry->y;
		colorBlock->i = entry->i;
		colorBlock->q = entry->q;
		colorBlock->a = entry->a;
		colorBlock->weight = entry->weight;
		colorBlock->isLeaf = FALSE;
		return;
	}

	double greatestValue = -1e30;
	double leastValue = 1e30;

	double principal[4];
	approximatePrincipalComponent(reduction, colorBlock->startIndex, colorBlock->endIndex, principal);

	int balance = reduction->balance;
	int colorBalance = reduction->colorBalance;
	int shiftColorBalance = reduction->shiftColorBalance;
	int alphaScale = 40;

	for (int i = colorBlock->startIndex; i < colorBlock->endIndex; i++) {
		HIST_ENTRY *histEntry = reduction->histogramFlat[i];
		double value = histEntry->i * colorBalance * principal[1]
			+ histEntry->q * shiftColorBalance * principal[2]
			+ histEntry->a * alphaScale * principal[3]
			+ reduction->lumaTable[histEntry->y] * balance * principal[0];
			
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
	HIST_ENTRY **thisHistogram = reduction->histogramFlat + colorBlock->startIndex;
	qsort(thisHistogram, nColors, sizeof(HIST_ENTRY *), histEntryComparator);

	//fill out color information in colorInfo
	for (int i = 0; i < nColors; i++) {
		HIST_ENTRY *entry = thisHistogram[i];
		double weight = entry->weight;
		double cy = reduction->balance * reduction->lumaTable[entry->y];
		double ci = reduction->colorBalance * entry->i;
		double cq = reduction->shiftColorBalance * entry->q;

		colorInfo[i].y = weight * cy;
		colorInfo[i].i = weight * ci;
		colorInfo[i].q = weight * cq;
		colorInfo[i].a = weight * 40 * entry->a;
		colorInfo[i].weightedSquares = weight * lengthSquared(cy, ci, cq);
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
	avgY = pow((avgY / (double) reduction->balance) * 0.00195695, 1.0 / reduction->gamma);

	//compute average color
	int initA = (int) ((totalA / totalWeight) / 40.0 + 0.5);
	int initQ = (int) ((totalQ / totalWeight) / reduction->shiftColorBalance + 0.5);
	int initI = (int) ((totalI / totalWeight) / reduction->colorBalance + 0.5);
	int initY = (int) (avgY * 511.0 + 0.5);

	colorBlock->y = initY;
	colorBlock->i = initI;
	colorBlock->q = initQ;
	colorBlock->a = initA;
	colorBlock->weight = totalWeight;

	double adjustedWeight = 1.0;
	if (!reduction->enhanceColors) {
		adjustedWeight = sqrt(totalWeight);
	}

	//determine pivot index
	int pivotIndex = 0;
	double leastThing = 1e30;
	for (int i = 0; i < nColors; i++) {
		COLOR_INFO *entry = colorInfo + i;
		if (entry->weight > 0.0) {
			double weightAfter = totalWeight - entry->partialSumWeights;
			if (weightAfter <= 0.0) {
				weightAfter = 0.0001;
			}
			double length = lengthSquared(entry->y, entry->i, entry->q);
			double subLength = lengthSquared(totalY - entry->y, totalI - entry->i, totalQ - entry->q);
			double thing = sumWeightedSquares - length / entry->partialSumWeights - subLength / weightAfter;
			
			if (thing <= leastThing) {
				leastThing = thing;
				pivotIndex = i + 1;
			}
		}
	}

	//double check pivot index
	if (pivotIndex == 0) pivotIndex = 1;
	else if (pivotIndex >= nColors) pivotIndex = nColors - 1;
	pivotIndex = pivotIndex + colorBlock->startIndex;
	colorBlock->pivotIndex = pivotIndex;

	double length = lengthSquared(totalY / totalWeight, totalI / totalWeight, totalQ / totalWeight);
	colorBlock->priority = (sumWeightedSquares - length * totalWeight - leastThing) / adjustedWeight;
	free(colorInfo);
}

DWORD maskColor(DWORD color) {
	return ColorConvertFromDS(ColorConvertToDS(color & 0xFFFFFF));
}

void optimizePalette(REDUCTION *reduction) {
	//do it
	COLOR_NODE *treeHead = (COLOR_NODE *) calloc(1, sizeof(COLOR_NODE));
	treeHead->isLeaf = TRUE;
	treeHead->a = 0xFF;
	treeHead->endIndex = reduction->histogram->nEntries;

	reduction->colorTreeHead = treeHead;
	setupLeaf(reduction, treeHead);

	int numberOfTreeElements = 0;
	if (treeHead->left == NULL && treeHead->right == NULL) {
		numberOfTreeElements = 1;
	} else {
		if (treeHead->left != NULL) {
			numberOfTreeElements = getNumberOfTreeLeaves(treeHead->left);
		}
		if (treeHead->right != NULL) {
			numberOfTreeElements = getNumberOfTreeLeaves(treeHead->right);
		}
	}

	if (numberOfTreeElements < reduction->nPaletteColors) {
		COLOR_NODE *colorBlock;
		while ((colorBlock = getLeaf(treeHead)) != NULL) {
			createLeaves(colorBlock, colorBlock->pivotIndex);

			COLOR_NODE *leftBlock = colorBlock->left, *rightBlock = colorBlock->right;
			if (leftBlock != NULL) {
				setupLeaf(reduction, leftBlock);

				//destroy this node?
				if (leftBlock->weight < 1.0) {
					if (leftBlock->left != NULL) {
						freeColorTree(leftBlock->left, TRUE);
						leftBlock->left = NULL;
					}
					if (leftBlock->right != NULL) {
						freeColorTree(leftBlock->right, TRUE);
						leftBlock->right = NULL;
					}
					colorBlock->left = NULL;
				}
			}

			if (rightBlock != NULL) {
				setupLeaf(reduction, rightBlock);

				//destroy this node?
				if (rightBlock->weight < 1.0) {
					if (rightBlock->left != NULL) {
						freeColorTree(rightBlock->left, TRUE);
						rightBlock->left = NULL;
					}
					if (rightBlock->right != NULL) {
						freeColorTree(rightBlock->right, TRUE);
						rightBlock->right = NULL;
					}
					colorBlock->right = NULL;
				}
			}
			//it is possible to end up with a branch with no leaves, if they all die :(

			if (colorBlock->left != NULL && colorBlock->right != NULL) {
				int decodedLeft[4], decodedRight[4];
				decodeColor(decodedLeft, &colorBlock->left->y);
				decodeColor(decodedRight, &colorBlock->right->y);
				int leftAlpha = colorBlock->left->a;
				int rightAlpha = colorBlock->right->a;
				DWORD leftRgb = decodedLeft[0] | (decodedLeft[1] << 8) | (decodedLeft[2] << 16);
				DWORD rightRgb = decodedRight[0] | (decodedRight[1] << 8) | (decodedRight[2] << 16);
				DWORD maskedLeft = maskColor(leftRgb), maskedRight = maskColor(rightRgb);

				if (maskedLeft == maskedRight && leftAlpha == rightAlpha) {
					leftBlock = colorBlock->left, rightBlock = colorBlock->right;

					//prune left
					if (leftBlock != NULL) {
						if (leftBlock->left != NULL) {
							freeColorTree(leftBlock->left, TRUE);
							leftBlock->left = NULL;
						}
						if (leftBlock->right != NULL) {
							freeColorTree(leftBlock->right, TRUE);
							leftBlock->right = NULL;
						}
						free(leftBlock);
						leftBlock = NULL;
					}
					colorBlock->left = NULL;

					//prune right
					if (rightBlock != NULL) {
						if (rightBlock->left != NULL) {
							freeColorTree(rightBlock->left, TRUE);
							rightBlock->left = NULL;
						}
						if (rightBlock->right != NULL) {
							freeColorTree(rightBlock->right, TRUE);
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
					numberOfTreeElements += getNumberOfTreeLeaves(treeHead->left);
				}
				if (treeHead->right != NULL) {
					numberOfTreeElements += getNumberOfTreeLeaves(treeHead->right);
				}
			}

			if (numberOfTreeElements >= reduction->nPaletteColors) break;
		}
	}
}

COLOR_NODE **addColorBlocks(COLOR_NODE *colorBlock, COLOR_NODE **colorBlockList) {
	if (colorBlock->left == NULL && colorBlock->right == NULL) {
		*colorBlockList = colorBlock;
		return colorBlockList + 1;
	}
	if(colorBlock->left != NULL) {
		colorBlockList = addColorBlocks(colorBlock->left, colorBlockList);
	}
	if (colorBlock->right != NULL) {
		colorBlockList = addColorBlocks(colorBlock->right, colorBlockList);
	}
	return colorBlockList;
}

void paletteToArray(REDUCTION *reduction) {
	if (reduction->colorTreeHead == NULL) return;

	//flatten
	COLOR_NODE **colorBlockPtr = reduction->colorBlocks;
	memset(colorBlockPtr, 0, sizeof(reduction->colorBlocks));
	addColorBlocks(reduction->colorTreeHead, colorBlockPtr);

	//convert to RGB
	int ofs = 0;
	for (int i = 0; i < reduction->nPaletteColors; i++) {
		if (colorBlockPtr[i] != NULL) {
			COLOR_NODE *block = colorBlockPtr[i];
			int y = block->y, i = block->i, q = block->q;
			int yiq[] = { y, i, q, 0xFF };
			int rgb[4];
			decodeColor(rgb, yiq);
			
			reduction->paletteRgb[ofs][0] = rgb[0];
			reduction->paletteRgb[ofs][1] = rgb[1];
			reduction->paletteRgb[ofs][2] = rgb[2];
			ofs++;
		}
	}
}

void freeAllocations(ALLOCATOR *allocator) {
	if (allocator->allocation != NULL) free(allocator->allocation);
	allocator->allocation = NULL;
	if (allocator->next != NULL) {
		freeAllocations(allocator->next);
		allocator->next = NULL;
	}
}

void destroyReduction(REDUCTION *reduction) {
	if(reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	if (reduction->histogram != NULL) {
		freeAllocations(&reduction->histogram->allocator);
		free(reduction->histogram);
	}
	if(reduction->colorBlocks != NULL) freeColorTree(reduction->colorTreeHead, FALSE);
	free(reduction->colorTreeHead);
}

extern int lightnessCompare(const void *d1, const void *d2);

int createPaletteSlow(DWORD *img, int width, int height, DWORD *pal, unsigned int nColors) {
	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, 20, 20, 15, FALSE, nColors);
	computeHistogram(reduction, img, width, height);
	optimizePalette(reduction);
	paletteToArray(reduction);
	
	for (unsigned int i = 0; i < nColors; i++) {
		BYTE r = reduction->paletteRgb[i][0];
		BYTE g = reduction->paletteRgb[i][1];
		BYTE b = reduction->paletteRgb[i][2];
		pal[i] = r | (g << 8) | (b << 16);
	}

	destroyReduction(reduction);
	free(reduction);
	qsort(pal, nColors, 4, lightnessCompare);
	return 0;
}