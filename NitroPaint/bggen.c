#include "bggen.h"
#include "color.h"
#include "palette.h"


float tileDifferenceFlip(RxReduction *reduction, BGTILE *t1, BGTILE *t2, unsigned char mode) {
	double err = 0.0;
	COLOR32 *px1 = t1->px;
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {

			int x2 = (mode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (mode & TILE_FLIPY) ? (7 - y) : y;

			RxYiqColor *yiq1 = &t1->pxYiq[x + y * 8];
			RxYiqColor *yiq2 = &t2->pxYiq[x2 + y2 * 8];
			double dy = reduction->lumaTable[yiq1->y] - reduction->lumaTable[yiq2->y];
			double di = yiq1->i - yiq2->i;
			double dq = yiq1->q - yiq2->q;
			double da = yiq1->a - yiq2->a;
			err += yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq + 1600 * da * da;
		}
	}

	return (float) err;
}

float tileDifference(RxReduction *reduction, BGTILE *t1, BGTILE *t2, unsigned char *flipMode) {
	float err = tileDifferenceFlip(reduction, t1, t2, 0);
	if (err == 0) {
		*flipMode = 0;
		return err;
	}
	float err2 = tileDifferenceFlip(reduction, t1, t2, TILE_FLIPX);
	if (err2 == 0) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	float err3 = tileDifferenceFlip(reduction, t1, t2, TILE_FLIPY);
	if (err3 == 0) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	float err4 = tileDifferenceFlip(reduction, t1, t2, TILE_FLIPXY);
	if (err4 == 0) {
		*flipMode = TILE_FLIPXY;
		return err4;
	}

	if (err <= err2 && err <= err3 && err <= err4) {
		*flipMode = 0;
		return err;
	}
	if (err2 <= err && err2 <= err3 && err2 <= err4) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	if (err3 <= err && err3 <= err2 && err3 <= err4) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	if (err4 <= err && err4 <= err2 && err4 <= err3) {
		*flipMode = TILE_FLIPXY;
		return err4;
	}
	*flipMode = 0;
	return err;
}

void bgAddTileToTotal(RxReduction *reduction, int *pxBlock, BGTILE *tile) {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			COLOR32 col = tile->px[x + y * 8];

			int x2 = (tile->flipMode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (tile->flipMode & TILE_FLIPY) ? (7 - y) : y;
			int *dest = pxBlock + 4 * (x2 + y2 * 8);

			RxYiqColor yiq;
			RxConvertRgbToYiq(col, &yiq);
			dest[0] += (int) (16.0 * reduction->lumaTable[yiq.y] + 0.5f);
			dest[1] += yiq.i;
			dest[2] += yiq.q;
			dest[3] += yiq.a;
		}
	}
}

typedef struct TILE_DIFF_ {
	int tile1;
	int tile2;
	double diff;		//post-biased
} TILE_DIFF;

typedef struct TILE_DIFF_LIST_ {
	TILE_DIFF *diffBuff;
	int diffBuffSize;
	int diffBuffLength;
	double minDiff;
	double maxDiff;
} TILE_DIFF_LIST;

void tdlInit(TILE_DIFF_LIST *list, int nEntries) {
	list->diffBuffSize = nEntries;
	list->diffBuffLength = 0;
	list->minDiff = 1e32;
	list->maxDiff = 0;
	list->diffBuff = (TILE_DIFF *) calloc(list->diffBuffSize, sizeof(TILE_DIFF));
}

void tdlFree(TILE_DIFF_LIST *list) {
	free(list->diffBuff);
	list->diffBuff = NULL;
	list->diffBuffLength = 0;
	list->diffBuffSize = 0;
}

void tdlAdd(TILE_DIFF_LIST *list, int tile1, int tile2, double diff) {
	if (list->diffBuffLength == list->diffBuffSize && diff >= list->maxDiff) return;

	//find an insertion point
	//TODO: binary search
	int destIndex = list->diffBuffLength;
	if (diff < list->minDiff) {
		destIndex = 0;
	} else {
		for (int i = 0; i < list->diffBuffLength; i++) {
			if (diff < list->diffBuff[i].diff) {
				destIndex = i;
				break;
			}
		}
	}

	//insert
	int nEntriesToMove = list->diffBuffLength - destIndex;
	int added = 1; //was a new entry created?
	if (destIndex + 1 + nEntriesToMove > list->diffBuffSize) {
		nEntriesToMove = list->diffBuffSize - destIndex - 1;
		added = 0;
	}
	memmove(list->diffBuff + destIndex + 1, list->diffBuff + destIndex, nEntriesToMove * sizeof(TILE_DIFF));
	list->diffBuff[destIndex].tile1 = tile1;
	list->diffBuff[destIndex].tile2 = tile2;
	list->diffBuff[destIndex].diff = diff;
	if (added) {
		list->diffBuffLength++;
	}
	list->minDiff = list->diffBuff[0].diff;
	list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
}

void tdlRemoveAll(TILE_DIFF_LIST *list, int tile1, int tile2) {
	//remove all diffs involving tile1 and tile2
	for (int i = 0; i < list->diffBuffLength; i++) {
		TILE_DIFF *td = list->diffBuff + i;
		if (td->tile1 == tile1 || td->tile2 == tile1 || td->tile1 == tile2 || td->tile2 == tile2) {
			memmove(td, td + 1, (list->diffBuffLength - i - 1) * sizeof(TILE_DIFF));
			list->diffBuffLength--;
			i--;
		}
	}
	if (list->diffBuffLength > 0) {
		list->minDiff = list->diffBuff[0].diff;
		list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
	}
}

void tdlPop(TILE_DIFF_LIST *list, TILE_DIFF *out) {
	if (list->diffBuffLength > 0) {
		memcpy(out, list->diffBuff, sizeof(TILE_DIFF));
		memmove(list->diffBuff, list->diffBuff + 1, (list->diffBuffLength - 1) * sizeof(TILE_DIFF));
		list->diffBuffLength--;
		if (list->diffBuffLength > 0) {
			list->minDiff = list->diffBuff[0].diff;
		}
	}
}

void tdlReset(TILE_DIFF_LIST *list) {
	list->diffBuffLength = 0;
	list->maxDiff = 0;
	list->minDiff = 1e32;
}

int performCharacterCompression(BGTILE *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes,
	int paletteBase, int paletteOffset, int balance, int colorBalance, int *progress) {
	int nChars = nTiles;
	float *diffBuff = (float *) calloc(nTiles * nTiles, sizeof(float));
	unsigned char *flips = (unsigned char *) calloc(nTiles * nTiles, 1); //how must each tile be manipulated to best match its partner

	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, 15, 0, 255);
	for (int i = 0; i < nTiles; i++) {
		BGTILE *t1 = tiles + i;
		for (int j = 0; j < i; j++) {
			BGTILE *t2 = tiles + j;

			diffBuff[i + j * nTiles] = tileDifference(reduction, t1, t2, &flips[i + j * nTiles]);
			diffBuff[j + i * nTiles] = diffBuff[i + j * nTiles];
			flips[j + i * nTiles] = flips[i + j * nTiles];
		}
		*progress = (i * i) / nTiles * 500 / nTiles;
	}

	//first, combine tiles with a difference of 0.

	for (int i = 0; i < nTiles; i++) {
		BGTILE *t1 = tiles + i;
		if (t1->masterTile != i) continue;

		for (int j = 0; j < i; j++) {
			BGTILE *t2 = tiles + j;
			if (t2->masterTile != j) continue;

			if (diffBuff[i + j * nTiles] == 0) {
				//merge all tiles with master index i to j
				for (int k = 0; k < nTiles; k++) {
					if (tiles[k].masterTile == i) {
						tiles[k].masterTile = j;
						tiles[k].flipMode ^= flips[i + j * nTiles];
						tiles[k].nRepresents = 0;
						tiles[j].nRepresents++;
					}
				}
				nChars--;
				if (nTiles > nMaxChars) *progress = 500 + (int) (500 * sqrt((float) (nTiles - nChars) / (nTiles - nMaxChars)));
			}
		}
	}

	//still too many? 
	if (nChars > nMaxChars) {
		//damn

		//create a rolling buffer of similar tiles. 
		//when tiles are combined, combinations that involve affected tiles in the array are removed.
		//fill it to capacity initially, then keep using it until it's empty, then fill again.
		TILE_DIFF_LIST tdl;
		tdlInit(&tdl, 64);

		//keep finding the most similar tile until we get character count down
		int direction = 0;
		while (nChars > nMaxChars) {
			for (int iOuter = 0; iOuter < nTiles; iOuter++) {
				int i = direction ? (nTiles - 1 - iOuter) : iOuter; //criss cross the direction
				BGTILE *t1 = tiles + i;
				if (t1->masterTile != i) continue;

				for (int j = 0; j < i; j++) {
					BGTILE *t2 = tiles + j;
					if (t2->masterTile != j) continue;

					double thisErrorEntry = diffBuff[i + j * nTiles];
					double thisError = thisErrorEntry;
					double bias = t1->nRepresents + t2->nRepresents;
					bias *= bias;

					thisError = thisErrorEntry * bias;
					tdlAdd(&tdl, j, i, thisError);
				}
			}

			//now merge tiles while we can
			int tile1, tile2;
			while (tdl.diffBuffLength > 0 && nChars > nMaxChars) {
				TILE_DIFF td;
				tdlPop(&tdl, &td);

				//tile merging
				tile1 = td.tile1;
				tile2 = td.tile2;

				//should we swap tile1 and tile2? tile2 should have <= tile1's nRepresents
				if (tiles[tile2].nRepresents > tiles[tile1].nRepresents) {
					int t = tile1;
					tile1 = tile2;
					tile2 = t;
				}

				//merge tile1 and tile2. All tile2 tiles become tile1 tiles
				unsigned char flipDiff = flips[tile1 + tile2 * nTiles];
				for (int i = 0; i < nTiles; i++) {
					if (tiles[i].masterTile == tile2) {
						tiles[i].masterTile = tile1;
						tiles[i].flipMode ^= flipDiff;
						tiles[i].nRepresents = 0;
						tiles[tile1].nRepresents++;
					}
				}

				nChars--;
				*progress = 500 + (int) (500 * sqrt((float) (nTiles - nChars) / (nTiles - nMaxChars)));

				tdlRemoveAll(&tdl, td.tile1, td.tile2);
			}
			direction = !direction;
			tdlReset(&tdl);
		}
		tdlFree(&tdl);
	}

	free(flips);
	free(diffBuff);

	//try to make the compressed result look less bad
	for (int i = 0; i < nTiles; i++) {
		if (tiles[i].masterTile != i) continue;
		if (tiles[i].nRepresents <= 1) continue; //no averaging required for just one tile
		BGTILE *tile = tiles + i;

		//average all tiles that use this master tile.
		int pxBlock[64 * 4] = { 0 };
		int nRep = tile->nRepresents;
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			BGTILE *tile2 = tiles + j;
			bgAddTileToTotal(reduction, pxBlock, tile2);
		}

		//divide by count, convert to 32-bit RGB
		for (int j = 0; j < 64 * 4; j++) {
			int ch = pxBlock[j];

			//proper round to nearest
			if (ch >= 0) {
				ch = (ch * 2 + nRep) / (nRep * 2);
			} else {
				ch = (ch * 2 - nRep) / (nRep * 2);
			}
			pxBlock[j] = ch;
		}
		for (int j = 0; j < 64; j++) {
			int cy = pxBlock[j * 4 + 0]; //times 16
			int ci = pxBlock[j * 4 + 1];
			int cq = pxBlock[j * 4 + 2];
			int ca = pxBlock[j * 4 + 3];

			double dcy = ((double) cy) / 16.0;
			cy = (int) (pow(dcy * 0.00195695, 1.0 / reduction->gamma) * 511.0);

			RxYiqColor yiq = { cy, ci, cq, ca };
			RxRgbColor rgb;
			RxConvertYiqToRgb(&rgb, &yiq);

			tile->px[j] = rgb.r | (rgb.g << 8) | (rgb.b << 16) | (ca << 24);
		}

		//try to determine the most optimal palette. Child tiles can be different palettes.
		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits) + paletteOffset + !paletteOffset;
			double err = RxComputePaletteError(reduction, tile->px, 64, pal, paletteSize - !paletteOffset, 128, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//now, match colors to indices.
		COLOR32 *pal = palette + (bestPalette << nBits);
		RxReduceImageEx(tile->px, NULL, 8, 8, pal + paletteOffset + !paletteOffset,
			paletteSize - !paletteOffset, 0, 1, 0, 0.0f, balance, colorBalance, 0);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = RxPaletteFindClosestColorSimple(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset)
					+ !paletteOffset + paletteOffset;
			}

			tile->indices[j] = index;
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;
		}
		tile->palette = bestPalette;

		//lastly, copy tile->indices to all child tile->indices, just to make sure palette and character are in synch.
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			if (j == i) continue;
			BGTILE *tile2 = tiles + j;

			memcpy(tile2->indices, tile->indices, 64);
			tile2->palette = tile->palette;
		}
	}

	RxDestroy(reduction);
	free(reduction);
	return nChars;
}

void setupBgTiles(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse) {
	setupBgTilesEx(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset, dither, diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

void setupBgTilesEx(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors) {
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, 15, enhanceColors, paletteSize);

	if (!dither) diffuse = 0.0f;
	for (int i = 0; i < nTiles; i++) {
		BGTILE *tile = tiles + i;

		//create histogram for tile
		RxHistClear(reduction);
		RxHistAdd(reduction, tile->px, 8, 8);
		RxHistFinalize(reduction);

		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits);
			double err = RxHistComputePaletteError(reduction, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//match colors
		COLOR32 *pal = palette + (bestPalette << nBits);

		//do optional dithering (also matches colors at the same time)
		RxReduceImageEx(tile->px, NULL, 8, 8, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, FALSE, TRUE, FALSE, diffuse, balance, colorBalance, enhanceColors);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = RxPaletteFindClosestColorSimple(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset)
					+ !paletteOffset + paletteOffset;
			}

			tile->indices[j] = index;
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;

			//YIQ color
			RxConvertRgbToYiq(col, &tile->pxYiq[j]);
		}

		tile->masterTile = i;
		tile->nRepresents = 1;
		tile->palette = bestPalette;
	}
	RxDestroy(reduction);
	free(reduction);
}

int findLeastDistanceToColor(COLOR32 *px, int nPx, int destR, int destG, int destB) {
	int leastDistance = 0x7FFFFFFF;
	for (int i = 0; i < nPx; i++) {
		COLOR32 c = px[i];
		if ((c >> 24) < 0x80) continue;

		int dr = (c & 0xFF) - destR;
		int dg = ((c >> 8) & 0xFF) - destG;
		int db = ((c >> 16) & 0xFF) - destB;
		int dy, du, dv;
		RxConvertRgbToYuv(dr, dg, db, &dy, &du, &dv);
		int dd = 4 * dy * dy + du * du + dv * dv;
		if (dd < leastDistance) {
			leastDistance = dd;
		}
	}
	return leastDistance;
}

COLOR32 chooseBGColor0(COLOR32 *px, int width, int height, int mode) {
	//based on mode, determine color 0 mode
	if (mode == BG_COLOR0_FIXED) return 0xFF00FF;

	if (mode == BG_COLOR0_AVERAGE || mode == BG_COLOR0_EDGE) {
		int totalR = 0, totalG = 0, totalB = 0, nColors = 0;
		for (int i = 0; i < height; i++) {
			for (int j = 0; j < width; j++) {
				int index = j + i * width;
				COLOR32 c = px[index];

				int add = 0;
				if (mode == BG_COLOR0_AVERAGE) {
					add = 1;
				} else if (mode == BG_COLOR0_EDGE) {

					//must be opaque and on the edge of opaque pixels
					if ((c >> 24) >= 0x80) {
						if (i == 0 || i == height - 1 || j == 0 || j == width - 1) add = 1;
						else {
							int up = px[index - width] >> 24;
							int down = px[index + width] >> 24;
							int left = px[index - 1] >> 24;
							int right = px[index + 1] >> 24;

							if (up < 0x80 || down < 0x80 || left < 0x80 || right < 0x80) add = 1;
						}
					}

				}

				if (add) {
					totalR += c & 0xFF;
					totalG += (c >> 8) & 0xFF;
					totalB += (c >> 16) & 0xFF;
					nColors++;
				}
			}
		}

		if (nColors > 0) {
			totalR = (totalR + nColors / 2) / nColors;
			totalG = (totalG + nColors / 2) / nColors;
			totalB = (totalB + nColors / 2) / nColors;
			return totalR | (totalG << 8) | (totalB << 16);
		}
	}

	//use an octree to find the space with the least weight
	//in the event of a tie, favor the order RGB, RGb, rGB, rGb, RgB, Rgb, rgB, rgb
	int rMin = 0, rMax = 256, gMin = 0, gMax = 256, bMin = 0, bMax = 256;
	int rMid, gMid, bMid, boxSize = 256;
	for (int i = 0; i < 7; i++) {
		int octreeScores[2][2][2] = { 0 }; //[r][g][b]
		rMid = (rMin + rMax) / 2;
		gMid = (gMin + gMax) / 2;
		bMid = (bMin + bMax) / 2;
		for (int j = 0; j < width * height; j++) {
			COLOR32 c = px[j];
			int a = (c >> 24) & 0xFF;
			if (a < 128) continue;

			//add to bucket if it fits
			int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			if (r < (rMin - boxSize / 2) || r >= (rMax + boxSize / 2)) continue;
			if (g < (gMin - boxSize / 2) || g >= (gMax + boxSize / 2)) continue;
			if (b < (bMin - boxSize / 2) || b >= (bMax + boxSize / 2)) continue;

			//which bucket?
			octreeScores[r >= rMid][g >= gMid][b >= bMid]++;
		}

		//find winner
		int bestScore = 0x7FFFFFFF;
		int bestIndex = 0;
		for (int g = 1; g >= 0; g--) {
			for (int b = 1; b >= 0; b--) {
				for (int r = 1; r >= 0; r--) {
					int score = octreeScores[r][g][b];
					if (score < bestScore) {
						bestScore = score;
						bestIndex = r | (g << 1) | (b << 2);
					}
				}
			}
		}

		//shrink box
		if ((bestIndex >> 0) & 1) rMin = rMid;
		else rMax = rMid;
		if ((bestIndex >> 1) & 1) gMin = gMid;
		else gMax = gMid;
		if ((bestIndex >> 2) & 1) bMin = bMid;
		else bMax = bMid;
		boxSize /= 2;
	}

	//retrieve midpoint as final color
	COLOR32 pt = rMid | (gMid << 8) | (bMid << 16);
	return pt;
}

void nscrCreate(COLOR32 *imgBits, int width, int height, int nBits, int dither, float diffuse,
	int paletteBase, int nPalettes, int fmt, int tileBase, int mergeTiles, int alignment,
	int paletteSize, int paletteOffset, int rowLimit, int nMaxChars,
	int color0Mode, int balance, int colorBalance, int enhanceColors,
	int *progress1, int *progress1Max, int *progress2, int *progress2Max,
	NCLR *nclr, NCGR *ncgr, NSCR *nscr) {

	//cursory sanity checks
	if (nPalettes > 16) nPalettes = 16;
	else if (nPalettes < 1) nPalettes = 1;
	if (nBits == 4) {
		if (paletteBase >= 16) paletteBase = 15;
		else if (paletteBase < 0) paletteBase = 0;
		if (paletteBase + nPalettes > 16) nPalettes = 16 - paletteBase;

		if (paletteOffset < 0) paletteOffset = 0;
		else if (paletteOffset >= 16) paletteOffset = 15;
		if (paletteOffset + paletteSize > 16) paletteSize = 16 - paletteOffset;
	} else {
		if (paletteOffset < 0) paletteOffset = 0;
		if (paletteSize < 1) paletteSize = 1;
		if (paletteOffset >= 256) paletteOffset = 255;
		if (paletteSize > 256) paletteSize = 256;
		if (paletteOffset + paletteSize > 256) paletteSize = 256 - paletteOffset;
	}
	if (paletteSize < 1) paletteSize = 1;
	if (balance <= 0) balance = BALANCE_DEFAULT;
	if (colorBalance <= 0) colorBalance = BALANCE_DEFAULT;

	int tilesX = width / 8;
	int tilesY = height / 8;
	int nTiles = tilesX * tilesY;
	BGTILE *tiles = (BGTILE *) calloc(nTiles, sizeof(BGTILE));

	//initialize progress
	*progress1Max = nTiles * 2; //2 passes
	*progress2Max = 1000;

	COLOR32 *palette = (COLOR32 *) calloc(256 * 16, 4);
	COLOR32 color0 = chooseBGColor0(imgBits, width, height, color0Mode);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	if (nPalettes == 1) {
		if (paletteOffset) {
			RxCreatePaletteEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize, balance, colorBalance, enhanceColors, 0);
		} else {
			RxCreatePaletteEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset + 1, paletteSize - 1, balance, colorBalance, enhanceColors, 0);
			palette[(paletteBase << nBits) + paletteOffset] = color0; //transparent fill color
		}
	} else {
		RxCreateMultiplePalettesEx(imgBits, tilesX, tilesY, palette, paletteBase, nPalettes, 1 << nBits, paletteSize, paletteOffset, balance, colorBalance, enhanceColors, progress1);
		if (paletteOffset == 0) {
			for (int i = paletteBase; i < paletteBase + nPalettes; i++) palette[i << nBits] = color0;
		}
	}
	*progress1 = nTiles * 2; //make sure it's done

	//by default the palette generator only enforces palette density, but not
	//the actual truncating of RGB values. Do that here. This will also be
	//important when fixed palettes are allowed.
	for (int i = 0; i < 256 * 16; i++) {
		palette[i] = ColorConvertFromDS(ColorConvertToDS(palette[i]));
	}

	//split image into 8x8 tiles.
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			COLOR32 *block = tiles[x + y * tilesX].px;

			memcpy(block, imgBits + srcOffset, 32);
			memcpy(block + 8, imgBits + srcOffset + width, 32);
			memcpy(block + 16, imgBits + srcOffset + width * 2, 32);
			memcpy(block + 24, imgBits + srcOffset + width * 3, 32);
			memcpy(block + 32, imgBits + srcOffset + width * 4, 32);
			memcpy(block + 40, imgBits + srcOffset + width * 5, 32);
			memcpy(block + 48, imgBits + srcOffset + width * 6, 32);
			memcpy(block + 56, imgBits + srcOffset + width * 7, 32);
			for (int i = 0; i < 8 * 8; i++) {
				int a = (block[i] >> 24) & 0xFF;
				if (a < 128) block[i] = 0; //make transparent pixels transparent black
				else block[i] |= 0xFF000000; //opaque
			}
		}
	}

	//match palettes to tiles
	setupBgTilesEx(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset,
		dither, diffuse, balance, colorBalance, enhanceColors);

	//match tiles to each other
	int nChars = nTiles;
	if (mergeTiles) {
		nChars = performCharacterCompression(tiles, nTiles, nBits, nMaxChars, palette, paletteSize, nPalettes, paletteBase,
			paletteOffset, balance, colorBalance, progress2);
	}

	DWORD *blocks = (DWORD *) calloc(64 * nChars, sizeof(DWORD));
	int writeIndex = 0;
	for (int i = 0; i < nTiles; i++) {
		if (tiles[i].masterTile != i) continue;
		BGTILE *t = tiles + i;
		DWORD *dest = blocks + 64 * writeIndex;

		for (int j = 0; j < 64; j++) {
			if (nBits == 4) dest[j] = t->indices[j] & 0xF;
			else dest[j] = t->indices[j];
		}

		writeIndex++;
		if (writeIndex >= nTiles) {
			break;
		}
	}
	*progress2 = 1000;

	//scrunch down masterTile indices
	int nFoundMasters = 0;
	for (int i = 0; i < nTiles; i++) {
		int master = tiles[i].masterTile;
		if (master != i) continue;

		//a master tile. Overwrite all tiles that use this master with nFoundMasters with bit 31 set (just in case)
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile == master) tiles[j].masterTile = nFoundMasters | 0x40000000;
		}
		nFoundMasters++;
	}
	for (int i = 0; i < nTiles; i++) {
		tiles[i].masterTile &= 0xFFFF;
	}

	//prep data output
	uint16_t *indices = (uint16_t *) calloc(nTiles, 2);
	for (int i = 0; i < nTiles; i++) {
		indices[i] = tiles[i].masterTile + tileBase;
	}
	unsigned char *modes = (unsigned char *) calloc(nTiles, 1);
	for (int i = 0; i < nTiles; i++) {
		modes[i] = tiles[i].flipMode;
	}
	unsigned char *paletteIndices = (unsigned char *) calloc(nTiles, 1);
	for (int i = 0; i < nTiles; i++) {
		paletteIndices[i] = tiles[i].palette;
	}

	//create output
	int paletteFormat = NCLR_TYPE_NCLR, characterFormat = NCGR_TYPE_NCGR, screenFormat = NSCR_TYPE_NSCR;
	int compressPalette = 0, compressCharacter = 0, compressScreen = 0;
	switch (fmt) {
		case BGGEN_FORMAT_NITROSYSTEM:
			paletteFormat = NCLR_TYPE_NCLR;
			characterFormat = NCGR_TYPE_NCGR;
			screenFormat = NSCR_TYPE_NSCR;
			break;
		case BGGEN_FORMAT_HUDSON:
			paletteFormat = NCLR_TYPE_HUDSON;
			characterFormat = NCGR_TYPE_HUDSON;
			screenFormat = NSCR_TYPE_HUDSON;
			break;
		case BGGEN_FORMAT_HUDSON2:
			paletteFormat = NCLR_TYPE_HUDSON;
			characterFormat = NCGR_TYPE_HUDSON2;
			screenFormat = NSCR_TYPE_HUDSON2;
			break;
		case BGGEN_FORMAT_NITROCHARACTER:
			paletteFormat = NCLR_TYPE_NC;
			characterFormat = NCGR_TYPE_NC;
			screenFormat = NSCR_TYPE_NC;
			break;
		case BGGEN_FORMAT_AGBCHARACTER:
			paletteFormat = NCLR_TYPE_BIN;
			characterFormat = NCGR_TYPE_AC;
			screenFormat = NSCR_TYPE_AC;
			break;
		case BGGEN_FORMAT_BIN:
		case BGGEN_FORMAT_BIN_COMPRESSED:
			paletteFormat = NCLR_TYPE_BIN;
			characterFormat = NCGR_TYPE_BIN;
			screenFormat = NSCR_TYPE_BIN;
			if (fmt == BGGEN_FORMAT_BIN_COMPRESSED) {
				compressCharacter = COMPRESSION_LZ77;
				compressScreen = COMPRESSION_LZ77;
			}
			break;
	}

	PalInit(nclr, paletteFormat);
	ChrInit(ncgr, characterFormat);
	ScrInit(nscr, screenFormat);
	nclr->header.compression = compressPalette;
	ncgr->header.compression = compressCharacter;
	nscr->header.compression = compressScreen;

	int colorOutputBase = rowLimit ? (nBits == 4 ? (paletteBase * 16) : 0) : 0;
	int nColorsOutput = rowLimit ? (nBits == 4 ? (16 * nPalettes) : (paletteOffset + paletteSize)) : (nBits == 4 ? 256 : (256 * nPalettes));
	int nPalettesOutput = rowLimit ? (nPalettes) : (nBits == 4 ? 16 : nPalettes);
	nclr->nBits = nBits;
	nclr->nColors = nColorsOutput;
	nclr->totalSize = nclr->nColors * 2;
	nclr->nPalettes = nPalettesOutput;
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	nclr->idxTable = (short *) calloc(nclr->nPalettes, 2);
	nclr->extPalette = (nBits == 8 && (nPalettes > 1 || paletteBase > 0));
	for (int i = 0; i < nclr->nColors; i++) {
		nclr->colors[i] = ColorConvertToDS(palette[i + colorOutputBase]);
	}
	for (int i = 0; i < nclr->nPalettes; i++) {
		nclr->idxTable[i] = rowLimit ? (i + paletteBase) : i;
	}

	int nCharsFile = ((nChars + alignment - 1) / alignment) * alignment;
	ncgr->nBits = nBits;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->nTiles = nCharsFile;
	ncgr->tileWidth = 8;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ncgr->tiles = (BYTE **) calloc(nCharsFile, sizeof(BYTE *));
	int charSize = nBits == 4 ? 32 : 64;
	for (int j = 0; j < nCharsFile; j++) {
		BYTE *b = (BYTE *) calloc(64, 1);
		if (j < nChars) {
			for (int i = 0; i < 64; i++) {
				b[i] = (BYTE) blocks[i + j * 64];
			}
		}
		ncgr->tiles[j] = b;
	}
	ncgr->attr = (unsigned char *) calloc(ncgr->nTiles, 1);
	ncgr->attrWidth = ncgr->tilesX;
	ncgr->attrHeight = ncgr->tilesY;
	for (int i = 0; i < ncgr->nTiles; i++) {
		int attr = paletteBase;
		for (int j = 0; j < nTiles; j++) {
			if (indices[j] == i) {
				attr = paletteIndices[j];
				break;
			}
		}
		ncgr->attr[i] = attr;
	}

	nscr->nWidth = width;
	nscr->nHeight = height;
	nscr->fmt = nBits == 4 ? SCREENFORMAT_TEXT : (nPalettes == 1 ? SCREENFORMAT_TEXT : SCREENFORMAT_AFFINEEXT);
	nscr->dataSize = nTiles * 2;
	nscr->data = (uint16_t *) malloc(nscr->dataSize);
	int nHighestIndex = 0;
	for (int i = 0; i < nTiles; i++) {
		nscr->data[i] = indices[i] | (modes[i] << 10) | (paletteIndices[i] << 12);
		if (indices[i] > nHighestIndex) nHighestIndex = indices[i];
	}
	nscr->nHighestIndex = nHighestIndex;

	free(modes);
	free(blocks);
	free(indices);
	free(palette);
	free(paletteIndices);
}
