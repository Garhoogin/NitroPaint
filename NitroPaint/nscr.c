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

int nscrIsValidHudson(unsigned char *buffer, unsigned int size) {
	if (*buffer == 0x10) return 0;
	if (size < 4) return 0;
	int fileSize = 4 + *(uint16_t *) (buffer + 1);
	if (fileSize != size) {
		//might be format 2
		fileSize = 4 + *(uint16_t *) buffer;
		if (fileSize != size) return 0;
		int tilesX = buffer[2];
		int tilesY = buffer[3];
		if (tilesX * tilesY * 2 + 4 != fileSize) return 0;
		return NSCR_TYPE_HUDSON2;
	}
	int tilesX = buffer[6];
	int tilesY = buffer[7];
	if (!tilesX || !tilesY) return 0;
	if (tilesX * tilesY * 2 + 8 != fileSize) return 0;
	return NSCR_TYPE_HUDSON;
}

int nscrIsValidBin(unsigned char *buffer, unsigned int size) {
	if (size == 0 || (size & 1)) return 0;
	int nPx = (size >> 1) * 64;
	return isValidScreenSize(nPx);
}

int nscrIsValidNsc(unsigned char *buffer, unsigned int size) {
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

int hudsonScreenRead(NSCR *nscr, unsigned char *file, unsigned int dwFileSize) {
	if (*file == 0x10) return 1; //TODO: implement LZ77 decompression
	if (dwFileSize < 8) return 1; //file too small
	//if (file[4] != 0) return 1; //not a screen file
	int type = nscrIsValidHudson(file, dwFileSize);

	int tilesX = 0, tilesY = 0;
	uint16_t *srcData = NULL;

	if (type == NSCR_TYPE_HUDSON) {
		int fileSize = 4 + *(uint16_t *) (file + 1);
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

int nscrReadBin(NSCR *nscr, unsigned char *file, unsigned int dwFileSize) {
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
		case 256*192:
			nscr->nWidth = 256;
			nscr->nHeight = 192;
			break;
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

int nscrReadCombo(NSCR *nscr, unsigned char *file, unsigned int dwFileSize) {
	int type = combo2dIsValid(file, dwFileSize);
	nscrInit(nscr, NSCR_TYPE_COMBO);

	int width = 0, height = 0, dataSize = 0;
	switch (type) {
		case COMBO2D_TYPE_TIMEACE:
			height = 256;
			width = 256;
			dataSize = 2048;

			nscr->data = (uint16_t *) calloc(1024, 2);
			memcpy(nscr->data, file + 0x208, dataSize);
			break;
		case COMBO2D_TYPE_5BG:
		{
			char *bgdt = g2dGetSectionByMagic(file, dwFileSize, 'BGDT');
			if (bgdt == NULL) bgdt = g2dGetSectionByMagic(file, dwFileSize, 'TDGB');

			int scrX = *(uint16_t *) (bgdt + 0x10);
			int scrY = *(uint16_t *) (bgdt + 0x12);
			char *scr = bgdt + 0x1C;

			width = scrX * 8;
			height = scrY * 8;
			dataSize = scrX * scrY * 2;

			nscr->data = (uint16_t *) calloc(scrX * scrY, 2);
			memcpy(nscr->data, scr, dataSize);
			break;
		}
	}
	nscr->nWidth = width;
	nscr->nHeight = height;
	nscr->dataSize = dataSize;
	nscr->nHighestIndex = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}
	return 0;
}

int nscrReadNsc(NSCR *nscr, unsigned char *file, unsigned int size) {
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

int nscrRead(NSCR *nscr, unsigned char *file, unsigned int dwFileSize) {
	if (!dwFileSize) return 1;
	if (*(uint32_t *) file != 0x4E534352) {
		if (nscrIsValidNsc(file, dwFileSize)) return nscrReadNsc(nscr, file, dwFileSize);
		if (nscrIsValidHudson(file, dwFileSize)) return hudsonScreenRead(nscr, file, dwFileSize);
		if (nscrIsValidBin(file, dwFileSize)) return nscrReadBin(nscr, file, dwFileSize);
		if (combo2dIsValid(file, dwFileSize)) return nscrReadCombo(nscr, file, dwFileSize);
	}
	if (dwFileSize < 0x14) return 1;
	uint32_t dwFirst = *(uint32_t *) file;
	if (dwFirst != 0x5243534E && dwFirst != 0x4E534352) return 1;
	uint16_t endianness = *(uint16_t *) (file + 0x4);
	if (endianness != 0xFFFE && endianness != 0xFEFF) return 1;
	uint32_t dwSize = *(uint32_t *) (file + 0x8);
	if (dwSize < dwFileSize) return 1;
	uint16_t wHeaderSize = *(uint16_t *) (file + 0xC);
	if (wHeaderSize < 0x10) return 1;
	uint16_t nSections = *(uint16_t *) (file + 0xE);
	if (nSections == 0) return 1;
	file += wHeaderSize;

	//NSCR data
	uint32_t nrcsMagic = *(uint32_t *) (file);
	if (nrcsMagic != 0x5343524E) return 1;
	uint32_t dwSectionSize = *(uint32_t *) (file + 0x4);
	if (!dwSectionSize) return 1;
	uint16_t nWidth = *(uint16_t *) (file + 0x8);
	uint16_t nHeight = *(uint16_t *) (file + 0xA);
	nscr->fmt = *(int *) (file + 0xC);
	uint32_t dwDataSize = *(uint32_t *) (file + 0x10);
	//printf("%dx%d, %d bytes\n", nWidth, nHeight, dwDataSize);

	nscrInit(nscr, NSCR_TYPE_NSCR);
	nscr->data = malloc(dwDataSize);
	nscr->nWidth = nWidth;
	nscr->nHeight = nHeight;
	nscr->dataSize = dwDataSize;
	nscr->nHighestIndex = 0;
	memcpy(nscr->data, file + 0x14, dwDataSize);
	for (unsigned int i = 0; i < dwDataSize / 2; i++) {
		uint16_t w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}

	return 0;
}

int nscrReadFile(NSCR *nscr, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nscr, (OBJECT_READER) nscrRead);
}

COLOR32 *toBitmap(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int *width, int *height, int transparent) {
	*width = nscr->nWidth;
	*height = nscr->nHeight;
	COLOR32 *px = (COLOR32 *) calloc(*width * *height, 4);

	int tilesX = nscr->nWidth >> 3;
	int tilesY = nscr->nHeight >> 3;
	int tilesStored = nscr->dataSize >> 1;
	//printf("%dx%d, expected %d\n", tilesX, tilesY, tilesStored);

	COLOR32 block[64];
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

int nscrGetTile(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int x, int y, int checker, COLOR32 *out, int transparent) {
	return nscrGetTileEx(nscr, ncgr, nclr, 0, x, y, checker, out, NULL, transparent);
}

int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int x, int y, int checker, COLOR32 *out, int *tileNo, int transparent) {
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
					COLOR32 bg = ColorConvertFromDS(CREVERSE(nclr->colors[0])) | 0xFF000000;
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
			uint8_t *ncgrTile = ncgr->tiles[tileNumber];

			for (int i = 0; i < 64; i++) {
				if (ncgrTile[i] || !transparent) {
					int colIndex = ncgrTile[i];
					COLOR c = 0;
					if (colIndex + paletteSize * paletteNumber < nclr->nColors)
						c = palette[colIndex];
					if (colIndex == 0 && !transparent)
						c = nclr->colors[0];
					out[i] = ColorConvertFromDS(CREVERSE(c)) | 0xFF000000;
				} else {
					out[i] = 0;
				}
			}
			if (transform & TILE_FLIPX) charFlipX(out);
			if (transform & TILE_FLIPY) charFlipY(out);
			if (checker) {
				for (int i = 0; i < 64; i++) {
					COLOR32 px = out[i];
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
	unsigned char gridHeader[] = { 'G', 'R', 'I', 'D', 0x18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
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
	*(uint32_t *) (linkHeader + 0x04) = (nscr->link == NULL) ? 0x0C : (((strlen(nscr->link) + 4) & ~3) + sizeof(linkHeader));
	*(uint32_t *) (cmntHeader + 0x04) = (nscr->comment == NULL) ? 0x0C : (((strlen(nscr->comment) + 4) & ~3) + sizeof(cmntHeader));

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
		unsigned char header[8] = { 0 };
		*(uint16_t *) (header + 1) = 2 * nTotalTiles + 4;
		*(uint16_t *) (header + 4) = 2 * nTotalTiles;
		header[6] = nscr->nWidth / 8;
		header[7] = nscr->nHeight / 8;
		bstreamWrite(stream, header, sizeof(header));
	} else if (nscr->header.format == NSCR_TYPE_HUDSON2) {
		unsigned char header[4] = { 0, 0, 0, 0 };
		*(uint16_t *) header = nTotalTiles * 2;
		header[2] = nscr->nWidth / 8;
		header[3] = nscr->nHeight / 8;
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

int nscrWriteFile(NSCR *nscr, LPCWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) nscr, (OBJECT_WRITER) nscrWrite);
}

float tileDifferenceFlip(REDUCTION *reduction, BGTILE *t1, BGTILE *t2, unsigned char mode) {
	double err = 0.0;
	COLOR32 *px1 = t1->px;
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {

			int x2 = (mode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (mode & TILE_FLIPY) ? (7 - y) : y;

			int *yiq1 = &t1->pxYiq[x + y * 8][0];
			int *yiq2 = &t2->pxYiq[x2 + y2 * 8][0];
			double dy = reduction->lumaTable[yiq1[0]] - reduction->lumaTable[yiq2[0]];
			double di = yiq1[1] - yiq2[1];
			double dq = yiq1[2] - yiq2[2];
			double da = yiq1[3] - yiq2[3];
			err += yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq + 1600 * da * da;
		}
	}

	return (float) err;
}

float tileDifference(REDUCTION *reduction, BGTILE *t1, BGTILE *t2, unsigned char *flipMode) {
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

void bgAddTileToTotal(REDUCTION *reduction, int *pxBlock, BGTILE *tile) {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			COLOR32 col = tile->px[x + y * 8];
			
			int x2 = (tile->flipMode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (tile->flipMode & TILE_FLIPY) ? (7 - y) : y;
			int *dest = pxBlock + 4 * (x2 + y2 * 8);

			int yiq[4];
			rgbToYiq(col, yiq);
			dest[0] += (int) (16.0 * reduction->lumaTable[yiq[0]] + 0.5f);
			dest[1] += yiq[1];
			dest[2] += yiq[2];
			dest[3] += yiq[3];
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

	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, balance, colorBalance, 15, 0, 255);
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
				if(nTiles > nMaxChars) *progress = 500 + (int) (500 * sqrt((float) (nTiles - nChars) / (nTiles - nMaxChars)));
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
			int yiq[] = { cy, ci, cq, ca };
			int rgb[4];
			yiqToRgb(rgb, yiq);

			tile->px[j] = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16) | (ca << 24);
		}

		//try to determine the most optimal palette. Child tiles can be different palettes.
		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits) + paletteOffset + !paletteOffset;
			double err = computePaletteErrorYiq(reduction, tile->px, 64, pal, paletteSize - !paletteOffset, 128, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//now, match colors to indices.
		COLOR32 *pal = palette + (bestPalette << nBits);
		ditherImagePaletteEx(tile->px, NULL, 8, 8, pal + paletteOffset + !paletteOffset, 
			paletteSize - !paletteOffset, 0, 1, 0, 0.0f, balance, colorBalance, 0);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestPalette(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset)
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

	destroyReduction(reduction);
	free(reduction);
	return nChars;
}

void setupBgTiles(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse) {
	setupBgTilesEx(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset, dither, diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

void setupBgTilesEx(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors) {
	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, balance, colorBalance, 15, enhanceColors, paletteSize);

	if (!dither) diffuse = 0.0f;
	for (int i = 0; i < nTiles; i++) {
		BGTILE *tile = tiles + i;

		//create histogram for tile
		resetHistogram(reduction);
		computeHistogram(reduction, tile->px, 8, 8);
		flattenHistogram(reduction);

		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits);
			double err = computeHistogramPaletteError(reduction, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//match colors
		COLOR32 *pal = palette + (bestPalette << nBits);

		//do optional dithering (also matches colors at the same time)
		ditherImagePaletteEx(tile->px, NULL, 8, 8, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, FALSE, TRUE, FALSE, diffuse, balance, colorBalance, enhanceColors);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestPalette(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset) 
					+ !paletteOffset + paletteOffset;
			}
			
			tile->indices[j] = index;
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;

			//YIQ color
			rgbToYiq(col, &tile->pxYiq[j][0]);
		}

		tile->masterTile = i;
		tile->nRepresents = 1;
		tile->palette = bestPalette;
	}
	destroyReduction(reduction);
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
			createPaletteSlowEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize, balance, colorBalance, enhanceColors, 0);
		} else {
			createPaletteSlowEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset + 1, paletteSize - 1, balance, colorBalance, enhanceColors, 0);
			palette[(paletteBase << nBits) + paletteOffset] = color0; //transparent fill color
		}
	} else {
		createMultiplePalettesEx(imgBits, tilesX, tilesY, palette, paletteBase, nPalettes, 1 << nBits, paletteSize, paletteOffset, balance, colorBalance, enhanceColors, progress1);
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
	ncgr->tilesX = calculateWidth(ncgr->nTiles);
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