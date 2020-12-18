#include "ncer.h"
#include "nclr.h"
#include "ncgr.h"

int ncerIsValidHudson(char *buffer, int size) {
	int nCells = *(int *) buffer;

	for (int i = 0; i < nCells; i++) {
		DWORD ofs = ((DWORD *) (buffer + 4))[i] + 4;
		if (ofs >= size) return 0;
		SHORT nOAM = *(SHORT *) (buffer + ofs);
		WORD *attrs = (WORD *) (buffer + ofs + 2);
		//attrs size: 0xA * nOAM
		DWORD endOfs = ofs + 2 + 0xA * nOAM;
		if (endOfs > size) return 0;
	}
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
	ncer->isHudson = 1;
	ncer->labl = NULL;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->uextSize = 0;
	ncer->compress = 0;
	ncer->nCells = nCells;
	ncer->bankAttribs = 0;
	
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
			if (x - info.width / 2 < minX) minX = x - info.width / 2;
			if (x + info.width / 2 > maxX) maxX = x + info.width / 2;
			if (y - info.height / 2 < minY) minY = y - info.height / 2;
			if (y + info.height / 2 > maxY) maxY = y + info.height / 2;
		}
		thisCell->maxX = maxX;
		thisCell->minX = minX;
		thisCell->maxY = maxY;
		thisCell->minY = minY;
	}
	return 0;
}

int ncerRead(NCER *ncer, char *buffer, int size) {
	if (size < 16) return 1;
	DWORD dwMagic = *(DWORD *) buffer;
	if (dwMagic != 0x5245434E && dwMagic != 0x4E434552) return ncerReadHudson(ncer, buffer, size);

	ncer->nCells = 0;
	ncer->compress = 0;
	ncer->isHudson = 0;
	ncer->uextSize = 0;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->labl = NULL;

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
int ncerReadFile(NCER *ncer, LPWSTR path) {
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

int decodeAttributesEx(NCER_CELL_INFO *info, NCER_CELL *cell, int oam) {
	WORD attr0 = cell->attr[oam * 3];
	WORD attr1 = cell->attr[oam * 3 + 1];
	WORD attr2 = cell->attr[oam * 3 + 2];

	info->x = attr1 & 0x1FF;
	info->y = attr0 & 0xFF;
	int shape = attr0 >> 14;
	int size = attr1 >> 14;

	int widths[3][4] = { {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32} };
	int heights[3][4] = { {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64} };

	info->width = widths[shape][size];
	info->height = heights[shape][size];

	info->characterName = attr2 & 0x3FF;
	info->priority = (attr2 >> 10) & 0x3;
	info->palette = (attr2 >> 12) & 0xF;

	int rotateScale = (attr0 >> 8) & 1;
	if (rotateScale) {
		info->flipX = 0;
		info->flipY = 0;
	} else {
		info->flipX = (attr1 >> 12) & 1;
		info->flipY = (attr1 >> 13) & 1;
	}

	int is8 = (attr0 >> 13) & 1;
	info->characterBits = 4;
	if (is8) {
		info->characterBits = 8;
		info->palette = 0;
	}

	return 0;
}

DWORD *ncerCellToBitmap(NCER_CELL_INFO *info, NCGR * ncgr, NCLR * nclr, int * width, int * height, int checker) {
	*width = info->width;
	*height = info->height;
	DWORD *bits = calloc(*width * *height, 4);

	int ncgrStart = info->characterName;

	int tilesX = *width / 8;
	int tilesY = *height / 8;

	if (ncgr != NULL) {
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				DWORD block[64];

				int bitsOffset = x * 8 + (y * 8 * tilesX * 8);
				if (ncgr->mapping == 0) {
					int startX = ncgrStart % ncgr->tilesX;
					int startY = ncgrStart / ncgr->tilesX;
					int ncx = x + startX;
					int ncy = y + startY;
					ncgrGetTile(ncgr, nclr, ncx, ncy, block, info->palette, checker);
				} else {
					int index = ncgrStart + x + y * tilesX;
					int ncx = index % ncgr->tilesX;
					int ncy = index / ncgr->tilesX;
					ncgrGetTile(ncgr, nclr, ncx, ncy, block, info->palette, checker);
				}
				for (int i = 0; i < 8; i++) {
					memcpy(bits + bitsOffset + tilesX * 8 * i, block + i * 8, 32);
				}
			}
		}
	}

	return bits;
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
	if (!ncer->isHudson) {
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
}