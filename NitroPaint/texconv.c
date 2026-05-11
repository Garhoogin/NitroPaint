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
#include "palette.h"
#include "color.h"
#include "texconv.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _MSC_VER
#define strdup _strdup
#endif

//optimize for speed rather than size
#ifndef _DEBUG
#ifdef _MSC_VER
#pragma optimize("t", on)
#endif
#endif

//assumption+assertion macros
#ifdef NDEBUG
#ifdef _MSC_VER
#define TX_ASSUME(x)    __assume(x)
#else
#define TX_ASSUME(x)    if(!(x)) __builtin_unreachable()
#endif
#else
#define TX_ASSUME(x)    if(!(x)) __debugbreak()
#endif

#define TEXCONV_THROW_STATUS(status) do { result = status; goto Cleanup; } while (0)
#define TEXCONV_CHECK_ABORT(flag) do { if (flag) { TEXCONV_THROW_STATUS(TEXCONV_ABORT); } } while (0)


int ilog2(int x);

static unsigned int TxiRoundUpDimension(unsigned int x) {
	x = (x << 1) - 1;
	return 1 << ilog2(x); //rounds down
}

static COLOR32 *TxiPadTextureImage(COLOR32 *px, unsigned int width, unsigned int height, unsigned int *outWidth, unsigned int *outHeight) {
	//if this is a 0x0 image somehow just return an 8x8 transparent square
	if (width == 0 || height == 0) {
		*outWidth = 8;
		*outHeight = 8;
		return (COLOR32 *) calloc(8 * 8, sizeof(COLOR32));
	}

	//function imitates iMageStudio behavior
	unsigned int padWidth = TxiRoundUpDimension(width);
	unsigned int padHeight = TxiRoundUpDimension(height);
	if (padWidth < 8) padWidth = 8;
	if (padHeight < 8) padHeight = 8;

	COLOR32 *out = (COLOR32 *) calloc(padWidth * padHeight, sizeof(COLOR32));
	if (out == NULL) return NULL;

	//fill rows
	for (unsigned int y = 0; y < padHeight; y++) {
		const COLOR32 *rowSrc = px + y * width;
		COLOR32 *rowDst = out + y * padWidth;
		if (y >= height) {
			rowSrc = px + (height - 1) * width;
		}
		memcpy(rowDst, rowSrc, width * sizeof(COLOR32));

		//copy last pixel for the remainder of the width
		for (unsigned int x = width; x < padWidth; x++) {
			rowDst[x] = rowSrc[width - 1];
		}
	}

	*outWidth = padWidth;
	*outHeight = padHeight;
	return out;
}



static void TxiConvertProgressUpdate1(RxReduction *reduction, unsigned int progress, unsigned int progressMax, void *data) {
	//first half of progress update
	(void) reduction;

	TxConversionParameters *params = (TxConversionParameters *) data;
	params->progress = progress * 500 / progressMax;
	params->progressMax = 1000;
}

static void TxiConvertProgressUpdate2(RxReduction *reduction, unsigned int progress, unsigned int progressMax, void *data) {
	//second half of progress update
	(void) reduction;

	TxConversionParameters *params = (TxConversionParameters *) data;
	params->progress = 500 + progress * 500 / progressMax;
	params->progressMax = 1000;
}

static void TxiConvertProgressUpdate(RxReduction *reduction, unsigned int progress, unsigned int progressMax, void *data) {
	//full progress update
	(void) reduction;

	TxConversionParameters *params = (TxConversionParameters *) data;
	params->progress = progress * 1000 / progressMax;
	params->progressMax = 1000;
}



static int TxiConvertDirect(TxConversionParameters *params, RxReduction *reduction) {
	//convert to direct color.
	TxConversionResult result = TEXCONV_SUCCESS;
	unsigned int width = params->width, height = params->height;

	//progress
	params->progressMax = (int) (width * height);

	COLOR *txel = (COLOR *) calloc(width * height, sizeof(COLOR));
	COLOR32 *pltt = (COLOR32 *) calloc(32769, sizeof(COLOR32));
	int *idxs = (int *) calloc(width * height, sizeof(int));
	if (txel == NULL || pltt == NULL || idxs == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM);

	float diffuse = params->dither ? params->diffuseAmount : 0.0f;

	//we'll create a color palette of every 15-bit color and index the image using it.
	pltt[0] = 0; // placeholder for transparent color
	for (unsigned int i = 0; i < 32768; i++) pltt[i + 1] = ColorConvertFromDS((COLOR) i) | 0xFF000000;

	RxFlag flag = RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_NO_WRITEBACK;
	if (!params->ditherAlpha) flag |= RX_FLAG_NO_ALPHA_DITHER;      // diable alpha dither
	else                      flag |= RX_FLAG_NO_ADAPTIVE_DIFFUSE;  // disable adaptive diffusion for alpha dither

	RxApplyFlags(reduction, flag);
	RxSetProgressCallback(reduction, TxiConvertProgressUpdate, params);
	RxPaletteLoad(reduction, pltt, 32769);
	RxReduceImage(reduction, params->px, idxs, params->width, params->height, flag, diffuse);

	for (unsigned int i = 0; i < params->width * params->height; i++) {
		if (idxs[i] == 0) txel[i] = 0; // transparent
		else txel[i] = ((COLOR) (idxs[i] - 1)) | 0x8000;
	}

	//set texture parameters
	params->dest->texels.texImageParam = (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (params->fmt << 26);
	params->dest->texels.cmp = NULL;
	params->dest->palette.pal = NULL;
	params->dest->palette.nColors = 0;
	params->dest->texels.texel = (unsigned char *) txel;

Cleanup:
	free(pltt);
	free(idxs);
	if (result != TEXCONV_SUCCESS) {
		free(txel);
	}
	return result;
}

static int TxiConvertPlttN(TxConversionParameters *params, RxReduction *reduction) {
	//generate a palette ofcolors.
	TxConversionResult result = TEXCONV_SUCCESS;
	unsigned int nColors = 0, bitsPerPixel = 0;
	unsigned int width = params->width, height = params->height;
	switch (params->fmt) {
		case CT_4COLOR:
			nColors      =   4; // 4-color palette
			bitsPerPixel =   2; // 2 bits per pixel
			break;
		case CT_16COLOR:
			nColors      =  16; // 16-color palette
			bitsPerPixel =   4; // 4 bits per pixel
			break;
		case CT_256COLOR:
			nColors      = 256; // 256-color palette
			bitsPerPixel =   8; // 8 bits per pixel
			break;
		default:
			TX_ASSUME(0);
	}

	unsigned int pixelsPerByte = 8 / bitsPerPixel;
	if (params->colorEntries < nColors) nColors = params->colorEntries;

	COLOR32 palette[256] = { 0 };

	//should we reserve a color for transparent?
	int hasTransparent = !!params->c0xp;
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;

	//allocate texel space.
	unsigned int nBytes = width * height * bitsPerPixel / 8;
	uint8_t *txel = (uint8_t *) calloc(nBytes, 1);
	COLOR *pal = (COLOR *) calloc(nColors, sizeof(COLOR));
	int *idxs = (int *) calloc(width * height, sizeof(int));

	if (txel == NULL || pal == NULL || idxs == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM);

	RxFlag flag = (hasTransparent ? RX_FLAG_ALPHA_MODE_RESERVE : RX_FLAG_ALPHA_MODE_NONE);
	if (!params->ditherAlpha) flag |= RX_FLAG_NO_ALPHA_DITHER;      // diable alpha dither
	else                      flag |= RX_FLAG_NO_ADAPTIVE_DIFFUSE;  // disable adaptive diffusion for alpha dither

	RxApplyFlags(reduction, flag);

	RxSetProgressCallback(reduction, TxiConvertProgressUpdate1, params);
	if (params->fixedPalette == NULL) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		RxCreatePalette(reduction, params->px, width, height, palette + hasTransparent, nColors - hasTransparent,
			flag | RX_FLAG_SORT_ONLY_USED, NULL);
	} else {
		for (unsigned int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(params->fixedPalette[i]) | 0xFF000000;
		}
	}

	TEXCONV_CHECK_ABORT(params->terminate);

	RxSetProgressCallback(reduction, TxiConvertProgressUpdate2, params);
	RxPaletteLoad(reduction, palette, nColors);
	RxReduceImage(reduction, params->px, idxs, width, height, flag | RX_FLAG_NO_PRESERVE_ALPHA, diffuse);

	TEXCONV_CHECK_ABORT(params->terminate);

	//write texel data.
	for (unsigned int i = 0; i < width * height; i++) {
		txel[i / pixelsPerByte] |= idxs[i] << (bitsPerPixel * (i & (pixelsPerByte - 1)));
	}

	//update texture info
	unsigned int param = (params->fmt << 26) | (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (hasTransparent << 29);
	params->dest->palette.nColors = nColors;
	params->dest->palette.pal = pal;
	params->dest->texels.cmp = NULL;
	params->dest->texels.texel = txel;
	params->dest->texels.texImageParam = param;

	for (unsigned int i = 0; i < nColors; i++) {
		params->dest->palette.pal[i] = ColorConvertToDS(palette[i]);
	}

Cleanup:
	//cleanup memory
	if (result != TEXCONV_SUCCESS) {
		free(pal);
		free(txel);
	}
	free(idxs);
	return result;
}

static int TxiConvertAxIy(TxConversionParameters *params, RxReduction *reduction) {
	//convert to translucent. First, generate a palette of colors.
	TxConversionResult result = TEXCONV_SUCCESS;
	unsigned int nColors = 0, alphaShift = 0, alphaMax = 0;
	unsigned int width = params->width, height = params->height;
	switch (params->fmt) {
		case CT_A3I5:
			nColors    = 32; // 32-color
			alphaShift =  5; // alpha shift 5-bit
			alphaMax   =  7; // alpha max=7
			break;
		case CT_A5I3:
			nColors    =  8; // 8-color
			alphaShift =  3; // alpha shift 3-bit
			alphaMax   = 31; // alpha max=31
			break;
		default:
			TX_ASSUME(0);
	}

	if (params->colorEntries < nColors) nColors = params->colorEntries;
	COLOR32 palette[256] = { 0 };

	//allocate texel space.
	unsigned int nBytes = width * height;
	uint8_t *txel = (uint8_t *) calloc(nBytes, 1);
	int *idxs = (int *) calloc(width * height, sizeof(int));
	COLOR *pal = (COLOR *) calloc(nColors, sizeof(COLOR));

	if (txel == NULL || pal == NULL || idxs == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM);

	float diffuse = params->dither ? params->diffuseAmount : 0.0f;

	RxSetProgressCallback(reduction, TxiConvertProgressUpdate1, params);
	if (params->fixedPalette == NULL) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		RxCreatePalette(reduction, params->px, width, height, palette, nColors,
			RX_FLAG_SORT_ONLY_USED | RX_FLAG_ALPHA_MODE_PIXEL, NULL);
	} else {
		for (unsigned int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(params->fixedPalette[i]) | 0xFF000000;
		}
	}

	// duplicate palette data
	for (unsigned int i = 0; i <= alphaMax; i++) {
		unsigned int a = i;
		if (alphaMax == 7) a = (a << 2) | (a >> 1); // scale alpha to 5-bit
		a = (a * 510 + 31) / 62;                    // scale 5-bit alpha to 8-bit

		for (unsigned int j = 0; j < nColors; j++) {
			palette[j + (i << alphaShift)] = ((palette[j]) & 0x00FFFFFF) | (a << 24);
		}
	}

	TEXCONV_CHECK_ABORT(params->terminate);

	RxSetProgressCallback(reduction, TxiConvertProgressUpdate2, params);

	if (!params->dither || !params->ditherAlpha) {
		RxFlag flag = RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_PRESERVE_ALPHA | RX_FLAG_NO_ALPHA_DITHER;
		RxApplyFlags(reduction, flag);

		//set the alpha channel for the texel data
		for (unsigned int i = 0; i < width * height; i++) {
			unsigned int a = params->px[i] >> 24;
			a = (a * alphaMax * 2 + 255) / 510;

			//setting upper bits of the texel data
			txel[i] = a << alphaShift;
			params->px[i] |= 0xFF000000;
		}

		//when color and alpha not jointly dithered, we fall back to a simplified model.
		RxPaletteLoad(reduction, palette + (alphaMax << alphaShift), nColors);
		RxReduceImage(reduction, params->px, idxs, width, height, flag, diffuse);
	} else {
		RxFlag flag = RX_FLAG_ALPHA_MODE_PALETTE | RX_FLAG_PRESERVE_ALPHA;
		RxApplyFlags(reduction, flag);

		//dithering with alpha: use alpha dithered mode
		RxPaletteLoad(reduction, palette, 256);
		RxReduceImage(reduction, params->px, idxs, width, height, flag, diffuse);
	}

	//write texel data.
	for (unsigned int i = 0; i < width * height; i++) txel[i] |= (uint8_t) idxs[i];

	TEXCONV_CHECK_ABORT(params->terminate);

	//update texture info
	unsigned int param = (params->fmt << 26) | (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23);
	params->dest->texels.texImageParam = param;
	params->dest->palette.nColors = nColors;
	params->dest->palette.pal = pal;
	params->dest->texels.cmp = NULL;
	params->dest->texels.texel = txel;

	for (unsigned int i = 0; i < nColors; i++) {
		params->dest->palette.pal[i] = ColorConvertToDS(palette[i]);
	}

Cleanup:
	if (result != TEXCONV_SUCCESS) {
		free(pal);
		free(txel);
	}
	free(idxs);
	return result;
}


// ----- 4x4 texture compression routines

typedef struct TxTileData_ {
	COLOR32 rgb[16];       // the tile's initial RGBA color data
	COLOR32 palette32[4];  // the tile's initial color palette
	uint16_t mode;         // the tile's working palette mode
	uint16_t initMode;     // the tile's initial mode
	uint16_t paletteIndex; // the tile's working palette index
	uint16_t used;         // marks a used tile
	uint8_t nTransparent;  // number of transparent pixels
	uint8_t duplicate;     // is duplicate?
	uint32_t txel;         // indexed color data
} TxTileData;

typedef struct TxiTileErrorMpEntry_ {
	TxTileData *tile;      // pointer to tile data
	double error;          // error value for tile
} TxiTileErrorMapEntry;

typedef struct TxiConversionWork_ {
	RxReduction *reduction;          // the color reduction context
	float diffuse;                   // error diffusion amount

	TxTileData *tiles;               // the texture tile data
	TxiTileErrorMapEntry *errorMap;  // the texture tile error entries
	unsigned char *useMap;           // the texture palette usage map
	unsigned int nTiles;             // the number of total tiles

	uint32_t *txel;                  // output: texel data
	uint16_t *pidx;                  // output: palette index data
	COLOR *pltt;                     // output: texture palette data
	unsigned int plttSize;           // output: texture palette size

	volatile int *terminate;         // IPC: flag to terminate conversion
	volatile int *progress;          // IPC: current progress level
} TxiConversionWork;


//threshold for tentatively selecting an interpolated mode for a 4x4 block based on mean square
//error. Calculated as about the max squared error of rounding a color to its nearest representable
//color, and dividing by the sum of squared channel weights.
#define TXC_BLOCK_INTERP_THRESHOLD     53.0


//TxiBlend18 two colors together by weight. (out of 8)
static COLOR32 TxiBlend18(COLOR32 col1, unsigned int weight1, COLOR32 col2, unsigned int weight2) {
	if (col1 == col2) return col1;
	unsigned int r1 = (col1 >>  0) & 0xFF, r2 = (col2 >>  0) & 0xFF;
	unsigned int g1 = (col1 >>  8) & 0xFF, g2 = (col2 >>  8) & 0xFF;
	unsigned int b1 = (col1 >> 16) & 0xFF, b2 = (col2 >> 16) & 0xFF;
	unsigned int r3 = (r1 * weight1 + r2 * weight2 + 4) / 8;
	unsigned int g3 = (g1 * weight1 + g2 * weight2 + 4) / 8;
	unsigned int b3 = (b1 * weight1 + b2 * weight2 + 4) / 8;
	return ColorRoundToDS18(r3 | (g3 << 8) | (b3 << 16)) | 0xFF000000;
}

//RGB to YIQ, for only Y channel
static double TxiYFromRGB(COLOR32 rgb) {
	RxYiqColor yiq;
	RxConvertRgbToYiq(rgb, &yiq);
	return yiq.y;
}

static int TxiCreatePaletteFromHistogram(
	RxReduction *reduction,
	unsigned int nColors,
	COLOR32     *out
) {
	RxComputePalette(reduction, nColors);

	//extract created palette
	unsigned int nUsed = reduction->nUsedColors;
	for (unsigned int i = 0; i < nColors; i++) {
		if (i < nUsed) out[i] = reduction->paletteRgb[i][0];
		else           out[i] = 0xFF000000;
	}

	qsort(out, nColors, sizeof(COLOR32), RxColorLightnessComparator);
	return nUsed;
}

static double TxiComputeInterpolatedError(
	RxReduction *reduction,
	COLOR        c1,
	COLOR        c2,
	RxBool       transparentMode,
	double       maxError
) {
	//expand palette
	COLOR32 col0 = ColorConvertFromDS(c1) | 0xFF000000;
	COLOR32 col1 = ColorConvertFromDS(c2) | 0xFF000000;
	COLOR32 col2 = 0xFF000000, col3 = 0xFF000000;
	if (!transparentMode) {
		col2 = TxiBlend18(col0, 3, col1, 5);
		col3 = TxiBlend18(col0, 5, col1, 3);
	} else {
		col2 = TxiBlend18(col0, 4, col1, 4);
	}
	int nColors = 3 + !transparentMode;
	COLOR32 palette[] = { col0, col1, col2, col3 };

	return RxHistComputePaletteError(reduction, palette, nColors, maxError);
}

static double TxiTestAddEndpoints(
	RxReduction *reduction,
	RxBool       transparentMode,
	COLOR       *pc1,
	COLOR       *pc2,
	int          amt,
	int          cshift,
	double       error
) {
	//try adding to color 1
	int channel = (*pc1 >> cshift) & 0x1F;
	if ((amt < 0 && channel >= -amt) || (amt > 0 && channel <= 31 - amt)) { //check for over/underflows
		*pc1 -= (amt << cshift);
		double err2 = TxiComputeInterpolatedError(reduction, *pc1, *pc2, transparentMode, error);
		if (err2 < error) {
			error = err2;
		} else {
			*pc1 += (amt << cshift);
		}
	}

	//now try adding to color 2
	channel = (*pc2 >> cshift) & 0x1F;
	if ((amt < 0 && channel >= -amt) || (amt > 0 && channel <= 31 - amt)) { //check for over/underflows
		*pc2 -= (amt << cshift);
		double err2 = TxiComputeInterpolatedError(reduction, *pc1, *pc2, transparentMode, error);
		if (err2 < error) {
			error = err2;
		} else {
			*pc2 += (amt << cshift);
		}
	}

	//whatever error we settled on...
	return error;
}

static double TxiTestStepEndpoints(
	RxReduction *reduction,
	RxBool       transparentMode,
	COLOR       *c1,
	COLOR       *c2,
	int          channel,
	double       error
) {
	TX_ASSUME(channel == COLOR_CHANNEL_R || channel == COLOR_CHANNEL_G || channel == COLOR_CHANNEL_B);

	//test adding 1 to the channel, then test subtracting if adding did not decrease error.
	double newErr = TxiTestAddEndpoints(reduction, transparentMode, c1, c2, 1, 5 * channel, error); // add
	if (newErr < error) {
		error = newErr;
	} else {
		error = TxiTestAddEndpoints(reduction, transparentMode, c1, c2, -1, 5 * channel, error);   // subtract
	}
	return error;
}

static void TxiComputeEndpointsFromHistogram(
	RxReduction *reduction,
	RxBool       transparentMode,
	COLOR32     *colorMin,
	COLOR32     *colorMax
) {
	//if only 1 or 2 colors, fill the palette with those.
	COLOR32 colors[2];
	int nColors = 0;
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		COLOR32 col = RxConvertYiqToRgb(&reduction->histogramFlat[i]->color[0]);

		//round to 15-bit color for counting
		col = ColorRoundToDS15(col) | 0xFF000000;

		//count up to 2 unique colors in this block.
		if (nColors == 0) {
			colors[nColors++] = col;
		} else if (nColors == 1 && col != colors[0]) {
			colors[nColors++] = col;
		} else if (nColors == 2 && col != colors[0] && col != colors[1]) {
			nColors++;
			break;
		}
	}

	if (nColors <= 2) {
		if (nColors == 0) {
			//no opaque colors this block: flil black.
			*colorMin = 0xFF000000;
			*colorMax = 0xFF000000;
		} else if (nColors == 1) {
			//one opaque color: fill palette with one color
			*colorMin = colors[0];
			*colorMax = colors[0];
		} else {
			//two colors: sort the two colors such that the lighter one is first.
			if (TxiYFromRGB(colors[0]) > TxiYFromRGB(colors[1])) {
				*colorMin = colors[1];
				*colorMax = colors[0];
			} else {
				*colorMin = colors[0];
				*colorMax = colors[1];
			}
		}
		return;
	}

	//use principal component analysis to determine endpoints.
	//choose first and last colors along the principal axis (greatest Y is at the end)
	RxHistSort(reduction, 0, reduction->histogram->nEntries);
	RxHistEntry *firstEntry = reduction->histogramFlat[0];
	RxHistEntry *lastEntry = reduction->histogramFlat[reduction->histogram->nEntries - 1];

	COLOR32 full1 = RxConvertYiqToRgb(&firstEntry->color[0]);
	COLOR32 full2 = RxConvertYiqToRgb(&lastEntry->color[0]);

	//round to nearest colors.
	COLOR c1 = ColorConvertToDS(full1);
	COLOR c2 = ColorConvertToDS(full2);

	//try out varying the RGB values. Start G, then R, then B. Do this a few times.
	double error = TxiComputeInterpolatedError(reduction, c1, c2, transparentMode, 1e32);
	for (int i = 0; i < 10; i++) {
		COLOR old1 = c1, old2 = c2;
		error = TxiTestStepEndpoints(reduction, transparentMode, &c1, &c2, COLOR_CHANNEL_G, error);
		error = TxiTestStepEndpoints(reduction, transparentMode, &c1, &c2, COLOR_CHANNEL_R, error);
		error = TxiTestStepEndpoints(reduction, transparentMode, &c1, &c2, COLOR_CHANNEL_B, error);

		//early breakout check: are we doing anything?
		if (old1 == c1 && old2 == c2) break;
	}

	//sanity check: impose color ordering (high Y must come first)
	full1 = ColorConvertFromDS(c1);
	full2 = ColorConvertFromDS(c2);
	if (TxiYFromRGB(full2) > TxiYFromRGB(full1)) {
		//swap order to keep me sane
		COLOR32 temp = full2;
		full2 = full1;
		full1 = temp;
	}
	*colorMin = full2 | 0xFF000000;
	*colorMax = full1 | 0xFF000000;
}

//compute mean square error
static double TxiComputeMSE(
	RxReduction   *reduction,
	const COLOR32 *palette,
	unsigned int   nTransparent
) {
	unsigned int nPlttColors = 4 - (nTransparent > 0);
	unsigned int nOpaquePixel = 16 - nTransparent;
	if (nOpaquePixel == 0) return 0;

	//take the sum of square errors and normalize by the channel weighting norm
	double sse = RxHistComputePaletteError(reduction, palette, nPlttColors, 1e32);
	return (sse / (reduction->yWeight2 + reduction->iWeight2 + reduction->qWeight2)) / reduction->histogram->totalWeight;
}

static void TxiChoosePaletteAndMode(
	RxReduction *reduction,
	TxTileData  *tile
) {
	//add pixels to histogram
	RxHistClear(reduction);
	RxHistAdd(reduction, tile->rgb, 4, 4);
	RxHistFinalize(reduction);

	//first try interpolated. If it's not good enough, use full color.
	COLOR32 colorMin, colorMax;
	TxiComputeEndpointsFromHistogram(reduction, tile->nTransparent > 0, &colorMin, &colorMax);

	if (tile->nTransparent > 0) {
		COLOR32 mid = TxiBlend18(colorMin, 4, colorMax, 4);
		COLOR32 palette[] = { colorMax, mid, colorMin, 0 };
		COLOR32 paletteFull[4];

		double error = TxiComputeMSE(reduction, palette, 1);
		int nFull = TxiCreatePaletteFromHistogram(reduction, 3, paletteFull);

		//if error <= thresh, then these colors are good enough
		if (error <= TXC_BLOCK_INTERP_THRESHOLD || nFull <= 2) {
			tile->palette32[0] = colorMax;
			tile->palette32[1] = colorMin;
			tile->palette32[2] = 0xFF000000;
			tile->palette32[3] = 0xFF000000;
			tile->mode = COMP_TRANSPARENT | COMP_INTERPOLATE;
		} else {
			//swap index 3 and 0, 2 and 1
			tile->palette32[0] = paletteFull[2]; // entry 3 empty, double up entry 2
			tile->palette32[1] = paletteFull[1];
			tile->palette32[2] = paletteFull[2];
			tile->palette32[3] = paletteFull[0];
			tile->mode = COMP_TRANSPARENT | COMP_FULL;
		}
	} else {
		COLOR32 mid1 = TxiBlend18(colorMin, 5, colorMax, 3);
		COLOR32 mid2 = TxiBlend18(colorMin, 3, colorMax, 5);
		COLOR32 palette[] = { colorMax, mid2, mid1, colorMin };
		COLOR32 paletteFull[4];

		double error = TxiComputeMSE(reduction, palette, 0);
		int nFull = TxiCreatePaletteFromHistogram(reduction, 4, paletteFull);

		if (error <= TXC_BLOCK_INTERP_THRESHOLD || nFull <= 2) {
			tile->palette32[0] = colorMax;
			tile->palette32[1] = colorMin;
			tile->palette32[2] = 0xFF000000;
			tile->palette32[3] = 0xFF000000;
			tile->mode = COMP_OPAQUE | COMP_INTERPOLATE;
		} else {
			//swap index 3 and 0, 2 and 1
			if (nFull < 4) paletteFull[0] = paletteFull[1];
			tile->palette32[0] = paletteFull[3];
			tile->palette32[1] = paletteFull[1];
			tile->palette32[2] = paletteFull[2];
			tile->palette32[3] = paletteFull[0];
			tile->mode = COMP_OPAQUE | COMP_FULL;
		}
	}
}

static unsigned int TxiGetPaletteSizeForMode(int type) {
	if (type & COMP_INTERPOLATE) return 2;
	return 4;
}

static void TxiAddTile(
	TxiConversionWork *work,
	unsigned int       index,
	const COLOR32     *pxBlock,
	RxBool             createPalette,
	unsigned int      *pPlttIndex
) {
	TxTileData *tile = &work->tiles[index];
	tile->duplicate = 0;
	tile->used = 1;
	tile->nTransparent = 0;
	tile->mode = 0;
	tile->paletteIndex = 0;

	//fill and count transparent pixels
	for (unsigned int i = 0; i < 16; i++) {
		COLOR32 c = pxBlock[i];
		unsigned int a = (c >> 24);
		if (a < 0x80) {
			tile->nTransparent++;
			tile->rgb[i] = 0; // set alpha=0 (under threshold)
		} else {
			tile->rgb[i] = c | 0xFF000000; // set alpha=1 (over threshold)
		}
	}

	//is fully transparent?
	if (tile->nTransparent == 16) {
		//put dummy data for fully transparent tile
		tile->used = 0;
		tile->paletteIndex = 0;
		tile->mode = COMP_TRANSPARENT | COMP_FULL;
		tile->palette32[0] = 0xFF000000;
		tile->palette32[1] = 0xFF000000;
		return;
	}
	
	//is it a duplicate?
	for (unsigned int i = index; i > 0; i--) {
		COLOR32 *px1 = work->tiles[i - 1].rgb;

		if (!memcmp(px1, tile->rgb, 16 * sizeof(COLOR32))) {
			//tile pixels are duplicate
			memcpy(tile, &work->tiles[i - 1], sizeof(TxTileData));
			tile->paletteIndex = work->tiles[i - 1].paletteIndex;
			tile->duplicate = 1;
			return;
		}
	}

	if (createPalette) {
		//generate a palette and determine the mode.
		TxiChoosePaletteAndMode(work->reduction, tile);
		tile->paletteIndex = *pPlttIndex;

		//is the palette and mode identical to a non-duplicate tile?
		for (unsigned int i = index; i > 0; i--) {
			TxTileData *tile1 = &work->tiles[i - 1];
			if (tile1->duplicate) continue;
			if (tile1->mode != tile->mode) continue;

			if (tile1->palette32[0] != tile->palette32[0] || tile1->palette32[1] != tile->palette32[1]) continue;
			if (!(tile1->mode & COMP_INTERPOLATE)) {
				if (tile1->palette32[2] != tile->palette32[2] || tile1->palette32[3] != tile->palette32[3]) continue;
			}

			//palettes and modes are the same, mark as duplicate.
			tile->duplicate = 1;
			tile->paletteIndex = tile1->paletteIndex;
			return;
		}
	} else {
		//do not create a palette.
		tile->paletteIndex = 0;
		tile->mode = COMP_FULL | COMP_TRANSPARENT;
	}

	//reaching here, the tile is not marked as duplicate, so increment the palette index.
	*pPlttIndex += TxiGetPaletteSizeForMode(tile->mode) / 2;
}

static void TxiCreateTileData(
	TxiConversionWork *work,
	const COLOR32     *px,
	unsigned int       tilesX,
	unsigned int       tilesY,
	RxBool             createPalette
) {
	//initialize the tile info for each pixel block in the image
	unsigned int paletteIndex = 0, i = 0;
	for (unsigned int y = 0; y < tilesY; y++) {
		for (unsigned int x = 0; x < tilesX; x++) {
			unsigned int offs = x * 4 + y * 4 * tilesX * 4;

			COLOR32 pxBlock[16];
			memcpy(pxBlock +  0, px + offs + tilesX *  0, 4 * sizeof(COLOR32));
			memcpy(pxBlock +  4, px + offs + tilesX *  4, 4 * sizeof(COLOR32));
			memcpy(pxBlock +  8, px + offs + tilesX *  8, 4 * sizeof(COLOR32));
			memcpy(pxBlock + 12, px + offs + tilesX * 12, 4 * sizeof(COLOR32));

			TxiAddTile(work, i, pxBlock, createPalette, &paletteIndex);

			work->tiles[i].initMode = work->tiles[i].mode;
			i++;

			(*work->progress)++;
			if (*work->terminate) return; // terminate check
		}
	}
}

static double TxiComputePaletteDifference(
	RxReduction      *reduction,
	const RxYiqColor *pltt1,
	const RxYiqColor *pltt2,
	unsigned int      nPltt,
	double            maxError
) {
	TX_ASSUME(nPltt == 2 || nPltt == 4);

	double total = 0.0, errScale = 1.0, errInvScale = 1.0;
	if (nPltt == 2) {
		//2-color palettes have doubled error (to be comparable to 4-color errors)
		errScale = 2.0;
		errInvScale = 0.5;
		maxError *= errInvScale;
	}
	
	for (unsigned int i = 0; i < nPltt; i++) {
		const RxYiqColor *yiq1 = &pltt1[i], *yiq2 = &pltt2[i];
#ifndef RX_SIMD
		double dy = yiq1->y - yiq2->y;
		double di = yiq1->i - yiq2->i;
		double dq = yiq1->q - yiq2->q;
		total += reduction->yWeight2 * (dy * dy) + reduction->iWeight2 * (di * di) + reduction->qWeight2 * (dq * dq);
#else
		__m128 diff = _mm_sub_ps(yiq1->yiq, yiq2->yiq);
		diff = _mm_mul_ps(diff, diff);
		diff = _mm_mul_ps(diff, reduction->yiqaWeight2);
		diff = _mm_add_ps(diff, _mm_shuffle_ps(diff, diff, _MM_SHUFFLE(2, 3, 0, 1)));
		diff = _mm_add_ss(diff, _mm_movehl_ps(diff, diff));
		total += _mm_cvtss_f32(diff);
#endif

		if (total >= maxError) return maxError * errScale;
	}

	//for 2-color (interpolated) palettes, we'll double the difference to scale to 4 colors.
	return total * errScale;
}

static double TxiFindClosestPalettes(
	RxReduction      *reduction,
	const RxYiqColor *pltt,
	const int        *colorTable,
	unsigned int      nColors,
	int              *colorIndex1,
	int              *colorIndex2
) {
	//determine which two palettes are the most similar. For 2-color palettes, multiply difference by 2.
	double leastDistance = 1e32;
	unsigned int idx1 = 0;

	while (idx1 < nColors) {
		int type1 = colorTable[idx1];
		unsigned int nColorsInThisPalette = 4;
		if (type1 & COMP_INTERPOLATE) nColorsInThisPalette = 2;

		//start searching forward.
		unsigned int idx2 = idx1 + nColorsInThisPalette;
		while (idx2 + nColorsInThisPalette <= nColors) {
			int type2 = colorTable[idx2];
			int nColorsInSecondPalette = TxiGetPaletteSizeForMode(type2);

			if (type2 != type1) {
				idx2 += nColorsInSecondPalette;
				continue;
			}

			//same type, let's compare.
			double dst = TxiComputePaletteDifference(reduction, &pltt[idx1], &pltt[idx2], nColorsInThisPalette, leastDistance);
			if (dst < leastDistance) {
				leastDistance = dst;
				*colorIndex1 = idx1;
				*colorIndex2 = idx2;
				if (leastDistance == 0.0) return 0.0;
			}

			idx2 += nColorsInThisPalette;
		}
		idx1 += nColorsInThisPalette;

	}
	return leastDistance;
}

static void TxiMergePalettes(
	TxiConversionWork *work,
	RxYiqColor        *palette,
	unsigned int       paletteIndex,
	uint16_t           mode
) {
	//create histogram
	RxHistClear(work->reduction);
	for (unsigned int i = 0; i < work->nTiles; i++) {
		if (work->tiles[i].paletteIndex == paletteIndex && work->tiles[i].used) {
			RxHistAdd(work->reduction, work->tiles[i].rgb, 4, 4);
		}
	}
	RxHistFinalize(work->reduction);

	//use the mode to determine the appropriate method of creating the palette.
	COLOR32 expandPal[4];
	RxYiqColor *yiqPalette = &palette[paletteIndex * 2];
	if (mode == (COMP_TRANSPARENT | COMP_FULL)) {
		//transparent, full color
		TxiCreatePaletteFromHistogram(work->reduction, 3, expandPal + 1);

		RxConvertRgbToYiq(expandPal[2], &yiqPalette[0]); // don't waste this slot
		RxConvertRgbToYiq(expandPal[1], &yiqPalette[1]);
		RxConvertRgbToYiq(expandPal[2], &yiqPalette[2]);
		RxConvertRgbToYiq(expandPal[0], &yiqPalette[3]);
	} else if (mode & COMP_INTERPOLATE) {
		//transparent, interpolated, and opaque, interpolated
		TxiComputeEndpointsFromHistogram(work->reduction, !!(mode & COMP_TRANSPARENT), &expandPal[0], &expandPal[1]);

		RxConvertRgbToYiq(expandPal[1], &yiqPalette[0]);
		RxConvertRgbToYiq(expandPal[0], &yiqPalette[1]);
	} else if (mode == (COMP_OPAQUE | COMP_FULL)) {
		//opaque, full color
		int nFull = TxiCreatePaletteFromHistogram(work->reduction, 4, expandPal);

		if (nFull < 4) expandPal[0] = expandPal[1];
		RxConvertRgbToYiq(expandPal[3], &yiqPalette[0]);
		RxConvertRgbToYiq(expandPal[1], &yiqPalette[1]);
		RxConvertRgbToYiq(expandPal[2], &yiqPalette[2]);
		RxConvertRgbToYiq(expandPal[0], &yiqPalette[3]);
	}
}

static unsigned int TxiBuildCompressedPalette(
	TxiConversionWork *work,
	int                threshold
) {
	//the main palette loop must be able to accommadate at least 16 colors. If the requested palette
	//size is below this, we will still make a best effort, truncating if needed.
	//16 colors is the sum of sizes of all 4 types (4+4+2+2) plus the largest size again (+4).
	unsigned int outPlttSize = work->plttSize;
	unsigned int plttSize = outPlttSize;
	if (plttSize < 16) plttSize = 16;

	RxYiqColor *plttYiq = (RxYiqColor *) RxMemCalloc(plttSize, sizeof(RxYiqColor));
	int *colorTable = (int *) calloc(plttSize, sizeof(int));
	
	RxReduction *reduction = work->reduction;
	TxTileData *tiles = work->tiles;
	unsigned int nTiles = work->nTiles;

	//user-specified threshold, normalized to correspond to a half (squared) difference in Y value.
	double diffThreshold = (threshold * threshold) * work->reduction->yWeight2 * (255.0 * 4.0 / 10000.0);

	//iterate over all non-duplicate tiles, adding the palettes.
	unsigned int availableSlot = 0;
	for (unsigned int i = 0; i < nTiles; i++) {
		TxTileData *tile = &tiles[i];
		if (tile->duplicate || !tile->used) {
			//the paletteIndex field of a duplicate tile is first set to the tile index it is a duplicate of.
			(*work->progress)++;
			continue;
		}

		//how many color entries does this consume?
		unsigned int nConsumed = TxiGetPaletteSizeForMode(tile->mode);  // number of colors required by the added palette

		//palette merge loop: merge palettes while either the colors to be added do not fit, or palettes
		//are within merge threshold.
		while ((availableSlot + nConsumed) > outPlttSize || threshold > 0) {
			//determine which two palettes are the most similar.
			int colorIndex1 = -1, colorIndex2 = -1;
			double distance = TxiFindClosestPalettes(work->reduction, plttYiq, colorTable, availableSlot, &colorIndex1, &colorIndex2);
			if (colorIndex1 == -1) break;
			if ((availableSlot + nConsumed) <= outPlttSize && distance > diffThreshold) break;

			uint16_t palettesMode = colorTable[colorIndex1];
			unsigned int nColsRemove = TxiGetPaletteSizeForMode(palettesMode);

			//find tiles that use colorIndex2. Set them to use colorIndex1. 
			//then subtract from all palette indices > colorIndex2. Then we can
			//shift over all the palette colors. Then regenerate the palette.
			for (unsigned int j = 0; j < nTiles; j++) {
				if (tiles[j].paletteIndex == colorIndex2 / 2) {
					tiles[j].paletteIndex = colorIndex1 / 2;
				} else if (tiles[j].paletteIndex > colorIndex2 / 2) {
					tiles[j].paletteIndex -= nColsRemove / 2;
				}
			}

			//move entries in palette and colorTable.
			unsigned int nToShift = plttSize - colorIndex2 - nColsRemove;
			memmove(plttYiq + colorIndex2, plttYiq + colorIndex2 + nColsRemove, nToShift * sizeof(RxYiqColor));
			memmove(colorTable + colorIndex2, colorTable + colorIndex2 + nColsRemove, nToShift * sizeof(int));

			//merge those palettes that we've just combined.
			TxiMergePalettes(work, plttYiq, colorIndex1 / 2, palettesMode);
			availableSlot -= nColsRemove;
		}

		//now add this tile's colors
		for (unsigned int j = 0; j < nConsumed; j++) {
			RxConvertRgbToYiq(tile->palette32[j], &plttYiq[availableSlot + j]);
			colorTable[availableSlot + j] = tile->mode;
		}
		tile->paletteIndex = availableSlot / 2;
		availableSlot += nConsumed;

		(*work->progress)++;
		if (*work->terminate) goto Done;
	}
Done:
	free(colorTable);

	//copy palette out
	if (plttYiq != NULL) {
		for (unsigned int i = 0; i < outPlttSize; i++) {
			work->pltt[i] = ColorConvertToDS(RxConvertYiqToRgb(&plttYiq[i]));
		}
		RxMemFree(plttYiq);
	}

	//if the output palette data was less than the internal buffer size, we reassign palette
	//indices to something in bounds (tiles will be re-indexed later anyway).
	if (availableSlot > outPlttSize) {
		availableSlot = outPlttSize;

		for (unsigned int i = 0; i < nTiles; i++) {
			unsigned int plttAddr = tiles[i].paletteIndex * 2;
			unsigned int nPlttUse = 4;
			if (tiles[i].mode & COMP_INTERPOLATE) nPlttUse = 2;

			if ((plttAddr + nPlttUse) > outPlttSize) {
				//dummy index and mode
				tiles[i].paletteIndex = 0;
				tiles[i].mode = COMP_INTERPOLATE | COMP_TRANSPARENT;
			}
		}
	}

	return availableSlot;
}

static void TxiExpandPalette32(
	const COLOR32 *pltt,
	uint16_t       mode,
	COLOR32       *dest,
	unsigned int  *pnPltt
) {
	dest[0] = pltt[0];
	dest[1] = pltt[1];

	if (mode & COMP_OPAQUE) *pnPltt = 4;
	else *pnPltt = 3;

	switch (mode & COMP_MODE_MASK) {
		case COMP_OPAQUE | COMP_FULL:
			dest[2] = pltt[2];
			dest[3] = pltt[3];
			break;
		case COMP_TRANSPARENT | COMP_FULL:
			dest[2] = pltt[2];
			dest[3] = 0;
			break;
		case COMP_TRANSPARENT | COMP_INTERPOLATE:
			dest[2] = TxiBlend18(dest[0], 4, dest[1], 4) | 0xFF000000;
			dest[3] = 0;
			break;
		case COMP_OPAQUE | COMP_INTERPOLATE:
			dest[2] = TxiBlend18(dest[0], 5, dest[1], 3) | 0xFF000000;
			dest[3] = TxiBlend18(dest[0], 3, dest[1], 5) | 0xFF000000;
			break;
		default:
			TX_ASSUME(0);
	}
}

static void TxiExpandPalette(
	const COLOR  *pltt,
	uint16_t      mode,
	COLOR32      *dest,
	unsigned int *pnPltt
) {
	//convert 2 to 4 colors to 32-bit
	COLOR32 nnsPal32[4];
	nnsPal32[0] = ColorConvertFromDS(pltt[0]) | 0xFF000000;
	nnsPal32[1] = ColorConvertFromDS(pltt[1]) | 0xFF000000;
	if (!(mode & COMP_INTERPOLATE)) {
		nnsPal32[2] = ColorConvertFromDS(pltt[2]) | 0xFF000000;
		if (mode & COMP_OPAQUE) {
			nnsPal32[3] = ColorConvertFromDS(pltt[3]) | 0xFF000000;
		}
	}

	TxiExpandPalette32(nnsPal32, mode, dest, pnPltt);
}

static double TxiComputeTilePidxError(
	TxiConversionWork *work,
	const COLOR32     *px,
	uint16_t           mode,
	double             maxError
) {
	unsigned int nOpaque;
	COLOR32 effPltt[4];
	TxiExpandPalette(work->pltt + COMP_INDEX(mode), mode, effPltt, &nOpaque);
	return RxComputePaletteError(work->reduction, px, 4, 4, effPltt, nOpaque, maxError);
}

static uint16_t TxiFindOptimalPidx(
	TxiConversionWork *work,
	const TxTileData  *tile,
	unsigned int       nColors,
	unsigned int       startIdx,
	double            *pError
) {
	const COLOR32 *px = tile->rgb;
	int hasTransparent = tile->nTransparent;

	//start with default values
	uint16_t leastPidx = tile->mode | tile->paletteIndex;
	double leastError = TxiComputeTilePidxError(work, px, leastPidx, 1e32);
	if (tile->nTransparent == 16 || leastError == 0.0) {
		//if the tile is fully transparent or has no quantization error, no search is needed
		return leastPidx;
	}

	//yes, iterate over every possible palette and mode.
	for (unsigned int i = startIdx; i < nColors; i += 2) {
		for (unsigned int j = 0; j < 4; j++) {
			//check that we don't run off the end of the palette
			unsigned int nConsumed = 2;
			if (j == 0 || j == 2) nConsumed = 4;
			if ((i + nConsumed) > nColors) continue;

			//nothing to gain from these modes sometimes
			if (!hasTransparent && j == 0) continue;
			if (hasTransparent && j >= 2) break;
			
			uint16_t mode = (j << 14) | (i >> 1);
			double dst = TxiComputeTilePidxError(work, px, mode, leastError);
			if (dst < leastError) {
				leastPidx = mode;
				leastError = dst;
			}
		}
	}

	if (pError != NULL) *pError = leastError;
	return leastPidx;
}

static int TxiErrorMapComparator(const void *p1, const void *p2) {
	double e1 = ((TxiTileErrorMapEntry *) p1)->error;
	double e2 = ((TxiTileErrorMapEntry *) p2)->error;

	double diff = e1 - e2;
	if (diff < 0) return  1;
	if (diff > 0) return -1;
	return 0;
}

static TxiTileErrorMapEntry *TxiGetGreatestErrorTile(TxiConversionWork *work) {
	qsort(work->errorMap, work->nTiles, sizeof(TxiTileErrorMapEntry), TxiErrorMapComparator);

	//if the first entry has 0 error, all tiles are matched
	if (work->nTiles == 0 || work->errorMap[0].error == 0.0) {
		return NULL;
	}
	return &work->errorMap[0];
}

static void TxiIndexTile(
	TxiConversionWork *work,
	TxTileData        *tile,
	const COLOR32     *effPltt,
	unsigned int       nEffPltt,
	unsigned int       baseIndex
) {
	int idxbuf[16];
	RxPaletteLoad(work->reduction, effPltt, nEffPltt);
	RxReduceImage(work->reduction, tile->rgb, idxbuf, 4, 4, RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_NO_WRITEBACK, work->diffuse);

	uint32_t texel = 0;
	for (unsigned int j = 0; j < 16; j++) {
		unsigned int index = 0;
		COLOR32 col = tile->rgb[j];
		if ((col >> 24) < 0x80) {
			index = 3;
		} else {
			index = idxbuf[j] + baseIndex;
		}
		texel |= index << (j * 2);
	}
	tile->txel = texel;
}

static void TxiIndexTilesByPalette(TxiConversionWork *work) {
	for (unsigned int i = 0; i < work->nTiles; i++) {
		//double check that these settings are the most optimal for this tile.
		double err = 0.0;
		uint16_t idx = TxiFindOptimalPidx(work, &work->tiles[i], work->plttSize, 0, &err);
		uint16_t mode  = idx & COMP_MODE_MASK;
		uint16_t index = idx & COMP_INDEX_MASK;
		const COLOR *thisPalette = work->pltt + (index << 1);

		work->tiles[i].mode = mode;
		work->tiles[i].paletteIndex = index;

		COLOR32 palette[4];
		unsigned int paletteSize;
		TxiExpandPalette(thisPalette, mode, palette, &paletteSize);

		//store palette error
		work->errorMap[i].tile = &work->tiles[i];
		work->errorMap[i].error = err;

		//index this tile
		TxiIndexTile(work, &work->tiles[i], palette, paletteSize, 0);

		(*work->progress)++;

		//check termination
		if (*work->terminate) break;
	}
}

static void TxiAccountColor(
	unsigned char *useMap,
	uint16_t       pidx,
	unsigned int   cindex
) {
	unsigned int pindex = COMP_INDEX(pidx);

	//transparent pixel ignore
	if (!(pidx & COMP_OPAQUE) && cindex == 3) return;

	//check interpolation
	if (pidx & COMP_INTERPOLATE) {
		//color slots 0 and 1 mark those colors, 2 and 3 mark both
		if (cindex == 0 || cindex == 1) {
			useMap[pindex + cindex] = 1;
		} else {
			useMap[pindex + 0] = 1;
			useMap[pindex + 1] = 1;
		}
	} else {
		//mark color used
		useMap[pindex + cindex] = 1;
	}
}

static void TxiAccountColors(TxiConversionWork *work) {
	//clear the account buffer
	memset(work->useMap, 0, work->plttSize);

	//account palette colors from each block
	for (unsigned int i = 0; i < work->nTiles; i++) {
		uint32_t thisTexel = work->tiles[i].txel;
		uint16_t thisIndex = work->tiles[i].mode | work->tiles[i].paletteIndex;

		//each pixel of tile
		for (unsigned int j = 0; j < 16; j++) {
			unsigned int cindex = (thisTexel >> (j * 2)) & 3;
			TxiAccountColor(work->useMap, thisIndex, cindex);
		}
	}
}

static void TxiRefineRemapColors(
	TxTileData  *tile,
	unsigned int to0,
	unsigned int to1,
	unsigned int to2,
	unsigned int to3
) {
	TX_ASSUME(to0 < 4);
	TX_ASSUME(to1 < 4);
	TX_ASSUME(to2 < 4);
	TX_ASSUME(to3 < 4);

	//create array for easy lookup
	unsigned int to[] = { to0, to1, to2, to3 };

	//for transparent mode, avoid remapping color index 3
	if (!(tile->mode & COMP_OPAQUE)) to[3] = 3;

	//remap colors from the texel block
	uint32_t newval = 0;
	for (unsigned int i = 0; i < 16; i++) {
		unsigned int c = (tile->txel >> (2 * i)) & 3;
		newval |= to[c] << (2 * i);
	}

	tile->txel = newval;
}

static void TxiRefineCoalesceDown(TxiConversionWork *work) {
	//we'll enter a pass where we try to coalesce palette indices down. This helps to
	//free up redundant sections of the palette, making higher-index colors unused.
	for (unsigned int i = 0; i < work->nTiles; i++) {
		uint32_t texPtn = work->tiles[i].txel;
		uint16_t mode = work->tiles[i].mode;
		unsigned int idx = work->tiles[i].paletteIndex << 1;

		const COLOR *currCols = work->pltt + idx;

		//map the used colors
		unsigned char usedCols[4] = { 0 };
		for (unsigned int j = 0; j < 16; j++) {
			TxiAccountColor(usedCols, mode & COMP_MODE_MASK, (texPtn >> (j * 2)) & 3);
		}

		//search for an appearance of the colors used by this block.
		for (unsigned int j = 0; j < idx; j += 2) {
			//we may compare 4 colors without checking the bounds, since our index is below a valid one.
			int diff = 0;
			for (unsigned int k = 0; k < 4; k++) {
				//compare only those colors that are used
				if (usedCols[k] && (currCols[k] != work->pltt[j + k])) {
					diff = 1;
					break;
				}
			}

			if (!diff) {
				//matching colors found, repoint the palette index
				work->tiles[i].paletteIndex = j >> 1;
				break;
			}
		}
	}
}

static void TxiRefineCoalesceSingleColor(TxiConversionWork *work) {
	//search for tiles using interpolation mode and a palette with both identical colors. We try to
	//find this one color represented in another palette (that isn't duplicating this color), with
	//the hope that we may remove the single-color palette.
	for (unsigned int i = 0; i < work->nTiles; i++) {
		uint16_t mode = work->tiles[i].mode;
		if (!(mode & COMP_INTERPOLATE)) continue;

		const COLOR *tilePltt = work->pltt + (work->tiles[i].paletteIndex << 1);
		if (tilePltt[0] == tilePltt[1]) {
			COLOR findCol = tilePltt[0];

			//search for an instance of the color somewhere else
			for (unsigned int j = 0; (j + 1) < work->plttSize; j += 2) {
				if (tilePltt[j] != findCol && tilePltt[j + 1] != findCol) continue; // must contain the search color
				if (tilePltt[j] == tilePltt[j + 1]) continue;                       // must not be doubled color

				//found, map any opaque color index to the found slot.
				int iCol = (tilePltt[j + 1] == findCol);            // 0 or 1 color index
				TxiRefineRemapColors(&work->tiles[i], iCol, iCol, iCol, iCol);

				work->tiles[i].paletteIndex = j >> 1;
				break;
			}
		}
	}
}

static void TxiRefineReduceColorPairs(TxiConversionWork *work) {
	//search for adjacent identical color pairs. These can always be reduced.
	unsigned int pairScanLength = work->plttSize;
	for (unsigned int i = 0; i < (pairScanLength - 2); i += 2) {
		COLOR *pair1 = work->pltt + i;
		COLOR *pair2 = work->pltt + i + 2;
		if (pair1[0] == pair2[0] && pair1[1] == pair2[1]) {

			//pair match. Cut the second appearance and make adjustments.
			for (unsigned int j = 0; j < work->nTiles; j++) {
				unsigned int idx = work->tiles[j].paletteIndex << 1;

				//when the index is equal to the index of the first appearance, its index is kept but the texels may
				//need to be adjusted.
				if (idx == i && !(work->tiles[j].mode & COMP_INTERPOLATE)) {
					//force pixel values of 2,3 to 0,1.
					TxiRefineRemapColors(&work->tiles[j], 0, 1, 0, 1);
				}

				if (idx > i) work->tiles[j].paletteIndex--; // decrement palette index of entries we cut, no texel adjustments.
			}

			//move colors
			memmove(pair1, pair2, (work->plttSize - i - 2) * sizeof(COLOR));

			//decrement the search pointer and search size.
			i -= 2;
			pairScanLength -= 2;
		}
	}
}

static void TxiRefineFillGaps(TxiConversionWork *work) {
	//search for palette usage in the pattern XoXo, XooX, oXXo, oXoX (X=used, o=unused), and
	//merge the two halves.
	unsigned int paletteSize = work->plttSize;
	for (unsigned int i = 0; (i + 3) < paletteSize; i += 2) {
		int nUsedPair1 = work->useMap[i + 0] + work->useMap[i + 1];
		int nUsedPair2 = work->useMap[i + 2] + work->useMap[i + 3];

		if (nUsedPair1 != 1 || nUsedPair2 != 1) continue;

		//merge the pairs of colors.
		int iDest1 = work->useMap[i + 0] ? 1 : 0; // unused slot in first pair
		int iSrc2  = work->useMap[i + 2] ? 0 : 1; // used slot in second pair
		work->pltt[i + iDest1] = work->pltt[i + 2 + iSrc2];

		//adjust texel and pidx data
		for (unsigned int j = 0; j < work->nTiles; j++) {
			unsigned int cidx = work->tiles[j].paletteIndex << 1;

			if (cidx > i) {
				if (cidx == (i + 2)) {
					//merge low 2 color indices into the base palette.
					TxiRefineRemapColors(&work->tiles[j], iDest1, iDest1, 2, 3);
				}

				work->tiles[j].paletteIndex--;
			} else if (cidx == i) {
				//XoXo  : 0 . 1 . 
				//XooX  : 0 . . 1
				//oXXo  : . 1 0 .
				//oXoX  : . 1 . 0
				TxiRefineRemapColors(&work->tiles[j], 1 - iDest1, 1 - iDest1, iDest1, iDest1);
			}
		}

		//update use map
		work->useMap[i + iDest1] = 1;

		//slide colors over
		memmove(work->pltt + i + 2, work->pltt + i + 4, (paletteSize - i - 4) * sizeof(COLOR));
		memmove(work->useMap + i + 2, work->useMap + i + 4, (paletteSize - i - 4));
		work->useMap[--paletteSize] = 0;
		work->useMap[--paletteSize] = 0;
	}
}

static void TxiRefineBubbleUnusedPairs(
	TxiConversionWork *work,
	unsigned int      *pUsed,
	unsigned int      *pSingles
) {
	//move aligned 2-color palettes to the end of the palette
	unsigned int nUsedColors = work->plttSize, nSingleAvailable = 0;
	for (unsigned int i = 0; i < nUsedColors - 1; i += 2) {
		//if only one of the two colors is unused, add to the list of single colors
		if ((work->useMap[i] && !work->useMap[i + 1]) || (!work->useMap[i] && work->useMap[i + 1])) nSingleAvailable++;

		if (work->useMap[i] || work->useMap[i + 1]) continue;

		//slide over the palette, slide over the usage buffer, and subract palette indices
		int nMovedColors = nUsedColors - i - 2;
		memmove(work->pltt + i, work->pltt + i + 2, nMovedColors * sizeof(COLOR));
		memmove(work->useMap + i, work->useMap + i + 2, nMovedColors);
		work->useMap[i + nMovedColors + 0] = 0;
		work->useMap[i + nMovedColors + 1] = 0;

		//adjust palette indices for all 4x4 blocks that have had their palette colors moved.
		for (unsigned int j = 0; j < work->nTiles; j++) {
			unsigned int cidx = work->tiles[j].paletteIndex << 1;

			//we decrement the palette index of this block if it has any opaque pixels and is using colors after
			//the remove point. We must check for palette index greater than *or equal* because a 4x4 block may
			//only be using the second half of its assigned palette, and these indices should be adjusted as well.
			//there is an exceptional case for a 4x4 block using only the second half of palette index 0, these
			//must not be decremented (and instead its texel data should be adjusted only).
			if (cidx >= i) {
				if (cidx > 0) {
					//decrement palette index
					work->tiles[j].paletteIndex--;
				} else {
					//adjust texel data (switch pixel values of 2,3 to 0,1)
					TxiRefineRemapColors(&work->tiles[j], 0, 1, 0, 1);
				}
			}
		}

		i -= 2;
		nUsedColors -= 2;
	}

	*pUsed = nUsedColors;
	*pSingles = nSingleAvailable;
}

static int TxiRefineIteration(TxiConversionWork *work) {
	//We begin with a few passes over the texture data with the goal of maximizing the number of
	//unused colors, which may then be moved and reused.
	//The order of operations is significant here. Alterations to the order will cause the steps
	//to undo each other.
	TxiRefineCoalesceDown(work);        // reduce palette indices down
	TxiRefineCoalesceSingleColor(work); // single-color tile optimization
	TxiRefineReduceColorPairs(work);    // remove identical color pair pairs

	//next, we create a map of palette colors that are being used by the texture data. Colors are
	//marked as "used" when they are directly referenced by index, or are used as part of a color
	//interpolation. This allows the discovery of inefficiencies in the current layout.
	TxiAccountColors(work);

	//for all tiles marked as duplicates, exclude them from the list.
	for (unsigned int i = 0; i < work->nTiles; i++) {
		TxiTileErrorMapEntry *e = &work->errorMap[i];
		if (e->tile->duplicate || !e->tile->used) e->error = 0.0;
	}

	TxiRefineFillGaps(work);

	//we bubble unused pairs of colors up to the end of the palette. This creates a large
	//region of colors at the end which may be used for expansion.
	unsigned int nUsedColors, nSingleAvailable;
	TxiRefineBubbleUnusedPairs(work, &nUsedColors, &nSingleAvailable);

	//get expansion budget
	int enclaveSize = work->plttSize - nUsedColors;
	int enclaveStart = nUsedColors;
	int nAvailable = enclaveSize + nSingleAvailable;
	if (nAvailable == 0) return work->plttSize; // can't do anything

	//if singles are available, try to fill them
	for (unsigned int i = 0; i < work->nTiles && nSingleAvailable > 0; i++) {
		TxiTileErrorMapEntry *entry = &work->errorMap[i];
		TxTileData *tile = entry->tile;
		uint16_t mode = tile->initMode;

		if (entry->error == 0 || tile->duplicate) continue;
		if (!(mode & COMP_INTERPOLATE) || tile->palette32[0] != tile->palette32[1]) continue;

		//better fit?
		COLOR32 temp[1] = { 0 };
		temp[0] = tile->palette32[0];
		double newErr = RxComputePaletteError(work->reduction, tile->rgb, 4, 4, temp, 1, entry->error);
		if (newErr >= entry->error) continue;

		//single candidate, slot in
		unsigned int foundIndex = 0;
		for (unsigned int j = 0; j < work->plttSize; j++) {
			if (work->useMap[j]) continue;

			work->useMap[j] = 1;
			work->pltt[j] = ColorConvertToDS(tile->palette32[0]);
			nSingleAvailable--;
			nAvailable--;
			foundIndex = j;
			break;
		}

		//index
		entry->error = 0.0; // no way to improve this tile
		tile->mode = mode;
		tile->paletteIndex = foundIndex >> 1;
		TxiIndexTile(work, tile, temp, 1, foundIndex & 1);
	}

	//repeat until we can't
	while (1) {
		TxiTileErrorMapEntry *errorEntry = TxiGetGreatestErrorTile(work);
		if (errorEntry == NULL || errorEntry->error == 0) break;

		double highestError = errorEntry->error;
		TxTileData *tile = errorEntry->tile;

		//first try using the tile's initial palette and mode
		int nOpaque;
		COLOR32 tilepal[4] = { 0 };
		TxiExpandPalette32(tile->palette32, tile->initMode, tilepal, &nOpaque);

		double newErr = RxComputePaletteError(work->reduction, tile->rgb, 4, 4, tilepal, nOpaque, highestError);
		if (newErr >= highestError) {
			//tile is beyond saving, give up
			errorEntry->error = 0.0; // ignored from now on
		} else {
			//try to slot in
			uint16_t mode = tile->initMode;
			unsigned int nPaletteColors = 0;
			if (mode & COMP_INTERPOLATE) nPaletteColors = 2;
			else if (mode & COMP_OPAQUE) nPaletteColors = 4;
			else nPaletteColors = 3;

			//if nPaletteColors == 2 and both colors are the same, then we can drop nPaletteColors to 1
			if (nPaletteColors == 2 && tile->palette32[0] == tile->palette32[1]) nPaletteColors = 1;
			if (nPaletteColors == 1) nOpaque = 1; // only one free color, can go anywhere

			//for 1-color palettes, there is no restriction on where it can go
			//for 2-color palettes and above, the base must be even (required for interpolation)
			unsigned int slottedIndex = 0;
			if (nPaletteColors == 1 && nSingleAvailable > 0) {
				//if we have singles available, slot the color into one of them
				for (unsigned int j = 0; j < work->plttSize; j++) {
					if (!work->useMap[j]) {
						work->pltt[j] = ColorConvertToDS(tile->palette32[0]);

						//mark as used
						work->useMap[j] = 1;
						nSingleAvailable--;
						slottedIndex = j;
						break;
					}
				}
			} else {
				//either we need to add 1 color and no singles available, or we're adding 2 or more colors
				//either way, append all colors to the end of the palette.
				unsigned int nColsToCopy = nPaletteColors;
				if (nUsedColors + nPaletteColors > work->plttSize) {
					//exit loop (out of space)
					break;
				}

				//copy to end of palette
				slottedIndex = nUsedColors;
				for (unsigned int i = 0; i < nColsToCopy; i++) work->pltt[slottedIndex + i] = ColorConvertToDS(tile->palette32[i]);
				memset(work->useMap + slottedIndex, 1, nColsToCopy);

				//if we add an odd number of colors, increase size by 2 and mark the second color as an unused single.
				nUsedColors += nColsToCopy;
				if (nColsToCopy & 1) {
					nUsedColors++;
					nSingleAvailable++;
					work->useMap[nUsedColors - 1] = 0;
				}
			}

			//index tile with palette
			tile->mode = mode;
			tile->paletteIndex = slottedIndex >> 1;
			TxiIndexTile(work, tile, tilepal, nOpaque, slottedIndex & 1);

			errorEntry->error = 0.0; // ignore now

			//try indexing other tiles
			for (unsigned int i = 0; i < work->nTiles; i++) {
				//traverse the error map for its pre-calculated errors
				TxiTileErrorMapEntry *entry = &work->errorMap[i];
				TxTileData *tile2 = entry->tile;
				if (tile2 == tile) continue;

				//if our working tile doesn't have transparency, don't use it on a transparent tile
				if (tile->nTransparent == 0 && tile2->nTransparent) continue;

				double err = RxComputePaletteError(work->reduction, tile2->rgb, 4, 4, tilepal, nOpaque, entry->error);
				if (err < entry->error) {
					//better
					entry->error = err;
					tile2->mode = tile->mode;
					tile2->paletteIndex = slottedIndex >> 1;
					TxiIndexTile(work, tile2, tilepal, nOpaque, slottedIndex & 1);
				}
			}
		}
	}

	//try re-indexing tiles with the new palettes
	int reindexBase = enclaveStart - 2;
	if (reindexBase < 0) reindexBase = 0;
	for (unsigned int i = 0; i < work->nTiles; i++) {
		TxiTileErrorMapEntry *entry = &work->errorMap[i];
		TxTileData *tile = entry->tile;
		if (entry->error == 0.0) continue;

		double newerr = 0.0;
		uint16_t newpidx = TxiFindOptimalPidx(work, tile, nUsedColors, reindexBase, &newerr);
		uint16_t newIdx  = newpidx & COMP_INDEX_MASK;
		uint16_t newMode = newpidx & COMP_MODE_MASK;

		//if it's the same pidx as before or no improvement, do nothing
		if (newpidx == (tile->mode | tile->paletteIndex)) continue;
		if (newerr >= entry->error) continue;

		//store error
		entry->error = newerr;

		unsigned int nOpaque = 0;
		COLOR32 tilepal[4] = { 0 };
		TxiExpandPalette(work->pltt + COMP_INDEX(newpidx), newMode, tilepal, &nOpaque);

		tile->mode = newMode;
		tile->paletteIndex = newIdx;
		TxiIndexTile(work, tile, tilepal, nOpaque, 0);
	}

	return nUsedColors;
}

static void TxiRefinePalette(TxiConversionWork *work) {
	//the maximum palette size is nUsedColors from here on.
	work->useMap = (unsigned char *) calloc(work->plttSize, 1);
	unsigned int nNewUsed = work->plttSize;

	//perform a series of refinement steps on the resultant palette. This will alter the texel,
	//index, and palette data and try to remove any inefficiencies. 
	for (int i = 0; i < 4; i++) {
		unsigned int nAfterRefinement = TxiRefineIteration(work);
		nNewUsed = (nAfterRefinement + 7) & ~7;

		if (*work->terminate) return; // memory is cleaned up later
	}

	//shrink palette
	work->plttSize = nNewUsed;
	work->pltt = realloc(work->pltt, work->plttSize * sizeof(COLOR));
}

static int TxConvert4x4(TxConversionParameters *params, RxReduction *reduction) {
	TxConversionResult result = TEXCONV_SUCCESS;

	//3-stage compression. First stage builds tile data, second stage builds palettes, third stage builds the final texture.
	unsigned int width = params->width, height = params->height;
	unsigned int tilesX = width / 4, tilesY = height / 4;
	unsigned int nTiles = tilesX * tilesY;

	TxiConversionWork work = { 0 };
	work.reduction = reduction;
	work.diffuse = params->dither ? params->diffuseAmount : 0.0f;
	work.nTiles = nTiles;
	work.plttSize = params->colorEntries;
	work.terminate = &params->terminate;
	work.progress = &params->progress;

	//init progress
	params->progressMax = nTiles * 3;
	params->progress = 0;

	//allocate texel, index, and palette data.
	work.pidx = (uint16_t *) calloc(nTiles, sizeof(uint16_t));
	work.txel = (uint32_t *) calloc(nTiles, sizeof(uint32_t));
	work.pltt = (COLOR *) calloc((work.plttSize + 7) & ~7, sizeof(COLOR));
	if (work.pidx == NULL || work.txel == NULL || work.pltt == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM);

	//create tile data
	work.tiles = (TxTileData *) calloc(nTiles, sizeof(TxTileData));
	work.errorMap = (TxiTileErrorMapEntry *) calloc(nTiles, sizeof(TxiTileErrorMapEntry));
	if (work.tiles == NULL || work.errorMap == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM);

	TxiCreateTileData(&work, params->px, tilesX, tilesY, params->fixedPalette == NULL);

	TEXCONV_CHECK_ABORT(params->terminate);

	//build the palettes.
	if (params->fixedPalette == NULL) {
		//build the texture palette from tile data
		work.plttSize = TxiBuildCompressedPalette(&work, params->threshold);
	} else {
		//copy the palette from the fixed palette
		work.plttSize = params->colorEntries;
		memcpy(work.pltt, params->fixedPalette, params->colorEntries * sizeof(COLOR));
		params->progress += nTiles;
	}

	TEXCONV_CHECK_ABORT(params->terminate);

	//generate indexed texel data
	TxiIndexTilesByPalette(&work);

	TEXCONV_CHECK_ABORT(params->terminate);

	if (params->fixedPalette == NULL) {
		//when not using the fixed palette, run refinement iterations after initial indexing.
		TxiRefinePalette(&work);
	} else {
		//when the fixed palette is used, we round up the palette size, but only after rendering.
		work.plttSize = (work.plttSize + 7) & ~7;
	}

	TEXCONV_CHECK_ABORT(params->terminate);

	//lastly, place texture data into final buffers.
	for (unsigned int i = 0; i < nTiles; i++) {
		work.txel[i] = work.tiles[i].txel;
		work.pidx[i] = work.tiles[i].mode | work.tiles[i].paletteIndex;
	}

	//set fields in the texture
	params->dest->palette.nColors = work.plttSize;
	params->dest->palette.pal = work.pltt;
	params->dest->texels.cmp = work.pidx;
	params->dest->texels.texel = (unsigned char *) work.txel;
	params->dest->texels.texImageParam = (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (params->fmt << 26);
	
Cleanup:
	if (result != TEXCONV_SUCCESS) {
		free(work.pltt);
		free(work.txel);
		free(work.pidx);
	}
	free(work.tiles);
	free(work.errorMap);
	free(work.useMap);
	return result;
}

TxConversionResult TxConvert(TxConversionParameters *params) {
	TxConversionResult result = TEXCONV_INVALID;
	params->complete = 0;     // not complete
	params->progressMax = 1;  // dummy progress max
	params->progress = 0;     // progress=0

	//pad texture if needed
	unsigned int padWidth, padHeight, sourceWidth = params->width, sourceHeight = params->height;
	COLOR32 *srcPx = params->px;
	COLOR32 *padded = TxiPadTextureImage(srcPx, sourceWidth, sourceHeight, &padWidth, &padHeight);

	RxReduction *reduction = RxNew(&params->balance);
	if (padded == NULL || reduction == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM); // no memory

	params->width = padWidth;
	params->height = padHeight;
	params->px = padded;

	//begin conversion.
	switch (params->fmt) {
		case CT_DIRECT:
			result = TxiConvertDirect(params, reduction);
			break;
		case CT_4COLOR:
		case CT_16COLOR:
		case CT_256COLOR:
			result = TxiConvertPlttN(params, reduction);
			break;
		case CT_A3I5:
		case CT_A5I3:
			result = TxiConvertAxIy(params, reduction);
			break;
		case CT_4x4:
			result = TxConvert4x4(params, reduction);
			break;
	}

	//replace unpadded image
	params->width = sourceWidth;
	params->height = sourceHeight;
	params->px = srcPx;
	params->dest->texels.height = sourceHeight;

	//copy name (null-terminated)
	if (params->fmt != CT_DIRECT) {
		params->dest->palette.name = strdup(params->pnam);
	}

	if (result == TEXCONV_SUCCESS) TxRenderRect(params->px, 0, 0, sourceWidth, sourceHeight, &params->dest->texels, &params->dest->palette);
	
Cleanup:
	//free resources
	free(padded);
	if (reduction != NULL) RxFree(reduction);

	//mark progress complete
	params->result = result;
	params->complete = 1;
	params->progress = params->progressMax;
	return result;
}
