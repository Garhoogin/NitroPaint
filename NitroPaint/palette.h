#pragma once

#include "color.h"

#define BALANCE_DEFAULT  20
#define BALANCE_MIN      1
#define BALANCE_MAX      39

#define RECLUSTER_DEFAULT 8

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
int closestPalette(COLOR32 rgb, COLOR32 *palette, int paletteSize);

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
void ditherImagePaletteEx(COLOR32 *img, int *indices, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse, int balance, int colorBalance, int enhanceColors);

//
// Calculate the average color from a list of colors
//
COLOR32 averageColor(COLOR32 *cols, int nColors);

//
// Computes the total squared error for a palette being applied to an image.
//
unsigned int getPaletteError(COLOR32 *px, int nPx, COLOR32 *pal, int paletteSize);

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

typedef struct RGB_COLOR_ {
	int r;
	int g;
	int b;
	int a;
} RGB_COLOR;

typedef struct YIQ_COLOR_ {
	int y;
	int i;
	int q;
	int a;
} YIQ_COLOR;

//histogram linked list entry as secondary sorting
typedef struct HIST_ENTRY_ {
	YIQ_COLOR color;
	struct HIST_ENTRY_ *next;
	int entry;
	double weight;
	double value;
} HIST_ENTRY;

//structure for a node in the color tree
typedef struct COLOR_NODE_ {
	int isLeaf;
	double weight;
	double priority;
	YIQ_COLOR color;
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
	int firstSlot;
} HISTOGRAM;

//struct for totaling a bucket in reclustering
typedef struct {
	double y;
	double i;
	double q;
	double a;
	double weight;
	double error;
} TOTAL_BUFFER;

//reduction workspace structure
typedef struct REDUCTION_ {
	int nPaletteColors;
	int nUsedColors;
	int yWeight;
	int iWeight;
	int qWeight;
	int enhanceColors;
	int nReclusters;
	int maskColors;
	int optimization;
	HISTOGRAM *histogram;
	HIST_ENTRY **histogramFlat;
	TOTAL_BUFFER blockTotals[256];
	COLOR_NODE *colorTreeHead;
	COLOR_NODE *colorBlocks[0x2000];
	uint8_t paletteRgb[256][3];
	uint8_t paletteRgbCopy[256][3];
	YIQ_COLOR paletteYiq[256];
	YIQ_COLOR paletteYiqCopy[256];
	double lumaTable[512];
	double gamma;
} REDUCTION;

//
// Encode an RGBA color to a YIQA color.
//
void rgbToYiq(COLOR32 rgb, YIQ_COLOR *yiq);

//
// Decode a YIQ color to RGB.
//
void yiqToRgb(RGB_COLOR *rgb, YIQ_COLOR *yiq);

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
// Sort a histogram's colors by their principal component.
//
void sortHistogram(REDUCTION *reduction, int startIndex, int endIndex);

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
// Find the closest YIQA color to a specified YIQA color with a provided
// reduction context.
//
int closestPaletteYiq(REDUCTION *reduction, YIQ_COLOR *yiqColor, YIQ_COLOR *palette, int nColors);

//
// Compute palette error on a bitmap given a specified reduction context.
//
double computePaletteErrorYiq(REDUCTION *reduction, COLOR32 *px, int nPx, COLOR32 *pal, int nColors, int alphaThreshold, double nMaxError);

//
// Compute palette error on a histogram.
//
double computeHistogramPaletteError(REDUCTION *reduction, COLOR32 *palette, int nColors, double maxError);

//
// Compute palette error on a histogram for a YIQ palette.
//
double computeHistogramPaletteErrorYiq(REDUCTION *reduction, YIQ_COLOR *yiqPalette, int nColors, double maxError);

//
// Free all resources consumed by a REDUCTION.
//
void destroyReduction(REDUCTION *reduction);
