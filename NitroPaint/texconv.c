#include "palette.h"
#include "color.h"
#include "texconv.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

//optimize for speed rather than size
#ifndef _DEBUG
#ifdef _MSC_VER
#pragma optimize("t", on)
#endif
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



static int TxConvertDirect(TxConversionParameters *params, RxReduction *reduction) {
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
	RxReduceImageWithContext(reduction, params->px, idxs, params->width, params->height, pltt, 32769, flag, diffuse);

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

static int TxConvertIndexedOpaque(TxConversionParameters *params, RxReduction *reduction) {
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
	}

	unsigned int pixelsPerByte = 8 / bitsPerPixel;
	if (params->useFixedPalette) nColors = min(nColors, params->colorEntries);
	else if (params->colorEntries < nColors) nColors = params->colorEntries;

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
	if (!params->useFixedPalette) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		RxCreatePaletteWithContext(reduction, params->px, width, height, palette + hasTransparent, nColors - hasTransparent,
			flag | RX_FLAG_SORT_ONLY_USED, NULL);
	} else {
		for (unsigned int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(params->fixedPalette[i]) | 0xFF000000;
		}
	}

	TEXCONV_CHECK_ABORT(params->terminate);

	RxSetProgressCallback(reduction, TxiConvertProgressUpdate2, params);
	RxReduceImageWithContext(reduction, params->px, idxs, width, height, palette, nColors, flag | RX_FLAG_NO_PRESERVE_ALPHA, diffuse);

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

static int TxConvertIndexedTranslucent(TxConversionParameters *params, RxReduction *reduction) {
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
	}

	if (params->useFixedPalette) nColors = min(nColors, params->colorEntries);
	else if (params->colorEntries < nColors) nColors = params->colorEntries;
	COLOR32 palette[256] = { 0 };

	//allocate texel space.
	unsigned int nBytes = width * height;
	uint8_t *txel = (uint8_t *) calloc(nBytes, 1);
	int *idxs = (int *) calloc(width * height, sizeof(int));
	COLOR *pal = (COLOR *) calloc(nColors, sizeof(COLOR));

	if (txel == NULL || pal == NULL || idxs == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM);

	float diffuse = params->dither ? params->diffuseAmount : 0.0f;

	RxSetProgressCallback(reduction, TxiConvertProgressUpdate1, params);
	if (!params->useFixedPalette) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		RxCreatePaletteWithContext(reduction, params->px, width, height, palette, nColors,
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

	RxFlag flag = RX_FLAG_ALPHA_MODE_PALETTE | RX_FLAG_PRESERVE_ALPHA;
	if (!params->ditherAlpha) flag |= RX_FLAG_NO_ALPHA_DITHER;
	RxApplyFlags(reduction, flag);

	RxSetProgressCallback(reduction, TxiConvertProgressUpdate2, params);
	RxReduceImageWithContext(reduction, params->px, idxs, width, height, palette, 256, flag, diffuse);

	TEXCONV_CHECK_ABORT(params->terminate);

	//write texel data.
	for (unsigned int i = 0; i < width * height; i++) txel[i] = (uint8_t) idxs[i];

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


//threshold for tentatively selecting an interpolated mode for a 4x4 block based on mean square
//error. Calculated as about the max squared error of rounding a color to its nearest representable
//color, and dividing by the sum of squared channel weights.
#define TXC_BLOCK_INTERP_THRESHOLD     71.0


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

typedef struct TxTileData_ {
	COLOR32 rgb[16];           // the tile's initial RGBA color data
	uint16_t used;             // marks a used tile
	uint16_t mode;             // the tile's working palette mode
	COLOR32 palette32[4];      // the tile's initial color palette
	uint16_t paletteIndex;     // the tile's working palette index
	uint8_t transparentPixels; // number of transparent pixels
	uint8_t duplicate;         // is duplicate?
} TxTileData;

static int TxiCreatePaletteFromHistogram(RxReduction *reduction, int nColors, COLOR32 *out) {
	RxComputePalette(reduction, nColors);

	//extract created palette
	int nUsed = reduction->nUsedColors;
	memcpy(out, reduction->paletteRgb, nUsed * sizeof(COLOR32));
	for (int i = nUsed; i < nColors; i++) {
		out[i] = 0xFF000000;
	}

	qsort(out, nColors, sizeof(COLOR32), RxColorLightnessComparator);
	return nUsed;
}

static double TxiComputeInterpolatedError(RxReduction *reduction, COLOR c1, COLOR c2, int transparent, double maxError) {
	//expand palette
	COLOR32 col0 = ColorConvertFromDS(c1) | 0xFF000000;
	COLOR32 col1 = ColorConvertFromDS(c2) | 0xFF000000;
	COLOR32 col2 = 0xFF000000, col3 = 0xFF000000;
	if (!transparent) {
		col2 = TxiBlend18(col0, 3, col1, 5);
		col3 = TxiBlend18(col0, 5, col1, 3);
	} else {
		col2 = TxiBlend18(col0, 4, col1, 4);
	}
	int nColors = 3 + !transparent;
	COLOR32 palette[] = { col0, col1, col2, col3 };

	return RxHistComputePaletteError(reduction, palette, nColors, maxError);
}

static double TxiTestAddEndpoints(RxReduction *reduction, int transparent, COLOR *pc1, COLOR *pc2, int amt, int cshift, double error) {
	//try adding to color 1
	int channel = (*pc1 >> cshift) & 0x1F;
	if ((amt < 0 && channel >= -amt) || (amt > 0 && channel <= 31 - amt)) { //check for over/underflows
		*pc1 -= (amt << cshift);
		double err2 = TxiComputeInterpolatedError(reduction, *pc1, *pc2, transparent, error);
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
		double err2 = TxiComputeInterpolatedError(reduction, *pc1, *pc2, transparent, error);
		if (err2 < error) {
			error = err2;
		} else {
			*pc2 += (amt << cshift);
		}
	}

	//whatever error we settled on...
	return error;
}

static double TxiTestStepEndpoints(RxReduction *reduction, int transparent, COLOR *c1, COLOR *c2, int channel, double error) {
	double newErr = TxiTestAddEndpoints(reduction, transparent, c1, c2, 1, 5 * channel, error); //add
	if (newErr < error) {
		error = newErr;
	} else {
		error = TxiTestAddEndpoints(reduction, transparent, c1, c2, -1, 5 * channel, error);   //subtract
	}
	return error;
}

static void TxiComputeEndpointsFromHistogram(RxReduction *reduction, int transparent, COLOR32 *colorMin, COLOR32 *colorMax) {
	//if only 1 or 2 colors, fill the palette with those.
	COLOR32 colors[2];
	int nColors = 0;
	for (int i = 0; i < reduction->histogram->nEntries; i++) {
		COLOR32 col = RxConvertYiqToRgb(&reduction->histogramFlat[i]->color);

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

	COLOR32 full1 = RxConvertYiqToRgb(&firstEntry->color);
	COLOR32 full2 = RxConvertYiqToRgb(&lastEntry->color);

	//round to nearest colors.
	COLOR c1 = ColorConvertToDS(full1);
	COLOR c2 = ColorConvertToDS(full2);

	//try out varying the RGB values. Start G, then R, then B. Do this a few times.
	double error = TxiComputeInterpolatedError(reduction, c1, c2, transparent, 1e32);
	for (int i = 0; i < 10; i++) {
		COLOR old1 = c1, old2 = c2;
		error = TxiTestStepEndpoints(reduction, transparent, &c1, &c2, COLOR_CHANNEL_G, error);
		error = TxiTestStepEndpoints(reduction, transparent, &c1, &c2, COLOR_CHANNEL_R, error);
		error = TxiTestStepEndpoints(reduction, transparent, &c1, &c2, COLOR_CHANNEL_B, error);

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
static double TxiComputeMSE(RxReduction *reduction, const COLOR32 *palette, unsigned int transparent) {
	unsigned int nPlttColors = 4 - (transparent > 0);
	unsigned int nOpaquePixel = 16 - transparent;
	if (nOpaquePixel == 0) return 0;

	//take the sum of square errors and normalize by the channel weighting norm
	double sse = RxHistComputePaletteError(reduction, palette, nPlttColors, 1e32);
	return (sse / (reduction->yWeight2 + reduction->iWeight2 + reduction->qWeight2)) / reduction->histogram->totalWeight;
}

static void TxiChoosePaletteAndMode(RxReduction *reduction, TxTileData *tile) {
	//add pixels to histogram
	RxHistClear(reduction);
	RxHistAdd(reduction, tile->rgb, 4, 4);
	RxHistFinalize(reduction);

	//first try interpolated. If it's not good enough, use full color.
	COLOR32 colorMin, colorMax;
	TxiComputeEndpointsFromHistogram(reduction, tile->transparentPixels, &colorMin, &colorMax);

	if (tile->transparentPixels) {
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

static void TxiAddTile(RxReduction *reduction, TxTileData *data, int index, const COLOR32 *px, int createPalette, int *totalIndex, volatile int *pProgress) {
	TxTileData *tile = &data[index];
	tile->duplicate = 0;
	tile->used = 1;
	tile->transparentPixels = 0;
	tile->mode = 0;
	tile->paletteIndex = 0;

	//fill and count transparent pixels
	int nTransparentPixels = 0;
	for (int i = 0; i < 16; i++) {
		COLOR32 c = px[i];
		unsigned int a = (c >> 24);
		if (a < 0x80) {
			nTransparentPixels++;
			tile->rgb[i] = 0; // set alpha=0 (under threshold)
		} else {
			tile->rgb[i] = c | 0xFF000000; // set alpha=1 (over threshold)
		}
	}
	tile->transparentPixels = nTransparentPixels;

	//is fully transparent?
	if (nTransparentPixels == 16) {
		tile->used = 0;
		tile->paletteIndex = 0;
		tile->mode = COMP_TRANSPARENT | COMP_FULL;
		tile->palette32[0] = 0xFF000000;
		tile->palette32[1] = 0xFF000000;
		(*pProgress)++;
		return;
	}
	
	//is it a duplicate?
	for (int i = index - 1; i >= 0; i--) {
		COLOR32 *px1 = data[i].rgb;
		COLOR32 *px2 = tile->rgb;

		if (!memcmp(px1, px2, 16 * sizeof(COLOR32))) {
			memcpy(tile, &data[i], sizeof(TxTileData));
			tile->paletteIndex = data[i].paletteIndex;
			tile->duplicate = 1;
			break;
		}
	}

	if (!tile->duplicate) {
		if (createPalette) {
			//generate a palette and determine the mode.
			TxiChoosePaletteAndMode(reduction, tile);
			tile->paletteIndex = *totalIndex;

			//is the palette and mode identical to a non-duplicate tile?
			for (int i = index - 1; i >= 0; i--) {
				TxTileData *tile1 = &data[i];
				if (tile1->duplicate) continue;
				if (tile1->mode != tile->mode) continue;

				if (tile1->palette32[0] != tile->palette32[0] || tile1->palette32[1] != tile->palette32[1]) continue;
				if (!(tile1->mode & COMP_INTERPOLATE)) {
					if (tile1->palette32[2] != tile->palette32[2] || tile1->palette32[3] != tile->palette32[3]) continue;
				}

				//palettes and modes are the same, mark as duplicate.
				tile->duplicate = 1;
				tile->paletteIndex = tile1->paletteIndex;
				break;
			}
		} else {
			//do not create a palette.
			tile->paletteIndex = 0;
			tile->mode = COMP_FULL | COMP_TRANSPARENT;
		}

		//secondary search may deem the palette data duplicate, so we check again.
		if (!tile->duplicate) {
			int nPalettes = 1;
			if (!(tile->mode & COMP_INTERPOLATE)) {
				nPalettes = 2;
			}
			*totalIndex += nPalettes;
		}
	}

	(*pProgress)++;
}

static TxTileData *TxiCreateTileData(RxReduction *reduction, const COLOR32 *px, int tilesX, int tilesY, int createPalette, volatile int *pProgress, volatile int *pTerminate) {
	TxTileData *data = (TxTileData *) calloc(tilesX * tilesY, sizeof(TxTileData));
	if (data == NULL) return NULL;

	int paletteIndex = 0, i = 0;
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			COLOR32 tile[16];
			int offs = x * 4 + y * 4 * tilesX * 4;
			memcpy(tile +  0, px + offs + tilesX *  0, 4 * sizeof(COLOR32));
			memcpy(tile +  4, px + offs + tilesX *  4, 4 * sizeof(COLOR32));
			memcpy(tile +  8, px + offs + tilesX *  8, 4 * sizeof(COLOR32));
			memcpy(tile + 12, px + offs + tilesX * 12, 4 * sizeof(COLOR32));
			TxiAddTile(reduction, data, i++, tile, createPalette, &paletteIndex, pProgress);

			if (*pTerminate) return data; // terminate check
		}
	}
	return data;
}

static int TxiTableToPaletteSize(int type) {
	if (type & COMP_INTERPOLATE) return 2;
	return 4;
}

static double TxiComputePaletteDifference(RxReduction *reduction, const RxYiqColor *pal1, const RxYiqColor *pal2, int nColors, double nMaxError) {
	double total = 0.0, errScale = 1.0, errInvScale = 1.0;
	if (nColors == 2) {
		//2-color palettes have doubled error (to be comparable to 4-color errors)
		errScale = 2.0;
		errInvScale = 0.5;
		nMaxError *= errInvScale;
	}
	
	for (int i = 0; i < nColors; i++) {
		const RxYiqColor *yiq1 = &pal1[i], *yiq2 = &pal2[i];
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

		if (total >= nMaxError) return nMaxError * errScale;
	}

	//for 2-color (interpolated) palettes, we'll double the difference to scale to 4 colors.
	return total * errScale;
}

static double TxiFindClosestPalettes(RxReduction *reduction, const RxYiqColor *pltt, const int *colorTable, int nColors, int *colorIndex1, int *colorIndex2) {
	//determine which two palettes are the most similar. For 2-color palettes, multiply difference by 2.
	double leastDistance = 1e32;
	int idx1 = 0;

	while (idx1 < nColors) {
		int type1 = colorTable[idx1];
		int nColorsInThisPalette = 4;
		if (type1 & COMP_INTERPOLATE) nColorsInThisPalette = 2;

		//start searching forward.
		int idx2 = idx1 + nColorsInThisPalette;
		while (idx2 + nColorsInThisPalette <= nColors) {
			int type2 = colorTable[idx2];
			int nColorsInSecondPalette = TxiTableToPaletteSize(type2);

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
				if (!leastDistance) return 0;
			}

			idx2 += nColorsInThisPalette;
		}
		idx1 += nColorsInThisPalette;

	}
	return leastDistance;
}

static void TxiMergePalettes(RxReduction *reduction, TxTileData *tileData, int nTiles, RxYiqColor *palette, int paletteIndex, uint16_t palettesMode) {
	//create histogram
	RxHistClear(reduction);
	for (int i = 0; i < nTiles; i++) {
		if (tileData[i].paletteIndex == paletteIndex && tileData[i].used) {
			RxHistAdd(reduction, tileData[i].rgb, 4, 4);
		}
	}
	RxHistFinalize(reduction);

	//use the mode to determine the appropriate method of creating the palette.
	COLOR32 expandPal[4];
	if (palettesMode == (COMP_TRANSPARENT | COMP_FULL)) {
		//transparent, full color
		TxiCreatePaletteFromHistogram(reduction, 3, expandPal + 1);

		RxConvertRgbToYiq(expandPal[2], &palette[paletteIndex * 2 + 0]); // don't waste this slot
		RxConvertRgbToYiq(expandPal[1], &palette[paletteIndex * 2 + 1]);
		RxConvertRgbToYiq(expandPal[2], &palette[paletteIndex * 2 + 2]);
		RxConvertRgbToYiq(expandPal[0], &palette[paletteIndex * 2 + 3]);
	} else if (palettesMode & COMP_INTERPOLATE) {
		//transparent, interpolated, and opaque, interpolated
		TxiComputeEndpointsFromHistogram(reduction, !!(palettesMode & COMP_TRANSPARENT), &expandPal[0], &expandPal[1]);

		RxConvertRgbToYiq(expandPal[1], &palette[paletteIndex * 2 + 0]);
		RxConvertRgbToYiq(expandPal[0], &palette[paletteIndex * 2 + 1]);
	} else if (palettesMode == (COMP_OPAQUE | COMP_FULL)) {
		//opaque, full color
		int nFull = TxiCreatePaletteFromHistogram(reduction, 4, expandPal);

		if (nFull < 4) expandPal[0] = expandPal[1];
		RxConvertRgbToYiq(expandPal[3], &palette[paletteIndex * 2 + 0]);
		RxConvertRgbToYiq(expandPal[1], &palette[paletteIndex * 2 + 1]);
		RxConvertRgbToYiq(expandPal[2], &palette[paletteIndex * 2 + 2]);
		RxConvertRgbToYiq(expandPal[0], &palette[paletteIndex * 2 + 3]);
	}
}

static int TxiBuildCompressedPalette(RxReduction *reduction, COLOR *outPltt, int plttSize, TxTileData *tileData, int tilesX, int tilesY, int threshold, volatile int *pProgress, volatile int *pTerminate) {
	//the main palette loop must be able to accommadate at least 16 colors. If the requested palette
	//size is below this, we will still make a best effort, truncating if needed.
	//16 colors is the sum of sizes of all 4 types (4+4+2+2) plus the largest size again (+4).
	int outPlttSize = plttSize;
	if (plttSize < 16) plttSize = 16;

	RxYiqColor *plttYiq = (RxYiqColor *) RxMemCalloc(plttSize, sizeof(RxYiqColor));
	int *colorTable = (int *) calloc(plttSize, sizeof(int));
	
	//user-specified threshold, normalized to correspond to a half (squared) difference in Y value.
	double diffThreshold = (threshold * threshold) * reduction->yWeight2 * (255.0 * 4.0 / 10000.0);

	//iterate over all non-duplicate tiles, adding the palettes.
	int availableSlot = 0;
	int nTiles = tilesX * tilesY;
	for (int i = 0; i < nTiles; i++) {
		TxTileData *tile = &tileData[i];
		if (tile->duplicate || !tile->used) {
			//the paletteIndex field of a duplicate tile is first set to the tile index it is a duplicate of.
			(*pProgress)++;
			continue;
		}

		//how many color entries does this consume?
		int nConsumed = TxiTableToPaletteSize(tile->mode);   // number of colors required by the added palette

		//palette merge loop: merge palettes while either the colors to be added do not fit, or palettes
		//are within merge threshold.
		while ((availableSlot + nConsumed) > outPlttSize || threshold > 0) {
			//determine which two palettes are the most similar.
			int colorIndex1 = -1, colorIndex2 = -1;
			double distance = TxiFindClosestPalettes(reduction, plttYiq, colorTable, availableSlot, &colorIndex1, &colorIndex2);
			if (colorIndex1 == -1) break;
			if ((availableSlot + nConsumed) <= outPlttSize && distance > diffThreshold) break;

			uint16_t palettesMode = colorTable[colorIndex1];
			int nColsRemove = TxiTableToPaletteSize(palettesMode);

			//find tiles that use colorIndex2. Set them to use colorIndex1. 
			//then subtract from all palette indices > colorIndex2. Then we can
			//shift over all the palette colors. Then regenerate the palette.
			for (int j = 0; j < nTiles; j++) {
				if (tileData[j].paletteIndex == colorIndex2 / 2) {
					tileData[j].paletteIndex = colorIndex1 / 2;
				} else if (tileData[j].paletteIndex > colorIndex2 / 2) {
					tileData[j].paletteIndex -= nColsRemove / 2;
				}
			}

			//move entries in palette and colorTable.
			int nToShift = plttSize - colorIndex2 - nColsRemove;
			memmove(plttYiq + colorIndex2, plttYiq + colorIndex2 + nColsRemove, nToShift * sizeof(RxYiqColor));
			memmove(colorTable + colorIndex2, colorTable + colorIndex2 + nColsRemove, nToShift * sizeof(int));

			//merge those palettes that we've just combined.
			TxiMergePalettes(reduction, tileData, tilesX * tilesY, plttYiq, colorIndex1 / 2, palettesMode);
			availableSlot -= nColsRemove;
		}

		//now add this tile's colors
		for (int j = 0; j < nConsumed; j++) {
			RxConvertRgbToYiq(tile->palette32[j], &plttYiq[availableSlot + j]);
			colorTable[availableSlot + j] = tile->mode;
		}
		tile->paletteIndex = availableSlot / 2;
		availableSlot += nConsumed;

		(*pProgress)++;
		if (*pTerminate) goto Done;
	}
Done:
	free(colorTable);

	//copy palette out
	if (plttYiq != NULL) {
		for (int i = 0; i < outPlttSize; i++) {
			outPltt[i] = ColorConvertToDS(RxConvertYiqToRgb(&plttYiq[i]));
		}
		RxMemFree(plttYiq);
	}
	return availableSlot;
}

static void TxiExpandPalette32(const COLOR32 *nnsPal, uint16_t mode, COLOR32 *dest, int *nOpaque) {
	dest[0] = nnsPal[0];
	dest[1] = nnsPal[1];
	mode &= COMP_MODE_MASK;

	if (mode & COMP_OPAQUE) *nOpaque = 4;
	else *nOpaque = 3;

	if (mode == (COMP_OPAQUE | COMP_FULL)) {
		dest[2] = nnsPal[2];
		dest[3] = nnsPal[3];
	} else if (mode == (COMP_TRANSPARENT | COMP_FULL)) {
		dest[2] = nnsPal[2];
		dest[3] = 0;
	} else if (mode == (COMP_TRANSPARENT | COMP_INTERPOLATE)) {
		dest[2] = TxiBlend18(dest[0], 4, dest[1], 4) | 0xFF000000;
		dest[3] = 0;
	} else if (mode == (COMP_OPAQUE | COMP_INTERPOLATE)) {
		dest[2] = TxiBlend18(dest[0], 5, dest[1], 3) | 0xFF000000;
		dest[3] = TxiBlend18(dest[0], 3, dest[1], 5) | 0xFF000000;
	}
}

static void TxiExpandPalette(const COLOR *nnsPal, uint16_t mode, COLOR32 *dest, int *nOpaque) {
	//convert 2 to 4 colors to 32-bit
	COLOR32 nnsPal32[4];
	nnsPal32[0] = ColorConvertFromDS(nnsPal[0]) | 0xFF000000;
	nnsPal32[1] = ColorConvertFromDS(nnsPal[1]) | 0xFF000000;
	if (!(mode & COMP_INTERPOLATE)) {
		nnsPal32[2] = ColorConvertFromDS(nnsPal[2]) | 0xFF000000;
		if (mode & COMP_OPAQUE) {
			nnsPal32[3] = ColorConvertFromDS(nnsPal[3]) | 0xFF000000;
		}
	}

	TxiExpandPalette32(nnsPal32, mode, dest, nOpaque);
}

static double TxiComputeTilePidxError(RxReduction *reduction, const COLOR32 *px, const COLOR *palette, uint16_t mode, double maxError) {
	int nOpaque;
	COLOR32 expandPal[4];
	TxiExpandPalette(palette + COMP_INDEX(mode), mode, expandPal, &nOpaque);
	return RxComputePaletteError(reduction, px, 4, 4, expandPal, nOpaque, maxError);
}

static uint16_t TxiFindOptimalPidx(RxReduction *reduction, TxTileData *tile, const COLOR *palette, int nColors, int startIdx, double *error) {
	COLOR32 *px = tile->rgb;
	int hasTransparent = tile->transparentPixels;

	//start with default values
	uint16_t leastPidx = tile->mode | tile->paletteIndex;
	double leastError = TxiComputeTilePidxError(reduction, px, palette, leastPidx, 1e32);
	if (tile->transparentPixels == 16 || leastError == 0.0) {
		return leastPidx;
	}

	//yes, iterate over every possible palette and mode.
	for (int i = startIdx; i < nColors; i += 2) {
		for (int j = 0; j < 4; j++) {
			//check that we don't run off the end of the palette
			int nConsumed = 2;
			if (j == 0 || j == 2) nConsumed = 4;
			if (i + nConsumed > nColors) continue;

			//nothing to gain from these modes sometimes
			if (!hasTransparent && j == 0) continue;
			if (hasTransparent && j >= 2) break;
			
			uint16_t mode = (j << 14) | (i >> 1);
			double dst = TxiComputeTilePidxError(reduction, px, palette, mode, leastError);
			if (dst < leastError) {
				leastPidx = mode;
				leastError = dst;
			}
		}
	}

	if (error != NULL) *error = leastError;
	return leastPidx;
}

typedef struct TxiTileErrorMpEntry_ {
	int tileIndex;
	TxTileData *tile;
	double error;
	uint16_t mode;
	uint16_t idx;
} TxiTileErrorMapEntry;

static int TxiErrorMapComparator(const void *p1, const void *p2) {
	double e1 = ((TxiTileErrorMapEntry *) p1)->error;
	double e2 = ((TxiTileErrorMapEntry *) p2)->error;

	double diff = e1 - e2;
	if (diff < 0) return 1;
	if (diff > 0) return -1;
	return 0;
}

static TxiTileErrorMapEntry *TxiGetGreatestErrorTile(TxiTileErrorMapEntry *map, int nTiles) {
	qsort(map, nTiles, sizeof(*map), TxiErrorMapComparator);

	//if the first entry has 0 error, all tiles are matched
	if (nTiles == 0 || map[0].error == 0) {
		return NULL;
	}
	return map;
}

static void TxiIndexTile(RxReduction *reduction, TxTileData *tile, uint32_t *txel, const COLOR32 *tilepal, int nOpaque, int baseIndex, float diffuse) {
	int idxbuf[16];
	RxReduceImageWithContext(reduction, tile->rgb, idxbuf, 4, 4, tilepal, nOpaque, RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_PRESERVE_ALPHA | RX_FLAG_NO_WRITEBACK, diffuse);

	uint32_t texel = 0;
	for (int j = 0; j < 16; j++) {
		int index = 0;
		COLOR32 col = tile->rgb[j];
		if ((col >> 24) < 0x80) {
			index = 3;
		} else {
			index = idxbuf[j] + baseIndex;
		}
		texel |= index << (j * 2);
	}
	*txel = texel;
}

static void TxiAccountColor(unsigned char *useMap, uint16_t pidx, int cindex) {
	int pindex = COMP_INDEX(pidx);

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

static void TxiAccountColors(unsigned char *useMap, int paletteSize, uint32_t *txel, uint16_t *pidx, int nTiles) {
	memset(useMap, 0, paletteSize);
	for (int i = 0; i < nTiles; i++) {
		uint32_t thisTexel = txel[i];
		uint16_t thisIndex = pidx[i];

		//each pixel of tile
		for (int j = 0; j < 16; j++) {
			int cindex = (thisTexel >> (j * 2)) & 3;
			TxiAccountColor(useMap, thisIndex, cindex);
		}
	}
}

static void TxiRefineRemapColors(uint32_t *texel, uint16_t mode, int to0, int to1, int to2, int to3) {
	//create array for easy lookup
	int to[] = { to0, to1, to2, to3 };

	//for transparent mode, avoid remapping color index 3
	if (!(mode & COMP_OPAQUE)) to[3] = 3;

	uint32_t newval = 0;
	for (int i = 0; i < 16; i++) {
		int c = (*texel >> (2 * i)) & 3;
		newval |= to[c] << (2 * i);
	}

	*texel = newval;
}

static void TxiRefineCoalesceDown(uint32_t *txel, uint16_t *pidx, int nTiles, COLOR *nnsPal, int paletteSize) {
	//we'll enter a pass where we try to coalesce palette indices down. This helps to
	//free up redundant sections of the palette, making higher-index colors unused.
	for (int i = 0; i < nTiles; i++) {
		uint32_t texPtn = txel[i];
		uint16_t idx = pidx[i];
		COLOR *currCols = nnsPal + COMP_INDEX(idx);

		//map the used colors
		unsigned char usedCols[4] = { 0 };
		for (int j = 0; j < 16; j++) {
			TxiAccountColor(usedCols, idx & COMP_MODE_MASK, (texPtn >> (j * 2)) & 3);
		}

		//search for an appearance of the colors used by this block.
		for (int j = 0; j < COMP_INDEX(idx); j += 2) {
			//we may compare 4 colors without checking the bounds, since our index is below a valid one.
			int diff = 0;
			for (int k = 0; k < 4; k++) {
				//compare only those colors that are used
				if (usedCols[k] && (currCols[k] != nnsPal[j + k])) {
					diff = 1;
					break;
				}
			}

			if (!diff) {
				//matching colors found, repoint the palette index
				pidx[i] = (idx & COMP_MODE_MASK) | (j >> 1);
				break;
			}
		}
	}
}

static void TxiRefineCoalesceSingleColor(uint32_t *txel, uint16_t *pidx, int nTiles, COLOR *nnsPal, int paletteSize) {
	//search for tiles using interpolation mode and a palette with both identical colors. We try to
	//find this one color represented in another palette (that isn't duplicating this color), with
	//the hope that we may remove the single-color palette.
	for (int i = 0; i < nTiles; i++) {
		uint16_t idx = pidx[i];
		if (!(idx & COMP_INTERPOLATE)) continue;

		int pno = COMP_INDEX(idx);
		COLOR *pltt = nnsPal + pno;
		if (pltt[0] == pltt[1]) {
			COLOR findCol = pltt[0];

			//search for an instance of the color somewhere else
			for (int j = 0; (j + 1) < paletteSize; j += 2) {
				if (nnsPal[j] != findCol && nnsPal[j + 1] != findCol) continue; // must contain the search color
				if (nnsPal[j] == nnsPal[j + 1]) continue;                       // must not be doubled color

				//found
				int iCol = (nnsPal[j + 1] == findCol);            // 0 or 1 color index
				TxiRefineRemapColors(&txel[i], idx, iCol, iCol, iCol, iCol);
				pidx[i] = (idx & COMP_MODE_MASK) | (j >> 1);
				break;
			}
		}
	}
}

static void TxiRefineReduceColorPairs(uint32_t *txel, uint16_t *pidx, int nTiles, COLOR *nnsPal, int paletteSize) {
	//search for adjacent identical color pairs. These can always be reduced.
	int pairScanLength = paletteSize;
	for (int i = 0; i < (pairScanLength - 2); i += 2) {
		COLOR *pair1 = nnsPal + i;
		COLOR *pair2 = nnsPal + i + 2;
		if (pair1[0] == pair2[0] && pair1[1] == pair2[1]) {

			//pair match. Cut the second appearance and make adjustments.
			for (int j = 0; j < nTiles; j++) {
				int idx = COMP_INDEX(pidx[j]);

				//when the index is equal to the index of the first appearance, its index is kept but the texels may
				//need to be adjusted.
				if (idx == i && !(pidx[j] & COMP_INTERPOLATE)) {
					//force pixel values of 2,3 to 0,1.
					TxiRefineRemapColors(&txel[j], pidx[j], 0, 1, 0, 1);
				}

				if (idx > i) pidx[j]--; // decrement palette index of entries we cut, no texel adjustments.
			}

			//move colors
			memmove(pair1, pair2, (paletteSize - i - 2) * sizeof(COLOR));

			//decrement the search pointer and search size.
			i -= 2;
			pairScanLength -= 2;
		}
	}
}

static void TxiRefineFillGaps(uint32_t *txel, uint16_t *pidx, int nTiles, COLOR *nnsPal, unsigned char *useMap, int paletteSize) {
	//search for palette usage in the pattern XoXo, XooX, oXXo, oXoX (X=used, o=unused), and
	//merge the two halves.
	for (int i = 0; (i + 3) < paletteSize; i += 2) {
		int nUsedPair1 = useMap[i + 0] + useMap[i + 1];
		int nUsedPair2 = useMap[i + 2] + useMap[i + 3];

		if (nUsedPair1 != 1 || nUsedPair2 != 1) continue;

		//merge the pairs of colors.
		int iDest1 = useMap[i + 0] ? 1 : 0; // unused slot in first pair
		int iSrc2 = useMap[i + 2] ? 0 : 1;  // used slot in second pair
		nnsPal[i + iDest1] = nnsPal[i + 2 + iSrc2];

		//adjust texel and pidx data
		for (int j = 0; j < nTiles; j++) {
			uint16_t idx = pidx[j];
			int cidx = COMP_INDEX(idx);

			if (cidx > i) {
				if (cidx == (i + 2)) {
					//merge low 2 color indices into the base palette.
					TxiRefineRemapColors(&txel[j], idx, iDest1, iDest1, 2, 3);
				}

				pidx[j]--;
			} else if (cidx == i) {
				//XoXo  : 0 . 1 . 
				//XooX  : 0 . . 1
				//oXXo  : . 1 0 .
				//oXoX  : . 1 . 0
				TxiRefineRemapColors(&txel[j], idx, 1 - iDest1, 1 - iDest1, iDest1, iDest1);
			}
		}

		//update use map
		useMap[i + iDest1] = 1;

		//slide colors over
		memmove(nnsPal + i + 2, nnsPal + i + 4, (paletteSize - i - 4) * sizeof(COLOR));
		memmove(useMap + i + 2, useMap + i + 4, (paletteSize - i - 4));
		useMap[--paletteSize] = 0;
		useMap[--paletteSize] = 0;
	}
}

static void TxiRefineBubbleUnusedPairs(uint32_t *txel, uint16_t *pidx, int nTiles, COLOR *nnsPal, unsigned char *useMap, int paletteSize, int *pUsed, int *pSingles) {
	//move aligned 2-color palettes to the end of the palette
	int nUsedColors = paletteSize, nSingleAvailable = 0;
	for (int i = 0; i < nUsedColors - 1; i += 2) {
		//if only one of the two colors is unused, add to the list of single colors
		if ((useMap[i] && !useMap[i + 1]) || (!useMap[i] && useMap[i + 1])) nSingleAvailable++;

		if (useMap[i] || useMap[i + 1]) continue;

		//slide over the palette, slide over the usage buffer, and subract palette indices
		int nMovedColors = nUsedColors - i - 2;
		memmove(nnsPal + i, nnsPal + i + 2, nMovedColors * sizeof(COLOR));
		memmove(useMap + i, useMap + i + 2, nMovedColors);
		useMap[i + nMovedColors + 0] = 0;
		useMap[i + nMovedColors + 1] = 0;

		//adjust palette indices for all 4x4 blocks that have had their palette colors moved.
		for (int j = 0; j < nTiles; j++) {
			uint16_t idx = pidx[j];

			//we decrement the palette index of this block if it has any opaque pixels and is using colors after
			//the remove point. We must check for palette index greater than *or equal* because a 4x4 block may
			//only be using the second half of its assigned palette, and these indices should be adjusted as well.
			//there is an exceptional case for a 4x4 block using only the second half of palette index 0, these
			//must not be decremented (and instead its texel data should be adjusted only).
			if (COMP_INDEX(idx) >= i) {
				if (COMP_INDEX(idx) > 0) {
					//decrement palette index
					pidx[j]--;
				} else {
					//adjust texel data (switch pixel values of 2,3 to 0,1)
					TxiRefineRemapColors(&txel[j], pidx[j], 0, 1, 0, 1);
				}
			}
		}

		i -= 2;
		nUsedColors -= 2;
	}

	*pUsed = nUsedColors;
	*pSingles = nSingleAvailable;
}

static int TxiRefinePalette(RxReduction *reduction, TxTileData *tiles, uint32_t *txel, uint16_t *pidx, int nTiles, COLOR *nnsPal, int paletteSize, TxiTileErrorMapEntry *errorMap, unsigned char *useMap, float diffuse) {
	//We begin with a few passes over the texture data with the goal of maximizing the number of
	//unused colors, which may then be moved and reused.
	//The order of operations is significant here. Alterations to the order will cause the steps
	//to undo each other.
	TxiRefineCoalesceDown(txel, pidx, nTiles, nnsPal, paletteSize);        // reduce palette indices down
	TxiRefineCoalesceSingleColor(txel, pidx, nTiles, nnsPal, paletteSize); // single-color tile optimization
	TxiRefineReduceColorPairs(txel, pidx, nTiles, nnsPal, paletteSize);    // remove identical color pair pairs

	//next, we create a map of palette colors that are being used by the texture data. Colors are
	//marked as "used" when they are directly referenced by index, or are used as part of a color
	//interpolation. This allows the discovery of inefficiencies in the current layout.
	TxiAccountColors(useMap, paletteSize, txel, pidx, nTiles);

	//for all tiles marked as duplicates, exclude them from the list.
	for (int i = 0; i < nTiles; i++) {
		TxiTileErrorMapEntry *e = errorMap + i;
		if (e->tile->duplicate || !e->tile->used) e->error = 0;
	}

	TxiRefineFillGaps(txel, pidx, nTiles, nnsPal, useMap, paletteSize);

	//we bubble unused pairs of colors up to the end of the palette. This creates a large
	//region of colors at the end which may be used for expansion.
	int nUsedColors, nSingleAvailable;
	TxiRefineBubbleUnusedPairs(txel, pidx, nTiles, nnsPal, useMap, paletteSize, &nUsedColors, &nSingleAvailable);

	//get expansion budget
	int enclaveSize = paletteSize - nUsedColors;
	int enclaveStart = nUsedColors;
	int nAvailable = enclaveSize + nSingleAvailable;
	if (nAvailable == 0) return paletteSize; //can't do anything

	//if singles are available, try to fill them
	for (int i = 0; i < nTiles && nSingleAvailable > 0; i++) {
		TxiTileErrorMapEntry *entry = errorMap + i;
		TxTileData *tile = entry->tile;
		uint16_t mode = entry->tile->mode;

		if (entry->error == 0 || tile->duplicate) continue;
		if (!(mode & COMP_INTERPOLATE) || tile->palette32[0] != tile->palette32[1]) continue;

		//better fit?
		COLOR32 temp[1] = { 0 };
		temp[0] = tile->palette32[0];
		double newErr = RxComputePaletteError(reduction, tile->rgb, 4, 4, temp, 1, entry->error);
		if (newErr >= entry->error) continue;

		//single candidate, slot in
		int foundIndex = 0;
		for (int j = 0; j < paletteSize; j++) {
			if (useMap[j]) continue;
			useMap[j] = 1;
			nnsPal[j] = ColorConvertToDS(tile->palette32[0]);
			nSingleAvailable--;
			nAvailable--;
			foundIndex = j;
			break;
		}

		//index
		entry->error = 0; //no way to improve this tile
		pidx[entry->tileIndex] = tile->mode | (foundIndex >> 1);
		TxiIndexTile(reduction, tile, txel + entry->tileIndex, temp, 1, foundIndex & 1, diffuse);
	}

	//repeat until we can't
	while (1) {
		TxiTileErrorMapEntry *errorEntry = TxiGetGreatestErrorTile(errorMap, nTiles);
		if (errorEntry == NULL || errorEntry->error == 0) break;

		double highestError = errorEntry->error;
		int worstTile = errorEntry->tileIndex;
		TxTileData *tile = tiles + worstTile;

		//first try using the tile's initial palette and mode
		int nOpaque;
		COLOR32 tilepal[4] = { 0 };
		TxiExpandPalette32(tile->palette32, tile->mode, tilepal, &nOpaque);

		double newErr = RxComputePaletteError(reduction, tile->rgb, 4, 4, tilepal, nOpaque, highestError);
		if (newErr >= highestError) {
			//tile is beyond saving, give up
			errorEntry->error = 0.0; //ignored from now on
		} else {
			//try to slot in
			uint16_t mode = tile->mode;
			int nPaletteColors = 0;
			if (mode & COMP_INTERPOLATE) nPaletteColors = 2;
			else if (mode & COMP_OPAQUE) nPaletteColors = 4;
			else nPaletteColors = 3;

			//if nPaletteColors == 2 and both colors are the same, then we can drop nPaletteColors to 1
			if (nPaletteColors == 2 && tile->palette32[0] == tile->palette32[1]) nPaletteColors = 1;
			if (nPaletteColors == 1) nOpaque = 1; //only one free color, can go anywhere

			//for 1-color palettes, there is no restriction on where it can go
			//for 2-color palettes and above, the base must be even (required for interpolation)
			int slottedIndex = 0;
			if (nPaletteColors == 1 && nSingleAvailable > 0) {
				//if we have singles available, slot the color into one of them
				for (int j = 0; j < paletteSize; j++) {
					if (!useMap[j]) {
						nnsPal[j] = ColorConvertToDS(tile->palette32[0]);

						//mark as used
						useMap[j] = 1;
						nSingleAvailable--;
						slottedIndex = j;
						break;
					}
				}
			} else {
				//either we need to add 1 color and no singles available, or we're adding 2 or more colors
				//either way, append all colors to the end of the palette.
				int nColsToCopy = nPaletteColors;
				if (nUsedColors + nPaletteColors > paletteSize) {
					//exit loop (out of space)
					break;
				}

				//copy to end of palette
				slottedIndex = nUsedColors;
				for (int i = 0; i < nColsToCopy; i++) nnsPal[slottedIndex + i] = ColorConvertToDS(tile->palette32[i]);
				memset(useMap + slottedIndex, 1, nColsToCopy);

				//if we add an odd number of colors, increase size by 2 and mark the second color as an unused single.
				nUsedColors += nColsToCopy;
				if (nColsToCopy & 1) {
					nUsedColors++;
					nSingleAvailable++;
					useMap[nUsedColors - 1] = 0;
				}
			}

			//index tile with palette
			pidx[worstTile] = tile->mode | (slottedIndex >> 1);
			TxiIndexTile(reduction, tile, txel + worstTile, tilepal, nOpaque, slottedIndex & 1, diffuse);

			errorEntry->error = 0.0; //ignore now

			//try indexing other tiles
			for (int i = 0; i < nTiles; i++) {
				//traverse the error map for its pre-calculated errors
				TxiTileErrorMapEntry *entry = errorMap + i;
				TxTileData *tile2 = entry->tile;
				if (entry->tileIndex == worstTile) continue;

				//if our working tile doesn't have transparency, don't use it on a transparent tile
				if (!tile->transparentPixels && tile2->transparentPixels) continue;

				double err = RxComputePaletteError(reduction, tile2->rgb, 4, 4, tilepal, nOpaque, entry->error);
				if (err < entry->error) {
					//better
					entry->error = err;
					pidx[entry->tileIndex] = tile->mode | (slottedIndex >> 1);
					TxiIndexTile(reduction, tile2, txel + entry->tileIndex, tilepal, nOpaque, slottedIndex & 1, diffuse);
				}
			}
		}
	}

	//try re-indexing tiles with the new palettes
	int reindexBase = enclaveStart - 2;
	if (reindexBase < 0) reindexBase = 0;
	for (int i = 0; i < nTiles; i++) {
		TxiTileErrorMapEntry *entry = errorMap + i;
		TxTileData *tile = entry->tile;
		if (entry->error == 0) continue;

		double newerr = 0.0;
		uint16_t newpidx = TxiFindOptimalPidx(reduction, tile, nnsPal, nUsedColors, reindexBase, &newerr);

		//if it's the same pidx as before or no improvement, do nothing
		if (newpidx == (entry->mode | entry->idx)) continue;
		if (newerr >= entry->error) continue;

		//store error
		entry->error = newerr;
		entry->idx = newpidx & COMP_INDEX_MASK;
		entry->mode = newpidx & COMP_MODE_MASK;

		int nOpaque = 0;
		COLOR32 tilepal[4] = { 0 };
		TxiExpandPalette(nnsPal + COMP_INDEX(newpidx), newpidx & COMP_MODE_MASK, tilepal, &nOpaque);

		pidx[entry->tileIndex] = newpidx;
		TxiIndexTile(reduction, tile, txel + entry->tileIndex, tilepal, nOpaque, 0, diffuse);
	}

	return nUsedColors;
}

static int TxConvert4x4(TxConversionParameters *params, RxReduction *reduction) {
	TxConversionResult result = TEXCONV_SUCCESS;

	//3-stage compression. First stage builds tile data, second stage builds palettes, third stage builds the final texture.

	unsigned int width = params->width, height = params->height;
	unsigned int tilesX = width / 4, tilesY = height / 4;
	params->progressMax = tilesX * tilesY * 3;
	params->progress = 0;

	//allocate texel, index, and palette data.
	uint16_t *pidx = (uint16_t *) calloc(tilesX * tilesY, 2);
	uint32_t *txel = (uint32_t *) calloc(tilesX * tilesY, 4);
	COLOR *pltt = (COLOR *) calloc((params->colorEntries + 7) & ~7, sizeof(COLOR));

	//create tile data
	TxTileData *tileData = TxiCreateTileData(reduction, params->px, tilesX, tilesY, !params->useFixedPalette, &params->progress, &params->terminate);
	TxiTileErrorMapEntry *errorMap = (TxiTileErrorMapEntry *) calloc(tilesX * tilesY, sizeof(TxiTileErrorMapEntry));
	if (tileData == NULL || errorMap == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM);

	TEXCONV_CHECK_ABORT(params->terminate);

	//build the palettes.
	int nUsedColors;
	if (!params->useFixedPalette) {
		nUsedColors = TxiBuildCompressedPalette(reduction, pltt, params->colorEntries, tileData, tilesX, tilesY, params->threshold,
			&params->progress, &params->terminate);
	} else {
		nUsedColors = params->colorEntries;
		memcpy(pltt, params->fixedPalette, params->colorEntries * 2);
		params->progress += tilesX * tilesY;
	}

	TEXCONV_CHECK_ABORT(params->terminate);

	//generate texel data.
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;
	for (unsigned int i = 0; i < tilesX * tilesY; i++) {
		//double check that these settings are the most optimal for this tile.
		double err = 0.0;
		uint16_t idx = TxiFindOptimalPidx(reduction, &tileData[i], pltt, nUsedColors, 0, &err);
		uint16_t mode = idx & COMP_MODE_MASK;
		uint16_t index = idx & COMP_INDEX_MASK; 
		COLOR *thisPalette = pltt + COMP_INDEX(idx);
		pidx[i] = idx;

		COLOR32 palette[4];
		int paletteSize;
		TxiExpandPalette(thisPalette, mode, palette, &paletteSize);

		//store palette error
		errorMap[i].tileIndex = i;
		errorMap[i].tile = &tileData[i];
		errorMap[i].error = err;
		errorMap[i].mode = mode;
		errorMap[i].idx = index;

		//index this tile
		TxiIndexTile(reduction, &tileData[i], txel + i, palette, paletteSize, 0, diffuse);

		params->progress++;
		TEXCONV_CHECK_ABORT(params->terminate);
	}

	if (params->fixedPalette == NULL) {
		//the maximum palette size is nUsedColors from here on.
		unsigned char *useMap = (unsigned char *) calloc(nUsedColors, 1);
		int nNewUsed = nUsedColors;

		//perform a series of refinement steps on the resultant palette. This will alter the texel,
		//index, and palette data and try to remove any inefficiencies. 
		for (int i = 0; i < 4; i++) {
			int nAfterRefinement = TxiRefinePalette(reduction, tileData, txel, pidx, tilesX * tilesY, pltt, nUsedColors, errorMap, useMap, diffuse);
			nNewUsed = (nAfterRefinement + 7) & ~7;

			TEXCONV_CHECK_ABORT(params->terminate);
		}
		
		//shrink palette
		nUsedColors = nNewUsed;
		pltt = realloc(pltt, nUsedColors * sizeof(COLOR));

		free(useMap);
	} else {
		//when the fixed palette is used, we round up the palette size, but only after rendering.
		nUsedColors = (nUsedColors + 7) & ~7;
	}

	//set fields in the texture
	params->dest->palette.nColors = nUsedColors;
	params->dest->palette.pal = pltt;
	params->dest->texels.cmp = pidx;
	params->dest->texels.texel = (unsigned char *) txel;
	params->dest->texels.texImageParam = (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (params->fmt << 26);
	
Cleanup:
	if (result != TEXCONV_SUCCESS) {
		free(pltt);
		free(txel);
		free(pidx);
	}
	free(tileData);
	free(errorMap);
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

	RxReduction *reduction = RxNew(params->balance, params->colorBalance, params->enhanceColors);
	if (padded == NULL || reduction == NULL) TEXCONV_THROW_STATUS(TEXCONV_NOMEM); // no memory

	params->width = padWidth;
	params->height = padHeight;
	params->px = padded;

	//begin conversion.
	switch (params->fmt) {
		case CT_DIRECT:
			result = TxConvertDirect(params, reduction);
			break;
		case CT_4COLOR:
		case CT_16COLOR:
		case CT_256COLOR:
			result = TxConvertIndexedOpaque(params, reduction);
			break;
		case CT_A3I5:
		case CT_A5I3:
			result = TxConvertIndexedTranslucent(params, reduction);
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
		params->dest->palette.name = _strdup(params->pnam);
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
