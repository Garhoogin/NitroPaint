#include <math.h>

#include "bggen.h"
#include "color.h"
#include "palette.h"

//cosine table: [frequency][t]
static const float sCosTable[8][8] = {
	{  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f },
	{  0.980785f,  0.831470f,  0.555570f,  0.195090f, -0.195090f, -0.555570f, -0.831470f, -0.980785f },
	{  0.923880f,  0.382683f, -0.382683f, -0.923880f, -0.923880f, -0.382683f,  0.382683f,  0.923880f },
	{  0.831470f, -0.195090f, -0.980785f, -0.555570f,  0.555570f,  0.980785f,  0.195090f, -0.831470f },
	{  0.707107f, -0.707107f, -0.707107f,  0.707107f,  0.707107f, -0.707107f, -0.707107f,  0.707107f },
	{  0.555570f, -0.980785f,  0.195090f,  0.831470f, -0.831470f, -0.195090f,  0.980785f, -0.555570f },
	{  0.382683f, -0.923880f,  0.923880f, -0.382683f, -0.382683f,  0.923880f, -0.923880f,  0.382683f },
	{  0.195090f, -0.555570f,  0.831470f, -0.980785f,  0.980785f, -0.831470f,  0.555570f, -0.195090f }
};

//coefficient weightings
static const float sWeightLuma[64] = {
	0.7500f, 1.0000f, 0.8571f, 0.8571f, 0.6667f, 0.5000f, 0.2449f, 0.1667f,
	1.0000f, 1.0000f, 0.9231f, 0.7059f, 0.5455f, 0.3429f, 0.1875f, 0.1304f,
	0.8571f, 0.9231f, 0.7500f, 0.5455f, 0.3243f, 0.2182f, 0.1538f, 0.1263f,
	0.8571f, 0.7059f, 0.5455f, 0.4138f, 0.2143f, 0.1875f, 0.1379f, 0.1224f,
	0.6667f, 0.5455f, 0.3243f, 0.2143f, 0.1765f, 0.1481f, 0.1165f, 0.1071f,
	0.5000f, 0.3429f, 0.2182f, 0.1875f, 0.1481f, 0.1154f, 0.0992f, 0.1200f,
	0.2449f, 0.1875f, 0.1538f, 0.1379f, 0.1165f, 0.0992f, 0.1000f, 0.1165f,
	0.1667f, 0.1304f, 0.1263f, 0.1224f, 0.1071f, 0.1200f, 0.1165f, 0.1212f
};

static const float sWeightChroma[64] = {
	0.7059f, 0.6667f, 0.5000f, 0.2553f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.6667f, 0.5714f, 0.4615f, 0.1818f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.5000f, 0.4615f, 0.2143f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.2553f, 0.1818f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f
};

static void BgiComputeDctBlock(float *in, float *out) {
	for (int ky = 0; ky < 8; ky++) {
		for (int kx = 0; kx < 8; kx++) {

			double sum = 0.0;
			for (int y = 0; y < 8; y++) {
				double cosY = sCosTable[ky][y];
				for (int x = 0; x < 8; x++) {
					double cosX = sCosTable[kx][x];

					sum += in[y * 8 + x] * cosX * cosY;
				}
			}

			//if first row/column, halve.
			if (kx == 0) sum *= 0.5;
			if (ky == 0) sum *= 0.5;
			out[ky * 8 + kx] = (float) (sum * 0.0625);
		}
	}
}

static void BgiComputeDct(RxReduction *reduction, BgTile *tile) {
	//compute DCT block for Y, I, Q
	float blockY[64], blockI[64], blockQ[64], blockA[64];

	for (int i = 0; i < 64; i++) {
		double y = reduction->lumaTable[tile->pxYiq[i].y];
		if (tile->pxYiq[i].a < 128) {
			//A < 128: turn black transparent
			y = 0.0;
			blockA[i] = 0;
		} else {
			//else turn full opaque
			blockA[i] = 255;
		}

		if (y > 0.0) {
			blockY[i] = (float) y;
			blockI[i] = (float) tile->pxYiq[i].i;
			blockQ[i] = (float) tile->pxYiq[i].q;
		} else {
			blockY[i] = 0.0f;
			blockI[i] = 0.0f;
			blockQ[i] = 0.0f;
		}
	}

	//compute output blocks
	BgiComputeDctBlock(blockY, tile->dct.blockY);
	BgiComputeDctBlock(blockI, tile->dct.blockI);
	BgiComputeDctBlock(blockQ, tile->dct.blockQ);
	BgiComputeDctBlock(blockA, tile->dct.blockA);
}

static double BgiCompareTilesDct(RxReduction *reduction, BgTile *tile1, BgTile *tile2, unsigned char mode) {
	//sum of square
	double error = 0.0;

	for (int i = 0; i < 64; i++) {
		double block2Y = tile2->dct.blockY[i];
		double block2I = tile2->dct.blockI[i];
		double block2Q = tile2->dct.blockQ[i];
		double block2A = tile2->dct.blockA[i];

		//flip X: negate every odd column
		if ((mode & TILE_FLIPX) && (i % 2 == 1)) {
			block2Y = -block2Y, block2I = -block2I, block2Q = -block2Q, block2A = -block2A;
		}

		//flip Y: negate every odd row
		if ((mode & TILE_FLIPY) && ((i / 8) % 2 == 1)) {
			block2Y = -block2Y, block2I = -block2I, block2Q = -block2Q, block2A = -block2A;
		}

		double dy = sqrt(sWeightLuma[i])   * reduction->yWeight * (tile1->dct.blockY[i] - block2Y);
		double di = sqrt(sWeightChroma[i]) * reduction->iWeight * (tile1->dct.blockI[i] - block2I);
		double dq = sqrt(sWeightChroma[i]) * reduction->qWeight * (tile1->dct.blockQ[i] - block2Q);
		double da = 40.0                                        * (tile1->dct.blockA[i] - block2A);

		//weighted error
		error += dy * dy + di * di + dq * dq + da * da;
	}

	return error;
}


static float BgiTileDifferenceFlip(RxReduction *reduction, BgTile *t1, BgTile *t2, unsigned char mode) {
	if (1) {
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
	} else {
		return (float) BgiCompareTilesDct(reduction, t1, t2, mode);
	}
}

static float BgiTileDifference(RxReduction *reduction, BgTile *t1, BgTile *t2, unsigned char *flipMode) {
	float err = BgiTileDifferenceFlip(reduction, t1, t2, 0);
	if (err == 0) {
		*flipMode = 0;
		return err;
	}
	float err2 = BgiTileDifferenceFlip(reduction, t1, t2, TILE_FLIPX);
	if (err2 == 0) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	float err3 = BgiTileDifferenceFlip(reduction, t1, t2, TILE_FLIPY);
	if (err3 == 0) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	float err4 = BgiTileDifferenceFlip(reduction, t1, t2, TILE_FLIPXY);
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

static void BgiAddTileToTotal(RxReduction *reduction, int *pxBlock, BgTile *tile) {
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

typedef struct BgTileDiff_ {
	int tile1;
	int tile2;
	double diff;		//post-biased
} BgTileDiff;

typedef struct BgTileDiffList_ {
	BgTileDiff *diffBuff;
	int diffBuffSize;
	int diffBuffLength;
	double minDiff;
	double maxDiff;
} BgTileDiffList;

static void BgiTdlInit(BgTileDiffList *list, int nEntries) {
	list->diffBuffSize = nEntries;
	list->diffBuffLength = 0;
	list->minDiff = 1e32;
	list->maxDiff = 0;
	list->diffBuff = (BgTileDiff *) calloc(list->diffBuffSize, sizeof(BgTileDiff));
}

static void BgiTdlFree(BgTileDiffList *list) {
	free(list->diffBuff);
	list->diffBuff = NULL;
	list->diffBuffLength = 0;
	list->diffBuffSize = 0;
}

static void BgiTdlAdd(BgTileDiffList *list, int tile1, int tile2, double diff) {
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
	memmove(list->diffBuff + destIndex + 1, list->diffBuff + destIndex, nEntriesToMove * sizeof(BgTileDiff));
	list->diffBuff[destIndex].tile1 = tile1;
	list->diffBuff[destIndex].tile2 = tile2;
	list->diffBuff[destIndex].diff = diff;
	if (added) {
		list->diffBuffLength++;
	}
	list->minDiff = list->diffBuff[0].diff;
	list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
}

static void BgiTdlRemoveAll(BgTileDiffList *list, int tile1, int tile2) {
	//remove all diffs involving tile1 and tile2
	for (int i = 0; i < list->diffBuffLength; i++) {
		BgTileDiff *td = list->diffBuff + i;
		if (td->tile1 == tile1 || td->tile2 == tile1 || td->tile1 == tile2 || td->tile2 == tile2) {
			memmove(td, td + 1, (list->diffBuffLength - i - 1) * sizeof(BgTileDiff));
			list->diffBuffLength--;
			i--;
		}
	}
	if (list->diffBuffLength > 0) {
		list->minDiff = list->diffBuff[0].diff;
		list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
	}
}

static void BgiTdlPop(BgTileDiffList *list, BgTileDiff *out) {
	if (list->diffBuffLength > 0) {
		memcpy(out, list->diffBuff, sizeof(BgTileDiff));
		memmove(list->diffBuff, list->diffBuff + 1, (list->diffBuffLength - 1) * sizeof(BgTileDiff));
		list->diffBuffLength--;
		if (list->diffBuffLength > 0) {
			list->minDiff = list->diffBuff[0].diff;
		}
	}
}

static void BgiTdlReset(BgTileDiffList *list) {
	list->diffBuffLength = 0;
	list->maxDiff = 0;
	list->minDiff = 1e32;
}

int BgPerformCharacterCompression(BgTile *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes,
	int paletteBase, int paletteOffset, int balance, int colorBalance, int *progress) {
	int nChars = nTiles;
	float *diffBuff = (float *) calloc(nTiles * nTiles, sizeof(float));
	unsigned char *flips = (unsigned char *) calloc(nTiles * nTiles, 1); //how must each tile be manipulated to best match its partner

	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, 0, 255);
	for (int i = 0; i < nTiles; i++) {
		BgTile *t1 = tiles + i;
		for (int j = 0; j < i; j++) {
			BgTile *t2 = tiles + j;

			diffBuff[i + j * nTiles] = BgiTileDifference(reduction, t1, t2, &flips[i + j * nTiles]);
			diffBuff[j + i * nTiles] = diffBuff[i + j * nTiles];
			flips[j + i * nTiles] = flips[i + j * nTiles];
		}
		*progress = (i * i) / nTiles * 500 / nTiles;
	}

	//first, combine tiles with a difference of 0.

	for (int i = 0; i < nTiles; i++) {
		BgTile *t1 = tiles + i;
		if (t1->masterTile != i) continue;

		for (int j = 0; j < i; j++) {
			BgTile *t2 = tiles + j;
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
		BgTileDiffList tdl;
		BgiTdlInit(&tdl, 64);

		//keep finding the most similar tile until we get character count down
		int direction = 0;
		while (nChars > nMaxChars) {
			for (int iOuter = 0; iOuter < nTiles; iOuter++) {
				int i = direction ? (nTiles - 1 - iOuter) : iOuter; //criss cross the direction
				BgTile *t1 = tiles + i;
				if (t1->masterTile != i) continue;

				for (int j = 0; j < i; j++) {
					BgTile *t2 = tiles + j;
					if (t2->masterTile != j) continue;

					double thisErrorEntry = diffBuff[i + j * nTiles];
					double thisError = thisErrorEntry;
					double bias = t1->nRepresents + t2->nRepresents;
					bias *= bias;

					thisError = thisErrorEntry * bias;
					BgiTdlAdd(&tdl, j, i, thisError);
				}
			}

			//now merge tiles while we can
			int tile1, tile2;
			while (tdl.diffBuffLength > 0 && nChars > nMaxChars) {
				BgTileDiff td;
				BgiTdlPop(&tdl, &td);

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

				BgiTdlRemoveAll(&tdl, td.tile1, td.tile2);
			}
			direction = !direction;
			BgiTdlReset(&tdl);
		}
		BgiTdlFree(&tdl);
	}

	free(flips);
	free(diffBuff);

	//try to make the compressed result look less bad
	for (int i = 0; i < nTiles; i++) {
		if (tiles[i].masterTile != i) continue;
		if (tiles[i].nRepresents <= 1) continue; //no averaging required for just one tile
		BgTile *tile = &tiles[i];

		//average all tiles that use this master tile.
		int pxBlock[64 * 4] = { 0 };
		int nRep = tile->nRepresents;
		for (int j = 0; j < nTiles; j++) {
			BgTile *tile2 = &tiles[j];
			if (tile2->masterTile == i) BgiAddTileToTotal(reduction, pxBlock, tile2);
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
			tile->px[j] = RxConvertYiqToRgb(&yiq);
		}

		//try to determine the most optimal palette. Child tiles can be different palettes.
		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits) + paletteOffset + !paletteOffset;
			double err = RxComputePaletteError(reduction, tile->px, 8, 8, pal, paletteSize - !paletteOffset, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//now, match colors to indices.
		COLOR32 *pal = palette + (bestPalette << nBits);
		int idxs[64];
		RxReduceImageWithContext(reduction, tile->px, idxs, 8, 8, pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset,
			RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_PRESERVE_ALPHA, 0.0f);
		for (int j = 0; j < 64; j++) {
			tile->indices[j] = idxs[j] == 0 ? 0 : (idxs[j] + paletteOffset - !!paletteOffset);
			tile->px[j] = tile->indices[j] ? (pal[tile->indices[j]] | 0xFF000000) : 0;
		}
		tile->palette = bestPalette;

		//lastly, copy tile->indices to all child tile->indices, just to make sure palette and character are in synch.
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			if (j == i) continue;
			BgTile *tile2 = &tiles[j];

			memcpy(tile2->indices, tile->indices, 64);
			tile2->palette = tile->palette;
		}
	}

	RxDestroy(reduction);
	free(reduction);
	return nChars;
}

void BgSetupTiles(BgTile *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors) {
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, enhanceColors, paletteSize);

	if (!dither) diffuse = 0.0f;
	for (int i = 0; i < nTiles; i++) {
		BgTile *tile = &tiles[i];

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
		int idxs[64];
		RxReduceImageWithContext(reduction, tile->px, idxs, 8, 8, pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset,
			RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_PRESERVE_ALPHA, diffuse);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			tile->indices[j] = idxs[j] == 0 ? 0 : (idxs[j] + paletteOffset - !!paletteOffset);
			tile->px[j] = tile->indices[j] ? (pal[tile->indices[j]] | 0xFF000000) : 0;

			//YIQ color
			RxConvertRgbToYiq(col, &tile->pxYiq[j]);
		}

		//compute DCT
		BgiComputeDct(reduction, tile);

		tile->masterTile = i;
		tile->nRepresents = 1;
		tile->palette = bestPalette;
	}
	RxDestroy(reduction);
	free(reduction);
}

static COLOR32 BgiSelectColor0(COLOR32 *px, int width, int height, int mode) {
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

void BgGenerate(NCLR *nclr, NCGR *ncgr, NSCR *nscr, COLOR32 *imgBits, int width, int height, 
	BgGenerateParameters *params,
	int *progress1, int *progress1Max, int *progress2, int *progress2Max) {

	//palette setting
	int nPalettes = params->paletteRegion.count;
	int paletteBase = params->paletteRegion.base;
	int paletteOffset = params->paletteRegion.offset;
	int paletteSize = params->paletteRegion.length;

	//balance settting
	int balance = params->balance.balance;
	int colorBalance = params->balance.colorBalance;
	int enhanceColors = params->balance.enhanceColors;

	//character setting
	int tileBase = params->characterSetting.base;
	int characterCompression = params->characterSetting.compress;
	int nMaxChars = params->characterSetting.nMax;
	int alignment = params->characterSetting.alignment;

	//cursory sanity checks
	if (nPalettes > 16) nPalettes = 16;
	else if (nPalettes < 1) nPalettes = 1;
	if (params->nBits == 4) {
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
	BgTile *tiles = (BgTile *) calloc(nTiles, sizeof(BgTile));

	//initialize progress
	*progress1Max = nTiles * 2; //2 passes
	*progress2Max = 1000;

	int nBits = params->nBits;
	COLOR32 *palette = (COLOR32 *) calloc(256 * 16, 4);
	COLOR32 color0 = BgiSelectColor0(imgBits, width, height, params->color0Mode);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	if (nPalettes == 1) {
		if (paletteOffset) {
			RxCreatePaletteEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize, balance, colorBalance, enhanceColors, RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE);
		} else {
			RxCreatePaletteEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset + 1, paletteSize - 1, balance, colorBalance, enhanceColors, RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE);
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
	//for (int i = 0; i < 256 * 16; i++) {
	//	palette[i] = ColorRoundToDS15(palette[i]) | 0xFF000000;
	//}

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
	BgSetupTiles(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset,
		params->dither.dither, params->dither.diffuse, balance, colorBalance, enhanceColors);

	//match tiles to each other
	int nChars = nTiles;
	if (characterCompression) {
		nChars = BgPerformCharacterCompression(tiles, nTiles, nBits, nMaxChars, palette, paletteSize, nPalettes, paletteBase,
			paletteOffset, balance, colorBalance, progress2);
	}

	COLOR32 *blocks = (COLOR32 *) calloc(64 * nChars, sizeof(COLOR32));
	int writeIndex = 0;
	for (int i = 0; i < nTiles; i++) {
		if (tiles[i].masterTile != i) continue;
		BgTile *t = tiles + i;
		COLOR32 *dest = blocks + 64 * writeIndex;

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
	switch (params->fmt) {
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
		case BGGEN_FORMAT_IRISCHARACTER:
			paletteFormat = NCLR_TYPE_BIN;
			characterFormat = NCGR_TYPE_IC;
			screenFormat = NSCR_TYPE_IC;
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
			if (params->fmt == BGGEN_FORMAT_BIN_COMPRESSED) {
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

	int nColorsOutput = (nBits == 4) ? 256 : (256 * (paletteBase + nPalettes));
	nclr->nBits = nBits;
	nclr->nColors = nColorsOutput;
	nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
	nclr->compressedPalette = params->compressPalette;
	nclr->extPalette = nBits == 8 && (nPalettes > 1 || paletteBase > 0);
	for (int i = 0; i < nclr->nColors; i++) {
		nclr->colors[i] = ColorConvertToDS(palette[i]);
	}

	//TODO: revise compresed palettes
	// 2. for raw files, revise the file I/O code to omit unused palettes

	int nCharsFile = ((nChars + alignment - 1) / alignment) * alignment;
	ncgr->nBits = nBits;
	ncgr->extPalette = nclr->extPalette;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->nTiles = nCharsFile;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ncgr->tiles = (unsigned char **) calloc(nCharsFile, sizeof(unsigned char *));
	int charSize = nBits == 4 ? 32 : 64;
	for (int j = 0; j < nCharsFile; j++) {
		unsigned char *b = (unsigned char *) calloc(64, 1);
		if (j < nChars) {
			for (int i = 0; i < 64; i++) {
				b[i] = (unsigned char) blocks[i + j * 64];
			}
		}
		ncgr->tiles[j] = b;
	}

	ncgr->attr = (unsigned char *) calloc(ncgr->nTiles, 1);
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

	int bgScreenFormat = SCREENFORMAT_TEXT, screenColorMode = SCREENCOLORMODE_16x16;
	if (nBits == 4) {
		//4-bit: use text BG
		bgScreenFormat = SCREENFORMAT_TEXT;
		screenColorMode = SCREENCOLORMODE_16x16;
	} else {
		//8-bit: use affine if we have a nonzero palette base, more than 1 palette, or we say so
		if (nPalettes > 1 || paletteBase > 0 || params->affine) {
			bgScreenFormat = SCREENFORMAT_AFFINEEXT;
			screenColorMode = SCREENCOLORMODE_256x16;
		} else {
			bgScreenFormat = SCREENFORMAT_TEXT;
			screenColorMode = SCREENCOLORMODE_256x1;
		}
	}

	nscr->tilesX = tilesX;
	nscr->tilesY = tilesY;
	nscr->fmt = bgScreenFormat;
	nscr->colorMode = screenColorMode;
	nscr->dataSize = nTiles * 2;
	nscr->data = (uint16_t *) malloc(nscr->dataSize);
	for (int i = 0; i < nTiles; i++) {
		nscr->data[i] = indices[i] | (modes[i] << 10) | (paletteIndices[i] << 12);
	}
	ScrComputeHighestCharacter(nscr);

	free(modes);
	free(blocks);
	free(indices);
	free(palette);
	free(paletteIndices);
}

double BgiPaletteCharError(RxReduction *reduction, COLOR32 *block, RxYiqColor *pals, unsigned char *character, int flip, double maxError) {
	double error = 0;
	for (int i = 0; i < 64; i++) { //0b111 111
		int srcIndex = i;
		if (flip & TILE_FLIPX) srcIndex ^= 7;
		if (flip & TILE_FLIPY) srcIndex ^= 7 << 3;

		//convert source image pixel
		RxYiqColor yiq;
		COLOR32 col = block[srcIndex];
		RxConvertRgbToYiq(col, &yiq);

		//char pixel
		int index = character[i];
		RxYiqColor *matchedYiq = pals + index;
		int matchedA = index > 0 ? 255 : 0;
		if (matchedA == 0 && yiq.a < 128) {
			continue; //to prevent superfluous non-alpha difference
		}

		//diff
		double dy = reduction->yWeight * (reduction->lumaTable[yiq.y] - reduction->lumaTable[matchedYiq->y]);
		double di = reduction->iWeight * (yiq.i - matchedYiq->i);
		double dq = reduction->qWeight * (yiq.q - matchedYiq->q);
		double da = 40 * (yiq.a - matchedA);


		error += dy * dy;
		if (da != 0.0) error += da * da;
		if (error >= maxError) return maxError;
		error += di * di + dq * dq;
		if (error >= maxError) return maxError;
	}
	return error;
}

double BgiBestPaletteCharError(RxReduction *reduction, COLOR32 *block, RxYiqColor *pals, unsigned char *character, int *flip, double maxError) {
	double e00 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPNONE, maxError);
	if (e00 == 0) {
		*flip = TILE_FLIPNONE;
		return e00;
	}
	double e01 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPX, maxError);
	if (e01 == 0) {
		*flip = TILE_FLIPX;
		return e01;
	}
	double e10 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPY, maxError);
	if (e10 == 0) {
		*flip = TILE_FLIPY;
		return e10;
	}
	double e11 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPXY, maxError);
	if (e11 == 0) {
		*flip = TILE_FLIPXY;
		return e11;
	}

	if (e00 <= e01 && e00 <= e10 && e00 <= e11) {
		*flip = TILE_FLIPNONE;
		return e00;
	}
	if (e01 <= e00 && e01 <= e10 && e01 <= e11) {
		*flip = TILE_FLIPX;
		return e01;
	}
	if (e10 <= e00 && e10 <= e01 && e10 <= e11) {
		*flip = TILE_FLIPY;
		return e10;
	}
	*flip = TILE_FLIPXY;
	return e11;
}

void BgReplaceSection(NCLR *nclr, NCGR *ncgr, NSCR *nscr, COLOR32 *px, int width, int height,
	int writeScreen, int writeCharacterIndices,
	int tileBase, int nPalettes, int paletteNumber, int paletteOffset,
	int paletteSize, BOOL newPalettes, int writeCharBase, int nMaxChars,
	BOOL newCharacters, BOOL dither, float diffuse, int maxTilesX, int maxTilesY,
	int nscrTileX, int nscrTileY, int balance, int colorBalance, int enhanceColors,
	int *progress, int *progressMax, int *progress2, int *progress2Max) {

	int tilesX = width / 8;
	int tilesY = height / 8;
	int paletteStartFrom0 = 0;
	int maxPaletteSize = ncgr->nBits == 4 ? 16 : 256;

	//sanity checks
	if (tilesX > maxTilesX) tilesX = maxTilesX;
	if (tilesY > maxTilesY) tilesY = maxTilesY;
	if (paletteOffset >= maxPaletteSize) paletteOffset = maxPaletteSize - 1;
	if (paletteSize > maxPaletteSize) paletteSize = maxPaletteSize;
	if (paletteOffset + paletteSize > maxPaletteSize) paletteSize = maxPaletteSize - paletteOffset;
	if (writeCharBase >= ncgr->nTiles) writeCharBase = ncgr->nTiles - 1;
	if (writeCharBase + nMaxChars > ncgr->nTiles) nMaxChars = ncgr->nTiles - writeCharBase;

	//if no write screen, still set some proper bounds.
	if (!writeScreen) {
		paletteNumber = 0;
		if (ncgr->nBits == 4) {
			nPalettes = nclr->nColors / 16;
		} else {
			nPalettes = 1;
		}
	}

	*progress = 0;
	*progress2 = 0;
	*progressMax = tilesX * tilesY * 2;
	*progress2Max = 1000;

	BgTile *blocks = (BgTile *) calloc(tilesX * tilesY, sizeof(BgTile));
	COLOR32 *pals = (COLOR32 *) calloc(16 * maxPaletteSize, 4);

	//split image into 8x8 chunks
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			COLOR32 *block = blocks[x + y * tilesX].px;
			memcpy(block +  0, px + srcOffset + width * 0, 8 * sizeof(COLOR32));
			memcpy(block +  8, px + srcOffset + width * 1, 8 * sizeof(COLOR32));
			memcpy(block + 16, px + srcOffset + width * 2, 8 * sizeof(COLOR32));
			memcpy(block + 24, px + srcOffset + width * 3, 8 * sizeof(COLOR32));
			memcpy(block + 32, px + srcOffset + width * 4, 8 * sizeof(COLOR32));
			memcpy(block + 40, px + srcOffset + width * 5, 8 * sizeof(COLOR32));
			memcpy(block + 48, px + srcOffset + width * 6, 8 * sizeof(COLOR32));
			memcpy(block + 56, px + srcOffset + width * 7, 8 * sizeof(COLOR32));

			for (int i = 0; i < 8 * 8; i++) {
				int a = (block[i] >> 24) & 0xFF;
				if (a < 128) block[i] = 0; //make transparent pixels transparent black
				else block[i] |= 0xFF000000; //opaque
			}
		}
	}

	int charBase = tileBase;
	int nscrTilesX = nscr->tilesX;
	int nscrTilesY = nscr->tilesY;
	uint16_t *nscrData = nscr->data;

	//create dummy reduction to setup parameters for color matching
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, enhanceColors, paletteSize - !paletteOffset);

	//generate an nPalettes color palette
	if (newPalettes) {
		if (writeScreen) {
			//if we're writing the screen, we can write the palette as normal.
			RxCreateMultiplePalettesEx(px, tilesX, tilesY, pals, 0, nPalettes, maxPaletteSize, paletteSize,
				paletteOffset, balance, colorBalance, enhanceColors, progress);
		} else {
			//else, we need to be a bit more methodical. Lucky for us, though, the palettes are already partitioned.
			//due to this, we can't respect user-set palette base and count. We're at the whim of the screen's
			//existing data. Iterate all 16 palettes. If tiles in our region use them, construct a histogram and
			//write its palette data.
			//first read in original palette, we'll write over it.
			for (int i = 0; i < nclr->nColors; i++) {
				pals[i] = ColorConvertFromDS(nclr->colors[i]);
			}
			for (int palNo = 0; palNo < nPalettes; palNo++) {
				int nTilesHistogram = 0;

				for (int y = 0; y < tilesY; y++) {
					for (int x = 0; x < tilesX; x++) {
						uint16_t d = nscrData[x + nscrTileX + (y + nscrTileY) * nscrTilesX];
						int thisPalNo = (d & 0xF000) >> 12;
						if (thisPalNo != palNo) continue;

						nTilesHistogram++;
						RxHistAdd(reduction, blocks[x + y * tilesX].px, 8, 8);
					}
				}

				//if we counted tiles, create palette
				if (nTilesHistogram > 0) {
					RxHistFinalize(reduction);
					RxComputePalette(reduction);

					COLOR32 *outPal = pals + palNo * maxPaletteSize + paletteOffset + !paletteOffset;
					for (int i = 0; i < paletteSize - !paletteOffset; i++) {
						outPal[i] = reduction->paletteRgb[i];
					}
					qsort(outPal, paletteSize - !paletteOffset, sizeof(COLOR32), RxColorLightnessComparator);
					if (paletteOffset == 0) pals[palNo * maxPaletteSize] = 0xFF00FF;
					RxHistClear(reduction);
				}
			}
		}
	} else {
		COLOR *destPalette = nclr->colors + paletteNumber * maxPaletteSize;
		int nColors = nPalettes * paletteSize;
		for (int i = 0; i < nColors; i++) {
			COLOR c = destPalette[i];
			pals[i] = ColorConvertFromDS(c);
		}
	}
	*progress = *progressMax;

	//write to NCLR
	if (newPalettes) {
		COLOR *destPalette = nclr->colors + paletteNumber * maxPaletteSize;
		for (int i = 0; i < nPalettes; i++) {
			COLOR *dest = destPalette + i * maxPaletteSize;
			for (int j = paletteOffset; j < paletteOffset + paletteSize; j++) {
				COLOR32 col = (pals + i * maxPaletteSize)[j];
				dest[j] = ColorConvertToDS(col);
			}
		}
	}

	//pre-convert palette to YIQ
	RxYiqColor *palsYiq = (RxYiqColor *) calloc(nPalettes * paletteSize, sizeof(RxYiqColor));
	for (int i = 0; i < nPalettes * paletteSize; i++) {
		RxConvertRgbToYiq(pals[i], &palsYiq[i]);
	}

	if (!writeScreen) {
		//no write screen, only character can be written (palette was already dealt with)
		if (newCharacters) {
			//just write each tile
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					BgTile *tile = &blocks[x + y * tilesX];

					uint16_t d = nscrData[x + nscrTileX + (y + nscrTileY) * nscrTilesX];
					int charIndex = (d & 0x3FF) - charBase;
					int palIndex = (d & 0xF000) >> 12;
					int flip = (d & 0x0C00) >> 10;
					if (charIndex < 0) continue;

					int idxs[64];
					unsigned char *chr = ncgr->tiles[charIndex];
					COLOR32 *thisPalette = pals + palIndex * maxPaletteSize + paletteOffset - !!paletteOffset;
					RxReduceImageWithContext(reduction, tile->px, idxs, 8, 8, thisPalette, paletteSize + !!paletteOffset,
						RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_PRESERVE_ALPHA, dither ? diffuse : 0.0f);

					//mask to map source to destination pixels
					unsigned int xorMask = 0;
					if (flip & TILE_FLIPX) xorMask ^= 007;
					if (flip & TILE_FLIPY) xorMask ^= 070;

					for (int i = 0; i < 64; i++) {
						int cidx = idxs[i];
						if (cidx > 0) cidx += paletteOffset - !!paletteOffset;

						chr[i ^ xorMask] = cidx;
					}
				}
			}
		}
	} else if (writeCharacterIndices) {
		//write screen, write character indices
		//if we can write characters, do a normal character compression.
		if (newCharacters) {
			//do normal character compression.
			BgSetupTiles(blocks, tilesX * tilesY, ncgr->nBits, pals, paletteSize, nPalettes, 0, paletteOffset,
				dither, diffuse, balance, colorBalance, enhanceColors);
			int nOutChars = BgPerformCharacterCompression(blocks, tilesX * tilesY, ncgr->nBits, nMaxChars, pals, paletteSize,
				nPalettes, 0, paletteOffset, balance, colorBalance, progress2);

			//keep track of master tiles and how they map to real character indices
			int *masterMap = (int *) calloc(tilesX * tilesY, sizeof(int));

			//write chars
			int nCharsWritten = 0;
			int indexMask = (1 << ncgr->nBits) - 1;
			for (int i = 0; i < tilesX * tilesY; i++) {
				BgTile *tile = blocks + i;
				if (tile->masterTile != i) continue;

				//master tile
				unsigned char *destTile = ncgr->tiles[nCharsWritten + writeCharBase];
				for (int j = 0; j < 64; j++)
					destTile[j] = tile->indices[j] & indexMask;
				masterMap[i] = nCharsWritten + writeCharBase;
				nCharsWritten++;
			}

			//next, write screen.
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					BgTile *tile = blocks + x + y * tilesX;
					int palette = tile->palette + paletteNumber;
					int charIndex = masterMap[tile->masterTile] + charBase;

					if (x + nscrTileX < nscrTilesX && y + nscrTileY < nscrTilesY) {
						uint16_t d = (tile->flipMode << 10) | (palette << 12) | charIndex;
						nscrData[x + nscrTileX + (y + nscrTileY) * nscr->tilesX] = d;
					}
				}
			}
			free(masterMap);
		} else {
			//else, we have to get by just using the screen itself.
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					BgTile *tile = blocks + x + y * tilesX;
					COLOR32 *block = tile->px;

					//search best tile.
					int chosenCharacter = 0, chosenPalette = 0, chosenFlip = TILE_FLIPNONE;
					double minError = 1e32;
					for (int j = 0; j < ncgr->nTiles; j++) {
						for (int i = 0; i < nPalettes; i++) {
							int charId = j, mode;
							double err = BgiBestPaletteCharError(reduction, block, palsYiq + i * maxPaletteSize, ncgr->tiles[charId], &mode, minError);
							if (err < minError) {
								chosenCharacter = charId;
								chosenPalette = i;
								minError = err;
								chosenFlip = mode;
							}
						}
					}

					int nscrX = x + nscrTileX;
					int nscrY = y + nscrTileY;

					if (nscrX < nscrTilesX && nscrY < nscrTilesY) {
						uint16_t d = 0;
						d = d & 0xFFF;
						d |= (chosenPalette + paletteNumber) << 12;
						d &= 0xFC00;
						d |= (chosenCharacter + charBase);
						d |= chosenFlip << 10;
						nscrData[nscrX + nscrY * nscrTilesX] = d;
					}
				}
			}
		}

	} else {
		//write screen, no write character indices.
		//next, start palette matching. See which palette best fits a tile, set it in the NSCR, then write the bits to the NCGR.
		if (newCharacters) {
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					COLOR32 *block = blocks[x + y * tilesX].px;

					double leastError = 1e32;
					int leastIndex = 0;
					for (int i = 0; i < nPalettes; i++) {
						COLOR32 *thisPal = pals + i * maxPaletteSize + paletteOffset + !paletteOffset;
						double err = RxComputePaletteError(reduction, block, 8, 8, thisPal, paletteSize - !paletteOffset, leastError);
						if (err < leastError) {
							leastError = err;
							leastIndex = i;
						}
					}

					int nscrX = x + nscrTileX;
					int nscrY = y + nscrTileY;

					if (nscrX < nscrTilesX && nscrY < nscrTilesY) {
						uint16_t d = nscrData[nscrX + nscrY * nscrTilesX];
						d = d & 0x3FF;
						d |= (leastIndex + paletteNumber) << 12;
						nscrData[nscrX + nscrY * nscr->tilesX] = d;

						int charOrigin = d & 0x3FF;
						if (charOrigin - charBase < 0) continue;
						unsigned char *ncgrTile = ncgr->tiles[charOrigin - charBase];

						int idxs[64];
						COLOR32 *thisPal = pals + leastIndex * maxPaletteSize + paletteOffset - !!paletteOffset;
						RxReduceImageWithContext(reduction, block, idxs, 8, 8, thisPal, paletteSize + !!paletteOffset,
							RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_PRESERVE_ALPHA, dither ? diffuse : 0.0f);

						for (int i = 0; i < 64; i++) {
							unsigned int index = idxs[i];
							if (index > 0) index += paletteOffset - !!paletteOffset;
							ncgrTile[i] = index;
						}
					}
				}
			}
		} else {
			//no new character
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					COLOR32 *block = blocks[x + y * tilesX].px;
					int nscrX = x + nscrTileX;
					int nscrY = y + nscrTileY;

					//check bounds
					if (nscrX < nscrTilesX && nscrY < nscrTilesY) {

						//find what combination of palette and flip minimizes the error.
						uint16_t oldData = nscrData[nscrX + nscrY * nscrTilesX];
						int charId = (oldData & 0x3FF) - charBase, chosenPalette = 0, chosenFlip = TILE_FLIPNONE;
						double minError = 1e32;
						for (int i = 0; i < nPalettes; i++) {
							int mode;
							double err = BgiBestPaletteCharError(reduction, block, palsYiq + i * maxPaletteSize, ncgr->tiles[charId], &mode, minError);
							if (err < minError) {
								chosenPalette = i;
								minError = err;
								chosenFlip = mode;
							}
						}

						uint16_t d = 0;
						d |= (chosenPalette + paletteNumber) << 12;
						d |= (charId + charBase);
						d |= chosenFlip << 10;
						nscrData[nscrX + nscrY * nscrTilesX] = d;
					}
				}
			}
		}
	}

	free(palsYiq);
	RxDestroy(reduction);
	free(reduction);

	free(blocks);
	free(pals);
}
