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
	nscr->header.type = FILE_TYPE_SCREEN;
	nscr->header.format = type;
	nscr->header.size = sizeof(*nscr);
	nscr->header.compression = COMPRESSION_NONE;
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
	nscr->dataSize = dwFileSize;
	nscr->data = malloc(dwFileSize);
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
	nscr->header.type = FILE_TYPE_SCREEN;
	nscr->header.format = NSCR_TYPE_NSCR;
	nscr->header.size = sizeof(*nscr);
	nscr->header.compression = COMPRESSION_NONE;
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

int isDuplicateAbsolute(DWORD * block1, DWORD * block2) {
	for (int i = 0; i < 64; i++) {
		if (block1[i] != block2[i]) return 0;
	}
	return 1;
}

int isDuplicateFlipped(DWORD * block1, DWORD * block2) {
	//test for flipX
	flipX(block2);
	if (isDuplicateAbsolute(block1, block2)) {
		flipX(block2);
		return TILE_FLIPX + 1;
	}
	flipX(block2);
	//test for flipY
	flipY(block2);
	if (isDuplicateAbsolute(block1, block2)) {
		flipY(block2);
		return TILE_FLIPY + 1;
	}
	//test flipXY
	flipX(block2);
	if (isDuplicateAbsolute(block1, block2)) {
		flipX(block2);
		flipY(block2);
		return TILE_FLIPXY + 1;
	}
	flipX(block2);
	flipY(block2);
	return 0;
}


int isDuplicate(DWORD * block1, DWORD * block2) {
	if (isDuplicateAbsolute(block1, block2)) return 1;
	return isDuplicateFlipped(block1, block2);
}

void nscrWrite(NSCR *nscr, LPWSTR name) {
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

int m(int a);

#define diffuse(a,r,g,b,ap) a=m((int)((a&0xFF)+(r)))|(m((int)(((a>>8)&0xFF)+(g)))<<8)|(m((int)(((a>>16)&0xFF)+(b)))<<16)|(m((int)(((a>>24)&0xFF)+(ap)))<<24)

void doDiffuseRespectTile(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt) {
	//if ((pixels[i] >> 24) < 127) return;
	if (i % width < width - 1) {
		unsigned int right = pixels[i + 1];
		diffuse(right, errorRed * 7 * amt / 16, errorGreen * 7 * amt / 16, errorBlue * 7 * amt / 16, errorAlpha * 7 * amt / 16);
		pixels[i + 1] = right;
	}
	if (i / width < height - 1) {
		if (i % width > 0) {//downleft
			if ((i % width) % 8 != 0 || (i / width) % 8 == 7) {
				unsigned int right = pixels[i + width - 1];
				diffuse(right, errorRed * 3 * amt / 16, errorGreen * 3 * amt / 16, errorBlue * 3 * amt / 16, errorAlpha * 3 * amt / 16);
				pixels[i + width - 1] = right;
			}
		}
		if (1) {//down
			unsigned int right = pixels[i + width];
			diffuse(right, errorRed * 5 * amt / 16, errorGreen * 5 * amt / 16, errorBlue * 5 * amt / 16, errorAlpha * 5 * amt / 16);
			pixels[i + width] = right;
		}
		if (i % width < width - 1) {
			unsigned int right = pixels[i + width + 1];
			diffuse(right, errorRed * 1 * amt / 16, errorGreen * 1 * amt / 16, errorBlue * 1 * amt / 16, errorAlpha * 1 * amt / 16);
			pixels[i + width + 1] = right;
		}
	}
}

void nscrCreate(DWORD *imgBits, int width, int height, int nBits, int dither, 
				LPWSTR lpszNclrLocation, LPWSTR lpszNcgrLocation, LPWSTR lpszNscrLocation, 
				int paletteBase, int nPalettes, int fmt, int tileBase, int mergeTiles,
				int paletteSize, int paletteOffset, int rowLimit) {

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
	DWORD *blocks = (DWORD *) calloc(tilesX * tilesY, 64 * 4);

	//split image into 8x8 chunks, and find the average color in each.
	DWORD *avgs = calloc(tilesX * tilesY, 4);
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			DWORD *block = blocks + 64 * (x + y * tilesX);
			memcpy(block, imgBits + srcOffset, 32);
			memcpy(block + 8, imgBits + srcOffset + width, 32);
			memcpy(block + 16, imgBits + srcOffset + width * 2, 32);
			memcpy(block + 24, imgBits + srcOffset + width * 3, 32);
			memcpy(block + 32, imgBits + srcOffset + width * 4, 32);
			memcpy(block + 40, imgBits + srcOffset + width * 5, 32);
			memcpy(block + 48, imgBits + srcOffset + width * 6, 32);
			memcpy(block + 56, imgBits + srcOffset + width * 7, 32);
			DWORD avg = averageColor(block, 64);
			avgs[x + y * tilesX] = avg;
		}
	}

	DWORD * palette = (DWORD *) calloc(1024, 1);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	if (nPalettes == 1) {
		if (paletteOffset) {
			createPaletteExact(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize);
		} else {
			createPalette_(imgBits, width, height, palette + (paletteBase << nBits), paletteSize);
		}
	} else {
		
		createMultiplePalettes(blocks, avgs, width, tilesX, tilesY, palette + (paletteBase << nBits), nPalettes, 1 << nBits, paletteSize, paletteOffset);

	}
	//apply the palette to the image. 
	BYTE *paletteIndices = (BYTE *) calloc(tilesX * tilesY, 1);
	DWORD *paletted = (DWORD *) calloc(width * height, 4);
	memcpy(paletted, imgBits, width * height * 4);

	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			DWORD *block = blocks + 64 * (x + y * tilesX);
			int bestPalette = paletteBase;
			int bestError = 0x7FFFFFFF;
			for (int i = paletteBase; i < nPalettes + paletteBase; i++) {
				int err = getPaletteError((RGB *) block, 64, (RGB *) palette + (i << nBits) + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset);
				if (err < bestError) {
					bestError = err;
					bestPalette = i;
				}
			}
			paletteIndices[x + y * tilesX] = bestPalette;

			DWORD *thisPalette = palette + (bestPalette << nBits);
			for (int i = 0; i < 64; i++) {
				int tileX = i % 8;
				int tileY = i / 8;
				int index = x * 8 + tileX + (y * 8 + tileY) * width;
				DWORD d = paletted[index];

				int useOffset = paletteOffset ? paletteOffset : 1;
				int closest = closestpalette(*(RGB *) &d, (RGB *) (thisPalette + useOffset), paletteSize - !paletteOffset, NULL) + useOffset;
				if (((d >> 24) & 0xFF) < 127) closest = 0;
				RGB chosen = *(RGB *) (thisPalette + closest);
				int errorRed = (d & 0xFF) - chosen.r;
				int errorGreen = ((d >> 8) & 0xFF) - chosen.g;
				int errorBlue = ((d >> 16) & 0xFF) - chosen.b;
				paletted[index] = closest; //effectively turns this pixel array into an index array.
				if (dither && closest) {
					float amt = 1.0f;
					doDiffuseRespectTile(index, width, height, paletted, errorRed, errorGreen, errorBlue, 0, amt);
				}
			}
		}
	}
	//split into 8x8 blocks.
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			DWORD *block = blocks + 64 * (x + y * tilesX);
			memcpy(block, paletted + srcOffset, 32);
			memcpy(block + 8, paletted + srcOffset + width, 32);
			memcpy(block + 16, paletted + srcOffset + width * 2, 32);
			memcpy(block + 24, paletted + srcOffset + width * 3, 32);
			memcpy(block + 32, paletted + srcOffset + width * 4, 32);
			memcpy(block + 40, paletted + srcOffset + width * 5, 32);
			memcpy(block + 48, paletted + srcOffset + width * 6, 32);
			memcpy(block + 56, paletted + srcOffset + width * 7, 32);
			DWORD avg = averageColor(block, 64);
			avgs[x + y * tilesX] = avg;
		}
	}

	//blocks is now an array of blocks. Next, we need to find duplicate blocks. 
	int nBlocks = tilesX * tilesY; //number of generated tiles
	int nTotalTiles = nBlocks; //number of output tiles
							   //first, merge duplicates. Then, merge similar blocks until nBlocks <= 0x400.
	WORD * indices = (WORD *) calloc(width * height, 2);
	for (int i = 0; i < width * height; i++) {
		indices[i] = i;
	}
	BYTE * modes = calloc(width * height, 1);

	//start by merging duplicates.
	if (mergeTiles) {
		for (int i = 0; i < nBlocks; i++) {
			DWORD * block1 = blocks + i * (64);
			//test for up to i
			for (int j = 0; j < i; j++) {
				//test - is block i equal to block j?
				DWORD * block2 = blocks + j * 64;
				int dup = isDuplicate(block1, block2);
				if (!dup) continue;

				//decrement all indices greater than i.
				for (int k = 0; k < nTotalTiles; k++) {

					//point all references to i to references to j.
					if (indices[k] == i) {
						indices[k] = j;
						modes[k] = dup - 1;
					}
				}
				for (int k = 0; k < nTotalTiles; k++) {
					if (indices[k] > i) indices[k]--;
				}
				//now, remove block i, by sliding the rest over it.
				memmove(blocks + i * 64, blocks + 64 + i * 64, (nBlocks - 1 - i) * 256);
				nBlocks--;
				i--;
				break;
			}
		}
	}

	//see how many are left
	nBlocks;
	if (nBlocks + tileBase > 1024) {
		char bf[32];
		sprintf(bf, "Too many tiles! Tiles: %d", nBlocks);
		MessageBoxA(NULL, bf, "Warning", MB_ICONWARNING);
	}

	for (int i = 0; i < nTotalTiles; i++) {
		indices[i] += tileBase;
	}

	//create nclr
	nclrCreate(palette, rowLimit ? (nBits == 4 ? ((paletteBase + nPalettes) << 4) : (paletteOffset + paletteSize)) : 256, nBits, 0, lpszNclrLocation, fmt);
	//create ngr
	ncgrCreate(blocks, nBlocks, nBits, lpszNcgrLocation, fmt);
	//create nscr
	nscrCreate_(indices, modes, paletteIndices, nTotalTiles, width, height, nBits, lpszNscrLocation, fmt);
	free(modes);
	free(blocks);
	free(indices);
	free(palette);
	free(avgs);
	free(paletteIndices);
}