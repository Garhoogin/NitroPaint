#include "filecommon.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "ncer.h"
#include "nsbtx.h"
#include "nanr.h"
#include "texture.h"
#include "gdip.h"

LPCWSTR compressionNames[] = { L"None", L"LZ77", L"LZ11", L"LZ11 COMP", L"Huffman 4", L"Huffman 8", NULL };

int pathEndsWith(LPCWSTR str, LPCWSTR substr) {
	if (wcslen(substr) > wcslen(str)) return 0;
	LPCWSTR str1 = str + wcslen(str) - wcslen(substr);
	return !_wcsicmp(str1, substr);
}

int pathStartsWith(LPCWSTR str, LPCWSTR substr) {
	if (wcslen(substr) > wcslen(str)) return 1;
	return !_wcsnicmp(str, substr, wcslen(substr));
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

void fileInitCommon(OBJECT_HEADER *header, int type, int format) {
	memset(header, 0, header->size);
	header->type = type;
	header->format = format;
	header->compression = COMPRESSION_NONE;
}

int fileIdentify(char *file, int size, LPCWSTR path) {
	char *buffer = file;
	int bufferSize = size;
	if (getCompressionType(file, size) != COMPRESSION_NONE) {
		buffer = decompress(file, size, &bufferSize);
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
					if (nclrIsValidBin(buffer, bufferSize) && (pathEndsWith(path, L"ncl.bin") || 
															   pathEndsWith(path, L"icl.bin") || 
															   pathEndsWith(path, L".pltt") || 
															   pathEndsWith(path, L".nbfp"))) type = FILE_TYPE_PALETTE;
					else if (nclrIsValidNtfp(buffer, bufferSize) && pathEndsWith(path, L".ntfp")) type = FILE_TYPE_PALETTE;
					else if (nscrIsValidBin(buffer, bufferSize) && (pathEndsWith(path, L"nsc.bin") || 
																	pathEndsWith(path, L"isc.bin") ||
																	pathEndsWith(path, L".nbfs"))) type = FILE_TYPE_SCREEN;
					else if (ncgrIsValidBin(buffer, bufferSize) && (pathEndsWith(path, L"ncg.bin") || 
																	pathEndsWith(path, L"icg.bin") || 
																	pathEndsWith(path, L".char") ||
																	pathEndsWith(path, L".nbfc"))) type = FILE_TYPE_CHARACTER;
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

unsigned short computeCrc16(unsigned char *data, int length, unsigned short init) {
	unsigned short r = init;
	unsigned short tbl[] = { 
		0x0000, 0xCC01, 0xD801, 0x1400, 
		0xF001, 0x3C00, 0x2800, 0xE401, 
		0xA001, 0x6C00, 0x7800, 0xB401, 
		0x5000, 0x9C01, 0x8801, 0x4400 
	};

	for (int i = 0; i < length; i++) {
		unsigned short c = (tbl[*data & 0xF] ^ (r >> 4)) ^ tbl[r & 0xF];
		r = (tbl[*data >> 4] ^ (c >> 4)) ^ tbl[c & 0xF];
		data++;
	}
	return r;
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
	compressedBuffer = compress(buffer, dwSizeLow, compression, &compressedSize);

	hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, compressedBuffer, compressedSize, &dwWritten, NULL);
	CloseHandle(hFile);
	if (compressedBuffer != buffer) free(compressedBuffer);
	free(buffer);
}

void fileFree(OBJECT_HEADER *header) {
	if(header->dispose != NULL) header->dispose(header);
}

void *fileReadWhole(LPCWSTR name, int *size) {
	HANDLE hFile = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwRead, dwSizeLow, dwSizeHigh = 0;

	void *buffer;
	if (pathStartsWith(name, L"\\\\.\\pipe\\")) {
		//pipe protocol: first 4 bytes file size, followed by file data.
		ReadFile(hFile, &dwSizeLow, 4, &dwRead, NULL);
		buffer = malloc(dwSizeLow);
		ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	} else {
		dwSizeLow = GetFileSize(hFile, &dwSizeHigh);
		buffer = malloc(dwSizeLow);
		ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	}

	CloseHandle(hFile);
	*size = dwSizeLow;
	return buffer;
}

int fileRead(LPCWSTR name, OBJECT_HEADER *object, OBJECT_READER reader) {
	int size;
	void *buffer = fileReadWhole(name, &size);

	int status;
	int compType = getCompressionType(buffer, size);
	if (compType == COMPRESSION_NONE) {
		status = reader(object, buffer, size);
	} else {
		int decompressedSize;
		void *decompressed = decompress(buffer, size, &decompressedSize);
		status = reader(object, decompressed, decompressedSize);
		free(decompressed);
		object->compression = compType;
	}

	free(buffer);
	return status;
}

int fileWrite(LPCWSTR name, OBJECT_HEADER *object, OBJECT_WRITER writer) {
	BSTREAM stream;
	bstreamCreate(&stream, NULL, 0);
	int status = writer(object, &stream);

	if (status == 0) {
		if (object->compression != COMPRESSION_NONE) {
			bstreamCompress(&stream, object->compression, 0, 0);
		}

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, stream.buffer, stream.size, &dwWritten, NULL);
		CloseHandle(hFile);
	}

	bstreamFree(&stream);
	return status;
}
