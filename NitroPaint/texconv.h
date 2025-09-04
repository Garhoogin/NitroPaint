#pragma once
#include <Windows.h>
#include "texture.h"

//
// Structure used by texture conversion functions.
//
typedef struct TxConversionParameters_ {
	COLOR32 *px;
	unsigned int width;
	unsigned int height;
	int fmt;
	int forTwl;
	int dither;
	float diffuseAmount;
	int ditherAlpha;
	int c0xp;
	unsigned int colorEntries;
	int useFixedPalette;
	COLOR *fixedPalette;
	int threshold;
	int balance;
	int colorBalance;
	int enhanceColors;
	TEXTURE *dest;
	void (*callback) (void *);
	void *callbackParam;
	char *pnam;
} TxConversionParameters;

//
// Counts the number of colors in an image (transparent counts as a color)
//
int ImgCountColors(COLOR32 *px, int nPx);

//
// Convert an image to a direct mode texture
//
int TxConvertDirect(TxConversionParameters *params);

//
// Convert an image to a paletted texture
//
int TxConvertIndexedOpaque(TxConversionParameters *params);

//
// Convert an image to a translucent (a3i5 or a5i3) texture
//
int TxConvertIndexedTranslucent(TxConversionParameters *params);

//progress markers for TxConvert4x4.
extern volatile int g_texCompressionProgress;
extern volatile int g_texCompressionProgressMax;
extern volatile int g_texCompressionFinished;

//
// Convert an image to a 4x4 compressed texture
//
int TxConvert4x4(TxConversionParameters *params);

//
// Convert a texture given some input parameters.
//
int TxConvert(TxConversionParameters *params);
