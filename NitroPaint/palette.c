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

//this has to be passed to the comparator.
static int _chunkSize;

int chunkSortComparator(void *block1, void *block2) {
	int chunkSize = _chunkSize;
	return chunkDeviation(block2, chunkSize) - chunkDeviation(block1, chunkSize);
}

//Functions for creating multiple palettes for a bitmap.
void createPalettes(DWORD *img, int width, int height, int chunkSize, DWORD *palette, int nPalettes, const int paletteSize) {
	//first, divide the image into chunks. 
	int chunksX = width >> 3;
	int chunksY = height >> 3;
	int nChunks = chunksX * chunksY;
	DWORD *palettes = (DWORD *) calloc(nChunks, paletteSize * 4);

	DWORD *chunks = (DWORD *) calloc(width * height, 4);
	//read off the image into the chunks
	int index = 0;
	for (int y = 0; y < chunksY; y++) {
		for (int x = 0; x < chunksX; x++) {
			int offs = x * chunkSize + y * chunkSize * chunksX;
			DWORD *origin = img + offs;
			DWORD *block = chunks + index * chunkSize * chunkSize;
			//copy from origin to block
			for (int _y = 0; _y < chunkSize; _y++) {
				CopyMemory(block + _y * chunkSize, origin + _y * width, chunkSize * 4);
			}

			index++;
		}
	}

	//next, sort the blocks by their standard deviation.
	_chunkSize = chunkSize;
	qsort(chunks, nChunks, chunkSize * chunkSize * 4, chunkSortComparator);

	//next, create a palette for each chunk of the image.
	for (int i = 0; i < nChunks; i++) {
		createPalette_(chunks + i * chunkSize * chunkSize, chunkSize, chunkSize, palettes + i * paletteSize, paletteSize);
	}

	//continually reduce the tolerance until the palettes are merged correctly.
	/*int tolerance = 2;
	while (1) {
		int nCreatedPalettes = 0;
		for (int i = 0; i < nChunks; i++) {
			DWORD *thisPal = palettes + i * paletteSize;
			//which palette does it best fit into?
			int bestFit = 0, bestTolerance = 0x7FFFFFFF;
			if (nCreatedPalettes == 0) {
				//if there are no palettes yet, just put this one down.
				bestTolerance = 0;
			} else {
				//iterate over the palettes to try to ind the best match.
				for (int j = 0; j < nCreatedPalettes; j++) {
					DWORD *compareTo = palettes + j * paletteSize;
					int dst = comparePalettes(thisPal, compareTo, paletteSize);
					if (dst < bestTolerance) {
						bestTolerance = dst;
					}
					
				}
			}

			//if the tolerance exceeds the limits or if there are no entries, put it down.
			if (bestTolerance > tolerance || nCreatedPalettes == 0) {
				CopyMemory(thisPal, palette + nCreatedPalettes * paletteSize, paletteSize);
				nCreatedPalettes++;
			} else {
				//otherwise, merge with its closest match.
			}
		}
		tolerance += 4;
	}*/














	DWORD * paletteIndices = (DWORD *) calloc(nChunks, 4);

	typedef struct {
		DWORD r;
		DWORD g;
		DWORD b;
		DWORD a;
	} BIGCOLOR;

	int maxColors = nPalettes * paletteSize;//127;
	/*if (maxColors & 7) {
		//not a multiple of 8
		if((maxColors & 7) > 3) maxColors += 8 - (maxColors & 7);
		else maxColors -= maxColors & 7;
	}*/
	int n = maxColors / paletteSize;
	BIGCOLOR * colors = (BIGCOLOR *) calloc(n * paletteSize + paletteSize, sizeof(BIGCOLOR));
	int tolerance = 0;
	int * sizes = (int *) calloc(n * paletteSize + paletteSize, sizeof(int));
	int nPals = 0;

	RGB *c = (RGB *) calloc(paletteSize, 4);
	RGB *testSmall = (RGB *) calloc(paletteSize, 4);
	BIGCOLOR *test = (BIGCOLOR *) calloc(paletteSize, sizeof(BIGCOLOR));
	BIGCOLOR *b = (BIGCOLOR *) calloc(paletteSize, sizeof(BIGCOLOR));
	int *indices = (int *) calloc(paletteSize, sizeof(int));
	char *used = (char *) calloc(paletteSize, 1);

	while(1){		//oddly, this function performs worse when given more color room.
		printf("-----------------\nNew tolerance: %d\n", tolerance);
		int nProcessed = 0;
		memset(colors, 0, (n * paletteSize + paletteSize) * sizeof(BIGCOLOR));
		memset(sizes, 0, n * 4 * paletteSize);
		nPals = 0;
		for(int i = 0; i < nChunks; i++, nProcessed++){
			memcpy(c, palette + (i * paletteSize), paletteSize * sizeof(RGB));
			int foundMatch = 0, foundIndex = -1;
			//find matching palette within tolerance to c. Check from 0 to (nPals-1).
			double leastDistance = 100000.0;
			//for the mappings, indices
			for(int j = 0; j < nPals; j++){
				memcpy(test, &colors[j * paletteSize], sizeof(test));
				//divide
				for(int k = 0; k < paletteSize; k++){
					int testSize = sizes[j * paletteSize + k];
					test[k].r = (test[k].r + (testSize >> 1)) / testSize;
					test[k].g = (test[k].g + (testSize >> 1)) / testSize;
					test[k].b = (test[k].b + (testSize >> 1)) / testSize;
					testSmall[k].r = test[k].r;
					testSmall[k].g = test[k].g;
					testSmall[k].b = test[k].b;
				}
				//map each color c to a color in test
				//color 0 gets index 0, color 1 gets index 1...
				for (int k = 0; k < paletteSize; k++) {
					indices[k] = closestpalette(c[k], testSmall, 4, NULL);
				}
				//add up distances
				double distance = 0.0;
				for(int k = 0; k < paletteSize; k++){
					int dR = c[k].r - testSmall[indices[k]].r;
					int dG = c[k].g - testSmall[indices[k]].g;
					int dB = c[k].b - testSmall[indices[k]].b;
					distance += sqrt(dR * dR + dG * dG + dB * dB);
				}
				if(distance <= tolerance && distance < leastDistance){
					foundMatch = 1;
					foundIndex = j;
					leastDistance = distance;
				}
			}
			if(!foundMatch || ((nChunks - i) <= (n - nPals))){
				for (int j = 0; j < paletteSize; j++) {
					b[j].r = c[j].r;
					b[j].g = c[j].g;
					b[j].b = c[j].b;
				}
				memcpy(colors + paletteSize * nPals, b, paletteSize * sizeof(BIGCOLOR));
				//_flags[nPals] = flags[i];
				paletteIndices[i] = nPals;
				for (int j = 0; j < paletteSize; j++) {
					sizes[nPals * paletteSize + j] = 1;
				}
				printf("Created new entry %d from slot %d.\n", nPals, i);
				nPals++;
				if(nPals > n){
					break;
				}
			} else {
				//map each color in c to its closest color in the cumulative palette (divided by each respective size).
				//add to the cumulative palette, then add to sizes.
				//indices has the closest color mappings. Add to sizes, and add to the colors.
				for(int j = 0; j < paletteSize; j++){
					sizes[foundIndex * paletteSize + indices[j]]++;
					colors[foundIndex * paletteSize + indices[j]].r += c[j].r;
					colors[foundIndex * paletteSize + indices[j]].g += c[j].g;
					colors[foundIndex * paletteSize + indices[j]].b += c[j].b;
				}
				printf("Palette %d slotted in %d.\n", i, foundIndex);
				//paletteIndices[i] = foundIndex;
			}
		}
		if(nPals <= n) break; //success
							  //failure
		tolerance += paletteSize / 2;

	}

	//average colors
	for(int i = 0; i < nPals * paletteSize; i++){
		colors[i].r = (colors[i].r + (sizes[i] >> 1)) / sizes[i];
		colors[i].g = (colors[i].g + (sizes[i] >> 1)) / sizes[i];
		colors[i].b = (colors[i].b + (sizes[i] >> 1)) / sizes[i];
		DWORD c = colors[i].r | (colors[i].g << 8) | (colors[i].b << 16);
		palette[i] = c;
	}











	
	//CopyMemory(palette, palettes, nPalettes * paletteSize * 4);
	free(palettes);
	free(chunks);
}