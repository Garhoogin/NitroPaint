#include "filecommon.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "ncer.h"
#include "nsbtx.h"
#include "nanr.h"
#include "texture.h"
#include "gdip.h"

LPCWSTR compressionNames[] = { L"None", L"LZ77", NULL };

int pathEndsWith(LPCWSTR str, LPCWSTR substr) {
	if (wcslen(substr) > wcslen(str)) return 0;
	LPCWSTR str1 = str + wcslen(str) - wcslen(substr);
	return !_wcsicmp(str1, substr);
}

LPCWSTR *getFormatNamesFromType(int type) {
	switch (type) {
		case FILE_TYPE_PALETTE:
			return paletteFormatNames;
		case FILE_TYPE_CHAR:
			return characterFormatNames;
		case FILE_TYPE_SCREEN:
			return screenFormatNames;
		case FILE_TYPE_CELL:
			return cellFormatNames;
		case FILE_TYPE_NANR:
			return cellAnimationFormatNames;
		default:
			return NULL;
	}
}

int fileIdentify(char *file, int size, LPCWSTR path) {
	char *buffer = file;
	int bufferSize = size;
	if (lz77IsCompressed(file, size)) {
		buffer = lz77decompress(file, size, &bufferSize);
	}

	int type = FILE_TYPE_INVALID;

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
			case 'BMD0':
			case '0DMB':
			{
				type = FILE_TYPE_NSBTX;
				break;
			}
			case 'NANR':
			case 'RNAN':
			{
				type = FILE_TYPE_NANR;
				break;
			}
		}
	}

	//no matches?
	if (type == FILE_TYPE_INVALID) {
		if (nitrotgaIsValid(buffer, bufferSize)) {
			type = FILE_TYPE_TEXTURE;
		} else {
			//image file?
			int width, height;
			DWORD *bits = gdipReadImage(path, &width, &height);
			if (bits != NULL && width && height) {
				free(bits);
				type = FILE_TYPE_IMAGE;
			} else {

				//test other formats
				if (combo2dIsValid(buffer, bufferSize)) type = FILE_TYPE_COMBO2D;
				else if (nclrIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
				else if (nscrIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_SCREEN;
				else if (ncgrIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_CHARACTER;
				else if (ncerIsValidHudson(buffer, bufferSize)) type = FILE_TYPE_CELL;

				//test for bin format files
				else {
					if (nclrIsValidBin(buffer, bufferSize) && pathEndsWith(path, L"ncl.bin")) type = FILE_TYPE_PALETTE;
					else if (nclrIsValidNtfp(buffer, bufferSize) && pathEndsWith(path, L".ntfp")) type = FILE_TYPE_PALETTE;
					else if (nscrIsValidBin(buffer, bufferSize) && pathEndsWith(path, L"nsc.bin")) type = FILE_TYPE_SCREEN;
					else if (ncgrIsValidBin(buffer, bufferSize) && pathEndsWith(path, L"ncg.bin")) type = FILE_TYPE_CHARACTER;
					else {
						//double check, without respect to the file name.
						if (nclrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
						else if (nscrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_SCREEN;
						else if (ncgrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_CHARACTER;
						else if (nclrIsValidNtfp(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
					}
				}
			}
		}
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

void fileFree(OBJECT_HEADER *header) {
	if(header->dispose != NULL) header->dispose(header);
}
