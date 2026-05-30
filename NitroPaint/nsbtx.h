#pragma once
#include "texture.h"
#include "filecommon.h"

#define NSBTX_TYPE_INVALID   0
#define NSBTX_TYPE_NNS       1
#define NSBTX_TYPE_BMD       2
#define NSBTX_TYPE_STTEX     3


// Texture archive format keys

#define NSBTX_KEY_MAX_TEXNAME_LEN (OBJ_KEY_MAX+0)  // Max texture name length
#define NSBTX_KEY_MAX_PLTNAME_LEN (OBJ_KEY_MAX+1)  // Max palette name length
#define NSBTX_KEY_TEXFMT_SUPPORT  (OBJ_KEY_MAX+2)  // Supported texture format bitmap


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
	ObjHeader header;
	int nTextures;
	int nPalettes;
	TEXELS *textures;
	PALETTE *palettes;

	void *mdl0;			//for handling NSBMD files as well
	int mdl0Size;
	BMD_DATA *bmdData;	//for handling BMD files
} TexArc;

void TexarcRegisterFormats(void);

int TexarcIsValidBmd(const unsigned char *buffer, unsigned int size);

int TexarcGetTextureIndexByName(TexArc *nsbtx, const char *name);

int TexarcGetPaletteIndexByName(TexArc *nsbtx, const char *name);

TEXELS *TexarcGetTextureByName(TexArc *nsbtx, const char *name);

PALETTE *TexarcGetPaletteByName(TexArc *nsbtx, const char *name);

int TexarcAddTexture(TexArc *nsbtx, TEXELS *texture);

int TexarcAddPalette(TexArc *nsbtx, PALETTE *palette);
