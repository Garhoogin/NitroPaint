#include "ncgr.h"
#include "nclr.h"
#include "nscr.h"
#include "color.h"
#include "nns.h"

#include <stdio.h>

LPCWSTR characterFormatNames[] = { L"Invalid", L"NCGR", L"NCG", L"ACG", L"Hudson", L"Hudson 2", L"Binary", NULL };

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

int ChrIsValidNcg(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;

	char *sChar = NnsG2dGetSectionByMagic(buffer, size, 'CHAR');
	if (sChar == NULL) sChar = NnsG2dGetSectionByMagic(buffer, size, 'RAHC');
	if (sChar == NULL) return 0;
	return 1;
}

static int ChriAcgScanFooter(const unsigned char *buffer, unsigned int size) {
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
	int dataOffset = ChriAcgScanFooter(buffer, size);
	if (dataOffset == -1) return 0;

	return 1;
}

int ChrIsValidNcgr(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "RGCN", 4) != 0) return 0;

	//find CHAR section
	const unsigned char *sChar = NnsG2dGetSectionByMagic(buffer, size, 'CHAR');
	if (sChar == NULL) sChar = NnsG2dGetSectionByMagic(buffer, size, 'RAHC');
	if (sChar == NULL) return 0;

	return 1;
}

int ChrIdentify(const unsigned char *buffer, unsigned int size) {
	if (ChrIsValidNcgr(buffer, size)) return NCGR_TYPE_NCGR;
	if (ChrIsValidNcg(buffer, size)) return NCGR_TYPE_NC;
	if (ChrIsValidAcg(buffer, size)) return NCGR_TYPE_AC;
	if (ChrIsValidHudson(buffer, size)) return NCGR_TYPE_HUDSON;
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

	if (ncgr->comment != NULL) {
		free(ncgr->comment);
		ncgr->comment = NULL;
	}
	if (ncgr->link != NULL) {
		free(ncgr->link);
		ncgr->link = NULL;
	}
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
	ncgr->tileWidth = 8;
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

int ChrReadAcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	int footerOffset = ChriAcgScanFooter(buffer, size);

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
			depth = (mode == 3 || mode == 2) ? 8 : 4;
		} else if (memcmp(section, "LINK", 4) == 0) {
			//LINK
			int linkLen = sectionData[1];
			ncgr->link = (char *) calloc(linkLen + 1, 1);
			memcpy(ncgr->link, sectionData + 2, linkLen);
		} else if (memcmp(section, "CMNT", 4) == 0) {
			//CMNT
			int cmntLen = sectionData[1];
			ncgr->comment = (char *) calloc(cmntLen + 1, 1);
			memcpy(ncgr->comment, sectionData + 2, cmntLen);
		}

		offset += len + 8;
		if (offset >= size) break;
	}

	int attrSize = width * height;
	int nChars = width * height;
	const unsigned char *attr = buffer + (nChars * 8 * depth);

	ChrInit(ncgr, NCGR_TYPE_AC);
	ncgr->tilesX = width;
	ncgr->tilesY = height;
	ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
	ncgr->tileWidth = 8;
	ncgr->nBits = depth;
	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;

	ChrReadGraphics(ncgr, buffer);

	//attr
	ncgr->attrWidth = width;
	ncgr->attrHeight = height;
	ncgr->attr = (unsigned char *) calloc(width * height, 1);
	for (int i = 0; i < width * height; i++) {
		ncgr->attr[i] = attr[i] & 0xF;
	}
	return 0;
}

int ChrReadBin(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_BIN);
	ncgr->nTiles = size / 0x20;
	ncgr->nBits = 4;
	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tileWidth = 8;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;

	ChrReadGraphics(ncgr, buffer);
	return 0;
}

int ChrReadNcg(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	ChrInit(ncgr, NCGR_TYPE_NC);

	unsigned char *sChar = NnsG2dGetSectionByMagic(buffer, size, 'CHAR');
	if (sChar == NULL) sChar = NnsG2dGetSectionByMagic(buffer, size, 'RAHC');
	unsigned char *sAttr = NnsG2dGetSectionByMagic(buffer, size, 'ATTR');
	if (sAttr == NULL) sAttr = NnsG2dGetSectionByMagic(buffer, size, 'RTTA');
	unsigned char *sLink = NnsG2dGetSectionByMagic(buffer, size, 'LINK');
	if (sLink == NULL) sLink = NnsG2dGetSectionByMagic(buffer, size, 'KNIL');
	unsigned char *sCmnt = NnsG2dGetSectionByMagic(buffer, size, 'CMNT');
	if (sCmnt == NULL) sCmnt = NnsG2dGetSectionByMagic(buffer, size, 'TNMC');
	
	ncgr->nBits = *(uint32_t *) (sChar + 0x10) == 0 ? 4 : 8;
	ncgr->bitmap = 0;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tilesX = *(uint32_t *) (sChar + 0x8);
	ncgr->tilesY = *(uint32_t *) (sChar + 0xC);
	ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
	ncgr->tileWidth = 8;

	ChrReadChars(ncgr, sChar + 0x14);

	if (sCmnt != NULL) {
		int len = *(uint32_t *) (sCmnt + 4) - 8;
		ncgr->comment = (char *) calloc(len, 1);
		memcpy(ncgr->comment, sCmnt + 8, len);
	}
	if (sLink != NULL) {
		int len = *(uint32_t *) (sLink + 4) - 8;
		ncgr->link = (char *) calloc(len, 1);
		memcpy(ncgr->link, sLink + 8, len);
	}
	if (sAttr != NULL) {
		int attrSize = *(uint32_t *) (sAttr + 0x4) - 0x10;
		ncgr->attrWidth = *(uint32_t *) (sAttr + 0x8);
		ncgr->attrHeight = *(uint32_t *) (sAttr + 0xC);
		ncgr->attr = (unsigned char *) calloc(attrSize, 1);
		memcpy(ncgr->attr, sAttr + 0x10, attrSize);
	}

	return 0;
}

int ChrReadNcgr(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	const unsigned char *sChar = NnsG2dGetSectionByMagic(buffer, size, 'CHAR');
	if (sChar == NULL) sChar = NnsG2dGetSectionByMagic(buffer, size, 'RAHC');

	int tilesY = *(uint16_t *) (sChar + 0x8);
	int tilesX = *(uint16_t *) (sChar + 0xA);
	int depth = *(uint32_t *) (sChar + 0xC);
	int mapping = *(uint32_t *) (sChar + 0x10);
	depth = 1 << (depth - 1);
	int tileDataSize = *(uint32_t *) (sChar + 0x18);
	int type = *(uint32_t *) (sChar + 0x14);
	uint32_t gfxOffset = *(uint32_t *) (sChar + 0x1C);

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
	ncgr->tileWidth = 8;
	ncgr->tilesX = tilesX;
	ncgr->tilesY = tilesY;
	ncgr->mappingMode = mapping;
	ncgr->bitmap = (type == 1);

	ChrReadGraphics(ncgr, sChar + gfxOffset + 8);
	return 0;
}

int ChrRead(NCGR *ncgr, const unsigned char *buffer, unsigned int size) {
	int type = ChrIdentify(buffer, size);
	switch (type) {
		case NCGR_TYPE_NCGR:
			return ChrReadNcgr(ncgr, buffer, size);
		case NCGR_TYPE_NC:
			return ChrReadNcg(ncgr, buffer, size);
		case NCGR_TYPE_AC:
			return ChrReadAcg(ncgr, buffer, size);
		case NCGR_TYPE_HUDSON:
			return ChrReadHudson(ncgr, buffer, size);
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
	unsigned char ncgrHeader[] = { 'R', 'G', 'C', 'N', 0xFF, 0xFE, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0, 0x1, 0 };
	unsigned char charHeader[] = { 'R', 'A', 'H', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	if (ncgr->bitmap) {
		charHeader[20] = 1;
	}

	int nTiles = ncgr->nTiles;
	int nBytesPerTile = 64;
	if (ncgr->nBits == 4) nBytesPerTile >>= 1;
	int sectionSize = 0x20 + nTiles * nBytesPerTile;
	int fileSize = 0x10 + sectionSize;

	if (ncgr->nBits == 8) *(int *) (charHeader + 0xC) = 4;
	else if (ncgr->nBits == 4) *(int *) (charHeader + 0xC) = 3;
	*(int *) (charHeader + 0x10) = ncgr->mappingMode;
	*(int *) (charHeader + 0x4) = sectionSize;
	if (NCGR_2D(ncgr->mappingMode)) {
		*(unsigned short *) (charHeader + 0x8) = ncgr->tilesY;
		*(unsigned short *) (charHeader + 0xA) = ncgr->tilesX;
	} else {
		*(int *) (charHeader + 0x8) = 0xFFFFFFFF;
	}
	*(int *) (charHeader + 0x1C) = 0x18;
	*(int *) (charHeader + 0x18) = sectionSize - 0x20;

	*(int *) (ncgrHeader + 0x8) = fileSize;

	bstreamWrite(stream, ncgrHeader, sizeof(ncgrHeader));
	bstreamWrite(stream, charHeader, sizeof(charHeader));
	ChrWriteGraphics(ncgr, stream);
	return 0;
}

int ChrWriteNcg(NCGR *ncgr, BSTREAM *stream) {
	unsigned char ncgHeader[] = { 'N', 'C', 'C', 'G', 0xFF, 0xFE, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0, 0x1, 0 };
	unsigned char charHeader[] = { 'C', 'H', 'A', 'R', 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char attrHeader[] = { 'A', 'T', 'T', 'R', 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char linkHeader[] = { 'L', 'I', 'N', 'K', 0x08, 0, 0, 0 };
	unsigned char cmntHeader[] = { 'C', 'M', 'N', 'T', 0x08, 0, 0, 0 };

	int charSize = ncgr->nTiles * ncgr->nBits * 8 + sizeof(charHeader);
	int attrSize = ncgr->attrWidth * ncgr->attrHeight + sizeof(attrHeader);
	int linkSize = ncgr->link == NULL ? 0 : (((strlen(ncgr->link) + 4) & ~3) + sizeof(linkHeader));
	int cmntSize = ncgr->comment == NULL ? 0 : (((strlen(ncgr->comment) + 4) & ~3) + sizeof(cmntHeader));
	int totalSize = sizeof(ncgHeader) + charSize + attrSize + linkSize + cmntSize;
	*(uint32_t *) (charHeader + 0x4) = charSize;
	*(uint32_t *) (charHeader + 0x8) = ncgr->tilesX;
	*(uint32_t *) (charHeader + 0xC) = ncgr->tilesY;
	*(uint32_t *) (charHeader + 0x10) = ncgr->nBits == 8;
	*(uint32_t *) (attrHeader + 0x4) = attrSize;
	*(uint32_t *) (attrHeader + 0x8) = ncgr->attrWidth;
	*(uint32_t *) (attrHeader + 0xC) = ncgr->attrHeight;
	*(uint32_t *) (linkHeader + 0x4) = linkSize;
	*(uint32_t *) (cmntHeader + 0x4) = cmntSize;
	*(uint32_t *) (ncgHeader + 0x8) = totalSize;
	*(uint16_t *) (ncgHeader + 0xE) = !!charSize + !!attrSize + !!linkSize + !!cmntSize;
	bstreamWrite(stream, ncgHeader, sizeof(ncgHeader));
	bstreamWrite(stream, charHeader, sizeof(charHeader));
	ChrWriteChars(ncgr, stream); //NCG doesn't store bitmap graphics
	if (ncgr->tilesX == ncgr->attrWidth && ncgr->tilesY == ncgr->attrHeight) {
		bstreamWrite(stream, attrHeader, sizeof(attrHeader));
		bstreamWrite(stream, ncgr->attr, ncgr->attrWidth * ncgr->attrHeight);
	} else {
		unsigned char *dummy = (unsigned char *) calloc(ncgr->tilesX * ncgr->tilesY, 1);
		*(uint32_t *) (attrHeader + 0x8) = ncgr->tilesX;
		*(uint32_t *) (attrHeader + 0xC) = ncgr->tilesY;
		bstreamWrite(stream, attrHeader, sizeof(attrHeader));
		bstreamWrite(stream, dummy, ncgr->tilesX * ncgr->tilesY);
		free(dummy);
	}
	if (linkSize) {
		bstreamWrite(stream, linkHeader, sizeof(linkHeader));
		bstreamWrite(stream, ncgr->link, linkSize - sizeof(linkHeader));
	}
	if (cmntSize) {
		bstreamWrite(stream, cmntHeader, sizeof(cmntHeader));
		bstreamWrite(stream, ncgr->comment, cmntSize - sizeof(cmntHeader));
	}
	return 0;
}

int ChrWriteAcg(NCGR *ncgr, BSTREAM *stream) {
	int attrSize = ncgr->attrWidth * ncgr->attrHeight;

	ChrWriteChars(ncgr, stream);

	//write attribute for ACG
	for (int i = 0; i < attrSize; i++) {
		//bstreamWrite(stream, ncgr->attr, attrSize);
		unsigned char a = ncgr->attr[i];
		a |= 0x20; //exists
		a |= (ncgr->nBits == 8) ? 0x10 : 0x00;
		bstreamWrite(stream, &a, sizeof(a));
	}

	//footer
	unsigned char linkFooter[] = { 'L', 'I', 'N', 'K', 2, 0, 0, 0, 1, 0 };
	unsigned char cmntFooter[] = { 'C', 'M', 'N', 'T', 2, 0, 0, 0, 1, 0 };
	unsigned char modeFooter[] = { 'M', 'O', 'D', 'E', 4, 0, 0, 0, 1, 0, 0, 0 };
	unsigned char sizeFooter[] = { 'S', 'I', 'Z', 'E', 4, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char verFooter[] = { 'V', 'E', 'R', ' ', 0, 0, 0, 0 };
	unsigned char endFooter[] = { 'E', 'N', 'D', ' ', 0, 0, 0, 0 };

	char *version = "IS-ACG03";
	int linkLen = (ncgr->link == NULL) ? 0 : strlen(ncgr->link);
	int commentLen = (ncgr->comment == NULL) ? 0 : strlen(ncgr->comment);

	*(uint32_t *) (linkFooter + 4) = linkLen + 2;
	linkFooter[9] = (unsigned char) linkLen;
	*(uint32_t *) (cmntFooter + 4) = commentLen + 2;
	cmntFooter[9] = (unsigned char) commentLen;
	*(uint32_t *) (modeFooter + 8) = (ncgr->nBits == 8) ? 3 : 1;
	*(uint16_t *) (sizeFooter + 8) = ncgr->tilesX;
	*(uint16_t *) (sizeFooter + 10) = ncgr->tilesY;
	*(uint32_t *) (verFooter + 4) = strlen(version);

	bstreamWrite(stream, linkFooter, sizeof(linkFooter));
	bstreamWrite(stream, ncgr->link, linkLen);
	bstreamWrite(stream, cmntFooter, sizeof(cmntFooter));
	bstreamWrite(stream, ncgr->comment, commentLen);
	bstreamWrite(stream, modeFooter, sizeof(modeFooter));
	bstreamWrite(stream, sizeFooter, sizeof(sizeFooter));
	bstreamWrite(stream, verFooter, sizeof(verFooter));
	bstreamWrite(stream, version, strlen(version));
	bstreamWrite(stream, endFooter, sizeof(endFooter));
	return 0;
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
		case NCGR_TYPE_HUDSON:
		case NCGR_TYPE_HUDSON2:
			return ChrWriteHudson(ncgr, stream);
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
