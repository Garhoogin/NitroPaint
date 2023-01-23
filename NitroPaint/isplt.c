#include <math.h>
#include <stdint.h>

#include "color.h"
#include "palette.h"
#include "analysis.h"

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

void initReduction(REDUCTION *reduction, int balance, int colorBalance, int optimization, int enhanceColors, unsigned int nColors) {
	memset(reduction, 0, sizeof(REDUCTION));
	reduction->yWeight = 60 - balance;
	reduction->iWeight = colorBalance;
	reduction->optimization = optimization;
	reduction->qWeight = 40 - colorBalance;
	reduction->enhanceColors = enhanceColors;
	reduction->nReclusters = nColors <= 32 ? RECLUSTER_DEFAULT : 0;
	reduction->nPaletteColors = nColors;
	reduction->gamma = 1.27;
	reduction->maskColors = TRUE;

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
	if (a == 0) return;
	int slotIndex = (q + (y * 64 + i) * 4 + 0x60E + a) & 0x1FFFF;
	if (slotIndex < histogram->firstSlot) histogram->firstSlot = slotIndex;

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

void rgbToYiq(COLOR32 rgb, int *yiq) {
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
	yiq[0] = yInt;
	yiq[1] = iInt;
	yiq[2] = qInt;
	yiq[3] = (rgb >> 24) & 0xFF;
}

void yiqToRgb(int *rgb, int *yiq) {
	double i = (double) yiq[1];
	double q = (double) yiq[2];
	double y;
	if(i >= 0.0 || q <= 0.0) {
		y = (double) yiq[0];
	} else {
		y = ((double) yiq[0]) + (q * i) * 0.00195313;
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

	rgb[0] = r;
	rgb[1] = g;
	rgb[2] = b;
}

void flattenHistogram(REDUCTION *reduction) {
	if (reduction->histogramFlat != NULL) free(reduction->histogram);

	reduction->histogramFlat = (HIST_ENTRY **) calloc(reduction->histogram->nEntries, sizeof(HIST_ENTRY *));
	HIST_ENTRY **pos = reduction->histogramFlat;

	for (int i = reduction->histogram->firstSlot; i < 0x20000; i++) {
		HIST_ENTRY *entry = reduction->histogram->entries[i];

		while (entry != NULL) {
			*(pos++) = entry;
			entry = entry->next;
		}
	}
}

void computeHistogram(REDUCTION *reduction, COLOR32 *img, int width, int height) {
	int iMask = 0xFFFFFFFF, qMask = 0xFFFFFFFF;
	if (reduction->optimization < 5) {
		qMask = 0xFFFFFFFE;
		if (reduction->optimization < 2) {
			iMask = 0xFFFFFFFE;
		}
	}

	if (reduction->histogram == NULL) {
		reduction->histogram = (HISTOGRAM *) calloc(1, sizeof(HISTOGRAM));
		reduction->histogram->firstSlot = 0x20000;
	}

	for (int y = 0; y < height; y++) {
		int yiqLeft[4];
		rgbToYiq(img[y * width], yiqLeft);
		int yLeft = yiqLeft[0];

		for (int x = 0; x < width; x++) {
			int yiq[4];
			rgbToYiq(img[x + y * width], yiq);

			int dy = yiq[0] - yLeft;
			double weight = (double) (16 - abs(16 - abs(dy)) / 8);
			if (weight < 1.0) weight = 1.0;

			histogramAddColor(reduction->histogram, yiq[0], yiq[1] & iMask, yiq[2] & qMask, yiq[3], weight);
			yLeft = yiq[0];
		}
	}
}

void freeColorTree(COLOR_NODE *colorBlock, int freeThis) {
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
		double scaledY = reduction->yWeight * reduction->lumaTable[entry->y];
		double scaledI = reduction->iWeight * entry->i;
		double scaledQ = reduction->qWeight * entry->q;
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

double __inline lengthSquared(double x, double y, double z) {
	return x * x + y * y + z * z;
}

void sortHistogram(REDUCTION *reduction, int startIndex, int endIndex) {
	double principal[4];
	int nColors = endIndex - startIndex;
	HIST_ENTRY **thisHistogram = reduction->histogramFlat + startIndex;
	approximatePrincipalComponent(reduction, startIndex, endIndex, principal);

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
		HIST_ENTRY *histEntry = reduction->histogramFlat[i];
		double value = reduction->lumaTable[histEntry->y] * yWeight
			+ histEntry->i * iWeight
			+ histEntry->q * qWeight
			+ histEntry->a * aWeight;

		histEntry->value = value;
	}

	//sort colors by dot product with the vector
	qsort(thisHistogram, nColors, sizeof(HIST_ENTRY *), histEntryComparator);
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

	double yWeight = principal[0] * reduction->yWeight;
	double iWeight = principal[1] * reduction->iWeight;
	double qWeight = principal[2] * reduction->qWeight;
	double aWeight = principal[3] * 40.0;

	for (int i = colorBlock->startIndex; i < colorBlock->endIndex; i++) {
		HIST_ENTRY *histEntry = reduction->histogramFlat[i];
		double value = reduction->lumaTable[histEntry->y] * yWeight
			+ histEntry->i * iWeight
			+ histEntry->q * qWeight
			+ histEntry->a * aWeight;
			
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
		double cy = reduction->yWeight * reduction->lumaTable[entry->y];
		double ci = reduction->iWeight * entry->i;
		double cq = reduction->qWeight * entry->q;

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
	avgY = pow((avgY / (double) reduction->yWeight) * 0.00195695, 1.0 / reduction->gamma);

	//compute average color
	int initY = (int) (avgY * 511.0 + 0.5);
	int initI = (int) ((totalI / totalWeight) / reduction->iWeight + 0.5);
	int initQ = (int) ((totalQ / totalWeight) / reduction->qWeight + 0.5);
	int initA = (int) ((totalA / totalWeight) / 40.0 + 0.5);

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
	double leastVariance = 1e30;
	for (int i = 0; i < nColors; i++) {
		COLOR_INFO *entry = colorInfo + i;
		if (entry->weight > 0.0) {
			double weightAfter = totalWeight - entry->partialSumWeights;
			if (weightAfter <= 0.0) {
				weightAfter = 0.0001;
			}
			double averageLeftSquared = lengthSquared(entry->y, entry->i, entry->q) / entry->partialSumWeights;
			double averageRightSquared = lengthSquared(totalY - entry->y, totalI - entry->i, totalQ - entry->q) / weightAfter;
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

	double averageSquares = lengthSquared(totalY, totalI, totalQ) / totalWeight;
	colorBlock->priority = (sumWeightedSquares - averageSquares - leastVariance) / adjustedWeight;
	free(colorInfo);
}

double addPriorities(COLOR_NODE *n1, COLOR_NODE *n2, REDUCTION *reduction) {
	double p1 = n1->priority, p2 = n2->priority;
	double adjustedWeight = 1.0;
	if (!reduction->enhanceColors) {
		p1 *= sqrt(n1->weight);
		p2 *= sqrt(n2->weight);
		adjustedWeight = sqrt(n1->weight + n2->weight);
	}
	return (p1 + p2) / adjustedWeight;
}

COLOR_NODE *findNodeByColor(COLOR_NODE *treeHead, COLOR_NODE *src, int maskColors) {
	if (treeHead == NULL || treeHead == src) return NULL;

	if (treeHead->left != NULL || treeHead->right != NULL) {
		COLOR_NODE *foundLeft = findNodeByColor(treeHead->left, src, maskColors);
		if (foundLeft != NULL) return foundLeft;

		COLOR_NODE *foundRight = findNodeByColor(treeHead->right, src, maskColors);
		return foundRight;
	}

	//is leaf, does this match?
	int rgb[4];
	int yiq[] = { src->y, src->i, src->q, 0 };
	yiqToRgb(rgb, yiq);
	COLOR32 compare = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
	if (maskColors) compare = ColorRoundToDS15(compare);

	yiq[0] = treeHead->y;
	yiq[1] = treeHead->i;
	yiq[2] = treeHead->q;
	yiqToRgb(rgb, yiq);
	COLOR32 thisRgb = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
	if (maskColors) thisRgb = ColorRoundToDS15(thisRgb);

	return (compare == thisRgb) ? treeHead : NULL;
}

COLOR_NODE *findNodeByChild(COLOR_NODE *treeHead, COLOR_NODE *child) {
	if (treeHead == NULL) return NULL;
	if (treeHead->left == child || treeHead->right == child) return treeHead;

	COLOR_NODE *foundLeft = findNodeByChild(treeHead->left, child);
	if (foundLeft != NULL) return foundLeft;

	return findNodeByChild(treeHead->right, child);
}

COLOR_NODE *findNodeByIndex(COLOR_NODE *treeHead, int index) {
	if (treeHead == NULL) return NULL;
	if (treeHead->left == NULL && treeHead->right == NULL) return treeHead;
	if (treeHead->left == NULL) return findNodeByIndex(treeHead->right, index);
	if (treeHead->right == NULL) return findNodeByIndex(treeHead->left, index);

	//count nodes left. If greater than index, search left, else search right
	int nodesLeft = getNumberOfTreeLeaves(treeHead->left);
	if (nodesLeft > index) return findNodeByIndex(treeHead->left, index);
	return findNodeByIndex(treeHead->right, index - nodesLeft);
}

void cleanEmptyNode(COLOR_NODE *treeHead, COLOR_NODE *node) {
	//trace up the tree clearing out empty non-leaf nodes
	COLOR_NODE *parent = findNodeByChild(treeHead, node);
	if (parent != NULL) {
		if (parent->left == node) parent->left = NULL;
		if (parent->right == node) parent->right = NULL;
		if (parent->left == NULL && parent->right == NULL) cleanEmptyNode(treeHead, parent);
	}
	freeColorTree(node, TRUE);
}

COLOR_NODE **addColorBlocks(COLOR_NODE *colorBlock, COLOR_NODE **colorBlockList) {
	if (colorBlock->left == NULL && colorBlock->right == NULL) {
		*colorBlockList = colorBlock;
		return colorBlockList + 1;
	}
	if (colorBlock->left != NULL) {
		colorBlockList = addColorBlocks(colorBlock->left, colorBlockList);
	}
	if (colorBlock->right != NULL) {
		colorBlockList = addColorBlocks(colorBlock->right, colorBlockList);
	}
	return colorBlockList;
}

void paletteToArray(REDUCTION *reduction) {
	if (reduction->colorTreeHead == NULL) return;

	//convert to RGB
	int ofs = 0;
	COLOR_NODE **colorBlockPtr = reduction->colorBlocks;
	for (int i = 0; i < reduction->nPaletteColors; i++) {
		if (colorBlockPtr[i] != NULL) {
			COLOR_NODE *block = colorBlockPtr[i];
			int y = block->y, i = block->i, q = block->q, a = block->a;
			int yiq[] = { y, i, q, a };
			int rgb[4];
			yiqToRgb(rgb, yiq);

			COLOR32 rgb32 = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
			if (reduction->maskColors) rgb32 = ColorRoundToDS15(rgb32);

			//write RGB
			reduction->paletteRgb[ofs][0] = rgb32 & 0xFF;
			reduction->paletteRgb[ofs][1] = (rgb32 >> 8) & 0xFF;
			reduction->paletteRgb[ofs][2] = (rgb32 >> 16) & 0xFF;

			//write YIQ (with any loss of information to RGB)
			rgbToYiq(rgb32, &reduction->paletteYiq[ofs][0]);
			ofs++;
		}
	}
}

void iterateRecluster(REDUCTION *reduction) {
	//simple termination conditions
	int nIterations = reduction->nReclusters;
	if (nIterations <= 0) return;
	if (reduction->nUsedColors < reduction->nPaletteColors) return;
	if (reduction->nUsedColors >= reduction->histogram->nEntries) return;

	int nHistEntries = reduction->histogram->nEntries;
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	//iterate up to n times
	for (int k = 0; k < nIterations; k++) {
		//reset block totals
		TOTAL_BUFFER *totalsBuffer = reduction->blockTotals;
		memset(totalsBuffer, 0, sizeof(reduction->blockTotals));

		//voronoi iteration
		for (int i = 0; i < nHistEntries; i++) {
			HIST_ENTRY *entry = reduction->histogramFlat[i];
			double weight = entry->weight;
			int hy = entry->y, hi = entry->i, hq = entry->q, ha = entry->a;

			double bestDistance = 1e30;
			int bestIndex = 0;
			for (int j = 0; j < reduction->nUsedColors; j++) {
				int *pyiq = &reduction->paletteYiq[j][0];

				double dy = hy - pyiq[0];
				double di = hi - pyiq[1];
				double dq = hq - pyiq[2];
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
		}

		//quick sanity check of bucket weights (if any are 0, terminate)
		for (int i = 0; i < reduction->nUsedColors; i++) {
			if (totalsBuffer[i].weight <= 0.0) return;
		}

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
			int rgb[4];
			int yiq[] = { iy, ii, iq, ia };
			yiqToRgb(rgb, yiq);
			COLOR32 as32 = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
			if (reduction->maskColors) as32 = ColorRoundToDS15(as32);

			reduction->paletteRgb[i][0] = as32 & 0xFF;
			reduction->paletteRgb[i][1] = (as32 >> 8) & 0xFF;
			reduction->paletteRgb[i][2] = (as32 >> 16) & 0xFF;
			rgbToYiq(as32, &reduction->paletteYiq[i][0]);
		}

		//TODO: check collisions (rounding to 15-bit makes this possible)
	}
}

void optimizePalette(REDUCTION *reduction) {
	//do it
	COLOR_NODE *treeHead = (COLOR_NODE *) calloc(1, sizeof(COLOR_NODE));
	treeHead->isLeaf = TRUE;
	treeHead->a = 0xFF;
	treeHead->endIndex = reduction->histogram->nEntries;

	reduction->colorTreeHead = treeHead;
	if (reduction->histogram->nEntries == 0) {
		reduction->nUsedColors = 0;
		return;
	}
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

	reduction->nUsedColors = 1;
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
				yiqToRgb(decodedLeft, &colorBlock->left->y);
				yiqToRgb(decodedRight, &colorBlock->right->y);
				int leftAlpha = colorBlock->left->a;
				int rightAlpha = colorBlock->right->a;
				COLOR32 leftRgb = decodedLeft[0] | (decodedLeft[1] << 8) | (decodedLeft[2] << 16);
				COLOR32 rightRgb = decodedRight[0] | (decodedRight[1] << 8) | (decodedRight[2] << 16);
				if (reduction->maskColors && numberOfTreeElements > 2) { //don't prune too quickly
					leftRgb = ColorRoundToDS15(leftRgb);
					rightRgb = ColorRoundToDS15(rightRgb);
				}

				if (leftRgb == rightRgb && leftAlpha == rightAlpha) {
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

			if (numberOfTreeElements >= reduction->nPaletteColors) {

				//duplicate color test
				int prunedNode = 0;
				for (int i = 0; i < numberOfTreeElements; i++) {
					COLOR_NODE *node = findNodeByIndex(treeHead, i);
					COLOR_NODE *dup = findNodeByColor(treeHead, node, reduction->maskColors);
					if (dup != NULL) {
						double p1 = node->priority, p2 = dup->priority;
						double total = addPriorities(node, dup, reduction);
						COLOR_NODE *toDelete = dup, *toKeep = node, *parent = NULL;

						//find which node should keep, which to remove using priority
						if (p1 < p2) {
							toDelete = node;
							toKeep = dup;
						}
						parent = findNodeByChild(treeHead, toDelete);
						toKeep->priority = total;

						//remove node from existence
						freeColorTree(toDelete, TRUE);
						if (parent->left == toDelete) parent->left = NULL;
						if (parent->right == toDelete) parent->right = NULL;
						if (parent->right == NULL && parent->left == NULL) cleanEmptyNode(treeHead, parent);
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
	COLOR_NODE **colorBlockPtr = reduction->colorBlocks;
	memset(colorBlockPtr, 0, sizeof(reduction->colorBlocks));
	addColorBlocks(reduction->colorTreeHead, colorBlockPtr);

	//to array
	paletteToArray(reduction);

	//perform voronoi iteration
	iterateRecluster(reduction);
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
	if(reduction->colorTreeHead != NULL) freeColorTree(reduction->colorTreeHead, FALSE);
	free(reduction->colorTreeHead);
}

void resetHistogram(REDUCTION *reduction) {
	if (reduction->histogramFlat != NULL) free(reduction->histogramFlat);
	reduction->histogramFlat = NULL;
	if (reduction->histogram != NULL) {
		freeAllocations(&reduction->histogram->allocator);
		free(reduction->histogram);
		reduction->histogram = NULL;
	}
	if (reduction->colorTreeHead != NULL) freeColorTree(reduction->colorTreeHead, FALSE);
	free(reduction->colorTreeHead);
	reduction->colorTreeHead = NULL;
	reduction->nUsedColors = 0;
	memset(reduction->paletteRgb, 0, sizeof(reduction->paletteRgb));
}

extern int lightnessCompare(const void *d1, const void *d2);

int createPaletteSlow(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors) {
	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, 20, 20, 15, FALSE, nColors);
	computeHistogram(reduction, img, width, height);
	flattenHistogram(reduction);
	optimizePalette(reduction);
	
	for (unsigned int i = 0; i < nColors; i++) {
		uint8_t r = reduction->paletteRgb[i][0];
		uint8_t g = reduction->paletteRgb[i][1];
		uint8_t b = reduction->paletteRgb[i][2];
		pal[i] = r | (g << 8) | (b << 16);
	}

	destroyReduction(reduction);
	free(reduction);
	qsort(pal, nColors, 4, lightnessCompare);
	return 0;
}

typedef struct {
	COLOR32 rgb[64];
	uint8_t indices[64];
	int palette[16][4]; //YIQ
	int useCounts[16];
	unsigned short palIndex; //points to the index of the tile that is maintaining the palette this tile uses
	unsigned short nUsedColors; //number of filled slots
	unsigned short nSwallowed;
} TILE;

void copyTile(TILE *dest, COLOR32 *pxOrigin, int width) {
	for (int y = 0; y < 8; y++) {
		memcpy(dest->rgb + y * 8, pxOrigin + y * width, 32);
	}
}

int findClosestPaletteColorRGB(int *palette, int nColors, COLOR32 col, int *outDiff) {
	int rgb[4];
	int y, u, v;
	convertRGBToYUV(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, &y, &u, &v);

	int leastDiff = 0x7FFFFFFF;
	int leastIndex = 0;
	for (int i = 0; i < nColors; i++) {
		int y2, u2, v2;
		yiqToRgb(rgb, palette + i * 4);
		convertRGBToYUV(rgb[0], rgb[1], rgb[2], &y2, &u2, &v2);

		int dy = y2 - y, du = u2 - u, dv = v2 - v;
		int diff = dy * dy * 2 + du * du + dv * dv;
		if (diff < leastDiff) {
			leastDiff = diff;
			leastIndex = i;
		}
	}
	if (outDiff != NULL) *outDiff = leastDiff;
	return leastIndex;
}

int findClosestPaletteColor(REDUCTION *reduction, int *palette, int nColors, int *col, int *outDiff) {
	int rgb[4];
	yiqToRgb(rgb, col);
	return findClosestPaletteColorRGB(palette, nColors, rgb[0] | (rgb[1] << 8) | (rgb[2] << 16), outDiff);
}

int computeTilePaletteDifference(REDUCTION *reduction, TILE *tile1, TILE *tile2) {
	//are the palettes identical?
	if (tile1->nUsedColors == tile2->nUsedColors && memcmp(tile1->palette, tile2->palette, tile1->nUsedColors * sizeof(tile1->palette[0])) == 0) return 0;

	//map each color from tile2 to one of tile1
	double totalDiff = 0.0;
	for (int i = 0; i < tile2->nUsedColors; i++) {
		int *yiq = &tile2->palette[i][0];
		int diff = 0;
		int closest = findClosestPaletteColor(reduction, &tile1->palette[0][0], tile1->nUsedColors, yiq, &diff);

		if (diff > 0) {
			totalDiff += sqrt(diff) * tile2->useCounts[i];
		}

	}

	if (totalDiff == 0.0 && tile2->nUsedColors <= tile1->nUsedColors) return 0;
	if (totalDiff == 0.0 || tile2->nUsedColors > tile1->nUsedColors) totalDiff += 1.0;

	if ((tile1->nUsedColors + tile2->nUsedColors) <= reduction->nPaletteColors) {
		if (tile2->nUsedColors <= reduction->nPaletteColors / 2) {
			totalDiff = totalDiff * (double) tile2->nUsedColors / reduction->nPaletteColors;
		}
	} else {
		totalDiff += 2.0 * (tile1->nUsedColors + tile2->nUsedColors - reduction->nPaletteColors);
	}

	totalDiff += 15.0 * sqrt(tile1->nSwallowed * tile2->nSwallowed);
	if (totalDiff >= 0x7FFFFFFF) return 0x7FFFFFFF;
	return (int) totalDiff;
}

int findSimilarTiles(TILE *tiles, int *similarities, int nTiles, int *i1, int *i2) {
	//find a pair of tiles. Both must be representative tiles.

	int leastDiff = 0x7FFFFFFF;
	int best1 = 0, best2 = 1;
	for (int i = 0; i < nTiles; i++) {
		TILE *tile1 = tiles + i;
		if (tile1->palIndex != i) continue;

		for (int j = 0; j < nTiles; j++) {
			TILE *tile2 = tiles + j;

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

void createMultiplePalettes(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							int paletteSize, int nColsPerPalette, int paletteOffset, int *progress) {
	createMultiplePalettesEx(imgBits, tilesX, tilesY, dest, paletteBase, nPalettes, paletteSize, nColsPerPalette, 
							 paletteOffset, BALANCE_DEFAULT, BALANCE_DEFAULT, 0, progress);
}

void createMultiplePalettesEx(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes,
							  int paletteSize, int nColsPerPalette, int paletteOffset, int balance, 
							  int colorBalance, int enhanceColors, int *progress) {
	if (nPalettes == 0) return;
	if (nPalettes == 1) {
		if (paletteOffset) {
			createPaletteSlowEx(imgBits, tilesX * 8, tilesY * 8, dest + (paletteBase * paletteSize) + paletteOffset, paletteSize, balance, colorBalance, enhanceColors, 0);
		} else {
			createPaletteSlowEx(imgBits, tilesX * 8, tilesY * 8, dest + (paletteBase * paletteSize) + paletteOffset + 1, paletteSize - 1, balance, colorBalance, enhanceColors, 0);
			dest[(paletteBase * paletteSize) + paletteOffset] = 0xFF00FF; //transparent fill
		}
		return;
	}

	if (nColsPerPalette >= 16) nColsPerPalette = 15;

	//3 stage algorithm:
	//	1 - split into tiles
	//	2 - map similarities
	//	3 - palette merging

	//------------STAGE 1
	int nTiles = tilesX * tilesY;
	TILE *tiles = (TILE *) calloc(nTiles, sizeof(TILE));
	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, balance, colorBalance, 15, enhanceColors, nColsPerPalette);
	reduction->maskColors = FALSE;
	COLOR32 palBuf[16];
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			TILE *tile = tiles + x + (y * tilesX);
			COLOR32 *pxOrigin = imgBits + x * 8 + (y * 8 * tilesX * 8);
			copyTile(tile, pxOrigin, tilesX * 8);

			//createPaletteSlow(tile->rgb, 8, 8, palBuf + 1, 15);
			resetHistogram(reduction);
			computeHistogram(reduction, tile->rgb, 8, 8);
			flattenHistogram(reduction);
			optimizePalette(reduction);
			for (int i = 0; i < 16; i++) {
				uint8_t *col = &reduction->paletteRgb[i][0];
				palBuf[i] = col[0] | (col[1] << 8) | (col[2] << 16);
			}

			for (int i = 0; i < 16; i++) {
				int yiq[4];
				rgbToYiq(palBuf[i], yiq);
				tile->palette[i][0] = yiq[0];
				tile->palette[i][1] = yiq[1];
				tile->palette[i][2] = yiq[2];
			}
			tile->nUsedColors = reduction->nUsedColors;

			//match pixels to palette indices
			for (int i = 0; i < 64; i++) {
				int index = findClosestPaletteColorRGB(&tile->palette[0][0], tile->nUsedColors, tile->rgb[i], NULL);
				if ((tile->rgb[i] >> 24) == 0) index = 15;
				tile->indices[i] = index;
				tile->useCounts[index]++;
			}
			tile->palIndex = x + y * tilesX;
			tile->nSwallowed = 1;
		}
	}

	//-------------STAGE 2
	int *diffBuff = (int *) calloc(nTiles * nTiles, sizeof(int));
	for (int i = 0; i < nTiles; i++) {
		TILE *tile1 = tiles + i;
		for (int j = 0; j < nTiles; j++) {
			TILE *tile2 = tiles + j;

			//write difference
			if (i == j) diffBuff[i + j * nTiles] = 0;
			else {
				diffBuff[i + j * nTiles] = computeTilePaletteDifference(reduction, tile1, tile2);
			}
		}
		(*progress)++;
	}

	//-----------STAGE 3
	int nCurrentPalettes = nTiles;
	while (nCurrentPalettes > nPalettes) {

		int index1, index2;
		int diff = findSimilarTiles(tiles, diffBuff, nTiles, &index1, &index2);

		//find all  instances of index2, replace with index1
		int nSwitched = 0;
		for (int i = 0; i < nTiles; i++) {
			if (tiles[i].palIndex == index2) {
				tiles[i].palIndex = index1;
				nSwitched++;
			}
		}

		//build new palette
		resetHistogram(reduction);
		for (int i = 0; i < nTiles; i++) {
			if (tiles[i].palIndex == index1) {
				computeHistogram(reduction, tiles[i].rgb, 8, 8);
			}
		}
		flattenHistogram(reduction);
		optimizePalette(reduction);

		//write over the palette of the tile
		TILE *palTile = tiles + index1;
		COLOR32 palBuf[16];
		for (int i = 0; i < 15; i++) {
			int *yiqDest = &palTile->palette[i][0];
			uint8_t *srcRgb = &reduction->paletteRgb[i][0];
			COLOR32 rgb = srcRgb[0] | (srcRgb[1] << 8) | (srcRgb[2] << 16);
			palBuf[i] = rgb;
			rgbToYiq(rgb, yiqDest);
		}
		palTile->nUsedColors = reduction->nUsedColors;
		palTile->nSwallowed += nSwitched;

		//get new use count
		TILE *rep = tiles + index1;
		memset(rep->useCounts, 0, sizeof(rep->useCounts));
		for (int i = 0; i < nTiles; i++) {
			TILE *tile = tiles + i;
			if (tile->palIndex != index1) continue;

			for (int j = 0; j < 64; j++) {
				COLOR32 col = tile->rgb[j];
				int index = findClosestPaletteColorRGB(&tile->palette[0][0], tile->nUsedColors, tile->rgb[j], NULL);
				if ((col >> 24) == 0) index = 15;
				tile->indices[j] = index;
				rep->useCounts[index]++;
			}
		}

		//recompute differences for index1 and representative tiles
		for (int i = 0; i < nTiles; i++) {
			TILE *t = tiles + i;
			if (t->palIndex != i) continue;
			int diff1 = computeTilePaletteDifference(reduction, t, rep);
			int diff2 = computeTilePaletteDifference(reduction, rep, t);
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
		TILE *t = tiles + i;
		if (t->palIndex != i) continue;

		//rebuild palette but with masking enabled
		resetHistogram(reduction);
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].palIndex == t->palIndex) {
				computeHistogram(reduction, tiles[j].rgb, 8, 8);
			}
		}
		flattenHistogram(reduction);
		optimizePalette(reduction);
		
		for (int j = 0; j < 15; j++) {
			uint8_t *rgb = &reduction->paletteRgb[j][0];
			palettes[j + nPalettesWritten * 16] = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
		}
		paletteIndices[nPalettesWritten] = i;
		nPalettesWritten++;
		(*progress)++;
	}

	//palette refinement
	if (0) { //TODO: make this viable
		int nRefinements = 2;
		int *bestPalettes = (int *) calloc(nTiles, sizeof(int));
		for (int k = 0; k < nRefinements; k++) {
			//find best palette for each tile again
			for (int i = 0; i < nTiles; i++) {
				TILE *t = tiles + i;
				COLOR32 *px = t->rgb;
				int best = 0;
				double bestError = 1e32;

				//determine which palette is best for this tile for remap
				for (int j = 0; j < nPalettes; j++) {
					double error = computePaletteErrorYiq(reduction, px, 64, palettes + j * 16, nColsPerPalette, 128, bestError);
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
				resetHistogram(reduction);
				for (int j = 0; j < nTiles; j++) {
					if (bestPalettes[j] != i) continue;
					computeHistogram(reduction, tiles[j].rgb, 8, 8);
				}
				flattenHistogram(reduction);
				optimizePalette(reduction);

				//write back
				for (int j = 0; j < nColsPerPalette; j++) {
					uint8_t *rgb = &reduction->paletteRgb[j][0];
					palettes[j + i * 16] = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
				}
			}
		}
		free(bestPalettes);
	}

	//write palettes
	for (int i = 0; i < nPalettes; i++) {
		qsort(palettes + i * 16, nColsPerPalette, 4, lightnessCompare);
		memcpy(dest + 16 * (i + paletteBase) + outputOffs, palettes + i * 16, nColsPerPalette * sizeof(COLOR32));
		if (paletteOffset == 0) dest[i * 16] = 0xFF00FF;
	}

	destroyReduction(reduction);
	free(reduction);
	free(tiles);
	free(diffBuff);
}

int createPaletteSlowEx(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors, int balance, int colorBalance, int enhanceColors, int sortOnlyUsed) {
	REDUCTION *reduction = (REDUCTION *) calloc(sizeof(REDUCTION), 1);

	initReduction(reduction, balance, colorBalance, 15, enhanceColors, nColors);
	computeHistogram(reduction, img, width, height);
	flattenHistogram(reduction);
	optimizePalette(reduction);

	for (unsigned int i = 0; i < nColors; i++) {
		uint8_t r = reduction->paletteRgb[i][0];
		uint8_t g = reduction->paletteRgb[i][1];
		uint8_t b = reduction->paletteRgb[i][2];
		pal[i] = r | (g << 8) | (b << 16);
	}
	destroyReduction(reduction);

	int nProduced = reduction->nUsedColors;
	free(reduction);

	if (sortOnlyUsed) {
		qsort(pal, nProduced, sizeof(COLOR32), lightnessCompare);
	} else {
		qsort(pal, nColors, sizeof(COLOR32), lightnessCompare);
	}

	return nProduced;
}

int diffuseCurveY(int x) {
	if (x < 0) return -diffuseCurveY(-x);
	if (x <= 8) return x;
	return (int) (8.5f + pow(x - 8, 0.9) * 0.94140625);
}

int diffuseCurveI(int x) {
	if (x < 0) return -diffuseCurveI(-x);
	if (x <= 8) return x;
	return (int) (8.5f + pow(x - 8, 0.85) * 0.98828125);
}

int diffuseCurveQ(int x) {
	if (x < 0) return -diffuseCurveQ(-x);
	if (x <= 8) return x;
	return (int) (8.5f + pow(x - 8, 0.85) * 0.89453125);
}

int closestPaletteYiq(REDUCTION *reduction, int *yiqColor, int *palette, int nColors) {
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	double minDistance = 1e32;
	int minIndex = 0;
	for (int i = 0; i < nColors; i++) {
		int *yiq = palette + i * 4;

		double dy = reduction->lumaTable[yiq[0]] - reduction->lumaTable[yiqColor[0]];
		double di = yiq[1] - yiqColor[1];
		double dq = yiq[2] - yiqColor[2];
		double dst = dy * dy * yw2 + di * di * iw2 + dq * dq * qw2;
		if (dst < minDistance) {
			minDistance = dst;
			minIndex = i;
			if (minDistance == 0.0) return i;
		}
	}

	return minIndex;
}

void ditherImagePalette(COLOR32 *img, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse) {
	ditherImagePaletteEx(img, NULL, width, height, palette, nColors, touchAlpha, binaryAlpha, c0xp, diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

void ditherImagePaletteEx(COLOR32 *img, int *indices, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse, int balance, int colorBalance, int enhanceColors) {
	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, balance, colorBalance, 15, enhanceColors, nColors);

	//convert palette to YIQ
	int *yiqPalette = (int *) calloc(nColors, 4 * sizeof(int));
	for (int i = 0; i < nColors; i++) {
		rgbToYiq(palette[i], yiqPalette + i * 4);
	}

	//allocate row buffers for color and diffuse.
	int *thisRow = (int *) calloc(width + 2, 16);
	int *lastRow = (int *) calloc(width + 2, 16);
	int *thisDiffuse = (int *) calloc(width + 2, 16);
	int *nextDiffuse = (int *) calloc(width + 2, 16);

	//fill the last row with the first row, just to make sure we don't run out of bounds
	for (int i = 0; i < width; i++) {
		rgbToYiq(img[i], lastRow + 4 * (i + 1));
	}
	memcpy(lastRow, lastRow + 4, 16);
	memcpy(lastRow + 4 * (width + 1), lastRow + 4 * width, 16);

	//start dithering, do so in a serpentine path.
	for (int y = 0; y < height; y++) {

		//which direction?
		int hDirection = (y & 1) ? -1 : 1;
		COLOR32 *rgbRow = img + y * width;
		for (int x = 0; x < width; x++) {
			rgbToYiq(rgbRow[x], thisRow + 4 * (x + 1));
		}
		memcpy(thisRow, thisRow + 4, 16);
		memcpy(thisRow + 4 * (width + 1), thisRow + 4 * width, 16);

		//scan across
		int startPos = (hDirection == 1) ? 0 : (width - 1);
		int x = startPos;
		for (int xPx = 0; xPx < width; xPx++) {
			//take a sample of pixels nearby. This will be a gauge of variance around this pixel, and help
			//determine if dithering should happen. Weight the sampled pixels with respect to distance from center.

			int colorY = (thisRow[(x + 1) * 4 + 0] * 3 + thisRow[(x + 2) * 4 + 0] * 3 + thisRow[x * 4 + 0] * 3 + lastRow[(x + 1) * 4 + 0] * 3
						  + lastRow[x * 4 + 0] * 2 + lastRow[(x + 2) * 4 + 0] * 2) / 16;
			int colorI = (thisRow[(x + 1) * 4 + 1] * 3 + thisRow[(x + 2) * 4 + 1] * 3 + thisRow[x * 4 + 1] * 3 + lastRow[(x + 1) * 4 + 1] * 3
						  + lastRow[x * 4 + 1] * 2 + lastRow[(x + 2) * 4 + 1] * 2) / 16;
			int colorQ = (thisRow[(x + 1) * 4 + 2] * 3 + thisRow[(x + 2) * 4 + 2] * 3 + thisRow[x * 4 + 2] * 3 + lastRow[(x + 1) * 4 + 2] * 3
						  + lastRow[x * 4 + 2] * 2 + lastRow[(x + 2) * 4 + 2] * 2) / 16;
			int colorA = thisRow[(x + 1) * 4 + 3];

			if (touchAlpha && binaryAlpha) {
				if (colorA < 128) {
					colorY = 0;
					colorI = 0;
					colorQ = 0;
					colorA = 0;
				}
			}

			//match it to a palette color. We'll measure distance to it as well.
			int colorYiq[] = { colorY, colorI, colorQ, colorA };
			int matched = c0xp + closestPaletteYiq(reduction, colorYiq, yiqPalette + c0xp * 4, nColors - c0xp);
			if (colorA == 0 && c0xp) matched = 0;

			//measure distance. From middle color to sampled color, and from palette color to sampled color.
			int *matchedYiq = yiqPalette + matched * 4;
			double paletteDy = reduction->lumaTable[matchedYiq[0]] - reduction->lumaTable[colorY];
			int paletteDi = matchedYiq[1] - colorI;
			int paletteDq = matchedYiq[2] - colorQ;
			double paletteDistance = paletteDy * paletteDy * reduction->yWeight * reduction->yWeight +
				paletteDi * paletteDi * reduction->iWeight * reduction->iWeight +
				paletteDq * paletteDq * reduction->qWeight * reduction->qWeight;

			//now measure distance from the actual color to its average surroundings
			int centerY = thisRow[(x + 1) * 4 + 0];
			int centerI = thisRow[(x + 1) * 4 + 1];
			int centerQ = thisRow[(x + 1) * 4 + 2];
			int centerA = thisRow[(x + 1) * 4 + 3];
			int centerYiq[] = { centerY, centerI, centerQ, centerA };

			double centerDy = reduction->lumaTable[centerY] - reduction->lumaTable[colorY];
			int centerDi = centerI - colorI;
			int centerDq = centerQ - colorQ;
			double centerDistance = centerDy * centerDy * reduction->yWeight * reduction->yWeight +
				centerDi * centerDi * reduction->iWeight * reduction->iWeight +
				centerDq * centerDq * reduction->qWeight * reduction->qWeight;

			//now test: Should we dither?
			double balanceSquare = reduction->yWeight * reduction->yWeight;
			if (centerDistance < 110.0 * balanceSquare && paletteDistance >  2.0 * balanceSquare && diffuse > 0.0f) {
				//Yes, we should dither :)

				int diffuseY = (int) (thisDiffuse[(x + 1) * 4 + 0] * diffuse / 16); //correct for Floyd-Steinberg coefficients
				int diffuseI = (int) (thisDiffuse[(x + 1) * 4 + 1] * diffuse / 16);
				int diffuseQ = (int) (thisDiffuse[(x + 1) * 4 + 2] * diffuse / 16);
				int diffuseA = (int) (thisDiffuse[(x + 1) * 4 + 3] * diffuse / 16);

				if (!touchAlpha || binaryAlpha) diffuseA = 0; //don't diffuse alpha if no alpha channel, or we're told not to

				colorY += diffuseCurveY(diffuseY);
				colorI += diffuseCurveI(diffuseI);
				colorQ += diffuseCurveQ(diffuseQ);
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
				int diffusedYiq[] = { colorY, colorI, colorQ, colorA };
				matched = c0xp + closestPaletteYiq(reduction, diffusedYiq, yiqPalette + c0xp * 4, nColors - c0xp);
				if (diffusedYiq[3] < 128 && c0xp) matched = 0;
				COLOR32 chosen = (palette[matched] & 0xFFFFFF) | (colorA << 24);
				img[x + y * width] = chosen;
				if (indices != NULL) indices[x + y * width] = matched;

				int *chosenYiq = yiqPalette + matched * 4;
				int offY = colorY - chosenYiq[0];
				int offI = colorI - chosenYiq[1];
				int offQ = colorQ - chosenYiq[2];
				int offA = colorA - chosenYiq[3];

				//now diffuse to neighbors
				int *diffNextPixel = thisDiffuse + (x + 1 + hDirection) * 4 + 0;
				int *diffDownPixel = nextDiffuse + (x + 1) * 4 + 0;
				int *diffNextDownPixel = nextDiffuse + (x + 1 + hDirection) * 4 + 0;
				int *diffBackDownPixel = nextDiffuse + (x + 1 - hDirection) * 4 + 0;

				if (colorA >= 128 || !binaryAlpha) { //don't dither if there's no alpha channel and this is transparent!
					diffNextPixel[0] += offY * 7;
					diffNextPixel[1] += offI * 7;
					diffNextPixel[2] += offQ * 7;
					diffNextPixel[3] += offA * 7;
					diffDownPixel[0] += offY * 5;
					diffDownPixel[1] += offI * 5;
					diffDownPixel[2] += offQ * 5;
					diffDownPixel[3] += offA * 5;
					diffBackDownPixel[0] += offY * 3;
					diffBackDownPixel[1] += offI * 3;
					diffBackDownPixel[2] += offQ * 3;
					diffBackDownPixel[3] += offA * 3;
					diffNextDownPixel[0] += offY * 1;
					diffNextDownPixel[1] += offI * 1;
					diffNextDownPixel[2] += offQ * 1;
					diffNextDownPixel[3] += offA * 1;
				}

			} else {
				//anomaly in the picture, just match the original color. Don't diffuse, it'll cause issues.
				//That or the color is pretty homogeneous here, so dithering is bad anyway.
				if (c0xp && touchAlpha) {
					if (centerYiq[3] < 128) {
						centerYiq[0] = 0;
						centerYiq[1] = 0;
						centerYiq[2] = 0;
						centerYiq[3] = 0;
					}
				}

				matched = c0xp + closestPaletteYiq(reduction, centerYiq, yiqPalette + c0xp * 4, nColors - c0xp);
				if (c0xp && centerYiq[3] < 128) matched = 0;
				COLOR32 chosen = (palette[matched] & 0xFFFFFF) | (centerYiq[3] << 24);
				img[x + y * width] = chosen;
				if (indices != NULL) indices[x + y * width] = matched;
			}

			x += hDirection;
		}

		//swap row buffers
		int *temp = thisRow;
		thisRow = lastRow;
		lastRow = temp;
		temp = nextDiffuse;
		nextDiffuse = thisDiffuse;
		thisDiffuse = temp;
		memset(nextDiffuse, 0, 16 * (width + 2));
	}

	free(yiqPalette);
	free(thisRow);
	free(lastRow);
	free(thisDiffuse);
	free(nextDiffuse);

	destroyReduction(reduction);
	free(reduction);
}

double computePaletteErrorYiq(REDUCTION *reduction, COLOR32 *px, int nPx, COLOR32 *pal, int nColors, int alphaThreshold, double nMaxError) {
	if (nMaxError == 0) nMaxError = 1e32;
	double error = 0;

	int paletteYiqStack[16 * 4]; //small palettes
	int *paletteYiq = paletteYiqStack;
	if (nColors > 16) {
		paletteYiq = (int *) calloc(nColors, 4 * sizeof(int));
	}

	//palette to YIQ
	for (int i = 0; i < nColors; i++) {
		rgbToYiq(pal[i], paletteYiq + i * 4);
	}

	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;
	for (int i = 0; i < nPx; i++) {
		COLOR32 p = px[i];
		int a = (p >> 24) & 0xFF;
		if (a < alphaThreshold) continue;

		int yiq[4];
		rgbToYiq(px[i], yiq);
		int best = closestPaletteYiq(reduction, yiq, paletteYiq, nColors);
		int *chosen = paletteYiq + best * 4;

		double dy = reduction->lumaTable[yiq[0]] - reduction->lumaTable[chosen[0]];
		double di = yiq[1] - chosen[1];
		double dq = yiq[2] - chosen[2];

		error += dy * dy * yw2;
		if (error >= nMaxError) return nMaxError;
		error += di * di * iw2 + dq * dq * qw2;
		if (error >= nMaxError) return nMaxError;
	}

	if (paletteYiq != paletteYiqStack) free(paletteYiq);
	return error;
}
