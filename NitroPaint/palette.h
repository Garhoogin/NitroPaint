#pragma once

#include "color.h"

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
