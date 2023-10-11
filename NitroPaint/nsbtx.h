#pragma once
#include <Windows.h>
#include "texture.h"
#include "filecommon.h"

#define NSBTX_TYPE_INVALID   0
#define NSBTX_TYPE_NNS       1
#define NSBTX_TYPE_BMD       2


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


typedef struct TexArc_ { //these should not be converted to other formats
	OBJECT_HEADER header;
	int nTextures;
	int nPalettes;
	TEXELS *textures;
	PALETTE *palettes;

	void *mdl0;			//for handling NSBMD files as well
	int mdl0Size;
	BMD_DATA *bmdData;	//for handling BMD files
} TexArc;

void TexarcInit(TexArc *nsbtx, int format);

int TexarcRead(TexArc *nsbtx, char *buffer, int size);

int TexarcIsValidBmd(char *buffer, unsigned int size);

int TexarcReadFile(TexArc *nsbtx, LPCWSTR path);

int TexarcWriteFile(TexArc *nsbtx, LPWSTR filename);

int TexarcWrite(TexArc *nsbtx, BSTREAM *stream);
