#include "ntft.h"

int ntftIsValid(char *buffer, int size) {
	if (size < 128 || size > 2048 * 1024) return 0;
	if (size & (size - 1)) return 0;
	return 1;
}

int ntftRead(NTFT *ntft, char *buffer, int size) {
	if (lz77IsCompressed(buffer, size)) {
		int uncompressedSize;
		char *bf = lz77decompress(buffer, size, &uncompressedSize);
		int r = ntftRead(ntft, bf, uncompressedSize);
		free(bf);
		ntft->header.compression = COMPRESSION_LZ77;
		return r;
	}
	if (!ntftIsValid(buffer, size)) return 1;

	ntft->header.compression = COMPRESSION_NONE;
	ntft->header.format = BMAP_TYPE_NTFT;
	ntft->header.size = sizeof(*ntft);
	ntft->header.type = FILE_TYPE_BMAP;

	ntft->nPx = size / 2;
	ntft->px = (COLOR *) calloc(ntft->nPx, 2);
	memcpy(ntft->px, buffer, ntft->nPx * 2);

	return 0;
}

int ntftCreate(NTFT *ntft, DWORD *px, int nPx) {
	ntft->header.compression = COMPRESSION_NONE;
	ntft->header.format = BMAP_TYPE_NTFT;
	ntft->header.size = sizeof(*ntft);
	ntft->header.type = FILE_TYPE_BMAP;
	ntft->nPx = nPx;
	ntft->px = (COLOR *) malloc(nPx * 2);
	for (int i = 0; i < nPx; i++) {
		int a = px[i] >> 24;
		COLOR c = ColorConvertToDS(px[i]);
		if (a > 127) c |= 0x8000;
		ntft->px[i] = c;
	}
	return 0;
}

int ntftReadFile(NTFT *ntft, LPWSTR path) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);
	int n = ntftRead(ntft, lpBuffer, dwSize);
	free(lpBuffer);
	return n;
}

int ntftWrite(NTFT *ntft, LPWSTR path) {
	DWORD dwWritten;
	HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, ntft->px, ntft->nPx * 2, &dwWritten, NULL);
	CloseHandle(hFile);
	if (ntft->header.compression != COMPRESSION_NONE) {
		fileCompress(path, ntft->header.compression);
	}
	return 0;
}