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

	unsigned char *scrn = NnsG2dGetSectionByMagic(buffer, size, 'SCRN');
	if (scrn == NULL) scrn = NnsG2dGetSectionByMagic(buffer, size, 'NRCS');
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

	const unsigned char *sChar = NnsG2dGetSectionByMagic(file, size, 'SCRN');
	if (sChar == NULL) sChar = NnsG2dGetSectionByMagic(file, size, 'NRCS');
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

	COMBO2D *combo = nscr->combo2d;
	if (combo != NULL) {
		combo2dUnlink(combo, &nscr->header);
		if (combo->nLinks == 0) {
			combo2dFree(combo);
			free(combo);
		}
	}
	nscr->combo2d = NULL;
}

void ScrInit(NSCR *nscr, int format) {
	nscr->header.size = sizeof(NSCR);
	ObjInit((OBJECT_HEADER *) nscr, FILE_TYPE_SCREEN, format);
	nscr->header.dispose = ScrFree;
	nscr->combo2d = NULL;
}

int ScrReadHudson(NSCR *nscr, const unsigned char *file, unsigned int dwFileSize) {
	if (*file == 0x10) return 1; //TODO: implement LZ77 decompression
	if (dwFileSize < 8) return 1; //file too small
	//if (file[4] != 0) return 1; //not a screen file
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
	nscr->data = malloc(tilesX * tilesY * 2);
	nscr->nWidth = tilesX * 8;
	nscr->nHeight = tilesY * 8;
	nscr->dataSize = tilesX * tilesY * 2;
	nscr->nHighestIndex = 0;
	memcpy(nscr->data, srcData, nscr->dataSize);
	ScrComputeHighestCharacter(nscr);
	return 0;
}

int ScrReadBin(NSCR *nscr, const unsigned char *file, unsigned int dwFileSize) {
	ScrInit(nscr, NSCR_TYPE_BIN);
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

int ScrReadNsc(NSCR *nscr, const unsigned char *file, unsigned int size) {
	ScrInit(nscr, NSCR_TYPE_NC);

	unsigned char *scrn = NnsG2dGetSectionByMagic(file, size, 'SCRN');
	if (scrn == NULL) scrn = NnsG2dGetSectionByMagic(file, size, 'NRCS');
	unsigned char *escr = NnsG2dGetSectionByMagic(file, size, 'ESCR');
	if (escr == NULL) escr = NnsG2dGetSectionByMagic(file, size, 'RCSE'); //xxxxFFxxxxxxPPPPCCCCCCCCCCCCCCCC
	unsigned char *clrf = NnsG2dGetSectionByMagic(file, size, 'CLRF');
	if (clrf == NULL) clrf = NnsG2dGetSectionByMagic(file, size, 'FRLC');
	unsigned char *clrc = NnsG2dGetSectionByMagic(file, size, 'CLRC');
	if (clrc == NULL) clrc = NnsG2dGetSectionByMagic(file, size, 'CRLC');
	unsigned char *grid = NnsG2dGetSectionByMagic(file, size, 'GRID');
	if (grid == NULL) grid = NnsG2dGetSectionByMagic(file, size, 'DIRG');
	unsigned char *link = NnsG2dGetSectionByMagic(file, size, 'LINK');
	if (link == NULL) link = NnsG2dGetSectionByMagic(file, size, 'KNIL');
	unsigned char *cmnt = NnsG2dGetSectionByMagic(file, size, 'CMNT');
	if (cmnt == NULL) cmnt = NnsG2dGetSectionByMagic(file, size, 'TNMC');

	nscr->dataSize = *(uint32_t *) (scrn + 0x4) - 0x18;
	nscr->nWidth = *(uint32_t *) (scrn + 0x8) * 8;
	nscr->nHeight = *(uint32_t *) (scrn + 0xC) * 8;
	nscr->data = (uint16_t *) calloc(nscr->dataSize, 1);
	memcpy(nscr->data, scrn + 0x18, nscr->dataSize);

	if (link != NULL) {
		int len = *(uint32_t *) (link + 4) - 8;
		nscr->header.fileLink = (char *) calloc(len, 1);
		memcpy(nscr->header.fileLink, link + 8, len);
	}
	if (cmnt != NULL) {
		int len = *(uint32_t *) (cmnt + 4) - 8;
		nscr->header.comment = (char *) calloc(len, 1);
		memcpy(nscr->header.comment, cmnt + 8, len);
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
	ScrComputeHighestCharacter(nscr);
	return 0;
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
			} else if (type == NSCR_TYPE_IC) {
				//byte 0: 0 for text BG
				nscr->fmt = sectionData[0] ? SCREENFORMAT_AFFINE : SCREENFORMAT_TEXT;

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
	nscr->nWidth = width * 8;
	nscr->nHeight = height * 8;
	nscr->dataSize = width * height * sizeof(uint16_t);
	nscr->gridWidth = 8;
	nscr->gridHeight = 8;
	nscr->showGrid = 0;
	nscr->data = (uint16_t *) calloc(width * height, sizeof(uint16_t));
	memcpy(nscr->data, file, nscr->dataSize);

	ScrComputeHighestCharacter(nscr);

	return 0;
}

int ScrReadAsc(NSCR *nscr, const unsigned char *file, unsigned int size) {
	return ScriIsCommonRead(nscr, file, size, NSCR_TYPE_AC);
}

int ScrReadIsc(NSCR *nscr, const unsigned char *file, unsigned int size) {
	return ScriIsCommonRead(nscr, file, size, NSCR_TYPE_IC);
}

int ScrReadNscr(NSCR* nscr, const unsigned char *file, unsigned int dwFileSize) {
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

	ScrInit(nscr, NSCR_TYPE_NSCR);
	nscr->data = malloc(dwDataSize);
	nscr->nWidth = nWidth;
	nscr->nHeight = nHeight;
	nscr->dataSize = dwDataSize;
	nscr->nHighestIndex = 0;
	memcpy(nscr->data, file + 0x14, dwDataSize);
	ScrComputeHighestCharacter(nscr);

	return 0;
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
	return 1;
}

int ScrReadFile(NSCR *nscr, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) nscr, (OBJECT_READER) ScrRead);
}

int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int charBase, int x, int y, COLOR32 *out, int *tileNo, int transparent) {
	if (nscr == NULL || ncgr == NULL || nclr == NULL) {
		memset(out, 0, 64 * sizeof(COLOR32));
		return 0;
	}

	int nWidthTiles = nscr->nWidth >> 3;
	int nHeightTiles = nscr->nHeight >> 3;
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

int ScrWriteNsc(NSCR *nscr, BSTREAM *stream) {
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
	*(uint32_t *) (linkHeader + 0x04) = (nscr->header.fileLink == NULL) ? 0x0C : (((strlen(nscr->header.fileLink) + 4) & ~3) + sizeof(linkHeader));
	*(uint32_t *) (cmntHeader + 0x04) = (nscr->header.comment == NULL) ? 0x0C : (((strlen(nscr->header.comment) + 4) & ~3) + sizeof(cmntHeader));

	int scrnSize = nscr->dataSize + sizeof(scrnHeader);
	int escrSize = nscr->dataSize * 2 + sizeof(escrHeader);
	int clrfSize = nscr->nWidth * nscr->nHeight / 512 + sizeof(clrfHeader);
	int clrcSize = sizeof(clrcHeader);
	int gridSize = nscr->gridWidth == 0 ? 0 : sizeof(gridHeader);
	int linkSize = nscr->header.fileLink == NULL ? 0 : (((strlen(nscr->header.fileLink) + 4) & ~3) + sizeof(linkHeader));
	int cmntSize = nscr->header.comment == NULL ? 0 : (((strlen(nscr->header.comment) + 4) & ~3) + sizeof(cmntHeader));
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
		bstreamWrite(stream, nscr->header.fileLink, linkSize - sizeof(linkHeader));
	}
	if (cmntSize) {
		bstreamWrite(stream, cmntHeader, sizeof(cmntHeader));
		bstreamWrite(stream, nscr->header.comment, cmntSize - sizeof(cmntHeader));
	}
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
	*(uint16_t *) (sizeFooter + 0x8) = nscr->nWidth / 8;
	*(uint16_t *) (sizeFooter + 0xA) = nscr->nHeight / 8;
	cmntFooter[9] = commentLen;
	modeFooter[0x8] = nscr->nWidth / 8;
	modeFooter[0x9] = nscr->nHeight / 8;
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

int ScrWriteBin(NSCR *nscr, BSTREAM *stream) {
	bstreamWrite(stream, nscr->data, nscr->dataSize);
	return 0;
}

int ScrWriteCombo(NSCR *nscr, BSTREAM *stream) {
	return combo2dWrite(nscr->combo2d, stream);
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
