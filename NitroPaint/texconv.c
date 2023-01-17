#include "palette.h"
#include "color.h"
#include "texconv.h"
#include "analysis.h"

#include <math.h>

int ilog2(int x);

int textureConvertDirect(CREATEPARAMS *params) {
	//convert to direct color.
	int width = params->width, height = params->height;
	COLOR32 *px = params->px;

	params->dest->texels.texImageParam = (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (params->fmt << 26);
	if (params->dest->texels.cmp) free(params->dest->texels.cmp);
	params->dest->texels.cmp = NULL;
	if (params->dest->texels.texel) free(params->dest->texels.texel);
	if (params->dest->palette.pal) free(params->dest->palette.pal);
	params->dest->palette.pal = NULL;
	params->dest->palette.nColors = 0;
	COLOR *txel = (COLOR *) calloc(width * height, 2);
	params->dest->texels.texel = (char *) txel;
	for (int i = 0; i < width * height; i++) {
		COLOR32 p = px[i];
		COLOR c = ColorConvertToDS(p);
		if (px[i] & 0xFF000000) c |= 0x8000;
		if (params->dither) {
			COLOR32 back = ColorConvertFromDS(c);
			int errorRed = (back & 0xFF) - (p & 0xFF);
			int errorGreen = ((back >> 8) & 0xFF) - ((p >> 8) & 0xFF);
			int errorBlue = ((back >> 16) & 0xFF) - ((back >> 16) & 0xFF);
			doDiffuse(i, width, height, px, -errorRed, -errorGreen, -errorBlue, 0, params->diffuseAmount);
		}
		txel[i] = c;
	}
	return 0;
}

int textureConvertPalette(CREATEPARAMS *params) {
	//convert to translucent. First, generate a palette of colors.
	int nColors = 0, bitsPerPixel = 0;
	int width = params->width, height = params->height;
	switch (params->fmt) {
		case CT_4COLOR:
			nColors = 4;
			bitsPerPixel = 2;
			break;
		case CT_16COLOR:
			nColors = 16;
			bitsPerPixel = 4;
			break;
		case CT_256COLOR:
			nColors = 256;
			bitsPerPixel = 8;
			break;
	}
	int pixelsPerByte = 8 / bitsPerPixel;
	if (params->useFixedPalette) nColors = min(nColors, params->colorEntries);
	COLOR32 *palette = (COLOR32 *) calloc(nColors, 4);

	//should we reserve a color for transparent?
	int hasTransparent = 0;
	for (int i = 0; i < width * height; i++) {
		if ((params->px[i] & 0xFF000000) == 0) {
			hasTransparent = 1;
			break;
		}
	}

	if (!params->useFixedPalette) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		createPaletteSlow(params->px, width, height, palette + hasTransparent, nColors - hasTransparent);

		//reduce palette color depth
		for (int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(ColorConvertToDS(palette[i]));
		}
	} else {
		for (int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(params->fixedPalette[i]);
		}
	}

	//allocate texel space.
	int nBytes = width * height * bitsPerPixel / 8;
	uint8_t *txel = (uint8_t *) calloc(nBytes, 1);
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;
	ditherImagePalette(params->px, width, height, palette, nColors, TRUE, TRUE, hasTransparent, diffuse);

	//write texel data.
	for (int i = 0; i < width * height; i++) {
		COLOR32 p = params->px[i];
		int index = 0;
		if (p & 0xFF000000) index = closestPalette(p, palette + hasTransparent, nColors - hasTransparent) + hasTransparent;
		txel[i / pixelsPerByte] |= index << (bitsPerPixel * (i & (pixelsPerByte - 1)));
	}

	//update texture info
	if (params->dest->palette.pal) free(params->dest->palette.pal);
	if (params->dest->texels.cmp) free(params->dest->texels.cmp);
	if (params->dest->texels.texel) free(params->dest->texels.texel);
	params->dest->palette.nColors = nColors;
	params->dest->palette.pal = (COLOR *) calloc(nColors, 2);
	params->dest->texels.cmp = NULL;
	params->dest->texels.texel = txel;
	memcpy(params->dest->palette.name, params->pnam, 16);

	unsigned int param = (params->fmt << 26) | (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23);
	if (hasTransparent) param |= (1 << 29);
	params->dest->texels.texImageParam = param;

	for (int i = 0; i < nColors; i++) {
		params->dest->palette.pal[i] = ColorConvertToDS(palette[i]);
	}
	free(palette);
	return 0;
}

int textureConvertTranslucent(CREATEPARAMS *params) {
	//convert to translucent. First, generate a palette of colors.
	int nColors = 0, alphaShift = 0, alphaMax = 0;
	int width = params->width, height = params->height;
	switch (params->fmt) {
		case CT_A3I5:
			nColors = 32;
			alphaShift = 5;
			alphaMax = 7;
			break;
		case CT_A5I3:
			nColors = 8;
			alphaShift = 3;
			alphaMax = 31;
			break;
	}
	if (params->useFixedPalette) nColors = min(nColors, params->colorEntries);
	COLOR32 *palette = (COLOR32 *) calloc(nColors, 4);

	if (!params->useFixedPalette) {
		//generate a palette, making sure to leave a transparent color, if applicable.
		createPaletteSlow(params->px, width, height, palette, nColors);

		//reduce palette color depth
		for (int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(ColorConvertToDS(palette[i]));
		}
	} else {
		for (int i = 0; i < nColors; i++) {
			palette[i] = ColorConvertFromDS(params->fixedPalette[i]);
		}
	}

	//allocate texel space.
	int nBytes = width * height;
	uint8_t *txel = (uint8_t *) calloc(nBytes, 1);
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;
	ditherImagePalette(params->px, width, height, palette, nColors, FALSE, FALSE, FALSE, diffuse);

	//write texel data.
	for (int i = 0; i < width * height; i++) {
		COLOR32 p = params->px[i];
		int index = closestPalette(p, palette, nColors);
		int alpha = (((p >> 24) & 0xFF) * alphaMax + 127) / 255;
		txel[i] = index | (alpha << alphaShift);
		if (params->ditherAlpha) {				
			int backAlpha = (alpha * 255 + (alphaMax >> 1)) / alphaMax;
			int errorAlpha = backAlpha - ((p >> 24) & 0xFF);
			doDiffuse(i, width, height, params->px, 0, 0, 0, -errorAlpha, params->diffuseAmount);
		}
	}

	//update texture info
	if (params->dest->palette.pal) free(params->dest->palette.pal);
	if (params->dest->texels.cmp) free(params->dest->texels.cmp);
	if (params->dest->texels.texel) free(params->dest->texels.texel);
	params->dest->palette.nColors = nColors;
	params->dest->palette.pal = (COLOR *) calloc(nColors, 2);
	params->dest->texels.cmp = NULL;
	params->dest->texels.texel = txel;
	memcpy(params->dest->palette.name, params->pnam, 16);

	unsigned int param = (params->fmt << 26) | (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23);
	params->dest->texels.texImageParam = param;

	for (int i = 0; i < nColors; i++) {
		params->dest->palette.pal[i] = ColorConvertToDS(palette[i]);
	}
	free(palette);
	return 0;
}

//blend two colors together by weight. (out of 8)
COLOR32 blend(COLOR32 col1, int weight1, COLOR32 col2, int weight2) {
	int r1 = col1 & 0xFF;
	int g1 = (col1 >> 8) & 0xFF;
	int b1 = (col1 >> 16) & 0xFF;
	int r2 = col2 & 0xFF;
	int g2 = (col2 >> 8) & 0xFF;
	int b2 = (col2 >> 16) & 0xFF;
	int r3 = (r1 * weight1 + r2 * weight2) >> 3;
	int g3 = (g1 * weight1 + g2 * weight2) >> 3;
	int b3 = (b1 * weight1 + b2 * weight2) >> 3;
	return r3 | (g3 << 8) | (b3 << 16);
}

volatile _globColors = 0;
volatile _globFinal = 0;
volatile _globFinished = 0;

typedef struct {
	uint8_t rgb[64];           //the tile's initial RGBA color data
	uint16_t used;             //marks a used tile
	uint16_t mode;             //the tile's working palette mode
	COLOR palette[4];          //the tile's initial color palette
	uint16_t paletteIndex;     //the tile's working palette index
	uint8_t transparentPixels; //number of transparent pixels
	uint8_t duplicate;         //is duplicate?
} TILEDATA;

int createPaletteFromHistogram(REDUCTION *reduction, int nColors, COLOR32 *out) {
	reduction->nPaletteColors = nColors;
	flattenHistogram(reduction);
	optimizePalette(reduction);

	int nUsed = reduction->nUsedColors;
	for (int i = 0; i < nColors; i++) {
		if (i < nUsed) {
			uint8_t *c = &reduction->paletteRgb[i][0];
			out[i] = c[0] | (c[1] << 8) | (c[2] << 16);
		} else {
			out[i] = 0;
		}
	}

	qsort(out, nColors, sizeof(COLOR32), lightnessCompare);
	return nUsed;
}

double computeInterpolatedError(REDUCTION *reduction, COLOR32 *px, int nPx, COLOR c1, COLOR c2, int transparent, double maxError) {
	//expand palette
	COLOR32 col0 = ColorConvertFromDS(c1);
	COLOR32 col1 = ColorConvertFromDS(c2);
	COLOR32 col2 = 0, col3 = 0;
	if (!transparent) {
		col2 = blend(col0, 3, col1, 5);
		col3 = blend(col0, 5, col1, 3);
	} else {
		col2 = blend(col0, 4, col1, 4);
	}
	int nColors = 3 + !transparent;
	COLOR32 palette[] = { col0, col1, col2, col3 };

	return computePaletteErrorYiq(reduction, px, nPx, palette, nColors, 128, maxError);
}

double testBlockAdd(REDUCTION *reduction, COLOR32 *px, int nPx, int transparent, COLOR *pc1, COLOR *pc2, int amt, int cshift, double error) {
	//try adding to color 1
	int channel = (*pc1 >> cshift) & 0x1F;
	if ((amt < 0 && channel >= -amt) || (amt > 0 && channel <= 31 - amt)) { //check for over/underflows
		*pc1 -= (amt << cshift);
		double err2 = computeInterpolatedError(reduction, px, nPx, *pc1, *pc2, transparent, error);
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
		double err2 = computeInterpolatedError(reduction, px, nPx, *pc1, *pc2, transparent, error);
		if (err2 < error) {
			error = err2;
		} else {
			*pc2 += (amt << cshift);
		}
	}

	//whatever error we settled on...
	return error;
}

double testBlockStep(REDUCTION *reduction, COLOR32 *px, int nPx, int transparent, COLOR *c1, COLOR *c2, int channel, double error) {
	double newErr = testBlockAdd(reduction, px, nPx, transparent, c1, c2, 1, 5 * channel, error); //add
	if (newErr < error) {
		error = newErr;
	} else {
		error = testBlockAdd(reduction, px, nPx, transparent, c1, c2, -1, 5 * channel, error);   //subtract
	}
	return error;
}

void getColorBounds(REDUCTION *reduction, COLOR32 *px, int nPx, COLOR32 *colorMin, COLOR32 *colorMax) {
	//if only 1 or 2 colors, fill the palette with those.
	
	COLOR32 colors[2];
	int nColors = 0;
	int transparent = 0;
	for (int i = 0; i < nPx; i++) {
		COLOR32 col = px[i];
		if ((col >> 24) < 0x80) {
			transparent = 1;
			continue;
		}
		if (nColors == 0) {
			colors[0] = col;
			nColors++;
		} else if (nColors == 1 && col != colors[0]) {
			colors[1] = col;
			nColors++;
		} else if (nColors == 2 && col != colors[0] && col != colors[1]) {
			nColors++;
			break;
		}
	}
	if (nColors <= 2) {
		if (nColors == 0) {
			*colorMin = 0;
			*colorMax = 0;
		} else if (nColors == 1) {
			*colorMin = colors[0];
			*colorMax = colors[0]; //color 1 doesn't exist dumbass!
		} else {
			int y1, u1, v1, y2, u2, v2;
			convertRGBToYUV(colors[0] & 0xFF, (colors[0] >> 8) & 0xFF, (colors[0] >> 16) & 0xFF, &y1, &u1, &v1);
			convertRGBToYUV(colors[1] & 0xFF, (colors[1] >> 8) & 0xFF, (colors[1] >> 16) & 0xFF, &y2, &u2, &v2);
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
	int yiq1[4], yiq2[4], rgb1[4], rgb2[4];
	HIST_ENTRY *firstEntry, *lastEntry;
	resetHistogram(reduction);
	computeHistogram(reduction, px, 4, nPx / 4);
	flattenHistogram(reduction);
	sortHistogram(reduction, 0, reduction->histogram->nEntries);

	//choose first and last colors along the principal axis (greatest Y is at the end)
	firstEntry = reduction->histogramFlat[0];
	lastEntry = reduction->histogramFlat[reduction->histogram->nEntries - 1];
	yiq1[0] = firstEntry->y, yiq1[1] = firstEntry->i, yiq1[2] = firstEntry->q, yiq1[3] = 0xFF;
	yiq2[0] = lastEntry->y, yiq2[1] = lastEntry->i, yiq2[2] = lastEntry->q, yiq2[3] = 0xFF;
	yiqToRgb(rgb1, yiq1);
	yiqToRgb(rgb2, yiq2);

	//round to nearest colors.
	COLOR32 full1 = rgb1[0] | (rgb1[1] << 8) | (rgb1[2] << 16);
	COLOR32 full2 = rgb2[0] | (rgb2[1] << 8) | (rgb2[2] << 16);
	COLOR c1 = ColorConvertToDS(full1);
	COLOR c2 = ColorConvertToDS(full2);

	//try out varying the RGB values. Start G, then R, then B. Do this a few times.
	double error = computeInterpolatedError(reduction, px, nPx, c1, c2, transparent, 1e32);
	for (int i = 0; i < 2; i++) {
		error = testBlockStep(reduction, px, nPx, transparent, &c1, &c2, COLOR_CHANNEL_G, error);
		error = testBlockStep(reduction, px, nPx, transparent, &c1, &c2, COLOR_CHANNEL_R, error);
		error = testBlockStep(reduction, px, nPx, transparent, &c1, &c2, COLOR_CHANNEL_B, error);
	}

	//sanity check: impose color ordering (high Y must come first)
	int y1, u1, v1, y2, u2, v2;
	full1 = ColorConvertFromDS(c1);
	full2 = ColorConvertFromDS(c2);
	convertRGBToYUV(full1 & 0xFF, (full1 >> 8) & 0xFF, (full1 >> 16) & 0xFF, &y1, &u1, &v1);
	convertRGBToYUV(full2 & 0xFF, (full2 >> 8) & 0xFF, (full2 >> 16) & 0xFF, &y2, &u2, &v2);
	if (y2 > y1) {
		//swap order to keep me sane
		COLOR32 temp = full2;
		full2 = full1;
		full1 = temp;
	}
	*colorMin = full1;
	*colorMax = full2;
}

int computeColorDifference(COLOR32 c1, COLOR32 c2) {
	int r1 = c1 & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = (c1 >> 16) & 0xFF;
	int r2 = c2 & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = (c2 >> 16) & 0xFF;

	int dy, du, dv;
	//property of linear transformations :)
	convertRGBToYUV(r2 - r1, g2 - g1, b2 - b1, &dy, &du, &dv);

	return 4 * dy * dy + du * du + dv * dv;
}

//compute LMS squared
int computeLMS(COLOR32 *tile, COLOR32 *palette, int transparent) {
	int total = 0, nCount = 0;
	for (int i = 0; i < 16; i++) {
		COLOR32 c = tile[i];
		if (!transparent || (c >> 24) >= 0x80) {
			int closest = closestPalette(c, palette, 4 - transparent);
			COLOR32 chosen = palette[closest];
			total += computeColorDifference(c, chosen);
			nCount++;
		}
	}
	if (nCount == 0) return 0;
	return total / nCount;
}

void choosePaletteAndMode(REDUCTION *reduction, TILEDATA *tile) {
	//first try interpolated. If it's not good enough, use full color.
	COLOR32 colorMin, colorMax;
	getColorBounds(reduction, (COLOR32 *) tile->rgb, 16, &colorMin, &colorMax);
	if (tile->transparentPixels) {
		COLOR32 mid = blend(colorMin, 4, colorMax, 4);
		COLOR32 palette[] = { colorMax, mid, colorMin, 0 };
		COLOR32 paletteFull[4];

		int error = computeLMS((COLOR32 *) tile->rgb, palette, 1);
		resetHistogram(reduction);
		computeHistogram(reduction, (COLOR32 *) tile->rgb, 4, 4);
		int nFull = createPaletteFromHistogram(reduction, 3, paletteFull);
		//if error <= 24, then these colors are good enough
		if (error <= 24 || nFull <= 2) {
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
		COLOR32 mid1 = blend(colorMin, 5, colorMax, 3);
		COLOR32 mid2 = blend(colorMin, 3, colorMax, 5);
		COLOR32 palette[] = { colorMax, mid2, mid1, colorMin };
		COLOR32 paletteFull[4];

		int error = computeLMS((COLOR32 *) tile->rgb, palette, 0);
		resetHistogram(reduction);
		computeHistogram(reduction, (COLOR32 *) tile->rgb, 4, 4);
		int nFull = createPaletteFromHistogram(reduction, 4, paletteFull);
		if (error <= 24 || nFull <= 2) {
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

void addTile(REDUCTION *reduction, TILEDATA *data, int index, COLOR32 *px, int *totalIndex) {
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
		int a = (c >> 24) & 0xFF;
		if (a < 0x80) nTransparentPixels++;
	}
	data[index].transparentPixels = nTransparentPixels;
	
	//is it a duplicate?
	int isDuplicate = 0;
	int duplicateIndex = 0;
	for (int i = index - 1; i >= 0; i--) {
		TILEDATA *tile = data + i;
		COLOR32 *px1 = (COLOR32 *) tile->rgb;
		COLOR32 *px2 = (COLOR32 *) data[index].rgb;

		if (!memcmp(px1, px2, 16 * sizeof(COLOR32))) {
			isDuplicate = 1;
			duplicateIndex = i;
			break;
		}
	}

	if (isDuplicate) {
		memcpy(data + index, data + duplicateIndex, sizeof(TILEDATA));
		data[index].duplicate = 1;
		data[index].paletteIndex = data[duplicateIndex].paletteIndex;
	} else {
		//generate a palette and determine the mode.
		choosePaletteAndMode(reduction, data + index);
		data[index].paletteIndex = *totalIndex;
		//is the palette and mode identical to a non-duplicate tile?
		for (int i = index - 1; i >= 0; i--) {
			TILEDATA *tile1 = data + i;
			TILEDATA *tile2 = data + index;
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
	}
	if (!data[index].duplicate) {
		int nPalettes = 1;
		if (!(data[index].mode & COMP_INTERPOLATE)) {
			nPalettes = 2;
		}
		*totalIndex += nPalettes;
	}
	_globColors++;
}

TILEDATA *createTileData(REDUCTION *reduction, COLOR32 *px, int tilesX, int tilesY) {
	TILEDATA *data = (TILEDATA *) calloc(tilesX * tilesY, sizeof(TILEDATA));
	int paletteIndex = 0;
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			COLOR32 tile[16];
			int offs = x * 4 + y * 4 * tilesX * 4;
			memcpy(tile, px + offs, 16);
			memcpy(tile + 4, px + offs + tilesX * 4, 16);
			memcpy(tile + 8, px + offs + tilesX * 8, 16);
			memcpy(tile + 12, px + offs + tilesX * 12, 16);
			addTile(reduction, data, x + y * tilesX, tile, &paletteIndex);
		}
	}
	return data;
}

int getColorsFromTable(uint8_t type) {
	if (type == 0) return 0;
	if (type == 2 || type == 8) return 2;
	return 4;
}

uint16_t getModeFromTable(uint8_t type) {
	if (type == 1) return COMP_TRANSPARENT | COMP_FULL;
	if (type == 2) return COMP_TRANSPARENT | COMP_INTERPOLATE;
	if (type == 4) return COMP_OPAQUE | COMP_FULL;
	if (type == 8) return COMP_OPAQUE | COMP_INTERPOLATE;
	return COMP_TRANSPARENT | COMP_FULL;
}

int computePaletteDifference(COLOR *pal1, COLOR *pal2, int nColors, int nMaxError) {
	int total = 0;
	
	for (int i = 0; i < nColors; i++) {
		if (pal1[i] != pal2[i]) {
			COLOR32 c1 = ColorConvertFromDS(pal1[i]);
			COLOR32 c2 = ColorConvertFromDS(pal2[i]);
			total += computeColorDifference(c1, c2);
		}

		if (total >= nMaxError) return nMaxError;
		if (nColors == 2 && total * 2 >= nMaxError) return nMaxError;
	}

	if (nColors == 2) total *= 2;
	return total;
}

int findClosestPalettes(COLOR *palette, uint8_t *colorTable, int nColors, int *colorIndex1, int *colorIndex2) {
	//determine which two palettes are the most similar. For 2-color palettes, multiply difference by 2.
	int leastDistance = 0x10000000;
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
			int nColorsInSecondPalette = getColorsFromTable(type2);
			if (type2 != type1) {
				idx2 += nColorsInSecondPalette;
				continue;
			}

			//same type, let's compare.
			int dst = computePaletteDifference(palette + idx1, palette + idx2, nColorsInThisPalette, leastDistance);
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

void mergePalettes(REDUCTION *reduction, TILEDATA *tileData, int nTiles, COLOR *palette, int paletteIndex, uint16_t palettesMode) {
	//count the number of tiles that use this palette.
	int nUsedTiles = 0;
	for (int i = 0; i < nTiles; i++) {
		if (tileData[i].paletteIndex == paletteIndex) nUsedTiles++;
	}

	//use the mode to determine the appropriate method of creating the palette.
	COLOR32 expandPal[4];
	if (palettesMode == (COMP_TRANSPARENT | COMP_FULL)) {
		//transparent, full color
		resetHistogram(reduction);
		for (int i = 0; i < nTiles; i++) {
			if (tileData[i].paletteIndex == paletteIndex) {
				computeHistogram(reduction, (COLOR32 *) tileData[i].rgb, 4, 4);
			}
		}
		createPaletteFromHistogram(reduction, 3, expandPal + 1);

		palette[paletteIndex * 2 + 0] = ColorConvertToDS(expandPal[2]); //don't waste this slot
		palette[paletteIndex * 2 + 1] = ColorConvertToDS(expandPal[1]);
		palette[paletteIndex * 2 + 2] = ColorConvertToDS(expandPal[2]);
		palette[paletteIndex * 2 + 3] = ColorConvertToDS(expandPal[0]);
	} else if (palettesMode & COMP_INTERPOLATE) {
		//transparent, interpolated, and opaque, interpolated

		//allocate space for all of the color data
		COLOR32 *px = (COLOR32 *) calloc(nUsedTiles, 16 * 4);

		//copy tiles into the buffer
		int copiedTiles = 0;
		for (int i = 0; i < nTiles; i++) {
			if (tileData[i].paletteIndex == paletteIndex) {
				memcpy(px + copiedTiles * 16, tileData[i].rgb, 16 * 4);
				copiedTiles++;
			}
		}
		getColorBounds(reduction, px, 16 * nUsedTiles, &expandPal[0], &expandPal[1]);
		free(px);

		palette[paletteIndex * 2 + 0] = ColorConvertToDS(expandPal[1]);
		palette[paletteIndex * 2 + 1] = ColorConvertToDS(expandPal[0]);
	} else if (palettesMode == (COMP_OPAQUE | COMP_FULL)) {
		//opaque, full color
		resetHistogram(reduction);
		for (int i = 0; i < nTiles; i++) {
			if (tileData[i].paletteIndex == paletteIndex) {
				computeHistogram(reduction, (COLOR32 *) tileData[i].rgb, 4, 4);
			}
		}
		int nFull = createPaletteFromHistogram(reduction, 4, expandPal);

		if (nFull < 4) expandPal[0] = expandPal[1];
		palette[paletteIndex * 2 + 0] = ColorConvertToDS(expandPal[3]);
		palette[paletteIndex * 2 + 1] = ColorConvertToDS(expandPal[1]);
		palette[paletteIndex * 2 + 2] = ColorConvertToDS(expandPal[2]);
		palette[paletteIndex * 2 + 3] = ColorConvertToDS(expandPal[0]);
	}
}

int buildPalette(REDUCTION *reduction, COLOR *palette, int nPalettes, TILEDATA *tileData, int tilesX, int tilesY, int threshold) {
	//iterate over all non-duplicate tiles, adding the palettes.
	//colorTable keeps track of how each color is intended to be used.
	//00 - unused. 01 - mode 0x0000. 02 - mode 0x4000. 04 - mode 0x8000. 08 - mode 0xC000.
	uint8_t *colorTable = (uint8_t *) calloc(nPalettes * 2, 1);
	int diffThreshold = threshold * threshold * 52; //threshold 0-100, square normalized to 0-1040400/2
	int firstSlot = 0;
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int index = x + y * tilesX;
			TILEDATA *tile = tileData + index;
			if (tile->duplicate) {
				//the paletteIndex field of a duplicate tile is first set to the tile index it is a duplicate of.
				//set it to an actual palette index here.
				_globColors++;
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
					int distance = findClosestPalettes(palette, colorTable, firstSlot, &colorIndex1, &colorIndex2);
					if (colorIndex1 == -1) break;
					if (fits && (distance > diffThreshold || firstSlot < 8)) break;
					int nColorsInPalettes = getColorsFromTable(colorTable[colorIndex1]);
					uint16_t palettesMode = getModeFromTable(colorTable[colorIndex1]);

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
					memmove(palette + colorIndex2, palette + colorIndex2 + nColorsInPalettes, (nPalettes * 2 - colorIndex2 - nColorsInPalettes) * sizeof(COLOR));
					memmove(colorTable + colorIndex2, colorTable + colorIndex2 + nColorsInPalettes, nPalettes * 2 - colorIndex2 - nColorsInPalettes);

					//merge those palettes that we've just combined.
					mergePalettes(reduction, tileData, tilesX * tilesY, palette, colorIndex1 / 2, palettesMode);

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
			_globColors++;
		}
	}
	free(colorTable);
	return firstSlot;
}

void expandPalette(COLOR *nnsPal, uint16_t mode, COLOR32 *dest, int *nOpaque) {
	dest[0] = ColorConvertFromDS(nnsPal[0]);
	dest[1] = ColorConvertFromDS(nnsPal[1]);
	mode &= COMP_MODE_MASK;
	if (mode & COMP_OPAQUE) *nOpaque = 4;
	else *nOpaque = 3;
	
	if (mode == (COMP_OPAQUE | COMP_FULL)) {
		dest[2] = ColorConvertFromDS(nnsPal[2]);
		dest[3] = ColorConvertFromDS(nnsPal[3]);
	} else if (mode == (COMP_TRANSPARENT | COMP_FULL)) {
		dest[2] = ColorConvertFromDS(nnsPal[2]);
		dest[3] = 0;
	} else if (mode == (COMP_TRANSPARENT | COMP_INTERPOLATE)) {
		dest[2] = blend(dest[0], 4, dest[1], 4);
		dest[3] = 0;
	} else if (mode == (COMP_OPAQUE | COMP_INTERPOLATE)) {
		dest[2] = blend(dest[0], 5, dest[1], 3);
		dest[3] = blend(dest[0], 3, dest[1], 5);
	}
}

uint16_t findOptimalPidx(REDUCTION *reduction, COLOR32 *px, int hasTransparent, COLOR *palette, int nColors) {
	//yes, iterate over every possible palette and mode.
	double leastError = 1e32;
	uint16_t leastPidx = 0;
	for (int i = 0; i < nColors; i += 2) {
		COLOR *thisPalette = palette + i;
		COLOR32 expand[4];
		
		for (int j = 0; j < 4; j++) {
			int nConsumed = 2;
			if (j == 0 || j == 2) nConsumed = 4;
			if (i + nConsumed > nColors) continue;

			//nothing to gain from these modes sometimes
			if (!hasTransparent && j == 0) continue;
			if (hasTransparent && j >= 2) break;
			
			uint16_t mode = j << 14;
			expandPalette(thisPalette, mode, expand, &nConsumed);
			if (hasTransparent && nConsumed == 4) continue;

			//unsigned long long dst = computeLMS(px, expand, nConsumed == 3); //35.36s
			//unsigned long long dst = computePaletteError(px, 16, expand, 4 - (nConsumed == 3), 128, leastError); //22.26s, lot faster
			double dst = computePaletteErrorYiq(reduction, px, 16, expand, 4 - (nConsumed == 3), 128, leastError);
			if (dst < leastError) {
				leastPidx = mode | (i >> 1);
				leastError = dst;
			}
		}
	}
	return leastPidx;
}

int textureConvert4x4(CREATEPARAMS *params) {
	//3-stage compression. First stage builds tile data, second stage builds palettes, third stage builds the final texture.
	if (params->colorEntries < 16) params->colorEntries = 16;
	params->colorEntries = (params->colorEntries + 7) & 0xFFFFFFF8;
	int width = params->width, height = params->height;
	int tilesX = width / 4, tilesY = height / 4;
	_globFinal = tilesX * tilesY * 3;
	_globColors = 0;

	//create tile data
	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, BALANCE_DEFAULT, BALANCE_DEFAULT, 15, 0, 4);
	TILEDATA *tileData = createTileData(reduction, params->px, tilesX, tilesY);

	//build the palettes.
	COLOR *nnsPal = (COLOR *) calloc(params->colorEntries, sizeof(COLOR));
	int nUsedColors;
	if (!params->useFixedPalette) {
		nUsedColors = buildPalette(reduction, nnsPal, params->colorEntries / 2, tileData, tilesX, tilesY, params->threshold);
	} else {
		nUsedColors = params->colorEntries;
		memcpy(nnsPal, params->fixedPalette, params->colorEntries * 2);
		_globColors += tilesX * tilesY;
	}
	if (nUsedColors & 7) nUsedColors += 8 - (nUsedColors & 7);
	if (nUsedColors < 16) nUsedColors = 16;

	//allocate index data.
	uint16_t *pidx = (uint16_t *) calloc(tilesX * tilesY, 2);

	//generate texel data.
	uint32_t *txel = (uint32_t *) calloc(tilesX * tilesY, 4);
	float diffuse = params->dither ? params->diffuseAmount : 0.0f;
	for (int i = 0; i < tilesX * tilesY; i++) {
		uint32_t texel = 0;

		//double check that these settings are the most optimal for this tile.
		uint16_t idx = findOptimalPidx(reduction, (COLOR32 *) tileData[i].rgb, tileData[i].transparentPixels, nnsPal, nUsedColors);
		uint16_t mode = idx & 0xC000;
		uint16_t index = idx & 0x3FFF;
		COLOR *thisPalette = nnsPal + (index * 2);
		pidx[i] = idx;

		COLOR32 palette[4];
		int paletteSize;
		expandPalette(thisPalette, mode, palette, &paletteSize);

		//if dither is enabled, do so here.
		ditherImagePalette((COLOR32 *) tileData[i].rgb, 4, 4, palette, paletteSize, 0, 1, 0, diffuse);

		for (int j = 0; j < 16; j++) {
			int index = 0;
			COLOR32 col = ((COLOR32 *) tileData[i].rgb)[j];
			if ((col >> 24) < 0x80) {
				index = 3;
			} else {
				index = closestPalette(col, palette, paletteSize);
			}
			texel |= index << (j * 2);
		}
		txel[i] = texel;
		_globColors++;
	}
	destroyReduction(reduction);
	free(reduction);

	//set fields in the texture
	params->dest->palette.nColors = nUsedColors;
	if (params->dest->palette.pal) free(params->dest->palette.pal);
	params->dest->palette.pal = nnsPal;
	memcpy(params->dest->palette.name, params->pnam, 16);
	if (params->dest->texels.cmp) free(params->dest->texels.cmp);
	if (params->dest->texels.texel) free(params->dest->texels.texel);
	params->dest->texels.cmp = (short *) pidx;
	params->dest->texels.texel = (char *) txel;
	params->dest->texels.texImageParam = (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23) | (params->fmt << 26);
	
	free(tileData);
	return 0;
}

int textureConvert(CREATEPARAMS *params) {
	//begin conversion.
	switch (params->fmt) {
		case CT_DIRECT:
			textureConvertDirect(params);
			break;
		case CT_4COLOR:
		case CT_16COLOR:
		case CT_256COLOR:
			textureConvertPalette(params);
			break;
		case CT_A3I5:
		case CT_A5I3:
			textureConvertTranslucent(params);
			break;
		case CT_4x4:
			textureConvert4x4(params);
			break;
	}
	textureRender(params->px, &params->dest->texels, &params->dest->palette, 0);
	//textureRender outputs red and blue in the opposite order, so flip them here.
	for (int i = 0; i < params->width * params->height; i++) {
		COLOR32 p = params->px[i];
		params->px[i] = REVERSE(p);
	}
	_globFinished = 1;
	if(params->callback) params->callback(params->callbackParam);
	if (params->useFixedPalette) free(params->fixedPalette);
	return 0;
}

DWORD CALLBACK textureStartConvertThreadEntry(LPVOID lpParam) {
	CREATEPARAMS *params = (CREATEPARAMS *) lpParam;
	return textureConvert(params);
}

HANDLE textureConvertThreaded(COLOR32 *px, int width, int height, int fmt, int dither, float diffuse, int ditherAlpha, int colorEntries, int useFixedPalette, COLOR *fixedPalette, int threshold, char *pnam, TEXTURE *dest, void (*callback) (void *), void *callbackParam) {
	CREATEPARAMS *params = (CREATEPARAMS *) calloc(1, sizeof(CREATEPARAMS));
	_globFinished = 0;
	params->px = px;
	params->width = width;
	params->height = height;
	params->fmt = fmt;
	params->dither = dither;
	params->diffuseAmount = diffuse;
	params->ditherAlpha = ditherAlpha;
	params->colorEntries = colorEntries;
	params->threshold = threshold;
	params->dest = dest;
	params->callback = callback;
	params->callbackParam = callbackParam;
	params->useFixedPalette = useFixedPalette;
	params->fixedPalette = useFixedPalette ? fixedPalette : NULL;
	memcpy(params->pnam, pnam, strlen(pnam) + 1);
	return CreateThread(NULL, 0, textureStartConvertThreadEntry, (LPVOID) params, 0, NULL);
}