#include "ncgr.h"
#include "nclr.h"
#include "nscr.h"
#include "color.h"
#include <stdio.h>

LPCWSTR characterFormatNames[] = { L"Invalid", L"NCGR", L"Hudson", L"Hudson 2", L"NCBR", L"Binary", NULL };

int calculateWidth(int nTiles) {
	int width = 1;
	for (int i = 1; i < nTiles; i++) {
		if (i * i > nTiles) break;
		if (nTiles % i == 0) width = i;
	}
	return width;
}

int ncgrIsValidHudson(LPBYTE buffer, int size) {
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

int ncgrIsValidBin(LPBYTE buffer, int size) {
	if (size & 0x1F) return 0;
	return NCGR_TYPE_BIN;
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

	COMBO2D *combo = ncgr->combo2d;
	if (ncgr->combo2d != NULL) {
		ncgr->combo2d->ncgr = NULL;
		if (combo->nclr == NULL && combo->ncgr == NULL && combo->nscr == NULL) free(combo);
	}
	ncgr->combo2d = NULL;
}

int hudsonReadCharacter(NCGR *ncgr, char *buffer, int size) {
	if (size < 8) return 1; //file too small
	if (*buffer == 0x10) return 1; //TODO: LZ77 decompress
	int type = ncgrIsValidHudson(buffer, size);
	if (type == NCGR_TYPE_INVALID) return 1;

	int nCharacters = 0;
	if (type == NCGR_TYPE_HUDSON) {

		int dataLength = *(WORD *) (buffer + 1);
		dataLength -= 4;
		if (dataLength + 8 > size) return 1;

		nCharacters = *(WORD *) (buffer + 5);
	} else if (type == NCGR_TYPE_HUDSON2) {
		nCharacters = *(WORD *) (buffer + 1);
	}

	ncgr->header.type = FILE_TYPE_CHARACTER;
	ncgr->header.format = type;
	ncgr->header.size = sizeof(*ncgr);
	ncgr->header.compression = COMPRESSION_NONE;
	ncgr->header.dispose = ncgrFree;
	ncgr->nTiles = nCharacters;
	ncgr->tileWidth = 8;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->nBits = 8;
	ncgr->tilesX = -1;
	ncgr->tilesY = -1;
	ncgr->combo2d = NULL;

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

	BYTE ** tiles = (BYTE **) calloc(tileCount, sizeof(BYTE **));
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

int ncgrReadCombo(NCGR *ncgr, char *buffer, int size) {
	ncgr->header.compression = COMPRESSION_NONE;
	ncgr->header.dispose = ncgrFree;
	ncgr->header.size = sizeof(NCGR);
	ncgr->header.type = FILE_TYPE_CHARACTER;
	ncgr->header.format = NCGR_TYPE_COMBO;
	ncgr->nTiles = *(int *) (buffer + 0xA08);
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_2D;
	ncgr->nBits = *(int *) buffer == 0 ? 4 : 8;
	ncgr->tilesX = calculateWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ncgr->combo2d = NULL;

	int nTiles = ncgr->nTiles;
	BYTE **tiles = (BYTE **) calloc(nTiles, sizeof(BYTE *));
	for (int i = 0; i < nTiles; i++) {
		BYTE *tile = (BYTE *) calloc(64, 1);
		tiles[i] = tile;

		if (ncgr->nBits == 8) {
			memcpy(tile, buffer + 0xA0C + i * 0x40, 0x40);
		} else {
			BYTE *src = buffer + 0xA0C + i * 0x20;
			for (int j = 0; j < 32; j++) {
				BYTE b = src[j];
				tile[j * 2] = b & 0xF;
				tile[j * 2 + 1] = b >> 4;
			}
		}
	}


	ncgr->tiles = tiles;
	return 0;
}

int ncgrReadBin(NCGR *ncgr, char *buffer, int size) {
	ncgr->header.compression = COMPRESSION_NONE;
	ncgr->header.format = NCGR_TYPE_BIN;
	ncgr->header.size = sizeof(NCGR);
	ncgr->header.type = FILE_TYPE_CHARACTER;
	ncgr->header.dispose = ncgrFree;
	ncgr->nTiles = size / 0x20;
	ncgr->nBits = 4;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->tileWidth = 8;
	ncgr->tilesX = calculateWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ncgr->combo2d = NULL;

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

int ncgrRead(NCGR *ncgr, char *buffer, int size) {
	if (*(DWORD *) buffer != 0x4E434752) {
		if (ncgrIsValidHudson(buffer, size)) return hudsonReadCharacter(ncgr, buffer, size);
		if (ncgrIsValidBin(buffer, size)) return ncgrReadBin(ncgr, buffer, size);
		if (combo2dIsValid(buffer, size)) return ncgrReadCombo(ncgr, buffer, size);
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
							tile[i * 2 + j * 8] = indices[i + j * 4] & 0xF;
							tile[i * 2 + 1 + j * 8] = indices[i + j * 4] >> 4;
						}
					}
				}

			}
		}
	}


	//_asm int 3
	ncgr->nBits = depth;
	ncgr->nTiles = tileCount;
	ncgr->tiles = tiles;
	ncgr->tileWidth = 8;
	ncgr->tilesX = tilesX;
	ncgr->tilesY = tilesY;
	ncgr->mappingMode = mapping;
	ncgr->combo2d = NULL;
	ncgr->header.type = FILE_TYPE_CHARACTER;
	ncgr->header.format = format;
	ncgr->header.size = sizeof(*ncgr);
	ncgr->header.compression = COMPRESSION_NONE;
	ncgr->header.dispose = ncgrFree;
	return 0;

}

int ncgrReadFile(NCGR *ncgr, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) ncgr, (OBJECT_READER) ncgrRead);
}

int ncgrGetTile(NCGR * ncgr, NCLR * nclr, int x, int y, DWORD * out, int previewPalette, BOOL drawChecker, BOOL transparent) {
	int nIndex = x + y * ncgr->tilesX;
	BYTE * tile = ncgr->tiles[nIndex];
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

int ncgrWrite(NCGR *ncgr, BSTREAM *stream) {
	int status = 0;

	if (ncgr->header.format == NCGR_TYPE_NCGR || ncgr->header.format == NCGR_TYPE_NCBR) {
		BYTE ncgrHeader[] = { 'R', 'G', 'C', 'N', 0xFF, 0xFE, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0, 0x1, 0 };
		BYTE charHeader[] = { 'R', 'A', 'H', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
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
			for (int i = 0; i < ncgr->nTiles; i++) {
				if (ncgr->nBits == 8) {
					bstreamWrite(stream, ncgr->tiles[i], 64);
				} else {
					BYTE buffer[32];
					for (int j = 0; j < 32; j++) {
						BYTE b = ncgr->tiles[i][(j << 1)] | (ncgr->tiles[i][(j << 1) + 1] << 4);

						buffer[j] = b;
					}
					bstreamWrite(stream, buffer, 32);
				}
			}
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
	} else if(ncgr->header.format == NCGR_TYPE_HUDSON || ncgr->header.format == NCGR_TYPE_HUDSON2) {

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

		for (int i = 0; i < ncgr->nTiles; i++) {
			if (ncgr->nBits == 8) {
				bstreamWrite(stream, ncgr->tiles[i], 64);
			} else {
				BYTE buffer[32];
				for (int j = 0; j < 32; j++) {
					BYTE b = ncgr->tiles[i][(j << 1)] | (ncgr->tiles[i][(j << 1) + 1] << 4);

					buffer[j] = b;
				}
				bstreamWrite(stream, buffer, 32);
			}
		}
	} else if (ncgr->header.format == NCGR_TYPE_BIN) {
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
	} else if (ncgr->header.format == NCGR_TYPE_COMBO) {
		status = combo2dWrite(ncgr->combo2d, stream);
	}

	return status;
}

int ncgrWriteFile(NCGR *ncgr, LPWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) ncgr, (OBJECT_WRITER) ncgrWrite);
}

void ncgrCreate(DWORD * blocks, int nBlocks, int nBits, LPWSTR name, int fmt) {

	int nBlockSize = 64;
	if (nBits == 4) nBlockSize = 32;
	//_asm int 3
	BYTE * b = (BYTE *) HeapAlloc(GetProcessHeap(), 0, nBlocks * nBlockSize);
	for (int i = 0; i < nBlocks * nBlockSize; i++) {
		if(nBits == 8) b[i] = (BYTE) blocks[i];
		else {
			b[i] = (BYTE) (blocks[i * 2] | (blocks[i * 2 + 1] << 4));
		}
	}

	if (fmt == 0) {
		BYTE ncgrHeader[] = { 'R', 'G', 'C', 'N', 0xFF, 0xFE, 1, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
		BYTE rahcHeader[] = { 'R', 'A', 'H', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x18, 0, 0, 0 };
		//BYTE sopcHeader[] = {'S', 'O', 'P', 'C', 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

		int dataSize = nBlocks * nBlockSize;
		int rahcSize = dataSize + 0x20;
		int fileSize = rahcSize + 0x10;

		int tilesX = 0, tilesY = 0;

		{
			tilesX = 1, tilesY = nBlocks;
			for (int i = 1; i < nBlocks; i++) {
				if (nBlocks % i) continue;
				int other = nBlocks / i;
				if (i > other) {
					tilesX = i;
					tilesY = other;
					break;
				}
			}
		}


		*(int *) (rahcHeader + 0x4) = rahcSize;
		*(int *) (rahcHeader + 0x8) = tilesY | (tilesX << 16);
		*(int *) (rahcHeader + 0xC) = nBits == 8 ? 4 : 3;
		*(int *) (rahcHeader + 0x10) = 0;
		*(int *) (rahcHeader + 0x18) = dataSize;

		*(int *) (ncgrHeader + 0x8) = fileSize;

		//*(int *) (sopcHeader + 0xC) = tilesX | (tilesY << 16);

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, ncgrHeader, sizeof(ncgrHeader), &dwWritten, NULL);
		WriteFile(hFile, rahcHeader, sizeof(rahcHeader), &dwWritten, NULL);
		WriteFile(hFile, b, nBlocks * nBlockSize, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if(fmt == 1 || fmt == 2) {

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fmt == 1) {
			BYTE header[8] = { 0 };
			*(WORD *) (header + 1) = nBlocks * nBlockSize + 4;
			if(nBits == 8) header[4] = 1;
			*(WORD *) (header + 5) = nBlocks;
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		} else {
			BYTE header[4] = { 0 };
			*(WORD *) (header + 1) = nBlocks;
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		}
		WriteFile(hFile, b, nBlocks * nBlockSize, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if (fmt == 3 || fmt == 4) {
		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, b, nBlocks * nBlockSize, &dwWritten, NULL);
		CloseHandle(hFile);
		if (fmt == 4) fileCompress(name, COMPRESSION_LZ77);
	}

	HeapFree(GetProcessHeap(), 0, b);
}