#include "ncgr.h"
#include "nclr.h"
#include "nscr.h"
#include "color.h"
#include "nns.h"
#include "setosa.h"

#include <stdio.h>

LPCWSTR characterFormatNames[] = { L"Invalid", L"NCGR", L"NCG", L"ICG", L"ACG", L"Tose", L"Hudson", L"Hudson 2", L"Ghost Trick", L"Setosa", L"Binary", NULL };

//Setosa file flags
#define RES_CHAR_FLAG_1D        (1<<0)  // 1D mapping flag
#define RES_CHAR_FLAG_8         (1<<1)  // 8bpp flag
#define RES_CHAR_FLAG_BMP       (1<<2)  // bitmap flag
#define RES_CHAR_FLAG_IMD       (1<<3)  // intermediate flag
#define RES_CHAR_MAP_SHIFT_MASK (3<<4)  // mapping mode shift mask (for 1D mapping)
#define RES_CHAR_MAP_SHIFT_SHIFT     4  // mapping mode shift shift (for 1D mapping ^)
#define RES_CHAR_NOWIDTH        (1<<6)  // flag to indicate no specific width

int ChrGuessWidth(int nTiles) {
	int width = 1;

	//if tile count is a multiple of 32, use it
	if (nTiles % 32 == 0) {
		return 32;
	}

	//iterate factors
	for (int i = 1; i < nTiles; i++) {
		if (i * i > nTiles) break;
		if (nTiles % i == 0) width = i;
	}
	int height = nTiles / width;
	if (width > height) return width; //prioritize wide over tall output
	return height;
}

int ChrIsValidHudson(const unsigned char *buffer, unsigned int size) {
	if (size < 8) return 0;
	if (((*buffer) & 0xF0) != 0) return 0;
	int dataLength = *(uint16_t *) (buffer + 1);
	if (buffer[3] != 0) return 0;
	if (dataLength * 32 + 4 == size || dataLength * 64 + 4 == size) {
		//no second header
		return NCGR_TYPE_HUDSON2;
	}
	if (buffer[4] != 1 && buffer[4] != 0) return 0;
	dataLength -= 4;

	if (dataLength + 8 != size) return 0;
	return NCGR_TYPE_HUDSON;
}

int ChrIsValidBin(const unsigned char *buffer, unsigned int size) {
	if (size & 0x1F) return 0;
	return NCGR_TYPE_BIN;
}

int ChrIsValidGhostTrick(const unsigned char *buffer, unsigned int size) {
	//size must be at least 4
	if (size < 4) return 0;

	//made up of 1 or more LZX chunk
	const unsigned char *end = buffer + size;
	const unsigned char *pos = buffer;

	while (pos < end) {
		//advance LZ. If failure, invalid
		pos = CxAdvanceLZX(pos, end - pos);
		if (pos == NULL) return 0;

		unsigned int curpos = pos - buffer;

		//advance 0 bytes until we reach another chunk
		int nSkipped = 0;
		while (pos < end && *pos == '\0') {
			pos++;
			nSkipped++;
			if (nSkipped >= 8) return 0;
		}

		//if not at the end, check for a 0x11 byte.
		if (pos < end && *pos != 0x11) return 0;
	}
	return 1;
}

int ChrIsValidNcg(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NCCG", 4) != 0) return 0;

	const unsigned char *sChar = NnsG2dFindBlockBySignature(buffer, size, "CHAR", NNS_SIG_BE, NULL);
	if (sChar == NULL) return 0;

	return 1;
}

static int ChriIsCommonScanFooter(const unsigned char *buffer, unsigned int size, int type) {
	//scan for footer with these required blocks
	const char *const requiredBlocks[] = { "LINK", "CMNT", "MODE", "SIZE", "VER ", "END " };
	int offs = IscadScanFooter(buffer, size, requiredBlocks, sizeof(requiredBlocks) / sizeof(requiredBlocks[0]));
	if (offs == -1) return -1;

	unsigned int footerSize = size - (unsigned int) offs;
	const unsigned char *footer = buffer + offs;

	//check blocks
	unsigned int verSize;
	const unsigned char *verBlock = IscadFindBlockBySignature(footer, footerSize, "VER ", &verSize);

	if (type == NCGR_TYPE_AC && (verSize < 8 || memcmp(verBlock, "IS-ACG", 6))) return -1; // ASC must have version IS-ACGxx
	if (type == NCGR_TYPE_IC && (verSize < 8 || memcmp(verBlock, "IS-ICG", 6))) return -1; // ISC must have version IS-ICGxx
	return offs;
}

int ChrIsValidAcg(const unsigned char *buffer, unsigned int size) {
	int dataOffset = ChriIsCommonScanFooter(buffer, size, NCGR_TYPE_AC);
	if (dataOffset == -1) return 0;

	return 1;
}

int ChrIsValidIcg(const unsigned char *buffer, unsigned int size) {
	int dataOffset = ChriIsCommonScanFooter(buffer, size, NCGR_TYPE_IC);
	if (dataOffset == -1) return 0;

	return 1;
}

int ChrIsValidNcgr(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "RGCN", 4) != 0) return 0;

	//find CHAR section
	const unsigned char *sChar = NnsG2dFindBlockBySignature(buffer, size, "CHAR", NNS_SIG_LE, NULL);
	if (sChar == NULL) return 0;

	return 1;
}

int ChrIsValidSetosa(const unsigned char *buffer, unsigned int size) {
	if (!SetIsValid(buffer, size)) return 0;

	//must have CHAR block
	const unsigned char *pChar = SetGetBlock(buffer, size, "CHAR");
	if (pChar == NULL) return 0;

	return 1;
}

int ChrIsValidTose(const unsigned char *buffer, unsigned int size) {
	if (size < 8) return 0;                              // size of file header
	if (memcmp(buffer + 0x0, "NCG\0", 4) != 0) return 0; // file signature

	unsigned int nChr = *(const uint16_t *) (buffer + 0x4);
	unsigned int gfxSize = size - 8;

	if (gfxSize != nChr * 0x20 && gfxSize != nChr * 0x40) return 0;

	return 1;
}

int ChrIdentify(const unsigned char *buffer, unsigned int size) {
	if (ChrIsValidNcgr(buffer, size)) return NCGR_TYPE_NCGR;
	if (ChrIsValidSetosa(buffer, size)) return NCGR_TYPE_SETOSA;
	if (ChrIsValidNcg(buffer, size)) return NCGR_TYPE_NC;
	if (ChrIsValidTose(buffer, size)) return NCGR_TYPE_TOSE;
	if (ChrIsValidIcg(buffer, size)) return NCGR_TYPE_IC;
	if (ChrIsValidAcg(buffer, size)) return NCGR_TYPE_AC;
	if (ChrIsValidHudson(buffer, size)) return NCGR_TYPE_HUDSON;
	if (ChrIsValidGhostTrick(buffer, size)) return NCGR_TYPE_GHOSTTRICK;
	if (ChrIsValidBin(buffer, size)) return NCGR_TYPE_BIN;
	return NCGR_TYPE_INVALID;
}

void ChrFree(OBJECT_HEADER *header) {
	NCGR *ncgr = (NCGR *) header;
	if (ncgr->tiles != NULL) {
		for (int i = 0; i < ncgr->nTiles; i++) {
			free(ncgr->tiles[i]);
		}
		free(ncgr->tiles);
	}
	ncgr->tiles = NULL;

	if (ncgr->attr != NULL) {
		free(ncgr->attr);
		ncgr->attr = NULL;
	}

	COMBO2D *combo = (COMBO2D *) ncgr->header.combo;
	if (combo != NULL) {
		combo2dUnlink(combo, &ncgr->header);
	}
}

void ChrInit(NCGR *ncgr, int format) {
	ncgr->header.size = sizeof(NCGR);
	ObjInit((OBJECT_HEADER *) ncgr, FILE_TYPE_CHARACTER, format);

	ncgr->header.dispose = ChrFree;
	ncgr->header.writer = (OBJECT_WRITER) ChrWrite;
}

void ChrReadChars(NCGR *ncgr, const unsigned char *buffer) {
	int nChars = ncgr->nTiles;

	unsigned char **tiles = (unsigned char **) calloc(nChars, sizeof(unsigned char **));
	for (int i = 0; i < nChars; i++) {
		tiles[i] = (unsigned char *) calloc(64, 1);
		unsigned char *tile = tiles[i];

		if (ncgr->nBits == 8) {
			//8-bit graphics: no need to unpack

			memcpy(tile, buffer, 64);
			buffer += 64;
		} else if (ncgr->nBits == 4) {
			//4-bit graphics: unpack

			for (int j = 0; j < 32; j++) {
				unsigned char b = *buffer;
				tile[j * 2] = b & 0xF;
				tile[j * 2 + 1] = b >> 4;
				buffer++;
			}
		}
	}
	ncgr->tiles = tiles;
}

void ChrReadBitmap(NCGR *ncgr, const unsigned char *buffer) {
	int depth = ncgr->nBits;
	int tilesX = ncgr->tilesX, tilesY = ncgr->tilesY;
	unsigned char **tiles = (unsigned char **) calloc(ncgr->nTiles, sizeof(unsigned char **));

	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {

			int offset = x * 4 + 4 * y * tilesX * 8;
			unsigned char *tile = calloc(64, 1);
			tiles[x + y * tilesX] = tile;
			if (depth == 8) {
				offset *= 2;
				const unsigned char *indices = buffer + offset;
				memcpy(tile, indices, 8);
				memcpy(tile + 8, indices + 8 * tilesX, 8);
				memcpy(tile + 16, indices + 16 * tilesX, 8);
				memcpy(tile + 24, indices + 24 * tilesX, 8);
				memcpy(tile + 32, indices + 32 * tilesX, 8);
				memcpy(tile + 40, indices + 40 * tilesX, 8);
				memcpy(tile + 48, indices + 48 * tilesX, 8);
				memcpy(tile + 56, indices + 56 * tilesX, 8);
			} else if (depth == 4) {
				const unsigned char *indices = buffer + offset;
				for (int j = 0; j < 8; j++) {
					for (int i = 0; i < 4; i++) {
						tile[i * 2 + j * 8] = indices[i + j * 4 * tilesX] & 0xF;
						tile[i * 2 + 1 + j * 8] = indices[i + j * 4 * tilesX] >> 4;
					}
				}
			}

		}
	}

	ncgr->tiles = tiles;
}

void ChrReadGraphics(NCGR *ncgr, const unsigned char *buffer) {
	if (ncgr->bitmap) {
		ChrReadBitmap(ncgr, buffer);
	} else {
		ChrReadChars(ncgr, buffer);
	}
}

static int ChrReadHudson(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	int type = ChrIsValidHudson(buffer, size);

	int nCharacters = 0;
	if (type == NCGR_TYPE_HUDSON) {

		unsigned int dataLength = *(uint16_t *) (buffer + 1);
		dataLength -= 4;
		if (dataLength + 8 > size) return 1;

		nCharacters = *(uint16_t *) (buffer + 5);
	} else if (type == NCGR_TYPE_HUDSON2) {
		nCharacters = *(uint16_t *) (buffer + 1);
	}

	ChrInit(ncgr, type);
	ncgr->nTiles = nCharacters;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->nBits = 8;
	ncgr->tilesX = -1;
	ncgr->tilesY = -1;
	ncgr->bitmap = 0;

	if (type == NCGR_TYPE_HUDSON) {
		if (buffer[4] == 0) {
			ncgr->nBits = 4;
		}
	} else if (type == NCGR_TYPE_HUDSON2) {
		//00: 4-bit, 01: 8-bit
		if (buffer[0] == 0) {
			ncgr->nBits = 4;
		}
	}

	int tileCount = nCharacters;
	int tilesX, tilesY;
	tilesX = ChrGuessWidth(nCharacters);
	tilesY = tileCount / tilesX;

	buffer += 0x4;
	if (type == NCGR_TYPE_HUDSON) buffer += 0x4;

	ncgr->tilesX = tilesX;
	ncgr->tilesY = tilesY;
	ChrReadGraphics(ncgr, buffer);
	return OBJ_STATUS_SUCCESS;
}

static int ChriIsCommonRead(NCGR *ncgr, const unsigned char *buffer, unsigned int size, int type) {
	int footerOffset = ChriIsCommonScanFooter(buffer, size, type);

	int width = 0, height = 0, depth = 4;

	//process extra data
	unsigned int offset = (unsigned int) footerOffset;
	while (1) {
		const unsigned char *section = buffer + offset;
		unsigned int len = *(uint32_t *) (section + 4);
		const unsigned char *sectionData = section + 8;

		if (memcmp(section, "SIZE", 4) == 0) {
			//SIZE
			width = *(uint16_t *) (sectionData + 0);
			height = *(uint16_t *) (sectionData + 2);
		} else if (memcmp(section, "MODE", 4) == 0) {
			int mode = *(int *) (sectionData + 0);

			if (type == NCGR_TYPE_AC) {
				//2, 3: 8bpp text
				depth = (mode == 3 || mode == 2) ? 8 : 4;
			} else if (type == NCGR_TYPE_IC) {
				//6: 8bpp text, 7: 4bpp text, 8: 8bpp ext
				depth = (mode == 6 || mode == 8) ? 8 : 4;
				ncgr->extPalette = (mode == 8);
			}
		} else if (memcmp(section, "LINK", 4) == 0) {
			//LINK
			int linkLen = sectionData[1];
			ncgr->header.fileLink = (char *) calloc(linkLen + 1, 1);
			memcpy(ncgr->header.fileLink, sectionData + 2, linkLen);
		} else if (memcmp(section, "CMNT", 4) == 0) {
			//CMNT
			int cmntLen = sectionData[1];
			ncgr->header.comment = (char *) calloc(cmntLen + 1, 1);
			memcpy(ncgr->header.comment, sectionData + 2, cmntLen);
		}

		offset += len + 8;
		if (offset >= size) break;
	}

	int attrSize = width * height;
	int nChars = width * height;
	const unsigned char *attr = buffer + (nChars * 8 * depth);

	ChrInit(ncgr, type);
	ncgr->tilesX = width;
	ncgr->tilesY = height;
	ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
	ncgr->nBits = depth;
	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;

	ChrReadGraphics(ncgr, buffer);

	//attr
	ncgr->attr = (unsigned char *) calloc(width * height, 1);
	for (int i = 0; i < width * height; i++) {
		ncgr->attr[i] = attr[i] & 0xF;
	}
	return OBJ_STATUS_SUCCESS;
}

static int ChrReadAcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	return ChriIsCommonRead(ncgr, buffer, size, NCGR_TYPE_AC);
}

static int ChrReadIcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	return ChriIsCommonRead(ncgr, buffer, size, NCGR_TYPE_IC);
}

static int ChrReadBin(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_BIN);
	ncgr->nTiles = size / 0x20;
	ncgr->nBits = 4;
	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;

	ChrReadGraphics(ncgr, buffer);
	return OBJ_STATUS_SUCCESS;
}

static int ChrReadGhostTrick(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_GHOSTTRICK);
	ncgr->nBits = 4;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_128K;

	//made up of 1 or more LZX chunk
	const unsigned char *end = buffer + size;
	const unsigned char *pos = buffer;

	unsigned int uncompSize = 0;
	unsigned char *uncomp = NULL;
	int nSlices = 0;
	CHAR_SLICE *slices = NULL;

	while (pos < end) {
		unsigned int chunkSize;
		unsigned char *p = CxDecompressLZX(pos, end - pos, &chunkSize);

		uncompSize += chunkSize;
		uncomp = (unsigned char *) realloc(uncomp, uncompSize);
		memcpy(uncomp + uncompSize - chunkSize, p, chunkSize);

		nSlices++;
		slices = realloc(slices, nSlices * sizeof(CHAR_SLICE));
		slices[nSlices - 1].offset = uncompSize - chunkSize;
		slices[nSlices - 1].size = chunkSize;

		//advance LZ. If failure, invalid
		pos = CxAdvanceLZX(pos, end - pos);

		//advance 0 bytes until we reach another chunk
		while (pos < end && *pos == '\0') {
			pos++;
		}
	}

	//set graphics
	ncgr->nTiles = uncompSize / 0x20;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ncgr->tiles = (unsigned char **) calloc(ncgr->nTiles, sizeof(unsigned char *));
	ncgr->slices = slices;
	ncgr->nSlices = nSlices;
	ChrReadGraphics(ncgr, uncomp);
	free(uncomp);
	return OBJ_STATUS_SUCCESS;
}

static int ChrReadSetosa(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_SETOSA);

	//CHAR and CATR blocks
	const unsigned char *pChar = SetGetBlock(buffer, size, "CHAR");
	const unsigned char *pCatr = SetGetBlock(buffer, size, "CATR");

	unsigned int charsX = *(const uint16_t *) (pChar + 0x04);
	unsigned int charsY = *(const uint16_t *) (pChar + 0x06);
	unsigned int flags = *(const uint16_t *) (pChar + 0x08);

	//check size
	if (flags & RES_CHAR_NOWIDTH) {
		unsigned int nChar = charsX * charsY;
		charsX = ChrGuessWidth(nChar);
		charsY = nChar / charsX;
	}
	ncgr->tilesX = charsX;
	ncgr->tilesY = charsY;
	ncgr->nTiles = charsX * charsY;

	//get mapping mode
	if (flags & RES_CHAR_FLAG_1D) {
		unsigned int shift = (flags & RES_CHAR_MAP_SHIFT_MASK) >> RES_CHAR_MAP_SHIFT_SHIFT;
		switch (shift) {
			case 0: ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K; break;
			case 1: ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_64K; break;
			case 2: ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_128K; break;
			case 3: ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_256K; break;
		}
	} else {
		ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_2D;
	}

	//bit depth and bitmap mode
	ncgr->bitmap = !!(flags & RES_CHAR_FLAG_BMP);
	ncgr->nBits = (flags & RES_CHAR_FLAG_8) ? 8 : 4;
	ncgr->isIntermediate = !!(flags & RES_CHAR_FLAG_IMD);
	ChrReadGraphics(ncgr, pChar + 0x0C);

	//read attribute
	if ((flags & RES_CHAR_FLAG_IMD) && (pCatr != NULL)) {
		ncgr->attr = (unsigned char *) calloc(charsX * charsY, 1);
		memcpy(ncgr->attr, pCatr, charsX * charsY);
	}

	return OBJ_STATUS_SUCCESS;
}

static int ChrReadNcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_NC);

	unsigned int charSize = 0, attrSize = 0, linkSize = 0, cmntSize = 0;
	const unsigned char *sChar = NnsG2dFindBlockBySignature(buffer, size, "CHAR", NNS_SIG_BE, &charSize);
	const unsigned char *sAttr = NnsG2dFindBlockBySignature(buffer, size, "ATTR", NNS_SIG_BE, &attrSize);
	const unsigned char *sLink = NnsG2dFindBlockBySignature(buffer, size, "LINK", NNS_SIG_BE, &linkSize);
	const unsigned char *sCmnt = NnsG2dFindBlockBySignature(buffer, size, "CMNT", NNS_SIG_BE, &cmntSize);
	
	int paletteType = *(uint32_t *) (sChar + 0x8);
	ncgr->nBits = (paletteType == 0) ? 4 : 8;
	ncgr->extPalette = (paletteType == 2);
	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tilesX = *(uint32_t *) (sChar + 0x0);
	ncgr->tilesY = *(uint32_t *) (sChar + 0x4);
	ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;

	ChrReadChars(ncgr, sChar + 0xC);

	if (sCmnt != NULL) {
		ncgr->header.comment = (char *) malloc(cmntSize);
		memcpy(ncgr->header.comment, sCmnt, cmntSize);
	}
	if (sLink != NULL) {
		ncgr->header.fileLink = (char *) malloc(linkSize);
		memcpy(ncgr->header.fileLink, sLink, linkSize);
	}
	if (sAttr != NULL) {
		int attrWidth = *(uint32_t *) (sAttr + 0x0);
		int attrHeight = *(uint32_t *) (sAttr + 0x4);

		ncgr->attr = (unsigned char *) calloc(ncgr->tilesX * ncgr->tilesY, 1);
		for (int y = 0; y < ncgr->tilesY; y++) {
			const unsigned char *srcRow = sAttr + 0x8 + y * attrWidth;
			unsigned char *dstRow = ncgr->attr + y * ncgr->tilesX;
			memcpy(dstRow, srcRow, min(attrWidth, ncgr->tilesX));
		}
	}

	return OBJ_STATUS_SUCCESS;
}

static int ChrReadNcgr(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	unsigned int charSize = 0;
	const unsigned char *sChar = NnsG2dFindBlockBySignature(buffer, size, "CHAR", NNS_SIG_LE, &charSize);

	int tilesY = *(uint16_t *) (sChar + 0x0);
	int tilesX = *(uint16_t *) (sChar + 0x2);
	int depth = *(uint32_t *) (sChar + 0x4);
	int mapping = *(uint32_t *) (sChar + 0x8);
	depth = 1 << (depth - 1);
	int tileDataSize = *(uint32_t *) (sChar + 0x10);
	int type = *(uint32_t *) (sChar + 0xC);
	uint32_t gfxOffset = *(uint32_t *) (sChar + 0x14);

	int tileCount = tilesX * tilesY;
	int nPresentTiles = tileDataSize >> 5;
	if (depth == 8) nPresentTiles >>= 1;
	if (NCGR_1D(mapping) || tileCount != nPresentTiles) {
		tileCount = nPresentTiles;
		tilesX = ChrGuessWidth(tileCount);
		tilesY = tileCount / tilesX;
	}

	ChrInit(ncgr, NCGR_TYPE_NCGR);
	ncgr->nBits = depth;
	ncgr->bitmap = 0;
	ncgr->nTiles = tileCount;
	ncgr->tilesX = tilesX;
	ncgr->tilesY = tilesY;
	ncgr->mappingMode = mapping;
	ncgr->bitmap = (type == 1);

	ChrReadGraphics(ncgr, sChar + gfxOffset);
	return OBJ_STATUS_SUCCESS;
}

static int ChrReadTose(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_TOSE);

	//bit depth
	unsigned int nChr = *(const uint16_t *) (buffer + 0x4);
	unsigned int gfxSize = size - 8;
	if (gfxSize == nChr * 0x20) ncgr->nBits = 4;
	else ncgr->nBits = 8;

	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->nTiles = nChr;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ChrReadChars(ncgr, buffer + 8);

	return OBJ_STATUS_SUCCESS;
}

int ChrRead(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	int type = ChrIdentify(buffer, size);
	switch (type) {
		case NCGR_TYPE_NCGR:
			return ChrReadNcgr(ncgr, buffer, size);
		case NCGR_TYPE_NC:
			return ChrReadNcg(ncgr, buffer, size);
		case NCGR_TYPE_IC:
			return ChrReadIcg(ncgr, buffer, size);
		case NCGR_TYPE_AC:
			return ChrReadAcg(ncgr, buffer, size);
		case NCGR_TYPE_TOSE:
			return ChrReadTose(ncgr, buffer, size);
		case NCGR_TYPE_HUDSON:
			return ChrReadHudson(ncgr, buffer, size);
		case NCGR_TYPE_GHOSTTRICK:
			return ChrReadGhostTrick(ncgr, buffer, size);
		case NCGR_TYPE_SETOSA:
			return ChrReadSetosa(ncgr, buffer, size);
		case NCGR_TYPE_BIN:
			return ChrReadBin(ncgr, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

int ChrReadFile(NCGR *ncgr, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) ncgr, (OBJECT_READER) ChrRead);
}

void ChrGetChar(NCGR *ncgr, int chno, CHAR_VRAM_TRANSFER *transfer, unsigned char *out) {
	//if transfer == NULL, don't simulate any VRAM transfer operation
	if (transfer == NULL) {
		if (chno < ncgr->nTiles) memcpy(out, ncgr->tiles[chno], 64);
		else memset(out, 0, 64);
		return;
	}

	//get character source address
	unsigned int chrSize = 8 * ncgr->nBits;
	unsigned int srcAddr = chno * chrSize;
	if ((srcAddr + chrSize) < transfer->dstAddr || srcAddr >= (transfer->dstAddr + transfer->size)) {
		if (chno < ncgr->nTiles) memcpy(out, ncgr->tiles[chno], 64);
		else memset(out, 0, 64);
		return;
	}

	//character is within the destination region. For bytes within the region, copy from src.
	//TODO: handle bitmapped graphics transfers too
	for (unsigned int i = 0; i < 64; i++) {
		//copy ncgr->tiles[chrno][i] to out[i]
		unsigned int pxaddr = srcAddr + (i >> (ncgr->nBits == 4 ? 1 : 0));
		if (pxaddr >= transfer->dstAddr && pxaddr < (transfer->dstAddr + transfer->size)) {
			//in transfer destination
			pxaddr = pxaddr - transfer->dstAddr + transfer->srcAddr;
			unsigned int transferChr = pxaddr / chrSize;
			unsigned int transferChrPxOffset = pxaddr % chrSize;
			unsigned int pxno = transferChrPxOffset;
			if (ncgr->nBits == 4) {
				pxno <<= 1;
				pxno += (i & 1);
			}
			out[i] = ncgr->tiles[transferChr][pxno];
		} else {
			//out of transfer destination
			out[i] = ncgr->tiles[chno][i];
		}
	}
}

static int ChriRenderCharacter(unsigned char *chr, int depth, int palette, NCLR *nclr, COLOR32 *out, int transparent) {
	for (int i = 0; i < 64; i++) {
		int index = chr[i];
		if (index || !transparent) {
			COLOR w = 0;
			if (nclr && (index + (palette << depth)) < nclr->nColors)
				w = nclr->colors[index + (palette << depth)];
			out[i] = ColorConvertFromDS(CREVERSE(w)) | 0xFF000000;
		} else {
			out[i] = 0;
		}
	}
	return 0;
}

int ChrRenderCharacter(NCGR *ncgr, NCLR *nclr, int chNo, COLOR32 *out, int previewPalette, int transparent) {
	if (chNo < ncgr->nTiles) {
		unsigned char *tile = ncgr->tiles[chNo];
		return ChriRenderCharacter(tile, ncgr->nBits, previewPalette, nclr, out, transparent);
	} else {
		memset(out, 0, 64 * 4);
		return 1;
	}
}

int ChrRenderCharacterTransfer(NCGR *ncgr, NCLR *nclr, int chNo, CHAR_VRAM_TRANSFER *transfer, COLOR32 *out, int palette, int transparent) {
	//if transfer == NULL, render as normal
	if (transfer == NULL) return ChrRenderCharacter(ncgr, nclr, chNo, out, palette, transparent);

	//else, read graphics and render
	unsigned char buf[64];
	ChrGetChar(ncgr, chNo, transfer, buf);
	return ChriRenderCharacter(buf, ncgr->nBits, palette, nclr, out, transparent);
}

void ChrSetWidth(NCGR *ncgr, int width) {
	//unimplemented right now
	if (ncgr->nTiles % width) return;

	//only matters for bitmap graphics
	if (!ncgr->bitmap) {
		ncgr->tilesX = width;
		ncgr->tilesY = ncgr->nTiles / width;
		return;
	}

	//use a temporary buffer to unswizzle and reswizzle
	unsigned char *bmp = (unsigned char *) calloc(ncgr->tilesX * ncgr->tilesY * 64, 1);
	int bmpWidth = ncgr->tilesX * 8, bmpHeight = ncgr->tilesY * 8;
	for (int y = 0; y < ncgr->tilesY; y++) {
		for (int x = 0; x < ncgr->tilesX; x++) {
			unsigned char *tile = ncgr->tiles[y * ncgr->tilesX + x];
			int bmpX = x * 8;
			int bmpY = y * 8;

			for (int i = 0; i < 8; i++) {
				memcpy(bmp + bmpX + (bmpY + i) * bmpWidth, tile + i * 8, 8);
			}
		}
	}

	//update width and height, then reswizzle
	ncgr->tilesX = width;
	ncgr->tilesY = ncgr->nTiles / width;
	bmpWidth = ncgr->tilesX * 8, bmpHeight = ncgr->tilesY * 8;
	for (int y = 0; y < ncgr->tilesY; y++) {
		for (int x = 0; x < ncgr->tilesX; x++) {
			unsigned char *tile = ncgr->tiles[y * ncgr->tilesX + x];
			int bmpX = x * 8;
			int bmpY = y * 8;

			for (int i = 0; i < 8; i++) {
				memcpy(tile + i * 8, bmp + bmpX + (bmpY + i) * bmpWidth, 8);
			}
		}
	}

	free(bmp);
}

void ChrWriteChars(NCGR *ncgr, BSTREAM *stream) {
	for (int i = 0; i < ncgr->nTiles; i++) {
		if (ncgr->nBits == 8) {
			bstreamWrite(stream, ncgr->tiles[i], 64);
		} else {
			unsigned char t[32];
			for (int j = 0; j < 32; j++) {
				t[j] = ncgr->tiles[i][j * 2] | (ncgr->tiles[i][j * 2 + 1] << 4);
			}
			bstreamWrite(stream, t, 32);
		}
	}
}

void ChrWriteBitmap(NCGR *ncgr, BSTREAM *stream) {
	int nTiles = ncgr->nTiles;
	unsigned char *bmp = (unsigned char *) calloc(nTiles, 8 * ncgr->nBits);

	int nWidth = ncgr->tilesX * 8;
	for (int y = 0; y < ncgr->tilesY; y++) {
		for (int x = 0; x < ncgr->tilesX; x++) {
			unsigned char *tile = ncgr->tiles[x + y * ncgr->tilesX];
			if (ncgr->nBits == 8) {
				for (int i = 0; i < 64; i++) {
					int tX = x * 8 + (i % 8);
					int tY = y * 8 + (i / 8);
					bmp[tX + tY * nWidth] = tile[i];
				}
			} else {
				for (int i = 0; i < 32; i++) {
					int tX = x * 8 + ((i * 2) % 8);
					int tY = y * 8 + (i / 4);
					bmp[(tX + tY * nWidth) / 2] = tile[i * 2] | (tile[i * 2 + 1] << 4);
				}
			}
		}
	}
	bstreamWrite(stream, bmp, nTiles * 8 * ncgr->nBits);
	free(bmp);
}

void ChrWriteGraphics(NCGR *ncgr, BSTREAM *stream) {
	if (ncgr->bitmap) {
		ChrWriteBitmap(ncgr, stream);
	} else {
		ChrWriteChars(ncgr, stream);
	}
}

int ChrWriteBin(NCGR *ncgr, BSTREAM *stream) {
	ChrWriteGraphics(ncgr, stream);
	return 0;
}

int ChrWriteNcgr(NCGR *ncgr, BSTREAM *stream) {
	unsigned char charHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	int nTiles = ncgr->nTiles;
	int nBytesPerTile = 8 * ncgr->nBits;
	int gfxFormat = (ncgr->nBits == 8) ? 4 : 3;

	if (NCGR_2D(ncgr->mappingMode)) {
		*(uint16_t *) (charHeader + 0x00) = ncgr->tilesY;
		*(uint16_t *) (charHeader + 0x02) = ncgr->tilesX;
	} else {
		*(uint32_t *) (charHeader + 0x00) = 0xFFFFFFFF;
	}
	*(uint32_t *) (charHeader + 0x04) = gfxFormat;
	*(uint32_t *) (charHeader + 0x08) = ncgr->mappingMode;
	*(uint32_t *) (charHeader + 0x0C) = !!ncgr->bitmap;
	*(uint32_t *) (charHeader + 0x10) = nTiles * nBytesPerTile;
	*(uint32_t *) (charHeader + 0x14) = sizeof(charHeader);

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCGR", 1, 0, NNS_TYPE_G2D, NNS_SIG_LE);

	NnsStreamStartBlock(&nnsStream, "CHAR");
	NnsStreamWrite(&nnsStream, charHeader, sizeof(charHeader));
	ChrWriteGraphics(ncgr, NnsStreamGetBlockStream(&nnsStream));
	NnsStreamEndBlock(&nnsStream);

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);

	return 0;
}

int ChrWriteNcg(NCGR *ncgr, BSTREAM *stream) {
	unsigned char charHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char attrHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	*(uint32_t *) (charHeader + 0x0) = ncgr->tilesX;
	*(uint32_t *) (charHeader + 0x4) = ncgr->tilesY;
	*(uint32_t *) (charHeader + 0x8) = (ncgr->extPalette && ncgr->nBits == 8) ? 2 : (ncgr->nBits == 8);
	*(uint32_t *) (attrHeader + 0x0) = ncgr->tilesX;
	*(uint32_t *) (attrHeader + 0x4) = ncgr->tilesY;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCCG", 1, 0, NNS_TYPE_G2D, NNS_SIG_BE);
	NnsStreamStartBlock(&nnsStream, "CHAR");
	NnsStreamWrite(&nnsStream, charHeader, sizeof(charHeader));
	ChrWriteChars(ncgr, NnsStreamGetBlockStream(&nnsStream)); //NCG doesn't store bitmap graphics
	NnsStreamEndBlock(&nnsStream);

	NnsStreamStartBlock(&nnsStream, "ATTR");
	NnsStreamWrite(&nnsStream, attrHeader, sizeof(attrHeader));
	if (ncgr->attr != NULL) {
		NnsStreamWrite(&nnsStream, ncgr->attr, ncgr->tilesX * ncgr->tilesY);
	} else {
		unsigned char *dummy = (unsigned char *) calloc(ncgr->tilesX * ncgr->tilesY, 1);
		NnsStreamWrite(&nnsStream, dummy, ncgr->tilesX * ncgr->tilesY);
		free(dummy);
	}
	NnsStreamEndBlock(&nnsStream);

	if (ncgr->header.fileLink != NULL) {
		NnsStreamStartBlock(&nnsStream, "LINK");
		NnsStreamWrite(&nnsStream, ncgr->header.fileLink, strlen(ncgr->header.fileLink));
		NnsStreamEndBlock(&nnsStream);
	}
	if (ncgr->header.comment != NULL) {
		NnsStreamStartBlock(&nnsStream, "CMNT");
		NnsStreamWrite(&nnsStream, ncgr->header.comment, strlen(ncgr->header.comment));
		NnsStreamEndBlock(&nnsStream);
	}

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

static int ChriIsCommonWrite(NCGR *ncgr, BSTREAM *stream) {
	IscadStream cadStream;
	IscadStreamCreate(&cadStream);
	ChrWriteChars(ncgr, &cadStream.stream);

	//write attribute for ACG
	unsigned int attrSize = ncgr->tilesX * ncgr->tilesY;
	for (unsigned int i = 0; i < attrSize; i++) {
		unsigned char a = 0;
		if (ncgr->attr != NULL) {
			a = ncgr->attr[i];
		}

		if (ncgr->header.format == NCGR_TYPE_AC) {
			a |= 0x20; //exists
			a |= (ncgr->nBits == 8) ? 0x10 : 0x00;
		}

		IscadStreamWrite(&cadStream, &a, sizeof(a));
	}

	char *version = "";
	switch (ncgr->header.format) {
		case NCGR_TYPE_AC:
			version = "IS-ACG03"; break;
		case NCGR_TYPE_IC:
			version = "IS-ICG01"; break;
	}

	uint32_t mode = 0;
	if (ncgr->header.format == NCGR_TYPE_AC) {
		//3: 8bpp, 1: 4bpp
		if (ncgr->nBits == 8) mode = 3;
		else mode = 1;
	} else if (ncgr->header.format == NCGR_TYPE_IC) {
		//6: text 8bpp, 7: text 4bpp, 8: ext 8bpp
		if (ncgr->nBits == 4) mode = 7;
		else if (!ncgr->extPalette) mode = 6;
		else mode = 8;
	}

	uint16_t sizeFooter[2];
	sizeFooter[0] = ncgr->tilesX;
	sizeFooter[1] = ncgr->tilesY;

	IscadStreamStartBlock(&cadStream, "LINK");
	IscadStreamWriteCountedString(&cadStream, ncgr->header.fileLink);
	IscadStreamEndBlock(&cadStream);

	IscadStreamStartBlock(&cadStream, "CMNT");
	IscadStreamWriteCountedString(&cadStream, ncgr->header.comment);
	IscadStreamEndBlock(&cadStream);

	IscadWriteBlock(&cadStream, "MODE", &mode, sizeof(mode));
	IscadWriteBlock(&cadStream, "SIZE", sizeFooter, sizeof(sizeFooter));
	IscadWriteBlock(&cadStream, "VER ", version, strlen(version));
	IscadWriteBlock(&cadStream, "END ", NULL, 0);

	IscadStreamFinalize(&cadStream);
	IscadStreamFlushOut(&cadStream, stream);
	IscadStreamFree(&cadStream);
	return 0;
}

int ChrWriteAcg(NCGR *ncgr, BSTREAM *stream) {
	return ChriIsCommonWrite(ncgr, stream);
}

int ChrWriteIcg(NCGR *ncgr, BSTREAM *stream) {
	return ChriIsCommonWrite(ncgr, stream);
}

int ChrWriteHudson(NCGR *ncgr, BSTREAM *stream) {
	if (ncgr->header.format == NCGR_TYPE_HUDSON) {
		unsigned char header[] = { 0, 0, 0, 0, 1, 0, 0, 0 };
		if (ncgr->nBits == 4) header[4] = 0;
		*(uint16_t *) (header + 5) = ncgr->nTiles;
		int nCharacterBytes = 64 * ncgr->nTiles;
		if (ncgr->nBits == 4) nCharacterBytes >>= 1;
		*(uint16_t *) (header + 1) = nCharacterBytes + 4;

		bstreamWrite(stream, header, sizeof(header));
	} else if(ncgr->header.format == NCGR_TYPE_HUDSON2) {
		unsigned char header[] = { 0, 0, 0, 0 };
		header[0] = (ncgr->nBits == 8) ? 1 : 0;
		*(uint16_t *) (header + 1) = ncgr->nTiles;
		bstreamWrite(stream, header, sizeof(header));
	}

	ChrWriteChars(ncgr, stream);
	return 0;
}

int ChrWriteGhostTrick(NCGR *ncgr, BSTREAM *stream) {
	//get raw graphics data
	BSTREAM gfxStream;
	bstreamCreate(&gfxStream, NULL, 0);
	ChrWriteChars(ncgr, &gfxStream);

	//write slices
	for (int i = 0; i < ncgr->nSlices; i++) {
		CHAR_SLICE *slice = ncgr->slices + i;
		unsigned int compSize;
		unsigned char *comp = CxCompressLZX(gfxStream.buffer + slice->offset, slice->size, &compSize);
		bstreamWrite(stream, comp, compSize);
		free(comp);

		//align
		bstreamAlign(stream, 4);
	}

	bstreamFree(&gfxStream);
	return 0;
}

static int ChrWriteSetosa(NCGR *ncgr, BSTREAM *stream) {
	SetStream setStream;
	SetStreamCreate(&setStream);

	//CHAR block
	uint16_t flags = 0;
	if (ncgr->nBits == 8) flags |= RES_CHAR_FLAG_8;
	if (ncgr->bitmap) flags |= RES_CHAR_FLAG_BMP;
	if (ncgr->isIntermediate) flags |= RES_CHAR_FLAG_IMD;
	switch (ncgr->mappingMode) {
		case GX_OBJVRAMMODE_CHAR_2D:
			break;
		case GX_OBJVRAMMODE_CHAR_1D_32K:
			flags |= RES_CHAR_FLAG_1D | (0 << RES_CHAR_MAP_SHIFT_SHIFT);
			break;
		case GX_OBJVRAMMODE_CHAR_1D_64K:
			flags |= RES_CHAR_FLAG_1D | (1 << RES_CHAR_MAP_SHIFT_SHIFT);
			break;
		case GX_OBJVRAMMODE_CHAR_1D_128K:
			flags |= RES_CHAR_FLAG_1D | (2 << RES_CHAR_MAP_SHIFT_SHIFT);
			break;
		case GX_OBJVRAMMODE_CHAR_1D_256K:
			flags |= RES_CHAR_FLAG_1D | (3 << RES_CHAR_MAP_SHIFT_SHIFT);
			break;
	}

	unsigned char charHeader[0xC] = { 0 };
	*(uint32_t *) (charHeader + 0x0) = ncgr->tilesX * ncgr->tilesY * (8 * ncgr->nBits);
	*(uint16_t *) (charHeader + 0x4) = ncgr->tilesX;
	*(uint16_t *) (charHeader + 0x6) = ncgr->tilesY;
	*(uint16_t *) (charHeader + 0x8) = flags;

	SetStreamStartBlock(&setStream, "CHAR");
	SetStreamWrite(&setStream, charHeader, sizeof(charHeader));
	ChrWriteGraphics(ncgr, &setStream.currentStream);
	SetStreamEndBlock(&setStream);

	//CATR block
	if (ncgr->isIntermediate && ncgr->attr != NULL) {
		SetStreamStartBlock(&setStream, "CATR");
		SetStreamWrite(&setStream, ncgr->attr, ncgr->nTiles);
		SetStreamEndBlock(&setStream);
	}
	
	SetStreamFinalize(&setStream);
	SetStreamFlushOut(&setStream, stream);
	SetStreamFree(&setStream);
	return OBJ_STATUS_SUCCESS;
}

static int ChrWriteTose(NCGR *ncgr, BSTREAM *stream) {
	int isExPltt = ncgr->nBits == 8; // TODO?

	uint16_t hdr[2];
	hdr[0] = ncgr->nTiles;
	hdr[1] = (ncgr->nBits == 8) | (isExPltt << 1);
	bstreamWrite(stream, "NCG\0", 4);
	bstreamWrite(stream, hdr, sizeof(hdr));
	ChrWriteChars(ncgr, stream);

	return OBJ_STATUS_SUCCESS;
}

int ChrWrite(NCGR *ncgr, BSTREAM *stream) {
	switch (ncgr->header.format) {
		case NCGR_TYPE_NCGR:
			return ChrWriteNcgr(ncgr, stream);
		case NCGR_TYPE_NC:
			return ChrWriteNcg(ncgr, stream);
		case NCGR_TYPE_AC:
			return ChrWriteAcg(ncgr, stream);
		case NCGR_TYPE_IC:
			return ChrWriteIcg(ncgr, stream);
		case NCGR_TYPE_TOSE:
			return ChrWriteTose(ncgr, stream);
		case NCGR_TYPE_HUDSON:
		case NCGR_TYPE_HUDSON2:
			return ChrWriteHudson(ncgr, stream);
		case NCGR_TYPE_GHOSTTRICK:
			return ChrWriteGhostTrick(ncgr, stream);
		case NCGR_TYPE_SETOSA:
			return ChrWriteSetosa(ncgr, stream);
		case NCGR_TYPE_BIN:
			return ChrWriteBin(ncgr, stream);
	}
	return 1;
}

int ChrWriteFile(NCGR *ncgr, LPCWSTR name) {
	return ObjWriteFile(&ncgr->header, name);
}

void ChrSetDepth(NCGR *ncgr, int depth) {
	if (depth == ncgr->nBits) return; //do nothing

	//compute new tile count
	int nTiles2 = ncgr->nTiles;
	if (depth == 8) {
		//4bpp -> 8bpp, tile count /= 2
		nTiles2 = (nTiles2 + 1) / 2;
	} else {
		//8bpp -> 4bpp, tile count *= 2
		nTiles2 *= 2;
	}
	unsigned char **tiles2 = (unsigned char **) calloc(nTiles2, sizeof(unsigned char **));
	unsigned char *attr2 = (unsigned char *) calloc(nTiles2, 1);

	if (depth == 8) {
		//convert 4bpp graphic to 8bpp
		for (int i = 0; i < nTiles2; i++) {
			unsigned char *tile1 = ncgr->tiles[i * 2];
			unsigned char *dest = (unsigned char *) calloc(64, 1);
			tiles2[i] = dest;

			//first half
			for (int j = 0; j < 32; j++) {
				dest[j] = tile1[j * 2] | (tile1[j * 2 + 1] << 4);
			}

			//second half, only if it exists
			if ((i * 2 + 1) < ncgr->nTiles) {
				unsigned char *tile2 = ncgr->tiles[i * 2 + 1];
				for (int j = 0; j < 32; j++) {
					dest[j + 32] = tile2[j * 2] | (tile2[j * 2 + 1] << 4);
				}
			}
		}

		//attribute data: take every other attribute
		for (int i = 0; i < nTiles2; i++) {
			if (ncgr->attr != NULL) attr2[i] = ncgr->attr[i * 2];
		}
	} else {
		//covert 8bpp graphic to 4bpp
		for (int i = 0; i < ncgr->nTiles; i++) {
			unsigned char *tile1 = calloc(64, 1);
			unsigned char *tile2 = calloc(64, 1);
			unsigned char *src = ncgr->tiles[i];
			tiles2[i * 2 + 0] = tile1;
			tiles2[i * 2 + 1] = tile2;

			for (int j = 0; j < 32; j++) {
				tile1[j * 2 + 0] = (src[j +  0] >> 0) & 0xF;
				tile1[j * 2 + 1] = (src[j +  0] >> 4) & 0xF;
				tile2[j * 2 + 0] = (src[j + 32] >> 0) & 0xF;
				tile2[j * 2 + 1] = (src[j + 32] >> 4) & 0xF;
			}
		}

		//attribute data: double up each attribute byte
		for (int i = 0; i < nTiles2; i++) {
			if (ncgr->attr != NULL) attr2[i] = ncgr->attr[i / 2];
		}
	}

	//replace graphics
	for (int i = 0; i < ncgr->nTiles; i++) {
		if (ncgr->tiles[i] != NULL) free(ncgr->tiles[i]);
	}
	free(ncgr->tiles);
	ncgr->tiles = tiles2;

	//replace attributes
	if (ncgr->attr != NULL) free(ncgr->attr);
	ncgr->attr = attr2;

	//adjust dimensions and size
	ncgr->nTiles = nTiles2;
	ncgr->nBits = depth;
	if (depth == 8) {
		//may not be able to just halve the width
		if ((ncgr->tilesX & 1) == 0) {
			ncgr->tilesX /= 2;
		} else {
			ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
			ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
		}
	} else {
		//just double the width
		ncgr->tilesX *= 2;
	}
}

void ChrResize(NCGR *ncgr, int width, int height) {
	//either pad on the sides or crop
	if (ncgr->tilesX == width && ncgr->tilesY == height) return;

	//allocate new buffer
	unsigned char **chars2 = (unsigned char **) calloc(width * height, sizeof(unsigned char *));
	unsigned char *attr2 = (unsigned char *) calloc(width * height, 1);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			unsigned char *chr = (unsigned char *) calloc(64, 1);
			chars2[x + y * width] = chr;

			//read in
			if (x < ncgr->tilesX && y < ncgr->tilesY) {
				unsigned char *src = ncgr->tiles[x + y * ncgr->tilesX];
				memcpy(chr, src, 64);
				if (ncgr->attr != NULL) attr2[x + y * width] = ncgr->attr[x + y * ncgr->tilesX];
			}
		}
	}

	//free original character
	for (int i = 0; i < ncgr->nTiles; i++) {
		free(ncgr->tiles[i]);
	}
	free(ncgr->tiles);
	if (ncgr->attr != NULL) free(ncgr->attr);
	ncgr->tiles = chars2;
	ncgr->attr = attr2;

	ncgr->tilesX = width;
	ncgr->tilesY = height;
	ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
}
