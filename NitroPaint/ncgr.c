#include "ncgr.h"
#include "nclr.h"
#include "nscr.h"
#include "color.h"
#include "g2dfile.h"

#include <stdio.h>

LPCWSTR characterFormatNames[] = { L"Invalid", L"NCGR", L"Hudson", L"Hudson 2", L"NCBR", L"Binary", L"NCG", L"ACG", NULL };

int calculateWidth(int nTiles) {
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

int ncgrIsValidHudson(unsigned char *buffer, unsigned int size) {
	if (size < 8) return 0;
	if (*buffer == 0x10) return 0;
	if (((*buffer) & 0xF0) != 0) return 0;
	int dataLength = *(WORD *) (buffer + 1);
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

int ncgrIsValidBin(unsigned char *buffer, unsigned int size) {
	if (size & 0x1F) return 0;
	return NCGR_TYPE_BIN;
}

int ncgrIsValidNcg(unsigned char *buffer, unsigned int size) {
	if (!g2dIsValid(buffer, size)) return 0;

	char *sChar = g2dGetSectionByMagic(buffer, size, 'CHAR');
	if (sChar == NULL) sChar = g2dGetSectionByMagic(buffer, size, 'RAHC');
	if (sChar == NULL) return 0;
	return 1;
}

int ncgrAcgScanFooter(unsigned char *buffer, unsigned int size) {
	//scan for possible locations of the footer
	for (unsigned int i = 0; i < size - 8; i++) {
		if (buffer[i] != 'L') continue;
		if (memcmp(buffer + i, "LINK", 4) != 0) continue;
		
		//candidate location
		int hasLink = 0, hasCmnt = 0, hasMode = 0, hasSize = 0, hasVer = 0, hasEnd = 0;

		//scan sections
		unsigned int offset = i;
		while (1) {
			char *section = buffer + offset;
			unsigned int length = *(unsigned int *) (buffer + offset + 4);
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

int ncgrIsValidAcg(unsigned char *buffer, unsigned int size) {
	int dataOffset = ncgrAcgScanFooter(buffer, size);
	if (dataOffset == -1) return 0;

	return 1;
}

void ncgrFree(OBJECT_HEADER *header) {
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
	if (ncgr->combo2d != NULL) {
		ncgr->combo2d->ncgr = NULL;
		if (combo->nclr == NULL && combo->ncgr == NULL && combo->nscr == NULL) {
			combo2dFree(combo);
			free(combo);
		}
	}
	ncgr->combo2d = NULL;
}

void ncgrInit(NCGR *ncgr, int format) {
	ncgr->header.size = sizeof(NCGR);
	fileInitCommon((OBJECT_HEADER *) ncgr, FILE_TYPE_CHARACTER, format);
	ncgr->header.dispose = ncgrFree;
	ncgr->combo2d = NULL;
}

int hudsonReadCharacter(NCGR *ncgr, unsigned char *buffer, unsigned int size) {
	if (size < 8) return 1; //file too small
	if (*buffer == 0x10) return 1; //TODO: LZ77 decompress
	int type = ncgrIsValidHudson(buffer, size);
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

	ncgrInit(ncgr, type);
	ncgr->nTiles = nCharacters;
	ncgr->tileWidth = 8;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->nBits = 8;
	ncgr->tilesX = -1;
	ncgr->tilesY = -1;

	if (type == NCGR_TYPE_HUDSON) {
		if (buffer[4] == 0) {
			ncgr->nBits = 4;
		}
	} else if (type == NCGR_TYPE_HUDSON2) {
		ncgr->nBits = 4;
	}

	int tileCount = nCharacters;
	int tilesX, tilesY;
	tilesX = calculateWidth(nCharacters);
	tilesY = tileCount / tilesX;

	BYTE **tiles = (BYTE **) calloc(tileCount, sizeof(BYTE **));
	buffer += 0x4;
	if (type == NCGR_TYPE_HUDSON) buffer += 0x4;
	for (int i = 0; i < tileCount; i++) {
		tiles[i] = (BYTE *) calloc(8 * 8, 1);
		BYTE * tile = tiles[i];
		if (ncgr->nBits == 8) {
			memcpy(tile, buffer, 64);
			buffer += 64;
		} else if (ncgr->nBits == 4) {
			for (int j = 0; j < 32; j++) {
				BYTE b = *buffer;
				tile[j * 2] = b & 0xF;
				tile[j * 2 + 1] = b >> 4;
				buffer++;
			}
		}
	}

	ncgr->tilesX = tilesX;
	ncgr->tilesY = tilesY;
	ncgr->tiles = tiles;

	return 0;
}

int ncgrReadAcg(NCGR *ncgr, unsigned char *buffer, unsigned int size) {
	int footerOffset = ncgrAcgScanFooter(buffer, size);

	int width = 0, height = 0, depth = 4;

	//process extra data
	unsigned int offset = (unsigned int) footerOffset;
	while (1) {
		char *section = buffer + offset;
		unsigned int len = *(unsigned int *) (section + 4);
		unsigned char *sectionData = section + 8;

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
	unsigned char *attr = buffer + (nChars * 8 * depth);

	ncgrInit(ncgr, NCGR_TYPE_AC);
	ncgr->tilesX = width;
	ncgr->tilesY = height;
	ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
	ncgr->tileWidth = 8;
	ncgr->nBits = depth;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;

	BYTE **tiles = (BYTE **) calloc(nChars, sizeof(BYTE **));
	for (int i = 0; i < nChars; i++) {
		tiles[i] = (BYTE *) calloc(8 * 8, 1);
		BYTE * tile = tiles[i];
		if (ncgr->nBits == 8) {
			memcpy(tile, buffer, 64);
			buffer += 64;
		} else if (ncgr->nBits == 4) {
			for (int j = 0; j < 32; j++) {
				BYTE b = *buffer;
				tile[j * 2] = b & 0xF;
				tile[j * 2 + 1] = b >> 4;
				buffer++;
			}
		}
	}
	ncgr->tiles = tiles;

	//attr
	ncgr->attrWidth = width;
	ncgr->attrHeight = height;
	ncgr->attr = (unsigned char *) calloc(width * height, 1);
	for (int i = 0; i < width * height; i++) {
		ncgr->attr[i] = attr[i] & 0xF;
	}
	return 0;
}

int ncgrReadCombo(NCGR *ncgr, unsigned char *buffer, unsigned int size) {
	int format = combo2dIsValid(buffer, size);
	ncgrInit(ncgr, NCGR_TYPE_COMBO);

	char *charSrc = NULL;
	int freeChar = 0; //should free buffer?
	switch (format) {
		case COMBO2D_TYPE_TIMEACE:
		{
			ncgr->nTiles = *(int *) (buffer + 0xA08);
			ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_2D;
			ncgr->nBits = *(int *) buffer == 0 ? 4 : 8;
			ncgr->tilesX = calculateWidth(ncgr->nTiles);
			ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
			charSrc = buffer + 0xA0C;
			break;
		}
		case COMBO2D_TYPE_BANNER:
		{
			ncgr->nTiles = 16;
			ncgr->tileWidth = 8;
			ncgr->tilesX = 4;
			ncgr->tilesY = 4;
			ncgr->nBits = 4;
			ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
			charSrc = buffer + 0x20;
			break;
		}
		case COMBO2D_TYPE_5BG:
		{
			char *bgdt = g2dGetSectionByMagic(buffer, size, 'BGDT');
			if (bgdt == NULL) bgdt = g2dGetSectionByMagic(buffer, size, 'TDGB');
			char *dfpl = g2dGetSectionByMagic(buffer, size, 'DFPL');
			if (dfpl == NULL) dfpl = g2dGetSectionByMagic(buffer, size, 'LPFD');

			int chrWidth = *(uint16_t *) (bgdt + 0x14);
			int chrHeight = *(uint16_t *) (bgdt + 0x16);
			int scrSize = *(uint32_t *) (bgdt + 0x0C);
			int mapping = *(uint32_t *) (bgdt + 0x08);
			int charSize = *(uint32_t *) (bgdt + 0x18);
			int charOffset = (bgdt - buffer) + 0x1C + scrSize;
			int nBits = dfpl == NULL ? 8 : 4; //8-bit if no DFPL present
			charSrc = (char *) calloc(chrWidth * chrHeight, 8 * nBits);
			freeChar = 1; //since we allocated the buffer

			//copy from file up to the size stored, the remainder are left empty
			memcpy(charSrc, buffer + charOffset, charSize);

			ncgr->nTiles = chrWidth * chrHeight;
			ncgr->tilesX = chrWidth;
			ncgr->tilesY = chrHeight;
			ncgr->nBits = nBits;
			ncgr->mappingMode = mapping;
			break;
		}
	}

	int nTiles = ncgr->nTiles;
	BYTE **tiles = (BYTE **) calloc(nTiles, sizeof(BYTE *));
	for (int i = 0; i < nTiles; i++) {
		BYTE *tile = (BYTE *) calloc(64, 1);
		tiles[i] = tile;

		if (ncgr->nBits == 8) {
			memcpy(tile, charSrc + i * 0x40, 0x40);
		} else {
			BYTE *src = charSrc + i * 0x20;
			for (int j = 0; j < 32; j++) {
				BYTE b = src[j];
				tile[j * 2] = b & 0xF;
				tile[j * 2 + 1] = b >> 4;
			}
		}
	}
	ncgr->tiles = tiles;
	if (freeChar) free(charSrc);

	return 0;
}

int ncgrReadBin(NCGR *ncgr, unsigned char *buffer, unsigned int size) {
	ncgrInit(ncgr, NCGR_TYPE_BIN);
	ncgr->nTiles = size / 0x20;
	ncgr->nBits = 4;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tileWidth = 8;
	ncgr->tilesX = calculateWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;

	BYTE **tiles = (BYTE **) calloc(ncgr->nTiles, sizeof(BYTE **));
	for (int i = 0; i < ncgr->nTiles; i++) {
		BYTE *tile = (BYTE *) calloc(64, 1);
		for (int j = 0; j < 32; j++) {
			BYTE b = *buffer;
			tile[j * 2] = b & 0xF;
			tile[j * 2 + 1] = b >> 4;
			buffer++;
		}
		tiles[i] = tile;
	}
	ncgr->tiles = tiles;

	return 0;
}

int ncgrReadNcg(NCGR *ncgr, unsigned char *buffer, unsigned int size) {
	ncgrInit(ncgr, NCGR_TYPE_NC);

	unsigned char *sChar = g2dGetSectionByMagic(buffer, size, 'CHAR');
	if (sChar == NULL) sChar = g2dGetSectionByMagic(buffer, size, 'RAHC');
	unsigned char *sAttr = g2dGetSectionByMagic(buffer, size, 'ATTR');
	if (sAttr == NULL) sAttr = g2dGetSectionByMagic(buffer, size, 'RTTA');
	unsigned char *sLink = g2dGetSectionByMagic(buffer, size, 'LINK');
	if (sLink == NULL) sLink = g2dGetSectionByMagic(buffer, size, 'KNIL');
	unsigned char *sCmnt = g2dGetSectionByMagic(buffer, size, 'CMNT');
	if (sCmnt == NULL) sCmnt = g2dGetSectionByMagic(buffer, size, 'TNMC');
	
	ncgr->nBits = *(uint32_t *) (sChar + 0x10) == 0 ? 4 : 8;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tilesX = *(uint32_t *) (sChar + 0x8);
	ncgr->tilesY = *(uint32_t *) (sChar + 0xC);
	ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
	ncgr->tileWidth = 8;

	BYTE **tiles = (BYTE **) calloc(ncgr->nTiles, sizeof(BYTE **));
	buffer = sChar + 0x14;
	for (int i = 0; i < ncgr->nTiles; i++) {
		tiles[i] = (BYTE *) calloc(8 * 8, 1);
		BYTE *tile = tiles[i];
		if (ncgr->nBits == 8) {
			memcpy(tile, buffer, 64);
			buffer += 64;
		} else if (ncgr->nBits == 4) {
			for (int j = 0; j < 32; j++) {
				BYTE b = *buffer;
				tile[j * 2] = b & 0xF;
				tile[j * 2 + 1] = b >> 4;
				buffer++;
			}
		}
	}
	ncgr->tiles = tiles;

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

int ncgrRead(NCGR *ncgr, unsigned char *buffer, unsigned int size) {
	if (*(DWORD *) buffer != 0x4E434752) {
		if (ncgrIsValidNcg(buffer, size)) return ncgrReadNcg(ncgr, buffer, size);
		if (ncgrIsValidAcg(buffer, size)) return ncgrReadAcg(ncgr, buffer, size);
		if (ncgrIsValidHudson(buffer, size)) return hudsonReadCharacter(ncgr, buffer, size);
		if (combo2dIsValid(buffer, size)) return ncgrReadCombo(ncgr, buffer, size);
		if (ncgrIsValidBin(buffer, size)) return ncgrReadBin(ncgr, buffer, size);
	}
	if (size < 0x10) return 1;
	DWORD magic = *(DWORD *) buffer;
	if (magic != 0x4E434752 && magic != 0x5247434E) return 1;

	buffer += 0x10;
	int rahcSize = *(int *) buffer;
	unsigned short tilesY = *(unsigned short *) (buffer + 0x8);
	unsigned short tilesX = *(unsigned short *) (buffer + 0xA);
	int depth = *(int *) (buffer + 0xC);
	int mapping = *(int *) (buffer + 0x10);
	depth = 1 << (depth - 1);
	int tileDataSize = *(int *) (buffer + 0x18);
	int type = *(int *) (buffer + 0x14);

	int format = NCGR_TYPE_NCGR;
	if (type == 1) {
		format = NCGR_TYPE_NCBR;
	}


	int tileCount = tilesX * tilesY;
	int nPresentTiles = tileDataSize >> 5;
	if (depth == 8) nPresentTiles >>= 1;
	if (NCGR_1D(mapping) || tileCount != nPresentTiles) {
		tileCount = nPresentTiles;
		tilesX = calculateWidth(tileCount);
		tilesY = tileCount / tilesX;
	}

	BYTE **tiles = (BYTE **) calloc(tileCount, sizeof(BYTE **));
	buffer += 0x20;

	if (format == NCGR_TYPE_NCGR) {
		for (int i = 0; i < tileCount; i++) {
			tiles[i] = (BYTE *) calloc(8 * 8, 1);
			BYTE * tile = tiles[i];
			if (depth == 8) {
				memcpy(tile, buffer, 64);
				buffer += 64;
			} else if (depth == 4) {
				for (int j = 0; j < 32; j++) {
					BYTE b = *buffer;
					tile[j * 2] = b & 0xF;
					tile[j * 2 + 1] = b >> 4;
					buffer++;
				}
			}
		}
	} else if (format == NCGR_TYPE_NCBR) {
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {

				int offset = x * 4 + 4 * y * tilesX * 8;
				BYTE *tile = calloc(64, 1);
				tiles[x + y * tilesX] = tile;
				if (depth == 8) {
					offset *= 2;
					BYTE *indices = buffer + offset;
					memcpy(tile, indices, 8);
					memcpy(tile + 8, indices + 8 * tilesX, 8);
					memcpy(tile + 16, indices + 16 * tilesX, 8);
					memcpy(tile + 24, indices + 24 * tilesX, 8);
					memcpy(tile + 32, indices + 32 * tilesX, 8);
					memcpy(tile + 40, indices + 40 * tilesX, 8);
					memcpy(tile + 48, indices + 48 * tilesX, 8);
					memcpy(tile + 56, indices + 56 * tilesX, 8);
				} else if (depth == 4) {
					BYTE *indices = buffer + offset;
					for (int j = 0; j < 8; j++) {
						for (int i = 0; i < 4; i++) {
							tile[i * 2 + j * 8] = indices[i + j * 4 * tilesX] & 0xF;
							tile[i * 2 + 1 + j * 8] = indices[i + j * 4 * tilesX] >> 4;
						}
					}
				}

			}
		}
	}


	ncgrInit(ncgr, format);
	ncgr->nBits = depth;
	ncgr->nTiles = tileCount;
	ncgr->tiles = tiles;
	ncgr->tileWidth = 8;
	ncgr->tilesX = tilesX;
	ncgr->tilesY = tilesY;
	ncgr->mappingMode = mapping;
	return 0;

}

int ncgrReadFile(NCGR *ncgr, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) ncgr, (OBJECT_READER) ncgrRead);
}

int ncgrGetTile(NCGR *ncgr, NCLR *nclr, int x, int y, COLOR32 *out, int previewPalette, int drawChecker, int transparent) {
	int nIndex = x + y * ncgr->tilesX;
	BYTE *tile = ncgr->tiles[nIndex];
	int nTiles = ncgr->nTiles;
	if (x + y * ncgr->tilesX < nTiles) {
		for (int i = 0; i < 64; i++) {
			int index = tile[i];
			if ((index == 0 && drawChecker) && transparent) {
				int c = ((i & 0x7) ^ (i >> 3)) >> 2;
				if (c) out[i] = 0xFFFFFFFF;
				else out[i] = 0xFFC0C0C0;
			} else if (index || !transparent) {
				COLOR w = 0;
				if (nclr && (index + (previewPalette << ncgr->nBits)) < nclr->nColors)
					w = nclr->colors[index + (previewPalette << ncgr->nBits)];
				out[i] = ColorConvertFromDS(CREVERSE(w)) | 0xFF000000;
			} else out[i] = 0;
		}
	} else {
		if (!drawChecker) {
			memset(out, 0, 64 * 4);
		} else {
			for (int i = 0; i < 64; i++) {
				int c = ((i & 0x7) ^ (i >> 3)) >> 2;
				if (c) out[i] = 0xFFFFFFFF;
				else out[i] = 0xFFC0C0C0;
			}
		}
		return 1;
	}
	return 0;
}

void ncgrChangeWidth(NCGR *ncgr, int width) {
	//unimplemented right now
	if (ncgr->nTiles % width) return;

	//only matters for bitmap graphics
	if (ncgr->header.format != NCGR_TYPE_NCBR) {
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

int ncgrWriteBin(NCGR *ncgr, BSTREAM *stream) {
	for (int i = 0; i < ncgr->nTiles; i++) {
		if (ncgr->nBits == 8) {
			bstreamWrite(stream, ncgr->tiles[i], 64);
		} else {
			BYTE t[32];
			for (int j = 0; j < 32; j++) {
				t[j] = ncgr->tiles[i][j * 2] | (ncgr->tiles[i][j * 2 + 1] << 4);
			}
			bstreamWrite(stream, t, 32);
		}
	}
	return 0;
}

int ncgrWriteNcgr(NCGR *ncgr, BSTREAM *stream) {
	unsigned char ncgrHeader[] = { 'R', 'G', 'C', 'N', 0xFF, 0xFE, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0, 0x1, 0 };
	unsigned char charHeader[] = { 'R', 'A', 'H', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	if (ncgr->header.format == NCGR_TYPE_NCBR) {
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
	if (ncgr->header.format == NCGR_TYPE_NCGR) {
		ncgrWriteBin(ncgr, stream);
	} else if (ncgr->header.format == NCGR_TYPE_NCBR) {
		BYTE *bmp = (BYTE *) calloc(nTiles, 8 * ncgr->nBits);
		int nWidth = ncgr->tilesX * 8;
		for (int y = 0; y < ncgr->tilesY; y++) {
			for (int x = 0; x < ncgr->tilesX; x++) {
				BYTE *tile = ncgr->tiles[x + y * ncgr->tilesX];
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
	return 0;
}

int ncgrWriteNcg(NCGR *ncgr, BSTREAM *stream) {
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
	ncgrWriteBin(ncgr, stream);
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

int ncgrWriteAcg(NCGR *ncgr, BSTREAM *stream) {
	int attrSize = ncgr->attrWidth * ncgr->attrHeight;

	ncgrWriteBin(ncgr, stream);

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

int ncgrWriteHudson(NCGR *ncgr, BSTREAM *stream) {
	if (ncgr->header.format == NCGR_TYPE_HUDSON) {
		BYTE header[] = { 0, 0, 0, 0, 1, 0, 0, 0 };
		if (ncgr->nBits == 4) header[4] = 0;
		*(WORD *) (header + 5) = ncgr->nTiles;
		int nCharacterBytes = 64 * ncgr->nTiles;
		if (ncgr->nBits == 4) nCharacterBytes >>= 1;
		*(WORD *) (header + 1) = nCharacterBytes + 4;

		bstreamWrite(stream, header, sizeof(header));
	} else if(ncgr->header.format == NCGR_TYPE_HUDSON2) {
		BYTE header[] = { 0, 0, 0, 0 };
		*(WORD *) (header + 1) = ncgr->nTiles;
		bstreamWrite(stream, header, sizeof(header));
	}

	ncgrWriteBin(ncgr, stream);
	return 0;
}

int ncgrWriteCombo(NCGR *ncgr, BSTREAM *stream) {
	return combo2dWrite(ncgr->combo2d, stream);
}

int ncgrWrite(NCGR *ncgr, BSTREAM *stream) {
	switch (ncgr->header.format) {
		case NCGR_TYPE_NCGR:
		case NCGR_TYPE_NCBR:
			return ncgrWriteNcgr(ncgr, stream);
		case NCGR_TYPE_NC:
			return ncgrWriteNcg(ncgr, stream);
		case NCGR_TYPE_AC:
			return ncgrWriteAcg(ncgr, stream);
		case NCGR_TYPE_HUDSON:
		case NCGR_TYPE_HUDSON2:
			return ncgrWriteHudson(ncgr, stream);
		case NCGR_TYPE_BIN:
			return ncgrWriteBin(ncgr, stream);
		case NCGR_TYPE_COMBO:
			return ncgrWriteCombo(ncgr, stream);
	}
	return 1;
}

int ncgrWriteFile(NCGR *ncgr, LPCWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) ncgr, (OBJECT_WRITER) ncgrWrite);
}
