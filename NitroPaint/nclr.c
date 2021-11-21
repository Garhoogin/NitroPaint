#include "nclr.h"
#include "g2dfile.h"
#include <stdio.h>

LPCWSTR paletteFormatNames[] = { L"Invalid", L"NCLR", L"Hudson", L"Binary", L"NTFP", NULL };

void nclrFree(OBJECT_HEADER *header) {
	NCLR *nclr = (NCLR *) header;
	if (nclr->colors != NULL) free(nclr->colors);
	nclr->colors = NULL;
	if (nclr->idxTable != NULL) free(nclr->idxTable);
	nclr->idxTable = NULL;

	COMBO2D *combo2d = nclr->combo2d;
	if (nclr->combo2d != NULL) {
		nclr->combo2d->nclr = NULL;
		if (combo2d->nclr == NULL && combo2d->ncgr == NULL && combo2d->nscr == NULL) free(combo2d);
	}
	nclr->combo2d = NULL;
}

int hudsonPaletteRead(NCLR *nclr, char *buffer, int size) {
	if (size < 4) return 1;

	int dataLength = *(WORD *) buffer;
	int nColors = *(WORD *) (buffer + 2);

	nclr->nColors = nColors;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	nclr->header.type = FILE_TYPE_PALETTE;
	nclr->header.format = NCLR_TYPE_HUDSON;
	nclr->header.size = sizeof(*nclr);
	nclr->header.compression = COMPRESSION_NONE;
	nclr->header.dispose = nclrFree;
	nclr->combo2d = NULL;
	memcpy(nclr->colors, buffer + 4, nColors * 2);
	return 0;
}

int binPaletteRead(NCLR *nclr, char *buffer, int size) {
	if (!nclrIsValidNtfp(buffer, size)) return 1; //this function is being reused for NTFP as well
	
	int nColors = size >> 1;

	nclr->nColors = nColors;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	nclr->combo2d = NULL;
	nclr->header.type = FILE_TYPE_PALETTE;
	nclr->header.format = nclrIsValidBin(buffer, size) ? NCLR_TYPE_BIN : NCLR_TYPE_NTFP;
	nclr->header.size = sizeof(*nclr);
	nclr->header.compression = COMPRESSION_NONE;
	nclr->header.dispose = nclrFree;
	memcpy(nclr->colors, buffer, nColors * 2);
	return 0;
}

int comboReadPalette(NCLR *nclr, char *buffer, int size) {
	nclr->header.compression = COMPRESSION_NONE;
	nclr->header.dispose = nclrFree;
	nclr->header.size = sizeof(NCLR);
	nclr->header.type = FILE_TYPE_PALETTE;
	nclr->header.format = NCLR_TYPE_COMBO;
	nclr->nColors = 256;
	nclr->extPalette = 0;
	nclr->idxTable = NULL;
	nclr->nBits = 4;
	nclr->nPalettes = 0;
	nclr->totalSize = 256;
	nclr->combo2d = NULL;
	nclr->colors = (COLOR *) calloc(256, sizeof(COLOR));
	memcpy(nclr->colors, buffer + 4, 512);

	return 0;
}

int nclrRead(NCLR *nclr, char *buffer, int size) {
	if (lz77IsCompressed(buffer, size)) {
		int uncompressedSize;
		char *bf = lz77decompress(buffer, size, &uncompressedSize);
		int r = nclrRead(nclr, bf, uncompressedSize);
		free(bf);
		nclr->header.compression = COMPRESSION_LZ77;
		return r;
	}
	if (*buffer != 'R' && *buffer != 'N') {
		if(nclrIsValidHudson(buffer, size)) return hudsonPaletteRead(nclr, buffer, size);
		if (nclrIsValidBin(buffer, size)) return binPaletteRead(nclr, buffer, size);
		if (nclrIsValidNtfp(buffer, size)) return binPaletteRead(nclr, buffer, size);
		if (combo2dIsValid(buffer, size)) return comboReadPalette(nclr, buffer, size);
	}
	char *pltt = g2dGetSectionByMagic(buffer, size, 'PLTT');
	char *pcmp = g2dGetSectionByMagic(buffer, size, 'PCMP');
	if (pltt == NULL) return 1;

	int bits = *(int *) (pltt + 0x8);
	bits = 1 << (bits - 1);
	int dataSize = *(int *) (pltt + 0x10);
	int sectionSize = *(int *) (pltt + 0x4);
	int dataOffset = 8 + *(int *) (pltt + 0x14);
	int nColors = (sectionSize - dataOffset) >> 1;

	nclr->nColors = nColors;
	nclr->nBits = bits;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	nclr->extPalette = *(int *) (pltt + 0xC);
	nclr->totalSize = *(int *) (pltt + 0x10);
	nclr->nPalettes = 0;
	nclr->idxTable = NULL;
	nclr->combo2d = NULL;
	nclr->header.type = FILE_TYPE_PALETTE;
	nclr->header.format = NCLR_TYPE_NCLR;
	nclr->header.size = sizeof(*nclr);
	nclr->header.compression = COMPRESSION_NONE;
	nclr->header.dispose = nclrFree;
	
	if (pcmp != NULL) {
		nclr->nPalettes = *(unsigned short *) (pcmp + 8);
		nclr->idxTable = (short *) calloc(nclr->nPalettes, 2);
		memcpy(nclr->idxTable, pcmp + 8 + *(unsigned int *) (pcmp + 0xC), nclr->nPalettes * 2);
	}
	memcpy(nclr->colors, pltt + dataOffset, nColors * 2);
	return 0;
}

int nclrReadFile(NCLR *nclr, LPWSTR path) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);
	int n = nclrRead(nclr, lpBuffer, dwSize);
	free(lpBuffer);
	return n;
}

int nclrIsValidHudson(LPBYTE lpFile, int size) {
	if (size < 4) return 0;
	if (*lpFile == 0x10) return 0;
	int dataLength = *(WORD *) lpFile;
	int nColors = *(WORD *) (lpFile + 2);
	if (nColors == 0) return 0;
	if (dataLength & 1) return 0;
	if (dataLength + 4 != size) return 0;
	if (nColors * 2 + 4 != size) return 0;

	if (nColors & 0xF) return 0;
	if (nColors > 256) {
		if (nColors & 0xFF) return 0;
	}

	COLOR *data = (COLOR *) (lpFile + 4);
	for (int i = 0; i < nColors; i++) {
		COLOR w = data[i];
		if (w & 0x8000) return 0;
	}
	return 1;
}

int nclrIsValidBin(LPBYTE lpFile, int size) {
	if (size < 16) return 0;
	if (size & 1) return 0;
	if (size > 16 * 256 * 2) return 0;
	int nColors = size >> 1;
	if (nColors & 0xF) return 0;
	if (nColors > 256) {
		if (nColors & 0xFF) return 0;
	}

	COLOR *data = (COLOR *) lpFile;
	for (int i = 0; i < nColors; i++) {
		COLOR w = data[i];
		if (w & 0x8000) return 0;
	}
	return 1;
}

int nclrIsValidNtfp(LPBYTE lpFile, int size) {
	if (size & 1) return 0;
	for (int i = 0; i < size >> 1; i++) {
		COLOR c = *(COLOR *) (lpFile + i * 2);
		if (c & 0x8000) return 0;
	}
	return 1;
}

int nclrIsValid(LPBYTE lpFile, int size) {
	if (!g2dIsValid(lpFile, size)) return 0;

	if (size < 40) return 0;
	DWORD first = *(DWORD *) lpFile;
	if (first != 0x4E434C52) return 0;
	return 1;
}

void nclrWrite(NCLR *nclr, LPWSTR name) {
	if (nclr->header.format == NCLR_TYPE_COMBO) {
		combo2dWrite(nclr->combo2d, name);
		return;
	}
	HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (nclr->header.format == NCLR_TYPE_NCLR) {
		BYTE fileHeader[] = { 'R', 'L', 'C', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
		BYTE ttlpHeader[] = { 'T', 'T', 'L', 'P', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 };
		BYTE pmcpHeader[] = { 'P', 'M', 'C', 'P', 0x12, 0, 0, 0, 0, 0, 0xEF, 0xBE, 0x8, 0, 0, 0 };
		

		int sectionSize = 0x18 + (nclr->nColors << 1);
		int pcmpSize = nclr->nPalettes ? (0x10 + 2 * nclr->nPalettes) : 0;
		int nSections = 1 + (pcmpSize != 0);
		*(int *) (ttlpHeader + 0x4) = sectionSize;
		if (nclr->nBits == 8) *(int *) (ttlpHeader + 0x8) = 4;
		else *(int *) (ttlpHeader + 0x8) = 3;
		*(int *) (ttlpHeader + 0x10) = nclr->totalSize;
		*(int *) (ttlpHeader + 0xC) = nclr->extPalette;

		*(int *) (fileHeader + 0x8) = sectionSize + 0x10 + pcmpSize;
		*(short *) (fileHeader + 0xE) = nSections;

		*(short *) (pmcpHeader + 8) = nclr->nPalettes;
		*(int *) (pmcpHeader + 4) = 0x10 + 2 * nclr->nPalettes;

		DWORD dwWritten;
		WriteFile(hFile, fileHeader, 0x10, &dwWritten, NULL);
		WriteFile(hFile, ttlpHeader, 0x18, &dwWritten, NULL);
		WriteFile(hFile, nclr->colors, nclr->nColors << 1, &dwWritten, NULL);
		if (pcmpSize) {
			WriteFile(hFile, pmcpHeader, sizeof(pmcpHeader), &dwWritten, NULL);
			WriteFile(hFile, nclr->idxTable, nclr->nPalettes * 2, &dwWritten, NULL);
		}
	} else if(nclr->header.format == NCLR_TYPE_HUDSON) {
		BYTE fileHeader[] = {0, 0, 0, 0};
		*(WORD *) fileHeader = nclr->nColors * 2;
		*(WORD *) (fileHeader + 2) = nclr->nColors;

		DWORD dwWritten;
		WriteFile(hFile, fileHeader, sizeof(fileHeader), &dwWritten, NULL);
		WriteFile(hFile, nclr->colors, nclr->nColors << 1, &dwWritten, NULL);
	} else if(nclr->header.format == NCLR_TYPE_BIN || nclr->header.format == NCLR_TYPE_NTFP) {
		DWORD dwWritten;
		WriteFile(hFile, nclr->colors, nclr->nColors * 2, &dwWritten, NULL);
	}
	CloseHandle(hFile);
	if (nclr->header.compression != COMPRESSION_NONE) {
		fileCompress(name, nclr->header.compression);
	}
}

void nclrCreate(DWORD * palette, int nColors, int nBits, int extended, LPWSTR name, int fmt) {
	COLOR *cpal = (WORD *) calloc(nColors, 2);
	for (int i = 0; i < nColors; i++) {
		DWORD d = palette[i];
		cpal[i] = ColorConvertToDS(d);
	}

	if (fmt == 0) {
		BYTE nclrHeader[] = { 'R', 'L', 'C', 'N', 0xFF, 0xFE, 0x0, 0x1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
		BYTE ttlpHeader[] = { 'T', 'T', 'L', 'P', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 };
		//BYTE pmcpHeader[] = {'P', 'M', 'C', 'P', 0x12, 0, 0, 0, 1, 0, 0xEF, 0xBE, 8, 0, 0, 0, 0, 0};

		int dataSize = nColors << 1;
		int ttlpSize = dataSize + 0x18;
		int fileSize = ttlpSize + 0x10;

		*(int *) (nclrHeader + 0x8) = fileSize;
		*(int *) (ttlpHeader + 0x4) = ttlpSize;
		*(int *) (ttlpHeader + 0x8) = nBits == 8 ? 4 : 3;
		*(int *) (ttlpHeader + 0xC) = !!extended;
		*(int *) (ttlpHeader + 0x10) = dataSize;


		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, nclrHeader, sizeof(nclrHeader), &dwWritten, NULL);
		WriteFile(hFile, ttlpHeader, sizeof(ttlpHeader), &dwWritten, NULL);
		WriteFile(hFile, cpal, nColors * 2, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if(fmt == 1 || fmt == 2) {
		BYTE header[4];
		*(WORD *) header = 2 * nColors;
		*(WORD *) (header + 2) = nColors;

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		WriteFile(hFile, cpal, nColors * 2, &dwWritten, NULL);
		CloseHandle(hFile);
	} else if (fmt == 3 || fmt == 4) {
		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, cpal, nColors * 2, &dwWritten, NULL);
		CloseHandle(hFile);
	}
	free(cpal);

}