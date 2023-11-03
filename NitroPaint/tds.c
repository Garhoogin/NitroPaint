#include "tds.h"
#include "texture.h"
#include "textureeditor.h"

#include <Windows.h>
#include <stdio.h>

HWND CreateTdsViewer(HWND hWndParent, LPCWSTR path) {
	TdsFile tds;
	int n = TdsReadFile(&tds, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	return CreateTextureEditorImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndParent, &tds.texture);
}

void TdsFree(OBJECT_HEADER* header) {
	TdsFile* tds = (TdsFile*)header;
	if (tds->texture.texels.texel != NULL) free(tds->texture.texels.texel);
	if (tds->texture.texels.cmp != NULL) free(tds->texture.texels.cmp);
	if (tds->texture.palette.pal != NULL) free(tds->texture.palette.pal);
	
}

void TdsInit(TdsFile* tds) {
	tds->header.size = sizeof(TdsFile);
	ObjInit((OBJECT_HEADER*)tds, FILE_TYPE_TDS, 0);
	tds->header.dispose = TdsFree;
}

int TdsIsValid(char* buffer, unsigned int size) {
	if (size < 0x24 || (size & 3)) return 0;

	uint32_t magic = *(uint32_t*)(buffer + 0);
	if (magic != '.tds') return 0;

	uint32_t texCount = *(uint32_t*)(buffer + 0x04);
	if (texCount != 1) {
		printf("!!!TexCount not 1!!!\n");
		return 0;
	}

	uint32_t texFormat = *(uint8_t*)(buffer + 0x08);
	uint32_t texSizeS = *(uint8_t*)(buffer + 0x09);
	uint32_t texSizeT = *(uint8_t*)(buffer + 0x0A);
	uint32_t textureOffset = *(uint32_t*)(buffer + 0x0C);
	uint32_t paletteOffset = *(uint32_t*)(buffer + 0x14);
	uint32_t width = *(uint32_t*)(buffer + 0x1C);
	uint32_t height = *(uint32_t*)(buffer + 0x20);
	if (textureOffset < 0x24 || textureOffset >= size)
		return 0;
	if (paletteOffset < 0x24 || paletteOffset >= size)
		return 0;
	if (width > (8 << texSizeS) || height > (8 << texSizeT) || texFormat == 0)
		return 0;

	if (texFormat == CT_4x4) {
		printf("!!!TexFormat is 4x4!?!?\n");
		return 0;
	}
		
	return 1;

}

int TdsRead(TdsFile* tds, char* buffer, int size) {
	//is it valid?
	if (!TdsIsValid(buffer, size)) return 1;
	
	TdsInit(tds);

	uint32_t texFormat = *(uint8_t*)(buffer + 0x08);
	uint32_t texSizeS = *(uint8_t*)(buffer + 0x09);
	uint32_t texSizeT = *(uint8_t*)(buffer + 0x0A);
	uint32_t textureOffset = *(uint32_t*)(buffer + 0x0C);
	uint32_t textureLength = *(uint32_t*)(buffer + 0x10);
	uint32_t paletteOffset = *(uint32_t*)(buffer + 0x14);
	uint32_t paletteLength = *(uint32_t*)(buffer + 0x18);
	uint32_t width = *(uint32_t*)(buffer + 0x1C);
	uint32_t height = *(uint32_t*)(buffer + 0x20);

	uint32_t texImageParam = 0;
	texImageParam |= (texSizeS & 0x7) << 20;
	texImageParam |= (texSizeT & 0x7) << 23;
	texImageParam |= (texFormat & 0x7) << 26;

	tds->texture.texels.texImageParam = texImageParam;
	tds->texture.texels.height = height;
	tds->texture.texels.cmp = NULL;
	tds->texture.texels.texel = calloc(textureLength, 1);
	memcpy(tds->texture.texels.texel, buffer + textureOffset, textureLength);

	tds->texture.palette.nColors = paletteLength / 2;
	tds->texture.palette.pal = calloc(paletteLength, 1);
	memcpy(tds->texture.palette.pal, buffer + paletteOffset, paletteLength);

	return 0;
}

int TdsReadFile(TdsFile* tds, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER*)tds, (OBJECT_READER)TdsRead);
}

int TdsWrite(TdsFile* tds, BSTREAM* stream) {
	return 0;
}

int TdsWriteFile(TdsFile* tds, LPWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER*)tds, (OBJECT_WRITER)TdsWrite);
}
