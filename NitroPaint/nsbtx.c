#include "nsbtx.h"
#include "texture.h"
#include <Windows.h>
#include <stdio.h>

//NSBTX code adapted from Gericom's code in Fvery File Explorer.

char *readDictionary(DICTIONARY *dict, BYTE *base, int entrySize) {
	dict->revision = base[0];
	dict->nEntries = base[1];
	dict->sizeDictBlk = *(WORD *) (base + 2);
	dict->ofsEntry = *(WORD *) (base + 6);

	BYTE *pos = base + 8;

	dict->node = calloc(dict->nEntries + 1, sizeof(PTREENODE));
	for (int i = 0; i < dict->nEntries + 1; i++) {
		dict->node[i].refBit = *(pos++);
		dict->node[i].idxLeft = *(pos++);
		dict->node[i].idxRight = *(pos++);
		dict->node[i].idxEntry = *(pos++);
	}
	dict->nNode = dict->nEntries + 1;

	dict->entry.sizeUnit = *(WORD *) (pos);
	pos += 2;
	dict->entry.offsetName = *(WORD *) pos;
	pos += 2;
	dict->entry.data = calloc(dict->nEntries, entrySize);
	memcpy(dict->entry.data, pos, entrySize * dict->nEntries);
	pos += entrySize * dict->nEntries;

	dict->names = (char **) calloc(dict->nEntries, sizeof(char *));
	char *nameBase = (char *) calloc(16 * dict->nEntries, 1);
	memcpy(nameBase, pos, 16 * dict->nEntries);
	for (int i = 0; i < dict->nEntries; i++) {
		dict->names[i] = nameBase + i * 16;
		pos += 16;
	}
	return pos;
}

void freeDictionary(DICTIONARY *dictionary) {
	free(dictionary->names[0]);
	free(dictionary->names);
	free(dictionary->node);
}

int nsbtxRead(NSBTX *nsbtx, char *buffer, int size) {
	//is it valid?
	if ((buffer[0] != 'B' || buffer[1] != 'T' || buffer[2] != 'X' || buffer[3] != '0') &&
		(buffer[0] != 'B' || buffer[1] != 'M' || buffer[2] != 'D' || buffer[3] != '0')) return 1;
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
	int textureDataSize = (*(WORD *) (tex0 + 0xC)) << 3;
	int textureInfoOffset = *(WORD *) (tex0 + 0xE); //dictionary
	int textureDataOffset = *(int *) (tex0 + 0x14); //ofsTex

	int compressedTextureDataSize = (*(WORD *) (tex0 + 0x1C)) << 3;
	int compressedTextureInfoOffset = *(WORD *) (tex0 + 0x1E); //dictionary
	int compressedTextureDataOffset = *(int *) (tex0 + 0x24); //ofsTex
	int compressedTextureInfoDataOffset = *(int *) (tex0 + 0x28); //ofsTexPlttIdx

	int paletteDataSize = (*(WORD *) (tex0 + 0x30)) << 3;
	int paletteInfoOffset = *(int *) (tex0 + 0x34); //dictionary
	int paletteDataOffset = *(int *) (tex0 + 0x38);

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
	
	memcpy(&nsbtx->textureDictionary, &dictTex, sizeof(DICTIONARY));
	memcpy(&nsbtx->paletteDictionary, &dictPal, sizeof(DICTIONARY));

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
		int texelSize = getTexelSize(width, height, texData->texImageParam);

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
		memcpy(texels[i].name, dictTex.names[i], 16);
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
		
		palettes[i].nColors = nColors;
		palettes[i].pal = (short *) calloc(nColors, 2);
		//offset == size - paletteDataOffset - (palData->offset << 3) - tex0Offset + (palData->offset << 3)
		memcpy(palettes[i].pal, tex0 + paletteDataOffset + (palData->offset << 3), nColors * 2);

		memcpy(palettes[i].name, dictPal.names[i], 16);
	}

	//as a test, write out the first palette and texel data.

	nsbtx->nTextures = dictTex.nEntries;
	nsbtx->nPalettes = dictPal.nEntries;
	nsbtx->textures = (TEXELS *) calloc(nsbtx->nTextures, sizeof(TEXELS));
	nsbtx->palettes = (PALETTE *) calloc(nsbtx->nPalettes, sizeof(PALETTE));
	nsbtx->header.type = FILE_TYPE_NSBTX;
	nsbtx->header.format = 0;
	nsbtx->header.size = sizeof(*nsbtx);
	nsbtx->header.compression = COMPRESSION_NONE;

	memcpy(nsbtx->textures, texels, nsbtx->nTextures * sizeof(TEXELS));
	memcpy(nsbtx->palettes, palettes, nsbtx->nPalettes * sizeof(PALETTE));

	return 0;
}

int nsbtxReadFile(NSBTX *nsbtx, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nsbtx, (OBJECT_READER) nsbtxRead);
}

typedef struct {
	BYTE *ptr;
	int bufferSize;
	int length;
} BYTEARRAY;

void initializeArray(BYTEARRAY *arr) {
	arr->ptr = calloc(1024, 1);
	arr->length = 0;
	arr->bufferSize = 1024;
}

void freeArray(BYTEARRAY *arr) {
	if (arr->ptr != NULL) free(arr->ptr);
	arr->ptr = NULL;
	arr->length = 0;
	arr->bufferSize = 0;
}

void addBytes(BYTEARRAY *arr, BYTE *bytes, int length) {
	if (arr->length + length >= arr->bufferSize) {
		int newSize = arr->bufferSize;
		while (arr->length + length >= newSize) {
			newSize = newSize + (newSize >> 1);
		}
		arr->ptr = realloc(arr->ptr, newSize);
		arr->bufferSize = newSize;
	}
	memcpy(arr->ptr + arr->length, bytes, length);
	arr->length += length;
}

int nsbtxWrite(NSBTX *nsbtx, BSTREAM *stream) {
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

	BYTEARRAY texData, tex4x4Data, tex4x4PlttIdxData, paletteData;
	initializeArray(&texData);
	initializeArray(&tex4x4Data);
	initializeArray(&tex4x4PlttIdxData);
	initializeArray(&paletteData);
	for (int i = 0; i < nsbtx->nTextures; i++) {
		TEXELS *texture = nsbtx->textures + i;
		int width = TEXW(texture->texImageParam);
		int height = TEXH(texture->texImageParam);
		int texelSize = getTexelSize(width, height, texture->texImageParam);
		if (FORMAT(texture->texImageParam) == CT_4x4) {
			//write the offset in the texImageParams
			int ofs = (tex4x4Data.length >> 3) & 0xFFFF;
			texture->texImageParam = (texture->texImageParam & 0xFFFF0000) | ofs;
			addBytes(&tex4x4Data, texture->texel, texelSize);
			addBytes(&tex4x4PlttIdxData, (BYTE *) texture->cmp, texelSize / 2);
		} else {
			int ofs = (texData.length >> 3) & 0xFFFF;
			texture->texImageParam = (texture->texImageParam & 0xFFFF0000) | ofs;
			addBytes(&texData, texture->texel, texelSize);
		}
	}

	for (int i = 0; i < nsbtx->nPalettes; i++) {
		int offs = paletteData.length;
		PALETTE *palette = nsbtx->palettes + i;

		//add bytes, make sure to align to a multiple of 16 bytes if more than 4 colors! (or if it's the last palette)
		int nColors = palette->nColors;
		addBytes(&paletteData, (BYTE *) palette->pal, nColors * 2);
		if (nColors <= 4 && ((i == nsbtx->nPalettes - 1) || (nsbtx->palettes[i + 1].nColors > 4))) {
			BYTE padding[16] = { 0 };
			addBytes(&paletteData, padding, 16 - nColors * 2);
		}
		DICTPLTTDATA *data = ((DICTPLTTDATA *) nsbtx->paletteDictionary.entry.data) + i;
		data->offset = offs >> 3;
	}

	BYTE texInfo[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(WORD *) (texInfo + 6) = 60;
	*(WORD *) (texInfo + 4) = texData.length >> 3;
	*(DWORD *) (texInfo + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24;
	bstreamWrite(stream, texInfo, sizeof(texInfo));

	BYTE tex4x4Info[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(WORD *) (tex4x4Info + 6) = 60;
	*(WORD *) (tex4x4Info + 4) = tex4x4Data.length >> 3;
	*(DWORD *) (tex4x4Info + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24 + texData.length;
	*(DWORD *) (tex4x4Info + 16) = (*(DWORD *) (tex4x4Info + 12)) + tex4x4Data.length;
	bstreamWrite(stream, tex4x4Info, sizeof(tex4x4Info));

	BYTE plttInfo[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(WORD *) (plttInfo + 8) = 76 + nsbtx->nTextures * 28;
	*(WORD *) (plttInfo + 4) = paletteData.length >> 3;
	*(DWORD *) (plttInfo + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24 + texData.length + tex4x4Data.length + tex4x4PlttIdxData.length;
	bstreamWrite(stream, plttInfo, sizeof(plttInfo));

	{
		BYTE dictHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		//write dictTex
		DWORD startpos = stream->pos;
		dictHeader[1] = nsbtx->nTextures;
		*(WORD *) (dictHeader + 4) = 8;
		*(WORD *) (dictHeader + 6) = (nsbtx->nTextures + 1) * 4 + 8;
		bstreamWrite(stream, dictHeader, sizeof(dictHeader));
		for (int i = 0; i < nsbtx->nTextures + 1; i++) {
			PTREENODE *node = nsbtx->textureDictionary.node + i;
			BYTE nodeBits[] = { 0, 0, 0, 0 };
			nodeBits[0] = node->refBit;
			nodeBits[1] = node->idxLeft;
			nodeBits[2] = node->idxRight;
			nodeBits[3] = node->idxEntry;
			bstreamWrite(stream, nodeBits, sizeof(nodeBits));
		}
		BYTE entryBits[] = { 0, 0, 0, 0 };
		*(WORD *) entryBits = 8;
		*(WORD *) (entryBits + 2) = (4 + 8 * nsbtx->nTextures);
		bstreamWrite(stream, entryBits, sizeof(entryBits));
		//write data
		//make sure to copy the texImageParams over
		for (int i = 0; i < nsbtx->nTextures; i++) {
			DICTTEXDATA *tex = ((DICTTEXDATA *) nsbtx->textureDictionary.entry.data) + i;
			TEXELS *texels = nsbtx->textures + i;
			tex->texImageParam = texels->texImageParam;
		}
		bstreamWrite(stream, nsbtx->textureDictionary.entry.data, nsbtx->nTextures * sizeof(DICTTEXDATA));
		for (int i = 0; i < nsbtx->nTextures; i++) {
			bstreamWrite(stream, nsbtx->textures[i].name, 16);
		}
		DWORD curpos = stream->pos;
		stream->pos = startpos + 2;
		WORD diff = (WORD) (curpos - startpos);
		bstreamWrite(stream, &diff, 2);
		stream->pos = curpos;
	}
	//write dictPltt
	{
		BYTE dictHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		//write dictTex
		//long startpos = position
		DWORD startpos = stream->pos;
		dictHeader[1] = nsbtx->nPalettes;
		*(WORD *) (dictHeader + 4) = 8;
		*(WORD *) (dictHeader + 6) = (nsbtx->nPalettes + 1) * 4 + 8;
		bstreamWrite(stream, dictHeader, sizeof(dictHeader));
		for (int i = 0; i < nsbtx->nPalettes + 1; i++) {
			PTREENODE *node = nsbtx->paletteDictionary.node + i;
			BYTE nodeBits[] = { 0, 0, 0, 0 };
			nodeBits[0] = node->refBit;
			nodeBits[1] = node->idxLeft;
			nodeBits[2] = node->idxRight;
			nodeBits[3] = node->idxEntry;
			bstreamWrite(stream, nodeBits, sizeof(nodeBits));
		}
		BYTE entryBits[] = { 0, 0, 0, 0 };
		*(WORD *) entryBits = 4;
		*(WORD *) (entryBits + 2) = (4 + 4 * nsbtx->nPalettes);
		bstreamWrite(stream, entryBits, sizeof(entryBits));
		//write data
		bstreamWrite(stream, nsbtx->paletteDictionary.entry.data, nsbtx->nPalettes * sizeof(DICTPLTTDATA));
		for (int i = 0; i < nsbtx->nPalettes; i++) {
			bstreamWrite(stream, nsbtx->palettes[i].name, 16);
		}
		DWORD curpos = stream->pos;
		stream->pos = startpos + 2;
		WORD diff = (WORD) (curpos - startpos);
		bstreamWrite(stream, &diff, 2);
		stream->pos = curpos;
	}


	//write texData, tex4x4Data, tex4x4PlttIdxData, paletteData
	bstreamWrite(stream, texData.ptr, texData.length);
	bstreamWrite(stream, tex4x4Data.ptr, tex4x4Data.length);
	bstreamWrite(stream, tex4x4PlttIdxData.ptr, tex4x4PlttIdxData.length);
	bstreamWrite(stream, paletteData.ptr, paletteData.length);

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
	freeArray(&texData);
	freeArray(&tex4x4Data);
	freeArray(&tex4x4PlttIdxData);
	freeArray(&paletteData);

	return 0;
}

int nsbtxWriteFile(NSBTX *nsbtx, LPWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) nsbtx, (OBJECT_WRITER) nsbtxWrite);
}
