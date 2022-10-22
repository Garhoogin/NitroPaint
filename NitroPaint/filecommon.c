#include "filecommon.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "ncer.h"
#include "nsbtx.h"
#include "nanr.h"
#include "texture.h"
#include "gdip.h"
#include "g2dfile.h"

LPCWSTR compressionNames[] = { L"None", L"LZ77", L"LZ11", L"LZ11 COMP", L"Huffman 4", L"Huffman 8", L"LZ77 Header", NULL };

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
	int size = header->size; //restore this
	memset(header, 0, size);
	header->type = type;
	header->format = format;
	header->compression = COMPRESSION_NONE;
	header->size = size;
}

int screenCharComparator(const void *v1, const void *v2) {
	uint16_t u1 = *(const uint16_t *) v1;
	uint16_t u2 = *(const uint16_t *) v2;
	return ((int) u1) - ((int) u2);
}

//
// Attempt to guess if a file is a palette, character graphics, or screen.
//
int fileGuessPltChrScr(unsigned char *ptr, int size) {
	int canBePalette = 1, canBeChar = 1, canBeScreen = 1;
	int n16 = size / 2;

	if (size & 1 || size > 32768) canBePalette = 0;
	if (size & 0x1F || !size) canBeChar = 0;
	if (!nscrIsValidBin(ptr, size)) canBeScreen = 0;

	if(canBePalette) {
		COLOR *asColors = (COLOR *) ptr;
		for (int i = 0; i < n16; i++) {
			if (asColors[i] & 0x8000) {
				canBePalette = 0;
				break;
			}
		}
	}

	if (!canBePalette && !canBeChar && !canBeScreen) return FILE_TYPE_INVALID;
	if (canBePalette && !canBeChar && !canBeScreen) return FILE_TYPE_PALETTE;
	if (!canBePalette && canBeChar && !canBeScreen) return FILE_TYPE_CHARACTER;
	if (!canBePalette && !canBeChar && canBeScreen) return FILE_TYPE_SCREEN;

	//Use some heuristics. Check the flip bytes for screen.
	if (canBeScreen) {
		int nFlipped = 0;
		uint16_t *screen = (uint16_t *) ptr;
		for (int i = 0; i < n16; i++) {
			int f = (screen[i] >> 10) & 3;
			if (f) nFlipped++;
		}
		if (nFlipped <= (size / 2) / 4) {
			canBeScreen++;
		}
		if (nFlipped > (size / 2) / 2) {
			canBeScreen--;
			if (canBeScreen < 0) canBeScreen = 0;
		}
	}

	//double check valid returns
	if (canBePalette && !canBeChar && !canBeScreen) return FILE_TYPE_PALETTE;
	if (!canBePalette && canBeChar && !canBeScreen) return FILE_TYPE_CHARACTER;

	//heuristics for palettes
	if (canBePalette) {
		//
	}

	//Try a different heuristic for screens. Check number of tiles with a
	//character index greater than the previous one.
	if (canBeScreen) {
		uint16_t *screen = (uint16_t *) ptr;
		int lastChar = -1, nIncreasing = 0;
		for (int i = 0; i < n16; i++) {
			int c = screen[i] & 0x3FF;
			if (c >= lastChar) {
				nIncreasing++;
			}
			lastChar = c;
		}

		if (nIncreasing * 2 < n16) {
			canBeScreen--;
			if (canBeScreen < 0) canBeScreen = 0;
		}
		if (nIncreasing * 4 >= 3 * n16) {
			canBeScreen++;
		}
	}

	//Try another heuristic for screens. Find min and max char, and check total usage.
	if (canBeScreen) {
		uint16_t *screen = (uint16_t *) ptr;

		uint16_t *sorted = (uint16_t *) calloc(size, 1);
		memcpy(sorted, ptr, size);
		qsort(sorted, size / 2, 2, screenCharComparator);
		int minChar = sorted[0] & 0x3FF;
		int maxChar = sorted[n16 - 1] & 0x3FF;
		int charRange = maxChar - minChar + 1;

		int lastChar = minChar - 1;
		int nCounted = 0;
		for (int i = 0; i < n16; i++) {
			int chr = sorted[i] & 0x3FF;

			if (chr != lastChar) {
				nCounted++;
			}
			lastChar = chr;
		}

		//shoot for >=25%
		if (nCounted * 4 < charRange) {
			canBeScreen--;
			if (canBeScreen < 0) canBeScreen = 0;
		}
		if (nCounted * 4 >= 3 * charRange) {
			canBeScreen++;
		}
		free(sorted);
	}

	//double check valid returns
	if (!canBePalette && canBeChar && !canBeScreen) return FILE_TYPE_CHARACTER;
	if (!canBePalette && !canBeChar && canBeScreen) return FILE_TYPE_SCREEN;

	//heuristics for character
	if (canBeChar) {
		//check for identical runs of nonzero bytes
		int runLength = 0;
		for (int i = 0; i < size; i++) {
			unsigned char c = ptr[i];
			if (!c) continue;

			int len = 1;
			for (; i < size; i++) {
				unsigned char c2 = ptr[i];
				if (c2 == c) len++;
				else break;
			}
			if (len > runLength) runLength = len;
		}
		if (runLength < 0x10) {
			canBeChar--;
			if (canBeChar < 0) canBeChar = 0;
		}
		if (runLength >= 0x18) {
			canBeChar++;
		}
	}

	//final judgement
	if (canBePalette > canBeScreen && canBePalette > canBeChar) return FILE_TYPE_PALETTE;
	if (canBeScreen > canBePalette && canBeScreen > canBeChar) return FILE_TYPE_SCREEN;
	if (canBeChar > canBePalette && canBeChar > canBeScreen) return FILE_TYPE_CHAR;
	if (canBePalette) return FILE_TYPE_PALETTE;
	if (canBeScreen) return FILE_TYPE_SCREEN;
	if (canBeChar) return FILE_TYPE_CHAR;

	//when in doubt, it's character graphics
	return FILE_TYPE_CHAR;
}

int fileIdentify(char *file, int size, LPCWSTR path) {
	char *buffer = file;
	int bufferSize = size;
	if (getCompressionType(file, size) != COMPRESSION_NONE) {
		buffer = decompress(file, size, &bufferSize);
	}

	int type = FILE_TYPE_INVALID;

	//test Nitro formats
	if (g2dIsValid(buffer, bufferSize)) {
		unsigned int magic = *(unsigned int *) buffer;
		switch (magic) {
			case 'NCLR':
			case 'RLCN':
			case 'NCCL':
			case 'LCCN':
				type = FILE_TYPE_PALETTE;
				break;
			case 'NCGR':
			case 'RGCN':
			case 'NCCG':
			case 'GCCN':
				type = FILE_TYPE_CHARACTER;
				break;
			case 'NSCR':
			case 'RCSN':
			case 'NCSC':
			case 'CSCN':
				type = FILE_TYPE_SCREEN;
				break;
			case 'NCER':
			case 'RECN':
				type = FILE_TYPE_CELL;
				break;
			case 'BTX0':
			case '0XTB':
			case 'BMD0':
			case '0DMB':
				type = FILE_TYPE_NSBTX;
				break;
			case 'NANR':
			case 'RNAN':
				type = FILE_TYPE_NANR;
				break;
			case 'NMCR':
			case 'RCMN':
				type = FILE_TYPE_NMCR;
				break;
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
				if (nsbtxIsValidBmd(buffer, bufferSize)) type = FILE_TYPE_NSBTX;
				else if (combo2dIsValid(buffer, bufferSize)) type = FILE_TYPE_COMBO2D;
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
						type = fileGuessPltChrScr(buffer, bufferSize);

						//last ditch effort
						if (type == FILE_TYPE_INVALID) {
							if (nclrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
							else if (nscrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_SCREEN;
							else if (ncgrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_CHARACTER;
							else if (nclrIsValidNtfp(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
						}
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
