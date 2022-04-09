#pragma once

#include "color.h"

#define BALANCE_DEFAULT  20
#define BALANCE_MIN      1
#define BALANCE_MAX      39

typedef struct RGB_ {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} RGB;

//
// Comparator for use with qsort, sortrs an array of colors by lightness.
//
int lightnessCompare(const void *d1, const void *d2);

//
// Creates a color palette for an image.
//
int createPaletteSlow(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors);

//
// Creates a color palette for an image without reserving any color slots for transparency.
//
void createPaletteExact(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors);

//
// Creates a color palette for an image without reservin any color slots for transparency, with more specific parameters.
//
int createPaletteSlowEx(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors, int balance, int colorBalance, int enhanceColors, int sortOnlyUsed);

//
// Creates a color palette for an image, reserving the first color slot for transparency, regardless if the image has transparent pixels.
//
void createPalette_(COLOR32 *img, int width, int height, COLOR32 *pal, int nColors);

//
// Finds the closest color in a palette to the specified color, optionally writing out the error.
//
int closestpalette(RGB rgb, RGB *palette, int paletteSize, RGB *error);

//
// Apply Floyd-Steinberg dithering to a pixel's surroundings.
//
void doDiffuse(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt);

//
// Apply dithering to a whole image using an adaptive error diffusion
//
void ditherImagePalette(COLOR32 *img, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse);

//
// Apply dithering to a whole image using adaptive error diffusion, while
// allowing use of specific balance settings.
//
void ditherImagePaletteEx(COLOR32 *img, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse, int balance, int colorBalance, int enhanceColors);

//
// Calculate the average color from a list of colors
//
COLOR32 averageColor(COLOR32 *cols, int nColors);

//
// Computes total squared error for a palette being applied to an image.
//
unsigned int getPaletteError(RGB *px, int nPx, RGB *pal, int paletteSize);

//
// Creates multiple palettes for an image for character map color reduction.
//
void createMultiplePalettes(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes, int paletteSize, int nColsPerPalette, int paletteOffset, int *progress);


//
// Creates multiple palettes for an image for character map color reduction
// with user-provided balance, color balance, and color enhancement settings.
//
void createMultiplePalettesEx(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes, int paletteSize, int nColsPerPalette, int paletteOffset, int balance, int colorBalance, int enhanceColors, int *progress);

//
// Convert an RGB color to YUV space.
//
void convertRGBToYUV(int r, int g, int b, int *y, int *u, int *v);

//
// Convert a YUV color to RGB space.
//
void convertYUVToRGB(int y, int u, int v, int *r, int *g, int *b);

//
// Count the number of unique colors in an image (counting transparent as a color), and otherwise ignoring the alpha channel.
//
int countColors(COLOR32 *px, int nPx);

//
// Compute palette error for a block of pixels. The alpha threshold is used to
// determine which pixels should be treated as transparent and ignored for the 
// calculation. nMaxError specifies the maximum error to calculate before 
// stopping.
//
unsigned long long computePaletteError(COLOR32 *px, int nPx, COLOR32 *pal, int nColors, int alphaThreshold, unsigned long long nMaxError);

//----------structures used by palette generator

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
	int isLeaf;
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
	int nUsedColors;
	int yWeight;
	int iWeight;
	int qWeight;
	int enhanceColors;
	int maskColors;
	int optimization;
	HISTOGRAM *histogram;
	HIST_ENTRY **histogramFlat;
	COLOR_NODE *colorTreeHead;
	COLOR_NODE *colorBlocks[0x2000];
	uint8_t paletteRgb[256][3];
	double lumaTable[512];
	double gamma;
} REDUCTION;

//
// Encode an RGBA color to a YIQA color.
//
void rgbToYiq(COLOR32 rgb, int *yiq);

//
// Decode a YIQ color to RGB.
//
void yiqToRgb(int *rgb, int *yiq);

//
// Initialize a REDUCTION structure with palette parameters.
//
void initReduction(REDUCTION *reduction, int balance, int colorBalance, int optimization, int enhanceColors, unsigned int nColors);

//
// Add a YIQA color to a REDUCTION's histogram.
//
void histogramAddColor(HISTOGRAM *histogram, int y, int i, int q, int a, double weight);

//
// Add an image's color data to a REDUCTION's histogram.
//
void computeHistogram(REDUCTION *reduction, COLOR32 *img, int width, int height);

//
// Clears out a REDUCTION's histogram. Can be used to create multiple palettes.
//
void resetHistogram(REDUCTION *reduction);

//
// Flatten a REDUCTION's histogram. Do this once the histogram is complete.
//
void flattenHistogram(REDUCTION *reduction);

//
// Optimize a REDUCTION's flattened hitogram into a color palette.
//
void optimizePalette(REDUCTION *reduction);

//
// Flatten's a REDUCTION's palette into an array of RGB colors. Do this once
// the palette is optimized.
//
void paletteToArray(REDUCTION *reduction);

//
// Find the closest YIQA color to a specified YIQA color with a provided
// reduction context.
//
int closestPaletteYiq(REDUCTION *reduction, int *yiqColor, int *palette, int nColors);

//
// Free all resources consumed by a REDUCTION.
//
void destroyReduction(REDUCTION *reduction);
