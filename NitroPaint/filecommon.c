#include "filecommon.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "ncer.h"
#include "nsbtx.h"
#include "ntft.h"

int fileIdentify(char *file, int size) {
	char *buffer = file;
	int bufferSize = size;
	if (lz77IsCompressed(file, size)) {
		buffer = lz77decompress(file, size, &bufferSize);
	}

	int type = FILE_TYPE_UNKNOWN;

	//test Nitro formats
	if (bufferSize >= 4) {
		unsigned int magic = *(unsigned int *) buffer;
		switch (magic) {
			case 'NCLR':
			case 'RLCN':
			{
				type = FILE_TYPE_PALETTE;
				break;
			}
			case 'NCGR':
			case 'RGCN':
			{
				type = FILE_TYPE_CHARACTER;
				break;
			}
			case 'NSCR':
			case 'RCSN':
			{
				type = FILE_TYPE_SCREEN;
				break;
			}
			case 'NCER':
			case 'RECN':
			{
				type = FILE_TYPE_CELL;
				break;
			}
			case 'BTX0':
			case '0XTB':
			{
				type = FILE_TYPE_NSBTX;
				break;
			}
		}
	}

	//no matches?
	if (type == FILE_TYPE_UNKNOWN) {
		//test other formats
		if (nclrIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
		else if (nscrIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_SCREEN;
		else if (ncgrIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_CHARACTER;
		else if (ncerIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_CELL;
		else if (ntftIsValid(buffer, bufferSize)) type = FILE_TYPE_BMAP;
	}

	if (buffer != file) {
		free(buffer);
	}
	return type;
}

void fileCompress(LPWSTR name, int compression) {
	HANDLE hFile = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeLow, dwSizeHigh, dwRead, dwWritten;
	dwSizeLow = GetFileSize(hFile, &dwSizeHigh);
	char *buffer = (char *) calloc(dwSizeLow, 1);
	ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	CloseHandle(hFile);
	int compressedSize;
	char *compressedBuffer;
	switch (compression) {
		case COMPRESSION_NONE:
			compressedBuffer = buffer;
			compressedSize = dwSizeLow;
			break;
		case COMPRESSION_LZ77:
			compressedBuffer = lz77compress(buffer, dwSizeLow, &compressedSize);
			break;
	}
	hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, compressedBuffer, compressedSize, &dwWritten, NULL);
	CloseHandle(hFile);
	if (compressedBuffer != buffer) free(compressedBuffer);
	free(buffer);
}