#pragma once
#include <Windows.h>
#include "texture.h"

//
// Structure used by texture conversion functions.
//
typedef struct TxConversionParameters_ {
	COLOR32 *px;                    // input image pixels
	unsigned int width;             // input image width
	unsigned int height;            // input image height
	int fmt;                        // requested texture format
	int forTwl;                     // generate texture data for TWL (currently unimplemented)
	int dither;                     // enable dithering of color
	float diffuseAmount;            // dithering level (0-1)
	int ditherAlpha;                // enable dithering of the alpha channel
	int c0xp;                       // enable color-0 transparency
	unsigned int colorEntries;      // number of palette colors in the target texture (size of fixed palette, if used)
	int useFixedPalette;            // enable fixed palette (0=no, nonzero=yes)
	COLOR *fixedPalette;            // pointer to fixed palette (used if useFixedPalette is nonzero)
	int threshold;                  // 4x4 compression threshold (0-100)
	int balance;                    // balance setting (1-39)
	int colorBalance;               // color balance setting (1-39)
	int enhanceColors;              // enhance largely used colors (0=no, nonzero=yes)
	TEXTURE *dest;                  // pointer to destination texture object
	void (*callback) (void *);      // completion callback (called after conversion completed)
	void *callbackParam;            // callback parameter
	char *pnam;                     // palette name for output texture

	// IPC -> conversion
	volatile int terminate;         // terminate flag (set to nonzero from another thread to end conversion)

	// IPC <- conversion
	volatile int progress;          // current conversion progress
	volatile int progressMax;       // max conversion progress
	volatile int complete;          // conversion completion flag
} TxConversionParameters;

//
// Convert a texture given some input parameters.
//
int TxConvert(TxConversionParameters *params);
