#pragma once
#include "compression.h"
#include "bstream.h"
#include <Windows.h>

#define FILE_TYPE_INVALID    0
#define FILE_TYPE_PALETTE    1
#define FILE_TYPE_CHARACTER  2
#define FILE_TYPE_SCREEN     3
#define FILE_TYPE_CELL       4
#define FILE_TYPE_NSBTX      5
#define FILE_TYPE_TEXTURE    7
#define FILE_TYPE_NANR       8
#define FILE_TYPE_IMAGE      9
#define FILE_TYPE_COMBO2D    10

#define COMPRESSION_NONE     0
#define COMPRESSION_LZ77     1

typedef struct OBJECT_HEADER_ {
	int size;
	int type;
	int format;
	int compression;
	void (*dispose) (struct OBJECT_HEADER_ *);
} OBJECT_HEADER;

typedef int (*OBJECT_READER) (OBJECT_HEADER *object, char *buffer, int size);

extern LPCWSTR compressionNames[];

LPCWSTR *getFormatNamesFromType(int type);

int fileIdentify(char *file, int size, LPCWSTR path);

void fileCompress(LPWSTR name, int compression);

void fileFree(OBJECT_HEADER *header);

int fileRead(LPCWSTR name, OBJECT_HEADER *object, OBJECT_READER reader);
