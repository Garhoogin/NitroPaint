#include "nclr.h"
#include <stdio.h>

int hudsonPaletteRead(NCLR *nclr, char *buffer, int size) {
	if (size < 4) return 1;

	int dataLength = *(WORD *) buffer;
	int nColors = *(WORD *) (buffer + 2);
	
	nclr->nColors = nColors;
	nclr->nBits = 4;
	nclr->colors = (WORD *) calloc(nColors, 2);
	nclr->isHudson = 1;
	memcpy(nclr->colors, buffer + 4, nColors * 2);
	return 0;
}

int nclrRead(NCLR * nclr, char * buffer, int size) {
	if (*buffer != 'R' && *buffer != 'N') return hudsonPaletteRead(nclr, buffer, size);
	short nBlocks = *(short *) (buffer + 0xE);
	buffer += 0x10;
	int bits = *(int *) (buffer + 0x8);
	bits = 1 << (bits - 1);
	int dataSize = *(int *) (buffer + 0x10);
	int sectionSize = *(int *) (buffer + 0x4);
	int dataOffset = 8 + *(int *) (buffer + 0x14);
	int nColors = (sectionSize - dataOffset) >> 1;

	nclr->nColors = nColors;
	nclr->nBits = bits;
	nclr->colors = (WORD *) calloc(nColors, 2);
	nclr->isHudson = 0;
	memcpy(nclr->colors, buffer + dataOffset, nColors * 2);
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
	if (dataLength & 1) return 0;
	if (dataLength + 4 > size) return 0;
	if (nColors * 2 + 4 > size) return 0;
	return 1;
}

int nclrIsValid(LPBYTE lpFile, int size) {
	if (size < 40) return 0;
	DWORD first = *(DWORD *) lpFile;
	if (first != 0x4E434C52) return 0;
	return 1;
}

void nclrWrite(NCLR * nclr, LPWSTR name) {
	HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!nclr->isHudson) {
		BYTE fileHeader[] = { 'R', 'L', 'C', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
		BYTE ttlpHeader[] = { 'T', 'T', 'L', 'P', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 };
		//BYTE pmcpHeader[] = {'P', 'M', 'C', 'P', 0x12, 0, 0, 0, 0, 0, 0, 0, 0xEF, 0xBE, 0x8, 0}; 
		//no need to make PMCP likely.

		int sectionSize = 0x18 + (nclr->nColors << 1);
		*(int *) (ttlpHeader + 0x4) = sectionSize;
		if (nclr->nBits == 8) *(int *) (ttlpHeader + 0x8) = 4;
		else *(int *) (ttlpHeader + 0x8) = 3;
		*(int *) (ttlpHeader + 0x10) = sectionSize - 0x18;

		*(int *) (fileHeader + 0x8) = sectionSize + 0x10;

		DWORD dwWritten;
		WriteFile(hFile, fileHeader, 0x10, &dwWritten, NULL);
		WriteFile(hFile, ttlpHeader, 0x18, &dwWritten, NULL);
		WriteFile(hFile, nclr->colors, nclr->nColors << 1, &dwWritten, NULL);
	} else {
		BYTE fileHeader[] = {0, 0, 0, 0};
		*(WORD *) fileHeader = nclr->nColors * 2;
		*(WORD *) (fileHeader + 2) = nclr->nColors;

		DWORD dwWritten;
		WriteFile(hFile, fileHeader, sizeof(fileHeader), &dwWritten, NULL);
		WriteFile(hFile, nclr->colors, nclr->nColors << 1, &dwWritten, NULL);
	}
	CloseHandle(hFile);
}

void nclrCreate(DWORD * palette, int nColors, int nBits, int extended, LPWSTR name, int bin) {
	WORD * cpal = (WORD *) HeapAlloc(GetProcessHeap(), 0, nColors * 2);
	for (int i = 0; i < nColors; i++) {
		DWORD d = palette[i];
		int r = d & 0xFF;
		int g = (d >> 8) & 0xFF;
		int b = (d >> 16) & 0xFF;
		r = (r + 4) * 31 / 255;
		g = (g + 4) * 31 / 255;
		b = (b + 4) * 31 / 255;
		cpal[i] = r | (g << 5) | (b << 10);
	}

	if (!bin) {
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
	} else {
		BYTE header[4];
		*(WORD *) header = 2 * nColors;
		*(WORD *) (header + 2) = nColors;

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
		WriteFile(hFile, cpal, nColors * 2, &dwWritten, NULL);
		CloseHandle(hFile);
	}
	HeapFree(GetProcessHeap(), 0, cpal);

}