#include "palette.h"
#include "color.h"
#include "texconv.h"

#include <math.h>

//optimize for speed rather than size
#ifndef _DEBUG
#ifdef _MSC_VER
#pragma optimize("t", on)
#endif
#endif

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

int TxConvertDirect(TxConversionParameters *params) {
	//convert to direct color.
	unsigned int width = params->width, height = params->height;
	COLOR32 *px = params->px;

	//set texture parameters
	COLOR *txel = (COLOR *) calloc(width * height, 2);
	params->dest->texels.texImageParam = (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (params->fmt << 26);
	params->dest->texels.cmp = NULL;
	params->dest->palette.pal = NULL;
	params->dest->palette.nColors = 0;
	params->dest->texels.texel = (unsigned char *) txel;

	for (unsigned int i = 0; i < width * height; i++) {
		COLOR32 p = px[i];
		COLOR c = ColorConvertToDS(p);
		if ((px[i] >> 24) >= 0x80) c |= 0x8000;

		if (params->dither) {
			COLOR32 back = ColorConvertFromDS(c);
			int errorR = ((p >>  0) & 0xFF) - ((back >>  0) & 0xFF);
			int errorG = ((p >>  8) & 0xFF) - ((back >>  8) & 0xFF);
			int errorB = ((p >> 16) & 0xFF) - ((back >> 16) & 0xFF);
			doDiffuse(i, width, height, px, errorR, errorG, errorB, 0, params->diffuseAmount);
		}
		txel[i] = c;
	}
	return 0;
}

int TxConvertIndexedOpaque(TxConversionParameters *params) {
	//generate a palette ofcolors.
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

	COLOR32 *palette = (COLOR32 *) calloc(nColors, sizeof(COLOR32));

	//should we reserve a color for transparent?
	int hasTransparent = !!params->c0xp;

	if (!params->useFixedPalette) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		RxCreatePaletteEx(params->px, width, height, palette + hasTransparent, nColors - hasTransparent,
			params->balance, params->colorBalance, params->enhanceColors, RX_FLAG_SORT_ONLY_USED | RX_FLAG_ALPHA_MODE_NONE);
	} else {
		for (unsigned int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(params->fixedPalette[i]) | 0xFF000000;
		}
	}

	//allocate texel space.
	unsigned int nBytes = width * height * bitsPerPixel / 8;
	uint8_t *txel = (uint8_t *) calloc(nBytes, 1);
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;
	RxReduceImageEx(params->px, NULL, width, height, palette, nColors,
		(hasTransparent ? RX_FLAG_ALPHA_MODE_RESERVE : RX_FLAG_ALPHA_MODE_NONE) | RX_FLAG_NO_PRESERVE_ALPHA,
		diffuse, params->balance, params->colorBalance, params->enhanceColors);

	//write texel data.
	for (unsigned int i = 0; i < width * height; i++) {
		COLOR32 p = params->px[i];
		int index = 0;
		if ((p >> 24) >= 0x80) index = RxPaletteFindClosestColorSimple(p, palette + hasTransparent, nColors - hasTransparent) + hasTransparent;
		txel[i / pixelsPerByte] |= index << (bitsPerPixel * (i & (pixelsPerByte - 1)));
	}

	//update texture info
	unsigned int param = (params->fmt << 26) | (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23);
	if (hasTransparent) param |= (1 << 29);
	params->dest->palette.nColors = nColors;
	params->dest->palette.pal = (COLOR *) calloc(nColors, 2);
	params->dest->texels.cmp = NULL;
	params->dest->texels.texel = txel;
	params->dest->texels.texImageParam = param;

	for (unsigned int i = 0; i < nColors; i++) {
		params->dest->palette.pal[i] = ColorConvertToDS(palette[i]);
	}
	free(palette);
	return 0;
}

int TxConvertIndexedTranslucent(TxConversionParameters *params) {
	//convert to translucent. First, generate a palette of colors.
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
	COLOR32 *palette = (COLOR32 *) calloc(nColors, 4);

	if (!params->useFixedPalette) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		RxCreatePaletteEx(params->px, width, height, palette, nColors,
			params->balance, params->colorBalance, params->enhanceColors, RX_FLAG_SORT_ONLY_USED | RX_FLAG_ALPHA_MODE_PIXEL);
	} else {
		for (unsigned int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(params->fixedPalette[i]) | 0xFF000000;
		}
	}

	//allocate texel space.
	int nBytes = width * height;
	uint8_t *txel = (uint8_t *) calloc(nBytes, 1);
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;
	RxReduceImageEx(params->px, NULL, width, height, palette, nColors, RX_FLAG_ALPHA_MODE_PIXEL | RX_FLAG_PRESERVE_ALPHA,
		diffuse, params->balance, params->colorBalance, params->enhanceColors);

	//write texel data.
	for (unsigned int i = 0; i < width * height; i++) {
		COLOR32 p = params->px[i];
		unsigned int index = RxPaletteFindClosestColorSimple(p, palette, nColors);
		unsigned int alpha = (((p >> 24) & 0xFF) * alphaMax * 2 + 255) / 510;
		txel[i] = index | (alpha << alphaShift);

		if (params->ditherAlpha) {				
			unsigned int backAlpha = (alpha * 510 + alphaMax) / (alphaMax * 2);
			int errorAlpha = ((p >> 24) & 0xFF) - (int) backAlpha;
			doDiffuse(i, width, height, params->px, 0, 0, 0, errorAlpha, params->diffuseAmount);
		}
	}

	//update texture info
	if (params->dest->palette.pal) free(params->dest->palette.pal);
	if (params->dest->texels.cmp) free(params->dest->texels.cmp);
	if (params->dest->texels.texel) free(params->dest->texels.texel);
	params->dest->palette.nColors = nColors;
	params->dest->palette.pal = (COLOR *) calloc(nColors, sizeof(COLOR));
	params->dest->texels.cmp = NULL;
	params->dest->texels.texel = txel;

	unsigned int param = (params->fmt << 26) | (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23);
	params->dest->texels.texImageParam = param;

	for (unsigned int i = 0; i < nColors; i++) {
		params->dest->palette.pal[i] = ColorConvertToDS(palette[i]);
	}
	free(palette);
	return 0;
}

//TxiBlend18 two colors together by weight. (out of 8)
static COLOR32 TxiBlend18(COLOR32 col1, unsigned int weight1, COLOR32 col2, unsigned int weight2) {
	unsigned int r1 = (col1 >>  0) & 0xFF, r2 = (col2 >>  0) & 0xFF;
	unsigned int g1 = (col1 >>  8) & 0xFF, g2 = (col2 >>  8) & 0xFF;
	unsigned int b1 = (col1 >> 16) & 0xFF, b2 = (col2 >> 16) & 0xFF;
	unsigned int r3 = (r1 * weight1 + r2 * weight2 + 4) / 8;
	unsigned int g3 = (g1 * weight1 + g2 * weight2 + 4) / 8;
	unsigned int b3 = (b1 * weight1 + b2 * weight2 + 4) / 8;
	return ColorRoundToDS18(r3 | (g3 << 8) | (b3 << 16)) | 0xFF000000;
}

//RGB to YUV, for only Y channel
static double TxiYFromRGB(int r, int g, int b) {
	return 0.2990 * r + 0.5870 * g + 0.1140 * b;
}

//RGB to YUV
static void TxiYuvFromRGB(int r, int g, int b, double *y, double *u, double *v) {
	*y =  0.2990 * r + 0.5870 * g + 0.1140 * b;
	*u = -0.1684 * r - 0.3316 * g + 0.5000 * b;
	*v =  0.5000 * r - 0.4187 * g - 0.0813 * b;
}

volatile int g_texCompressionProgress = 0;
volatile int g_texCompressionProgressMax = 0;
volatile int g_texCompressionFinished = 0;

typedef struct TxTileData_ {
	COLOR32 rgb[16];           // the tile's initial RGBA color data
	uint16_t used;             // marks a used tile
	uint16_t mode;             // the tile's working palette mode
	COLOR palette[4];          // the tile's initial color palette
	uint16_t paletteIndex;     // the tile's working palette index
	uint8_t transparentPixels; // number of transparent pixels
	uint8_t duplicate;         // is duplicate?
} TxTileData;

static int TxiCreatePaletteFromHistogram(RxReduction *reduction, int nColors, COLOR32 *out) {
	reduction->nPaletteColors = nColors;
	RxHistFinalize(reduction);
	RxComputePalette(reduction);

	//extract created palette
	int nUsed = reduction->nUsedColors;
	memcpy(out, reduction->paletteRgb, nUsed * sizeof(COLOR32));
	if (nUsed < nColors) {
		memset(out + nUsed, 0, (nColors - nUsed) * sizeof(COLOR32));
	}
	qsort(out, nColors, sizeof(COLOR32), RxColorLightnessComparator);

	return nUsed;
}

static double TxiComputeInterpolatedError(RxReduction *reduction, const COLOR32 *px, unsigned int nPx, COLOR c1, COLOR c2, int transparent, double maxError) {
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

	return RxComputePaletteError(reduction, px, 4, nPx / 4, palette, nColors, maxError);
}

static double TxiTestAddEndpoints(RxReduction *reduction, const COLOR32 *px, int nPx, int transparent, COLOR *pc1, COLOR *pc2, int amt, int cshift, double error) {
	//try adding to color 1
	int channel = (*pc1 >> cshift) & 0x1F;
	if ((amt < 0 && channel >= -amt) || (amt > 0 && channel <= 31 - amt)) { //check for over/underflows
		*pc1 -= (amt << cshift);
		double err2 = TxiComputeInterpolatedError(reduction, px, nPx, *pc1, *pc2, transparent, error);
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
		double err2 = TxiComputeInterpolatedError(reduction, px, nPx, *pc1, *pc2, transparent, error);
		if (err2 < error) {
			error = err2;
		} else {
			*pc2 += (amt << cshift);
		}
	}

	//whatever error we settled on...
	return error;
}

double TxiTestStepEndpoints(RxReduction *reduction, const COLOR32 *px, int nPx, int transparent, COLOR *c1, COLOR *c2, int channel, double error) {
	double newErr = TxiTestAddEndpoints(reduction, px, nPx, transparent, c1, c2, 1, 5 * channel, error); //add
	if (newErr < error) {
		error = newErr;
	} else {
		error = TxiTestAddEndpoints(reduction, px, nPx, transparent, c1, c2, -1, 5 * channel, error);   //subtract
	}
	return error;
}

void TxiComputeEndpoints(RxReduction *reduction, const COLOR32 *px, int nPx, COLOR32 *colorMin, COLOR32 *colorMax) {
	//if only 1 or 2 colors, fill the palette with those.
	COLOR32 colors[2];
	int nColors = 0;
	int transparent = 0;
	for (int i = 0; i < nPx; i++) {
		COLOR32 col = px[i];
		if ((col >> 24) < 0x80) {
			//indicate transparent pixels, but do not count a color
			transparent = 1;
			continue;
		}

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
			*colorMin = 0;
			*colorMax = 0;
		} else if (nColors == 1) {
			//one opaque color: fill palette with one color
			*colorMin = colors[0];
			*colorMax = colors[0];
		} else {
			//two colors: sort the two colors such that the lighter one is first.
			colors[0] = ColorRoundToDS15(colors[0]);
			colors[1] = ColorRoundToDS15(colors[1]);
			double y1 = TxiYFromRGB(colors[0] & 0xFF, (colors[0] >> 8) & 0xFF, (colors[0] >> 16) & 0xFF);
			double y2 = TxiYFromRGB(colors[1] & 0xFF, (colors[1] >> 8) & 0xFF, (colors[1] >> 16) & 0xFF);
			if (y1 > y2) {
				*colorMin = colors[1];
				*colorMax = colors[0];
			} else {
				*colorMin = colors[0];
				*colorMax = colors[1];
			}
		}
		return;
	}

	//use principal component analysis to determine endpoints
	RxHistEntry *firstEntry, *lastEntry;
	RxHistClear(reduction);
	RxHistAdd(reduction, px, 4, nPx / 4);
	RxHistFinalize(reduction);
	RxHistSort(reduction, 0, reduction->histogram->nEntries);

	//choose first and last colors along the principal axis (greatest Y is at the end)
	firstEntry = reduction->histogramFlat[0];
	lastEntry = reduction->histogramFlat[reduction->histogram->nEntries - 1];
	COLOR32 full1 = RxConvertYiqToRgb(&firstEntry->color);
	COLOR32 full2 = RxConvertYiqToRgb(&lastEntry->color);

	//round to nearest colors.
	COLOR c1 = ColorConvertToDS(full1);
	COLOR c2 = ColorConvertToDS(full2);

	//try out varying the RGB values. Start G, then R, then B. Do this a few times.
	double error = TxiComputeInterpolatedError(reduction, px, nPx, c1, c2, transparent, 1e32);
	for (int i = 0; i < 10; i++) {
		COLOR old1 = c1, old2 = c2;
		error = TxiTestStepEndpoints(reduction, px, nPx, transparent, &c1, &c2, COLOR_CHANNEL_G, error);
		error = TxiTestStepEndpoints(reduction, px, nPx, transparent, &c1, &c2, COLOR_CHANNEL_R, error);
		error = TxiTestStepEndpoints(reduction, px, nPx, transparent, &c1, &c2, COLOR_CHANNEL_B, error);

		//early breakout check: are we doing anything?
		if (old1 == c1 && old2 == c2) break;
	}

	//sanity check: impose color ordering (high Y must come first)
	full1 = ColorConvertFromDS(c1);
	full2 = ColorConvertFromDS(c2);
	double y1 = TxiYFromRGB(full1 & 0xFF, (full1 >> 8) & 0xFF, (full1 >> 16) & 0xFF);
	double y2 = TxiYFromRGB(full2 & 0xFF, (full2 >> 8) & 0xFF, (full2 >> 16) & 0xFF);
	if (y2 > y1) {
		//swap order to keep me sane
		COLOR32 temp = full2;
		full2 = full1;
		full1 = temp;
	}
	*colorMin = full2 | 0xFF000000;
	*colorMax = full1 | 0xFF000000;
}

static double TxiComputeColorDifferenceSimple(COLOR32 c1, COLOR32 c2) {
	int r1 = c1 & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = (c1 >> 16) & 0xFF;
	int r2 = c2 & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = (c2 >> 16) & 0xFF;

	double dy, du, dv;
	//property of linear transformations :)
	TxiYuvFromRGB(r2 - r1, g2 - g1, b2 - b1, &dy, &du, &dv);

	return 4.0 * dy * dy + du * du + dv * dv;
}

//compute LMS squared
static double TxiComputeMSE(const COLOR32 *tile, const COLOR32 *palette, int transparent) {
	double total = 0, nCount = 0;
	for (int i = 0; i < 16; i++) {
		COLOR32 c = tile[i];
		if (!transparent || (c >> 24) >= 0x80) {
			int closest = RxPaletteFindClosestColorSimple(c, palette, 4 - transparent);
			COLOR32 chosen = palette[closest];
			total += TxiComputeColorDifferenceSimple(c, chosen);
			nCount++;
		}
	}
	if (nCount == 0) return 0;
	return total / nCount;
}

static void TxiChoosePaletteAndMode(RxReduction *reduction, TxTileData *tile) {
	//first try interpolated. If it's not good enough, use full color.
	COLOR32 colorMin, colorMax;
	TxiComputeEndpoints(reduction, tile->rgb, 16, &colorMin, &colorMax);
	if (tile->transparentPixels) {
		COLOR32 mid = TxiBlend18(colorMin, 4, colorMax, 4);
		COLOR32 palette[] = { colorMax, mid, colorMin, 0 };
		COLOR32 paletteFull[4];

		double error = TxiComputeMSE(tile->rgb, palette, 1);
		RxHistClear(reduction);
		RxHistAdd(reduction, tile->rgb, 4, 4);
		int nFull = TxiCreatePaletteFromHistogram(reduction, 3, paletteFull);
		//if error <= 64, then these colors are good enough
		if (error <= 64 || nFull <= 2) {
			tile->palette[0] = ColorConvertToDS(colorMax);
			tile->palette[1] = ColorConvertToDS(colorMin);
			tile->palette[2] = 0;
			tile->palette[3] = 0;
			tile->mode = COMP_TRANSPARENT | COMP_INTERPOLATE;
		} else {
			//swap index 3 and 0, 2 and 1
			tile->palette[0] = ColorConvertToDS(paletteFull[2]); //entry 3 empty, double up entry 2
			tile->palette[1] = ColorConvertToDS(paletteFull[1]);
			tile->palette[2] = ColorConvertToDS(paletteFull[2]);
			tile->palette[3] = ColorConvertToDS(paletteFull[0]);
			tile->mode = COMP_TRANSPARENT | COMP_FULL;
		}
	} else {
		COLOR32 mid1 = TxiBlend18(colorMin, 5, colorMax, 3);
		COLOR32 mid2 = TxiBlend18(colorMin, 3, colorMax, 5);
		COLOR32 palette[] = { colorMax, mid2, mid1, colorMin };
		COLOR32 paletteFull[4];

		double error = TxiComputeMSE(tile->rgb, palette, 0);
		RxHistClear(reduction);
		RxHistAdd(reduction, tile->rgb, 4, 4);
		int nFull = TxiCreatePaletteFromHistogram(reduction, 4, paletteFull);
		if (error <= 64 || nFull <= 2) {
			tile->palette[0] = ColorConvertToDS(colorMax);
			tile->palette[1] = ColorConvertToDS(colorMin);
			tile->palette[2] = 0;
			tile->palette[3] = 0;
			tile->mode = COMP_OPAQUE | COMP_INTERPOLATE;
		} else {
			//swap index 3 and 0, 2 and 1
			if (nFull < 4) paletteFull[0] = paletteFull[1];
			tile->palette[0] = ColorConvertToDS(paletteFull[3]);
			tile->palette[1] = ColorConvertToDS(paletteFull[1]);
			tile->palette[2] = ColorConvertToDS(paletteFull[2]);
			tile->palette[3] = ColorConvertToDS(paletteFull[0]);
			tile->mode = COMP_OPAQUE | COMP_FULL;
		}
	}
}

static void TxiAddTile(RxReduction *reduction, TxTileData *data, int index, const COLOR32 *px, int createPalette, int *totalIndex) {
	memcpy(data[index].rgb, px, 64);
	data[index].duplicate = 0;
	data[index].used = 1;
	data[index].transparentPixels = 0;
	data[index].mode = 0;
	data[index].paletteIndex = 0;

	//count transparent pixels
	int nTransparentPixels = 0;
	for (int i = 0; i < 16; i++) {
		COLOR32 c = px[i];
		unsigned int a = (c >> 24) & 0xFF;
		if (a < 0x80) {
			nTransparentPixels++;
			data[index].rgb[i] = 0; // set alpha=0 (under threshold)
		} else {
			data[index].rgb[i] |= 0xFF000000; // set alpha=1 (over threshold)
		}
	}
	data[index].transparentPixels = nTransparentPixels;

	//is fully transparent?
	if (nTransparentPixels == 16) {
		data[index].used = 0;
		data[index].paletteIndex = 0;
		data[index].mode = COMP_TRANSPARENT | COMP_FULL;
		data[index].palette[0] = 0;
		data[index].palette[1] = 0;
		return;
	}
	
	//is it a duplicate?
	int isDuplicate = 0;
	int duplicateIndex = 0;
	for (int i = index - 1; i >= 0; i--) {
		TxTileData *tile = data + i;
		COLOR32 *px1 = tile->rgb;
		COLOR32 *px2 = data[index].rgb;

		if (!memcmp(px1, px2, 16 * sizeof(COLOR32))) {
			isDuplicate = 1;
			duplicateIndex = i;
			break;
		}
	}

	if (isDuplicate) {
		memcpy(data + index, data + duplicateIndex, sizeof(TxTileData));
		data[index].duplicate = 1;
		data[index].paletteIndex = data[duplicateIndex].paletteIndex;
	} else if (createPalette) {
		//generate a palette and determine the mode.
		TxiChoosePaletteAndMode(reduction, data + index);
		data[index].paletteIndex = *totalIndex;

		//is the palette and mode identical to a non-duplicate tile?
		for (int i = index - 1; i >= 0; i--) {
			TxTileData *tile1 = &data[i];
			TxTileData *tile2 = &data[index];
			if (tile1->duplicate) continue;
			if (tile1->mode != tile2->mode) continue;
			if (tile1->palette[0] != tile2->palette[0] || tile1->palette[1] != tile2->palette[1]) continue;
			if (!(tile1->mode & COMP_INTERPOLATE)) {
				if (tile1->palette[2] != tile2->palette[2] || tile1->palette[3] != tile2->palette[3]) continue;
			}

			//palettes and modes are the same, mark as duplicate.
			tile2->duplicate = 1;
			tile2->paletteIndex = tile1->paletteIndex;
			break;
		}
	} else {
		//do not create a palette.
		data[index].duplicate = 0;
		data[index].paletteIndex = 0;
		data[index].mode = COMP_FULL | COMP_TRANSPARENT;
	}

	if (!data[index].duplicate) {
		int nPalettes = 1;
		if (!(data[index].mode & COMP_INTERPOLATE)) {
			nPalettes = 2;
		}
		*totalIndex += nPalettes;
	}
	g_texCompressionProgress++;
}

static TxTileData *TxiCreateTileData(RxReduction *reduction, COLOR32 *px, int tilesX, int tilesY, int createPalette) {
	TxTileData *data = (TxTileData *) calloc(tilesX * tilesY, sizeof(TxTileData));
	int paletteIndex = 0;
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			COLOR32 tile[16];
			int offs = x * 4 + y * 4 * tilesX * 4;
			memcpy(tile +  0, px + offs, 4 * sizeof(COLOR32));
			memcpy(tile +  4, px + offs + tilesX *  4, 4 * sizeof(COLOR32));
			memcpy(tile +  8, px + offs + tilesX *  8, 4 * sizeof(COLOR32));
			memcpy(tile + 12, px + offs + tilesX * 12, 4 * sizeof(COLOR32));
			TxiAddTile(reduction, data, x + y * tilesX, tile, createPalette, &paletteIndex);
		}
	}
	return data;
}

static int TxiTableToPaletteSize(uint8_t type) {
	if (type == 0) return 0;
	if (type == 2 || type == 8) return 2;
	return 4;
}

static uint16_t TxiTableToMode(uint8_t type) {
	if (type == 1) return COMP_TRANSPARENT | COMP_FULL;
	if (type == 2) return COMP_TRANSPARENT | COMP_INTERPOLATE;
	if (type == 4) return COMP_OPAQUE | COMP_FULL;
	if (type == 8) return COMP_OPAQUE | COMP_INTERPOLATE;
	return COMP_TRANSPARENT | COMP_FULL;
}

static double TxiComputePaletteDifference(const COLOR *pal1, const COLOR *pal2, int nColors, double nMaxError) {
	double total = 0;
	
	for (int i = 0; i < nColors; i++) {
		if (pal1[i] != pal2[i]) {
			COLOR32 c1 = ColorConvertFromDS(pal1[i]);
			COLOR32 c2 = ColorConvertFromDS(pal2[i]);
			total += TxiComputeColorDifferenceSimple(c1, c2);
		}

		if (total >= nMaxError) return nMaxError;
		if (nColors == 2 && total * 2 >= nMaxError) return nMaxError;
	}

	//for 2-color (interpolated) palettes, we'll double the difference to scale to 4 colors.
	if (nColors == 2) total *= 2;
	return total;
}

static double TxiFindClosestPalettes(COLOR *palette, uint8_t *colorTable, int nColors, int *colorIndex1, int *colorIndex2) {
	//determine which two palettes are the most similar. For 2-color palettes, multiply difference by 2.
	double leastDistance = 1e32;
	int idx1 = 0;

	while (idx1 < nColors) {
		uint8_t type1 = colorTable[idx1];
		if (type1 == 0) break;
		int nColorsInThisPalette = 2;
		if (type1 == 4 || type1 == 1) {
			nColorsInThisPalette = 4;
		}

		//start searching forward.
		int idx2 = idx1 + nColorsInThisPalette;
		while (idx2 + nColorsInThisPalette <= nColors) {
			uint8_t type2 = colorTable[idx2];
			if (!type2) break;
			int nColorsInSecondPalette = TxiTableToPaletteSize(type2);
			if (type2 != type1) {
				idx2 += nColorsInSecondPalette;
				continue;
			}

			//same type, let's compare.
			double dst = TxiComputePaletteDifference(&palette[idx1], &palette[idx2], nColorsInThisPalette, leastDistance);
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

static void TxiMergePalettes(RxReduction *reduction, TxTileData *tileData, int nTiles, COLOR *palette, int paletteIndex, uint16_t palettesMode) {
	//count the number of tiles that use this palette.
	int nUsedTiles = 0;
	for (int i = 0; i < nTiles; i++) {
		if (tileData[i].paletteIndex == paletteIndex && tileData[i].used) nUsedTiles++;
	}

	//use the mode to determine the appropriate method of creating the palette.
	COLOR32 expandPal[4];
	if (palettesMode == (COMP_TRANSPARENT | COMP_FULL)) {
		//transparent, full color
		RxHistClear(reduction);
		for (int i = 0; i < nTiles; i++) {
			if (tileData[i].paletteIndex == paletteIndex && tileData[i].used) {
				RxHistAdd(reduction, tileData[i].rgb, 4, 4);
			}
		}
		TxiCreatePaletteFromHistogram(reduction, 3, expandPal + 1);

		palette[paletteIndex * 2 + 0] = ColorConvertToDS(expandPal[2]); //don't waste this slot
		palette[paletteIndex * 2 + 1] = ColorConvertToDS(expandPal[1]);
		palette[paletteIndex * 2 + 2] = ColorConvertToDS(expandPal[2]);
		palette[paletteIndex * 2 + 3] = ColorConvertToDS(expandPal[0]);
	} else if (palettesMode & COMP_INTERPOLATE) {
		//transparent, interpolated, and opaque, interpolated

		//allocate space for all of the color data
		COLOR32 *px = (COLOR32 *) calloc(nUsedTiles, 16 * sizeof(COLOR32));

		//copy tiles into the buffer
		int copiedTiles = 0;
		for (int i = 0; i < nTiles; i++) {
			if (tileData[i].paletteIndex == paletteIndex && tileData[i].used) {
				memcpy(px + copiedTiles * 16, tileData[i].rgb, 16 * 4);
				copiedTiles++;
			}
		}
		TxiComputeEndpoints(reduction, px, 16 * nUsedTiles, &expandPal[0], &expandPal[1]);
		free(px);

		palette[paletteIndex * 2 + 0] = ColorConvertToDS(expandPal[1]);
		palette[paletteIndex * 2 + 1] = ColorConvertToDS(expandPal[0]);
	} else if (palettesMode == (COMP_OPAQUE | COMP_FULL)) {
		//opaque, full color
		RxHistClear(reduction);
		for (int i = 0; i < nTiles; i++) {
			if (tileData[i].paletteIndex == paletteIndex && tileData[i].used) {
				RxHistAdd(reduction, tileData[i].rgb, 4, 4);
			}
		}
		int nFull = TxiCreatePaletteFromHistogram(reduction, 4, expandPal);

		if (nFull < 4) expandPal[0] = expandPal[1];
		palette[paletteIndex * 2 + 0] = ColorConvertToDS(expandPal[3]);
		palette[paletteIndex * 2 + 1] = ColorConvertToDS(expandPal[1]);
		palette[paletteIndex * 2 + 2] = ColorConvertToDS(expandPal[2]);
		palette[paletteIndex * 2 + 3] = ColorConvertToDS(expandPal[0]);
	}
}

static int TxiBuildCompressedPalette(RxReduction *reduction, COLOR *palette, int nPalettes, TxTileData *tileData, int tilesX, int tilesY, int threshold) {
	//iterate over all non-duplicate tiles, adding the palettes.
	//colorTable keeps track of how each color is intended to be used.
	//00 - unused. 01 - mode 0x0000. 02 - mode 0x4000. 04 - mode 0x8000. 08 - mode 0xC000.
	uint8_t *colorTable = (uint8_t *) calloc(nPalettes * 2, 1);
	int diffThreshold = threshold * threshold * 52; //threshold 0-100, square normalized to 0-1040400/2
	int firstSlot = 0;
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int index = x + y * tilesX;
			TxTileData *tile = tileData + index;
			if (tile->duplicate || !tile->used) {
				//the paletteIndex field of a duplicate tile is first set to the tile index it is a duplicate of.
				//set it to an actual palette index here.
				g_texCompressionProgress++;
				continue;
			}

			//how many color entries does this consume?
			int nConsumed = 4;
			if (tile->mode & COMP_INTERPOLATE) nConsumed = 2;

			//does it fit?
			int fits = 0;
			if (firstSlot + nConsumed <= nPalettes * 2) {
				//yes, just add it to the list.
				fits = 1;
				memcpy(palette + firstSlot, tile->palette, nConsumed * sizeof(COLOR));
				uint8_t fill = 1 << (tile->mode >> 14);
				memset(colorTable + firstSlot, fill, nConsumed);
				tile->paletteIndex = firstSlot / 2;
				firstSlot += nConsumed;
			}
			if(!fits || (threshold && firstSlot >= 8)) {
				//does NOT fit, we need to rearrange some things.

				while ((firstSlot + nConsumed > nPalettes * 2) || (threshold && fits)) {
					//determine which two palettes are the most similar.
					int colorIndex1 = -1, colorIndex2 = -1;
					int distance = (int) TxiFindClosestPalettes(palette, colorTable, firstSlot, &colorIndex1, &colorIndex2);
					if (colorIndex1 == -1) break;
					if (fits && (distance > diffThreshold || firstSlot < 8)) break;
					int nColorsInPalettes = TxiTableToPaletteSize(colorTable[colorIndex1]);
					uint16_t palettesMode = TxiTableToMode(colorTable[colorIndex1]);

					//find tiles that use colorIndex2. Set them to use colorIndex1. 
					//then subtract from all palette indices > colorIndex2. Then we can
					//shift over all the palette colors. Then regenerate the palette.
					for (int i = 0; i < tilesX * tilesY; i++) {
						if (tileData[i].paletteIndex == colorIndex2 / 2) {
							tileData[i].paletteIndex = colorIndex1 / 2;
						} else if (tileData[i].paletteIndex > colorIndex2 / 2) {
							tileData[i].paletteIndex -= nColorsInPalettes / 2;
						}
					}

					//move entries in palette and colorTable.
					int nToShift = nPalettes * 2 - colorIndex2 - nColorsInPalettes;
					memmove(palette + colorIndex2, palette + colorIndex2 + nColorsInPalettes, nToShift * sizeof(COLOR));
					memmove(colorTable + colorIndex2, colorTable + colorIndex2 + nColorsInPalettes, nToShift);

					//merge those palettes that we've just combined.
					TxiMergePalettes(reduction, tileData, tilesX * tilesY, palette, colorIndex1 / 2, palettesMode);

					//update end pointer to reflect the change.
					firstSlot -= nColorsInPalettes;
				}

				//now add this tile's colors
				if (!fits) {
					memcpy(palette + firstSlot, tile->palette, nConsumed * sizeof(COLOR));
					uint8_t fill = 1 << (tile->mode >> 14);
					memset(colorTable + firstSlot, fill, nConsumed);
					tile->paletteIndex = firstSlot / 2;
					firstSlot += nConsumed;
				}
			}
			g_texCompressionProgress++;
		}
	}
	free(colorTable);
	return firstSlot;
}

static void TxiExpandPalette(const COLOR *nnsPal, uint16_t mode, COLOR32 *dest, int *nOpaque) {
	dest[0] = ColorConvertFromDS(nnsPal[0]) | 0xFF000000;
	dest[1] = ColorConvertFromDS(nnsPal[1]) | 0xFF000000;
	mode &= COMP_MODE_MASK;

	if (mode & COMP_OPAQUE) *nOpaque = 4;
	else *nOpaque = 3;
	
	if (mode == (COMP_OPAQUE | COMP_FULL)) {
		dest[2] = ColorConvertFromDS(nnsPal[2]) | 0xFF000000;
		dest[3] = ColorConvertFromDS(nnsPal[3]) | 0xFF000000;
	} else if (mode == (COMP_TRANSPARENT | COMP_FULL)) {
		dest[2] = ColorConvertFromDS(nnsPal[2]) | 0xFF000000;
		dest[3] = 0xFF000000;
	} else if (mode == (COMP_TRANSPARENT | COMP_INTERPOLATE)) {
		dest[2] = TxiBlend18(dest[0], 4, dest[1], 4) | 0xFF000000;
		dest[3] = 0xFF000000;
	} else if (mode == (COMP_OPAQUE | COMP_INTERPOLATE)) {
		dest[2] = TxiBlend18(dest[0], 5, dest[1], 3) | 0xFF000000;
		dest[3] = TxiBlend18(dest[0], 3, dest[1], 5) | 0xFF000000;
	}
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
	COLOR32 tilebuf[16];
	int idxbuf[16];
	memcpy(tilebuf, tile->rgb, sizeof(tilebuf));
	RxReduceImageWithContext(reduction, tile->rgb, idxbuf, 4, 4, tilepal, nOpaque, RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_PRESERVE_ALPHA, diffuse);

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
	memcpy(tile->rgb, tilebuf, sizeof(tilebuf));
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

static int TxiRefinePalette(RxReduction *reduction, TxTileData *tiles, uint32_t *txel, uint16_t *pidx, int nTiles, COLOR *nnsPal, int paletteSize, TxiTileErrorMapEntry *errorMap, unsigned char *useMap, float diffuse) {
	//account colors
	TxiAccountColors(useMap, paletteSize, txel, pidx, nTiles);

	//for all tiles marked as duplicates, exclude them from the list.
	for (int i = 0; i < nTiles; i++) {
		TxiTileErrorMapEntry *e = errorMap + i;
		if (e->tile->duplicate || !e->tile->used) e->error = 0;
	}

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

		for (int j = 0; j < nTiles; j++) {
			uint16_t idx = pidx[j];
			if (COMP_INDEX(idx) >= i && tiles[j].transparentPixels < 16) {
				pidx[j]--; //must check equal if second half of palette used
			}
		}

		i -= 2;
		nUsedColors -= 2;
	}

	//get expansion budget
	int enclaveSize = paletteSize - nUsedColors;
	int enclaveStart = nUsedColors;
	int nAvailable = enclaveSize + nSingleAvailable;
	if (nAvailable == 0) return paletteSize; //can't do anything

	//if singles are available, try to fill them
	if (nSingleAvailable > 0) {
		for (int i = 0; i < nTiles && nSingleAvailable > 0; i++) {
			TxiTileErrorMapEntry *entry = errorMap + i;
			TxTileData *tile = entry->tile;
			uint16_t mode = entry->tile->mode;

			if (entry->error == 0 || tile->duplicate) continue;
			if (!(mode & COMP_INTERPOLATE) || tile->palette[0] != tile->palette[1]) continue;

			//better fit?
			COLOR32 temp[1] = { 0 };
			temp[0] = ColorConvertFromDS(tile->palette[0]) | 0xFF000000;
			double newErr = RxComputePaletteError(reduction, tile->rgb, 4, 4, temp, 1, entry->error);
			if (newErr >= entry->error) continue;

			//single candidate, slot in
			int foundIndex = 0;
			for (int j = 0; j < paletteSize; j++) {
				if (useMap[j]) continue;
				useMap[j] = 1;
				nnsPal[j] = tile->palette[0];
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
		TxiExpandPalette(tile->palette, tile->mode, tilepal, &nOpaque);

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
			if (nPaletteColors == 2 && tile->palette[0] == tile->palette[1]) nPaletteColors = 1;
			if (nPaletteColors == 1) nOpaque = 1; //only one free color, can go anywhere

			//for 1-color palettes, there is no restriction on where it can go
			//for 2-color palettes and above, the base must be even (required for interpolation)
			int slottedIndex = 0, nColorsAvailable = nPaletteColors;
			if (nPaletteColors == 1 && nSingleAvailable > 0) {
				//if we have singles available, slot the color into one of them
				for (int j = 0; j < paletteSize; j++) {
					if (!useMap[j]) {
						nnsPal[j] = tile->palette[0];

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
				memcpy(nnsPal + slottedIndex, tile->palette, nColsToCopy * sizeof(COLOR));
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

int TxConvert4x4(TxConversionParameters *params) {
	//3-stage compression. First stage builds tile data, second stage builds palettes, third stage builds the final texture.
	if (params->colorEntries < 16) params->colorEntries = 16; // color reduction does not support max colors < 16
	params->colorEntries = (params->colorEntries + 7) & ~7;   // multiple of 8

	unsigned int width = params->width, height = params->height;
	unsigned int tilesX = width / 4, tilesY = height / 4;
	g_texCompressionProgressMax = tilesX * tilesY * 3;
	g_texCompressionProgress = 0;

	//create tile data
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, params->balance, params->colorBalance, params->enhanceColors, 4);
	TxTileData *tileData = TxiCreateTileData(reduction, params->px, tilesX, tilesY, !params->useFixedPalette);

	//build the palettes.
	COLOR *nnsPal = (COLOR *) calloc(params->colorEntries, sizeof(COLOR));
	int nUsedColors;
	if (!params->useFixedPalette) {
		nUsedColors = TxiBuildCompressedPalette(reduction, nnsPal, params->colorEntries / 2, tileData, tilesX, tilesY, params->threshold);
	} else {
		nUsedColors = params->colorEntries;
		memcpy(nnsPal, params->fixedPalette, params->colorEntries * 2);
		g_texCompressionProgress += tilesX * tilesY;
	}
	if (nUsedColors & 7) nUsedColors += 8 - (nUsedColors & 7);
	if (nUsedColors < 16) nUsedColors = 16;

	//for end indexing, a map of which palette colors were used
	//(interpolated tiles: non-endpoints use both endpoints)
	TxiTileErrorMapEntry *errorMap = (TxiTileErrorMapEntry *) calloc(tilesX * tilesY, sizeof(TxiTileErrorMapEntry));

	//allocate index data.
	uint16_t *pidx = (uint16_t *) calloc(tilesX * tilesY, 2);

	//generate texel data.
	uint32_t *txel = (uint32_t *) calloc(tilesX * tilesY, 4);
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;
	for (unsigned int i = 0; i < tilesX * tilesY; i++) {
		uint32_t texel = 0;

		//double check that these settings are the most optimal for this tile.
		double err = 0.0;
		uint16_t idx = TxiFindOptimalPidx(reduction, tileData + i, nnsPal, nUsedColors, 0, &err);
		uint16_t mode = idx & 0xC000;
		uint16_t index = idx & 0x3FFF;
		COLOR *thisPalette = nnsPal + (index * 2);
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
		g_texCompressionProgress++;
	}

	if (params->fixedPalette == NULL) {
		unsigned char *useMap = (unsigned char *) calloc(nUsedColors, 1);
		int nNewUsed = nUsedColors;

		for (int i = 0; i < 4; i++) {
			int nAfterRefinement = TxiRefinePalette(reduction, tileData, txel, pidx, tilesX * tilesY, nnsPal, nUsedColors, errorMap, useMap, diffuse);
			nAfterRefinement = (nAfterRefinement + 7) & ~7;
			nNewUsed = nAfterRefinement;
		}
		
		//shrink palette
		nUsedColors = nNewUsed;
		nnsPal = realloc(nnsPal, nUsedColors * sizeof(COLOR));

		free(useMap);
	}

	RxDestroy(reduction);
	free(reduction);

	//set fields in the texture
	params->dest->palette.nColors = nUsedColors;
	params->dest->palette.pal = nnsPal;
	params->dest->texels.cmp = pidx;
	params->dest->texels.texel = (unsigned char *) txel;
	params->dest->texels.texImageParam = (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (params->fmt << 26);
	
	free(tileData);
	free(errorMap);
	return 0;
}

int TxConvert(TxConversionParameters *params) {
	//pad texture if needed
	unsigned int padWidth, padHeight, sourceWidth = params->width, sourceHeight = params->height;
	COLOR32 *srcPx = params->px;
	COLOR32 *padded = TxiPadTextureImage(srcPx, sourceWidth, sourceHeight, &padWidth, &padHeight);

	params->width = padWidth;
	params->height = padHeight;
	params->px = padded;

	//begin conversion.
	switch (params->fmt) {
		case CT_DIRECT:
			TxConvertDirect(params);
			break;
		case CT_4COLOR:
		case CT_16COLOR:
		case CT_256COLOR:
			TxConvertIndexedOpaque(params);
			break;
		case CT_A3I5:
		case CT_A5I3:
			TxConvertIndexedTranslucent(params);
			break;
		case CT_4x4:
			TxConvert4x4(params);
			break;
	}

	params->width = sourceWidth;
	params->height = sourceHeight;
	params->px = srcPx;
	params->dest->texels.height = sourceHeight;
	free(padded);

	//copy name (null-terminated unless 16-char long)
	if (params->fmt != CT_DIRECT) {
		params->dest->palette.name = calloc(strlen(params->pnam) + 1, 1);
		memcpy(params->dest->palette.name, params->pnam, strlen(params->pnam));
	}

	TxRender(params->px, sourceWidth, sourceHeight, &params->dest->texels, &params->dest->palette, 0);
	
	g_texCompressionFinished = 1;
	if (params->callback) params->callback(params->callbackParam);
	if (params->useFixedPalette) free(params->fixedPalette);
	if (params->pnam != NULL) free(params->pnam);
	return 0;
}
