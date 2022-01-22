#pragma once
#include <Windows.h>
#include "texture.h"

//
// Structure used by texture conversion functions.
//
typedef struct {
	COLOR32 *px;
	int width;
	int height;
	int fmt;
	int dither;
	float diffuseAmount;
	int ditherAlpha;
	int colorEntries;
	int useFixedPalette;
	COLOR *fixedPalette;
	int threshold;
	TEXTURE *dest;
	void (*callback) (void *);
	void *callbackParam;
	char pnam[17];
} CREATEPARAMS;

//
// Counts the number of colors in an image (transparent counts as a color)
//
int countColors(COLOR32 *px, int nPx);

//
// Convert an image to a direct mode texture
//
int convertDirect(CREATEPARAMS *params);

//
// Convert an image to a paletted texture
//
int convertPalette(CREATEPARAMS *params);

//
// Convert an image to a translucent (a3i5 or a5i3) texture
//
int convertTranslucent(CREATEPARAMS *params);

//progress markers for convert4x4.
extern volatile _globColors;
extern volatile _globFinal;
extern volatile _globFinished;

//
// Convert an image to a 4x4 compressed texture
//
int convert4x4(CREATEPARAMS *params);

//to convert a texture directly. lpParam is a CREATEPARAMS struct pointer.
DWORD CALLBACK startConvert(LPVOID lpParam);

void threadedConvert(COLOR32 *px, int width, int height, int fmt, int dither, float diffuse, int ditherAlpha, int colorEntries, int useFixedPalette, COLOR *fixedPalette, int threshold, char *pnam, TEXTURE *dest, void (*callback) (void *), void *callbackParam);
