#pragma once

#include "color.h"

#define BALANCE_DEFAULT  20
#define BALANCE_MIN      1
#define BALANCE_MAX      39

#define RECLUSTER_DEFAULT 8

typedef struct RxBalanceSetting_ {
	int balance;          //relative priority of lightness over color information (1-39)
	int colorBalance;     //relative priority of reds over greens                 (1-39)
	int enhanceColors;    //give more weight to gradient colors                   (0 or 1)
} RxBalanceSetting;

typedef struct RxDitherSetting_ {
	int dither;           //enable dithering (0 or 1)
	float diffuse;        //dithering amount (0-1)
} RxDitherSetting;

//
// Comparator for use with qsort, sortrs an array of colors by lightness.
//
int RxColorLightnessComparator(const void *d1, const void *d2);

//
// Creates a color palette for an image.
//
int RxCreatePalette(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors);

//
// Creates a color palette for an image without reservin any color slots for transparency, with more specific parameters.
//
int RxCreatePaletteEx(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors, int balance, int colorBalance, int enhanceColors, int sortOnlyUsed);

//
// Creates a color palette for an image, reserving the first color slot for transparency, regardless if the image has transparent pixels.
//
void RxCreatePaletteTransparentReserve(COLOR32 *img, int width, int height, COLOR32 *pal, int nColors);

//
// Finds the closest color in a palette to the specified color, optionally writing out the error.
//
int RxPaletteFindClosestColorSimple(COLOR32 rgb, COLOR32 *palette, int paletteSize);

//
// Apply Floyd-Steinberg dithering to a pixel's surroundings.
//
void doDiffuse(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt);

//
// Apply dithering to a whole image using an adaptive error diffusion
//
void RxReduceImage(COLOR32 *img, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse);

//
// Apply dithering to a whole image using adaptive error diffusion, while
// allowing use of specific balance settings.
//
void RxReduceImageEx(COLOR32 *img, int *indices, int width, int height, COLOR32 *palette, int nColors, int touchAlpha, int binaryAlpha, int c0xp, float diffuse, int balance, int colorBalance, int enhanceColors);

//
// Creates multiple palettes for an image for character map color reduction.
//
void RxCreateMultiplePalettes(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes, int paletteSize, int nColsPerPalette, int paletteOffset, int *progress);


//
// Creates multiple palettes for an image for character map color reduction
// with user-provided balance, color balance, and color enhancement settings.
//
void RxCreateMultiplePalettesEx(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes, int paletteSize, int nColsPerPalette, int paletteOffset, int balance, int colorBalance, int enhanceColors, int *progress);

//
// Convert an RGB color to YUV space.
//
void RxConvertRgbToYuv(int r, int g, int b, int *y, int *u, int *v);

//
// Convert a YUV color to RGB space.
//
void RxConvertYuvToRgb(int y, int u, int v, int *r, int *g, int *b);


//----------structures used by palette generator

typedef struct RxRgbColor_ {
	int r;
	int g;
	int b;
	int a;
} RxRgbColor;

typedef struct RxYiqColor_ {
	int y;
	int i;
	int q;
	int a;
} RxYiqColor;

//histogram linked list entry as secondary sorting
typedef struct RxHistEntry_ {
	RxYiqColor color;
	struct RxHistEntry_ *next;
	int entry;
	double weight;
	double value;
} RxHistEntry;

//structure for a node in the color tree
typedef struct RxColorNode_ {
	int isLeaf;
	double weight;
	double priority;
	RxYiqColor color;
	int pivotIndex;
	int startIndex;
	int endIndex;
	struct RxColorNode_ *left;
	struct RxColorNode_ *right;
} RxColorNode;

//allocator for allocating the linked lists
typedef struct RxSlab_ {
	void *allocation;
	int nextEntryOffset;
	struct RxSlab_ *next;
} RxSlab;

//histogram structure
typedef struct RxHistogram_ {
	RxSlab allocator;
	RxHistEntry *entries[0x20000];
	int nEntries;
	int firstSlot;
} RxHistogram;

//struct for totaling a bucket in reclustering
typedef struct {
	double y;
	double i;
	double q;
	double a;
	double weight;
	double error;
} RxTotalBuffer;

//reduction workspace structure
typedef struct RxReduction_ {
	int nPaletteColors;
	int nUsedColors;
	int yWeight;
	int iWeight;
	int qWeight;
	int enhanceColors;
	int nReclusters;
	int maskColors;
	int optimization;
	RxHistogram *histogram;
	RxHistEntry **histogramFlat;
	RxTotalBuffer blockTotals[256];
	RxColorNode *colorTreeHead;
	RxColorNode *colorBlocks[0x2000];
	uint8_t paletteRgb[256][3];
	uint8_t paletteRgbCopy[256][3];
	RxYiqColor paletteYiq[256];
	RxYiqColor paletteYiqCopy[256];
	double lumaTable[512];
	double gamma;
} RxReduction;

//
// Encode an RGBA color to a YIQA color.
//
void RxConvertRgbToYiq(COLOR32 rgb, RxYiqColor *yiq);

//
// Decode a YIQ color to RGB.
//
void RxConvertYiqToRgb(RxRgbColor *rgb, RxYiqColor *yiq);

//
// Initialize a RxReduction structure with palette parameters.
//
void RxInit(RxReduction *reduction, int balance, int colorBalance, int optimization, int enhanceColors, unsigned int nColors);

//
// Add a YIQA color to a RxReduction's histogram.
//
void RxHistAddColor(RxHistogram *histogram, int y, int i, int q, int a, double weight);

//
// Add an image's color data to a RxReduction's histogram.
//
void RxHistAdd(RxReduction *reduction, COLOR32 *img, int width, int height);

//
// Sort a histogram's colors by their principal component.
//
void RxHistSort(RxReduction *reduction, int startIndex, int endIndex);

//
// Clears out a RxReduction's histogram. Can be used to create multiple palettes.
//
void RxHistClear(RxReduction *reduction);

//
// Flatten a RxReduction's histogram. Do this once the histogram is complete.
//
void RxHistFinalize(RxReduction *reduction);

//
// Optimize a RxReduction's flattened hitogram into a color palette.
//
void RxComputePalette(RxReduction *reduction);

//
// Find the closest YIQA color to a specified YIQA color with a provided
// reduction context.
//
int RxPaletteFindCloestColorYiq(RxReduction *reduction, RxYiqColor *yiqColor, RxYiqColor *palette, int nColors);

//
// Compute palette error on a bitmap given a specified reduction context.
//
double RxComputePaletteError(RxReduction *reduction, COLOR32 *px, int nPx, COLOR32 *pal, int nColors, int alphaThreshold, double nMaxError);

//
// Compute palette error on a histogram.
//
double RxHistComputePaletteError(RxReduction *reduction, COLOR32 *palette, int nColors, double maxError);

//
// Compute palette error on a histogram for a YIQ palette.
//
double RxHistComputePaletteErrorYiq(RxReduction *reduction, RxYiqColor *yiqPalette, int nColors, double maxError);

//
// Free all resources consumed by a RxReduction.
//
void RxDestroy(RxReduction *reduction);
