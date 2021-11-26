#include "ncer.h"
#include "nclr.h"
#include "ncgr.h"

LPCWSTR cellFormatNames[] = { "Invalid", L"NCER", L"Hudson", NULL };

int ncerIsValidHudson(char *buffer, unsigned int size) {
	unsigned int nCells = *(unsigned int *) buffer;
	if (nCells == 0) return 0;

	DWORD highestOffset = 4;
	for (unsigned int i = 0; i < nCells; i++) {
		DWORD ofs = ((DWORD *) (buffer + 4))[i] + 4;
		if (4 + i * 4 + 4 >= highestOffset) highestOffset = 8 + i * 4;
		if (ofs >= size) return 0;
		SHORT nOAM = *(SHORT *) (buffer + ofs);
		WORD *attrs = (WORD *) (buffer + ofs + 2);
		//attrs size: 0xA * nOAM
		DWORD endOfs = ofs + 2 + 0xA * nOAM;
		if (endOfs > highestOffset) highestOffset = endOfs;
		if (endOfs > size) return 0;
	}

	//if there is much unused data at the end, this is probably not an actual cell file!
	if (highestOffset < size) return 0;
	return 1;
}

int ncerIsValid(char *buffer, int size) {
	if (size <= 4) return 0;
	DWORD dwMagic = *(DWORD *) buffer;
	if (dwMagic != 0x5245434E && dwMagic != 0x4E434552) return ncerIsValidHudson(buffer, size);
	return 1;
}

int ncerReadHudson(NCER *ncer, char *buffer, int size) {
	int nCells = *(int *) buffer;
	ncer->labl = NULL;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->uextSize = 0;
	ncer->nCells = nCells;
	ncer->bankAttribs = 0;
	ncer->header.type = FILE_TYPE_CELL;
	ncer->header.format = NCER_TYPE_HUDSON;
	ncer->header.size = sizeof(*ncer);
	ncer->header.compression = COMPRESSION_NONE;
	
	NCER_CELL *cells = (NCER_CELL *) calloc(nCells, sizeof(NCER_CELL));
	ncer->cells = cells;
	for (int i = 0; i < nCells; i++) {
		DWORD ofs = ((DWORD *) (buffer + 4))[i] + 4;
		SHORT nOAM = *(SHORT *) (buffer + ofs);
		NCER_CELL *thisCell = cells + i;
		thisCell->nAttr = nOAM * 3;
		thisCell->nAttribs = nOAM;
		thisCell->attr = (WORD *) calloc(3 * nOAM, 2);
		thisCell->cellAttr = 0;
		int minX = 0x7FFF, maxX = -0x7FFF, minY = 0x7FFF, maxY = -0x7FFF;
		WORD *attrs = (WORD *) (buffer + ofs + 2);
		for (int j = 0; j < nOAM; j++) {
			memcpy(thisCell->attr + j * 3, attrs + j * 5, 6);
			NCER_CELL_INFO info;
			decodeAttributesEx(&info, thisCell, j);
			SHORT x = attrs[j * 5 + 3];
			SHORT y = attrs[j * 5 + 4];
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

int ncerRead(NCER *ncer, char *buffer, int size) {
	if (lz77IsCompressed(buffer, size)) {
		int uncompressedSize;
		char *bf = lz77decompress(buffer, size, &uncompressedSize);
		int r = ncerRead(ncer, bf, uncompressedSize);
		free(bf);
		ncer->header.compression = COMPRESSION_LZ77;
		return r;
	}
	if (size < 16) return 1;
	DWORD dwMagic = *(DWORD *) buffer;
	if (dwMagic != 0x5245434E && dwMagic != 0x4E434552) return ncerReadHudson(ncer, buffer, size);

	ncer->nCells = 0;
	ncer->uextSize = 0;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->labl = NULL;
	ncer->header.type = FILE_TYPE_CELL;
	ncer->header.format = NCER_TYPE_NCER;
	ncer->header.size = sizeof(*ncer);
	ncer->header.compression = COMPRESSION_NONE;

	char *end = buffer + size;
	buffer += 0x10;
	while (buffer + 8 < end) {
		dwMagic = *(DWORD *) buffer;
		DWORD dwSize = *(DWORD *) (buffer + 4) - 8;
		buffer += 8;

		switch (dwMagic) {
			case 0x4345424B: //CEBK
			{
				ncer->nCells = *(WORD *) buffer;
				ncer->cells = (NCER_CELL *) calloc(ncer->nCells, sizeof(NCER_CELL));
				ncer->bankAttribs = *(WORD *) (buffer + 2); //1 - with bounding rectangle, 0 - without
				char *cellData = buffer + *(DWORD *) (buffer + 4);
				
				DWORD mappingMode = *(DWORD *) (buffer + 8);
				char *vramTransferData = *(char **) (buffer + 12);
				if (vramTransferData) vramTransferData += (DWORD_PTR) buffer;

				int perCellDataSize = 8;
				if (ncer->bankAttribs == 1) perCellDataSize += 8;
				char *oamData = cellData + (ncer->nCells * perCellDataSize);

				for (int i = 0; i < ncer->nCells; i++) {
					WORD nOAMEntries = *(WORD *) cellData;
					WORD cellAttr = *(WORD *) (cellData + 2);
					DWORD pOamAttrs = *(DWORD *) (cellData + 4);

					NCER_CELL *cell = ncer->cells + i;
					cell->nAttribs = nOAMEntries;
					cell->cellAttr = cellAttr;

					WORD *cellOam = (WORD *) (oamData + pOamAttrs);
					//cell->attr0 = cellOam[0];
					//cell->attr1 = cellOam[1];
					//cell->attr2 = cellOam[2];
					cell->nAttr = nOAMEntries * 3;
					cell->attr = calloc(cell->nAttr, 2);
					memcpy(cell->attr, oamData + pOamAttrs, cell->nAttr * 2);
					//printf("n: %d (%X), size: %d (%X)\n", nOAMEntries, nOAMEntries, cell->nAttr * 2, cell->nAttr * 2);

					//cell->attr0 = cell->attr[0];
					//cell->attr1 = cell->attr[1];
					//cell->attr2 = cell->attr[2];

					if (perCellDataSize >= 16) {
						cell->maxX = *(SHORT *) (cellData + 8);
						cell->maxY = *(SHORT *) (cellData + 10);
						cell->minX = *(SHORT *) (cellData + 12);
						cell->minY = *(SHORT *) (cellData + 14);
					}

					cellData += perCellDataSize;
				}

				break;
			}
			case 0x4C41424C: //LABL
			{
				ncer->lablSize = dwSize;
				ncer->labl = calloc(dwSize, 1);
				memcpy(ncer->labl, buffer, dwSize);
				break;
			}
			case 0x55455854: //UEXT
			{

				ncer->uextSize = dwSize;
				ncer->uext = calloc(dwSize, 1);
				memcpy(ncer->uext, buffer, dwSize);
				break;
			}
		}

		buffer += dwSize;
	}

	return 0;
}
int ncerReadFile(NCER *ncer, LPCWSTR path) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);
	int n = ncerRead(ncer, lpBuffer, dwSize);
	free(lpBuffer);
	return n;
}

int decodeAttributes(NCER_CELL_INFO *info, NCER_CELL *cell) {
	return decodeAttributesEx(info, cell, 0);
}

void getObjSize(int shape, int size, int *width, int *height) {
	int widths[3][4] = { {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32} };
	int heights[3][4] = { {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64} };

	*width = widths[shape][size];
	*height = heights[shape][size];
}

int decodeAttributesEx(NCER_CELL_INFO *info, NCER_CELL *cell, int oam) {
	WORD attr0 = cell->attr[oam * 3];
	WORD attr1 = cell->attr[oam * 3 + 1];
	WORD attr2 = cell->attr[oam * 3 + 2];

	info->x = attr1 & 0x1FF;
	info->y = attr0 & 0xFF;
	int shape = attr0 >> 14;
	int size = attr1 >> 14;

	getObjSize(shape, size, &info->width, &info->height);
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
		info->palette = 0;
		info->characterName <<= 1;
	}

	return 0;
}

void ncerCellToBitmap2(NCER_CELL_INFO *info, NCGR *ncgr, NCLR *nclr, DWORD *out, int *width, int *height, int checker) {
	*width = info->width;
	*height = info->height;

	int tilesX = *width / 8;
	int tilesY = *height / 8;

	if (ncgr != NULL) {
		int ncgrStart = info->characterName * NCGR_BOUNDARY(ncgr);
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				DWORD block[64];

				int bitsOffset = x * 8 + (y * 8 * tilesX * 8);
				if (NCGR_2D(ncgr->mappingMode)) {
					int startX = ncgrStart % ncgr->tilesX;
					int startY = ncgrStart / ncgr->tilesX;
					int ncx = x + startX;
					int ncy = y + startY;
					ncgrGetTile(ncgr, nclr, ncx, ncy, block, info->palette, checker, TRUE);
				} else {
					int index = ncgrStart + x + y * tilesX;
					int ncx = index % ncgr->tilesX;
					int ncy = index / ncgr->tilesX;
					ncgrGetTile(ncgr, nclr, ncx, ncy, block, info->palette, checker, TRUE);
				}
				for (int i = 0; i < 8; i++) {
					memcpy(out + bitsOffset + tilesX * 8 * i, block + i * 8, 32);
				}
			}
		}
	}
}

DWORD *ncerCellToBitmap(NCER_CELL_INFO *info, NCGR *ncgr, NCLR *nclr, int *width, int *height, int checker) {
	*width = info->width;
	*height = info->height;
	DWORD *bits = calloc(*width * *height, 4);
	ncerCellToBitmap2(info, ncgr, nclr, bits, width, height, checker);
	return bits;
}

DWORD *ncerRenderWholeCell2(DWORD *px, NCER_CELL *cell, NCGR *ncgr, NCLR *nclr, int xOffs, int yOffs, int checker, int outline) {
	DWORD *block = (DWORD *) calloc(64 * 64, 4);
	for (int i = cell->nAttribs - 1; i >= 0; i--) {
		NCER_CELL_INFO info;
		int entryWidth, entryHeight;
		decodeAttributesEx(&info, cell, i);

		ncerCellToBitmap2(&info, ncgr, nclr, block, &entryWidth, &entryHeight, 0);

		//HV flip? Only if not affine!
		if (!info.rotateScale) {
			DWORD temp[64];
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
						DWORD left = block[i + j * info.width];
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
			for (int j = 0; j < info.height; j++) {
				int _y = (y + j + yOffs) & 0xFF;
				for (int k = 0; k < info.width; k++) {
					int _x = (x + k + xOffs) & 0x1FF;
					DWORD col = block[j * info.width + k];
					if (col >> 24) {
						px[_x + _y * 512] = block[j * info.width + k];
					}
				}
			}

			//outline
			if (outline == -2 || outline == i) {
				int outlineWidth = info.width << info.doubleSize;
				int outlineHeight = info.height << info.doubleSize;
				for (int j = 0; j < outlineWidth; j++) {
					int _x = (j + info.x + xOffs) & 0x1FF;
					int _y = (info.y + yOffs) & 0xFF;
					int _y2 = (_y + outlineHeight - 1) & 0xFF;
					px[_x + _y * 512] = 0xFE000000;
					px[_x + _y2 * 512] = 0xFE000000;
				}
				for (int j = 0; j < outlineHeight; j++) {
					int _x = (info.x + xOffs) & 0x1FF;
					int _y = (info.y + j + yOffs) & 0xFF;
					int _x2 = (_x + outlineWidth - 1) & 0x1FF;
					px[_x + _y * 512] = 0xFE000000;
					px[_x2 + _y * 512] = 0xFE000000;
				}
			}
		}
	}
	free(block);

	//apply checker background
	if (checker) {
		for (int y = 0; y < 256; y++) {
			for (int x = 0; x < 512; x++) {
				int index = y * 512 + x;
				if (px[index] >> 24 == 0) {
					int p = ((x >> 2) ^ (y >> 2)) & 1;
					if (p) {
						px[index] = 0xFFFFFFFF;
					} else {
						px[index] = 0xFFC0C0C0;
					}
				}
			}
		}
	}
	return px;
}

DWORD *ncerRenderWholeCell(NCER_CELL *cell, NCGR *ncgr, NCLR *nclr, int xOffs, int yOffs, int *width, int *height, int checker, int outline) {
	*width = 512, *height = 256;
	DWORD *px = (DWORD *) calloc(*width * *height, 4);

	ncerRenderWholeCell2(px, cell, ncgr, nclr, xOffs, yOffs, checker, outline);
	return px;
}

int ncerFree(NCER *ncer) {
	if (ncer->uext) free(ncer->uext);
	if (ncer->labl) free(ncer->labl);
	for (int i = 0; i < ncer->nCells; i++) {
		free(ncer->cells[i].attr);
	}
	if (ncer->cells) free(ncer->cells);
	
	return 0;
}

void ncerWrite(NCER * ncer, LPWSTR name) {
	HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (ncer->header.format == NCER_TYPE_NCER) {
		DWORD dwWritten;
		int hasLabl = ncer->labl != NULL;
		int hasUext = ncer->uext != NULL;
		int nSections = 1 + hasLabl + hasUext;

		int attr = ncer->bankAttribs;
		int cellSize = 8;
		if (attr == 1) cellSize = 16;

		//calculate data size of kbec
		int kbecSize = 32 + ncer->nCells * cellSize;
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = ncer->cells + i;
			kbecSize += 2 * cell->nAttr;
		}

		int fileSize = 16 + kbecSize;
		if (hasLabl) fileSize += ncer->lablSize + 8;
		if (hasUext) fileSize += ncer->uextSize + 8;

		BYTE ncerHeader[] = { 'R', 'E', 'C', 'N', 0xFF, 0xFE, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0, nSections, 0 };
		*(DWORD *) (ncerHeader + 8) = fileSize;
		WriteFile(hFile, ncerHeader, sizeof(ncerHeader), &dwWritten, NULL);
		//write the KBEC header
		BYTE kbecHeader[] = {'K', 'B', 'E', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0x18, 0, 0, 0
			, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		*(WORD *) (kbecHeader + 8) = ncer->nCells;
		*(WORD *) (kbecHeader + 10) = attr;
		*(DWORD *) (kbecHeader + 4) = kbecSize;

		WriteFile(hFile, kbecHeader, sizeof(kbecHeader), &dwWritten, NULL);

		//write out each cell. Keep track of the offsets of OAM data.
		int oamOffset = 0;
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = ncer->cells + i;
			BYTE data[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
			*(DWORD *) data = cell->nAttr / 3;
			*(DWORD *) (data + 4) = oamOffset;
			if (cellSize > 8) {
				*(SHORT *) (data + 8) = cell->maxX;
				*(SHORT *) (data + 10) = cell->maxY;
				*(SHORT *) (data + 12) = cell->minX;
				*(SHORT *) (data + 14) = cell->minY;
			}

			WriteFile(hFile, data, cellSize, &dwWritten, NULL);
			oamOffset += cell->nAttr * 2;
		}

		//write each cell's OAM
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = ncer->cells + i;
			WriteFile(hFile, cell->attr, cell->nAttr * 2, &dwWritten, NULL);
		}

		if (hasLabl) {
			BYTE lablHeader[] = {'L', 'B', 'A', 'L', 0, 0, 0, 0};
			*(DWORD *) (lablHeader + 4) = ncer->lablSize + 8;
			WriteFile(hFile, lablHeader, sizeof(lablHeader), &dwWritten, NULL);
			WriteFile(hFile, ncer->labl, ncer->lablSize, &dwWritten, NULL);
		}
		if (hasUext) {
			BYTE uextHeader[] = {'T', 'X', 'E', 'U', 0, 0, 0, 0};
			*(DWORD *) (uextHeader + 4) = ncer->uextSize + 8;
			WriteFile(hFile, uextHeader, sizeof(uextHeader), &dwWritten, NULL);
			WriteFile(hFile, ncer->uext, ncer->uextSize, &dwWritten, NULL);
		}

	} else {
		DWORD dwWritten;
		WriteFile(hFile, &ncer->nCells, 4, &dwWritten, NULL);
		int ofs = 4 * ncer->nCells;
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = ncer->cells + i;
			int attrsSize = cell->nAttribs * 0xA + 2;
			WriteFile(hFile, &ofs, 4, &dwWritten, NULL);
			ofs += attrsSize;
		}
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = ncer->cells + i;
			WriteFile(hFile, &cell->nAttribs, 2, &dwWritten, NULL);
			for (int j = 0; j < cell->nAttribs; j++) {
				NCER_CELL_INFO info;
				decodeAttributesEx(&info, cell, j);
				SHORT pos[2];
				pos[0] = info.x;
				pos[1] = info.y;
				if (pos[0] & 0x100) {
					pos[0] |= 0xFE00;
				}
				if (pos[1] & 0x80) {
					pos[1] |= 0xFF00;
				}
				WriteFile(hFile, cell->attr + j * 3, 6, &dwWritten, NULL);
				WriteFile(hFile, pos, 4, &dwWritten, NULL);
			}
		}
	}
	CloseHandle(hFile);
	if (ncer->header.compression != COMPRESSION_NONE) {
		fileCompress(name, ncer->header.compression);
	}
}