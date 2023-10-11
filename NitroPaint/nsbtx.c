#include "nsbtx.h"
#include "texture.h"
#include "nns.h"

#include <Windows.h>
#include <stdio.h>

//----- BEGIN Code for constructing an TexArc dictionary

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

//----- END Code for constructing an TexArc dictionary

void TexarcFree(OBJECT_HEADER *header) {
	TexArc *nsbtx = (TexArc *) header;
	if (nsbtx->textures != NULL) {
		for (int i = 0; i < nsbtx->nTextures; i++) {
			TEXELS *texture = nsbtx->textures + i;
			if (texture->texel != NULL) free(texture->texel);
			if (texture->cmp != NULL) free(texture->cmp);
		}
		free(nsbtx->textures);
		nsbtx->textures = NULL;
	}
	if (nsbtx->palettes != NULL) {
		for (int i = 0; i < nsbtx->nPalettes; i++) {
			PALETTE *palette = nsbtx->palettes + i;
			if (palette->pal != NULL) free(palette->pal);
		}
		free(nsbtx->palettes);
		nsbtx->palettes = NULL;
	}
	if (nsbtx->mdl0 != NULL) {
		free(nsbtx->mdl0);
		nsbtx->mdl0 = NULL;
		nsbtx->mdl0Size = 0;
	}
	if (nsbtx->bmdData != NULL) {
		BMD_DATA *bmd = nsbtx->bmdData;
		if (bmd->bones != NULL) free(bmd->bones);
		if (bmd->displaylists != NULL) free(bmd->displaylists);
		if (bmd->materials != NULL) free(bmd->materials);
		if (bmd->preTexture) free(bmd->preTexture);
		free(bmd);
		nsbtx->bmdData = NULL;
	}
	
}

void TexarcInit(TexArc *nsbtx, int format) {
	nsbtx->header.size = sizeof(TexArc);
	fileInitCommon((OBJECT_HEADER *) nsbtx, FILE_TYPE_NSBTX, format);
	nsbtx->header.dispose = TexarcFree;
}

//TexArc code adapted from Gericom's code in Fvery File Explorer.

unsigned char *readDictionary(DICTIONARY *dict, unsigned char *base, int entrySize) {
	unsigned char *pos = base;
	int nEntries = *(uint8_t *) (pos + 1);
	int dictSize = *(uint16_t *) (pos + 4);
	int ofsEntry = *(uint16_t *) (pos + 6);
	dict->nEntries = nEntries;
	pos += ofsEntry; //skips the P tree

	dict->entry.sizeUnit = *(uint16_t *) (pos + 0);
	dict->entry.offsetName = *(uint16_t *) (pos + 2);
	pos += 4;

	dict->entry.data = pos;
	pos += entrySize * dict->nEntries;

	dict->namesPtr = pos;
	pos = base + dictSize; //end of dict
	return pos;
}

int TexarcIsValidNsbtx(char *buffer, int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;

	//check magic (only TexArc or NSBMD)
	if ((buffer[0] != 'B' || buffer[1] != 'T' || buffer[2] != 'X' || buffer[3] != '0') &&
		(buffer[0] != 'B' || buffer[1] != 'M' || buffer[2] != 'D' || buffer[3] != '0')) return 0;

	return 1;
}

int TexarcIsValidBmd(char *buffer, unsigned int size) {
	if (size < 0x3C || (size & 3)) return 0;

	int scale = *(int *) (buffer + 0);
	if (scale >= 32) return 0;
	uint32_t boneOffset = *(uint32_t *) (buffer + 0x08);
	uint32_t displaylistOffset = *(uint32_t *) (buffer + 0x10);
	uint32_t texturesOffset = *(uint32_t *) (buffer + 0x18);
	uint32_t palettesOffset = *(uint32_t *) (buffer + 0x20);
	uint32_t materialsOffset = *(uint32_t *) (buffer + 0x28);

	uint32_t nDisplaylists = *(uint32_t *) (buffer + 0x0C);
	uint32_t nTextures = *(uint32_t *) (buffer + 0x14);
	uint32_t nPalettes = *(uint32_t *) (buffer + 0x1C);
	uint32_t nMaterials = *(uint32_t *) (buffer + 0x24);

	//bounds+alignment
	if (boneOffset < 0x3C || displaylistOffset < 0x3C || texturesOffset < 0x3C
		|| palettesOffset < 0x3C || materialsOffset < 0x3C) return 0;
	if (boneOffset > size || displaylistOffset > size || texturesOffset > size
		|| palettesOffset > size || materialsOffset > size) return 0;
	if ((boneOffset & 3) || (displaylistOffset & 3) || (texturesOffset & 3)
		|| (palettesOffset & 3) || (materialsOffset & 3)) return 0;
	if (displaylistOffset + nDisplaylists * 8 > size) return 0;
	if (texturesOffset + nTextures * 0x14 > size) return 0;
	if (palettesOffset + nPalettes * 0x10 > size) return 0;
	if (materialsOffset + nMaterials * 0x30 > size) return 0;
	if (nDisplaylists == 0 && nMaterials == 0) return 0;

	unsigned char *textureSection = buffer + texturesOffset;
	for (unsigned int i = 0; i < nTextures; i++) {
		unsigned char *thisTex = textureSection + i * 0x14;

		uint32_t nameOffset = *(uint32_t *) (thisTex + 0x00);
		uint32_t textureOffset = *(uint32_t *) (thisTex + 0x04);
		uint32_t texelSize = *(uint32_t *) (thisTex + 0x08);
		uint32_t width = *(uint16_t *) (thisTex + 0x0C);
		uint32_t height = *(uint16_t *) (thisTex + 0x0E);
		uint32_t texImageParam = *(uint32_t *) (thisTex + 0x10);
		if (nameOffset < 0x3C || textureOffset < 0x3C || nameOffset >= size || textureOffset >= size)
			return 0;
		if (width != TEXW(texImageParam) || height != TEXH(texImageParam) || FORMAT(texImageParam) == 0)
			return 0;
		if (texelSize & (texelSize - 1))
			return 0;
	}

	return 1;
}

int TexarcReadNsbtx(TexArc *nsbtx, char *buffer, int size) {
	//is it valid?
	if (!TexarcIsValidNsbtx(buffer, size)) return 1;

	TexarcInit(nsbtx, NSBTX_TYPE_NNS);
	//iterate over each section
	int *sectionOffsets = (int *) (buffer + 0x10);
	int nSections = *(short *) (buffer + 0xE);
	//find the TEX0 section
	char *tex0 = NULL;
	int tex0Offset = 0;
	nsbtx->mdl0 = NULL;
	nsbtx->mdl0Size = 0;
	for (int i = 0; i < nSections; i++) {
		char *sect = buffer + sectionOffsets[i];
		if (sect[0] == 'T' && sect[1] == 'E' && sect[2] == 'X' && sect[3] == '0') {
			tex0 = sect;
			tex0Offset = sect - buffer;
			break;
		} else if (sect[0] == 'M' && sect[1] == 'D' && sect[2] == 'L' && sect[3] == '0') {
			nsbtx->mdl0Size = *(DWORD *) (sect + 4) - 8;
			nsbtx->mdl0 = malloc(nsbtx->mdl0Size);
			memcpy(nsbtx->mdl0, sect + 8, nsbtx->mdl0Size);
		}
	}
	if (tex0 == NULL) {
		if (nsbtx->mdl0 != NULL) {
			free(nsbtx->mdl0);
			nsbtx->mdl0 = NULL;
			nsbtx->mdl0Size = 0;
		}
		return 1;
	}

	//next, process the tex0 section.
	int blockSize = *(int *) (tex0 + 0x4);
	
	//texture header
	int textureDataSize = (*(uint16_t *) (tex0 + 0xC)) << 3;
	int textureInfoOffset = *(uint16_t *) (tex0 + 0xE); //dictionary
	int textureDataOffset = *(uint32_t *) (tex0 + 0x14); //ofsTex

	int compressedTextureDataSize = (*(uint16_t *) (tex0 + 0x1C)) << 3;
	int compressedTextureInfoOffset = *(uint16_t *) (tex0 + 0x1E); //dictionary
	int compressedTextureDataOffset = *(uint32_t *) (tex0 + 0x24); //ofsTex
	int compressedTextureInfoDataOffset = *(uint32_t *) (tex0 + 0x28); //ofsTexPlttIdx

	int paletteDataSize = (*(uint16_t *) (tex0 + 0x30)) << 3;
	int paletteInfoOffset = *(uint32_t *) (tex0 + 0x34); //dictionary
	int paletteDataOffset = *(uint32_t *) (tex0 + 0x38);

	char *texInfo = tex0 + textureInfoOffset;
	char *palInfo = tex0 + paletteInfoOffset;

	DICTIONARY dictTex;
	readDictionary(&dictTex, tex0 + textureInfoOffset, sizeof(DICTTEXDATA));
	DICTTEXDATA *dictTexData = (DICTTEXDATA *) dictTex.entry.data;
	TEXELS *texels = (TEXELS *) calloc(dictTex.nEntries, sizeof(TEXELS));
	
	DICTIONARY dictPal;
	char *pos = readDictionary(&dictPal, tex0 + paletteInfoOffset, sizeof(DICTPLTTDATA));
	DICTPLTTDATA *dictPalData = (DICTPLTTDATA *) dictPal.entry.data;
	PALETTE *palettes = (PALETTE *) calloc(dictPal.nEntries, sizeof(PALETTE));

	int baseOffsetTex = textureDataOffset;
	int baseOffsetTex4x4 = compressedTextureDataOffset;
	int baseOffsetTex4x4Info = compressedTextureInfoDataOffset;
	int texPlttSetOffset = tex0Offset;
	for (int i = 0; i < dictTex.nEntries; i++) {
		//read a texture from pos.
		DICTTEXDATA *texData = dictTexData + i;
		int offset = OFFSET(texData->texImageParam);

		int width = TEXW(texData->texImageParam);
		int height = TEXH(texData->texImageParam);
		int texelSize = TxGetTexelSize(width, height, texData->texImageParam);

		uint32_t paramEx = texData->extraParam;
		int origWidth = width, origHeight = height;
		if (!(paramEx & 0x80000000)) {
			origWidth = (paramEx >> 0) & 0x7FF;
			origHeight = (paramEx >> 11) & 0x7FF;
		}

		if (FORMAT(texData->texImageParam) == CT_4x4) {
			texels[i].texImageParam = texData->texImageParam;
			texels[i].texel = calloc(texelSize, 1);
			texels[i].cmp = calloc(texelSize >> 1, 1);
			memcpy(texels[i].texel, tex0 + offset + baseOffsetTex4x4, texelSize);
			memcpy(texels[i].cmp, tex0 + baseOffsetTex4x4Info + offset / 2, texelSize >> 1);
			
		} else {
			texels[i].texImageParam = texData->texImageParam;
			texels[i].cmp = NULL;
			texels[i].texel = calloc(texelSize, 1);
			memcpy(texels[i].texel, tex0 + offset + baseOffsetTex, texelSize);
		}
		memcpy(texels[i].name, dictTex.namesPtr + i * 16, 16);
		texels[i].height = origHeight;
	}

	for (int i = 0; i < dictPal.nEntries; i++) {
		DICTPLTTDATA *palData = dictPalData + i;
		
		//find the length of the palette, by finding the least offset greater than this one's palette
		int offset = size - paletteDataOffset - (palData->offset << 3) - tex0Offset + (palData->offset << 3);
		for (int j = 0; j < dictPal.nEntries; j++) {
			int offset2 = dictPalData[j].offset << 3;
			if (offset2 <= (palData->offset << 3)) continue;
			if (offset2 < offset || offset == 0) {
				offset = offset2;
			}
		}

		int nColors = (offset - (palData->offset << 3)) >> 1;
		if (palData->flag & 0x0001) nColors = 4; //4-color flag
		palettes[i].nColors = nColors;
		palettes[i].pal = (COLOR *) calloc(nColors, 2);
		memcpy(palettes[i].pal, tex0 + paletteDataOffset + (palData->offset << 3), nColors * 2);

		memcpy(palettes[i].name, dictPal.namesPtr + i * 16, 16);
	}

	//finally write out tex and pal info
	nsbtx->nTextures = dictTex.nEntries;
	nsbtx->nPalettes = dictPal.nEntries;
	nsbtx->textures = texels;
	nsbtx->palettes = palettes;
	return 0;
}

int TexarcReadBmd(TexArc *nsbtx, unsigned char *buffer, int size) {
	if (!TexarcIsValidBmd(buffer, size)) return 1;

	TexarcInit(nsbtx, NSBTX_TYPE_BMD);
	nsbtx->bmdData = (BMD_DATA *) calloc(1, sizeof(BMD_DATA));
	BMD_DATA *bmd = nsbtx->bmdData;

	bmd->scale = *(uint32_t *) (buffer + 0x00);
	bmd->nBones = *(uint32_t *) (buffer + 0x04);
	bmd->boneOffset = *(uint32_t *) (buffer + 0x08);
	bmd->nDisplaylists = *(uint32_t *) (buffer + 0x0C);
	bmd->displaylistOffset = *(uint32_t *) (buffer + 0x10);
	bmd->nMaterials = *(uint32_t *) (buffer + 0x24);
	bmd->transformOffset = *(uint32_t *) (buffer + 0x2C);
	bmd->field30 = *(uint32_t *) (buffer + 0x30);
	bmd->field34 = *(uint32_t *) (buffer + 0x34);

	nsbtx->nTextures = *(uint32_t *) (buffer + 0x14);
	nsbtx->nPalettes = *(uint32_t *) (buffer + 0x1C);
	nsbtx->textures = (TEXELS *) calloc(nsbtx->nTextures, sizeof(TEXELS));
	nsbtx->palettes = (PALETTE *) calloc(nsbtx->nPalettes, sizeof(PALETTE));

	//read textures and palettes
	unsigned char *texDescriptors = buffer + *(uint32_t *) (buffer + 0x18);
	unsigned char *palDescriptors = buffer + *(uint32_t *) (buffer + 0x20);
	for (int i = 0; i < nsbtx->nTextures; i++) {
		unsigned char *thisTex = texDescriptors + i * 0x14;
		TEXELS *texture = nsbtx->textures + i;

		uint32_t nameOffset = *(uint32_t *) (thisTex + 0x00);
		uint32_t texelOffset = *(uint32_t *) (thisTex + 0x04);
		uint32_t texelSize = *(uint32_t *) (thisTex + 0x08);
		uint32_t texImageParam = *(uint32_t *) (thisTex + 0x10);
		char *name = buffer + nameOffset;
		int format = FORMAT(texImageParam);

		texture->texImageParam = texImageParam;
		memcpy(texture->name, name, min(strlen(name), 16));
		texture->texel = (char *) calloc(texelSize, 1);
		memcpy(texture->texel, buffer + texelOffset, texelSize);
		if (format == CT_4x4) {
			texture->cmp = (short *) calloc(texelSize / 2, 1);
			memcpy(texture->cmp, buffer + texelOffset + texelSize, texelSize / 2);
		}
		texture->height = TEXH(texImageParam);
	}
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		unsigned char *thisPal = palDescriptors + i * 0x10;
		PALETTE *palette = nsbtx->palettes + i;

		uint32_t nameOffset = *(uint32_t *) (thisPal + 0x00);
		uint32_t paletteOffset = *(uint32_t *) (thisPal + 0x04);
		uint32_t paletteSize = *(uint32_t *) (thisPal + 0x08);
		char *name = buffer + nameOffset;

		palette->nColors = paletteSize / 2;
		palette->pal = (COLOR *) calloc(palette->nColors, sizeof(COLOR));
		memcpy(palette->pal, buffer + paletteOffset, paletteSize);
		memcpy(palette->name, name, min(strlen(name), 16));
	}

	//all the other stuff
	uint32_t materialsOffset = *(uint32_t *) (buffer + 0x28);
	uint32_t materialsEnd = materialsOffset + bmd->nMaterials * 0x30;
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = buffer + materialsOffset + i * 0x30;
		uint32_t nameOffset = *(uint32_t *) material;
		uint32_t nameEnd = nameOffset + 1 + strlen(buffer + nameOffset);
		if (nameEnd > materialsEnd) {
			materialsEnd = nameEnd;
		}
	}
	materialsEnd = (materialsEnd + 3) & ~3;
	bmd->materials = calloc(materialsEnd - materialsOffset, 1);
	bmd->materialsSize = materialsEnd - materialsOffset;
	memcpy(bmd->materials, buffer + materialsOffset, materialsEnd - materialsOffset);

	//make name offsets in the material section relative
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = ((unsigned char *) bmd->materials) + i * 0x30;
		*(uint32_t *) material -= materialsOffset;
	}

	uint32_t preTextureSize = *(uint32_t *) (buffer + 0x18) - 0x3C;
	bmd->preTextureSize = preTextureSize;
	bmd->preTexture = calloc(bmd->preTextureSize, 1);
	memcpy(bmd->preTexture, buffer + 0x3C, preTextureSize);

	return 0;
}

int TexarcRead(TexArc *nsbtx, char *buffer, int size) {
	if (TexarcIsValidNsbtx(buffer, size)) return TexarcReadNsbtx(nsbtx, buffer, size);
	if (TexarcIsValidBmd(buffer, size)) return TexarcReadBmd(nsbtx, buffer, size);
	return 1;
}

int TexarcReadFile(TexArc *nsbtx, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nsbtx, (OBJECT_READER) TexarcRead);
}

static char *TexarciGetTextureNameCallback(void *texels) {
	return ((TEXELS *) texels)->name;
}

static char *TexarciGetPaletteNameCallback(void *palette) {
	return ((PALETTE *) palette)->name;
}

int TexarcWriteNsbtx(TexArc *nsbtx, BSTREAM *stream) {
	if (nsbtx->mdl0 != NULL) {
		BYTE fileHeader[] = { 'B', 'M', 'D', '0', 0xFF, 0xFE, 1, 0, 0, 0, 0, 0, 0x10, 0, 2, 0, 0x18, 0, 0, 0, 0, 0, 0, 0 };

		bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	} else {
		BYTE fileHeader[] = { 'B', 'T', 'X', '0', 0xFF, 0xFE, 1, 0, 0, 0, 0, 0, 0x10, 0, 1, 0, 0x14, 0, 0, 0 };

		bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	}

	if (nsbtx->mdl0 != NULL) {
		BYTE mdl0Header[] = { 'M', 'D', 'L', '0', 0, 0, 0, 0 };
		*(DWORD *) (mdl0Header + 4) = nsbtx->mdl0Size + 8;
		bstreamWrite(stream, mdl0Header, sizeof(mdl0Header));
		bstreamWrite(stream, nsbtx->mdl0, nsbtx->mdl0Size);
	}
	
	DWORD tex0Offset = stream->pos;

	BYTE tex0Header[] = { 'T', 'E', 'X', '0', 0, 0, 0, 0 };
	bstreamWrite(stream, tex0Header, sizeof(tex0Header));

	BSTREAM texData, tex4x4Data, tex4x4PlttIdxData, paletteData;
	bstreamCreate(&texData, NULL, 0);
	bstreamCreate(&tex4x4Data, NULL, 0);
	bstreamCreate(&tex4x4PlttIdxData, NULL, 0);
	bstreamCreate(&paletteData, NULL, 0);
	for (int i = 0; i < nsbtx->nTextures; i++) {
		TEXELS *texture = nsbtx->textures + i;
		int width = TEXW(texture->texImageParam);
		int height = texture->height;
		int texelSize = TxGetTexelSize(width, height, texture->texImageParam);

		if (FORMAT(texture->texImageParam) == CT_4x4) {
			//write the offset in the texImageParams
			int ofs = (tex4x4Data.pos >> 3) & 0xFFFF;
			texture->texImageParam = (texture->texImageParam & 0xFFFF0000) | ofs;
			bstreamWrite(&tex4x4Data, texture->texel, texelSize);
			bstreamWrite(&tex4x4PlttIdxData, (BYTE *) texture->cmp, texelSize / 2);
		} else {
			int ofs = (texData.pos >> 3) & 0xFFFF;
			texture->texImageParam = (texture->texImageParam & 0xFFFF0000) | ofs;
			bstreamWrite(&texData, texture->texel, texelSize);
		}
	}

	int *paletteOffsets = (int *) calloc(nsbtx->nTextures, sizeof(int));
	int has4Color = 0;
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		int offs = paletteData.pos;
		PALETTE *palette = nsbtx->palettes + i;
		paletteOffsets[i] = paletteData.pos;

		//add bytes, make sure to align to a multiple of 16 bytes if more than 4 colors! (or if it's the last palette)
		int nColors = palette->nColors;
		bstreamWrite(&paletteData, (BYTE *) palette->pal, nColors * 2);
		if (nColors <= 4 && ((i == nsbtx->nPalettes - 1) || (nsbtx->palettes[i + 1].nColors > 4))) {
			BYTE padding[16] = { 0 };
			bstreamWrite(&paletteData, padding, 16 - nColors * 2);
		}

		//do we have 4 color?
		if (nColors <= 4) has4Color = 1;
	}

	uint8_t texInfo[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint16_t *) (texInfo + 6) = 60;
	*(uint16_t *) (texInfo + 4) = texData.pos >> 3;
	*(uint32_t *) (texInfo + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24;
	bstreamWrite(stream, texInfo, sizeof(texInfo));

	uint8_t tex4x4Info[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint16_t *) (tex4x4Info + 6) = 60;
	*(uint16_t *) (tex4x4Info + 4) = tex4x4Data.pos >> 3;
	*(uint32_t *) (tex4x4Info + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24 + texData.pos;
	*(uint32_t *) (tex4x4Info + 16) = (*(uint32_t *) (tex4x4Info + 12)) + tex4x4Data.pos;
	bstreamWrite(stream, tex4x4Info, sizeof(tex4x4Info));

	uint8_t plttInfo[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint16_t *) (plttInfo + 8) = 76 + nsbtx->nTextures * 28;
	*(uint16_t *) (plttInfo + 4) = paletteData.pos >> 3;
	*(uint16_t *) (plttInfo + 6) = has4Color ? 0x8000 : 0;
	*(uint32_t *) (plttInfo + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24 + texData.pos + tex4x4Data.pos + tex4x4PlttIdxData.pos;
	bstreamWrite(stream, plttInfo, sizeof(plttInfo));

	{
		//write dictTex
		int dictOfs = NnsG3dWriteDictionary(stream, nsbtx->textures, sizeof(TEXELS), nsbtx->nTextures, TexarciGetTextureNameCallback, 8);
		int dictEndOfs = stream->pos;

		//write dict data
		//make sure to copy the texImageParams over
		bstreamSeek(stream, dictOfs, 0);
		for (int i = 0; i < nsbtx->nTextures; i++) {
			uint32_t dictData[2];
			TEXELS *texels = nsbtx->textures + i;
			int texImageParam = texels->texImageParam;
			dictData[0] = texImageParam;
			dictData[1] = TEXW(texImageParam);
			if (texels->height == TEXH(texImageParam)) {
				dictData[1] |= 0x80000000 | (TEXH(texImageParam) << 11); //original size match
			} else {
				dictData[1] |= texels->height << 11; //original size mismatch
			}

			bstreamWrite(stream, dictData, sizeof(dictData));
		}
		bstreamSeek(stream, dictEndOfs, 0);
	}
	{
		//write dictPltt
		int dictOfs = NnsG3dWriteDictionary(stream, nsbtx->palettes, sizeof(PALETTE), nsbtx->nPalettes, TexarciGetPaletteNameCallback, 4);
		int dictEndOfs = stream->pos;
		
		//write data
		bstreamSeek(stream, dictOfs, 0);
		for (int i = 0; i < nsbtx->nTextures; i++) {
			PALETTE *palette = nsbtx->palettes + i;
			uint16_t dictData[2];
			dictData[0] = paletteOffsets[i] >> 3;
			dictData[1] = palette->nColors <= 4;
			bstreamWrite(stream, dictData, sizeof(dictData));
		}
		bstreamSeek(stream, dictEndOfs, 0);
	}
	free(paletteOffsets);

	//write texData, tex4x4Data, tex4x4PlttIdxData, paletteData
	bstreamWrite(stream, texData.buffer, texData.pos);
	bstreamWrite(stream, tex4x4Data.buffer, tex4x4Data.pos);
	bstreamWrite(stream, tex4x4PlttIdxData.buffer, tex4x4PlttIdxData.pos);
	bstreamWrite(stream, paletteData.buffer, paletteData.pos);

	//write back the proper sizes
	DWORD endPos = stream->pos;
	stream->pos = 8;
	bstreamWrite(stream, &endPos, 4);
	if (nsbtx->mdl0 == NULL) {
		int tex0Size = endPos - 0x14;

		stream->pos = tex0Offset + 4;
		bstreamWrite(stream, &tex0Size, 4);
	} else {
		int mdl0Size = 8 + nsbtx->mdl0Size;
		int tex0Size = endPos - 0x18 - mdl0Size;

		stream->pos = tex0Offset + 4;
		bstreamWrite(stream, &tex0Size, 4);
		stream->pos = 0x14;
		bstreamWrite(stream, &tex0Offset, 4);
	}

	//free resources
	bstreamFree(&texData);
	bstreamFree(&tex4x4Data);
	bstreamFree(&tex4x4PlttIdxData);
	bstreamFree(&paletteData);

	return 0;
}

int TexarcWriteBmd(TexArc *nsbtx, BSTREAM *stream) {
	unsigned char header[0x3C];

	BMD_DATA *bmd = nsbtx->bmdData;
	bstreamWrite(stream, header, sizeof(header));
	bstreamWrite(stream, bmd->preTexture, bmd->preTextureSize);

	//write textures
	int texturePos = stream->pos;
	for (int i = 0; i < nsbtx->nTextures; i++) {
		unsigned char texEntry[0x14] = { 0 };
		TEXELS *texture = nsbtx->textures + i;

		int texImageParam = texture->texImageParam;
		*(uint32_t *) (texEntry + 0x08) = TxGetTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
		*(uint16_t *) (texEntry + 0x0C) = TEXW(texImageParam);
		*(uint16_t *) (texEntry + 0x0E) = TEXH(texImageParam);
		*(uint32_t *) (texEntry + 0x10) = texImageParam;

		bstreamWrite(stream, texEntry, sizeof(texEntry));
	}

	//write texture names
	char terminator = '\0';
	for (int i = 0; i < nsbtx->nTextures; i++) {
		TEXELS *texture = nsbtx->textures + i;
		char *name = texture->name;
		int len = 0;
		for (; len < 16; len++) {
			if (name[len] == '\0') break;
		}

		uint32_t pos = stream->pos;
		bstreamSeek(stream, texturePos + i * 0x14, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, name, len);
		bstreamWrite(stream, &terminator, 1);
	}
	while (stream->pos & 3) { //pad
		bstreamWrite(stream, &terminator, 1);
	}

	//write palettes
	int palettePos = stream->pos;
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		unsigned char palEntry[0x10] = { 0 };
		PALETTE *palette = nsbtx->palettes + i;

		*(uint32_t *) (palEntry + 0x08) = palette->nColors * 2;
		*(uint32_t *) (palEntry + 0x0C) = 0xFFFFFFFF;

		bstreamWrite(stream, palEntry, sizeof(palEntry));
	}

	//write palette names
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		PALETTE *palette = nsbtx->palettes + i;
		char *name = palette->name;
		int len = 0;
		for (; len < 16; len++) {
			if (name[len] == '\0') break;
		}

		uint32_t pos = stream->pos;
		bstreamSeek(stream, palettePos + i * 0x10, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, name, len);
		bstreamWrite(stream, &terminator, 1);
	}
	while (stream->pos & 3) { //pad
		bstreamWrite(stream, &terminator, 1);
	}

	int materialPos = stream->pos;
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = ((unsigned char *) bmd->materials) + i * 0x30;
		*(uint32_t *) material += materialPos;
	}
	bstreamWrite(stream, bmd->materials, bmd->materialsSize);
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = ((unsigned char *) bmd->materials) + i * 0x30;
		*(uint32_t *) material -= materialPos;
	}

	//write texture data
	int textureDataPos = stream->pos;
	for (int i = 0; i < nsbtx->nTextures; i++) {
		TEXELS *texture = nsbtx->textures + i;
		int texImageParam = texture->texImageParam;
		int texelSize = TxGetTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
		uint32_t pos = stream->pos;

		bstreamSeek(stream, texturePos + i * 0x14 + 4, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, texture->texel, texelSize);
		if (FORMAT(texImageParam) == CT_4x4) {
			bstreamWrite(stream, texture->cmp, texelSize / 2);
		}
	}

	//write palette data
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		PALETTE *palette = nsbtx->palettes + i;
		uint32_t pos = stream->pos;

		bstreamSeek(stream, palettePos + i * 0x10 + 4, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, palette->pal, palette->nColors * 2);
	}
	bstreamSeek(stream, 0, 0);

	*(uint32_t *) (header + 0x00) = bmd->scale;
	*(uint32_t *) (header + 0x04) = bmd->nBones;
	*(uint32_t *) (header + 0x08) = bmd->boneOffset;
	*(uint32_t *) (header + 0x0C) = bmd->nDisplaylists;
	*(uint32_t *) (header + 0x10) = bmd->displaylistOffset;
	*(uint32_t *) (header + 0x14) = nsbtx->nTextures;
	*(uint32_t *) (header + 0x18) = texturePos;
	*(uint32_t *) (header + 0x1C) = nsbtx->nPalettes;
	*(uint32_t *) (header + 0x20) = palettePos;
	*(uint32_t *) (header + 0x24) = bmd->nMaterials;
	*(uint32_t *) (header + 0x28) = materialPos;
	*(uint32_t *) (header + 0x2C) = bmd->transformOffset;
	*(uint32_t *) (header + 0x30) = bmd->field30;
	*(uint32_t *) (header + 0x34) = bmd->field34;
	*(uint32_t *) (header + 0x38) = textureDataPos;
	bstreamWrite(stream, header, sizeof(header));

	return 0;
}

int TexarcWrite(TexArc *nsbtx, BSTREAM *stream) {
	int fmt = nsbtx->header.format;
	switch (fmt) {
		case NSBTX_TYPE_NNS:
			return TexarcWriteNsbtx(nsbtx, stream);
		case NSBTX_TYPE_BMD:
			return TexarcWriteBmd(nsbtx, stream);
	}
	return 1;
}

int TexarcWriteFile(TexArc *nsbtx, LPWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) nsbtx, (OBJECT_WRITER) TexarcWrite);
}

int TexarcGetTextureIndexByName(TexArc *nsbtx, const char *name) {
	for (int i = 0; i < nsbtx->nTextures; i++) {
		if (strncmp(nsbtx->textures[i].name, name, 16) == 0) {
			return i;
		}
	}
	return -1;
}

int TexarcGetPaletteIndexByName(TexArc *nsbtx, const char *name) {
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		if (strncmp(nsbtx->palettes[i].name, name, 16) == 0) {
			return i;
		}
	}
	return -1;
}

TEXELS *TexarcGetTextureByName(TexArc *nsbtx, const char *name) {
	int index = TexarcGetTextureIndexByName(nsbtx, name);
	if (index == -1) return NULL;

	return nsbtx->textures + index;
}

PALETTE *TexarcGetPaletteByName(TexArc *nsbtx, const char *name) {
	int index = TexarcGetPaletteIndexByName(nsbtx, name);
	if (index == -1) return NULL;

	return nsbtx->palettes + index;
}

int TexarcAddTexture(TexArc *nsbtx, TEXELS *texture) {
	//if the texture already exists, return 0
	if (TexarcGetTextureByName(nsbtx, texture->name) != NULL) return -1;

	//add texture
	nsbtx->textures = realloc(nsbtx->textures, (nsbtx->nTextures + 1) * sizeof(TEXELS));
	memcpy(nsbtx->textures + nsbtx->nTextures, texture, sizeof(TEXELS));
	nsbtx->nTextures++;
	return nsbtx->nTextures - 1;
}

int TexarcAddPalette(TexArc *nsbtx, PALETTE *palette) {
	//if the palette already exists, check its content
	PALETTE *existing = TexarcGetPaletteByName(nsbtx, palette->name);
	if (existing != NULL) {
		//one must be contained within the other
		int nColsCompare = palette->nColors;
		if (existing->nColors < nColsCompare) nColsCompare = existing->nColors;

		if (memcmp(existing->pal, palette->pal, nColsCompare * sizeof(COLOR)) != 0) {
			//data mismatch. Report error.
			return -1;
		}

		//if the palette to add is smaller, return success
		int index = existing - nsbtx->palettes;
		if (palette->nColors < existing->nColors) {
			free(palette->pal);
			palette->pal = NULL;
			palette->nColors = 0;
			return index;
		}

		//expand and fill
		free(existing->pal);
		existing->pal = palette->pal;
		existing->nColors = palette->nColors;
		return index;
	}

	//add palette
	nsbtx->palettes = realloc(nsbtx->palettes, (nsbtx->nPalettes + 1) * sizeof(PALETTE));
	memcpy(nsbtx->palettes + nsbtx->nPalettes, palette, sizeof(PALETTE));
	nsbtx->nPalettes++;
	return nsbtx->nPalettes - 1;
}

