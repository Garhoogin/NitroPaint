#include "nscr.h"
#include "palette.h"
#include "ncgr.h"
#include <Windows.h>
#include <stdio.h>
#include <math.h>

LPCWSTR screenFormatNames[] = { L"Invalid", L"NSCR", L"Hudson", L"Hudson 2", L"Binary", NULL };

#define NSCR_FLIPNONE 0
#define NSCR_FLIPX 1
#define NSCR_FLIPY 2
#define NSCR_FLIPXY (NSCR_FLIPX|NSCR_FLIPY)

int isValidScreenSize(int nPx) {
	if (nPx == 256 * 256 || nPx == 512 * 256 || 
		nPx == 512 * 512 || nPx == 128 * 128 || 
		nPx == 1024 * 1024 || nPx == 512 * 1024) return 1;
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

void nscrFree(OBJECT_HEADER *header) {
	NSCR *nscr = (NSCR *) header;
	if (nscr->data != NULL) free(nscr->data);
	nscr->data = NULL;

	COMBO2D *combo = nscr->combo2d;
	if (nscr->combo2d != NULL) {
		nscr->combo2d->nscr = NULL;
		if (combo->nclr == NULL && combo->ncgr == NULL && combo->nscr == NULL) free(combo);
	}
	nscr->combo2d = NULL;
}

int hudsonScreenRead(NSCR *nscr, char *file, DWORD dwFileSize) {
	if (*file == 0x10) return 1; //TODO: implement LZ77 decompression
	if (dwFileSize < 8) return 1; //file too small
	//if (file[4] != 0) return 1; //not a screen file
	int type = nscrIsValidHudson(file, dwFileSize);

	int tilesX = 0, tilesY = 0;
	WORD *srcData = NULL;

	if (type == NSCR_TYPE_HUDSON) {
		int fileSize = 4 + *(WORD *) (file + 1);
		tilesX = file[6];
		tilesY = file[7];
		srcData = (WORD *) (file + 8);
	} else if (type == NSCR_TYPE_HUDSON2) {
		tilesX = file[2];
		tilesY = file[3];
		srcData = (WORD *) (file + 4);
	}

	nscr->data = malloc(tilesX * tilesY * 2);
	nscr->nWidth = tilesX * 8;
	nscr->nHeight = tilesY * 8;
	nscr->dataSize = tilesX * tilesY * 2;
	nscr->nHighestIndex = 0;
	nscr->combo2d = NULL;
	nscr->header.type = FILE_TYPE_SCREEN;
	nscr->header.format = type;
	nscr->header.size = sizeof(*nscr);
	nscr->header.compression = COMPRESSION_NONE;
	nscr->header.dispose = nscrFree;
	memcpy(nscr->data, srcData, nscr->dataSize);
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		WORD w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}
	return 0;
}

int nscrReadBin(NSCR *nscr, char *file, DWORD dwFileSize) {
	nscr->header.compression = COMPRESSION_NONE;
	nscr->header.format = NSCR_TYPE_BIN;
	nscr->header.size = sizeof(NSCR);
	nscr->header.type = FILE_TYPE_SCREEN;
	nscr->header.dispose = nscrFree;
	nscr->dataSize = dwFileSize;
	nscr->data = malloc(dwFileSize);
	nscr->combo2d = NULL;
	memcpy(nscr->data, file, dwFileSize);
	nscr->nHighestIndex = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		WORD w = nscr->data[i];
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
	nscr->header.compression = COMPRESSION_NONE;
	nscr->header.dispose = nscrFree;
	nscr->header.size = sizeof(NSCR);
	nscr->header.type = FILE_TYPE_SCREEN;
	nscr->header.format = NSCR_TYPE_COMBO;
	nscr->dataSize = 2048;
	nscr->nHeight = 256;
	nscr->nWidth = 256;
	nscr->data = (WORD *) calloc(1024, 2);
	nscr->combo2d = NULL;
	memcpy(nscr->data, file + 0x208, 2048);

	nscr->nHighestIndex = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		WORD w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}
	return 0;
}

int nscrRead(NSCR *nscr, char *file, DWORD dwFileSize) {
	if (lz77IsCompressed(file, dwFileSize)) {
		int uncompressedSize;
		char *bf = lz77decompress(file, dwFileSize, &uncompressedSize);
		int r = nscrRead(nscr, bf, uncompressedSize);
		free(bf);
		nscr->header.compression = COMPRESSION_LZ77;
		return r;
	}
	if (!dwFileSize) return 1;
	if (*(DWORD *) file != 0x4E534352) {
		if(nscrIsValidHudson(file, dwFileSize)) return hudsonScreenRead(nscr, file, dwFileSize);
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

	//now, I'm pretty sure the data area is just 2-byte indices.
	nscr->data = malloc(dwDataSize);
	nscr->nWidth = nWidth;
	nscr->nHeight = nHeight;
	nscr->dataSize = dwDataSize;
	nscr->nHighestIndex = 0;
	nscr->combo2d = NULL;
	nscr->header.type = FILE_TYPE_SCREEN;
	nscr->header.format = NSCR_TYPE_NSCR;
	nscr->header.size = sizeof(*nscr);
	nscr->header.compression = COMPRESSION_NONE;
	nscr->header.dispose = nscrFree;
	memcpy(nscr->data, file + 0x14, dwDataSize);
	for (unsigned int i = 0; i < dwDataSize / 2; i++) {
		WORD w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}

	return 0;
}

int nscrReadFile(NSCR *nscr, LPCWSTR path) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);
	int n = nscrRead(nscr, lpBuffer, dwSize);
	free(lpBuffer);
	return n;
}

DWORD * toBitmap(NSCR * nscr, NCGR * ncgr, NCLR * nclr, int * width, int * height) {
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
			nscrGetTile(nscr, ncgr, nclr, x, y, TRUE, block);
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

void flipX(DWORD * block) {
	//DWORD halfLine[4];
	for (int i = 0; i < 8; i++) {
		DWORD * line = block + i * 8;
		for (int j = 0; j < 4; j++) {
			DWORD p1 = line[j];
			line[j] = line[7 - j];
			line[7 - j] = p1;
		}
	}
}

void flipY(DWORD * block) {
	DWORD lineBuffer[8];
	for (int i = 0; i < 4; i++) {
		DWORD * line1 = block + i * 8;
		DWORD * line2 = block + (7 - i) * 8;
		CopyMemory(lineBuffer, line2, 32);
		CopyMemory(line2, line1, 32);
		CopyMemory(line1, lineBuffer, 32);
	}
}

int nscrGetTile(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int x, int y, BOOL checker, DWORD * out) {
	return nscrGetTileEx(nscr, ncgr, nclr, 0, x, y, checker, out, NULL);
}

int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int x, int y, BOOL checker, DWORD *out, int *tileNo) {
	if (x >= (int) (nscr->nWidth / 8)) return 1;
	if (y >= (int) (nscr->nHeight / 8)) return 1;
	int nWidthTiles = nscr->nWidth >> 3;
	int nHeightTiles = nscr->nHeight >> 3;
	int iTile = y * nWidthTiles + x;
	WORD tileData = nscr->data[iTile];

	int tileNumber = tileData & 0x3FF;
	int transform = (tileData >> 10) & 0x3;
	int paletteNumber = (tileData >> 12) & 0xF;
	if(tileNo != NULL) *tileNo = tileNumber;

	if (nclr) {
		int bitness = ncgr->nBits;
		int paletteSize = 16;
		if (bitness == 8) paletteSize = 256;
		COLOR* palette = nclr->colors + paletteSize * paletteNumber;
		int tileSize = 32;
		if (bitness == 8) tileSize = 64;
		tileNumber -= tileBase;
		if (ncgr) {
			if (tileNumber >= ncgr->nTiles || tileNumber < 0) { //? let's just paint a transparent square
				if (!checker) FillMemory(out, 64 * 4, 0);
				else {
					for (int i = 0; i < 64; i++) {
						int c = ((i & 0x7) ^ (i >> 3)) >> 2;
						if (c) out[i] = 0xFFFFFFFF;
						else out[i] = 0xFFC0C0C0;
					}
				}
				return 0;
			}
			BYTE * ncgrTile = ncgr->tiles[tileNumber];

			for (int i = 0; i < 64; i++) {
				if (ncgrTile[i]) {
					COLOR c = palette[ncgrTile[i]];
					out[i] = ColorConvertFromDS(CREVERSE(c)) | 0xFF000000;
				} else {
					out[i] = 0;
				}
			}
			if (transform & TILE_FLIPX) flipX(out);
			if (transform & TILE_FLIPY) flipY(out);
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

void nscrWrite(NSCR *nscr, LPWSTR name) {
	if (nscr->header.format == NSCR_TYPE_COMBO) {
		combo2dWrite(nscr->combo2d, name);
		return;
	}
	DWORD dwWritten;
	if (nscr->header.format == NSCR_TYPE_NSCR) {
		BYTE nscrHeader[] = { 'R', 'C', 'S', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
		BYTE nrcsHeader[] = { 'N', 'R', 'C', 'S', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 };

		int dataSize = ((nscr->nWidth * nscr->nHeight) >> 6) << 1;
		int nrcsSize = dataSize + 0x14;
		int fileSize = nrcsSize + 0x10;

		*(int *) (nscrHeader + 0x8) = fileSize;
		*(int *) (nrcsHeader + 0x4) = nrcsSize;
		*(short *) (nrcsHeader + 0x8) = (short) nscr->nWidth;
		*(short *) (nrcsHeader + 0xA) = (short) nscr->nHeight;
		*(int *) (nrcsHeader + 0x10) = dataSize;

		*(int *) (nrcsHeader + 0xC) = nscr->fmt;
		/*if (nBits == 4) {
			*(int *) (nrcsHeader + 0xC) = 0;
		}*/

		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, nscrHeader, sizeof(nscrHeader), &dwWritten, NULL);
		WriteFile(hFile, nrcsHeader, sizeof(nrcsHeader), &dwWritten, NULL);
		WriteFile(hFile, nscr->data, dataSize, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if(nscr->header.format == NSCR_TYPE_HUDSON || nscr->header.format == NSCR_TYPE_HUDSON2) {
		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		
		int nTotalTiles = (nscr->nWidth * nscr->nHeight) >> 6;
		if (nscr->header.format == NSCR_TYPE_HUDSON) {
			BYTE header[8] = { 0 };
			*(WORD *) (header + 1) = 2 * nTotalTiles + 4;
			*(WORD *) (header + 4) = 2 * nTotalTiles;
			header[6] = (BYTE) (nscr->nWidth / 8);
			header[7] = (BYTE) (nscr->nHeight / 8);
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		} else if (nscr->header.format == NSCR_TYPE_HUDSON2) {
			BYTE header[4] = { 0, 0, 0, 0 };
			*(WORD *) header = nTotalTiles * 2;
			header[2] = (BYTE) (nscr->nWidth / 8);
			header[3] = (BYTE) (nscr->nHeight / 8);
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		}

		WriteFile(hFile, nscr->data, 2 * nTotalTiles, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if (nscr->header.format == NSCR_TYPE_BIN) {
		DWORD dwWritten;

		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, nscr->data, nscr->dataSize, &dwWritten, NULL);
		CloseHandle(hFile);
	}
	if (nscr->header.compression != COMPRESSION_NONE) {
		fileCompress(name, nscr->header.compression);
	}
}

void nscrCreate_(WORD * indices, BYTE * modes, BYTE *paletteIndices, int nTotalTiles, int width, int height, int nBits, LPWSTR name, int fmt) {
	WORD * dataArea = (WORD *) (HeapAlloc(GetProcessHeap(), 0, nTotalTiles * 2));

	for (int i = 0; i < nTotalTiles; i++) {
		dataArea[i] = (indices[i] & 0x3FF) | ((modes[i] & 0x3) << 10) | ((paletteIndices[i] & 0xF) << 12);
	}

	if (fmt == 0) {
		BYTE nscrHeader[] = { 'R', 'C', 'S', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
		BYTE nrcsHeader[] = { 'N', 'R', 'C', 'S', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 };

		int dataSize = nTotalTiles << 1;
		int nrcsSize = dataSize + 0x14;
		int fileSize = nrcsSize + 0x10;

		*(int *) (nscrHeader + 0x8) = fileSize;
		*(int *) (nrcsHeader + 0x4) = nrcsSize;
		*(short *) (nrcsHeader + 0x8) = width;
		*(short *) (nrcsHeader + 0xA) = height;
		*(int *) (nrcsHeader + 0x10) = dataSize;

		if (nBits == 4) {
			*(int *) (nrcsHeader + 0xC) = 0;
		}

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, nscrHeader, sizeof(nscrHeader), &dwWritten, NULL);
		WriteFile(hFile, nrcsHeader, sizeof(nrcsHeader), &dwWritten, NULL);
		WriteFile(hFile, dataArea, 2 * nTotalTiles, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if(fmt == 1 || fmt == 2) {
		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fmt == 1) {
			BYTE header[8] = { 0 };
			*(WORD *) (header + 1) = 2 * nTotalTiles + 4;
			*(WORD *) (header + 4) = 2 * nTotalTiles;
			header[6] = width / 8;
			header[7] = height / 8;
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		} else {
			BYTE header[4] = { 0 };
			*(WORD *) (header) = 2 * nTotalTiles;
			header[2] = width / 8;
			header[3] = height / 8;
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		}
		WriteFile(hFile, dataArea, 2 * nTotalTiles, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if (fmt == 3 || fmt == 4) {
		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, dataArea, 2 * nTotalTiles, &dwWritten, NULL);
		CloseHandle(hFile);
		if (fmt == 4) fileCompress(name, COMPRESSION_LZ77);
	}
	HeapFree(GetProcessHeap(), 0, dataArea);
}

typedef struct BGTILE_ {
	BYTE indices[64];
	DWORD px[64]; //redundant, speed
	int masterTile;
	int nRepresents;
	int flipMode;
} BGTILE;

int tileDifferenceFlip(BGTILE *t1, BGTILE *t2, BYTE mode) {
	int err = 0;
	DWORD *px1 = t1->px;
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {

			int x2 = (mode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (mode & TILE_FLIPY) ? (7 - y) : y;
			DWORD c1 = *(px1++);
			DWORD c2 = t2->px[x2 + y2 * 8];

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

void bgAddTileToTotal(DWORD *pxBlock, BGTILE *tile) {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			DWORD col = tile->px[x + y * 8];
			
			int x2 = (tile->flipMode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (tile->flipMode & TILE_FLIPY) ? (7 - y) : y;
			DWORD *dest = pxBlock + 4 * (x2 + y2 * 8);

			dest[0] += col & 0xFF;
			dest[1] += (col >> 8) & 0xFF;
			dest[2] += (col >> 16) & 0xFF;
			dest[3] += (col >> 24) & 0xFF;
		}
	}
}

void nscrCreate(DWORD *imgBits, int width, int height, int nBits, int dither,
				LPWSTR lpszNclrLocation, LPWSTR lpszNcgrLocation, LPWSTR lpszNscrLocation,
				int paletteBase, int nPalettes, int fmt, int tileBase, int mergeTiles,
				int paletteSize, int paletteOffset, int rowLimit, int nMaxChars,
				int *progress1, int *progress1Max, int *progress2, int *progress2Max) {

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

	int tilesX = width / 8;
	int tilesY = height / 8;
	int nTiles = tilesX * tilesY;
	BGTILE *tiles = (BGTILE *) calloc(nTiles, sizeof(BGTILE));

	//initialize progress
	*progress1Max = nTiles * 2; //2 passes
	*progress2Max = 1000;

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

	DWORD *palette = (DWORD *) calloc(256, 4);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	if (nPalettes == 1) {
		if (paletteOffset) {
			createPaletteExact(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize);
		} else {
			createPalette_(imgBits, width, height, palette + (paletteBase << nBits), paletteSize);
		}
	} else {
		createMultiplePalettes(imgBits, tilesX, tilesY, palette, paletteBase, nPalettes, 1 << nBits, paletteSize, paletteOffset, progress1);
	}
	*progress1 = nTiles * 2; //make sure it's done

	//match palettes to tiles
	for (int i = 0; i < nTiles; i++) {
		BGTILE *tile = tiles + i;

		int bestPalette = paletteBase;
		int bestError = 0x7FFFFFFF;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			DWORD *pal = palette + (j << nBits);
			int err = getPaletteError((RGB *) tile->px, 64, (RGB *) pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//match colors
		DWORD *pal = palette + (bestPalette << nBits);
		for (int j = 0; j < 64; j++) {
			DWORD col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestpalette(*(RGB *) &col, (RGB *) pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, NULL) 
					+ !paletteOffset + paletteOffset;
			}
			if (nBits == 4) {
				tile->indices[j] = (bestPalette << 4) | index;
			} else {
				tile->indices[j] = index;
			}
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;

			//diffuse
			if (dither && index) {
				float amt = 1.0f;
				DWORD chosen = pal[index];

				int er = (col & 0xFF) - (chosen & 0xFF);
				int eg = ((col >> 8) & 0xFF) - ((chosen >> 8) & 0xFF);
				int eb = ((col >> 16) & 0xFF) - ((chosen >> 16) & 0xFF);

				doDiffuse(j, 8, 8, tile->px, er, eg, eb, 0, amt);
			}
		}
		tile->masterTile = i;
		tile->nRepresents = 1;
	}

	//match tiles to each other
	int nChars = nTiles;
	if (mergeTiles) {
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
			*progress2 = i * i * 500 / (nTiles * nTiles);
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
					*progress2 = 500 + (nTiles - nChars) * 500 / (nTiles - nMaxChars);
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
						unsigned long long int thisError = diffBuff[i + j * nTiles] * t1->nRepresents * t2->nRepresents;

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
				*progress2 = 500 + (nTiles - nChars) * 500 / (nTiles - nMaxChars);
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
				int err = getPaletteError((RGB *) tile->px, 64, (RGB *) pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset);

				if (err < bestError) {
					bestError = err;
					bestPalette = j;
				}
			}

			//now, match colors to indices.
			DWORD *pal = palette + (bestPalette << nBits);
			for (int j = 0; j < 64; j++) {
				DWORD col = tile->px[j];
				int index = 0;
				if (((col >> 24) & 0xFF) > 127) {
					index = closestpalette(*(RGB *) &col, (RGB *) pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, NULL)
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

	//create nclr
	nclrCreate(palette, rowLimit ? (nBits == 4 ? ((paletteBase + nPalettes) << 4) : (paletteOffset + paletteSize)) : 256, nBits, 0, lpszNclrLocation, fmt);
	//create ngr
	ncgrCreate(blocks, nChars, nBits, lpszNcgrLocation, fmt);
	//create nscr
	nscrCreate_(indices, modes, paletteIndices, nTiles, width, height, nBits, lpszNscrLocation, fmt);
	free(modes);
	free(blocks);
	free(indices);
	free(palette);
	free(paletteIndices);
}