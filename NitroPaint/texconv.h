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
int textureConvertDirect(CREATEPARAMS *params);

//
// Convert an image to a paletted texture
//
int textureConvertPalette(CREATEPARAMS *params);

//
// Convert an image to a translucent (a3i5 or a5i3) texture
//
int textureConvertTranslucent(CREATEPARAMS *params);

//progress markers for textureConvert4x4.
extern volatile _globColors;
extern volatile _globFinal;
extern volatile _globFinished;

//
// Convert an image to a 4x4 compressed texture
//
int textureConvert4x4(CREATEPARAMS *params);

//
// Convert a texture given some input parameters.
//
int textureConvert(CREATEPARAMS *params);

//
// Begin a texture conversion in a new thread, returning a handle to the thread.
//
HANDLE textureConvertThreaded(COLOR32 *px, int width, int height, int fmt, int dither, float diffuse, int ditherAlpha, int colorEntries, int useFixedPalette, COLOR *fixedPalette, int threshold, char *pnam, TEXTURE *dest, void (*callback) (void *), void *callbackParam);
