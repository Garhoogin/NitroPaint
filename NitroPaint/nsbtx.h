#pragma once
#include <Windows.h>
#include "texture.h"
#include "filecommon.h"

#define NSBTX_TYPE_INVALID   0
#define NSBTX_TYPE_NNS       1
#define NSBTX_TYPE_BMD       2

typedef struct DICTENTRY_ {
	int sizeUnit;
	int offsetName;
	void *data;
} DICTENTRY;

typedef struct DICTIONARY_ {
	int nEntries;

	DICTENTRY entry;
	char *namesPtr;
} DICTIONARY;

typedef struct DICTTEXDATA_ {
	int texImageParam;
	int extraParam;
} DICTTEXDATA;

typedef struct DICTPLTTDATA_ {
	uint16_t offset;
	uint16_t flag;
} DICTPLTTDATA;


typedef struct BMD_DATA_ {
	int scale;
	int nBones;
	int nDisplaylists;
	int nMaterials;
	int materialsSize;
	int preTextureSize;
	int boneOffset;
	int displaylistOffset;
	int transformOffset;
	int field30;
	int field34;
	void *bones;
	void *displaylists;
	void *materials;
	void *preTexture;
} BMD_DATA;


typedef struct NSBTX_ { //these should not be converted to other formats
	OBJECT_HEADER header;
	int nTextures;
	int nPalettes;
	TEXELS *textures;
	PALETTE *palettes;

	void *mdl0;			//for handling NSBMD files as well
	int mdl0Size;
	BMD_DATA *bmdData;	//for handling BMD files
} NSBTX;

int nsbtxRead(NSBTX *nsbtx, char *buffer, int size);

int nsbtxIsValidBmd(char *buffer, unsigned int size);

int nsbtxReadFile(NSBTX *nsbtx, LPCWSTR path);

int nsbtxWriteFile(NSBTX *nsbtx, LPWSTR filename);

int nsbtxWrite(NSBTX *nsbtx, BSTREAM *stream);
