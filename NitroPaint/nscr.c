#include "nscr.h"
#include "palette.h"
#include "ncgr.h"
#include "g2dfile.h"

#include <Windows.h>
#include <stdio.h>
#include <math.h>

LPCWSTR screenFormatNames[] = { L"Invalid", L"NSCR", L"Hudson", L"Hudson 2", L"Binary", L"NSC", NULL };

#define NSCR_FLIPNONE 0
#define NSCR_FLIPX 1
#define NSCR_FLIPY 2
#define NSCR_FLIPXY (NSCR_FLIPX|NSCR_FLIPY)

int isValidScreenSize(int nPx) {
	if (nPx == 256 * 256 || nPx == 512 * 256 || 
		nPx == 512 * 512 || nPx == 128 * 128 || 
		nPx == 1024 * 1024 || nPx == 512 * 1024 ||
		nPx == 256 * 192) return 1;
	return 0;
}

int nscrIsValidHudson(LPBYTE buffer, int size) {
	if (*buffer == 0x10) return 0;
	if (size < 4) return 0;
	int fileSize = 4 + *(WORD *) (buffer + 1);
	if (fileSize != size) {
		//might be format 2
		fileSize = 4 + *(WORD *) buffer;
		if (fileSize != size) return 0;
		int tilesX = buffer[2];
		int tilesY = buffer[3];
		if (tilesX * tilesY * 2 + 4 != fileSize) return 0;
		return NSCR_TYPE_HUDSON2;
	}
	int tilesX = buffer[6];
	int tilesY = buffer[7];
	if (!tilesX || !tilesY) return 0;
	return NSCR_TYPE_HUDSON;
}

int nscrIsValidBin(LPBYTE buffer, int size) {
	if (size == 0 || (size & 1)) return 0;
	int nPx = (size >> 1) * 64;
	return isValidScreenSize(nPx);
}

int nscrIsValidNsc(LPBYTE buffer, int size) {
	if (!g2dIsValid(buffer, size)) return 0;

	unsigned char *scrn = g2dGetSectionByMagic(buffer, size, 'SCRN');
	if (scrn == NULL) scrn = g2dGetSectionByMagic(buffer, size, 'NRCS');
	if (scrn == NULL) return 0;

	return 1;
}

void nscrFree(OBJECT_HEADER *header) {
	NSCR *nscr = (NSCR *) header;
	if (nscr->data != NULL) free(nscr->data);
	nscr->data = NULL;

	if (nscr->link != NULL) {
		free(nscr->link);
		nscr->link = NULL;
	}
	if (nscr->comment != NULL) {
		free(nscr->comment);
		nscr->comment = NULL;
	}

	COMBO2D *combo = nscr->combo2d;
	if (nscr->combo2d != NULL) {
		nscr->combo2d->nscr = NULL;
		if (combo->nclr == NULL && combo->ncgr == NULL && combo->nscr == NULL) {
			combo2dFree(combo);
			free(combo);
		}
	}
	nscr->combo2d = NULL;
}

void nscrInit(NSCR *nscr, int format) {
	nscr->header.size = sizeof(NSCR);
	fileInitCommon((OBJECT_HEADER *) nscr, FILE_TYPE_SCREEN, format);
	nscr->header.dispose = nscrFree;
	nscr->combo2d = NULL;
}

int hudsonScreenRead(NSCR *nscr, char *file, DWORD dwFileSize) {
	if (*file == 0x10) return 1; //TODO: implement LZ77 decompression
	if (dwFileSize < 8) return 1; //file too small
	//if (file[4] != 0) return 1; //not a screen file
	int type = nscrIsValidHudson(file, dwFileSize);

	int tilesX = 0, tilesY = 0;
	uint16_t *srcData = NULL;

	if (type == NSCR_TYPE_HUDSON) {
		int fileSize = 4 + *(WORD *) (file + 1);
		tilesX = file[6];
		tilesY = file[7];
		srcData = (uint16_t *) (file + 8);
	} else if (type == NSCR_TYPE_HUDSON2) {
		tilesX = file[2];
		tilesY = file[3];
		srcData = (uint16_t *) (file + 4);
	}

	nscrInit(nscr, type);
	nscr->data = malloc(tilesX * tilesY * 2);
	nscr->nWidth = tilesX * 8;
	nscr->nHeight = tilesY * 8;
	nscr->dataSize = tilesX * tilesY * 2;
	nscr->nHighestIndex = 0;
	memcpy(nscr->data, srcData, nscr->dataSize);
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}
	return 0;
}

int nscrReadBin(NSCR *nscr, char *file, DWORD dwFileSize) {
	nscrInit(nscr, NSCR_TYPE_BIN);
	nscr->dataSize = dwFileSize;
	nscr->data = malloc(dwFileSize);
	nscr->combo2d = NULL;
	memcpy(nscr->data, file, dwFileSize);
	nscr->nHighestIndex = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}

	//guess size
	switch ((dwFileSize >> 1) * 64) {
		case 256*256:
			nscr->nWidth = 256;
			nscr->nHeight = 256;
			break;
		case 512*512:
			nscr->nWidth = 512;
			nscr->nHeight = 512;
			break;
		case 1024*1024:
			nscr->nWidth = 1024;
			nscr->nHeight = 1024;
			break;
		case 128*128:
			nscr->nWidth = 128;
			nscr->nHeight = 128;
			break;
		case 1024*512:
			nscr->nWidth = 1024;
			nscr->nHeight = 512;
			break;
		case 512*256:
			nscr->nWidth = 256;
			nscr->nHeight = 512;
			break;
	}
	return 0;
}

int nscrReadCombo(NSCR *nscr, char *file, DWORD dwFileSize) {
	nscrInit(nscr, NSCR_TYPE_COMBO);
	nscr->dataSize = 2048;
	nscr->nHeight = 256;
	nscr->nWidth = 256;
	nscr->data = (uint16_t *) calloc(1024, 2);
	memcpy(nscr->data, file + 0x208, 2048);

	nscr->nHighestIndex = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}
	return 0;
}

int nscrReadNsc(NSCR *nscr, char *file, int size) {
	nscrInit(nscr, NSCR_TYPE_NC);

	unsigned char *scrn = g2dGetSectionByMagic(file, size, 'SCRN');
	if (scrn == NULL) scrn = g2dGetSectionByMagic(file, size, 'NRCS');
	unsigned char *escr = g2dGetSectionByMagic(file, size, 'ESCR');
	if (escr == NULL) escr = g2dGetSectionByMagic(file, size, 'RCSE'); //xxxxFFxxxxxxPPPPCCCCCCCCCCCCCCCC
	unsigned char *clrf = g2dGetSectionByMagic(file, size, 'CLRF');
	if (clrf == NULL) clrf = g2dGetSectionByMagic(file, size, 'FRLC');
	unsigned char *clrc = g2dGetSectionByMagic(file, size, 'CLRC');
	if (clrc == NULL) clrc = g2dGetSectionByMagic(file, size, 'CRLC');
	unsigned char *grid = g2dGetSectionByMagic(file, size, 'GRID');
	if (grid == NULL) grid = g2dGetSectionByMagic(file, size, 'DIRG');
	unsigned char *link = g2dGetSectionByMagic(file, size, 'LINK');
	if (link == NULL) link = g2dGetSectionByMagic(file, size, 'KNIL');
	unsigned char *cmnt = g2dGetSectionByMagic(file, size, 'CMNT');
	if (cmnt == NULL) cmnt = g2dGetSectionByMagic(file, size, 'TNMC');

	nscr->dataSize = *(uint32_t *) (scrn + 0x4) - 0x18;
	nscr->nWidth = *(uint32_t *) (scrn + 0x8) * 8;
	nscr->nHeight = *(uint32_t *) (scrn + 0xC) * 8;
	nscr->data = (uint16_t *) calloc(nscr->dataSize, 1);
	memcpy(nscr->data, scrn + 0x18, nscr->dataSize);

	if (link != NULL) {
		int len = *(uint32_t *) (link + 4) - 8;
		nscr->link = (char *) calloc(len, 1);
		memcpy(nscr->link, link + 8, len);
	}
	if (cmnt != NULL) {
		int len = *(uint32_t *) (cmnt + 4) - 8;
		nscr->comment = (char *) calloc(len, 1);
		memcpy(nscr->comment, cmnt + 8, len);
	}
	if (clrc != NULL) {
		nscr->clearValue = *(uint16_t *) (clrc + 8);
	}
	if (grid != NULL) {
		nscr->showGrid = *(uint32_t *) (grid + 0x8);
		nscr->gridWidth = *(uint16_t *) (grid + 0xC);
		nscr->gridHeight = *(uint16_t *) (grid + 0xE);
	}

	//calculate highest index
	nscr->nHighestIndex = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}

	return 0;
}

int nscrRead(NSCR *nscr, char *file, DWORD dwFileSize) {
	if (!dwFileSize) return 1;
	if (*(DWORD *) file != 0x4E534352) {
		if (nscrIsValidNsc(file, dwFileSize)) return nscrReadNsc(nscr, file, dwFileSize);
		if (nscrIsValidHudson(file, dwFileSize)) return hudsonScreenRead(nscr, file, dwFileSize);
		if (nscrIsValidBin(file, dwFileSize)) return nscrReadBin(nscr, file, dwFileSize);
		if (combo2dIsValid(file, dwFileSize)) return nscrReadCombo(nscr, file, dwFileSize);
	}
	if (dwFileSize < 0x14) return 1;
	DWORD dwFirst = *(DWORD *) file;
	if (dwFirst != 0x5243534E && dwFirst != 0x4E534352) return 1;
	WORD endianness = *(WORD *) (file + 0x4);
	if (endianness != 0xFFFE && endianness != 0xFEFF) return 1;
	DWORD dwSize = *(DWORD *) (file + 0x8);
	if (dwSize < dwFileSize) return 1;
	WORD wHeaderSize = *(WORD *) (file + 0xC);
	if (wHeaderSize < 0x10) return 1;
	WORD nSections = *(WORD *) (file + 0xE);
	if (nSections == 0) return 1;
	file += wHeaderSize;

	//NSCR data
	DWORD nrcsMagic = *(DWORD *) (file);
	if (nrcsMagic != 0x5343524E) return 1;
	DWORD dwSectionSize = *(DWORD *) (file + 0x4);
	if (!dwSectionSize) return 1;
	WORD nWidth = *(WORD *) (file + 0x8);
	WORD nHeight = *(WORD *) (file + 0xA);
	nscr->fmt = *(int *) (file + 0xC);
	DWORD dwDataSize = *(DWORD *) (file + 0x10);
	//printf("%dx%d, %d bytes\n", nWidth, nHeight, dwDataSize);

	nscrInit(nscr, NSCR_TYPE_NSCR);
	nscr->data = malloc(dwDataSize);
	nscr->nWidth = nWidth;
	nscr->nHeight = nHeight;
	nscr->dataSize = dwDataSize;
	nscr->nHighestIndex = 0;
	memcpy(nscr->data, file + 0x14, dwDataSize);
	for (unsigned int i = 0; i < dwDataSize / 2; i++) {
		WORD w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}

	return 0;
}

int nscrReadFile(NSCR *nscr, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nscr, (OBJECT_READER) nscrRead);
}

DWORD *toBitmap(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int *width, int *height, BOOL transparent) {
	*width = nscr->nWidth;
	*height = nscr->nHeight;
	DWORD * px = (DWORD *) calloc(*width * *height, 4);

	int tilesX = nscr->nWidth >> 3;
	int tilesY = nscr->nHeight >> 3;
	int tilesStored = nscr->dataSize >> 1;
	//printf("%dx%d, expected %d\n", tilesX, tilesY, tilesStored);

	DWORD block[64];
	for (int x = 0; x < tilesX; x++) {
		for (int y = 0; y < tilesY; y++) {
			nscrGetTile(nscr, ncgr, nclr, x, y, TRUE, block, transparent);
			int destOffset = y * 8 * nscr->nWidth + x * 8;
			memcpy(px + destOffset, block, 32);
			memcpy(px + destOffset + nscr->nWidth, block + 8, 32);
			memcpy(px + destOffset + 2 * nscr->nWidth, block + 16, 32);
			memcpy(px + destOffset + 3 * nscr->nWidth, block + 24, 32);

			memcpy(px + destOffset + 4 * nscr->nWidth, block + 32, 32);
			memcpy(px + destOffset + 5 * nscr->nWidth, block + 40, 32);
			memcpy(px + destOffset + 6 * nscr->nWidth, block + 48, 32);
			memcpy(px + destOffset + 7 * nscr->nWidth, block + 56, 32);
		}
	}

	return px;
}

void charFlipX(COLOR32 *block) {
	//DWORD halfLine[4];
	for (int i = 0; i < 8; i++) {
		COLOR32 *line = block + i * 8;
		for (int j = 0; j < 4; j++) {
			COLOR32 p1 = line[j];
			line[j] = line[7 - j];
			line[7 - j] = p1;
		}
	}
}

void charFlipY(COLOR32 *block) {
	COLOR32 lineBuffer[8];
	for (int i = 0; i < 4; i++) {
		COLOR32 *line1 = block + i * 8;
		COLOR32 *line2 = block + (7 - i) * 8;
		memcpy(lineBuffer, line2, 32);
		memcpy(line2, line1, 32);
		memcpy(line1, lineBuffer, 32);
	}
}

int nscrGetTile(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int x, int y, BOOL checker, DWORD *out, BOOL transparent) {
	return nscrGetTileEx(nscr, ncgr, nclr, 0, x, y, checker, out, NULL, transparent);
}

int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int x, int y, BOOL checker, DWORD *out, int *tileNo, BOOL transparent) {
	if (x >= (int) (nscr->nWidth / 8)) return 1;
	if (y >= (int) (nscr->nHeight / 8)) return 1;
	int nWidthTiles = nscr->nWidth >> 3;
	int nHeightTiles = nscr->nHeight >> 3;
	int iTile = y * nWidthTiles + x;
	uint16_t tileData = nscr->data[iTile];

	int tileNumber = tileData & 0x3FF;
	int transform = (tileData >> 10) & 0x3;
	int paletteNumber = (tileData >> 12) & 0xF;
	if(tileNo != NULL) *tileNo = tileNumber;

	if (nclr) {
		int bitness = ncgr->nBits;
		int paletteSize = 16;
		if (bitness == 8) paletteSize = 256;
		COLOR *palette = nclr->colors + paletteSize * paletteNumber;
		int tileSize = 32;
		if (bitness == 8) tileSize = 64;
		tileNumber -= tileBase;
		if (ncgr) {
			if (tileNumber >= ncgr->nTiles || tileNumber < 0) { //? let's just paint a transparent square
				if (!transparent) {
					COLOR32 bg = ColorConvertFromDS(CREVERSE(palette[0])) | 0xFF000000;
					for (int i = 0; i < 64; i++) {
						out[i] = bg;
					}
				} else {
					if (!checker) FillMemory(out, 64 * 4, 0);
					else {
						for (int i = 0; i < 64; i++) {
							int c = ((i & 0x7) ^ (i >> 3)) >> 2;
							if (c) out[i] = 0xFFFFFFFF;
							else out[i] = 0xFFC0C0C0;
						}
					}
				}
				return 0;
			}
			BYTE *ncgrTile = ncgr->tiles[tileNumber];

			for (int i = 0; i < 64; i++) {
				if (ncgrTile[i] || !transparent) {
					COLOR c = palette[ncgrTile[i]];
					out[i] = ColorConvertFromDS(CREVERSE(c)) | 0xFF000000;
				} else {
					out[i] = 0;
				}
			}
			if (transform & TILE_FLIPX) charFlipX(out);
			if (transform & TILE_FLIPY) charFlipY(out);
			if (checker) {
				for (int i = 0; i < 64; i++) {
					DWORD px = out[i];
					if (!(px >> 24)) {
						int c = ((i & 0x7) ^ (i >> 3)) >> 2;
						if (c) out[i] = 0xFFFFFFFF;
						else out[i] = 0xFFC0C0C0;
					}
				}
			}
		}
	}
	return 0;

}

int nscrWriteNscr(NSCR *nscr, BSTREAM *stream) {
	unsigned char nscrHeader[] = { 'R', 'C', 'S', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
	unsigned char nrcsHeader[] = { 'N', 'R', 'C', 'S', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 };

	int dataSize = ((nscr->nWidth * nscr->nHeight) >> 6) << 1;
	int nrcsSize = dataSize + 0x14;
	int fileSize = nrcsSize + 0x10;

	*(uint32_t *) (nscrHeader + 0x8) = fileSize;
	*(uint32_t *) (nrcsHeader + 0x4) = nrcsSize;
	*(uint16_t *) (nrcsHeader + 0x8) = nscr->nWidth;
	*(uint16_t *) (nrcsHeader + 0xA) = nscr->nHeight;
	*(uint32_t *) (nrcsHeader + 0x10) = dataSize;
	*(uint32_t *) (nrcsHeader + 0xC) = nscr->fmt;

	bstreamWrite(stream, nscrHeader, sizeof(nscrHeader));
	bstreamWrite(stream, nrcsHeader, sizeof(nrcsHeader));
	bstreamWrite(stream, nscr->data, dataSize);
	return 0;
}

int nscrWriteNsc(NSCR *nscr, BSTREAM *stream) {
	unsigned char ncscHeader[] = { 'N', 'C', 'S', 'C', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
	unsigned char scrnHeader[] = { 'S', 'C', 'R', 'N', 0x18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char escrHeader[] = { 'E', 'S', 'C', 'R', 0x18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char clrfHeader[] = { 'C', 'L', 'R', 'F', 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char clrcHeader[] = { 'C', 'L', 'R', 'C', 0x0C, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char gridHeader[] = { 'G', 'R', 'I', 'D', 0x18, 0, 0, 0 };
	unsigned char linkHeader[] = { 'L', 'I', 'N', 'K', 0x08, 0, 0, 0 };
	unsigned char cmntHeader[] = { 'C', 'M', 'N', 'T', 0x08, 0, 0, 0 };

	*(uint32_t *) (scrnHeader + 0x04) = nscr->dataSize + sizeof(scrnHeader);
	*(uint32_t *) (scrnHeader + 0x08) = nscr->nWidth / 8;
	*(uint32_t *) (scrnHeader + 0x0C) = nscr->nHeight / 8;
	*(uint32_t *) (escrHeader + 0x04) = nscr->dataSize * 2 + sizeof(escrHeader);
	*(uint32_t *) (escrHeader + 0x08) = nscr->nWidth / 8;
	*(uint32_t *) (escrHeader + 0x0C) = nscr->nHeight / 8;
	*(uint32_t *) (escrHeader + 0x10) = (nscr->clearValue & 0x3FF) | ((nscr->clearValue & 0xF000) << 4) | ((nscr->clearValue & 0x0C00) << 16);
	*(uint32_t *) (clrfHeader + 0x04) = nscr->nWidth * nscr->nHeight / 512 + sizeof(clrfHeader);
	*(uint32_t *) (clrfHeader + 0x08) = nscr->nWidth / 8;
	*(uint32_t *) (clrfHeader + 0x0C) = nscr->nHeight / 8;
	*(uint32_t *) (clrcHeader + 0x08) = nscr->clearValue;
	*(uint32_t *) (gridHeader + 0x08) = nscr->showGrid;
	*(uint16_t *) (gridHeader + 0x0C) = nscr->gridWidth;
	*(uint16_t *) (gridHeader + 0x0E) = nscr->gridHeight;

	int scrnSize = nscr->dataSize + sizeof(scrnHeader);
	int escrSize = nscr->dataSize * 2 + sizeof(escrHeader);
	int clrfSize = nscr->nWidth * nscr->nHeight / 512 + sizeof(clrfHeader);
	int clrcSize = sizeof(clrcHeader);
	int gridSize = nscr->gridWidth == 0 ? 0 : sizeof(gridHeader);
	int linkSize = nscr->link == NULL ? 0 : (((strlen(nscr->link) + 4) & ~3) + sizeof(linkHeader));
	int cmntSize = nscr->comment == NULL ? 0 : (((strlen(nscr->comment) + 4) & ~3) + sizeof(cmntHeader));
	int totalSize = scrnSize + escrSize + clrfSize + clrcSize + gridSize + linkSize + cmntSize + sizeof(ncscHeader);
	*(uint32_t *) (ncscHeader + 0x08) = totalSize;
	*(uint16_t *) (ncscHeader + 0x0E) = !!scrnSize + !!escrSize + !!clrfSize + !!clrcSize + !!gridSize + !!linkSize + !!cmntSize;

	void *dummyClrf = calloc(clrfSize - sizeof(clrfHeader), 1);
	uint32_t *escr = (uint32_t *) calloc(nscr->nWidth * nscr->nHeight / 64, 4);
	bstreamWrite(stream, ncscHeader, sizeof(ncscHeader));
	bstreamWrite(stream, scrnHeader, sizeof(scrnHeader));
	bstreamWrite(stream, nscr->data, nscr->dataSize);
	bstreamWrite(stream, escrHeader, sizeof(escrHeader));
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t t = nscr->data[i];
		int chr = t & 0x03FF;
		int pal = t & 0xF000;
		int hvf = t & 0x0C00;
		escr[i] = chr | (pal << 4) | (hvf << 16);
	}
	bstreamWrite(stream, escr, nscr->nWidth * nscr->nHeight / 64 * 4);
	bstreamWrite(stream, clrfHeader, sizeof(clrfHeader));
	bstreamWrite(stream, dummyClrf, clrfSize - sizeof(clrfHeader));
	bstreamWrite(stream, clrcHeader, sizeof(clrcHeader));
	if (gridSize) {
		bstreamWrite(stream, gridHeader, sizeof(gridHeader));
	}
	if (linkSize) {
		bstreamWrite(stream, linkHeader, sizeof(linkHeader));
		bstreamWrite(stream, nscr->link, linkSize - sizeof(linkHeader));
	}
	if (cmntSize) {
		bstreamWrite(stream, cmntHeader, sizeof(cmntHeader));
		bstreamWrite(stream, nscr->comment, cmntSize - sizeof(cmntHeader));
	}
	free(dummyClrf);
	free(escr);
	return 0;
}

int nscrWriteHudson(NSCR *nscr, BSTREAM *stream) {
	int nTotalTiles = (nscr->nWidth * nscr->nHeight) >> 6;
	if (nscr->header.format == NSCR_TYPE_HUDSON) {
		BYTE header[8] = { 0 };
		*(WORD *) (header + 1) = 2 * nTotalTiles + 4;
		*(WORD *) (header + 4) = 2 * nTotalTiles;
		header[6] = (BYTE) (nscr->nWidth / 8);
		header[7] = (BYTE) (nscr->nHeight / 8);
		bstreamWrite(stream, header, sizeof(header));
	} else if (nscr->header.format == NSCR_TYPE_HUDSON2) {
		BYTE header[4] = { 0, 0, 0, 0 };
		*(WORD *) header = nTotalTiles * 2;
		header[2] = (BYTE) (nscr->nWidth / 8);
		header[3] = (BYTE) (nscr->nHeight / 8);
		bstreamWrite(stream, header, sizeof(header));
	}

	bstreamWrite(stream, nscr->data, 2 * nTotalTiles);
	return 0;
}

int nscrWriteBin(NSCR *nscr, BSTREAM *stream) {
	bstreamWrite(stream, nscr->data, nscr->dataSize);
	return 0;
}

int nscrWriteCombo(NSCR *nscr, BSTREAM *stream) {
	return combo2dWrite(nscr->combo2d, stream);
}

int nscrWrite(NSCR *nscr, BSTREAM *stream) {
	switch (nscr->header.format) {
		case NSCR_TYPE_NSCR:
			return nscrWriteNscr(nscr, stream);
		case NSCR_TYPE_NC:
			return nscrWriteNsc(nscr, stream);
		case NSCR_TYPE_HUDSON:
		case NSCR_TYPE_HUDSON2:
			return nscrWriteHudson(nscr, stream);
		case NSCR_TYPE_BIN:
			return nscrWriteBin(nscr, stream);
		case NSCR_TYPE_COMBO:
			return nscrWriteCombo(nscr, stream);
	}

	return 1;
}

int nscrWriteFile(NSCR *nscr, LPWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) nscr, (OBJECT_WRITER) nscrWrite);
}

int tileDifferenceFlip(BGTILE *t1, BGTILE *t2, BYTE mode) {
	int err = 0;
	COLOR32 *px1 = t1->px;
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {

			int x2 = (mode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (mode & TILE_FLIPY) ? (7 - y) : y;
			COLOR32 c1 = *(px1++);
			COLOR32 c2 = t2->px[x2 + y2 * 8];

			int dr = (c1 & 0xFF) - (c2 & 0xFF);
			int dg = ((c1 >> 8) & 0xFF) - ((c2 >> 8) & 0xFF);
			int db = ((c1 >> 16) & 0xFF) - ((c2 >> 16) & 0xFF);
			int da = ((c1 >> 24) & 0xFF) - ((c2 >> 24) & 0xFF);
			int dy, du, dv;
			convertRGBToYUV(dr, dg, db, &dy, &du, &dv);

			err += 4 * dy * dy + du * du + dv * dv + 16 * da * da;

		}
	}

	return err;
}

int tileDifference(BGTILE *t1, BGTILE *t2, BYTE *flipMode) {
	int err = tileDifferenceFlip(t1, t2, 0);
	if (err == 0) {
		*flipMode = 0;
		return err;
	}
	int err2 = tileDifferenceFlip(t1, t2, TILE_FLIPX);
	if (err2 == 0) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	int err3 = tileDifferenceFlip(t1, t2, TILE_FLIPY);
	if (err3 == 0) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	int err4 = tileDifferenceFlip(t1, t2, TILE_FLIPXY);
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

void bgAddTileToTotal(COLOR32 *pxBlock, BGTILE *tile) {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			COLOR32 col = tile->px[x + y * 8];
			
			int x2 = (tile->flipMode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (tile->flipMode & TILE_FLIPY) ? (7 - y) : y;
			COLOR32 *dest = pxBlock + 4 * (x2 + y2 * 8);

			dest[0] += col & 0xFF;
			dest[1] += (col >> 8) & 0xFF;
			dest[2] += (col >> 16) & 0xFF;
			dest[3] += (col >> 24) & 0xFF;
		}
	}
}

int performCharacterCompression(BGTILE *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int *progress) {
	int nChars = nTiles;
	int *diffBuff = (int *) calloc(nTiles * nTiles, sizeof(int));
	BYTE *flips = (BYTE *) calloc(nTiles * nTiles, 1); //how must each tile be manipulated to best match its partner

	for (int i = 0; i < nTiles; i++) {
		BGTILE *t1 = tiles + i;
		for (int j = 0; j < i; j++) {
			BGTILE *t2 = tiles + j;

			diffBuff[i + j * nTiles] = tileDifference(t1, t2, &flips[i + j * nTiles]);
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
				if(nTiles > nMaxChars) *progress = 500 + (int) (500 * sqrt((float) (nTiles - nChars) / (nTiles - nMaxChars)));
			}
		}
	}

	//still too many? 
	if (nChars > nMaxChars) {
		//damn

		//keep finding the most similar tile until we get character count down
		while (nChars > nMaxChars) {
			unsigned long long int leastError = 0x7FFFFFFF;
			int tile1 = 0, tile2 = 1;

			for (int i = 0; i < nTiles; i++) {
				BGTILE *t1 = tiles + i;
				if (t1->masterTile != i) continue;

				for (int j = 0; j < i; j++) {
					BGTILE *t2 = tiles + j;
					if (t2->masterTile != j) continue;
					int thisErrorEntry = diffBuff[i + j * nTiles];
					unsigned long long int thisError = thisErrorEntry;
					if (thisError >= leastError) continue;

					thisError = thisErrorEntry * t1->nRepresents * t2->nRepresents;
					if (thisError < leastError) {
						//if (nBits == 8 || ((t2->indices[0] >> 4) == (t1->indices[0] >> 4))) { //make sure they're the same palette
						tile1 = j;
						tile2 = i;
						leastError = thisError;
						//}
					}
				}
			}

			//should we swap tile1 and tile2? tile2 should have <= tile1's nRepresents
			if (tiles[tile2].nRepresents > tiles[tile1].nRepresents) {
				int t = tile1;
				tile1 = tile2;
				tile2 = t;
			}

			//merge tile1 and tile2. All tile2 tiles become tile1 tiles
			BYTE flipDiff = flips[tile1 + tile2 * nTiles];
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
		}
	}

	free(flips);
	free(diffBuff);

	//try to make the compressed result look less bad
	for (int i = 0; i < nTiles; i++) {
		if (tiles[i].masterTile != i) continue;
		if (tiles[i].nRepresents <= 1) continue; //no averaging required for just one tile
		BGTILE *tile = tiles + i;

		//average all tiles that use this master tile.
		DWORD pxBlock[64 * 4] = { 0 };
		int nRep = tile->nRepresents;
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			BGTILE *tile2 = tiles + j;
			bgAddTileToTotal(pxBlock, tile2);
		}
		for (int j = 0; j < 64 * 4; j++) {
			pxBlock[j] = (pxBlock[j] + (nRep >> 1)) / nRep;
		}
		for (int j = 0; j < 64; j++) {
			tile->px[j] = pxBlock[j * 4] | (pxBlock[j * 4 + 1] << 8) | (pxBlock[j * 4 + 2] << 16) | (pxBlock[j * 4 + 3] << 24);
		}

		//try to determine the most optimal palette. Child tiles can be different palettes.
		int bestPalette = paletteBase;
		int bestError = 0x7FFFFFFF;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			DWORD *pal = palette + (j << nBits);
			int err = getPaletteError(tile->px, 64, pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//now, match colors to indices.
		DWORD *pal = palette + (bestPalette << nBits);
		ditherImagePalette(tile->px, 8, 8, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, 0, 1, 0, 0.0f);
		for (int j = 0; j < 64; j++) {
			DWORD col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestPalette(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset)
					+ !paletteOffset + paletteOffset;
			}
			if (nBits == 4) {
				tile->indices[j] = (bestPalette << 4) | index;
			} else {
				tile->indices[j] = index;
			}
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;
		}

		//lastly, copy tile->indices to all child tile->indices, just to make sure palette and character are in synch.
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			if (j == i) continue;
			BGTILE *tile2 = tiles + j;

			memcpy(tile2->indices, tile->indices, 64);
		}
	}
	return nChars;
}

void setupBgTiles(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse) {
	setupBgTilesEx(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset, dither, diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

void setupBgTilesEx(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors) {
	if (!dither) diffuse = 0.0f;
	for (int i = 0; i < nTiles; i++) {
		BGTILE *tile = tiles + i;

		int bestPalette = paletteBase;
		int bestError = 0x7FFFFFFF;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			DWORD *pal = palette + (j << nBits);
			int err = getPaletteError(tile->px, 64, pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//match colors
		DWORD *pal = palette + (bestPalette << nBits);

		//do optional dithering (also matches colors at the same time)
		ditherImagePaletteEx(tile->px, 8, 8, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, FALSE, TRUE, FALSE, diffuse, balance, colorBalance, enhanceColors);
		for (int j = 0; j < 64; j++) {
			DWORD col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestPalette(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset) 
					+ !paletteOffset + paletteOffset;
			}
			if (nBits == 4) {
				tile->indices[j] = (bestPalette << 4) | index;
			} else {
				tile->indices[j] = index;
			}
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;
		}

		tile->masterTile = i;
		tile->nRepresents = 1;
	}
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
		convertRGBToYUV(dr, dg, db, &dy, &du, &dv);
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

	//use an octree to find the most different one
	float centerR = 127.5f, centerG = 127.5f, centerB = 127.5f;
	float size = 127.5f;
	for (int i = 0; i < 4; i++) {
		int mostError = 0, mostErrorCoordinate = 0;
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < 2; y++) {
				for (int z = 0; z < 2; z++) {
					int r = (int) (centerR + (2 * x - 1) * size + 0.5f);
					int g = (int) (centerG + (2 * x - 1) * size + 0.5f);
					int b = (int) (centerB + (2 * x - 1) * size + 0.5f);
					int err = findLeastDistanceToColor(px, width * height, r, g, b);
					if (err > mostError) {
						mostError = err;
						mostErrorCoordinate = x | (y << 1) | (z << 2);
					}
				}
			}
		}

		centerR -= size * 0.5f;
		centerG -= size * 0.5f;
		centerB -= size * 0.5f;
		if ((mostErrorCoordinate >> 0) & 1) centerR += size;
		if ((mostErrorCoordinate >> 1) & 1) centerG += size;
		if ((mostErrorCoordinate >> 1) & 1) centerB += size;

		size *= 0.5f;
	}

	int r = (int) (centerR + 0.5f);
	int g = (int) (centerG + 0.5f);
	int b = (int) (centerB + 0.5f);
	return r | (g << 8) | (b << 16);
}

void nscrCreate(DWORD *imgBits, int width, int height, int nBits, int dither, float diffuse,
				int paletteBase, int nPalettes, int fmt, int tileBase, int mergeTiles,
				int paletteSize, int paletteOffset, int rowLimit, int nMaxChars,
				int color0Mode, int balance, int colorBalance, int enhanceColors,
				int *progress1, int *progress1Max, int *progress2, int *progress2Max,
				NCLR *nclr, NCGR *ncgr, NSCR *nscr) {

	//cursory sanity checks
	if (nBits == 4) {
		if (paletteBase >= 16) paletteBase = 15;
		else if (paletteBase < 0) paletteBase = 0;
		if (nPalettes > 16) nPalettes = 16;
		else if (nPalettes < 1) nPalettes = 1;
		if (paletteBase + nPalettes > 16) nPalettes = 16 - paletteBase;

		if (paletteOffset < 0) paletteOffset = 0;
		else if (paletteOffset >= 16) paletteOffset = 15;
		if (paletteOffset + paletteSize > 16) paletteSize = 16 - paletteOffset;
	} else {
		paletteBase = 0;
		nPalettes = 1;

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

	COLOR32 *palette = (COLOR32 *) calloc(256, 4);
	COLOR32 color0 = chooseBGColor0(imgBits, width, height, color0Mode);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	if (nPalettes == 1) {
		if (paletteOffset) {
			createPaletteSlowEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize, balance, colorBalance, enhanceColors, 0);
		} else {
			createPaletteSlowEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset + 1, paletteSize - 1, balance, colorBalance, enhanceColors, 0);
			palette[(paletteBase << nBits) + paletteOffset] = color0; //transparent fill color
		}
	} else {
		createMultiplePalettesEx(imgBits, tilesX, tilesY, palette, paletteBase, nPalettes, 1 << nBits, paletteSize, paletteOffset, balance, colorBalance, enhanceColors, progress1);
		if (paletteOffset == 0) {
			for (int i = paletteBase; i < paletteBase + nPalettes; i++) palette[i * 16] = color0;
		}
	}
	*progress1 = nTiles * 2; //make sure it's done

	//by default the palette generator only enforces palette density, but not
	//the actual truncating of RGB values. Do that here. This will also be
	//important when fixed palettes are allowed.
	for (int i = 0; i < 256; i++) {
		palette[i] = ColorConvertFromDS(ColorConvertToDS(palette[i]));
	}

	//split image into 8x8 tiles.
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			DWORD *block = tiles[x + y * tilesX].px;

			memcpy(block, imgBits + srcOffset, 32);
			memcpy(block + 8, imgBits + srcOffset + width, 32);
			memcpy(block + 16, imgBits + srcOffset + width * 2, 32);
			memcpy(block + 24, imgBits + srcOffset + width * 3, 32);
			memcpy(block + 32, imgBits + srcOffset + width * 4, 32);
			memcpy(block + 40, imgBits + srcOffset + width * 5, 32);
			memcpy(block + 48, imgBits + srcOffset + width * 6, 32);
			memcpy(block + 56, imgBits + srcOffset + width * 7, 32);
		}
	}

	//match palettes to tiles
	setupBgTiles(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset, dither, diffuse);

	//match tiles to each other
	int nChars = nTiles;
	if (mergeTiles) {
		nChars = performCharacterCompression(tiles, nTiles, nBits, nMaxChars, palette, paletteSize, nPalettes, paletteBase, paletteOffset, progress2);
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
	WORD *indices = (WORD *) calloc(nTiles, 2);
	for (int i = 0; i < nTiles; i++) {
		indices[i] = tiles[i].masterTile + tileBase;
	}
	BYTE *modes = (BYTE *) calloc(nTiles, 1);
	for (int i = 0; i < nTiles; i++) {
		modes[i] = tiles[i].flipMode;
	}
	BYTE *paletteIndices = (BYTE *) calloc(nTiles, 1);
	if (nBits == 4) {
		for (int i = 0; i < nTiles; i++) {
			paletteIndices[i] = tiles[i].indices[0] >> 4;
		}
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

	nclrInit(nclr, paletteFormat);
	ncgrInit(ncgr, characterFormat);
	nscrInit(nscr, screenFormat);
	nclr->header.compression = compressPalette;
	ncgr->header.compression = compressCharacter;
	nscr->header.compression = compressScreen;

	nclr->nBits = nBits;
	nclr->nColors = rowLimit ? (nBits == 4 ? ((paletteBase + nPalettes) << 4) : (paletteOffset + paletteSize)) : 256;
	nclr->totalSize = nclr->nColors * 2;
	nclr->nPalettes = nBits == 8 ? 1 : (rowLimit ? (paletteBase + nPalettes) : (nclr->nColors / 16));
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	for (int i = 0; i < nclr->nColors; i++) {
		nclr->colors[i] = ColorConvertToDS(palette[i]);
	}

	ncgr->nBits = nBits;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->nTiles = nChars;
	ncgr->tileWidth = 8;
	ncgr->tilesX = calculateWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ncgr->tiles = (BYTE **) calloc(nChars, sizeof(BYTE *));
	int charSize = nBits == 4 ? 32 : 64;
	for (int j = 0; j < nChars; j++) {
		BYTE *b = (BYTE *) calloc(64, 1);
		for (int i = 0; i < 64; i++) {
			b[i] = (BYTE) blocks[i + j * 64];
		}
		ncgr->tiles[j] = b;
	}
	ncgr->attr = (unsigned char *) calloc(nTiles, 1);
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
	nscr->fmt = nBits == 4 ? SCREENCOLORMODE_16x16 : SCREENCOLORMODE_256x1;
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