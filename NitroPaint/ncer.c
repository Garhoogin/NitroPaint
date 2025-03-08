#include "ncer.h"
#include "nclr.h"
#include "ncgr.h"
#include "nns.h"
#include "setosa.h"

#define SEXT8(n)   (((n)<0x080)?(n):((n)-0x100))
#define SEXT9(n)   (((n)<0x100)?(n):((n)-0x200))

LPCWSTR cellFormatNames[] = { L"Invalid", L"NCER", L"Setosa", L"Hudson", L"Ghost Trick", NULL };

void CellInit(NCER *ncer, int format) {
	ncer->header.size = sizeof(NCER);
	ObjInit((OBJECT_HEADER *) ncer, FILE_TYPE_CELL, format);
	ncer->header.dispose = CellFree;
}

int CellIsValidHudson(const unsigned char *buffer, unsigned int size) {
	unsigned int nCells = *(unsigned int *) buffer;
	if (nCells == 0) return 0;

	unsigned int highestOffset = 4;
	for (unsigned int i = 0; i < nCells; i++) {
		uint32_t ofs = ((uint32_t *) (buffer + 4))[i] + 4;
		if (4 + i * 4 + 4 >= highestOffset) highestOffset = 8 + i * 4;
		if (ofs >= size) return 0;
		unsigned int nOAM = *(uint16_t *) (buffer + ofs);
		
		//attrs size: 0xA * nOAM
		unsigned int endOfs = ofs + 2 + 0xA * nOAM;
		if (endOfs > highestOffset) highestOffset = endOfs;
		if (endOfs > size) return 0;
	}

	//if there is much unused data at the end, this is probably not an actual cell file!
	if (highestOffset < size) return 0;
	return 1;
}

int CellIsValidNcer(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "RECN", 4) != 0) return 0;

	//must have CEBK section
	const unsigned char *cebk = NnsG2dFindBlockBySignature(buffer, size, "CEBK", NNS_SIG_LE, NULL);
	if (cebk == NULL) return 0;

	return 1;
}

int CellIsValidGhostTrick(const unsigned char *buffer, unsigned int size) {
	if (size < 2) return 0; //must contain at least 1 cell

	const uint16_t *cellOffsets = (uint16_t *) buffer;
	unsigned int nCells = cellOffsets[0]; //first offset is count
	unsigned int cellOffset = nCells * 2;
	if (cellOffset > size) return 0;

	//read cells
	unsigned int readSize = 2;
	for (unsigned int i = 0; i < nCells; i++) {
		unsigned int offset = cellOffsets[i] * 2;
		if ((offset + 2) >= size) return 0;

		const unsigned char *cell = buffer + offset;
		unsigned int nObj = *(uint16_t *) cell;

		//file must be big enough to fit contained OBJ
		unsigned int endOfs = offset + 2 + nObj * 6;
		if (endOfs > size) return 0;
		if (endOfs > readSize) readSize = endOfs;
	}
	if (readSize < size) return 0;
	return 1;
}

int CellIdentify(const unsigned char *buffer, unsigned int size) {
	if (CellIsValidNcer(buffer, size)) return NCER_TYPE_NCER;
	if (CellIsValidHudson(buffer, size)) return NCER_TYPE_HUDSON;
	if (CellIsValidGhostTrick(buffer, size)) return NCER_TYPE_GHOSTTRICK;
	return NCER_TYPE_INVALID;
}

int CellReadHudson(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	int nCells = *(uint32_t *) buffer;
	CellInit(ncer, NCER_TYPE_HUDSON);
	ncer->labl = NULL;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->uextSize = 0;
	ncer->nCells = nCells;
	ncer->bankAttribs = 0;
	
	NCER_CELL *cells = (NCER_CELL *) calloc(nCells, sizeof(NCER_CELL));
	ncer->cells = cells;
	for (int i = 0; i < nCells; i++) {
		uint32_t ofs = ((uint32_t *) (buffer + 4))[i] + 4;
		int nOAM = *(uint16_t *) (buffer + ofs);
		NCER_CELL *thisCell = cells + i;
		CellInitBankCell(ncer, thisCell, nOAM);

		int minX = 0x7FFF, maxX = -0x7FFF, minY = 0x7FFF, maxY = -0x7FFF;
		uint16_t *attrs = (uint16_t *) (buffer + ofs + 2);
		for (int j = 0; j < nOAM; j++) {
			memcpy(thisCell->attr + j * 3, attrs + j * 5, 6);
			NCER_CELL_INFO info;
			CellDecodeOamAttributes(&info, thisCell, j);

			int16_t x = attrs[j * 5 + 3];
			int16_t y = attrs[j * 5 + 4];
			if (x < minX) minX = x;
			if (x + info.width > maxX) maxX = x + info.width;
			if (y < minY) minY = y;
			if (y + info.height > maxY) maxY = y + info.height;
		}

		thisCell->maxX = maxX;
		thisCell->minX = minX;
		thisCell->maxY = maxY;
		thisCell->minY = minY;
	}
	return 0;
}

int CellReadNcer(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	CellInit(ncer, NCER_TYPE_NCER);
	ncer->nCells = 0;
	ncer->uextSize = 0;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->labl = NULL;

	unsigned int cebkSize = 0, lablSize = 0, uextSize = 0;
	const unsigned char *cebk = NnsG2dFindBlockBySignature(buffer, size, "CEBK", NNS_SIG_LE, &cebkSize);
	const unsigned char *labl = NnsG2dFindBlockBySignature(buffer, size, "LABL", NNS_SIG_LE, &lablSize);
	const unsigned char *uext = NnsG2dFindBlockBySignature(buffer, size, "UEXT", NNS_SIG_LE, &uextSize);

	//bank
	if (cebk != NULL) {
		ncer->nCells = *(uint16_t *) cebk;
		ncer->cells = (NCER_CELL *) calloc(ncer->nCells, sizeof(NCER_CELL));
		ncer->bankAttribs = *(uint16_t *) (cebk + 2); //1 - with bounding rectangle, 0 - without
		const unsigned char *cellData = cebk + *(uint32_t *) (cebk + 4);

		uint32_t mappingMode = *(uint32_t *) (cebk + 8);
		const int mappingModes[] = {
			GX_OBJVRAMMODE_CHAR_1D_32K,
			GX_OBJVRAMMODE_CHAR_1D_64K,
			GX_OBJVRAMMODE_CHAR_1D_128K,
			GX_OBJVRAMMODE_CHAR_1D_256K,
			GX_OBJVRAMMODE_CHAR_2D
		};
		if (mappingMode < 5) ncer->mappingMode = mappingModes[mappingMode];
		else ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;

		int perCellDataSize = 8;
		if (ncer->bankAttribs == 1) perCellDataSize += 8;
		const unsigned char *oamData = cellData + (ncer->nCells * perCellDataSize);

		for (int i = 0; i < ncer->nCells; i++) {
			int nOAMEntries = *(uint16_t *) (cellData + 0);
			int cellAttr = *(uint16_t *) (cellData + 2);
			uint32_t pOamAttrs = *(uint32_t *) (cellData + 4);

			NCER_CELL *cell = ncer->cells + i;
			CellInitBankCell(ncer, cell, nOAMEntries ? nOAMEntries : 1);
			cell->cellAttr = cellAttr;

			if (nOAMEntries != 0) {
				uint16_t *cellOam = (uint16_t *) (oamData + pOamAttrs);
				memcpy(cell->attr, oamData + pOamAttrs, cell->nAttribs * 3 * 2);

				if (perCellDataSize >= 16) {
					cell->maxX = *(int16_t *) (cellData + 0x8);
					cell->maxY = *(int16_t *) (cellData + 0xA);
					cell->minX = *(int16_t *) (cellData + 0xC);
					cell->minY = *(int16_t *) (cellData + 0xE);
				}
			} else {
				// Provide at least one OAM attribute for empty cells
				cell->attr[0] = 0x0200; // Disable rendering
			}

			cellData += perCellDataSize;
		}

		//VRAM transfer
		uint32_t vramTransferOffset = *(uint32_t *) (cebk + 0xC);
		if (vramTransferOffset && vramTransferOffset != 0xFFFFFFFF) {
			const unsigned char *vramTransferData = (cebk + vramTransferOffset);
			uint32_t maxTransfer = *(uint32_t *) (vramTransferData);
			uint32_t transferDataOffset = vramTransferOffset + *(uint32_t *) (vramTransferData + 4);

			ncer->vramTransfer = (CHAR_VRAM_TRANSFER *) calloc(ncer->nCells, sizeof(CHAR_VRAM_TRANSFER));
			for (int i = 0; i < ncer->nCells; i++) {
				ncer->vramTransfer[i].dstAddr = 0;
				ncer->vramTransfer[i].srcAddr = *(uint32_t *) (cebk + transferDataOffset + i * 8 + 0);
				ncer->vramTransfer[i].size = *(uint32_t *) (cebk + transferDataOffset + i * 8 + 4);
			}
		}

		//user extended attributes
		uint32_t userExtendedOffset = *(uint32_t *) (cebk + 0x14);
		if (userExtendedOffset) {
			const unsigned char *userEx = cebk + userExtendedOffset;
			
			//search for UCAT block
			if (userEx[0] == 'T' && userEx[1] == 'A' && userEx[2] == 'C' && userEx[3] == 'U') {
				userEx += 8;

				uint32_t offsStart = *(uint32_t *) (userEx + 0x4);

				int nCellEx = *(const uint16_t *) (userEx + 0x0);
				const uint32_t *attroffs = (const uint32_t *) (userEx + offsStart);
				for (int i = 0; i < nCellEx; i++) {
					ncer->cells[i].attrEx = *(const uint32_t *) (userEx + attroffs[i]);
				}
				ncer->useExtAttr = 1;
			}
		}
	}

	//NC label
	if (labl != NULL) {
		ncer->lablSize = lablSize;
		ncer->labl = calloc(lablSize + 1, 1);
		memcpy(ncer->labl, labl, lablSize);
	}

	//user extended
	if (uext != NULL) {
		ncer->uextSize = uextSize;
		ncer->uext = calloc(uextSize, 1);
		memcpy(ncer->uext, uext, uextSize);
	}

	return 0;
}

int CellReadGhostTrick(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	const uint16_t *cellOffs = (const uint16_t *) buffer;
	int nCells = *(uint16_t *) buffer;
	NCER_CELL *cells = (NCER_CELL *) calloc(nCells, sizeof(NCER_CELL));

	for (int i = 0; i < nCells; i++) {
		const unsigned char *cell = buffer + cellOffs[i] * 2;
		int nObj = *(uint16_t *) cell;
		CellInitBankCell(ncer, &cells[i], nObj ? nObj : 1);

		if (nObj != 0) {
			memcpy(cells[i].attr, cell + 2, nObj * 3 * 2);
		} else {
			// Provide at least one OAM attribute for empty cells
			cells[i].attr[0] = 0x0200; // Disable rendering
		}

	}

	CellInit(ncer, NCER_TYPE_GHOSTTRICK);
	ncer->nCells = nCells;
	ncer->cells = cells;
	ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_128K;
	return 0;
}

int CellRead(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	int type = CellIdentify(buffer, size);
	switch (type) {
		case NCER_TYPE_NCER:
			return CellReadNcer(ncer, buffer, size);
		case NCER_TYPE_HUDSON:
			return CellReadHudson(ncer, buffer, size);
		case NCER_TYPE_GHOSTTRICK:
			return CellReadGhostTrick(ncer, buffer, size);
	}
	return 1;
}
int CellReadFile(NCER *ncer, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) ncer, (OBJECT_READER) CellRead);
}

void CellInitBankCell(NCER *ncer, NCER_CELL *cell, int nObj) {
	memset(cell, 0, sizeof(NCER_CELL));

	cell->nAttribs = nObj;
	cell->attr = (uint16_t *) calloc(nObj, 3 * sizeof(uint16_t));
}

void CellGetObjDimensions(int shape, int size, int *width, int *height) {
	//on hardware, shape=3 gives an 8x8 OBJ
	int widths [4][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, {  8,  8, 16, 32 }, { 8, 8, 8, 8 } };
	int heights[4][4] = { { 8, 16, 32, 64 }, {  8,  8, 16, 32 }, { 16, 32, 32, 64 }, { 8, 8, 8, 8 } };

	*width = widths[shape][size];
	*height = heights[shape][size];
}

int CellDecodeOamAttributes(NCER_CELL_INFO *info, NCER_CELL *cell, int oam) {
	if (oam >= cell->nAttribs) {
		return 1;
	}

	uint16_t attr0 = cell->attr[oam * 3 + 0];
	uint16_t attr1 = cell->attr[oam * 3 + 1];
	uint16_t attr2 = cell->attr[oam * 3 + 2];

	info->x = attr1 & 0x1FF;
	info->y = attr0 & 0xFF;
	int shape = attr0 >> 14;
	int size = attr1 >> 14;

	CellGetObjDimensions(shape, size, &info->width, &info->height);
	info->size = size;
	info->shape = shape;

	info->characterName = cell->useEx2d ? cell->ex2dCharNames[oam] : attr2 & 0x3FF;
	info->priority = (attr2 >> 10) & 0x3;
	info->palette = (attr2 >> 12) & 0xF;
	info->mode = (attr0 >> 10) & 3;
	info->mosaic = (attr0 >> 12) & 1;

	int rotateScale = (attr0 >> 8) & 1;
	info->rotateScale = rotateScale;
	if (rotateScale) {
		info->flipX = 0;
		info->flipY = 0;
		info->doubleSize = (attr0 >> 9) & 1;
		info->disable = 0;
		info->matrix = (attr1 >> 9) & 0x1F;
	} else {
		info->flipX = (attr1 >> 12) & 1;
		info->flipY = (attr1 >> 13) & 1;
		info->doubleSize = 0;
		info->disable = (attr0 >> 9) & 1;
		info->matrix = 0;
	}

	int is8 = (attr0 >> 13) & 1;
	info->characterBits = 4;
	if (is8) {
		info->characterBits = 8;
		//info->palette = 0;
		//info->characterName <<= 1;
	}

	return 0;
}

void CellRenderObj(NCER_CELL_INFO *info, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, COLOR32 *out) {
	int tilesX = info->width / 8;
	int tilesY = info->height / 8;

	if (ncgr != NULL) {
		int charSize = ncgr->nBits * 8;
		int ncgrStart = NCGR_CHNAME(info->characterName, mapping, ncgr->nBits);
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				COLOR32 block[64];

				int bitsOffset = x * 8 + (y * 8 * tilesX * 8);
				int index;
				if (NCGR_2D(mapping)) {
					int ncx = x + ncgrStart % ncgr->tilesX;
					int ncy = y + ncgrStart / ncgr->tilesX;
					index = ncx + ncgr->tilesX * ncy;
				} else {
					index = ncgrStart + x + y * tilesX;
				}

				ChrRenderCharacterTransfer(ncgr, nclr, index, vramTransfer, block, info->palette, TRUE);
				for (int i = 0; i < 8; i++) {
					memcpy(out + bitsOffset + tilesX * 8 * i, block + i * 8, 32);
				}
			}
		}
	}
}

COLOR32 *CellRenderCell(COLOR32 *px, NCER_CELL *cell, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, int xOffs, int yOffs, float a, float b, float c, float d) {
	COLOR32 *block = (COLOR32 *) calloc(64 * 64, 4);
	for (int i = cell->nAttribs - 1; i >= 0; i--) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		CellRenderObj(&info, mapping, ncgr, nclr, vramTransfer, block);

		//HV flip? Only if not affine!
		if (!info.rotateScale) {
			COLOR32 temp[64];
			if (info.flipY) {
				for (int i = 0; i < info.height / 2; i++) {
					memcpy(temp, block + i * info.width, info.width * 4);
					memcpy(block + i * info.width, block + (info.height - 1 - i) * info.width, info.width * 4);
					memcpy(block + (info.height - 1 - i) * info.width, temp, info.width * 4);

				}
			}
			if (info.flipX) {
				for (int i = 0; i < info.width / 2; i++) {
					for (int j = 0; j < info.height; j++) {
						COLOR32 left = block[i + j * info.width];
						block[i + j * info.width] = block[info.width - 1 - i + j * info.width];
						block[info.width - 1 - i + j * info.width] = left;
					}
				}
			}
		}

		if (!info.disable) {
			int x = info.x;
			int y = info.y;
			//adjust for double size
			if (info.doubleSize) {
				x += info.width / 2;
				y += info.height / 2;
			}
			//copy data
			if (!info.rotateScale) {
				for (int j = 0; j < info.height; j++) {
					int _y = (y + j + yOffs) & 0xFF;
					for (int k = 0; k < info.width; k++) {
						int _x = (x + k + xOffs) & 0x1FF;
						COLOR32 col = block[j * info.width + k];
						if (col >> 24) {
							px[_x + _y * 512] = block[j * info.width + k];
						}
					}
				}
			} else {
				//transform about center
				int realWidth = info.width << info.doubleSize;
				int realHeight = info.height << info.doubleSize;
				int cx = realWidth / 2;
				int cy = realHeight / 2;
				int realX = x - (realWidth - info.width) / 2;
				int realY = y - (realHeight - info.height) / 2;
				for (int j = 0; j < realHeight; j++) {
					int destY = (realY + j + yOffs) & 0xFF;
					for (int k = 0; k < realWidth; k++) {
						int destX = (realX + k + xOffs) & 0x1FF;

						int srcX = (int) ((k - cx) * a + (j - cy) * b) + cx;
						int srcY = (int) ((k - cx) * c + (j - cy) * d) + cy;

						if (info.doubleSize) {
							srcX -= realWidth / 4;
							srcY -= realHeight / 4;
						}
						if (srcX >= 0 && srcY >= 0 && srcX < info.width && srcY < info.height) {
							COLOR32 src = block[srcY * info.width + srcX];
							if(src >> 24) px[destX + destY * 512] = src;
						}

					}
				}
			}
		}
	}
	free(block);
	return px;
}

void CellDeleteCell(NCER *ncer, int idx) {
	memmove(ncer->cells + idx, ncer->cells + idx + 1, (ncer->nCells - idx - 1) * sizeof(NCER_CELL));
	if (ncer->vramTransfer != NULL) {
		memmove(ncer->vramTransfer + idx, ncer->vramTransfer + idx + 1, (ncer->nCells - idx - 1) * sizeof(CHAR_VRAM_TRANSFER));
	}

	ncer->nCells--;
	ncer->cells = (NCER_CELL *) realloc(ncer->cells, ncer->nCells * sizeof(NCER_CELL));
	if (ncer->vramTransfer != NULL) {
		ncer->vramTransfer = (CHAR_VRAM_TRANSFER *) realloc(ncer->vramTransfer, ncer->nCells * sizeof(CHAR_VRAM_TRANSFER));
	}
}

void CellMoveCellIndex(NCER *ncer, int iSrc, int iDst) {
	if (iSrc == iDst) return; // no-op

	//copy temporarily
	NCER_CELL cellTmp;
	CHAR_VRAM_TRANSFER transTmp;
	memcpy(&cellTmp, ncer->cells + iSrc, sizeof(cellTmp));
	if (ncer->vramTransfer != NULL) {
		memcpy(&transTmp, ncer->vramTransfer + iSrc, sizeof(transTmp));
	}

	//slide over the source
	memmove(ncer->cells + iSrc, ncer->cells + iSrc + 1, (ncer->nCells - iSrc - 1) * sizeof(NCER_CELL));
	if (ncer->vramTransfer != NULL) {
		memmove(ncer->vramTransfer + iSrc, ncer->vramTransfer + iSrc + 1, (ncer->nCells - iSrc - 1) * sizeof(CHAR_VRAM_TRANSFER));
	}
	
	//adjust destination index to account for changed indices
	if (iDst > iSrc) iDst--;

	//move items to make space
	memmove(ncer->cells + iDst + 1, ncer->cells + iDst, (ncer->nCells - iDst - 1) * sizeof(NCER_CELL));
	if (ncer->vramTransfer != NULL) {
		memmove(ncer->vramTransfer + iDst + 1, ncer->vramTransfer + iDst, (ncer->nCells - iDst - 1) * sizeof(CHAR_VRAM_TRANSFER));
	}

	//copy from temp
	memcpy(ncer->cells + iDst, &cellTmp, sizeof(cellTmp));
	if (ncer->vramTransfer != NULL) {
		memcpy(ncer->vramTransfer + iDst, &transTmp, sizeof(transTmp));
	}
}

int CellFree(OBJECT_HEADER *header) {
	NCER *ncer = (NCER *) header;
	if (ncer->uext) free(ncer->uext);
	if (ncer->labl) free(ncer->labl);
	for (int i = 0; i < ncer->nCells; i++) {
		free(ncer->cells[i].attr);
		if (ncer->cells[i].ex2dCharNames) free(ncer->cells[i].ex2dCharNames);
	}
	if (ncer->cells) free(ncer->cells);
	if (ncer->vramTransfer) free(ncer->vramTransfer);
	
	
	return 0;
}

static int CellWriteNcer(NCER *ncer, BSTREAM *stream) {
	int cellSize = 8;
	if (ncer->bankAttribs & 1) cellSize = 16;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCER", 1, 0, NNS_TYPE_G2D, NNS_SIG_LE);

	//write CEBK
	{
		unsigned char cebkHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		NnsStreamStartBlock(&nnsStream, "CEBK");

		//mapping mode
		uint32_t mappingMode = 0;
		switch (ncer->mappingMode) {
			case GX_OBJVRAMMODE_CHAR_1D_32K:
				mappingMode = 0; break;
			case GX_OBJVRAMMODE_CHAR_1D_64K:
				mappingMode = 1; break;
			case GX_OBJVRAMMODE_CHAR_1D_128K:
				mappingMode = 2; break;
			case GX_OBJVRAMMODE_CHAR_1D_256K:
				mappingMode = 3; break;
			case GX_OBJVRAMMODE_CHAR_2D:
				mappingMode = 4; break;
		}

		unsigned int offsVramTransfer = 0;
		unsigned int offsUserExtended = 0;
		if (ncer->vramTransfer != NULL) {
			offsVramTransfer = sizeof(cebkHeader) + ncer->nCells * cellSize;
			for (int i = 0; i < ncer->nCells; i++) offsVramTransfer += ncer->cells[i].nAttribs * 6;
			offsVramTransfer = (offsVramTransfer + 3) & ~3;
		}
		if (ncer->useExtAttr) {
			offsUserExtended += sizeof(cebkHeader);
			offsUserExtended += ncer->nCells * cellSize;
			for (int i = 0; i < ncer->nCells; i++) offsUserExtended += ncer->cells[i].nAttribs * 6;
			offsUserExtended = (offsUserExtended + 3) & ~3;
			if (ncer->vramTransfer != NULL) {
				//add size of VRAM transfer information
				offsUserExtended += 8 + 8 * ncer->nCells;
			}
		}

		*(uint16_t *) (cebkHeader + 0x00) = ncer->nCells;
		*(uint16_t *) (cebkHeader + 0x02) = ncer->bankAttribs;
		*(uint32_t *) (cebkHeader + 0x04) = sizeof(cebkHeader);
		*(uint32_t *) (cebkHeader + 0x08) = mappingMode;
		*(uint32_t *) (cebkHeader + 0x0C) = offsVramTransfer;
		*(uint32_t *) (cebkHeader + 0x14) = offsUserExtended;
		NnsStreamWrite(&nnsStream, cebkHeader, sizeof(cebkHeader));

		//write out each cell. Keep track of the offsets of OAM data.
		int oamOffset = 0;
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = ncer->cells + i;
			unsigned char data[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

			*(uint16_t *) data = cell->nAttribs;
			*(uint16_t *) (data + 2) = cell->cellAttr;
			*(uint32_t *) (data + 4) = oamOffset;
			if (cellSize > 8) {
				*(int16_t *) (data + 0x08) = cell->maxX;
				*(int16_t *) (data + 0x0A) = cell->maxY;
				*(int16_t *) (data + 0x0C) = cell->minX;
				*(int16_t *) (data + 0x0E) = cell->minY;
			}

			NnsStreamWrite(&nnsStream, data, cellSize);
			oamOffset += cell->nAttribs * 3 * 2;
		}

		//write each cell's OAM attributes
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = ncer->cells + i;
			NnsStreamWrite(&nnsStream, cell->attr, cell->nAttribs * 6);
		}

		//write VRAM transfer character information
		if (ncer->vramTransfer != NULL) {
			NnsStreamAlign(&nnsStream, 4);

			uint32_t transferSizeMax = 0;
			for (int i = 0; i < ncer->nCells; i++) {
				if (ncer->vramTransfer[i].size > transferSizeMax) transferSizeMax = ncer->vramTransfer[i].size;
			}

			uint32_t vramTransferHeader[2];
			vramTransferHeader[0] = transferSizeMax;
			vramTransferHeader[1] = sizeof(vramTransferHeader); // offset to VRAM transfer info
			NnsStreamWrite(&nnsStream, vramTransferHeader, sizeof(vramTransferHeader));

			//each cell's transfer info
			for (int i = 0; i < ncer->nCells; i++) {
				uint32_t transfer[2];
				transfer[0] = ncer->vramTransfer[i].srcAddr;
				transfer[1] = ncer->vramTransfer[i].size;
				NnsStreamWrite(&nnsStream, transfer, sizeof(transfer));
			}
		}

		//write user extended attribute data
		if (ncer->useExtAttr) {
			NnsStreamAlign(&nnsStream, 4);

			unsigned char extHeader[8];
			*(uint16_t *) (extHeader + 0x0) = ncer->nCells;
			*(uint16_t *) (extHeader + 0x2) = 1; // 1 attribute
			*(uint32_t *) (extHeader + 0x4) = sizeof(extHeader);

			uint32_t blockHeader[2];
			memcpy(blockHeader, "TACU", 4); // UCAT block
			blockHeader[1] = sizeof(blockHeader) + sizeof(extHeader) + ncer->nCells * 8;

			NnsStreamWrite(&nnsStream, blockHeader, sizeof(blockHeader));
			NnsStreamWrite(&nnsStream, extHeader, sizeof(extHeader));
			for (int i = 0; i < ncer->nCells; i++) {
				uint32_t offs = sizeof(extHeader) + 4 * ncer->nCells + 4 * i;
				NnsStreamWrite(&nnsStream, &offs, sizeof(offs));
			}
			for (int i = 0; i < ncer->nCells; i++) {
				uint32_t attr = ncer->cells[i].attrEx;
				NnsStreamWrite(&nnsStream, &attr, sizeof(attr));
			}
		}

		NnsStreamEndBlock(&nnsStream);
	}

	//write LABL block
	if (ncer->labl != NULL) {
		NnsStreamStartBlock(&nnsStream, "LABL");
		NnsStreamWrite(&nnsStream, ncer->labl, ncer->lablSize);
		NnsStreamEndBlock(&nnsStream);
	}

	//write UEXT block
	if (ncer->uext != NULL) {
		NnsStreamStartBlock(&nnsStream, "UEXT");
		NnsStreamWrite(&nnsStream, ncer->uext, ncer->uextSize);
		NnsStreamEndBlock(&nnsStream);
	}

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

static int CellWriteHudson(NCER *ncer, BSTREAM *stream) {
	bstreamWrite(stream, &ncer->nCells, 4);
	int ofs = 4 * ncer->nCells;
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		int attrsSize = cell->nAttribs * 0xA + 2;
		bstreamWrite(stream, &ofs, 4);
		ofs += attrsSize;
	}

	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		bstreamWrite(stream, &cell->nAttribs, 2);
		for (int j = 0; j < cell->nAttribs; j++) {
			NCER_CELL_INFO info;
			CellDecodeOamAttributes(&info, cell, j);

			uint16_t pos[2];
			pos[0] = info.x;
			pos[1] = info.y;
			if (pos[0] & 0x100) {
				pos[0] |= 0xFE00;
			}
			if (pos[1] & 0x80) {
				pos[1] |= 0xFF00;
			}
			bstreamWrite(stream, cell->attr + j * 3, 6);
			bstreamWrite(stream, pos, 4);
		}
	}
	return 0;
}

static int CellWriteSetosa(NCER *ncer, BSTREAM *stream) {
	SetStream setStream;
	SetStreamCreate(&setStream);

	SetStreamStartBlock(&setStream, "CELL");

	//write header
	uint32_t header[2];
	header[0] = ncer->nCells;
	header[1] = ncer->mappingMode;
	SetStreamWrite(&setStream, header, sizeof(header));

	//build data directory
	SetResDirectory dir;
	SetResDirCreate(&dir, 0);
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;

		//get cell attributes
		int cellAffine = 0, commonPalette = 0;
		for (int j = 0; j < cell->nAttribs; j++) {
			NCER_CELL_INFO info;
			CellDecodeOamAttributes(&info, cell, j);

			if (info.rotateScale) cellAffine = 1;
			if (j == 0) commonPalette = info.palette;
			else if (commonPalette != info.palette) commonPalette = -1;
		}

		//create OBJ data
		unsigned char *cellData = calloc(0xC + 0x6 * cell->nAttribs, 1);
		*(int16_t *) (cellData + 0x0) = cell->minX;
		*(int16_t *) (cellData + 0x2) = cell->minY;
		*(int16_t *) (cellData + 0x4) = cell->maxX;
		*(int16_t *) (cellData + 0x6) = cell->maxY;
		*(uint16_t *) (cellData + 0x8) = cell->nAttribs;
		*(uint16_t *) (cellData + 0xA) = (commonPalette & 0xF) | ((commonPalette != -1) << 4) | (cellAffine << 5);
		memcpy(cellData + 0xC, cell->attr, cell->nAttribs * 0x6);

		SetResDirAdd(&dir, NULL, cellData, 0xC + 0x6 * cell->nAttribs);
		free(cellData);
	}

	SetResDirFinalize(&dir);
	SetResDirFlushOut(&dir, &setStream);
	SetResDirFree(&dir);

	SetStreamEndBlock(&setStream);

	SetStreamFinalize(&setStream);
	SetStreamFlushOut(&setStream, stream);
	SetStreamFree(&setStream);

	return 0;
}

int CellWrite(NCER *ncer, BSTREAM *stream) {
	switch (ncer->header.format) {
		case NCER_TYPE_NCER:
			return CellWriteNcer(ncer, stream);
		case NCER_TYPE_HUDSON:
			return CellWriteHudson(ncer, stream);
		case NCER_TYPE_SETOSA:
			return CellWriteSetosa(ncer, stream);
		case NCER_TYPE_COMBO:
			return combo2dWrite((COMBO2D *) ncer->header.combo, stream);
	}

	return OBJ_STATUS_UNSUPPORTED;
}

int CellWriteFile(NCER *ncer, LPWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER *) ncer, (OBJECT_WRITER) CellWrite);
}


void CellInsertOBJ(NCER_CELL *cell, int index, int nObj) {
	int nMove = cell->nAttribs - index;

	cell->nAttribs += nObj;
	cell->attr = realloc(cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));
	memmove(cell->attr + 3 * (index + nObj), cell->attr + 3 * index, nMove * 3 * sizeof(uint16_t));
	memset(cell->attr + 3 * index, 0, nObj * 3 * sizeof(uint16_t));

	if (cell->useEx2d) {
		cell->ex2dCharNames = realloc(cell->ex2dCharNames, cell->nAttribs * sizeof(uint32_t));
		memmove(cell->ex2dCharNames + index + nObj, cell->ex2dCharNames + index, nMove * sizeof(uint32_t));
		memset(cell->ex2dCharNames + index, 0, nObj * sizeof(uint32_t));
	}
}

void CellDeleteOBJ(NCER_CELL *cell, int index, int nObj) {
	int nMove = cell->nAttribs - (index + nObj);

	cell->nAttribs -= nObj;

	memmove(cell->attr + (index) * 3, cell->attr + (index + nObj) * 3, nMove * 3 * sizeof(uint16_t));
	cell->attr = realloc(cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));

	if (cell->useEx2d) {
		memmove(cell->ex2dCharNames + index, cell->ex2dCharNames + index + nObj, nMove * sizeof(uint32_t));
		cell->ex2dCharNames = realloc(cell->ex2dCharNames, cell->nAttribs * sizeof(uint32_t));
	}
}

static void CellGetCellBounds(NCER_CELL *cell, int *pxMin, int *pyMin, int *pxMax, int *pyMax) {
	int xMin = 0, yMin = 0, xMax = 0, yMax = 0;

	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);
		int objX = SEXT9(info.x), objY = SEXT8(info.y);
		int objW = info.width << info.doubleSize, objH = info.height << info.doubleSize;

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
		int objX = SEXT9(info.x), objY = SEXT8(info.y);
		int objW = info.width << info.doubleSize, objH = info.height << info.doubleSize;

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
		int objX = SEXT9(info.x) - xMin, objY = SEXT8(info.y) - yMin;

		if ((objX & 7) || (objY & 7)) return 0;
	}

	//check overlap
	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info1;
		CellDecodeOamAttributes(&info1, cell, i);
		int obj1X = SEXT9(info1.x), obj1Y = SEXT8(info1.y);
		int obj1W = info1.width << info1.doubleSize, obj1H = info1.height << info1.doubleSize;

		for (int j = i + 1; j < cell->nAttribs; j++) {
			NCER_CELL_INFO info2;
			CellDecodeOamAttributes(&info2, cell, j);

			int obj2X = SEXT9(info2.x), obj2Y = SEXT8(info2.y);
			int obj2W = info2.width << info2.doubleSize, obj2H = info2.height << info2.doubleSize;

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

				int charX = (SEXT9(objInfo.x) - xMin) / 8;
				int charY = (SEXT8(objInfo.y) - yMin) / 8;

				if (outChars != NULL) {
					//wrap graphics into 2D mapping
					unsigned int nObjCharsX = objInfo.width / 8;
					unsigned int nObjCharsY = objInfo.height / 8;
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
	int            allowFlip,
	int           *pFoundFlipX,
	int           *pFoundFlipY
) {
	//we allow the match to run off the end (partial match)
	unsigned int nCharsNeedle = nCharsXNeedle * nCharsYNeedle;
	for (unsigned int i = 0; i < nCharsBuf; i += granularity) {
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
	if (mappingGranularity == 0) mappingGranularity = 1;

	int status = 1; // OK

	unsigned char *tempbuf = (unsigned char *) calloc(64 * 64, 1);
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = &ncer->cells[i];

		//search start: beginning of file (cell mode: limit to within cell)
		unsigned int searchStart = 0;
		if (cellCompression) searchStart = curbufSize;

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
			int foundFlipX, foundFlipY;
			unsigned int foundAt = CellSearchGraphics(curbuf, curbufSize, tempbuf, nCharsX, nCharsY, mappingGranularity, !info.rotateScale, &foundFlipX, &foundFlipY);
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
			
			unsigned int chrName = (foundAt << (ncgr->nBits == 8)) >> mappingShift;
			if (chrName & ~0x03FF) {
				status = 0;
				curbufSize = 0;
				goto Done;
			}

			if (outChars != NULL) {
				cell->attr[3 * j + 2] = (cell->attr[3 * j + 2] & 0xFC00) | (chrName & 0x03FF);
				if (foundFlipX) cell->attr[3 * j + 1] |= 0x1000; // flip H
				if (foundFlipY) cell->attr[3 * j + 1] |= 0x2000; // flip V
			}
		}
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
		for (unsigned int i = 0; i < graphicsSize; i++) {
			outChars[i] = calloc(64, 1);
		}
		CellArrangeBankIn1D(ncer, ncgr, cellCompression, &graphicsSize, outChars, outAttr);

		//replace graphics data with rearranged graphics
		for (int i = 0; i < ncgr->nTiles; i++) {
			free(ncgr->tiles[i]);
		}
		if (ncgr->attr != NULL) free(ncgr->attr);
		free(ncgr->tiles);
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

		int graphicsWidth, graphicsHeight;
		CellArrangeBankIn2D(ncer, ncgr, &graphicsWidth, &graphicsHeight, NULL, NULL);

		//allocate new graphics
		unsigned char *outAttr = (unsigned char *) calloc(graphicsWidth * graphicsHeight, 1);
		unsigned char **outChars = (unsigned char **) calloc(graphicsWidth * graphicsHeight, sizeof(void *));
		for (int i = 0; i < graphicsWidth * graphicsHeight; i++) {
			outChars[i] = calloc(64, 1);
		}
		CellArrangeBankIn2D(ncer, ncgr, &graphicsWidth, &graphicsHeight, outChars, outAttr);

		//replace graphics data with rearranged graphics
		for (int i = 0; i < ncgr->nTiles; i++) {
			free(ncgr->tiles[i]);
		}
		if (ncgr->attr != NULL) free(ncgr->attr);
		free(ncgr->tiles);
		ncgr->tiles = outChars;
		ncgr->attr = outAttr;
		ncgr->tilesX = graphicsWidth;
		ncgr->tilesY = graphicsHeight;
		ncgr->nTiles = graphicsWidth * graphicsHeight;

	}
	return 1;
}

