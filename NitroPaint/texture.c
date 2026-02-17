#include <Windows.h>
#include <stdio.h>
#include "texture.h"
#include "nns.h"

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

const char *TxNameFromTexFormat(int fmt) {
	const char *fmts[] = { "", "a3i5", "palette4", "palette16", "palette256", "tex4x4", "a5i3", "direct" };
	return fmts[fmt];
}

static COLOR32 TxiBlend(COLOR32 c1, COLOR32 c2, int factor) {
	unsigned int r = ((c1 >>  0) & 0xFF) * (8 - factor) + ((c2 >>  0) & 0xFF) * factor;
	unsigned int g = ((c1 >>  8) & 0xFF) * (8 - factor) + ((c2 >>  8) & 0xFF) * factor;
	unsigned int b = ((c1 >> 16) & 0xFF) * (8 - factor) + ((c2 >> 16) & 0xFF) * factor;
	r = (r + 4) / 8;
	g = (g + 4) / 8;
	b = (b + 4) / 8;
	return ColorRoundToDS18(r | (g << 8) | (b << 16)) | 0xFF000000;
}

static COLOR32 TxiSamplePltt(const COLOR *pltt, unsigned int nPltt, unsigned int i) {
	if (i >= nPltt) return 0;
	return ColorConvertFromDS(pltt[i]);
}

static COLOR32 TxiSampleDirect(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;
	(void) c0xp;
	(void) pltt;
	(void) nPltt;

	unsigned int iPx = x + y * texW;
	COLOR colSrc = ((COLOR *) txel)[iPx];

	COLOR32 c = ColorConvertFromDS(colSrc);
	if (colSrc & 0x8000) c |= 0xFF000000;
	return c;
}

static COLOR32 TxiSamplePltt4(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;

	unsigned int iPx = x + y * texW;
	unsigned int index = (txel[iPx >> 2] >> ((iPx & 3) * 2)) & 0x3;
	if (index == 0 && c0xp) return 0;

	return TxiSamplePltt(pltt, nPltt, index) | 0xFF000000;
}

static COLOR32 TxiSamplePltt16(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;

	unsigned int iPx = x + y * texW;
	unsigned int index = (txel[iPx >> 1] >> ((iPx & 1) * 4)) & 0xF;
	if (index == 0 && c0xp) return 0;

	return TxiSamplePltt(pltt, nPltt, index) | 0xFF000000;
}

static COLOR32 TxiSamplePltt256(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;

	unsigned int iPx = x + y * texW;
	unsigned int index = txel[iPx];
	if (index == 0 && c0xp) return 0;

	return TxiSamplePltt(pltt, nPltt, index) | 0xFF000000;
}

static COLOR32 TxiSampleA3I5(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) c0xp;
	(void) pidx;

	uint8_t d = txel[x + y * texW];
	unsigned int index = (d & 0x1F) >> 0;
	unsigned int alpha = (d & 0xE0) >> 5;

	alpha = (alpha << 2) | (alpha >> 1);  // 3-bit -> 5-bit alpha
	alpha = (alpha * 510 + 31) / 62;      // scale to 8-bit

	return TxiSamplePltt(pltt, nPltt, index) | (alpha << 24);
}

static COLOR32 TxiSampleA5I3(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;
	(void) c0xp;

	uint8_t d = txel[x + y * texW];
	unsigned int index = (d & 0x07) >> 0;
	unsigned int alpha = (d & 0xF8) >> 3;

	alpha = (alpha * 510 + 31) / 62;      // scale to 8-bit

	return TxiSamplePltt(pltt, nPltt, index) | (alpha << 24);
}

static COLOR32 TxiSampleTex4x4(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) c0xp;

	unsigned int i = (x / 4) + (y / 4) * (texW / 4);

	uint32_t texel = *(const uint32_t *) (txel + (i << 2));
	uint16_t index = pidx[i];

	unsigned int address = COMP_INDEX(index);

	COLOR32 colors[4] = { 0 };
	colors[0] = TxiSamplePltt(pltt, nPltt, address + 0) | 0xFF000000;
	colors[1] = TxiSamplePltt(pltt, nPltt, address + 1) | 0xFF000000;

	if (!(index & COMP_INTERPOLATE)) {
		colors[2] = TxiSamplePltt(pltt, nPltt, address + 2) | 0xFF000000;
		if (index & COMP_OPAQUE) {
			colors[3] = TxiSamplePltt(pltt, nPltt, address + 3) | 0xFF000000;
		}
	}

	if (index & COMP_INTERPOLATE) {
		if (index & COMP_OPAQUE) {
			//blend colors 0,1 to 2,3
			colors[2] = TxiBlend(colors[0], colors[1], 3);
			colors[3] = TxiBlend(colors[0], colors[1], 5);
		} else {
			//blend colors 0,1 to 2
			colors[2] = TxiBlend(colors[0], colors[1], 4);
		}
	}

	unsigned int j = (x & 3) + ((y & 3) << 2);
	unsigned int pVal = (texel >> (2 * j)) & 0x3;
	return colors[pVal];
}

void TxRenderRect(COLOR32 *px, unsigned int srcX, unsigned int srcY, unsigned int srcW, unsigned int srcH, TEXELS *texels, PALETTE *palette) {
	unsigned int width = TEXW(texels->texImageParam);
	unsigned int height = texels->height;

	typedef COLOR32(*pfnSample) (const unsigned char *texel, const uint16_t *pidx, unsigned int texW, int c0xp,
		unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt);

	static const pfnSample pfnSamples[8] = {
		NULL,
		TxiSampleA3I5,
		TxiSamplePltt4,
		TxiSamplePltt16,
		TxiSamplePltt256,
		TxiSampleTex4x4,
		TxiSampleA5I3,
		TxiSampleDirect
	};
	pfnSample sample = pfnSamples[FORMAT(texels->texImageParam)];
	
	for (unsigned int y = 0; y < srcH; y++) {
		for (unsigned int x = 0; x < srcW; x++) {
			COLOR32 c = 0;
			if (sample != NULL && x < width && y < height) {
				c = sample(texels->texel, texels->cmp, width, COL0TRANS(texels->texImageParam), srcX + x, srcY + y, palette->pal, palette->nColors);
			}

			px[x + y * srcW] = c;
		}
	}
}

void TxRender(COLOR32 *px, TEXELS *texels, PALETTE *palette) {
	TxRenderRect(px, 0, 0, TEXW(texels->texImageParam), texels->height, texels, palette);
}


void TxFree(ObjHeader *obj) {
	TextureObject *texture = (TextureObject *) obj;
	if (texture->texture.texels.texel != NULL) free(texture->texture.texels.texel);
	if (texture->texture.texels.cmp != NULL) free(texture->texture.texels.cmp);
	if (texture->texture.palette.pal != NULL) free(texture->texture.palette.pal);
	texture->texture.texels.texel = NULL;
	texture->texture.texels.cmp = NULL;
	texture->texture.palette.pal = NULL;
}

void TxUncontain(TextureObject *texture, TEXTURE *out) {
	//nothing needs to be done here at the moment.
	memcpy(out, &texture->texture, sizeof(TEXTURE));

	memset(&texture->texture, 0, sizeof(TEXTURE));
	ObjFree(&texture->header);
}

TextureObject *TxContain(TEXTURE *texture, int format) {
	TextureObject *object = (TextureObject *) ObjAlloc(FILE_TYPE_TEXTURE, format);
	memcpy(&object->texture, texture, sizeof(TEXTURE));
	return object;
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

int TxIsValidIStudio(const unsigned char *buffer, unsigned int size) {
	//must be valid G2D structure
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NTTX", 4) != 0) return 0;

	//magic must be "NTTX" and have "PALT" and "IMGE" sections
	unsigned int paltSize = 0, imgeSize = 0;
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, &paltSize);
	const unsigned char *imge = NnsG2dFindBlockBySignature(buffer, size, "IMGE", NNS_SIG_BE, &imgeSize);

	//IMGE must be present
	if (imge == NULL) return 0;

	//we don't support textures without palettes unless it's a direct color image
	int fmt = imge[0];
	if (fmt > CT_DIRECT) return 0;
	if (fmt != CT_DIRECT && palt == NULL) return 0;

	//validate the PALT section
	if (palt != NULL) {
		uint32_t nColors = *(uint32_t *) (palt + 0);
		if (nColors * 2 + 0x4 != paltSize) return 0;
	}

	//validate IMGE section
	int log2Width = imge[1];
	int log2Height = imge[2];
	if (log2Width > 7 || log2Height > 7) return 0; //too large

	int fullWidth = 8 << (log2Width);
	int fullHeight = 8 << (log2Height);
	int origWidth = *(uint16_t *) (imge + 0x4);
	int origHeight = *(uint16_t *) (imge + 0x6);
	if (origWidth > fullWidth || origHeight > fullHeight) return 0;
	return 1;
}

int TxIsValidTds(const unsigned char *buffer, unsigned int size) {
	if (size < 0x24 || (size & 3)) return 0;

	uint32_t magic = *(uint32_t *) (buffer + 0);
	if (magic != '.tds') return 0;

	uint32_t texCount = *(uint32_t *) (buffer + 0x04);
	if (texCount != 1) {
		//printf("!!!TexCount not 1!!!\n");
		return 0;
	}

	uint32_t texFormat = *(uint8_t *) (buffer + 0x08);
	uint32_t texSizeS = *(uint8_t *) (buffer + 0x09);
	uint32_t texSizeT = *(uint8_t *) (buffer + 0x0A);
	uint32_t textureOffset = *(uint32_t *) (buffer + 0x0C);
	uint32_t paletteOffset = *(uint32_t *) (buffer + 0x14);
	uint32_t width = *(uint32_t *) (buffer + 0x1C);
	uint32_t height = *(uint32_t *) (buffer + 0x20);
	if (textureOffset < 0x24 || textureOffset >= size)
		return 0;
	if (paletteOffset < 0x24 || paletteOffset >= size)
		return 0;
	if (width > (8u << texSizeS) || height > (8u << texSizeT) || texFormat == 0)
		return 0;

	if (texFormat == CT_4x4) {
		//printf("!!!TexFormat is 4x4!?!?\n");
		//return 0;
	}

	return 1;
}

int TxIsValidNtga(const unsigned char *buffer, unsigned int size) {
	if (size < 0x30) return 0;
	if (memcmp(buffer, "NTGA", 4) != 0) return 0;
	
	uint32_t fmt = *(uint32_t *) (buffer + 0x04);
	uint32_t sizeS = *(uint32_t *) (buffer + 0x08);
	uint32_t sizeT = *(uint32_t *) (buffer + 0x0C);
	uint16_t srcWidth = *(uint16_t *) (buffer + 0x14);
	uint16_t srcHeight = *(uint16_t *) (buffer + 0x16);
	if (fmt == 0 || fmt > CT_DIRECT) return 0;
	if (sizeS > 7 || sizeT > 7) return 0;

	//check size
	if (srcWidth > (8u << sizeS) || srcHeight > (8u << sizeT)) return 0;

	//check texel
	uint32_t ofstxel = *(uint32_t *) (buffer + 0x18);
	uint32_t siztxel = *(uint32_t *) (buffer + 0x1C);
	if (ofstxel < 0x30 || ofstxel >= size || siztxel == 0 || (ofstxel + siztxel) > size) return 0;

	//check palette
	uint32_t ofsplt = *(uint32_t *) (buffer + 0x20);
	uint32_t sizplt = *(uint32_t *) (buffer + 0x24);
	if (ofsplt != 0) {
		if (ofsplt < 0x30 || ofsplt >= size || sizplt == 0 || (ofsplt + sizplt) > size) return 0;
	}

	return 1;
}

static int TxIsValidToLoveRu(const unsigned char *buffer, unsigned int size) {
	//check header
	if (size < 0x28) return 0;
	if (buffer[0] != 't' || buffer[1] != 'e' || buffer[2] != 'x' || buffer[3] != 0) return 0;

	uint16_t fmt = *(uint16_t *) (buffer + 0x06);
	uint16_t width = *(uint16_t *) (buffer + 0x08);
	uint16_t height = *(uint16_t *) (buffer + 0x0A);
	uint32_t offsPltt = *(uint32_t *) (buffer + 0x0C);
	uint32_t sizePltt = *(uint32_t *) (buffer + 0x10);
	uint32_t offsTexImage = *(uint32_t *) (buffer + 0x18);
	uint32_t sizeTexImage = *(uint32_t *) (buffer + 0x1C);
	uint32_t offsPlttIdx = *(uint32_t *) (buffer + 0x20);
	uint32_t sizePlttIdx = *(uint32_t *) (buffer + 0x24);

	if (fmt < 1 || fmt > 7) return 0;
	if ((8 << width) > 1024) return 0;
	if ((8 << height) > 1024) return 0;

	if (offsTexImage >= size || (size - offsTexImage) < sizeTexImage) return 0;
	if (fmt == CT_4x4) {
		//check palette index fields
		if (sizePlttIdx != (sizeTexImage / 2)) return 0;
		if (offsPlttIdx == 0 || sizePlttIdx == 0) return 0;
		if (offsPlttIdx >= size || (size - offsPlttIdx) < sizePlttIdx) return 0;
	}
	if (fmt != CT_DIRECT) {
		//check texture palette fields
		if (offsPltt >= size || (size - offsPltt) < sizePltt) return 0;
		if (sizePltt & 1) return 0;
	}
	return 1;
}

static int TxIsValidSpt(const unsigned char *buffer, unsigned int size) {
	if (size < 0x20) return 0;
	if (memcmp(buffer, " TPS", 4) != 0) return 0; // file signature

	uint32_t param = *(const uint32_t *) (buffer + 0x04);
	uint32_t sizTex = *(const uint32_t *) (buffer + 0x08);
	uint32_t ofsPlt = *(const uint32_t *) (buffer + 0x0C);
	uint32_t sizPlt = *(const uint32_t *) (buffer + 0x10);
	uint32_t ofsIdx = *(const uint32_t *) (buffer + 0x14);
	uint32_t sizIdx = *(const uint32_t *) (buffer + 0x18);
	uint32_t sizAll = *(const uint32_t *) (buffer + 0x1C);

	//check offsets
	if (ofsPlt > size) return 0;
	if (ofsIdx > size) return 0;
	if (sizPlt > (size - ofsPlt)) return 0;
	if (sizTex > (size - 0x20)) return 0;
	if (sizIdx > (size - ofsIdx)) return 0;

	//check data size
	if ((sizTex + sizPlt + sizIdx + 0x20) != sizAll) return 0;

	return 1;
}

static int TxIsValidGrf(const unsigned char *buffer, unsigned int size) {
	int grfType = GrfIsValid(buffer, size);
	if (grfType == GRF_TYPE_INVALID) return 0;

	unsigned int hdrSize;
	unsigned char *hdr = GrfGetHeader(buffer, size, &hdrSize);
	if (hdr == NULL) return 0;

	int texFmt = *(const uint16_t *) (hdr + (grfType == GRF_TYPE_GRF_20 ? 0x02 : 0x00));
	int scrFmt = *(const uint16_t *) (hdr + (grfType == GRF_TYPE_GRF_20 ? 0x04 : 0x02));
	uint16_t flag = grfType == GRF_TYPE_GRF_20 ? *(const uint16_t *) (hdr + 0x0E) : 0;

	unsigned int palSize, gfxSize, pidxSize, cellSize;
	unsigned char *gfx = GrfReadBlockUncompressed(buffer, size, "GFX ", &gfxSize);
	unsigned char *pal = GrfReadBlockUncompressed(buffer, size, "PAL ", &palSize);
	unsigned char *idx = GrfReadBlockUncompressed(buffer, size, "PIDX", &pidxSize);
	unsigned char *obj = GrfReadBlockUncompressed(buffer, size, "CELL", &cellSize);

	int gfxExist = gfx != NULL;
	int palExist = pal != NULL;
	int idxExist = idx != NULL;
	int objExist = obj != NULL;
	if (gfxExist) free(gfx);
	if (palExist) free(pal);
	if (idxExist) free(idx);
	if (objExist) free(obj);

	if (!gfxExist) return 0;
	if (objExist)                    return 0; // OBJ mode graphics
	if ((flag >> 14) != 0)           return 0; // not texture mode flag
	if (texFmt != 0x10 && !palExist) return 0; // non-direct mode texture requires palette
	if (texFmt == 0x82 && !idxExist) return 0; // 4x4 texture requires palette index
	if (scrFmt != 0) return 0;                 // should have no BG screen data
	return 1;
}

static void TxiRegisterFormat(int format, const wchar_t *name, ObjIdFlag flag, ObjIdProc proc) {
	ObjRegisterFormat(FILE_TYPE_TEXTURE, format, name, flag, proc);
}

void TxRegisterFormats(void) {
	ObjRegisterType(FILE_TYPE_TEXTURE, sizeof(TextureObject), L"Texture", (ObjReader) TxRead, (ObjWriter) TxWrite, NULL, TxFree);
	TxiRegisterFormat(TEXTURE_TYPE_NNSTGA, L"NNS TGA", OBJ_ID_HEADER | OBJ_ID_VALIDATED | OBJ_ID_CHUNKED | OBJ_ID_OFFSETS | OBJ_ID_WINCODEC_OVERRIDE, TxIsValidNnsTga);
	TxiRegisterFormat(TEXTURE_TYPE_ISTUDIO, L"5TX", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED | OBJ_ID_CHUNKED, TxIsValidIStudio);
	TxiRegisterFormat(TEXTURE_TYPE_SPT, L"SPT", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_OFFSETS, TxIsValidSpt);
	TxiRegisterFormat(TEXTURE_TYPE_TDS, L"TDS", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED | OBJ_ID_OFFSETS, TxIsValidTds);
	TxiRegisterFormat(TEXTURE_TYPE_NTGA, L"NTGA", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED | OBJ_ID_OFFSETS, TxIsValidNtga);
	TxiRegisterFormat(TEXTURE_TYPE_TOLOVERU, L"To Love-Ru", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED | OBJ_ID_OFFSETS, TxIsValidToLoveRu);
	TxiRegisterFormat(TEXTURE_TYPE_GRF, L"GRF", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_VALIDATED, TxIsValidGrf);
}

int TxIdentify(const unsigned char *buffer, unsigned int size) {
	int fmt = TEXTURE_TYPE_INVALID;
	ObjIdentifyExByType(buffer, size, FILE_TYPE_TEXTURE, &fmt);
	return fmt;
}

int TxIdentifyFile(LPCWSTR path) {
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeHigh;
	DWORD dwSize = GetFileSize(hFile, &dwSizeHigh);
	LPBYTE lpBuffer = (LPBYTE) malloc(dwSize);
	DWORD dwRead;
	ReadFile(hFile, lpBuffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);

	int type = TxIdentify(lpBuffer, dwRead);
	free(lpBuffer);
	return type;
}

int TxReadNnsTga(TextureObject *texture, const unsigned char *lpBuffer, unsigned int dwSize) {
	int commentLength = *lpBuffer;
	int nitroOffset = *(int *) (lpBuffer + 0x12 + commentLength - 4);
	const unsigned char *buffer = lpBuffer + nitroOffset;

	int width = *(int16_t *) (lpBuffer + 0xC);
	int height = *(int16_t *) (lpBuffer + 0xE);

	int frmt = 0;
	int c0xp = 0;
	char *pnam = NULL;
	unsigned char *txel = NULL;
	unsigned char *pcol = NULL;
	unsigned char *pidx = NULL;

	int nColors = 0;

	while (1) {
		char sect[9] = { 0 };
		memcpy(sect, buffer, 8);
		if (!strcmp(sect, "nns_endb")) break;

		buffer += 8;
		int length = (*(uint32_t *) buffer) - 0xC;
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
			pnam = calloc(length + 1, 1);
			memcpy(pnam, buffer, length);
		}

		buffer += length;
	}

	if (frmt != CT_DIRECT) {
		texture->texture.palette.pal = (COLOR *) pcol;
		texture->texture.palette.nColors = nColors;
		texture->texture.palette.name = pnam;
	} else {
		if (pnam != NULL) free(pnam);
	}
	texture->texture.texels.cmp = (uint16_t *) pidx;
	texture->texture.texels.texel = txel;

	int texImageParam = 0;
	if (c0xp) texImageParam |= (1 << 29);
	texImageParam |= (1 << 17) | (1 << 16);
	texImageParam |= (ilog2(TxRoundTextureSize(width) >> 3) << 20) | (ilog2(TxRoundTextureSize(height) >> 3) << 23);
	texImageParam |= frmt << 26;
	texture->texture.texels.texImageParam = texImageParam;
	texture->texture.texels.height = height;

	return 0;
}

int TxReadIStudio(TextureObject *texture, const unsigned char *buffer, unsigned int size) {
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	const unsigned char *imge = NnsG2dFindBlockBySignature(buffer, size, "IMGE", NNS_SIG_BE, NULL);

	int frmt = imge[0];
	int log2Width = imge[1];
	int log2Height = imge[2];
	int c0xp = imge[3];

	int origWidth = *(uint16_t *) (imge + 0x4);
	int origHeight = *(uint16_t *) (imge + 0x6);

	//copy palette
	if (palt != NULL) {
		texture->texture.palette.nColors = *(uint32_t *) (palt + 0);
		texture->texture.palette.pal = (COLOR *) calloc(texture->texture.palette.nColors, sizeof(COLOR));
		memcpy(texture->texture.palette.pal, palt + 0x4, texture->texture.palette.nColors * sizeof(COLOR));
	} else {
		texture->texture.palette.nColors = 0;
		texture->texture.palette.pal = NULL;
	}
	

	int texImageParam = 0;
	if (c0xp) texImageParam |= (1 << 29);
	texImageParam |= (1 << 17) | (1 << 16);
	texImageParam |= (log2Width << 20) | (log2Height << 23);
	texImageParam |= frmt << 26;
	texture->texture.texels.texImageParam = texImageParam;
	texture->texture.texels.height = origHeight;

	//copy texel
	int texelSize = TxGetTexelSize(8 << log2Width, 8 << log2Height, texture->texture.texels.texImageParam);
	int indexSize = (frmt == CT_4x4) ? texelSize / 2 : 0;
	texture->texture.texels.texel = (unsigned char *) malloc(texelSize);
	memcpy(texture->texture.texels.texel, imge + 0xC, texelSize);

	if (indexSize) {
		texture->texture.texels.cmp = (uint16_t *) malloc(indexSize);
		memcpy(texture->texture.texels.cmp, imge + 0xC + texelSize, indexSize);
	}

	return 0;
}

int TxReadTds(TextureObject *texture, const unsigned char *buffer, unsigned int size) {
	if (!TxIsValidTds(buffer, size)) return 1;

	uint32_t texFormat = *(uint8_t*) (buffer + 0x08);
	uint32_t texSizeS = *(uint8_t*) (buffer + 0x09);
	uint32_t texSizeT = *(uint8_t*) (buffer + 0x0A);
	uint32_t textureOffset = *(uint32_t*) (buffer + 0x0C);
	uint32_t textureLength = *(uint32_t*) (buffer + 0x10);
	uint32_t paletteOffset = *(uint32_t*) (buffer + 0x14);
	uint32_t paletteLength = *(uint32_t*) (buffer + 0x18);
	uint32_t width = *(uint32_t*) (buffer + 0x1C);
	uint32_t height = *(uint32_t*) (buffer + 0x20);

	uint32_t texImageParam = 0;
	texImageParam |= (texSizeS & 0x7) << 20;
	texImageParam |= (texSizeT & 0x7) << 23;
	texImageParam |= (texFormat & 0x7) << 26;

	texture->texture.texels.texImageParam = texImageParam;
	texture->texture.texels.height = height;
	texture->texture.texels.cmp = NULL;
	texture->texture.texels.texel = calloc(textureLength, 1);
	memcpy(texture->texture.texels.texel, buffer + textureOffset, textureLength);

	if (texFormat == CT_4x4) {
		texture->texture.texels.cmp = (uint16_t *) calloc(textureLength / 3, 1);
		memcpy(texture->texture.texels.cmp, buffer + textureOffset + (textureLength * 2 / 3), textureLength / 3);
	}

	texture->texture.palette.nColors = paletteLength / 2;
	texture->texture.palette.pal = calloc(paletteLength, 1);
	memcpy(texture->texture.palette.pal, buffer + paletteOffset, paletteLength);
	return 0;
}

int TxReadNtga(TextureObject *texture, const unsigned char *buffer, unsigned int size) {
	uint32_t fmt = *(uint32_t *) (buffer + 0x4);
	uint32_t sizeS = *(uint32_t *) (buffer + 0x8);
	uint32_t sizeT = *(uint32_t *) (buffer + 0xC);
	uint16_t srcWidth = *(uint16_t *) (buffer + 0x14);
	uint16_t srcHeight = *(uint16_t *) (buffer + 0x16);
	uint32_t ofsTexel = *(uint32_t *) (buffer + 0x18);
	uint32_t sizeTexel = *(uint32_t *) (buffer + 0x1C);
	uint32_t ofsPidx = *(uint32_t *) (buffer + 0x20);
	uint32_t sizePidx = *(uint32_t *) (buffer + 0x24);
	uint32_t ofsPltt = *(uint32_t *) (buffer + 0x28);
	uint32_t sizePltt = *(uint32_t *) (buffer + 0x2C);

	texture->texture.texels.height = 8 << sizeT;
	texture->texture.texels.texImageParam = (sizeS << 20) | (sizeT << 23) | (fmt << 26);
	texture->texture.texels.texel = (unsigned char *) calloc(sizeTexel, 1);
	memcpy(texture->texture.texels.texel, buffer + ofsTexel, sizeTexel);

	if (fmt != CT_DIRECT) {
		texture->texture.palette.nColors = sizePltt / 2;
		texture->texture.palette.pal = (COLOR *) calloc(sizePltt / 2, sizeof(COLOR));
		memcpy(texture->texture.palette.pal, buffer + ofsPltt, sizePltt);
	}

	if (fmt == CT_4x4) {
		texture->texture.texels.cmp = (uint16_t *) calloc(sizePidx, 1);
		memcpy(texture->texture.texels.cmp, buffer + ofsPidx, sizePidx);
	}

	return 0;
}

int TxReadToLoveRu(TextureObject *texture, const unsigned char *buffer, unsigned int size) {
	//? + 0x04
	uint16_t fmt = *(uint16_t *) (buffer + 0x06);
	uint16_t width = *(uint16_t *) (buffer + 0x08);
	uint16_t height = *(uint16_t *) (buffer + 0x0A);
	uint32_t offsPltt = *(uint32_t *) (buffer + 0x0C);
	uint32_t sizePltt = *(uint32_t *) (buffer + 0x10);
	//? + 0x14
	uint32_t offsTexImage = *(uint32_t *) (buffer + 0x18);
	uint32_t sizeTexImage = *(uint32_t *) (buffer + 0x1C);
	uint32_t offsPlttIdx = *(uint32_t *) (buffer + 0x20);
	uint32_t sizePlttIdx = *(uint32_t *) (buffer + 0x24);

	texture->texture.texels.height = 8 << height;
	texture->texture.texels.texImageParam = (width << 20) | (height << 23) | (fmt << 26);
	texture->texture.texels.texel = (unsigned char *) calloc(sizeTexImage, 1);
	memcpy(texture->texture.texels.texel, buffer + offsTexImage, sizeTexImage);

	if (fmt != CT_DIRECT) {
		texture->texture.palette.nColors = sizePltt / 2;
		texture->texture.palette.pal = (COLOR *) calloc(sizePltt / 2, sizeof(COLOR));
		memcpy(texture->texture.palette.pal, buffer + offsPltt, sizePltt);
	}

	if (fmt == CT_4x4) {
		texture->texture.texels.cmp = (uint16_t *) calloc(sizePlttIdx, 1);
		memcpy(texture->texture.texels.cmp, buffer + offsPlttIdx, sizePlttIdx);
	}

	//if (fmt != CT_DIRECT && fmt != CT_4x4) texture->texture.texels.texImageParam |= (1 << 29); // c0xp

	return 0;
}

static int TxReadSpt(TextureObject *texture, const unsigned char *buffer, unsigned int size) {
	uint32_t param = *(const uint32_t *) (buffer + 0x04);
	uint32_t sizTex = *(const uint32_t *) (buffer + 0x08);
	uint32_t ofsPlt = *(const uint32_t *) (buffer + 0x0C);
	uint32_t sizPlt = *(const uint32_t *) (buffer + 0x10);
	uint32_t ofsIdx = *(const uint32_t *) (buffer + 0x14);
	uint32_t sizIdx = *(const uint32_t *) (buffer + 0x18);
	uint32_t sizAll = *(const uint32_t *) (buffer + 0x1C);

	unsigned int texfmt = (param >> 0) & 0xF;
	unsigned int s = (param >> 4) & 0xF;
	unsigned int t = (param >> 8) & 0xF;
	unsigned int rep = (param >> 12) & 0x3;
	unsigned int flp = (param >> 14) & 0x3;
	unsigned int plt0 = (param >> 16) & 0x1;
	unsigned int flipRepeat = (param >> 12) & 0xF;

	texture->texture.texels.texImageParam = (s << 20) | (t << 23) | (texfmt << 26) | (plt0 << 29) | (flipRepeat << 16);
	texture->texture.texels.height = 8 << t;
	texture->texture.texels.texel = (unsigned char *) calloc(sizTex, 1);
	memcpy(texture->texture.texels.texel, buffer + 0x20, sizTex);

	if (texfmt == CT_4x4) {
		texture->texture.texels.cmp = (uint16_t *) calloc(sizIdx, 1);
		memcpy(texture->texture.texels.cmp, buffer + ofsIdx, sizIdx);
	}

	if (texfmt != CT_DIRECT) {
		texture->texture.palette.nColors = sizPlt / 2;
		texture->texture.palette.pal = (COLOR *) calloc(sizPlt / 2, sizeof(COLOR));
		memcpy(texture->texture.palette.pal, buffer + ofsPlt, sizPlt);
	}

	return OBJ_STATUS_SUCCESS;
}

static int TxReadGrf(TextureObject *texture, const unsigned char *buffer, unsigned int size) {
	unsigned int headerSize;
	unsigned char *hdr = GrfGetHeader(buffer, size, &headerSize);

	int grfType = GrfIsValid(buffer, size);
	int gfxFmt = *(const uint16_t *) (hdr + (grfType == GRF_TYPE_GRF_20 ? 0x02 : 0x00));

	//graphics format to texture format
	int texFmt = 0;
	switch (gfxFmt) {
		case 0x02: texFmt = CT_4COLOR; break;
		case 0x04: texFmt = CT_16COLOR; break;
		case 0x08: texFmt = CT_256COLOR; break;
		case 0x10: texFmt = CT_DIRECT; break;
		case 0x80: texFmt = CT_A3I5;  break;
		case 0x81: texFmt = CT_A5I3;  break;
		case 0x82: texFmt = CT_4x4; break;
	}

	unsigned int palSize, gfxSize, pidxSize;
	unsigned char *gfx = GrfReadBlockUncompressed(buffer, size, "GFX ", &gfxSize);
	unsigned char *pal = GrfReadBlockUncompressed(buffer, size, "PAL ", &palSize);
	unsigned char *idx = GrfReadBlockUncompressed(buffer, size, "PIDX", &pidxSize);

	int plt0 = 0;
	int texW = (*(const uint32_t *) (hdr + (grfType == GRF_TYPE_GRF_20 ? 0x10 : 0x0C)));
	int texH = (*(const uint32_t *) (hdr + (grfType == GRF_TYPE_GRF_20 ? 0x14 : 0x10)));
	if (grfType != GRF_TYPE_GRF_10) plt0 = (*(const uint16_t *) (hdr + 0x0E)) & 1; // CHECKME: tentative

	int s = ilog2(texW) - 3;
	int t = ilog2(texH) - 3;
	if ((8 << t) < texH) t++;

	texture->texture.texels.height = texH;
	texture->texture.texels.texImageParam = (s << 20) | (t << 23) | (texFmt << 26) | (plt0 << 29);

	texture->texture.texels.texel = gfx;
	texture->texture.texels.cmp = (uint16_t *) idx;

	if (pal != NULL) {
		texture->texture.palette.pal = (COLOR *) pal;
		texture->texture.palette.nColors = palSize / 2;
	}

	if ((8 << t) > texH) {
		//pad internal texture buffer
		unsigned int texelSize = TxGetTexelSize(8 << s, 8 << t, texture->texture.texels.texImageParam);
		unsigned int indexSize = texelSize / 2;

		texture->texture.texels.texel = realloc(texture->texture.texels.texel, texelSize);
		memset(texture->texture.texels.texel + gfxSize, 0, texelSize - gfxSize);

		if (texFmt == CT_4x4) {
			texture->texture.texels.cmp = realloc(texture->texture.texels.cmp, indexSize);
			memset(((unsigned char *) texture->texture.texels.cmp) + pidxSize, 0, indexSize - pidxSize);
		}
	}

	return OBJ_STATUS_SUCCESS;
}

int TxRead(TextureObject *texture, const unsigned char *buffer, unsigned int size) {
	switch (texture->header.format) {
		case TEXTURE_TYPE_NNSTGA:
			return TxReadNnsTga(texture, buffer, size);
		case TEXTURE_TYPE_SPT:
			return TxReadSpt(texture, buffer, size);
		case TEXTURE_TYPE_ISTUDIO:
			return TxReadIStudio(texture, buffer, size);
		case TEXTURE_TYPE_TDS:
			return TxReadTds(texture, buffer, size);
		case TEXTURE_TYPE_NTGA:
			return TxReadNtga(texture, buffer, size);
		case TEXTURE_TYPE_TOLOVERU:
			return TxReadToLoveRu(texture, buffer, size);
		case TEXTURE_TYPE_GRF:
			return TxReadGrf(texture, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}


#include "gdip.h"

extern const char *NpGetVersion(void);

static void TxiNnsTgaWriteSection(BSTREAM *stream, const char *section, const void *data, int size) {
	//prepare and write
	unsigned char header[0xC] = { 0 };
	uint32_t dataSize = size == -1 ? strlen((const char *) data) : size;
	memcpy(header, section, 8);
	*(uint32_t *) (header + 0x8) = dataSize + sizeof(header);

	bstreamWrite(stream, header, sizeof(header));
	if (dataSize) {
		bstreamWrite(stream, (void *) data, dataSize);
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

static void TxiNnsTgaWritePixels(BSTREAM *stream, COLOR32 *rawPx, int width, int height, int depth) {
	if (depth == 32) {
		//write as-is
		bstreamWrite(stream, rawPx, width * height * sizeof(COLOR32));
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
		bstreamWrite(stream, buffer, width * height * 3);
		free(buffer);
		return;
	}
	//bad
}

int TxWriteNnsTga(TextureObject *texture, BSTREAM *stream) {
	TEXELS *texels = &texture->texture.texels;
	PALETTE *palette = &texture->texture.palette;

	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);

	COLOR32 *pixels = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	TxRender(pixels, texels, palette);
	ImgSwapRedBlue(pixels, width, height);
	ImgFlip(pixels, width, height, 0, 1);

	int depth = imageHasTransparent(pixels, width * height) ? 32 : 24;

	uint8_t header[] = { 0x14, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 8,
		'N', 'N', 'S', '_', 'T', 'g', 'a', ' ', 'V', 'e', 'r', ' ', '1', '.', '0', 0, 0, 0, 0, 0 };
	*(uint16_t *) (header + 0x0C) = width;
	*(uint16_t *) (header + 0x0E) = height;
	*(uint8_t *) (header + 0x10) = depth;
	*(uint32_t *) (header + 0x22) = sizeof(header) + width * height * (depth / 8);
	bstreamWrite(stream, header, sizeof(header));
	TxiNnsTgaWritePixels(stream, pixels, width, height, depth);

	//format
	const char *fstr = TxNameFromTexFormat(FORMAT(texels->texImageParam));
	TxiNnsTgaWriteSection(stream, "nns_frmt", fstr, -1);

	//texels
	int txelLength = TxGetTexelSize(width, height, texels->texImageParam);
	TxiNnsTgaWriteSection(stream, "nns_txel", texels->texel, txelLength);

	//write 4x4 if applicable
	if (FORMAT(texels->texImageParam) == CT_4x4) {
		int pidxLength = txelLength / 2;
		TxiNnsTgaWriteSection(stream, "nns_pidx", texels->cmp, pidxLength);
	}

	//palette (if applicable)
	if (FORMAT(texels->texImageParam) != CT_DIRECT) {
		TxiNnsTgaWriteSection(stream, "nns_pnam", palette->name, strlen(palette->name));

		int nColors = palette->nColors;
		if (FORMAT(texels->texImageParam) == CT_4COLOR && nColors > 4) nColors = 4;
		if (nColors == 4 || (nColors % 8) == 0) {
			//valid size
			TxiNnsTgaWriteSection(stream, "nns_pcol", palette->pal, nColors * sizeof(COLOR));
		} else {
			//needs padding
			int outPaletteSize = (nColors + 7) / 8 * 8;
			if (nColors < 4) outPaletteSize = (nColors + 3) / 4 * 4;

			COLOR *padded = (COLOR *) calloc(outPaletteSize, sizeof(COLOR));
			memcpy(padded, palette->pal, palette->nColors * sizeof(COLOR));
			TxiNnsTgaWriteSection(stream, "nns_pcol", padded, outPaletteSize * sizeof(COLOR));
			free(padded);
		}
	}

	//NitroPaint generator signature
	TxiNnsTgaWriteSection(stream, "nns_gnam", "NitroPaint", -1);
	TxiNnsTgaWriteSection(stream, "nns_gver", NpGetVersion(), -1);

	//dummy imagestudio data
	TxiNnsTgaWriteSection(stream, "nns_imst", NULL, 0);

	//if c0xp
	if (COL0TRANS(texels->texImageParam)) {
		TxiNnsTgaWriteSection(stream, "nns_c0xp", NULL, 0);
	}

	//write end
	TxiNnsTgaWriteSection(stream, "nns_endb", NULL, 0);
	free(pixels);

	return 0;
}

int TxWriteIStudio(TextureObject *texture, BSTREAM *stream) {
	unsigned char paltHeader[] = { 0, 0, 0, 0 };
	unsigned char imgeHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	int texImageParam = texture->texture.texels.texImageParam;
	int format = FORMAT(texImageParam);
	int width = TEXW(texImageParam);
	int height = texture->texture.texels.height;
	int texelSize = TxGetTexelSize(width, height, texImageParam);
	int nColors = texture->texture.palette.nColors;

	unsigned int imgeSize = texelSize + (format == CT_4x4 ? (texelSize / 2) : 0);
	*(uint32_t *) (paltHeader + 0x0) = nColors;
	*(uint8_t *) (imgeHeader + 0x0) = format;
	*(uint8_t *) (imgeHeader + 0x1) = (texImageParam >> 20) & 7;
	*(uint8_t *) (imgeHeader + 0x2) = (texImageParam >> 23) & 7;
	*(uint16_t *) (imgeHeader + 0x4) = width;
	*(uint16_t *) (imgeHeader + 0x6) = height;
	*(uint32_t *) (imgeHeader + 0x8) = imgeSize;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NTTX", 1, 0, NNS_TYPE_G2D, NNS_SIG_BE);
	
	if (format != CT_DIRECT) {
		NnsStreamStartBlock(&nnsStream, "PALT");
		NnsStreamWrite(&nnsStream, paltHeader, sizeof(paltHeader));
		NnsStreamWrite(&nnsStream, texture->texture.palette.pal, nColors * sizeof(COLOR));
		NnsStreamEndBlock(&nnsStream);
	}

	NnsStreamStartBlock(&nnsStream, "IMGE");
	NnsStreamWrite(&nnsStream, imgeHeader, sizeof(imgeHeader));
	NnsStreamWrite(&nnsStream, texture->texture.texels.texel, texelSize);
	if (format == CT_4x4) {
		NnsStreamWrite(&nnsStream, texture->texture.texels.cmp, texelSize / 2);
	}
	NnsStreamEndBlock(&nnsStream);

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

int TxWriteTds(TextureObject *texture, BSTREAM *stream) {
	unsigned char header[0x24] = { 's', 'd', 't', '.', 1, 0, 0, 0 };

	int texImageParam = texture->texture.texels.texImageParam;
	int format = FORMAT(texImageParam);
	int width = TEXW(texImageParam);
	int height = texture->texture.texels.height;
	int nColors = texture->texture.palette.nColors;

	unsigned int texelSize = TxGetTexelSize(width, height, texImageParam);
	unsigned int totalTexelSize = texelSize;
	if (format == CT_4x4) totalTexelSize += texelSize / 2;

	*(uint8_t *) (header + 0x08) = format;
	*(uint8_t *) (header + 0x09) = (texImageParam >> 20) & 7;
	*(uint8_t *) (header + 0x0A) = (texImageParam >> 23) & 7;
	*(uint32_t *) (header + 0x0C) = sizeof(header); //texture offset
	*(uint32_t *) (header + 0x10) = totalTexelSize;
	*(uint32_t *) (header + 0x14) = sizeof(header) + totalTexelSize;
	*(uint32_t *) (header + 0x18) = nColors * sizeof(COLOR);
	*(uint32_t *) (header + 0x1C) = width;
	*(uint32_t *) (header + 0x20) = height;

	bstreamWrite(stream, header, sizeof(header));
	bstreamWrite(stream, texture->texture.texels.texel, texelSize);
	if (format == CT_4x4) bstreamWrite(stream, texture->texture.texels.cmp, texelSize / 2);
	if (format != CT_DIRECT) bstreamWrite(stream, texture->texture.palette.pal, nColors * sizeof(COLOR));
	return 0;
}

int TxWriteNtga(TextureObject *texture, BSTREAM *stream) {
	unsigned char header[0x30] = { 'N', 'T', 'G', 'A' };

	int texImageParam = texture->texture.texels.texImageParam;
	int fmt = FORMAT(texImageParam);
	int width = TEXW(texImageParam);
	int height = TEXH(texImageParam);

	int texelSize = TxGetTexelSize(width, height, texImageParam);
	int paletteSize = texture->texture.palette.nColors * sizeof(COLOR);

	*(uint32_t *) (header + 0x04) = fmt;
	*(uint32_t *) (header + 0x08) = (texImageParam >> 20) & 7;
	*(uint32_t *) (header + 0x0C) = (texImageParam >> 23) & 7;
	*(uint16_t *) (header + 0x14) = width;
	*(uint16_t *) (header + 0x16) = texture->texture.texels.height;
	*(uint32_t *) (header + 0x18) = sizeof(header);
	*(uint32_t *) (header + 0x1C) = texelSize;
	if (fmt == CT_4x4) {
		*(uint32_t *) (header + 0x20) = sizeof(header) + texelSize;
		*(uint32_t *) (header + 0x24) = texelSize / 2;
	}
	if (fmt != CT_DIRECT) {
		*(uint32_t *) (header + 0x28) = sizeof(header) + texelSize + (fmt == CT_4x4 ? (texelSize / 2) : 0);
		*(uint32_t *) (header + 0x2C) = paletteSize;
	}

	bstreamWrite(stream, header, sizeof(header));
	bstreamWrite(stream, texture->texture.texels.texel, texelSize);
	if (fmt == CT_4x4) {
		bstreamWrite(stream, texture->texture.texels.cmp, texelSize / 2);
	}
	if (fmt != CT_DIRECT) {
		bstreamWrite(stream, texture->texture.palette.pal, paletteSize);
	}

	return 0;
}

static int TxWriteToLoveRu(TextureObject *texture, BSTREAM *stream) {
	unsigned char header[0x28] = { 't', 'e', 'x', 0 };

	int texImageParam = texture->texture.texels.texImageParam;
	int fmt = FORMAT(texImageParam);
	unsigned int txelSize = TxGetTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);

	unsigned int offsPltt = 0, sizePltt = 0, offsTexImage = 0, sizeTexImage = 0, offsPlttIdx = 0, sizePlttIdx = 0;
	sizeTexImage = txelSize;
	if (FORMAT(texImageParam) != CT_DIRECT) {
		sizePltt = texture->texture.palette.nColors * 2;
		offsPltt = sizeof(header);
		offsTexImage = offsPltt + sizePltt;
		if (FORMAT(texImageParam) == CT_4x4) {
			sizePlttIdx = txelSize / 2;
			offsPlttIdx = offsTexImage + sizeTexImage;
		}
	} else {
		offsTexImage = sizeof(header);
	}

	*(uint16_t *) (header + 0x06) = fmt;
	*(uint16_t *) (header + 0x08) = (texImageParam >> 20) & 0x7;
	*(uint16_t *) (header + 0x0A) = (texImageParam >> 23) & 0x7;
	*(uint32_t *) (header + 0x0C) = offsPltt;
	*(uint32_t *) (header + 0x10) = sizePltt;
	*(uint32_t *) (header + 0x14) = (fmt == CT_DIRECT || fmt == CT_4x4 || fmt == CT_256COLOR) ? 1 : (texture->texture.palette.nColors);
	*(uint32_t *) (header + 0x18) = offsTexImage;
	*(uint32_t *) (header + 0x1C) = sizeTexImage;
	*(uint32_t *) (header + 0x20) = offsPlttIdx;
	*(uint32_t *) (header + 0x24) = sizePlttIdx;
	bstreamWrite(stream, header, sizeof(header));
	
	if (FORMAT(texImageParam) != CT_DIRECT) {
		bstreamWrite(stream, texture->texture.palette.pal, texture->texture.palette.nColors * 2);
	}

	bstreamWrite(stream, texture->texture.texels.texel, txelSize);
	if (FORMAT(texImageParam) == CT_4x4) {
		bstreamWrite(stream, texture->texture.texels.cmp, txelSize / 2);
	}
	return 0;
}

static int TxWriteSpt(TextureObject *texture, BSTREAM *stream) {
	unsigned int texImageParam = texture->texture.texels.texImageParam;
	unsigned int s = (texImageParam >> 20) & 0x7;
	unsigned int t = (texImageParam >> 23) & 0x7;
	unsigned int c0xp = COL0TRANS(texImageParam);
	unsigned int flipRepeat = (texImageParam >> 16) & 0xF;
	int texfmt = FORMAT(texImageParam);

	unsigned int texSize = TxGetTexelSize(8 << s, 8 << t, texImageParam);
	unsigned int pltSize = texture->texture.palette.nColors * sizeof(COLOR);
	unsigned int idxSize = 0;
	if (texfmt == CT_4x4) {
		idxSize = texSize / 2;
	}

	unsigned char header[0x20] = { ' ', 'T', 'P', 'S' };
	unsigned int ofsTex = sizeof(header);
	unsigned int ofsPlt = ofsTex + texSize;
	unsigned int ofsIdx = ofsPlt + pltSize;

	//file header
	*(uint32_t *) (header + 0x04) = texfmt | (s << 4) | (t << 8) | (flipRepeat << 12) | (c0xp << 16);
	*(uint32_t *) (header + 0x08) = texSize;
	*(uint32_t *) (header + 0x0C) = ofsPlt;
	*(uint32_t *) (header + 0x10) = pltSize;
	*(uint32_t *) (header + 0x14) = ofsIdx;
	*(uint32_t *) (header + 0x18) = idxSize;
	*(uint32_t *) (header + 0x1C) = texSize + idxSize + pltSize + sizeof(header);
	bstreamWrite(stream, header, sizeof(header));

	//data
	bstreamWrite(stream, texture->texture.texels.texel, texSize);
	if (texfmt != CT_DIRECT) bstreamWrite(stream, texture->texture.palette.pal, pltSize);
	if (texfmt == CT_4x4) bstreamWrite(stream, texture->texture.texels.cmp, idxSize);

	return OBJ_STATUS_SUCCESS;
}

static int TxWriteGRF(TextureObject *texture, BSTREAM *stream) {
	int texImageParam = texture->texture.texels.texImageParam;
	int fmt = FORMAT(texImageParam);

	int gfxAttr = 0, tileWidth = 0, tileHeight = 0, flags = 0;
	switch (fmt) {
		case CT_4COLOR  : gfxAttr = 0x02; break;
		case CT_16COLOR : gfxAttr = 0x04; break;
		case CT_256COLOR: gfxAttr = 0x08; break;
		case CT_DIRECT  : gfxAttr = 0x10; break;
		case CT_A3I5    : gfxAttr = 0x80; break;
		case CT_A5I3    : gfxAttr = 0x81; break;
		case CT_4x4     : gfxAttr = 0x82; break;
	}

	int nColors = 0;
	if (fmt != CT_DIRECT) nColors = texture->texture.palette.nColors;

	if (fmt == CT_4x4) {
		tileWidth = 4;
		tileHeight = 4;
	}

	if (fmt == CT_4COLOR || fmt == CT_16COLOR || fmt == CT_256COLOR) {
		if (COL0TRANS(texImageParam)) flags |= 0x0001;
	}

	unsigned int texelSize = TxGetTexelSize(TEXW(texImageParam), texture->texture.texels.height, texImageParam);

	unsigned char hdr[0x18] = { 0 };
	*(uint16_t *) (hdr + 0x00) = 2; // version 2
	*(uint16_t *) (hdr + 0x02) = gfxAttr;
	*(uint16_t *) (hdr + 0x08) = nColors;
	*(uint8_t *) (hdr + 0x0A) = tileWidth;
	*(uint8_t *) (hdr + 0x0B) = tileHeight;
	*(uint16_t *) (hdr + 0x0E) = flags;
	*(uint32_t *) (hdr + 0x10) = TEXW(texImageParam);
	*(uint32_t *) (hdr + 0x14) = texture->texture.texels.height;

	BSTREAM grfStream;
	GrfStreamCreate(&grfStream);

	GrfStreamWriteBlock(&grfStream, "HDRX", hdr, sizeof(hdr));

	if (fmt != CT_DIRECT) {
		GrfStreamWriteBlockCompressedOptimal(&grfStream, "PAL ", texture->texture.palette.pal, nColors * sizeof(COLOR));
	}

	GrfStreamWriteBlockCompressedOptimal(&grfStream, "GFX ", texture->texture.texels.texel, texelSize);

	if (fmt == CT_4x4) {
		GrfStreamWriteBlockCompressedOptimal(&grfStream, "PIDX", texture->texture.texels.cmp, texelSize / 2);
	}

	GrfStreamFinalize(&grfStream);
	GrfStreamFlushOut(&grfStream, stream);
	GrfStreamFree(&grfStream);

	return OBJ_STATUS_SUCCESS;
}

int TxWrite(TextureObject *texture, BSTREAM *stream) {
	switch (texture->header.format) {
		case TEXTURE_TYPE_NNSTGA:
			return TxWriteNnsTga(texture, stream);
		case TEXTURE_TYPE_ISTUDIO:
			return TxWriteIStudio(texture, stream);
		case TEXTURE_TYPE_SPT:
			return TxWriteSpt(texture, stream);
		case TEXTURE_TYPE_TDS:
			return TxWriteTds(texture, stream);
		case TEXTURE_TYPE_NTGA:
			return TxWriteNtga(texture, stream);
		case TEXTURE_TYPE_TOLOVERU:
			return TxWriteToLoveRu(texture, stream);
		case TEXTURE_TYPE_GRF:
			return TxWriteGRF(texture, stream);
	}
	return 1;
}
