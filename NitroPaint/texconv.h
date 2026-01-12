#pragma once

#include "texture.h"
#include "palette.h"

// -----------------------------------------------------------------------------------------------
// Name: enum TxConversionResult
//
// Result code from texture conversion. This flag is returned by the TxConvert function, and
// stored in the TxConversionParams struct. This result code is only valid after TxConvert has
// returned.
//
// Values:
//   TEXCONV_SUCCESS             The texture convresion completed successfully. The converted
//                               texture data is written to params->dest.
//   TEXCONV_INVALID             One or more parameters passed are incorrect.
//   TEXCONV_NOMEM               Texture convresion ran out of memory and was aborted.
//   TEXCONV_ABORT               Texture conversion was aborted by another thread.
// -----------------------------------------------------------------------------------------------
typedef enum TxConversionResult_ {
	TEXCONV_SUCCESS,                // texture conversion is successful
	TEXCONV_INVALID,                // texture conversion could not proceed because of invalid parameters
	TEXCONV_NOMEM,                  // texture conversion has aborted because of no memory
	TEXCONV_ABORT                   // texture conversion has aborted because of an external signal
} TxConversionResult;

// -----------------------------------------------------------------------------------------------
// Name: struct TxConversionParameters
//
// The structure passed to TxConvert. This holds the input image data, conversion settings, and
// fields for IPC. 
// -----------------------------------------------------------------------------------------------
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
	char *pnam;                     // palette name for output texture

	// IPC -> conversion
	volatile int terminate;         // terminate flag (set to nonzero from another thread to end conversion)

	// IPC <- conversion
	volatile int progress;          // current conversion progress
	volatile int progressMax;       // max conversion progress
	volatile int complete;          // conversion completion flag
	volatile TxConversionResult result;
} TxConversionParameters;


// -----------------------------------------------------------------------------------------------
// Name: TxConvert
//
// Begins a texture conversion operation. Parameters are passed through the params parameter.
//
// When running this function from a separate thread, the operation can be aborted by writing
// a nonzero value to params->terminate. Wait until TxConvert returns to observe that the 
// termination is complete. A termination request must not be canceled.
//
// Parameters:
//   params                      The texture convresion parameters.
//
// Returns:
//   The status of the completed conversion operation. This status is also written to
//   params->result.
// -----------------------------------------------------------------------------------------------
TxConversionResult TxConvert(TxConversionParameters *params);
