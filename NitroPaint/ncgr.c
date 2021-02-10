#include "ncgr.h"
#include "nclr.h"
#include "nscr.h"
#include "color.h"
#include <stdio.h>

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
	if (*buffer != 0) return 0;
	int dataLength = *(WORD *) (buffer + 1);
	if (buffer[3] != 0) return 0;
	if (dataLength * 32 + 4 == size) {
		//no second header
		return NCGR_TYPE_HUDSON2;
	}
	if (buffer[4] != 1 && buffer[4] != 0) return 0;
	dataLength -= 4;
	
	if (dataLength + 8 != size) return 0;
	return NCGR_TYPE_HUDSON;
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
	ncgr->nTiles = nCharacters;
	ncgr->tileWidth = 8;
	ncgr->mapping = 0x10;
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

int ncgrRead(NCGR *ncgr, char *buffer, int size) {
	if (*buffer == 0x10 || *buffer == 0x00) return hudsonReadCharacter(ncgr, buffer, size);
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


	int tileCount = tilesX * tilesY;
	if (mapping == 0x10) {
		tileCount = tileDataSize >> 6;
		if (depth == 4) tileCount <<= 1;
		tilesX = calculateWidth(tileCount);
		tilesY = tileCount / tilesX;
	}

	BYTE ** tiles = (BYTE **) calloc(tileCount, sizeof(BYTE **));
	buffer += 0x20;
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


	//_asm int 3
	ncgr->nBits = depth;
	ncgr->nTiles = tileCount;
	ncgr->tiles = tiles;
	ncgr->tileWidth = 8;
	ncgr->tilesX = tilesX;
	ncgr->tilesY = tilesY;
	ncgr->mapping = mapping;
	ncgr->header.type = FILE_TYPE_CHARACTER;
	ncgr->header.format = NCGR_TYPE_NCGR;
	ncgr->header.size = sizeof(*ncgr);
	ncgr->header.compression = COMPRESSION_NONE;
	return 0;

}

int ncgrReadFile(NCGR *ncgr, LPWSTR path) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);
	int n = ncgrRead(ncgr, lpBuffer, dwSize);
	free(lpBuffer);
	return n;
}

int ncgrGetTile(NCGR * ncgr, NCLR * nclr, int x, int y, DWORD * out, int previewPalette, BOOL drawChecker) {
	if (x + y * ncgr->tilesX >= ncgr->nTiles) return 1;
	BYTE * tile = ncgr->tiles[x + y * ncgr->tilesX];
	int nTiles = ncgr->nTiles;
	for (int i = 0; i < 64; i++) {
		int index = tile[i];
		if (index == 0 && drawChecker) {
			int c = ((i & 0x7) ^ (i >> 3)) >> 2;
			if (c) out[i] = 0xFFFFFFFF;
			else out[i] = 0xFFC0C0C0;
		} else if(index) {
			COLOR w = 0;
			if(nclr && (index + (previewPalette << ncgr->nBits)) < nclr->nColors)
				w = nclr->colors[index + (previewPalette << ncgr->nBits)];
			out[i] = ColorConvertFromDS(CREVERSE(w)) | 0xFF000000;
		} else out[i] = 0;
	}
	return 0;
}

void ncgrWrite(NCGR * ncgr, LPWSTR name) {
	HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (ncgr->header.format == NCGR_TYPE_NCGR) {
		BYTE ncgrHeader[] = { 'R', 'G', 'C', 'N', 0xFF, 0xFE, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0, 0x1, 0 };
		BYTE charHeader[] = { 'R', 'A', 'H', 'C', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

		int nTiles = ncgr->nTiles;
		int nBytesPerTile = 64;
		if (ncgr->nBits == 4) nBytesPerTile >>= 1;
		int sectionSize = 0x20 + nTiles * nBytesPerTile;
		int fileSize = 0x10 + sectionSize;

		if (ncgr->nBits == 8) *(int *) (charHeader + 0xC) = 4;
		else if (ncgr->nBits == 4) *(int *) (charHeader + 0xC) = 3;
		*(int *) (charHeader + 0x10) = ncgr->mapping;
		*(int *) (charHeader + 0x4) = sectionSize;
		if (ncgr->mapping == 0) {
			*(unsigned short *) (charHeader + 0x8) = ncgr->tilesY;
			*(unsigned short *) (charHeader + 0xA) = ncgr->tilesX;
		} else {
			*(int *) (charHeader + 0x8) = 0xFFFFFFFF;
		}
		*(int *) (charHeader + 0x1C) = 0x18;
		*(int *) (charHeader + 0x18) = sectionSize - 0x20;

		*(int *) (ncgrHeader + 0x8) = fileSize;

		DWORD dwWritten;
		WriteFile(hFile, ncgrHeader, 0x10, &dwWritten, NULL);
		WriteFile(hFile, charHeader, 0x20, &dwWritten, NULL);
		for (int i = 0; i < ncgr->nTiles; i++) {
			if (ncgr->nBits == 8) {
				WriteFile(hFile, ncgr->tiles[i], 64, &dwWritten, NULL);
			} else {
				BYTE buffer[32];
				for (int j = 0; j < 32; j++) {
					BYTE b = ncgr->tiles[i][(j << 1)] | (ncgr->tiles[i][(j << 1) + 1] << 4);

					buffer[j] = b;
				}
				WriteFile(hFile, buffer, 32, &dwWritten, NULL);
			}
		}
	} else if(ncgr->header.format == NCGR_TYPE_HUDSON || ncgr->header.format == NCGR_TYPE_HUDSON2) {
		DWORD dwWritten;

		if (ncgr->header.format == NCGR_TYPE_HUDSON) {
			BYTE header[] = { 0, 0, 0, 0, 1, 0, 0, 0 };
			if (ncgr->nBits == 4) header[4] = 0;
			*(WORD *) (header + 5) = ncgr->nTiles;
			int nCharacterBytes = 64 * ncgr->nTiles;
			if (ncgr->nBits == 4) nCharacterBytes >>= 1;
			*(WORD *) (header + 1) = nCharacterBytes + 4;

			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		} else if(ncgr->header.format == NCGR_TYPE_HUDSON2) {
			BYTE header[] = { 0, 0, 0, 0 };
			*(WORD *) (header + 1) = ncgr->nTiles;
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		}

		for (int i = 0; i < ncgr->nTiles; i++) {
			if (ncgr->nBits == 8) {
				WriteFile(hFile, ncgr->tiles[i], 64, &dwWritten, NULL);
			} else {
				BYTE buffer[32];
				for (int j = 0; j < 32; j++) {
					BYTE b = ncgr->tiles[i][(j << 1)] | (ncgr->tiles[i][(j << 1) + 1] << 4);

					buffer[j] = b;
				}
				WriteFile(hFile, buffer, 32, &dwWritten, NULL);
			}
		}
	}

	CloseHandle(hFile);
}

void ncgrCreate(DWORD * blocks, int nBlocks, int nBits, LPWSTR name, int bin) {

	int nBlockSize = 64;
	if (nBits == 4) nBlockSize = 32;
	//_asm int 3
	BYTE * b = (BYTE *) HeapAlloc(GetProcessHeap(), 0, nBlocks * nBlockSize);
	for (int i = 0; i < nBlocks * nBlockSize; i++) {
		if(nBits == 8) b[i] = blocks[i];
		else {
			b[i] = blocks[i * 2] | (blocks[i * 2 + 1] << 4);
		}
	}

	if (!bin) {
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
	} else {
		BYTE header[8] = { 0 };
		*(WORD *) (header + 1) = nBlocks * nBlockSize + 4;
		if(nBits == 8) header[4] = 1;
		*(WORD *) (header + 5) = nBlocks;

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		WriteFile(hFile, b, nBlocks * nBlockSize, &dwWritten, NULL);
		CloseHandle(hFile);
	}

	HeapFree(GetProcessHeap(), 0, b);
}