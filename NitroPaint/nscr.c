#include "nscr.h"
#include "palette.h"
#include "ncgr.h"
#include "nns.h"

#include <Windows.h>
#include <stdio.h>
#include <math.h>

LPCWSTR screenFormatNames[] = { L"Invalid", L"NSCR", L"NSC", L"ISC", L"ASC", L"Hudson", L"Hudson 2", L"Binary", NULL };

#define NSCR_FLIPNONE 0
#define NSCR_FLIPX 1
#define NSCR_FLIPY 2
#define NSCR_FLIPXY (NSCR_FLIPX|NSCR_FLIPY)

int ScrScreenSizeValid(int nPx) {
	if (nPx == 256 * 256 || nPx == 512 * 256 || 
		nPx == 512 * 512 || nPx == 128 * 128 || 
		nPx == 1024 * 1024 || nPx == 512 * 1024 ||
		nPx == 256 * 192) return 1;
	return 0;
}

static void ScriReadScreenDataAs(NSCR *nscr, const void *src, int srcSize, int fmt, int doSwizzle) {
	switch (fmt) {
		case SCREENFORMAT_TEXT:
		{
			//NITRO-CHARACTER creates NSC files without swizzling the order of the screen data. It is optional
			//thus. If no swizzling is used, then the process is the same as for affine extended screens.
			if (doSwizzle) {
				//text: read data in 256x256 pixel panels
				const uint16_t *src16 = (const uint16_t *) src;
				nscr->data = (uint16_t *) malloc(srcSize);
				nscr->dataSize = srcSize;

				//split data in panels.
				unsigned int nPnlX = (nscr->tilesX + 31) / 32;
				unsigned int nPnlY = (nscr->tilesY + 31) / 32;

				unsigned int nTiles = nscr->tilesX * nscr->tilesY;
				unsigned int curPnlX = 0, curPnlY = 0, curPnlTileX = 0, curPnlTileY = 0;
				for (unsigned int i = 0; i < nTiles; i++) {
					//data in
					uint16_t d = src16[i];

					//locate data out
					nscr->data[(curPnlX * 32 + curPnlTileX) + (curPnlY * 32 + curPnlTileY) * nscr->tilesX] = d;

					//increment destination
					curPnlTileX++;
					if (curPnlTileX >= 32 || (32 * curPnlX + curPnlTileX) >= nscr->tilesX) {
						curPnlTileX = 0;
						curPnlTileY++;
						if (curPnlTileY >= 32 || (32 * curPnlY + curPnlTileY) >= nscr->tilesY) {
							curPnlTileY = 0;
							curPnlX++;
							if (curPnlX >= nPnlX) {
								curPnlX = 0;
								curPnlY++;
							}
						}
					}
				}
				return;
			}

			//fall through
		}
		case SCREENFORMAT_AFFINEEXT:
		{
			//affine ext: read data direct
			nscr->data = (uint16_t *) malloc(srcSize);
			nscr->dataSize = srcSize;
			memcpy(nscr->data, src, srcSize);
			break;
		}
		case SCREENFORMAT_AFFINE:
		{
			//affine: read data direct
			const uint8_t *data = (const uint8_t *) src;
			nscr->dataSize = srcSize * 2;
			nscr->data = (uint16_t *) malloc(nscr->dataSize);

			for (int i = 0; i < srcSize; i++) {
				nscr->data[i] = data[i];
			}
			break;
		}
	}
}

static void ScriReadScreenData(NSCR *nscr, const void *src, int srcSize) {
	//take the file's data and translate it internally to text/affine ext format.
	ScriReadScreenDataAs(nscr, src, srcSize, nscr->fmt, 1);
}

static void ScriWriteScreenDataAs(NSCR *nscr, BSTREAM *stream, int fmt, int swizzle) {
	switch (fmt) {
		case SCREENFORMAT_TEXT:
		{
			//NITRO-CHARACTER does not swizzle.
			if (swizzle) {
				uint16_t *out = (uint16_t *) malloc(nscr->dataSize);

				//split data into panels.
				unsigned int nPnlX = (nscr->tilesX + 31) / 32;
				unsigned int nPnlY = (nscr->tilesY + 31) / 32;

				unsigned int outpos = 0;
				for (unsigned int pnlY = 0; pnlY < nPnlY; pnlY++) {
					for (unsigned int pnlX = 0; pnlX < nPnlX; pnlX++) {
						for (unsigned int y = 0; y < 32; y++) {
							for (unsigned int x = 0; x < 32; x++) {

								if ((pnlY * 32 + y) < nscr->tilesY && (pnlX * 32 + x) < nscr->tilesX) {
									out[outpos++] = nscr->data[(pnlX * 32 + x) + (pnlY * 32 + y) * nscr->tilesX];
								}

							}
						}
					}
				}

				bstreamWrite(stream, out, nscr->dataSize);
				free(out);
				return;
			}

			//fall through
		}
		case SCREENFORMAT_AFFINEEXT:
			bstreamWrite(stream, nscr->data, nscr->dataSize);
			break;
		case SCREENFORMAT_AFFINE:
		{
			unsigned char *data = (unsigned char *) malloc(nscr->dataSize / 2);
			for (unsigned int i = 0; i < nscr->dataSize / 2; i++) data[i] = nscr->data[i] & 0xFF;
			bstreamWrite(stream, data, nscr->dataSize / 2);
			free(data);
			break;
		}
	}
}

static void ScriWriteScreenData(NSCR *nscr, BSTREAM *stream) {
	ScriWriteScreenDataAs(nscr, stream, nscr->fmt, 1);
}

static int ScriGetDataSizeAs(NSCR *nscr, int fmt) {
	switch (fmt) {
		case SCREENFORMAT_TEXT:
		case SCREENFORMAT_AFFINEEXT:
			return nscr->dataSize;
		case SCREENFORMAT_AFFINE:
			return nscr->dataSize / 2;
	}
	return 0;
}

static int ScriGetDataSize(NSCR *nscr) {
	return ScriGetDataSizeAs(nscr, nscr->fmt);
}

int ScrComputeHighestCharacter(NSCR *nscr) {
	int highest = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t tile = nscr->data[i];
		int charNum = tile & 0x3FF;
		if (charNum > highest) highest = charNum;
	}
	nscr->nHighestIndex = highest;
	return highest;
}

int ScrIsValidHudson(const unsigned char *buffer, unsigned int size) {
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

int ScrIsValidBin(const unsigned char *buffer, unsigned int size) {
	if (size == 0 || (size & 1)) return 0;
	int nPx = (size >> 1) * 64;
	return ScrScreenSizeValid(nPx);
}

int ScrIsValidNsc(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NCSC", 4) != 0) return 0;

	unsigned char *scrn = NnsG2dFindBlockBySignature(buffer, size, "SCRN", NNS_SIG_BE, NULL);
	if (scrn == NULL) return 0;

	return 1;
}

int ScriIsCommonScanFooter(const unsigned char *buffer, unsigned int size, int type) {
	if (size < 8) return -1;

	//scan for possible locations of the footer
	for (unsigned int i = 0; i < size - 8; i++) {
		if (buffer[i] != 'C') continue;
		if (memcmp(buffer + i, "CLRF", 4) != 0) continue;

		//candidate location
		int hasClrf = 0, hasLink = 0, hasCmnt = 0, hasClrc = 0, hasMode = 0, hasVer = 0, hasEnd = 0, hasSize = 0;

		//scan sections
		unsigned int offset = i;
		while (1) {
			const char *section = buffer + offset;
			unsigned int length = *(unsigned int *) (buffer + offset + 4);
			offset += 8;

			if (memcmp(section, "CLRF", 4) == 0) hasClrf = 1;
			else if (memcmp(section, "LINK", 4) == 0) hasLink = 1;
			else if (memcmp(section, "CMNT", 4) == 0) hasCmnt = 1;
			else if (memcmp(section, "CLRC", 4) == 0) hasClrc = 1;
			else if (memcmp(section, "MODE", 4) == 0) hasMode = 1;
			else if (memcmp(section, "SIZE", 4) == 0) hasSize = 1;
			else if (memcmp(section, "VER ", 4) == 0) hasVer = 1;
			else if (memcmp(section, "END ", 4) == 0) hasEnd = 1;

			if (memcmp(section, "VER ", 4) == 0) {
				//ACG: ver = "IS-ACG0x" (1-3)
				//ICG: ver = "IS-ICG01"
				const char *ver = section + 8;
				if (type == NSCR_TYPE_AC && (length < 8 || memcmp(ver, "IS-ASC", 6))) return -1;
				if (type == NSCR_TYPE_IC && (length < 8 || memcmp(ver, "IS-ISC", 6))) return -1;
			}

			offset += length;
			if (offset >= size) break;
			if (hasEnd) break;
		}

		//ISC files have a SIZE section, but ASC files do not
		int sizeSatisfied = (type == NSCR_TYPE_IC && hasSize) || (type != NSCR_TYPE_IC);
		if (hasClrf && hasLink && hasCmnt && hasClrc && hasMode && sizeSatisfied && hasVer && hasEnd && offset <= size) {
			//candidate found
			return i;
		}
	}
	return -1;
}

int ScrIsValidAsc(const unsigned char *file, unsigned int size) {
	int footerOffset = ScriIsCommonScanFooter(file, size, NSCR_TYPE_AC);
	if (footerOffset == -1) return 0;
	return 1;
}

int ScrIsValidIsc(const unsigned char *file, unsigned int size) {
	int footerOffset = ScriIsCommonScanFooter(file, size, NSCR_TYPE_IC);
	if (footerOffset == -1) return 0;
	return 1;
}

int ScrIsValidNscr(const unsigned char *file, unsigned int size) {
	if (!NnsG2dIsValid(file, size)) return 0;
	if (memcmp(file, "RCSN", 4) != 0) return 0;

	const unsigned char *sChar = NnsG2dFindBlockBySignature(file, size, "SCRN", NNS_SIG_LE, NULL);
	if (sChar == NULL) return 0;

	return 1;
}

int ScrIdentify(const unsigned char *file, unsigned int size) {
	if (ScrIsValidNscr(file, size)) return NSCR_TYPE_NSCR;
	if (ScrIsValidNsc(file, size)) return NSCR_TYPE_NC;
	if (ScrIsValidIsc(file, size)) return NSCR_TYPE_IC;
	if (ScrIsValidAsc(file, size)) return NSCR_TYPE_AC;
	if (ScrIsValidHudson(file, size)) return NSCR_TYPE_HUDSON;
	if (ScrIsValidBin(file, size)) return NSCR_TYPE_BIN;
	return NSCR_TYPE_INVALID;
}

void ScrFree(OBJECT_HEADER *header) {
	NSCR *nscr = (NSCR *) header;
	if (nscr->data != NULL) free(nscr->data);
	nscr->data = NULL;

	COMBO2D *combo = (COMBO2D *) nscr->header.combo;
	if (combo != NULL) {
		combo2dUnlink(combo, &nscr->header);
	}
}

void ScrInit(NSCR *nscr, int format) {
	nscr->header.size = sizeof(NSCR);
	ObjInit((OBJECT_HEADER *) nscr, FILE_TYPE_SCREEN, format);
	nscr->header.dispose = ScrFree;
	nscr->header.writer = (OBJECT_WRITER) ScrWrite;
	nscr->header.combo = NULL;
}

static int ScrReadHudson(NSCR *nscr, const unsigned char *file, unsigned int dwFileSize) {
	int type = ScrIsValidHudson(file, dwFileSize);

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

	ScrInit(nscr, type);
	nscr->fmt = SCREENFORMAT_TEXT;
	nscr->colorMode = SCREENCOLORMODE_16x16;
	nscr->tilesX = tilesX;
	nscr->tilesY = tilesY;
	ScriReadScreenData(nscr, srcData, tilesX * tilesY * 2);
	ScrComputeHighestCharacter(nscr);
	return OBJ_STATUS_SUCCESS;
}

static int ScrReadBin(NSCR *nscr, const unsigned char *file, unsigned int dwFileSize) {
	ScrInit(nscr, NSCR_TYPE_BIN);
	nscr->fmt = SCREENFORMAT_TEXT;
	nscr->colorMode = SCREENCOLORMODE_16x16;

	//guess size
	switch ((dwFileSize >> 1) * 64) {
		case 256*192:
			nscr->tilesX = 256 / 8;
			nscr->tilesY = 192 / 8;
			break;
		case 256*256:
			nscr->tilesX = 256 / 8;
			nscr->tilesY = 256 / 8;
			break;
		case 512*512:
			nscr->tilesX = 512 / 8;
			nscr->tilesY = 512 / 8;
			break;
		case 1024*1024:
			nscr->tilesX = 1024 / 8;
			nscr->tilesY = 1024 / 8;
			break;
		case 128*128:
			nscr->tilesX = 128 / 8;
			nscr->tilesY = 128 / 8;
			break;
		case 1024*512:
			nscr->tilesX = 1024 / 8;
			nscr->tilesY = 512 / 8;
			break;
		case 512*256:
			nscr->tilesX = 256 / 8;
			nscr->tilesY = 512 / 8;
			break;
	}
	nscr->dataSize = nscr->tilesX * nscr->tilesY * 2;
	ScriReadScreenData(nscr, file, dwFileSize);
	ScrComputeHighestCharacter(nscr);
	return OBJ_STATUS_SUCCESS;
}

static int ScrReadNsc(NSCR *nscr, const unsigned char *file, unsigned int size) {
	ScrInit(nscr, NSCR_TYPE_NC);

	unsigned int scrnSize = 0, escrSize = 0, clrfSize = 0, clrcSize = 0, gridSize = 0, linkSize = 0, cmntSize = 0;
	const unsigned char *scrn = NnsG2dFindBlockBySignature(file, size, "SCRN", NNS_SIG_BE, &scrnSize);
	const unsigned char *escr = NnsG2dFindBlockBySignature(file, size, "ESCR", NNS_SIG_BE, &escrSize); //xxxxFFxxxxxxPPPPCCCCCCCCCCCCCCCC
	const unsigned char *clrf = NnsG2dFindBlockBySignature(file, size, "CLRF", NNS_SIG_BE, &clrfSize);
	const unsigned char *clrc = NnsG2dFindBlockBySignature(file, size, "CLRC", NNS_SIG_BE, &clrcSize);
	const unsigned char *grid = NnsG2dFindBlockBySignature(file, size, "GRID", NNS_SIG_BE, &gridSize);
	const unsigned char *link = NnsG2dFindBlockBySignature(file, size, "LINK", NNS_SIG_BE, &linkSize);
	const unsigned char *cmnt = NnsG2dFindBlockBySignature(file, size, "CMNT", NNS_SIG_BE, &cmntSize);

	int affine = *(uint32_t *) (scrn + 0x8);
	int colorMode = *(uint32_t *) (scrn + 0xC);
	nscr->colorMode = colorMode;
	nscr->fmt = affine ? (colorMode == SCREENCOLORMODE_256x16 ? SCREENFORMAT_AFFINEEXT : SCREENFORMAT_AFFINE) : SCREENFORMAT_TEXT;
	nscr->tilesX = *(uint32_t *) (scrn + 0x0);
	nscr->tilesY = *(uint32_t *) (scrn + 0x4);
	
	//NITRO-CHARACTER: do not swizzle BG screen data
	ScriReadScreenDataAs(nscr, scrn + 0x10, scrnSize - 0x10, nscr->fmt, 0);

	if (link != NULL) {
		nscr->header.fileLink = (char *) malloc(linkSize);
		memcpy(nscr->header.fileLink, link, linkSize);
	}
	if (cmnt != NULL) {
		nscr->header.comment = (char *) malloc(cmntSize);
		memcpy(nscr->header.comment, cmnt, cmntSize);
	}
	if (clrc != NULL) {
		nscr->clearValue = *(uint16_t *) (clrc + 0);
	}
	if (grid != NULL) {
		nscr->showGrid = *(uint32_t *) (grid + 0x0);
		nscr->gridWidth = *(uint16_t *) (grid + 0x4);
		nscr->gridHeight = *(uint16_t *) (grid + 0x6);
	}

	//calculate highest index
	ScrComputeHighestCharacter(nscr);
	return OBJ_STATUS_SUCCESS;
}

static int ScriIsCommonRead(NSCR *nscr, const unsigned char *file, unsigned int size, int type) {
	int footerOffset = ScriIsCommonScanFooter(file, size, type);

	int width = 0, height = 0, depth = 4;

	//process extra data
	unsigned int offset = (unsigned int) footerOffset;
	while (1) {
		const char *section = file + offset;
		unsigned int len = *(unsigned int *) (section + 4);
		const unsigned char *sectionData = section + 8;

		if (memcmp(section, "CLRC", 4) == 0) {
			//CLRC
			nscr->clearValue = *(uint16_t *) (sectionData + 0);
		} else if (memcmp(section, "MODE", 4) == 0) {
			//MODE
			if (type == NSCR_TYPE_AC) {
				width = sectionData[0];
				height = sectionData[1];
				nscr->fmt = sectionData[2] ? SCREENFORMAT_AFFINE : SCREENFORMAT_TEXT;
				nscr->colorMode = (nscr->fmt == SCREENFORMAT_TEXT) ? SCREENCOLORMODE_16x16 : SCREENCOLORMODE_256x1; //TODO
			} else if (type == NSCR_TYPE_IC) {
				//byte 0: 0 for text BG
				nscr->fmt = sectionData[0] ? SCREENFORMAT_AFFINE : SCREENFORMAT_TEXT;
				nscr->colorMode = (nscr->fmt == SCREENFORMAT_TEXT) ? SCREENCOLORMODE_16x16 : SCREENCOLORMODE_256x1; //TODO

				//byte 1: doesn't seem to be used
			}
		} else if (memcmp(section, "SIZE", 4) == 0) {
			//size only for ISC files
			width = *(uint16_t *) (sectionData + 0);
			height = *(uint16_t *) (sectionData + 2);
		} else if (memcmp(section, "LINK", 4) == 0) {
			//LINK
			if (len) {
				int linkLen = len - 1;
				nscr->header.fileLink = (char *) calloc(linkLen + 1, 1);
				memcpy(nscr->header.fileLink, sectionData, linkLen);
			}
		} else if (memcmp(section, "CMNT", 4) == 0) {
			//CMNT
			int cmntLen = sectionData[1];
			nscr->header.comment = (char *) calloc(cmntLen + 1, 1);
			memcpy(nscr->header.comment, sectionData + 2, cmntLen);
		}

		offset += len + 8;
		if (offset >= size) break;
	}

	ScrInit(nscr, type);
	nscr->tilesX = width;
	nscr->tilesY = height;
	nscr->gridWidth = 8;
	nscr->gridHeight = 8;
	nscr->showGrid = 0;
	ScriReadScreenData(nscr, file, width * height * sizeof(uint16_t));

	ScrComputeHighestCharacter(nscr);

	return OBJ_STATUS_SUCCESS;
}

static int ScrReadAsc(NSCR *nscr, const unsigned char *file, unsigned int size) {
	return ScriIsCommonRead(nscr, file, size, NSCR_TYPE_AC);
}

static int ScrReadIsc(NSCR *nscr, const unsigned char *file, unsigned int size) {
	return ScriIsCommonRead(nscr, file, size, NSCR_TYPE_IC);
}

static int ScrReadNscr(NSCR *nscr, const unsigned char *file, unsigned int size) {
	ScrInit(nscr, NSCR_TYPE_NSCR);

	unsigned int scrnSize = 0;
	const unsigned char *scrn = NnsG2dFindBlockBySignature(file, size, "SCRN", NNS_SIG_LE, &scrnSize);

	//NSCR data
	uint16_t nWidth = *(uint16_t *) (scrn + 0x0);
	uint16_t nHeight = *(uint16_t *) (scrn + 0x2);
	uint16_t colorMode = *(uint16_t *) (scrn + 0x4);
	uint16_t fmt = *(uint16_t *) (scrn + 0x6);
	uint32_t dwDataSize = *(uint32_t *) (scrn + 0x8);

	nscr->colorMode = colorMode;
	nscr->fmt = fmt;
	nscr->tilesX = nWidth / 8;
	nscr->tilesY = nHeight / 8;
	ScriReadScreenData(nscr, scrn + 0xC, dwDataSize);
	ScrComputeHighestCharacter(nscr);

	return OBJ_STATUS_SUCCESS;
}

int ScrRead(NSCR *nscr, const unsigned char *file, unsigned int dwFileSize) {
	int type = ScrIdentify(file, dwFileSize);
	switch (type) {
		case NSCR_TYPE_NSCR:
			return ScrReadNscr(nscr, file, dwFileSize);
		case NSCR_TYPE_NC:
			return ScrReadNsc(nscr, file, dwFileSize);
		case NSCR_TYPE_IC:
			return ScrReadIsc(nscr, file, dwFileSize);
		case NSCR_TYPE_AC:
			return ScrReadAsc(nscr, file, dwFileSize);
		case NSCR_TYPE_HUDSON:
			return ScrReadHudson(nscr, file, dwFileSize);
		case NSCR_TYPE_BIN:
			return ScrReadBin(nscr, file, dwFileSize);
	}
	return OBJ_STATUS_INVALID;
}

int ScrReadFile(NSCR *nscr, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) nscr, (OBJECT_READER) ScrRead);
}

int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int charBase, int x, int y, COLOR32 *out, int *tileNo, int transparent) {
	if (nscr == NULL || ncgr == NULL || nclr == NULL) {
		memset(out, 0, 64 * sizeof(COLOR32));
		return 0;
	}

	int nWidthTiles = nscr->tilesX;
	int nHeightTiles = nscr->tilesY;
	if (x >= nWidthTiles || y >= nHeightTiles) {
		memset(out, 0, 64 * sizeof(COLOR32));
		return 1;
	}

	uint16_t tileData = nscr->data[y * nWidthTiles + x];
	int tileNumber = tileData & 0x3FF;
	int transform = (tileData >> 10) & 0x3;
	int paletteNumber = (tileData >> 12) & 0xF;
	if (tileNo != NULL) *tileNo = tileNumber;
	
	//get palette and base character
	COLOR *palette = nclr->colors + (paletteNumber << ncgr->nBits);
	tileNumber -= charBase;
	
	if (tileNumber >= ncgr->nTiles || tileNumber < 0) { //? let's just paint a transparent square
		if (!transparent) {
			COLOR32 bg = ColorConvertFromDS(CREVERSE(nclr->colors[0])) | 0xFF000000;
			for (int i = 0; i < 64; i++) {
				out[i] = bg;
			}
		} else {
			memset(out, 0, 64 * 4);
		}
		return 0;
	}

	COLOR32 charbuf[64];
	uint8_t *ncgrTile = ncgr->tiles[tileNumber];
	for (int i = 0; i < 64; i++) {
		if (ncgrTile[i] || !transparent) {
			int colIndex = ncgrTile[i];
			COLOR c = 0;
			if (colIndex + (paletteNumber << ncgr->nBits) < nclr->nColors)
				c = palette[colIndex];
			if (colIndex == 0 && !transparent)
				c = nclr->colors[0];
			charbuf[i] = ColorConvertFromDS(CREVERSE(c)) | 0xFF000000;
		} else {
			charbuf[i] = 0;
		}
	}

	//copy out
	if (transform == 0) {
		//copy straight
		memcpy(out, charbuf, sizeof(charbuf));
	} else {
		//complement X and/or Y coordinates when copying
		int srcXor = 0;
		if (transform & TILE_FLIPX) srcXor ^= 7;
		if (transform & TILE_FLIPY) srcXor ^= 7 << 3;
		for (int i = 0; i < 64; i++) {
			int src = i ^ srcXor;
			out[i] = charbuf[src];
		}
	}
	return 0;

}

int ScrWriteNscr(NSCR *nscr, BSTREAM *stream) {
	unsigned char scrnHeader[] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 };

	*(uint16_t *) (scrnHeader + 0x0) = nscr->tilesX * 8;
	*(uint16_t *) (scrnHeader + 0x2) = nscr->tilesY * 8;
	*(uint16_t *) (scrnHeader + 0x4) = nscr->colorMode;
	*(uint16_t *) (scrnHeader + 0x6) = nscr->fmt;
	*(uint32_t *) (scrnHeader + 0x8) = ScriGetDataSize(nscr);

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NSCR", 1, 0, NNS_TYPE_G2D, NNS_SIG_LE);
	NnsStreamStartBlock(&nnsStream, "SCRN");
	NnsStreamWrite(&nnsStream, scrnHeader, sizeof(scrnHeader));
	ScriWriteScreenData(nscr, NnsStreamGetBlockStream(&nnsStream));
	NnsStreamEndBlock(&nnsStream);
	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

int ScrWriteNsc(NSCR *nscr, BSTREAM *stream) {
	unsigned char scrnHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char escrHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char clrfHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char clrcHeader[] = { 0, 0, 0, 0 };
	unsigned char gridHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	
	*(uint32_t *) (scrnHeader + 0x0) = nscr->tilesX;
	*(uint32_t *) (scrnHeader + 0x4) = nscr->tilesY;
	*(uint32_t *) (scrnHeader + 0x8) = (nscr->fmt == SCREENFORMAT_TEXT) ? 0 : 1;
	*(uint32_t *) (scrnHeader + 0xC) = nscr->colorMode;
	*(uint32_t *) (escrHeader + 0x0) = nscr->tilesX;
	*(uint32_t *) (escrHeader + 0x4) = nscr->tilesY;
	*(uint32_t *) (escrHeader + 0x8) = (nscr->clearValue & 0x3FF) | ((nscr->clearValue & 0xF000) << 4) | ((nscr->clearValue & 0x0C00) << 16);
	*(uint32_t *) (clrfHeader + 0x0) = nscr->tilesX;
	*(uint32_t *) (clrfHeader + 0x4) = nscr->tilesY;
	*(uint32_t *) (clrcHeader + 0x0) = nscr->clearValue;
	*(uint32_t *) (gridHeader + 0x0) = nscr->showGrid;
	*(uint16_t *) (gridHeader + 0x4) = nscr->gridWidth;
	*(uint16_t *) (gridHeader + 0x6) = nscr->gridHeight;

	int clrfSize = (nscr->tilesX * nscr->tilesY + 7) / 8;
	void *dummyClrf = calloc(clrfSize, 1);

	uint32_t *escr = (uint32_t *) calloc(nscr->tilesX * nscr->tilesY, 4);
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t t = nscr->data[i];
		int chr = t & 0x03FF;
		int pal = t & 0xF000;
		int hvf = t & 0x0C00;
		escr[i] = chr | (pal << 4) | (hvf << 16);
	}

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCSC", 1, 0, NNS_TYPE_G2D, NNS_SIG_BE);

	NnsStreamStartBlock(&nnsStream, "SCRN");
	NnsStreamWrite(&nnsStream, scrnHeader, sizeof(scrnHeader));
	ScriWriteScreenDataAs(nscr, NnsStreamGetBlockStream(&nnsStream), nscr->fmt, 0); //do not swizzle
	NnsStreamEndBlock(&nnsStream);

	NnsStreamStartBlock(&nnsStream, "ESCR");
	NnsStreamWrite(&nnsStream, escrHeader, sizeof(escrHeader));
	NnsStreamWrite(&nnsStream, escr, nscr->tilesX * nscr->tilesY * 4);
	NnsStreamEndBlock(&nnsStream);

	NnsStreamStartBlock(&nnsStream, "CLRF");
	NnsStreamWrite(&nnsStream, clrfHeader, sizeof(clrfHeader));
	NnsStreamWrite(&nnsStream, dummyClrf, clrfSize);
	NnsStreamEndBlock(&nnsStream);

	NnsStreamStartBlock(&nnsStream, "CLRC");
	NnsStreamWrite(&nnsStream, clrcHeader, sizeof(clrcHeader));
	NnsStreamEndBlock(&nnsStream);

	if (nscr->gridWidth != 0) {
		NnsStreamStartBlock(&nnsStream, "GRID");
		NnsStreamWrite(&nnsStream, gridHeader, sizeof(gridHeader));
		NnsStreamEndBlock(&nnsStream);
	}

	if (nscr->header.fileLink != NULL) {
		NnsStreamStartBlock(&nnsStream, "LINK");
		NnsStreamWrite(&nnsStream, nscr->header.fileLink, strlen(nscr->header.fileLink));
		NnsStreamEndBlock(&nnsStream);
	}

	if (nscr->header.comment != NULL) {
		NnsStreamStartBlock(&nnsStream, "CMNT");
		NnsStreamWrite(&nnsStream, nscr->header.comment, strlen(nscr->header.comment));
		NnsStreamEndBlock(&nnsStream);
	}

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);

	free(dummyClrf);
	free(escr);
	return 0;
}

static int ScriIsCommonWrite(NSCR *nscr, BSTREAM *stream) {
	bstreamWrite(stream, nscr->data, nscr->dataSize);

	unsigned char clrfFooter[] = { 'C', 'L', 'R', 'F', 0, 0, 0, 0 };
	unsigned char linkFooter[] = { 'L', 'I', 'N', 'K', 0, 0, 0, 0 };
	unsigned char cmntFooter[] = { 'C', 'M', 'N', 'T', 0, 0, 0, 0, 1, 0 };
	unsigned char clrcFooter[] = { 'C', 'L', 'R', 'C', 2, 0, 0, 0, 0, 0 };
	unsigned char modeFooter[] = { 'M', 'O', 'D', 'E', 4, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char sizeFooter[] = { 'S', 'I', 'Z', 'E', 4, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char verFooter[] = { 'V', 'E', 'R', ' ', 0, 0, 0, 0 };
	unsigned char endFooter[] = { 'E', 'N', 'D', ' ', 0, 0, 0, 0 };

	//expects null terminated for ISC/ASC LINK specifically (not the others for some reason)
	int linkLen = (nscr->header.fileLink == NULL) ? 0 : (strlen(nscr->header.fileLink) + 1);
	int commentLen = (nscr->header.comment == NULL) ? 0 : strlen(nscr->header.comment);

	char *ver = "";
	switch (nscr->header.format) {
		case NSCR_TYPE_AC:
			ver = "IS-ASC03"; break;
		case NSCR_TYPE_IC:
			ver = "IS-ISC01"; break;
	}

	*(uint32_t *) (clrfFooter + 0x4) = (nscr->dataSize + 15) / 16;
	*(uint32_t *) (linkFooter + 0x4) = linkLen;
	*(uint32_t *) (cmntFooter + 0x4) = commentLen + 2;
	*(uint16_t *) (clrcFooter + 0x8) = nscr->clearValue;
	*(uint16_t *) (sizeFooter + 0x8) = nscr->tilesX;
	*(uint16_t *) (sizeFooter + 0xA) = nscr->tilesY;
	cmntFooter[9] = commentLen;
	modeFooter[0x8] = nscr->tilesX;
	modeFooter[0x9] = nscr->tilesY;
	modeFooter[0xA] = (nscr->fmt == SCREENFORMAT_AFFINE) ? 1 : 0;
	modeFooter[0xB] = 2;
	*(uint32_t *) (verFooter + 0x4) = strlen(ver);

	bstreamWrite(stream, clrfFooter, sizeof(clrfFooter));
	for (unsigned int i = 0; i < (nscr->dataSize + 15) / 16; i++) {
		unsigned char f = 0x00; //not clear character
		bstreamWrite(stream, &f, sizeof(f));
	}
	bstreamWrite(stream, linkFooter, sizeof(linkFooter));
	bstreamWrite(stream, nscr->header.fileLink, linkLen);
	bstreamWrite(stream, cmntFooter, sizeof(cmntFooter));
	bstreamWrite(stream, nscr->header.comment, commentLen);
	bstreamWrite(stream, clrcFooter, sizeof(clrcFooter));
	if (nscr->header.format == NSCR_TYPE_AC) {
		//ASC: MODE footer contains the size
		bstreamWrite(stream, modeFooter, sizeof(modeFooter));
	} else if (nscr->header.format == NSCR_TYPE_IC) {
		//ISC: MODE footer does not contain the size, has separate SIZE footer
		unsigned char iscModeFooter[] = { 'M', 'O', 'D', 'E', 2, 0, 0, 0, 0, 0 };
		iscModeFooter[0x8] = (nscr->fmt == SCREENFORMAT_AFFINE) ? 1 : 0;
		bstreamWrite(stream, iscModeFooter, sizeof(iscModeFooter));
		bstreamWrite(stream, sizeFooter, sizeof(sizeFooter));
	}
	bstreamWrite(stream, verFooter, sizeof(verFooter));
	bstreamWrite(stream, ver, strlen(ver));
	bstreamWrite(stream, endFooter, sizeof(endFooter));

	return 0;
}

int ScrWriteAsc(NSCR *nscr, BSTREAM *stream) {
	return ScriIsCommonWrite(nscr, stream);
}

int ScrWriteIsc(NSCR *nscr, BSTREAM *stream) {
	return ScriIsCommonWrite(nscr, stream);
}

int ScrWriteHudson(NSCR *nscr, BSTREAM *stream) {
	int nTotalTiles = nscr->tilesX * nscr->tilesY;
	if (nscr->header.format == NSCR_TYPE_HUDSON) {
		unsigned char header[8] = { 0 };
		*(uint16_t *) (header + 1) = 2 * nTotalTiles + 4;
		*(uint16_t *) (header + 4) = 2 * nTotalTiles;
		header[6] = nscr->tilesX;
		header[7] = nscr->tilesY;
		bstreamWrite(stream, header, sizeof(header));
	} else if (nscr->header.format == NSCR_TYPE_HUDSON2) {
		unsigned char header[4] = { 0, 0, 0, 0 };
		*(uint16_t *) header = nTotalTiles * 2;
		header[2] = nscr->tilesX;
		header[3] = nscr->tilesY;
		bstreamWrite(stream, header, sizeof(header));
	}

	bstreamWrite(stream, nscr->data, 2 * nTotalTiles);
	return 0;
}

int ScrWriteBin(NSCR *nscr, BSTREAM *stream) {
	bstreamWrite(stream, nscr->data, nscr->dataSize);
	return 0;
}

int ScrWriteCombo(NSCR *nscr, BSTREAM *stream) {
	return combo2dWrite((COMBO2D *) nscr->header.combo, stream);
}

int ScrWrite(NSCR *nscr, BSTREAM *stream) {
	switch (nscr->header.format) {
		case NSCR_TYPE_NSCR:
			return ScrWriteNscr(nscr, stream);
		case NSCR_TYPE_NC:
			return ScrWriteNsc(nscr, stream);
		case NSCR_TYPE_AC:
			return ScrWriteAsc(nscr, stream);
		case NSCR_TYPE_IC:
			return ScrWriteIsc(nscr, stream);
		case NSCR_TYPE_HUDSON:
		case NSCR_TYPE_HUDSON2:
			return ScrWriteHudson(nscr, stream);
		case NSCR_TYPE_BIN:
			return ScrWriteBin(nscr, stream);
		case NSCR_TYPE_COMBO:
			return ScrWriteCombo(nscr, stream);
	}

	return 1;
}

int ScrWriteFile(NSCR *nscr, LPCWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER *) nscr, (OBJECT_WRITER) ScrWrite);
}
