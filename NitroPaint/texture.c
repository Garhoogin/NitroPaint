#include <Windows.h>
#include <stdio.h>
#include "texture.h"

int ilog2(int x);

int TxRoundTextureSize(int dimension) {
	if (dimension < 8) return 8; //min
	
	//round up
	dimension = (dimension << 1) - 1;
	dimension = 1 << ilog2(dimension);
	return dimension;
}

int TxGetTexelSize(int width, int height, int texImageParam) {
	int nPx = width * height;
	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
	int b = bits[FORMAT(texImageParam)];
	return (nPx * b) >> 3;
}

int TxGetIndexVramSize(TEXELS *texels) {
	int texImageParam = texels->texImageParam;
	int format = FORMAT(texImageParam);
	int hasIndex = format == CT_4x4;

	int texelSize = TxGetTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
	int indexSize = hasIndex ? (texelSize / 2) : 0;
	return indexSize;
}

int TxGetTextureVramSize(TEXELS *texels) {
	int texImageParam = texels->texImageParam;
	int w = TEXW(texImageParam);
	int h = TEXH(texImageParam);
	int fmt = FORMAT(texImageParam);

	int bpps[] = { 0, 8, 2, 4, 8, 3, 8, 16 };
	return bpps[fmt] * w * h / 8;
}

int TxGetTexPlttVramSize(PALETTE *palette) {
	return palette->nColors * sizeof(COLOR);
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

const char *TxNameFromTexFormat(int fmt) {
	const char *fmts[] = { "", "a3i5", "palette4", "palette16", "palette256", "tex4x4", "a5i3", "direct" };
	return fmts[fmt];
}

static COLOR32 TxiBlend(COLOR32 c1, COLOR32 c2, int factor) {
	unsigned int r = ((c1 >> 0) & 0xFF) * (8 - factor) + ((c2 >> 0) & 0xFF) * factor;
	unsigned int g = ((c1 >> 8) & 0xFF) * (8 - factor) + ((c2 >> 8) & 0xFF) * factor;
	unsigned int b = ((c1 >> 16) & 0xFF) * (8 - factor) + ((c2 >> 16) & 0xFF) * factor;
	r = (r + 4) / 8;
	g = (g + 4) / 8;
	b = (b + 4) / 8;
	return ColorRoundToDS18(r | (g << 8) | (b << 16));
}

void TxRender(COLOR32 *px, int dstWidth, int dstHeight, TEXELS *texels, PALETTE *palette, int flip) {
	int format = FORMAT(texels->texImageParam);
	int c0xp = COL0TRANS(texels->texImageParam);
	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	int nPixels = width * height;
	int txelSize = TxGetTexelSize(width, height, texels->texImageParam);
	switch (format) {
		case CT_DIRECT:
		{
			for (int i = 0; i < nPixels; i++) {
				int curX = i % width, curY = i / width;
				COLOR pVal = ((COLOR *) texels->texel)[i];
				if (curX < dstWidth && curY < dstHeight) {
					px[curX + curY * dstWidth] = ColorConvertFromDS(pVal) | (GetA(pVal) ? 0xFF000000 : 0);
				}
			}
			break;
		}
		case CT_4COLOR:
		{
			int offs = 0;
			for (int i = 0; i < txelSize >> 2; i++) {
				uint32_t d = ((uint32_t *) texels->texel)[i];
				for (int j = 0; j < 16; j++) {
					int curX = offs % width, curY = offs / width;
					int pVal = d & 0x3;
					d >>= 2;
					if (curX < dstWidth && curY < dstHeight && pVal < palette->nColors) {
						if (!pVal && c0xp) {
							px[offs] = 0;
						} else {
							COLOR col = palette->pal[pVal];
							px[curX + curY * dstWidth] = ColorConvertFromDS(col) | 0xFF000000;
						}
					}
					offs++;
				}
			}
			break;
		}
		case CT_16COLOR:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t pVal = texels->texel[i];
				COLOR32 col0 = 0;
				COLOR32 col1 = 0;

				if ((pVal & 0xF) < palette->nColors) {
					col0 = ColorConvertFromDS(palette->pal[pVal & 0xF]) | 0xFF000000;
				}
				if ((pVal >> 4) < palette->nColors) {
					col1 = ColorConvertFromDS(palette->pal[pVal >> 4]) | 0xFF000000;
				}

				if (c0xp) {
					if (!(pVal & 0xF)) col0 = 0;
					if (!(pVal >> 4)) col1 = 0;
				}

				int offs = i * 2;
				int curX = offs % width, curY = offs / width;
				if ((curX + 0) < dstWidth && curY < dstHeight) px[offs + 0] = col0;
				if ((curX + 1) < dstWidth && curY < dstHeight) px[offs + 1] = col1;
			}
			break;
		}
		case CT_256COLOR:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t pVal = texels->texel[i];
				int destX = i % width, destY = i / width;
				if (destX < dstWidth && destY < dstHeight && pVal < palette->nColors) {
					if (!pVal && c0xp) {
						px[i] = 0;
					} else {
						COLOR col = palette->pal[pVal];
						px[destX + destY * dstWidth] = ColorConvertFromDS(col) | 0xFF000000;
					}
				}
			}
			break;
		}
		case CT_A3I5:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t d = texels->texel[i];
				int alpha = ((d & 0xE0) >> 5) * 255 / 7;
				int index = d & 0x1F;
				if (index < palette->nColors) {
					COLOR32 atIndex = ColorConvertFromDS(palette->pal[index]);
					int destX = i % width, destY = i / width;
					if (destX < dstWidth && destY < dstHeight) {
						px[destX + destY * dstWidth] = atIndex | (alpha << 24);
					}
				}
			}
			break;
		}
		case CT_A5I3:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t d = texels->texel[i];
				int alpha = ((d & 0xF8) >> 3) * 255 / 31;
				int index = d & 0x7;
				if (index < palette->nColors) {
					COLOR32 atIndex = ColorConvertFromDS(palette->pal[index]);
					int destX = i % width, destY = i / width;
					if (destX < dstWidth && destY < dstHeight) {
						px[destX + destY * dstWidth] = atIndex | (alpha << 24);
					}
				}
			}
			break;
		}
		case CT_4x4:
		{
			int tilesX = width / 4;
			int tilesY = height / 4;
			int nTiles = tilesX * tilesY;
			for (int i = 0; i < nTiles; i++) {
				int tileX = i % tilesX;
				int tileY = i / tilesX;

				COLOR32 colors[4] = { 0 };
				uint32_t texel = *(uint32_t *) (texels->texel + (i << 2));
				uint16_t index = texels->cmp[i];

				int address = COMP_INDEX(index);
				int mode = index & COMP_MODE_MASK;
				COLOR *base = palette->pal + address;
				if (address + 2 <= palette->nColors) {
					colors[0] = ColorConvertFromDS(base[0]) | 0xFF000000;
					colors[1] = ColorConvertFromDS(base[1]) | 0xFF000000;
				}

				if (mode == (COMP_TRANSPARENT | COMP_FULL)) {
					//require 3 colors
					if (address + 3 <= palette->nColors) {
						colors[2] = ColorConvertFromDS(base[2]) | 0xFF000000;
					}
					colors[3] = 0;
				} else if (mode == (COMP_TRANSPARENT | COMP_INTERPOLATE)) {
					//require 2 colors
					COLOR32 col0 = 0, col1 = 0;
					if (address + 2 <= palette->nColors) {
						col0 = colors[0];
						col1 = colors[1];
					}
					colors[2] = TxiBlend(col0, col1, 4) | 0xFF000000;
					colors[3] = 0;
				} else if (mode == (COMP_OPAQUE | COMP_FULL)) {
					//require 4 colors
					if (address + 4 <= palette->nColors) {
						colors[2] = ColorConvertFromDS(base[2]) | 0xFF000000;
						colors[3] = ColorConvertFromDS(base[3]) | 0xFF000000;
					}
				} else {
					//require 2 colors
					COLOR32 col0 = 0, col1 = 0;
					if (address + 2 <= palette->nColors) {
						col0 = colors[0];
						col1 = colors[1];
					}
					
					colors[2] = TxiBlend(col0, col1, 3) | 0xFF000000;
					colors[3] = TxiBlend(col0, col1, 5) | 0xFF000000;
				}

				for (int j = 0; j < 16; j++) {
					int pVal = texel & 0x3;
					texel >>= 2;

					int destX = (j % 4) + tileX * 4;
					int destY = (j / 4) + tileY * 4;
					if (destX < dstWidth && destY < dstHeight) {
						px[destX + destY * dstWidth] = colors[pVal];
					}
				}
			}
			break;
		}
	}

	//swap RB
	for (int i = 0; i < dstWidth * dstHeight; i++) {
		COLOR32 c = px[i];
		px[i] = REVERSE(c);
	}

	//flip upside down
	if (flip) {
		COLOR32 *tmp = calloc(dstWidth, 4);
		for (int y = 0; y < dstHeight / 2; y++) {
			COLOR32 *row1 = px + y * dstWidth;
			COLOR32 *row2 = px + (dstHeight - 1 - y) * dstWidth;
			memcpy(tmp, row1, dstWidth * sizeof(COLOR32));
			memcpy(row1, row2, dstWidth * sizeof(COLOR32));
			memcpy(row2, tmp, dstWidth * sizeof(COLOR32));
		}
		free(tmp);
	}
}

#pragma comment(lib, "Version.lib")

static void getVersion(char *buffer, int max) {
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

static void TxiNnsTgaWriteSection(HANDLE hFile, const char *section, const void *data, int size) {
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

static int imageHasTransparent(COLOR32 *px, int nPx) {
	for (int i = 0; i < nPx; i++) {
		COLOR32 c = px[i];
		int a = c >> 24;
		if (a < 255) return 1; //transparent/translucent pixel
	}
	return 0;
}

static void TxiNnsTgaWritePixels(HANDLE hFile, COLOR32 *rawPx, int width, int height, int depth) {
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

void TxWriteNnsTga(LPCWSTR name, TEXELS *texels, PALETTE *palette) {
	HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwWritten;

	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	COLOR32 *pixels = (COLOR32 *) calloc(width * height, 4);
	TxRender(pixels, width, height, texels, palette, 1);
	int depth = imageHasTransparent(pixels, width * height) ? 32 : 24;

	uint8_t header[] = {0x14, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 8,
		'N', 'N', 'S', '_', 'T', 'g', 'a', ' ', 'V', 'e', 'r', ' ', '1', '.', '0', 0, 0, 0, 0, 0};
	*(uint16_t *) (header + 0x0C) = width;
	*(uint16_t *) (header + 0x0E) = height;
	*(uint8_t *) (header + 0x10) = depth;
	*(uint32_t *) (header + 0x22) = sizeof(header) + width * height * (depth / 8);
	WriteFile(hFile, header, sizeof(header), &dwWritten, NULL);
	TxiNnsTgaWritePixels(hFile, pixels, width, height, depth);

	//format
	const char *fstr = TxNameFromTexFormat(FORMAT(texels->texImageParam));
	TxiNnsTgaWriteSection(hFile, "nns_frmt", fstr, -1);

	//texels
	int txelLength = TxGetTexelSize(width, height, texels->texImageParam);
	TxiNnsTgaWriteSection(hFile, "nns_txel", texels->texel, txelLength);

	//write 4x4 if applicable
	if (FORMAT(texels->texImageParam) == CT_4x4) {
		int pidxLength = txelLength / 2;
		TxiNnsTgaWriteSection(hFile, "nns_pidx", texels->cmp, pidxLength);
	}

	//palette (if applicable)
	if (FORMAT(texels->texImageParam) != CT_DIRECT) {
		int pnamLength = max16Len(palette->name);
		TxiNnsTgaWriteSection(hFile, "nns_pnam", palette->name, pnamLength);

		int nColors = palette->nColors;
		if (FORMAT(texels->texImageParam) == CT_4COLOR && nColors > 4) nColors = 4;
		if (nColors == 4 || (nColors % 8) == 0) {
			//valid size
			TxiNnsTgaWriteSection(hFile, "nns_pcol", palette->pal, nColors * sizeof(COLOR));
		} else {
			//needs padding
			int outPaletteSize = (nColors + 7) / 8 * 8;
			if (nColors < 4) outPaletteSize = (nColors + 3) / 4 * 4;

			COLOR *padded = (COLOR *) calloc(outPaletteSize, sizeof(COLOR));
			memcpy(padded, palette->pal, palette->nColors * sizeof(COLOR));
			TxiNnsTgaWriteSection(hFile, "nns_pcol", padded, outPaletteSize * sizeof(COLOR));
			free(padded);
		}
	}

	//NitroPaint generator signature
	char version[16];
	getVersion(version, 16);
	TxiNnsTgaWriteSection(hFile, "nns_gnam", "NitroPaint", -1);
	TxiNnsTgaWriteSection(hFile, "nns_gver", version, -1);

	//dummy imagestudio data
	TxiNnsTgaWriteSection(hFile, "nns_imst", NULL, 0);

	//if c0xp
	if (COL0TRANS(texels->texImageParam)) {
		TxiNnsTgaWriteSection(hFile, "nns_c0xp", NULL, 0);
	}

	//write end
	TxiNnsTgaWriteSection(hFile, "nns_endb", NULL, 0);
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

int TxDimensionIsValid(int x) {
	if (x & (x - 1)) return 0;
	if (x < 8 || x > 1024) return 0;
	return 1;
}

int TxIsValidNnsTga(const unsigned char *buffer, unsigned int size) {
	//is the file even big enough to hold a TGA header and comment?
	if (size < 0x16) return 0;
	
	unsigned int commentLength = *buffer;
	if (commentLength < 4) return 0;

	unsigned int ptrOffset = 0x12 + commentLength - 4;
	if (ptrOffset + 4 > size) return 0;

	uint32_t ptr = *(uint32_t *) (buffer + ptrOffset);
	if (ptr + 0xC > size) return 0;

	//process sections. When any anomalies are found, return 0.
	const unsigned char *curr = buffer + ptr;
	while (1) {
		//is there space enough left for a section header?
		if (curr + 0xC > buffer + size) return 0;

		//all sections must start with nns_.
		if (*(uint32_t *) curr != 0x5F736E6E) return 0;
		uint32_t section = *(uint32_t *) (curr + 4);
		uint32_t sectionSize = *(uint32_t *) (curr + 8);

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

int TxReadNnsTga(LPCWSTR path, TEXELS *texels, PALETTE *palette) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);

	if (!TxIsValidNnsTga(lpBuffer, dwSize)) {
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
	texImageParam |= (ilog2(TxRoundTextureSize(width) >> 3) << 20) | (ilog2(TxRoundTextureSize(height) >> 3) << 23);
	texImageParam |= frmt << 26;
	texels->texImageParam = texImageParam;
	texels->height = height;

	//copy texture name
	int nameOffset = 0;
	for (unsigned int i = 0; i < wcslen(path); i++) {
		if (path[i] == L'/' || path[i] == L'\\') nameOffset = i + 1;
	}
	LPCWSTR name = path + nameOffset;
	memset(texels->name, 0, 16);
	WCHAR *lastDot = wcsrchr(name, L'.');
	for (unsigned int i = 0; i <= wcslen(name); i++) { //copy up to including null terminator
		if (i == 16) break;
		if (name + i == lastDot) break; //file extension
		texels->name[i] = (char) name[i];
	}

	free(lpBuffer);
	return 0;
}
