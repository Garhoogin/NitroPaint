#include "CellExt2D.h"

#define SEXT8(n)   (((n)<0x080)?(n):((n)-0x100))
#define SEXT9(n)   (((n)<0x100)?(n):((n)-0x200))


static void CellGetEffectiveObjBounds(NCER_CELL_INFO *pObj, int *pX, int *pY, int *pW, int *pH) {
	//OBJ coordinates
	int objX = SEXT9(pObj->x), objY = SEXT8(pObj->y);
	int objW = pObj->width, objH = pObj->height;

	//when double size, the effective region is shifted down and right by half the width/height
	if (pObj->doubleSize) {
		objX += objW / 2;
		objY += objH / 2;
	}

	*pX = objX;
	*pY = objY;
	*pW = objW;
	*pH = objH;
}

static void CellGetCellBounds(NCER_CELL *cell, int *pxMin, int *pyMin, int *pxMax, int *pyMax) {
	int xMin = 0, yMin = 0, xMax = 0, yMax = 0;

	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		int objX, objY, objW, objH;
		CellGetEffectiveObjBounds(&info, &objX, &objY, &objW, &objH);

		if (i == 0 || objX < xMin) xMin = objX;
		if (i == 0 || objY < yMin) yMin = objY;
		if (i == 0 || (objX + objW) > xMax) xMax = objX + objW;
		if (i == 0 || (objY + objH) > yMax) yMax = objY + objH;
	}

	*pxMin = xMin;
	*pxMax = xMax;
	*pyMin = yMin;
	*pyMax = yMax;
}

static int CellIsCellSimple(NCER_CELL *cell) {
	//we'll determine if (relative to a minimum coordinate) all OBJ are non-overlapping and on
	//8x8 boundaries.
	int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
	CellGetCellBounds(cell, &xMin, &yMin, &xMax, &yMax);

	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		int objX, objY, objW, objH;
		CellGetEffectiveObjBounds(&info, &objX, &objY, &objW, &objH);

		//check overflow on right and bottom edges
		if ((objX + objW) > 256) return 0;
		if ((objY + objH) > 128) return 0;
	}

	//width must be within 256px for simple shape
	if ((xMax - xMin) > 256) return 0;

	//check 8x8 boundaries
	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		int objX, objY, objW, objH;
		CellGetEffectiveObjBounds(&info, &objX, &objY, &objW, &objH);
		objX -= xMin;
		objY -= yMin;

		if ((objX & 7) || (objY & 7)) return 0;
	}

	//check overlap
	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info1;
		CellDecodeOamAttributes(&info1, cell, i);

		int obj1X, obj1Y, obj1W, obj1H;
		CellGetEffectiveObjBounds(&info1, &obj1X, &obj1Y, &obj1W, &obj1H);

		for (int j = i + 1; j < cell->nAttribs; j++) {
			NCER_CELL_INFO info2;
			CellDecodeOamAttributes(&info2, cell, j);

			int obj2X, obj2Y, obj2W, obj2H;
			CellGetEffectiveObjBounds(&info2, &obj2X, &obj2Y, &obj2W, &obj2H);

			//check bounds
			if ((obj2X + obj2W) <= obj1X) continue;
			if ((obj1X + obj1W) <= obj2X) continue;
			if ((obj2Y + obj2H) <= obj1Y) continue;
			if ((obj1Y + obj1H) <= obj2Y) continue;
			return 0;
		}
	}

	return 1;
}

static void CellMapGraphicsTo2D(
	unsigned char **outChars,
	unsigned char  *outAttr,
	unsigned int    dstStride,
	unsigned int    destX,
	unsigned int    destY,
	unsigned char **srcChars,
	unsigned char  *srcAttr,
	unsigned int    srcSize,
	unsigned int    chrAddr,
	unsigned int    nCharsX,
	unsigned int    nCharsY,
	int             flipX,
	int             flipY
) {
	unsigned int nObjChars = nCharsX * nCharsY;

	//transform for pixel index in character
	unsigned int xorMask = 0;
	if (flipX) xorMask |= 007;
	if (flipY) xorMask |= 070;

	for (unsigned int k = 0; k < nObjChars; k++) {
		unsigned int dstOffsX = k % nCharsX;
		unsigned int dstOffsY = k / nCharsX;
		if (flipX) dstOffsX = (nCharsX - 1 - dstOffsX);
		if (flipY) dstOffsY = (nCharsY - 1 - dstOffsY);

		unsigned int dstAddr = (destY + dstOffsY) * dstStride + (destX + dstOffsX);
		unsigned int srcAddr = chrAddr + k;
		if (outAttr != NULL && srcAttr != NULL) outAttr[dstAddr] = srcAttr[srcAddr];

		//copy OBJ graphics
		if (srcAddr < srcSize) {
			for (unsigned int l = 0; l < 64; l++) {
				outChars[dstAddr][l] = srcChars[srcAddr][l ^ xorMask];
			}
		} else {
			memset(outChars[dstAddr], 0, 64);
		}

	}
}

static void CellArrangeBankIn2D(NCER *ncer, NCGR *ncgr, int *pGraphicsWidth, int *pGraphicsHeight, unsigned char **outChars, unsigned char *outAttr) {
	*pGraphicsWidth = 32; // default: width=32 chars
	*pGraphicsHeight = 0; // default: height=0 chars

	unsigned int chrSizeShift = ncgr->nBits == 8; // 4-bit: shift=0, 8-bit: shift=1
	unsigned int chrSizeBytes = 0x20 << chrSizeShift;
	unsigned int mappingShift = (ncer->mappingMode >> 20) & 0x7;
	unsigned int chnameShift = ncgr->nBits == 8;

	//iterate cells and find places to put each OBJ.
	int curX = 0, curY = 0, curRowHeight = 0;
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = &ncer->cells[i];

		int xMin, xMax, yMin, yMax;
		CellGetCellBounds(cell, &xMin, &yMin, &xMax, &yMax);

		//determine if it's a simple cell or a complex one
		if (CellIsCellSimple(cell)) {
			//simple cell: arrange OBJ relative to their position in space
			for (int j = 0; j < cell->nAttribs; j++) {
				NCER_CELL_INFO objInfo;
				CellDecodeOamAttributes(&objInfo, cell, j);

				//get placement of OBJ
				unsigned int chrName = cell->attr[j * 3 + 2] & 0x3FF; // use from OBJ directly, decode is overridden
				unsigned int chrAddr = (chrName << mappingShift) >> chrSizeShift;
				if (ncer->vramTransfer != NULL) {
					//transform character address in accordance with the VRAM transfer entry
					CHAR_VRAM_TRANSFER *trans = &ncer->vramTransfer[i];
					unsigned int chrAddrByte = chrAddr * chrSizeBytes;
					if (chrAddrByte >= trans->dstAddr && chrAddrByte < (trans->dstAddr + trans->size)) {
						chrAddr = (chrAddrByte + trans->srcAddr - trans->dstAddr) / chrSizeBytes;
					}
				}

				int objX, objY, objW, objH;
				CellGetEffectiveObjBounds(&objInfo, &objX, &objY, &objW, &objH);

				int charX = (objX - xMin) / 8;
				int charY = (objY - yMin) / 8;

				if (outChars != NULL) {
					//wrap graphics into 2D mapping
					unsigned int nObjCharsX = objW / 8;
					unsigned int nObjCharsY = objH / 8;
					CellMapGraphicsTo2D(
						outChars, outAttr,
						*pGraphicsWidth,
						curX + charX, curY + charY,
						ncgr->tiles, ncgr->attr,
						ncgr->nTiles,
						chrAddr,
						nObjCharsX, nObjCharsY,
						objInfo.flipX, objInfo.flipY
					);

					//write extended character name
					cell->ex2dCharNames[j] = ((curX + charX) + ((curY + charY) * *pGraphicsWidth)) << chnameShift;

					//reset flip state of current OBJ
					if (objInfo.flipX || objInfo.flipY) {
						cell->attr[j * 3 + 1] &= 0xCFFF;
					}
				}
			}

			//simple cell: advance row
			curX = 0;
			curRowHeight = 0;
			curY += (yMax - yMin) / 8;
		} else {
			//complex cell: arrange OBJ in order of occurrence
			for (int j = 0; j < cell->nAttribs; j++) {
				NCER_CELL_INFO objInfo;
				CellDecodeOamAttributes(&objInfo, cell, j);

				//get placement of OBJ
				unsigned int chrName = cell->attr[j * 3 + 2] & 0x3FF; // use from OBJ directly, decode is overridden
				unsigned int chrAddr = (chrName << mappingShift) >> chrSizeShift;
				if ((curX + objInfo.width / 8) > *pGraphicsWidth) {
					//advance to next row
					curY += curRowHeight;
					curX = 0;
					curRowHeight = 0;
				}

				//check size of row
				if (objInfo.height / 8 > curRowHeight) curRowHeight = objInfo.height / 8;

				//do write output?
				if (outChars != NULL) {
					//wrap graphics into 2D mapping
					unsigned int nObjCharsX = objInfo.width / 8;
					unsigned int nObjCharsY = objInfo.height / 8;
					CellMapGraphicsTo2D(
						outChars, outAttr,
						*pGraphicsWidth,
						curX, curY,
						ncgr->tiles, ncgr->attr,
						ncgr->nTiles,
						chrAddr,
						nObjCharsX, nObjCharsY,
						objInfo.flipX, objInfo.flipY
					);

					//write extended character name
					cell->ex2dCharNames[j] = (curX + (curY * *pGraphicsWidth)) << chnameShift;

					//reset flip state of current OBJ
					if (objInfo.flipX || objInfo.flipY) {
						cell->attr[j * 3 + 1] &= 0xCFFF;
					}
				}

				curX += objInfo.width / 8;
			}
		}

		//advance to a new line
		if (curX) {
			curY += curRowHeight;
			curX = 0;
			curRowHeight = 0;
		}
	}

	//on final output, stub out VRAM transfer entries
	if (outChars != NULL && ncer->vramTransfer != NULL) {
		for (int i = 0; i < ncer->nCells; i++) {
			CHAR_VRAM_TRANSFER *trans = &ncer->vramTransfer[i];
			trans->srcAddr = 0;
			trans->dstAddr = 0;
			trans->size = 0;
		}
	}

	//if graphics are empty, provide a minimum default
	if (curY == 0) {
		curY = 32;
	}

	*pGraphicsHeight = curY;
}

static unsigned char CellSampleObjPixelWithFlip(const unsigned char *gfx, unsigned int nCharsX, unsigned int nCharsY, unsigned int pi, int flipX, int flipY) {
	//xor mask for transforming coordinates
	unsigned int xorMask = 0;
	if (flipX) xorMask |= 007;
	if (flipY) xorMask |= 070;

	unsigned int charno = pi / 64;

	//get char index to retrieve
	unsigned int charSrcX = charno % nCharsX;
	unsigned int charSrcY = charno / nCharsX;
	if (flipX) charSrcX = nCharsX - 1 - charSrcX;
	if (flipY) charSrcY = nCharsY - 1 - charSrcY;

	const unsigned char *chr2 = gfx + 64 * (charSrcX + charSrcY * nCharsX);
	return chr2[(pi % 64) ^ xorMask];
}

static unsigned int CellSearchGraphics(
	unsigned char *buf,
	unsigned int   nCharsBuf,
	unsigned char *needle,
	unsigned int   nCharsXNeedle,
	unsigned int   nCharsYNeedle,
	unsigned int   granularity,
	unsigned int   searchStart,
	int            allowFlip,
	int           *pFoundFlipX,
	int           *pFoundFlipY
) {
	//we allow the match to run off the end (partial match)
	unsigned int nCharsNeedle = nCharsXNeedle * nCharsYNeedle;
	for (unsigned int i = searchStart; i < nCharsBuf; i += granularity) {
		unsigned int nCharsCompare = nCharsNeedle;
		if ((nCharsBuf - i) < nCharsCompare) nCharsCompare = nCharsBuf - i;

		//iterate flips
		for (int flip = 0; flip < 4 && (flip == 0 || allowFlip); flip++) {
			int flipX = (flip >> 0) & 1;
			int flipY = (flip >> 1) & 1;

			int matched = 1;
			for (unsigned int j = 0; j < (nCharsCompare * 64); j++) {
				if (buf[i * 64 + j] != CellSampleObjPixelWithFlip(needle, nCharsXNeedle, nCharsYNeedle, j, flipX, flipY)) {
					matched = 0;
					break;
				}
			}

			if (matched) {
				*pFoundFlipX = flipX;
				*pFoundFlipY = flipY;
				return i;
			}
		}
	}

	*pFoundFlipX = 0;
	*pFoundFlipY = 0;
	return nCharsBuf;
}

static int CellArrangeBankIn1D(NCER *ncer, NCGR *ncgr, int cellCompression, unsigned int *pGraphicsSize, unsigned char **outChars, unsigned char *outAttr) {
	//arrange all cell graphics in space.

	unsigned char *curbuf = NULL;
	unsigned int curbufSize = 0;

	//get mapping mode parameters
	unsigned int mappingShift = (ncer->ex2dBaseMappingMode >> 20) & 7;
	unsigned int mappingGranularity = (1 << mappingShift) >> (ncgr->nBits == 8);
	unsigned int charSizeBytes = 8 * ncgr->nBits;
	if (mappingGranularity == 0) mappingGranularity = 1;

	unsigned char *tempbuf = (unsigned char *) calloc(64 * 64, 1);
	if (tempbuf == NULL) return 0;

	int status = 1; // OK

	//we support two kinds of cells: those that will allow compression and those that do not. When a cell
	//forbids compression, we'll push it to the front of the graphics to keep them at predictable locations.
	//to accomplish this, we will run over the cell data twice.
	unsigned int compressCellstart = 0; // start of compressible cell graphics
	for (int doCompress = 0; doCompress <= 1; doCompress++) {
		//iterate cells
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = &ncer->cells[i];

			//we will only process cells with compression modes matching the current phase.
			if ((!cell->forbidCompression) != doCompress) continue;

			//search start: beginning of file (cell mode: limit to within cell)
			unsigned int searchStart = compressCellstart;
			if (cellCompression) searchStart = (curbufSize + mappingGranularity - 1) & ~(mappingGranularity - 1);

			if (ncer->vramTransfer != NULL) {
				//cell bank with VRAM transfer animation: set up source and destination
				CHAR_VRAM_TRANSFER *trans = &ncer->vramTransfer[i];
				trans->dstAddr = 0;
				trans->srcAddr = searchStart * charSizeBytes;
			}

			for (int j = 0; j < cell->nAttribs; j++) {
				NCER_CELL_INFO info;
				CellDecodeOamAttributes(&info, cell, j);

				//lay out graphics into temp buffer
				uint32_t chrAddr = info.characterName >> (ncgr->nBits == 8);
				unsigned int chrX = chrAddr % (unsigned int) ncgr->tilesX;
				unsigned int chrY = chrAddr / (unsigned int) ncgr->tilesX;
				unsigned int nCharsX = info.width / 8;
				unsigned int nCharsY = info.height / 8;

				for (unsigned int x = 0; x < nCharsX; x++) {
					for (unsigned int y = 0; y < nCharsY; y++) {
						unsigned int srcAddr = (x + chrX) + (y + chrY) * ncgr->tilesX;

						if (srcAddr < (unsigned int) ncgr->nTiles) {
							memcpy(tempbuf + 64 * (x + y * nCharsX), ncgr->tiles[srcAddr], 64);
						} else {
							memset(tempbuf + 64 * (x + y * nCharsX), 0, 64);
						}
					}
				}

				//search
				int foundFlipX = 0, foundFlipY = 0;
				unsigned int foundAt = curbufSize;
				if (doCompress) {
					//if compression of this cell's graphics is allowed, we will search for repeated graphics data
					foundAt = CellSearchGraphics(
						curbuf,
						curbufSize,
						tempbuf,
						nCharsX,
						nCharsY,
						mappingGranularity,
						searchStart,
						!info.rotateScale,
						&foundFlipX,
						&foundFlipY
					);
				}
				if ((curbufSize - foundAt) < (nCharsX * nCharsY)) {
					//append graphics to buffer
					unsigned int offsWrite = curbufSize - foundAt;

					//compute needed expansion plus padding to round up to a mapping unit
					unsigned int newbufSize = curbufSize + nCharsX * nCharsY - offsWrite;
					newbufSize = (newbufSize + mappingGranularity - 1) / mappingGranularity * mappingGranularity;

					unsigned char *newbuf = realloc(curbuf, newbufSize * 64);
					if (newbuf == NULL) {
						status = 0; // fail
						curbufSize = 0;
						goto Done;
					}
					curbuf = newbuf;
					memset(curbuf + curbufSize * 64, 0, (newbufSize - curbufSize) * 64);

					//write graphics flipped
					for (unsigned int l = 64 * offsWrite; l < 64 * (nCharsX * nCharsY); l++) {
						curbuf[foundAt * 64 + l] = CellSampleObjPixelWithFlip(tempbuf, nCharsX, nCharsY, l, foundFlipX, foundFlipY);
					}
					if (outAttr != NULL) {
						memset(outAttr + foundAt + offsWrite, info.palette, nCharsX * nCharsY - offsWrite);
					}
					curbufSize = newbufSize;
				}

				//compute character name
				unsigned int chrName = (foundAt << (ncgr->nBits == 8)) >> mappingShift;
				if (ncer->vramTransfer != NULL) {
					//cell bank uses VRAM transfer animations, subtract the base character name
					chrName = ((foundAt - searchStart) << (ncgr->nBits == 8)) >> mappingShift;
				}

				//check the character name did not overflow
				if (chrName & ~0x03FF) {
					status = 0;
					curbufSize = 0;
					goto Done;
				}

				if (outChars != NULL) {
					cell->attr[3 * j + 2] = (cell->attr[3 * j + 2] & 0xFC00) | (chrName & 0x03FF);
					if (foundFlipX) cell->attr[3 * j + 1] ^= 0x1000; // flip H
					if (foundFlipY) cell->attr[3 * j + 1] ^= 0x2000; // flip V
				}
			}

			//after adding OBJ to cell, finalize VRAM transfer settings
			if (ncer->vramTransfer != NULL) {
				CHAR_VRAM_TRANSFER *trans = &ncer->vramTransfer[i];
				trans->size = curbufSize * charSizeBytes - trans->srcAddr;
			}
		}

		//at the end of the no-compress pass, set the compressible cells offset.
		if (!doCompress) {
			compressCellstart = (curbufSize + mappingGranularity - 1) & ~(mappingGranularity - 1);
		}
	}

	if (curbufSize == 0) {
		curbufSize++;
		curbuf = realloc(curbuf, curbufSize * 64);
		memset(curbuf, 0, curbufSize * 64);
	}

	if (outChars != NULL) {
		for (unsigned int i = 0; i < curbufSize; i++) {
			memcpy(outChars[i], curbuf + 64 * i, 64);
		}
	}

Done:
	if (curbuf != NULL) free(curbuf);
	if (tempbuf != NULL) free(tempbuf);
	*pGraphicsSize = curbufSize;
	return status;
}

int CellSetBankExt2D(NCER *ncer, NCGR *ncgr, int enable) {
	enable = !!enable;
	if (ncer->isEx2d == enable) return 1; // do nothing

	if (!enable) {
		int cellCompression = (ncer->vramTransfer != NULL);
		unsigned int graphicsSize;
		int status = CellArrangeBankIn1D(ncer, ncgr, cellCompression, &graphicsSize, NULL, NULL);
		if (!status) return 0;

		//allocate new graphics
		unsigned char *outAttr = (unsigned char *) calloc(graphicsSize, 1);
		unsigned char **outChars = (unsigned char **) calloc(graphicsSize, sizeof(void *));
		unsigned char *charbuf = (unsigned char *) calloc(graphicsSize, 64);
		for (unsigned int i = 0; i < graphicsSize; i++) {
			outChars[i] = charbuf + 64 * i;
		}
		CellArrangeBankIn1D(ncer, ncgr, cellCompression, &graphicsSize, outChars, outAttr);

		//replace graphics data with rearranged graphics
		free(ncgr->charbuf);
		free(ncgr->tiles);
		free(ncgr->attr);
		ncgr->charbuf = charbuf;
		ncgr->tiles = outChars;
		ncgr->attr = outAttr;
		ncgr->nTiles = graphicsSize;
		ncgr->tilesX = ChrGuessWidth(graphicsSize);
		ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;

		//restore mapping mode
		ncer->mappingMode = ncer->ex2dBaseMappingMode;
		ncer->ex2dBaseMappingMode = 0;
	}

	//update ext 2D field
	ncer->isEx2d = enable;
	ncgr->isExChar = enable;
	for (int i = 0; i < ncer->nCells; i++) {
		ncer->cells[i].useEx2d = enable;

		if (enable) {
			ncer->cells[i].ex2dCharNames = calloc(ncer->cells[i].nAttribs, sizeof(uint32_t));
		} else {
			if (ncer->cells[i].ex2dCharNames != NULL) {
				free(ncer->cells[i].ex2dCharNames);
				ncer->cells[i].ex2dCharNames = NULL;
			}
		}
	}

	if (enable) {
		//save mapping mode
		ncer->ex2dBaseMappingMode = ncer->mappingMode;

		//check source mapping mode
		if (ncer->mappingMode == GX_OBJVRAMMODE_CHAR_2D) {
			//mapped in 2D: we will not rearrange graphics, just populate the ex2dCharNames.
			for (int i = 0; i < ncer->nCells; i++) {
				NCER_CELL *cell = &ncer->cells[i];
				for (int j = 0; j < cell->nAttribs; j++) {
					cell->ex2dCharNames[j] = cell->attr[3 * j + 2] & 0x03FF;
				}
			}

			//override backing mapping mode
			ncer->ex2dBaseMappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
		} else {
			//cell bank is not using 2D mapping mode, so reconstruct data.
			int graphicsWidth, graphicsHeight;
			CellArrangeBankIn2D(ncer, ncgr, &graphicsWidth, &graphicsHeight, NULL, NULL);

			unsigned int graphicsSize = graphicsWidth * graphicsHeight;

			//allocate new graphics
			unsigned char *outAttr = (unsigned char *) calloc(graphicsSize, 1);
			unsigned char **outChars = (unsigned char **) calloc(graphicsSize, sizeof(void *));
			unsigned char *charbuf = (unsigned char *) calloc(graphicsSize, 64);
			for (unsigned int i = 0; i < graphicsSize; i++) {
				outChars[i] = charbuf + 64 * i;
			}
			CellArrangeBankIn2D(ncer, ncgr, &graphicsWidth, &graphicsHeight, outChars, outAttr);

			//replace graphics data with rearranged graphics
			free(ncgr->charbuf);
			free(ncgr->tiles);
			free(ncgr->attr);
			ncgr->charbuf = charbuf;
			ncgr->tiles = outChars;
			ncgr->attr = outAttr;
			ncgr->tilesX = graphicsWidth;
			ncgr->tilesY = graphicsHeight;
			ncgr->nTiles = graphicsWidth * graphicsHeight;
		}
	}
	return 1;
}

void CellRemoveEx2dAttr(
	NCER *ncer
) {
	if (!ncer->isEx2d) return;

	//set mapping to 2D
	ncer->isEx2d = 0;
	ncer->mappingMode = GX_OBJVRAMMODE_CHAR_2D;

	//free attributes
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = &ncer->cells[i];

		free(cell->ex2dCharNames);
		cell->ex2dCharNames = NULL;
		cell->useEx2d = 0;
	}
}
