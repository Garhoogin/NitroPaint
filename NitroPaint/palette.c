#include "palette.h"
#include <math.h>

#define BIGINT unsigned long long

typedef struct BUCKET_ {
	DWORD * colors;
	int nColors;
	float deviation;
	DWORD avg;
} BUCKET;

/*
* reduce a color to its nearest 15-bit color, and convert back to 24-bit color.
*/
DWORD reduce(DWORD col) {
	int r = col & 0xFF;
	int g = (col >> 8) & 0xFF;
	int b = (col >> 16) & 0xFF;
	r = ((r + 4) * 31 / 255) * 255 / 31;
	g = ((g + 4) * 31 / 255) * 255 / 31;
	b = ((b + 4) * 31 / 255) * 255 / 31;
	return r | (g << 8) | (b << 16);
}

void createBucket(BUCKET * bucket, DWORD * colors, int nColors) {
	bucket->colors = colors;
	bucket->nColors = nColors;
	if (nColors < 2) {
		bucket->deviation = 0.0f;
		if (nColors == 1) {
			bucket->avg = *colors;
		} else {
			bucket->avg = 0;
		}
	} else {
		unsigned long long devRed = 0, devGreen = 0, devBlue = 0;
		DWORD * array = colors;
		unsigned avgr = 0, avgg = 0, avgb = 0;
		for (unsigned i = 0; i < nColors; i++) {
			DWORD col = colors[i];
			avgr += col & 0xFF;
			avgg += (col >> 8) & 0xFF;
			avgb += (col >> 16) & 0xFF;
		}
		avgr = (avgr + (nColors >> 1)) / nColors;
		avgg = (avgg + (nColors >> 1)) / nColors;
		avgb = (avgb + (nColors >> 1)) / nColors;
		bucket->avg = avgr | (avgg << 8) | (avgb << 16);
		for (unsigned i = 0; i < nColors; i++) {
			DWORD c = colors[i];
			int diffr = (c & 0xFF) - avgr;
			int diffg = ((c >> 8) & 0xFF) - avgg;
			int diffb = ((c >> 16) & 0xFF) - avgb;
			devRed += diffr * diffr;
			devGreen += diffg * diffg;
			devBlue += diffb * diffb;

		}
		//note, this isn't exactly a standard deviation, because I am square rooting the list size.
		//I do this to strike a balance between detail and smooth colors.
		bucket->deviation = sqrt((double) devRed / sqrt((double) nColors)) + sqrt((double) devGreen / sqrt((double) nColors)) + sqrt((double) devBlue / sqrt((double) nColors));

	}
	bucket->avg = reduce(bucket->avg);
}

int shiftBy = 0;
int paletteCcomparator(void * d1, void * d2) {
	int n1 = ((*(DWORD *) d1) >> shiftBy) & 0xFF;
	int n2 = ((*(DWORD *) d2) >> shiftBy) & 0xFF;
	return n1 - n2;
}

int lightnessCompare(void * d1, void * d2) {
	DWORD c1 = *(DWORD *) d1;
	DWORD c2 = *(DWORD *) d2;
	int l1 = (c1 & 0xFF) + ((c1 >> 8) & 0xFF) + ((c1 >> 16) & 0xFF);
	int l2 = (c2 & 0xFF) + ((c2 >> 8) & 0xFF) + ((c2 >> 16) & 0xFF);
	return l1 - l2;
}

void createPalette_(DWORD * img, int width, int height, DWORD * pal, int nColors) {
	/* if it has alpha 0, just overwrite it to be black. */
	for (int i = 0; i < width * height; i++) {
		DWORD d = img[i];
		if (d >> 24) continue;
		img[i] = 0;
	}
	/* is this image already compressed enough? */
	int nUniqueColors = 0, nSearched = 0;
	for (int i = 0; i < width * height; i++) {
		DWORD d = img[i];
		if (d == 0) continue;
		d &= 0xFFFFFF;
		int found = 0;
		for (int j = 0; j < nUniqueColors; j++) {
			if (d == pal[j + 1] & 0xFFFFFF) {
				found = 1;
				break;
			}
		}
		if (!found) {
			if (nUniqueColors >= nColors - 1) break;
			pal[nUniqueColors + 1] = d;
			nUniqueColors++;
		}
		nSearched++;
	}
	_asm int 3
	if (nSearched == width * height) {
		pal[0] = 0xFF00FF;
		return;
	}
													   /* create a copy. This way, we can modify it. */
	DWORD * copy = (DWORD *) calloc(width * height, sizeof(DWORD));
	unsigned scaleTo = 0;
	unsigned nPixels = width * height;
	for (unsigned i = 0; i < nPixels; i++) {
		DWORD d = img[i];
		if (d >> 24) {
			copy[scaleTo] = d;
			scaleTo++;
		}
	}


	unsigned nBuckets = 1;
	BUCKET * buckets = (BUCKET *) calloc(nColors, sizeof(BUCKET));
	createBucket(buckets, copy, scaleTo);
	unsigned nDesired = nColors - 1;
	while (nBuckets < nDesired) {

		float largestDeviation = 0.0f;
		unsigned largestIndex = 0;
		/* determine the bucket with the greatest deviation */
		for (unsigned j = 0; j < nBuckets; j++) {
			float dev = buckets[j].deviation;
			if (dev > largestDeviation) {
				largestDeviation = dev;
				largestIndex = j;
			}
		}
		/* break if there is no way to further divide the palette */
		if (largestDeviation == 0.0f) break;

		/* split the chosen bucket in half. */
		BUCKET popped = buckets[largestIndex];
		nBuckets--;
		DWORD * asArray = popped.colors;
		unsigned length = popped.nColors;
		memmove(buckets + largestIndex, buckets + largestIndex + 1, sizeof(BUCKET) * (nColors - largestIndex - 1)); /* remove it */

																													/* split the array into two subarrays. */
		int maxes[] = { 0, 0, 0 };
		int mins[] = { 255, 255, 255 };
		for (unsigned k = 0; k < length; k++) {
			DWORD c = asArray[k];
			int r = c & 0xFF;
			int g = (c >> 8) & 0xFF;
			int b = (c >> 16) & 0xFF;
			if (r > maxes[0]) maxes[0] = r;
			if (g > maxes[1]) maxes[1] = g;
			if (b > maxes[2]) maxes[2] = b;
			if (r < mins[0]) mins[0] = r;
			if (g < mins[1]) mins[1] = g;
			if (b < mins[2]) mins[2] = b;
		}
		int range[] = {maxes[0] - mins[0], maxes[1] - mins[1], maxes[2] - mins[2]};
		if ((range[0] >= range[1]) && (range[0] >= range[2])) {
			shiftBy = 0;
		} else if ((range[1] >= range[0]) && (range[1] >= range[2])) {
			shiftBy = 8;
		} else {
			shiftBy = 16;
		}
		qsort(asArray, length, 4, paletteCcomparator);

		unsigned length1 = length >> 1;
		unsigned length2 = length - length1;
		createBucket(buckets + nBuckets, asArray, length1);
		nBuckets++;
		createBucket(buckets + nBuckets, asArray + length1, length2);
		nBuckets++;
		/* in images with large amounts of one color, that one color may get too many entries. */
		/* try to mitigate this by checking for multiple buckets with a deviation of 0 and the  */
		/* same average. */
		if (nBuckets >= nDesired) {
			for (unsigned i = 0; i < nBuckets; i++) {
				//if (buckets[i].deviation != 0) continue;
				DWORD avg = buckets[i].avg;
				// bucket's deviation is 0! check all future buckets for duplication.
				for (unsigned j = i + 1; j < nBuckets; j++) {
					BUCKET * b = buckets + j;
					//if (b->deviation != 0) continue;
					DWORD avg2 = b->avg;
					if (avg2 != avg) continue;
					//a match. Delete this bucket. Delete it.
					MoveMemory(b, b + 1, (nBuckets - j - 1) * sizeof(BUCKET));
					nBuckets--;
					j--;
				}
			}
		}
	}
	/* write the averages into the output palette */
	for (unsigned i = 0; i < nBuckets; i++) {
		pal[i] = buckets[i].avg;
	}
	if (nBuckets < nColors) {
		for (int i = 0; i < nColors - nBuckets; i++) {
			pal[i + nBuckets] = 0;
		}
	}
	free(copy);
	free(buckets);

	pal[nColors - 1] = *pal;
	*pal = 0xFF00FF;

	qsort(pal + 1, nColors - 1, 4, lightnessCompare);
}


closestpalette(RGB rgb, RGB * palette, int paletteSize, RGB * error) {
	int smallestDistance = 1 << 24;
	int index = 0, i = 0;
	for (; i < paletteSize; i++) {
		RGB entry = palette[i];
		int dr = entry.r - rgb.r;
		int dg = entry.g - rgb.g;
		int db = entry.b - rgb.b;
		int dst = dr * dr + dg * dg + db * db;
		if (dst < smallestDistance) {
			index = i;
			smallestDistance = dst;
		}
	}
	RGB entry = palette[index];
	if (error) {
		error->r = -(rgb.r - entry.r);
		error->g = -(rgb.g - entry.g);
		error->b = -(rgb.b - entry.b);
	}
	return index;
}

int m(int a) {
	return a < 0? 0: (a > 255? 255: a);
}

#define diffuse(a,r,g,b,ap) a=m((a&0xFF)+(r))|(m(((a>>8)&0xFF)+(g))<<8)|(m(((a>>16)&0xFF)+(b))<<16)|(m(((a>>24)&0xFF)+(ap))<<24)

void doDiffuse(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt) {
	//if ((pixels[i] >> 24) < 127) return;
	if (i % width < width - 1) {
		unsigned int right = pixels[i + 1];
		diffuse(right, errorRed * 7 * amt / 16, errorGreen * 7 * amt / 16, errorBlue * 7 * amt / 16, errorAlpha * 7 * amt / 16);
		pixels[i + 1] = right;
	}
	if (i / width < height - 1) {
		if (i % width > 0) {//downleft
			unsigned int right = pixels[i + width - 1];
			diffuse(right, errorRed * 3 * amt / 16, errorGreen * 3 * amt / 16, errorBlue * 3 * amt / 16, errorAlpha * 3 * amt / 16);
			pixels[i + width - 1] = right;
		}
		if (1) {//down
			unsigned int right = pixels[i + width];
			diffuse(right, errorRed * 5 * amt / 16, errorGreen * 5 * amt / 16, errorBlue * 5 * amt / 16, errorAlpha * 5 * amt / 16);
			pixels[i + width] = right;
		}
		if (i % width < width - 1) {
			unsigned int right = pixels[i + width + 1];
			diffuse(right, errorRed * 1 * amt / 16, errorGreen * 1 * amt / 16, errorBlue * 1 * amt / 16, errorAlpha * 1 * amt / 16);
			pixels[i + width + 1] = right;
		}
	}
}


int chunkDeviation(DWORD *block, int width) {
	//find the average.
	int nPx = width * width;
	int avgr = 0, avgg = 0, avgb = 0;
	for (int i = 0; i < nPx; i++) {
		DWORD p = block[i];
		//if (p & 0xFF000000 == 0) continue;
		avgr += p & 0xFF;
		avgg += (p >> 8) & 0xFF;
		avgb += (p >> 16) & 0xFF;
	}
	avgr /= nPx;
	avgg /= nPx;
	avgb /= nPx;

	//calculate the deviation
	unsigned int deviation = 0;
	for (int i = 0; i < nPx; i++) {
		DWORD p = block[i];
		int r = p & 0xFF;
		int g = (p >> 8) & 0xFF;
		int b = (p >> 16) & 0xFF;
		int dr = r - avgr;
		int dg = g - avgg;
		int db = b - avgb;
		int c = dr * dr + dg * dg + db * db;
		deviation += c;
	}
	deviation /= nPx;
	return deviation;
}

DWORD averageColor(DWORD *cols, int nColors) {
	int tr = 0, tg = 0, tb = 0, nCounted = 0;

	for (int i = 0; i < nColors; i++) {
		DWORD c = cols[i];
		if (c & 0xFF000000 == 0) continue;
		tr += c & 0xFF;
		tg += (c >> 8) & 0xFF;
		tb += (c >> 16) & 0xFF;
		nCounted++;
	}

	if (nCounted == 0) return 0;
	tr = (tr + (nCounted >> 1)) / nCounted;
	tg = (tg + (nCounted >> 1)) / nCounted;
	tb = (tb + (nCounted >> 1)) / nCounted;
	
	return tr | (tg << 8) | (tb << 16) | 0xFF000000;
}

unsigned int getPaletteError(RGB *px, int nPx, RGB *pal, int paletteSize) {
	unsigned int error = 0;
	for (int i = 0; i < nPx; i++) {
		RGB thisColor = px[i];
		if (thisColor.a == 0) continue;
		RGB closest = pal[closestpalette(px[i], pal + 1, paletteSize - 1, NULL) + 1];
		int er = closest.r - thisColor.r;
		int eg = closest.g - thisColor.g;
		int eb = closest.b - thisColor.b;
		error += (int) (sqrt(er * er + eg * eg + eb * eb) + 0.5f);
	}
	return error;
}