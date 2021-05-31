#pragma once
#include <Windows.h>
#include "texture.h"
#include "filecommon.h"

typedef struct DICTENTRY_ {
	int sizeUnit;
	int offsetName;
	void *data;
} DICTENTRY;

typedef struct PTREENODE_{
	int refBit;
	int idxLeft;
	int idxRight;
	int idxEntry;
} PTREENODE;

typedef struct DICTIONARY_ {
	int revision;
	int nEntries;
	int sizeDictBlk;
	int ofsEntry;

	PTREENODE *node;
	int nNode;

	DICTENTRY entry;
	char **names;
} DICTIONARY;

typedef struct DICTTEXDATA_ {
	int texImageParam;
	int extraParam;
} DICTTEXDATA;

typedef struct DICTPLTTDATA_ {
	WORD offset;
	WORD flag;
} DICTPLTTDATA;

typedef struct NSBTX_ {
	OBJECT_HEADER header;
	int nTextures;
	int nPalettes;
	TEXELS *textures;
	PALETTE *palettes;
	DICTIONARY textureDictionary;
	DICTIONARY paletteDictionary;
} NSBTX;

int nsbtxRead(NSBTX *nsbtx, char *buffer, int size);

int nsbtxReadFile(NSBTX *nsbtx, LPCWSTR path);

void nsbtxSaveFile(LPWSTR filename, NSBTX *nsbtx);