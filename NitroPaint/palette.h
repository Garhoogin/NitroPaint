#pragma once
#include <Windows.h>

#define PALETTE_SLOW 0
#define PALETTE_FAST 1

typedef struct RGB_ {
	BYTE r;
	BYTE g;
	BYTE b;
	BYTE a;
} RGB;

int lightnessCompare(const void *d1, const void *d2);

int createPaletteSlow(DWORD *img, int width, int height, DWORD *pal, unsigned int nColors);

void createPaletteExact(DWORD *img, int width, int height, DWORD *pal, unsigned int nColors);

int createPaletteSlowEx(DWORD *img, int width, int height, DWORD *pal, unsigned int nColors, int balance, int colorBalance, BOOL enhanceColors, BOOL sortOnlyUsed);

void createPalette_(DWORD * img, int width, int height, DWORD * pal, int nColors);

closestpalette(RGB rgb, RGB * palette, int paletteSize, RGB * error);

void doDiffuse(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt);

void ditherImagePalette(DWORD *img, int width, int height, DWORD *palette, int nColors, BOOL touchAlpha, int c0xp, float diffuse);

DWORD averageColor(DWORD *cols, int nColors);

unsigned int getPaletteError(RGB *px, int nPx, RGB *pal, int paletteSize);

void createMultiplePalettes(DWORD *imgBits, int tilesX, int tilesY, DWORD *dest, int paletteBase, int nPalettes, int paletteSize, int nColsPerPalette, int paletteOffset, int *progress);

void convertRGBToYUV(int r, int g, int b, int *y, int *u, int *v);

void convertYUVToRGB(int y, int u, int v, int *r, int *g, int *b);

int countColors(DWORD *px, int nPx);