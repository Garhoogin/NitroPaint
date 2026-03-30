// -----------------------------------------------------------------------------------------------
// Copyright (c) 2020, Garhoogin
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// -----------------------------------------------------------------------------------------------
#pragma once

// -----------------------------------------------------------------------------------------------
// Texture Conversion Module
//
// This header provides an API for converting textures into DS texture formats. The conversion
// is thread-safe, and multiple threads may be converting textures simultaneously. All DS texture
// formats are supported.
// 
// This header describes one primary entry point, the TxConvert function, taking a conversion
// parameters structure. When done on a separate thread, the thread handling the texture 
// conversion may be controlled by using the IPC fields of the structure.
// -----------------------------------------------------------------------------------------------
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
//   TEXCONV_SUCCESS             The texture conversion completed successfully. The converted
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
//
// The px, width, and height fields describe the input image to conversion. When the width and/
// or height of the image are not powers of 2, they are padded to the next valid size by repeating
// the last row/column of pixels to fill the full size, as though by clamping the texture
// coordinates. The full power-of-2 size worth of texture data is allocated and stored in the
// result.
//
// The destination texture data is written to the struct pointed to by dest. Allocation of the
// destination struct is the responsibility of the caller, but fields in the struct are allocated
// by the conversion function. Allocated data may be freed using free.
//
// To use the fixed palette, set the fixedPalette field to a pointer to color palette, which the
// conversion routine treats as read-only. The returned palette after conversion is then a copy
// of the fixed palette in this struct. Using the fixed palette, the colorEntries field now
// specifies the number of colors in the fixed palette. If this would be greater than the maximum
// number of colors used by the requested texture format, then the palette data is truncated on
// conversion. Read out the number of colors in the returned palette to determine the actual size
// of the returned palette.
//
// During texture conversion, the progress and progressMax fields indicate the level of progress
// of the texture conversion. When displaying progress, the calling thread should wait for the
// progressMax field to be nonzero. When the conversion is completed (whether successfully or
// otherwise), the complete field is set nonzero, and the conversion result is written to the
// result field.
//
// To terminate the conversion, write a nonzero number to the terminate field. This field must be
// zero when conversion starts. When requesting a termination, do not set this field back to 0.
// When termination completes, the result TEXCONV_ABORT is written to the result field. If the
// texture conversion would complete before the termination request is fulfilled, the result is
// treated as successful as though the termination request had never happened.
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
	int c0xp;                       // enable color-0 transparency (supported for palette4, palette16, palette256 formats)
	unsigned int colorEntries;      // number of palette colors in the target texture (size of fixed palette, if used)
	COLOR *fixedPalette;            // pointer to fixed palette (set to NULL to not use)
	int threshold;                  // 4x4 compression threshold (0-100)
	RxBalanceSetting balance;       // balance setting
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
//   params                      The texture conversion parameters.
//
// Returns:
//   The status of the completed conversion operation. This status is also written to
//   params->result.
// -----------------------------------------------------------------------------------------------
TxConversionResult TxConvert(TxConversionParameters *params);
