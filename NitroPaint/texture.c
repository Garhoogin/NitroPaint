#include <Windows.h>
#include <stdio.h>
#include "texture.h"

int getTexelSize(int width, int height, int texImageParam) {
	int nPx = width * height;
	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
	int b = bits[FORMAT(texImageParam)];
	return (nPx * b) >> 3;
}

int getIndexVramSize(TEXELS *texels) {
	int texImageParam = texels->texImageParam;
	int format = FORMAT(texImageParam);
	int hasIndex = format == CT_4x4;

	int texelSize = getTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
	int indexSize = hasIndex ? (texelSize / 2) : 0;
	return indexSize;
}

int getTextureVramSize(TEXELS *texels) {
	int texImageParam = texels->texImageParam;
	int w = TEXW(texImageParam);
	int h = TEXH(texImageParam);
	int fmt = FORMAT(texImageParam);

	int bpps[] = { 0, 8, 2, 4, 8, 3, 8, 16 };
	return bpps[fmt] * w * h / 8;
}

int getPaletteVramSize(PALETTE *palette) {
	return palette->nColors * sizeof(COLOR);
}

typedef struct {
	BYTE r;
	BYTE g;
	BYTE b;
	BYTE a;
} RGB;

void getrgb(unsigned short n, RGB * ret){
	COLOR32 conv = ColorConvertFromDS(n);
	ret->r = conv & 0xFF;
	ret->g = (conv >> 8) & 0xFF;
	ret->b = (conv >> 16) & 0xFF;
	ret->a = (BYTE) (255 * (n >> 15));
}

int max16Len(char *str) {
	int len = 0;
	for (int i = 0; i < 16; i++) {
		char c = str[i];
		if (!c) return len;
		len++;
	}
	return len;
}

char *stringFromFormat(int fmt) {
	char *fmts[] = {"", "a3i5", "palette4", "palette16", "palette256", "tex4x4", "a5i3", "direct"};
	return fmts[fmt];
}

void textureRender(DWORD *px, TEXELS *texels, PALETTE *palette, int flip) {
	int format = FORMAT(texels->texImageParam);
	int c0xp = COL0TRANS(texels->texImageParam);
	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	int nPixels = width * height;
	int txelSize = getTexelSize(width, height, texels->texImageParam);
	switch (format) {
		case CT_DIRECT:
		{
			for(int i = 0; i < nPixels; i++){
				unsigned short pVal = *(((unsigned short *) texels->texel) + i);
				RGB rgb = {0};
				getrgb(pVal, &rgb);
				px[i] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
			}
			break;
		}
		case CT_4COLOR:
		{
			int offs = 0;
			for(int i = 0; i < txelSize >> 2; i++){
				unsigned d = (unsigned) *(((int *) texels->texel) + i);
				for(int j = 0; j < 16; j++){
					int pVal = d & 0x3;
					d >>= 2;
					if (pVal < palette->nColors) {
						unsigned short col = palette->pal[pVal] | 0x8000;
						if (!pVal && c0xp) col = 0;
						RGB rgb = { 0 };
						getrgb(col, &rgb);
						px[offs] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
					}
					offs++;
				}
			}
			break;
		}
		case CT_16COLOR:
		{
			int iters = txelSize;
			for(int i = 0; i < iters; i++){
				unsigned char pVal = *(((unsigned char *) texels->texel) + i);
				unsigned short col0 = 0;
				unsigned short col1 = 0;
				if ((pVal & 0xF) < palette->nColors) {
					col0 = palette->pal[pVal & 0xF] | 0x8000;
				}
				if ((pVal >> 4) < palette->nColors) {
					col1 = palette->pal[pVal >> 4] | 0x8000;
				}
				if(c0xp){
					if(!(pVal & 0xF)) col0 = 0;
					if(!(pVal >> 4)) col1 = 0;
				}
				RGB rgb = {0};
				getrgb(col0, &rgb);
				px[i * 2] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
				getrgb(col1, &rgb);
				px[i * 2 + 1] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
			}
			break;
		}
		case CT_256COLOR:
		{
			for(int i = 0; i < txelSize; i++){
				unsigned char pVal = *(texels->texel + i);
				if (pVal < palette->nColors) {
					unsigned short col = *(((unsigned short *) palette->pal) + pVal) | 0x8000;
					if (!pVal && c0xp) col = 0;
					RGB rgb = { 0 };
					getrgb(col, &rgb);
					px[i] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
				}
			}
			break;
		}
		case CT_A3I5:
		{
			for(int i = 0; i < txelSize; i++){
				unsigned char d = texels->texel[i];
				int alpha = ((d & 0xE0) >> 5) * 255 / 7;
				int index = d & 0x1F;
				if (index < palette->nColors) {
					unsigned short atIndex = *(((unsigned short *) palette->pal) + index);
					RGB r = { 0 };
					getrgb(atIndex, &r);
					r.a = alpha;
					px[i] = r.b | (r.g << 8) | (r.r << 16) | (r.a << 24);
				}
			}
			break;
		}
		case CT_A5I3:
		{
			for(int i = 0; i < txelSize; i++){
				unsigned char d = texels->texel[i];
				int alpha = ((d & 0xF8) >> 3) * 255 / 31;
				int index = d & 0x7;
				if (index < palette->nColors) {
					unsigned short atIndex = *(((unsigned short *) palette->pal) + index);
					RGB r = { 0 };
					getrgb(atIndex, &r);
					r.a = alpha;
					px[i] = r.b | (r.g << 8) | (r.r << 16) | (r.a << 24);
				}
			}
			break;
		}
		case CT_4x4:
		{
			int squares = (width * height) >> 4;
			RGB transparent = {0, 0, 0, 0};
			for(int i = 0; i < squares; i++){
				RGB colors[4] = { 0 };
				unsigned texel = *(unsigned *) (texels->texel + (i << 2));
				unsigned short data = *(unsigned short *) (texels->cmp + i);

				int address = COMP_INDEX(data);
				int mode = (data & COMP_MODE_MASK) >> 14;
				COLOR *base = ((COLOR *) palette->pal) + address;
				if (address + 2 <= palette->nColors) {
					getrgb(base[0], colors);
					getrgb(base[1], colors + 1);
				}
				colors[0].a = 255;
				colors[1].a = 255;
				if (mode == 0) {
					//require 3 colors
					if (address + 3 <= palette->nColors) {
						getrgb(base[2], colors + 2);
					}
					colors[2].a = 255;
					colors[3] = transparent;
				} else if (mode == 1) {
					//require 2 colors
					RGB col0 = { 0, 0, 0, 255 };
					RGB col1 = { 0, 0, 0, 255 };
					if (address + 2 <= palette->nColors) {
						col0 = *colors;
						col1 = *(colors + 1);
					}
					colors[2].r = (col0.r + col1.r + 1) >> 1;
					colors[2].g = (col0.g + col1.g + 1) >> 1;
					colors[2].b = (col0.b + col1.b + 1) >> 1;
					colors[2].a = 255;
					colors[3] = transparent;
				} else if (mode == 2) {
					//require 4 colors
					if (address + 4 <= palette->nColors) {
						getrgb(base[2], colors + 2);
						getrgb(base[3], colors + 3);
					}
					colors[2].a = 255;
					colors[3].a = 255;
				} else {
					//require 2 colors
					RGB col0 = { 0, 0, 0, 255 };
					RGB col1 = { 0, 0, 0, 255 };
					if (address + 2 <= palette->nColors) {
						col0 = *colors;
						col1 = *(colors + 1);
					}
					colors[2].r = (col0.r * 5 + col1.r * 3 + 4) >> 3;
					colors[2].g = (col0.g * 5 + col1.g * 3 + 4) >> 3;
					colors[2].b = (col0.b * 5 + col1.b * 3 + 4) >> 3;
					colors[2].a = 255;
					colors[3].r = (col0.r * 3 + col1.r * 5 + 4) >> 3;
					colors[3].g = (col0.g * 3 + col1.g * 5 + 4) >> 3;
					colors[3].b = (col0.b * 3 + col1.b * 5 + 4) >> 3;
					colors[3].a = 255;
				}
				for(int j = 0; j < 16; j++){
					int pVal = texel & 0x3;
					texel >>= 2;
					RGB rgb = colors[pVal];
					int offs = ((i & ((width >> 2) - 1)) << 2) + (j & 3) + (((i / (width >> 2)) << 2) + (j  >> 2)) * width;
					px[offs] = ColorRoundToDS18(rgb.b | (rgb.g << 8) | (rgb.r << 16)) | (rgb.a << 24);
				}
			}
			break;
		}
	}
	//flip upside down
	if (flip) {
		DWORD *tmp = calloc(width, 4);
		for (int y = 0; y < height / 2; y++) {
			DWORD *row1 = px + y * width;
			DWORD *row2 = px + (height - 1 - y) * width;
			memcpy(tmp, row1, width * 4);
			memcpy(row1, row2, width * 4);
			memcpy(row2, tmp, width * 4);
		}
		free(tmp);
	}
}

#pragma comment(lib, "Version.lib")

void getVersion(char *buffer, int max) {
	WCHAR path[MAX_PATH];
	GetModuleFileName(NULL, path, MAX_PATH);
	DWORD handle;
	DWORD dwSize = GetFileVersionInfoSize(path, &handle);
	if (dwSize) {
		BYTE *buf = (BYTE *) calloc(1, dwSize);
		if (GetFileVersionInfo(path, handle, dwSize, buf)) {
			UINT size;
			VS_FIXEDFILEINFO *info;
			if (VerQueryValue(buf, L"\\", &info, &size)) {		
				DWORD ms = info->dwFileVersionMS, ls = info->dwFileVersionLS;
				sprintf(buffer, "%d.%d.%d.%d", HIWORD(ms), LOWORD(ms), HIWORD(ls), LOWORD(ls));
			}
		}
		free(buf);
	}
}

void nnsTgaWriteSection(HANDLE hFile, const char *section, const void *data, int size) {
	DWORD dwWritten;
	
	//prepare and write
	unsigned char header[0xC] = { 0 };
	uint32_t dataSize = size == -1 ? strlen((const char *) data) : size;
	memcpy(header, section, 8);
	*(uint32_t *) (header + 0x8) = dataSize + sizeof(header);
	WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
	if (dataSize) {
		WriteFile(hFile, data, dataSize, &dwWritten, NULL);
	}
}

int imageHasTransparent(COLOR32 *px, int nPx) {
	for (int i = 0; i < nPx; i++) {
		COLOR32 c = px[i];
		int a = c >> 24;
		if (a < 255) return 1; //transparent/translucent pixel
	}
	return 0;
}

void nnsTgaWritePixels(HANDLE hFile, COLOR32 *rawPx, int width, int height, int depth) {
	DWORD dwWritten;
	if (depth == 32) {
		//write as-is
		WriteFile(hFile, rawPx, width * height * sizeof(COLOR32), &dwWritten, NULL);
		return;
	} else if (depth == 24) {
		//convert to 24-bit
		uint8_t *buffer = (uint8_t *) calloc(width * height, 3);
		for (int i = 0; i < width * height; i++) {
			COLOR32 c = rawPx[i];
			uint8_t *pixel = buffer + i * 3;
			pixel[0] = (c >> 0) & 0xFF;
			pixel[1] = (c >> 8) & 0xFF;
			pixel[2] = (c >> 16) & 0xFF;
		}
		WriteFile(hFile, buffer, width * height * 3, &dwWritten, NULL);
		free(buffer);
		return;
	}
	//bad
}

void writeNitroTGA(LPWSTR name, TEXELS *texels, PALETTE *palette) {
	HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwWritten;

	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	COLOR32 *pixels = (COLOR32 *) calloc(width * height, 4);
	textureRender(pixels, texels, palette, 1);
	int depth = imageHasTransparent(pixels, width * height) ? 32 : 24;

	uint8_t header[] = {0x14, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 8,
		'N', 'N', 'S', '_', 'T', 'g', 'a', ' ', 'V', 'e', 'r', ' ', '1', '.', '0', 0, 0, 0, 0, 0};
	*(uint16_t *) (header + 0x0C) = width;
	*(uint16_t *) (header + 0x0E) = height;
	*(uint8_t *) (header + 0x10) = depth;
	*(uint32_t *) (header + 0x22) = sizeof(header) + width * height * (depth / 8);
	WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
	nnsTgaWritePixels(hFile, pixels, width, height, depth);

	//format
	char *fstr = stringFromFormat(FORMAT(texels->texImageParam));
	nnsTgaWriteSection(hFile, "nns_frmt", fstr, -1);

	//texels
	int txelLength = getTexelSize(width, height, texels->texImageParam);
	nnsTgaWriteSection(hFile, "nns_txel", texels->texel, txelLength);

	//write 4x4 if applicable
	if (FORMAT(texels->texImageParam) == CT_4x4) {
		int pidxLength = txelLength / 2;
		nnsTgaWriteSection(hFile, "nns_pidx", texels->cmp, pidxLength);
	}

	//palette (if applicable)
	if (FORMAT(texels->texImageParam) != CT_DIRECT) {
		int pnamLength = max16Len(palette->name);
		nnsTgaWriteSection(hFile, "nns_pnam", palette->name, pnamLength);

		int nColors = palette->nColors;
		if (FORMAT(texels->texImageParam) == CT_4COLOR && nColors > 4) nColors = 4;
		nnsTgaWriteSection(hFile, "nns_pcol", palette->pal, nColors * sizeof(COLOR));
	}

	//NitroPaint generator signature
	char version[16];
	getVersion(version, 16);
	nnsTgaWriteSection(hFile, "nns_gnam", "NitroPaint", -1);
	nnsTgaWriteSection(hFile, "nns_gver", version, -1);

	//dummy imagestudio data
	nnsTgaWriteSection(hFile, "nns_imst", NULL, 0);

	//if c0xp
	if (COL0TRANS(texels->texImageParam)) {
		nnsTgaWriteSection(hFile, "nns_c0xp", NULL, 0);
	}

	//write end
	nnsTgaWriteSection(hFile, "nns_endb", NULL, 0);
	CloseHandle(hFile);
	free(pixels);
}

int ilog2(int x) {
	int n = 0;
	while (x) {
		x >>= 1;
		n++;
	}
	return n - 1;
}

int textureDimensionIsValid(int x) {
	if (x & (x - 1)) return 0;
	if (x < 8 || x > 1024) return 0;
	return 1;
}

int nitrotgaIsValid(unsigned char *buffer, unsigned int size) {
	//is the file even big enough to hold a TGA header and comment?
	if (size < 0x16) return 0;
	
	int commentLength = *buffer;
	if (commentLength < 4) return 0;
	unsigned int ptrOffset = 0x12 + commentLength - 4;
	if (ptrOffset + 4 > size) return 0;
	unsigned int ptr = *(unsigned int *) (buffer + ptrOffset);
	if (ptr + 0xC > size) return 0;

	//process sections. When any anomalies are found, return 0.
	char *curr = buffer + ptr;
	while (1) {
		//is there space enough left for a section header?
		if (curr + 0xC > buffer + size) return 0;

		//all sections must start with nns_.
		if (*(int *) curr != 0x5F736E6E) return 0;
		int section = *(int *) (curr + 4);
		unsigned int sectionSize = *(unsigned int *) (curr + 8);

		//does the section size make sense?
		if (sectionSize < 0xC) return 0;

		//is the entire section within the file?
		if (curr - buffer + sectionSize > size) return 0;

		//Is this the end marker?
		if (section == 0x62646E65) return 1;
		curr += sectionSize;
	}

	return 1;
}

int nitroTgaRead(LPWSTR path, TEXELS *texels, PALETTE *palette) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);

	if (!nitrotgaIsValid(lpBuffer, dwSize)) {
		free(lpBuffer);
		return 1;
	}

	int commentLength = *lpBuffer;
	int nitroOffset = *(int *) (lpBuffer + 0x12 + commentLength - 4);
	LPBYTE buffer = lpBuffer + nitroOffset;

	int width = *(short *) (lpBuffer + 0xC);
	int height = *(short *) (lpBuffer + 0xE);

	int frmt = 0;
	int c0xp = 0;
	char pnam[16] = { 0 };
	char *txel = NULL;
	char *pcol = NULL;
	char *pidx = NULL;

	int nColors = 0;

	while (1) {
		char sect[9] = { 0 };
		memcpy(sect, buffer, 8);
		if (!strcmp(sect, "nns_endb")) break;

		buffer += 8;
		int length = (*(int *) buffer) - 0xC;
		buffer += 4;

		if (!strcmp(sect, "nns_txel")) {
			txel = calloc(length, 1);
			memcpy(txel, buffer, length);
		} else if (!strcmp(sect, "nns_pcol")) {
			pcol = calloc(length, 1);
			memcpy(pcol, buffer, length);
			nColors = length / 2;
		} else if (!strcmp(sect, "nns_pidx")) {
			pidx = calloc(length, 1);
			memcpy(pidx, buffer, length);
		} else if (!strcmp(sect, "nns_frmt")) {
			if (!strncmp(buffer, "tex4x4", length)) {
				frmt = CT_4x4;
			} else if (!strncmp(buffer, "palette4", length)) {
				frmt = CT_4COLOR;
			} else if (!strncmp(buffer, "palette16", length)) {
				frmt = CT_16COLOR;
			} else if (!strncmp(buffer, "palette256", length)) {
				frmt = CT_256COLOR;
			} else if (!strncmp(buffer, "a3i5", length)) {
				frmt = CT_A3I5;
			} else if (!strncmp(buffer, "a5i3", length)) {
				frmt = CT_A5I3;
			} else if (!strncmp(buffer, "direct", length)) {
				frmt = CT_DIRECT;
			}
		} else if (!strcmp(sect, "nns_c0xp")) {
			c0xp = 1;
		} else if (!strcmp(sect, "nns_pnam")) {
			memcpy(pnam, buffer, length);
		}

		buffer += length;
	}

	if (frmt != CT_DIRECT) {
		palette->pal = (short *) pcol;
		palette->nColors = nColors;
		memcpy(palette->name, pnam, 16);
	}
	texels->cmp = (short *) pidx;
	texels->texel = txel;
	int texImageParam = 0;
	if (c0xp) texImageParam |= (1 << 29);
	texImageParam |= (1 << 17) | (1 << 16);
	texImageParam |= (ilog2(width >> 3) << 20) | (ilog2(height >> 3) << 23);
	texImageParam |= frmt << 26;
	texels->texImageParam = texImageParam;

	//copy texture name
	int nameOffset = 0;
	for (unsigned int i = 0; i < wcslen(path); i++) {
		if (path[i] == L'/' || path[i] == L'\\') nameOffset = i + 1;
	}
	LPWSTR name = path + nameOffset;
	memset(texels->name, 0, 16);
	for (unsigned int i = 0; i <= wcslen(name); i++) { //copy up to including null terminator
		if (i == 16) break;
		if (name[i] == L'.') break;
		texels->name[i] = (char) name[i];
	}

	return 0;
}
