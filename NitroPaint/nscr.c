#include "nscr.h"
#include "palette.h"
#include "ncgr.h"
#include <Windows.h>
#include <stdio.h>
#include <math.h>

#define NSCR_FLIPNONE 0
#define NSCR_FLIPX 1
#define NSCR_FLIPY 2
#define NSCR_FLIPXY (NSCR_FLIPX|NSCR_FLIPY)

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
		srcData = file + 8;
	} else if (type == NSCR_TYPE_HUDSON2) {
		tilesX = file[2];
		tilesY = file[3];
		srcData = file + 4;
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
	for (int i = 0; i < nscr->dataSize / 2; i++) {
		WORD w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}
	return 0;
}

int nscrRead(NSCR * nscr, char * file, DWORD dwFileSize) {
	if (!dwFileSize) return 1;
	if (*file == 0 || *file == 0x10) return hudsonScreenRead(nscr, file, dwFileSize);
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
	for (int i = 0; i < dwDataSize / 2; i++) {
		WORD w = nscr->data[i];
		w &= 0x3FF;
		if (w > nscr->nHighestIndex) nscr->nHighestIndex = w;
	}

	return 0;
}

int nscrReadFile(NSCR *nscr, LPWSTR path) {
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

int nscrGetTile(NSCR * nscr, NCGR * ncgr, NCLR * nclr, int x, int y, BOOL checker, DWORD * out) {
	return nscrGetTileEx(nscr, ncgr, nclr, x, y, checker, out, NULL);
}

int nscrGetTileEx(NSCR * nscr, NCGR * ncgr, NCLR * nclr, int x, int y, BOOL checker, DWORD * out, int *tileNo) {
	if (x >= nscr->nWidth / 8) return 1;
	if (y >= nscr->nHeight / 8) return 1;
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
		if (nscr->nHighestIndex == ncgr->nTiles) tileNumber--; //some NSCRs need this. Why? I'm not sure.
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
		*(short *) (nrcsHeader + 0x8) = nscr->nWidth;
		*(short *) (nrcsHeader + 0xA) = nscr->nHeight;
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
			header[6] = nscr->nWidth / 8;
			header[7] = nscr->nHeight / 8;
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		} else if (nscr->header.format == NSCR_TYPE_HUDSON2) {
			BYTE header[4] = { 0, 0, 0, 0 };
			*(WORD *) header = nTotalTiles * 2;
			header[2] = nscr->nWidth / 8;
			header[3] = nscr->nHeight / 8;
			WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		}

		WriteFile(hFile, nscr->data, 2 * nTotalTiles, &dwWritten, NULL);
		CloseHandle(hFile);
	}
}

void nscrCreate_(WORD * indices, BYTE * modes, BYTE *paletteIndices, int nTotalTiles, int width, int height, int nBits, LPWSTR name, int bin) {
	WORD * dataArea = (WORD *) (HeapAlloc(GetProcessHeap(), 0, nTotalTiles * 2));

	for (int i = 0; i < nTotalTiles; i++) {
		dataArea[i] = (indices[i] & 0x3FF) | ((modes[i] & 0x3) << 10) | ((paletteIndices[i] & 0xF) << 12);
	}

	if (!bin) {
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
	} else {
		BYTE header[8] = { 0 };
		*(WORD *) (header + 1) = 2 * nTotalTiles + 4;
		*(WORD *) (header + 4) = 2 * nTotalTiles;
		header[6] = width / 8;
		header[7] = height / 8;

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		WriteFile(hFile, dataArea, 2 * nTotalTiles, &dwWritten, NULL);
		CloseHandle(hFile);
	}
	HeapFree(GetProcessHeap(), 0, dataArea);
}

int m(int a);

#define diffuse(a,r,g,b,ap) a=m((a&0xFF)+(r))|(m(((a>>8)&0xFF)+(g))<<8)|(m(((a>>16)&0xFF)+(b))<<16)|(m(((a>>24)&0xFF)+(ap))<<24)

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

void nscrCreate(DWORD * imgBits, int width, int height, int nBits, int dither, LPWSTR lpszNclrLocation, LPWSTR lpszNcgrLocation, LPWSTR lpszNscrLocation, int paletteBase, int nPalettes, int bin) {
	//combine similar.
	DWORD * bits = imgBits;//combineSimilar(imgBits, width, height, 1024);
						   //create the palette.

	int tilesX = width / 8;
	int tilesY = height / 8;
	DWORD *blocks = (DWORD *) calloc(tilesX * tilesY, 64 * 4);

	//split image into 8x8 chunks, and find the average color in each.
	DWORD *avgs = calloc(tilesX * tilesY, 4);
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			DWORD *block = blocks + 64 * (x + y * tilesX);
			CopyMemory(block, imgBits + srcOffset, 32);
			CopyMemory(block + 8, imgBits + srcOffset + width, 32);
			CopyMemory(block + 16, imgBits + srcOffset + width * 2, 32);
			CopyMemory(block + 24, imgBits + srcOffset + width * 3, 32);
			CopyMemory(block + 32, imgBits + srcOffset + width * 4, 32);
			CopyMemory(block + 40, imgBits + srcOffset + width * 5, 32);
			CopyMemory(block + 48, imgBits + srcOffset + width * 6, 32);
			CopyMemory(block + 56, imgBits + srcOffset + width * 7, 32);
			DWORD avg = averageColor(block, 64);
			avgs[x + y * tilesX] = avg;
		}
	}

	DWORD * palette = (DWORD *) calloc(1024, 1);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	int paletteSize = 1 << nBits;
	if (nPalettes == 1) {
		createPalette_(bits, width, height, palette + paletteBase * paletteSize, paletteSize);
	} else {
		
		createMultiplePalettes(blocks, avgs, width, tilesX, tilesY, palette + paletteBase * paletteSize, nPalettes, paletteSize);

	}
	//apply the palette to the image. 
	BYTE *paletteIndices = (BYTE *) calloc(tilesX * tilesY, 1);
	DWORD *paletted = (DWORD *) calloc(width * height, 4);
	CopyMemory(paletted, bits, width * height * 4);

	/*for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++){
			int i = x + y * width;
			DWORD d = paletted[i];
			int closest = closestpalette(*(RGB *) &d, (RGB *) (palette + 1), paletteSize - 1, NULL) + 1;
			if (((d >> 24) & 0xFF) < 127) closest = 0;
			RGB chosen = *(RGB *) (palette + closest);
			int errorRed = -(chosen.r - (d & 0xFF));
			int errorGreen = -(chosen.g - ((d >> 8) & 0xFF));
			int errorBlue = -(chosen.b - ((d >> 16) & 0xFF));
			paletted[i] = closest; //effectively turns this pixel array into an index array.
			if(dither) doDiffuse(i, width, height, paletted, errorRed, errorGreen, errorBlue, 0, 1.0f);
		}
	}*/
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			DWORD *block = blocks + 64 * (x + y * tilesX);
			int bestPalette = paletteBase;
			int bestError = 0x7FFFFFFF;
			for (int i = paletteBase; i < nPalettes + paletteBase; i++) {
				int err = getPaletteError(block, 64, palette + i * paletteSize, paletteSize);
				if (err < bestError) {
					bestError = err;
					bestPalette = i;
				}
			}
			paletteIndices[x + y * tilesX] = bestPalette;

			DWORD *thisPalette = palette + bestPalette * paletteSize;
			for (int i = 0; i < 64; i++) {
				int tileX = i % 8;
				int tileY = i / 8;
				int index = x * 8 + tileX + (y * 8 + tileY) * width;
				DWORD d = paletted[index];

				int closest = closestpalette(*(RGB *) &d, (RGB *) (thisPalette + 1), paletteSize - 1, NULL) + 1;
				if (((d >> 24) & 0xFF) < 127) closest = 0;
				RGB chosen = *(RGB *) (thisPalette + closest);
				int errorRed = -(chosen.r - (d & 0xFF));
				int errorGreen = -(chosen.g - ((d >> 8) & 0xFF));
				int errorBlue = -(chosen.b - ((d >> 16) & 0xFF));
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
			CopyMemory(block, paletted + srcOffset, 32);
			CopyMemory(block + 8, paletted + srcOffset + width, 32);
			CopyMemory(block + 16, paletted + srcOffset + width * 2, 32);
			CopyMemory(block + 24, paletted + srcOffset + width * 3, 32);
			CopyMemory(block + 32, paletted + srcOffset + width * 4, 32);
			CopyMemory(block + 40, paletted + srcOffset + width * 5, 32);
			CopyMemory(block + 48, paletted + srcOffset + width * 6, 32);
			CopyMemory(block + 56, paletted + srcOffset + width * 7, 32);
			DWORD avg = averageColor(block, 64);
			avgs[x + y * tilesX] = avg;
		}
	}

	int nToAllocate = tilesX * tilesY;
	if (nToAllocate & 0xF) nToAllocate += (0x10 - (nToAllocate & 0xF));

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
	for (int i = 0; i < nBlocks; i++) {
		DWORD * block1 = blocks + i * (64);
		//test for up to i
		for (int j = 0; j < i; j++) {
			//test - is block i equal to block j?
			DWORD * block2 = blocks + j * 64;
			int dup = isDuplicate(block1, block2);
			if (!dup) continue;
			//modes[i] = dup - 1;
			//is a duplicate.
			//point indices[i] to j. 
			//indices[i] = j;
			//this leaves i unused as an index. 
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
			MoveMemory(blocks + i * 64, blocks + 64 + i * 64, (nBlocks - 1 - i) * 256);
			nBlocks--;
			i--;
			break;
		}
	}

	//see how many are left
	nBlocks;
	if (nBlocks > 1024) {
		char bf[32];// = "Too many tiles! Tiles: \0\0\0\0\0";
		//sprintf(bf, "Too many tiles! Tiles: %d", nBlocks);
		//itoa(nBlocks, bf + 23, 10);
		sprintf(bf, "Too many tiles! Tiles: %d", nBlocks);
		MessageBoxA(NULL, bf, "Warning", MB_ICONWARNING);
	}
	//_asm int 3
	//round up nBlocks to a multiple of 16.
	int nMisaligned = nBlocks & 0xF;
	int nMisalignedBlocks = nBlocks;
	int nAdded = 0;
	if (nMisaligned) nBlocks += (0x10 - nMisaligned), nAdded = (0x10 - nMisaligned);
	ZeroMemory(blocks + 64 * nMisalignedBlocks, 64 * nAdded * 4);

	//create nclr
	nclrCreate(palette, 256, nBits, 0, lpszNclrLocation, bin);
	//create ngr
	ncgrCreate(blocks, nBlocks, nBits, lpszNcgrLocation, bin);
	//create nscr
	nscrCreate_(indices, modes, paletteIndices, nTotalTiles, width, height, nBits, lpszNscrLocation, bin);
	free(modes);
	free(blocks);
	free(indices);
	free(palette);
	free(avgs);
	free(paletteIndices);
}