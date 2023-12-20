#include "ncgr.h"
#include "nclr.h"
#include "nscr.h"
#include "color.h"
#include "nns.h"

#include <stdio.h>

LPCWSTR characterFormatNames[] = { L"Invalid", L"NCGR", L"NCG", L"ICG", L"ACG", L"Hudson", L"Hudson 2", L"Ghost Trick", L"Binary", NULL };

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
	if (size < 8) return -1;

	//scan for possible locations of the footer
	for (unsigned int i = 0; i < size - 8; i++) {
		if (buffer[i] != 'L') continue;
		if (memcmp(buffer + i, "LINK", 4) != 0) continue;
		
		//candidate location
		int hasLink = 0, hasCmnt = 0, hasMode = 0, hasSize = 0, hasVer = 0, hasEnd = 0;

		//scan sections
		unsigned int offset = i;
		while (1) {
			const unsigned char *section = buffer + offset;
			unsigned int length = *(uint32_t *) (buffer + offset + 4);
			offset += 8;

			if (memcmp(section, "LINK", 4) == 0) hasLink = 1;
			else if (memcmp(section, "CMNT", 4) == 0) hasCmnt = 1;
			else if (memcmp(section, "MODE", 4) == 0) hasMode = 1;
			else if (memcmp(section, "SIZE", 4) == 0) hasSize = 1;
			else if (memcmp(section, "VER ", 4) == 0) hasVer = 1;
			else if (memcmp(section, "END ", 4) == 0) hasEnd = 1;

			if (memcmp(section, "VER ", 4) == 0) {
				//ACG: ver = "IS-ACG0x" (1-3)
				//ICG: ver = "IS-ICG01"
				const char *ver = section + 8;
				if (type == NCGR_TYPE_AC && (length < 8 || memcmp(ver, "IS-ACG", 6))) return -1;
				if (type == NCGR_TYPE_IC && (length < 8 || memcmp(ver, "IS-ICG", 6))) return -1;
			}

			offset += length;
			if (offset >= size) break;
			if (hasEnd) break;
		}

		if (hasLink && hasCmnt && hasMode && hasSize && hasVer && hasEnd && offset <= size) {
			//candidate found
			return i;
		}
	}
	return -1;
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

int ChrIdentify(const unsigned char *buffer, unsigned int size) {
	if (ChrIsValidNcgr(buffer, size)) return NCGR_TYPE_NCGR;
	if (ChrIsValidNcg(buffer, size)) return NCGR_TYPE_NC;
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

	COMBO2D *combo = ncgr->combo2d;
	if (combo != NULL) {
		combo2dUnlink(combo, &ncgr->header);
		if (combo->nLinks == 0) {
			combo2dFree(combo);
			free(combo);
		}
	}
	ncgr->combo2d = NULL;
}

void ChrInit(NCGR *ncgr, int format) {
	ncgr->header.size = sizeof(NCGR);
	ObjInit((OBJECT_HEADER *) ncgr, FILE_TYPE_CHARACTER, format);
	ncgr->header.dispose = ChrFree;
	ncgr->header.writer = (OBJECT_WRITER) ChrWrite;
	ncgr->combo2d = NULL;
}

void ChrReadChars(NCGR *ncgr, const unsigned char *buffer) {
	int nChars = ncgr->nTiles;

	unsigned char **tiles = (unsigned char **) calloc(nChars, sizeof(BYTE **));
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
				BYTE b = *buffer;
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

int ChrReadHudson(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	if (size < 8) return 1; //file too small
	if (*buffer == 0x10) return 1; //TODO: LZ77 decompress
	int type = ChrIsValidHudson(buffer, size);
	if (type == NCGR_TYPE_INVALID) return 1;

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
	return 0;
}

int ChriIsCommonRead(NCGR *ncgr, const unsigned char *buffer, unsigned int size, int type) {
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
	return 0;
}

int ChrReadAcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	return ChriIsCommonRead(ncgr, buffer, size, NCGR_TYPE_AC);
}

int ChrReadIcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	return ChriIsCommonRead(ncgr, buffer, size, NCGR_TYPE_IC);
}

int ChrReadBin(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_BIN);
	ncgr->nTiles = size / 0x20;
	ncgr->nBits = 4;
	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;

	ChrReadGraphics(ncgr, buffer);
	return 0;
}

int ChrReadGhostTrick(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
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
	return 0;
}

int ChrReadNcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
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

	return 0;
}

int ChrReadNcgr(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
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
	return 0;
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
		case NCGR_TYPE_HUDSON:
			return ChrReadHudson(ncgr, buffer, size);
		case NCGR_TYPE_GHOSTTRICK:
			return ChrReadGhostTrick(ncgr, buffer, size);
		case NCGR_TYPE_BIN:
			return ChrReadBin(ncgr, buffer, size);
	}
	return 1;
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
	BYTE *bmp = (BYTE *) calloc(ncgr->tilesX * ncgr->tilesY * 64, 1);
	int bmpWidth = ncgr->tilesX * 8, bmpHeight = ncgr->tilesY * 8;
	for (int y = 0; y < ncgr->tilesY; y++) {
		for (int x = 0; x < ncgr->tilesX; x++) {
			BYTE *tile = ncgr->tiles[y * ncgr->tilesX + x];
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
			BYTE *tile = ncgr->tiles[y * ncgr->tilesX + x];
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
	int attrSize = ncgr->tilesX * ncgr->tilesY;

	ChrWriteChars(ncgr, stream);

	//write attribute for ACG
	for (int i = 0; i < attrSize; i++) {
		unsigned char a = 0;
		if (ncgr->attr != NULL) {
			a = ncgr->attr[i];
		}

		if (ncgr->header.format == NCGR_TYPE_AC) {
			a |= 0x20; //exists
			a |= (ncgr->nBits == 8) ? 0x10 : 0x00;
		}

		bstreamWrite(stream, &a, sizeof(a));
	}

	//footer
	unsigned char linkFooter[] = { 'L', 'I', 'N', 'K', 2, 0, 0, 0, 1, 0 };
	unsigned char cmntFooter[] = { 'C', 'M', 'N', 'T', 2, 0, 0, 0, 1, 0 };
	unsigned char modeFooter[] = { 'M', 'O', 'D', 'E', 4, 0, 0, 0, 1, 0, 0, 0 };
	unsigned char sizeFooter[] = { 'S', 'I', 'Z', 'E', 4, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char verFooter[] = { 'V', 'E', 'R', ' ', 0, 0, 0, 0 };
	unsigned char endFooter[] = { 'E', 'N', 'D', ' ', 0, 0, 0, 0 };

	char *version = "";
	switch (ncgr->header.format) {
		case NCGR_TYPE_AC:
			version = "IS-ACG03"; break;
		case NCGR_TYPE_IC:
			version = "IS-ICG01"; break;
	}

	int linkLen = (ncgr->header.fileLink == NULL) ? 0 : strlen(ncgr->header.fileLink);
	int commentLen = (ncgr->header.comment == NULL) ? 0 : strlen(ncgr->header.comment);

	int mode = 0;
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

	*(uint32_t *) (linkFooter + 4) = linkLen + 2;
	linkFooter[9] = (unsigned char) linkLen;
	*(uint32_t *) (cmntFooter + 4) = commentLen + 2;
	cmntFooter[9] = (unsigned char) commentLen;
	*(uint32_t *) (modeFooter + 8) = mode;
	*(uint16_t *) (sizeFooter + 8) = ncgr->tilesX;
	*(uint16_t *) (sizeFooter + 10) = ncgr->tilesY;
	*(uint32_t *) (verFooter + 4) = strlen(version);

	bstreamWrite(stream, linkFooter, sizeof(linkFooter));
	bstreamWrite(stream, ncgr->header.fileLink, linkLen);
	bstreamWrite(stream, cmntFooter, sizeof(cmntFooter));
	bstreamWrite(stream, ncgr->header.comment, commentLen);
	bstreamWrite(stream, modeFooter, sizeof(modeFooter));
	bstreamWrite(stream, sizeFooter, sizeof(sizeFooter));
	bstreamWrite(stream, verFooter, sizeof(verFooter));
	bstreamWrite(stream, version, strlen(version));
	bstreamWrite(stream, endFooter, sizeof(endFooter));
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

int ChrWriteCombo(NCGR *ncgr, BSTREAM *stream) {
	return combo2dWrite(ncgr->combo2d, stream);
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
		case NCGR_TYPE_HUDSON:
		case NCGR_TYPE_HUDSON2:
			return ChrWriteHudson(ncgr, stream);
		case NCGR_TYPE_GHOSTTRICK:
			return ChrWriteGhostTrick(ncgr, stream);
		case NCGR_TYPE_BIN:
			return ChrWriteBin(ncgr, stream);
		case NCGR_TYPE_COMBO:
			return ChrWriteCombo(ncgr, stream);
	}
	return 1;
}

int ChrWriteFile(NCGR *ncgr, LPCWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER *) ncgr, (OBJECT_WRITER) ChrWrite);
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
			if ((nTiles2 & 1) == 0) {
				unsigned char *tile2 = ncgr->tiles[i * 2 + 1];
				for (int j = 0; j < 32; j++) {
					dest[j + 32] = tile2[j * 2] | (tile2[j * 2 + 1] << 4);
				}
			}
		}

	} else {
		//covert 8bpp graphic to 4bpp
		for (int i = 0; i < ncgr->nTiles; i++) {
			unsigned char *tile1 = calloc(64, 1);
			unsigned char *tile2 = calloc(64, 1);
			unsigned char *src = ncgr->tiles[i];
			tiles2[i * 2] = tile1;
			tiles2[i * 2 + 1] = tile2;

			for (int j = 0; j < 32; j++) {
				tile1[j * 2 + 0] = (src[j + 0] >> 0) & 0xF;
				tile1[j * 2 + 1] = (src[j + 0] >> 4) & 0xF;
				tile2[j * 2 + 0] = (src[j + 32] >> 0) & 0xF;
				tile2[j * 2 + 1] = (src[j + 32] >> 4) & 0xF;
			}
		}
	}

	//replace graphics
	for (int i = 0; i < ncgr->nTiles; i++) {
		if (ncgr->tiles[i] != NULL) free(ncgr->tiles[i]);
	}
	free(ncgr->tiles);
	ncgr->tiles = tiles2;

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
