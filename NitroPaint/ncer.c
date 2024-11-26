#include "ncer.h"
#include "nclr.h"
#include "ncgr.h"
#include "nns.h"
#include "setosa.h"

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
		thisCell->nAttribs = nOAM;
		thisCell->attr = (uint16_t *) calloc(3 * nOAM, 2);
		thisCell->cellAttr = 0;
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
			cell->nAttribs = nOAMEntries;
			cell->cellAttr = cellAttr;

			if (nOAMEntries != 0) {
				uint16_t *cellOam = (uint16_t *) (oamData + pOamAttrs);
				cell->attr = calloc(cell->nAttribs * 3, 2);
				memcpy(cell->attr, oamData + pOamAttrs, cell->nAttribs * 3 * 2);

				if (perCellDataSize >= 16) {
					cell->maxX = *(int16_t *) (cellData + 0x8);
					cell->maxY = *(int16_t *) (cellData + 0xA);
					cell->minX = *(int16_t *) (cellData + 0xC);
					cell->minY = *(int16_t *) (cellData + 0xE);
				}
			} else {
				// Provide at least one OAM attribute for empty cells
				cell->nAttribs = 1;
				cell->attr = calloc(3, 2);
				memset(cell->attr, 0, 3 * 2);
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

		cells[i].nAttribs = nObj;

		if (nObj != 0) {
			cells[i].attr = (uint16_t *) calloc(nObj, 3 * 2);
			memcpy(cells[i].attr, cell + 2, nObj * 3 * 2);
		} else {
			// Provide at least one OAM attribute for empty cells
			cells[i].nAttribs = 1;
			cells[i].attr = calloc(1, 3 * 2);
			memset(cells[i].attr, 0, 3 * 2);
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

void CellGetObjDimensions(int shape, int size, int *width, int *height) {
	int widths[3][4] = { {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32} };
	int heights[3][4] = { {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64} };

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

	info->characterName = attr2 & 0x3FF;
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
	}

	return OBJ_STATUS_UNSUPPORTED;
}

int CellWriteFile(NCER *ncer, LPWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER *) ncer, (OBJECT_WRITER) CellWrite);
}
