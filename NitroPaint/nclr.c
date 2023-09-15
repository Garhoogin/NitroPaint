#include <stdio.h>
#include <stdint.h>

#include "nclr.h"
#include "g2dfile.h"

LPCWSTR paletteFormatNames[] = { L"Invalid", L"NCLR", L"Hudson", L"Binary", L"NTFP", L"NCL", L"5PL", L"5PC", NULL };

void nclrFree(OBJECT_HEADER *header) {
	NCLR *nclr = (NCLR *) header;
	if (nclr->colors != NULL) free(nclr->colors);
	nclr->colors = NULL;
	if (nclr->idxTable != NULL) free(nclr->idxTable);
	nclr->idxTable = NULL;
	if (nclr->comment != NULL) free(nclr->comment);
	nclr->comment = NULL;

	COMBO2D *combo2d = nclr->combo2d;
	if (nclr->combo2d != NULL) {
		nclr->combo2d->nclr = NULL;
		if (combo2d->nclr == NULL && combo2d->ncgr == NULL && combo2d->nscr == NULL) {
			combo2dFree(combo2d);
			free(combo2d);
		}
	}
	nclr->combo2d = NULL;
}

void nclrInit(NCLR *nclr, int format) {
	nclr->header.size = sizeof(NCLR);
	fileInitCommon((OBJECT_HEADER *) nclr, FILE_TYPE_PALETTE, format);
	nclr->header.dispose = nclrFree;
	nclr->combo2d = NULL;
}

int nclrIsValidHudson(unsigned char *lpFile, unsigned int size) {
	if (size < 4) return 0;
	if (*lpFile == 0x10) return 0;
	int dataLength = *(uint16_t *) lpFile;
	int nColors = *(uint16_t *) (lpFile + 2);
	if (nColors == 0) return 0;
	if (dataLength & 1) return 0;
	if (dataLength + 4 != size) return 0;
	if (nColors * 2 + 4 != size) return 0;

	if (nColors & 0xF) return 0;
	if (nColors > 256) {
		if (nColors & 0xFF) return 0;
	}

	COLOR *data = (COLOR *) (lpFile + 4);
	for (int i = 0; i < nColors; i++) {
		COLOR w = data[i];
		if (w & 0x8000) return 0;
	}
	return 1;
}

int nclrIsValidBin(unsigned char *lpFile, unsigned int size) {
	if (size < 16) return 0;
	if (size & 1) return 0;
	if (size > 16 * 256 * 2) return 0;
	int nColors = size >> 1;
	if (nColors & 0xF) return 0;
	if (nColors > 256) {
		if (nColors & 0xFF) return 0;
	}

	COLOR *data = (COLOR *) lpFile;
	for (int i = 0; i < nColors; i++) {
		COLOR w = data[i];
		//if (w & 0x8000) return 0;
	}
	return 1;
}

int nclrIsValidNtfp(unsigned char *lpFile, unsigned int size) {
	if (size & 1) return 0;
	for (unsigned int i = 0; i < size >> 1; i++) {
		COLOR c = *(COLOR *) (lpFile + i * 2);
		//if (c & 0x8000) return 0;
	}
	return 1;
}

int nclrIsValidNclr(unsigned char *lpFile, unsigned int size) {
	if (!g2dIsValid(lpFile, size)) return 0;
	uint32_t magic = *(uint32_t *) lpFile;
	if (magic != 'NCLR' && magic != 'RLCN' && magic != 'NCPR' && magic != 'RPCN') return 0;

	char *pltt = g2dGetSectionByMagic(lpFile, size, 'PLTT');
	char *ttlp = g2dGetSectionByMagic(lpFile, size, 'TTLP');
	if (pltt == NULL && ttlp == NULL) return 0;
	return 1;
}

int nclrIsValidNcl(unsigned char *lpFile, unsigned int size) {
	if (!g2dIsValid(lpFile, size)) return 0;
	uint32_t magic = *(uint32_t *) lpFile;
	if (magic != 'NCCL' && magic != 'LCCN') return 0;

	char *palt = g2dGetSectionByMagic(lpFile, size, 'PALT');
	char *tlap = g2dGetSectionByMagic(lpFile, size, 'TLAP');
	if (palt == NULL && tlap == NULL) return 0;
	return 1;
}

int nclrIsValidIStudio(unsigned char *lpFile, unsigned int size) {
	if (!g2dIsValid(lpFile, size)) return 0;
	uint32_t magic = *(uint32_t *) lpFile;
	if (magic != 'NTPL' && magic != 'LPTN') return 0;

	unsigned char *palt = g2dGetSectionByMagic(lpFile, size, 'PALT');
	unsigned char *tlap = g2dGetSectionByMagic(lpFile, size, 'TLAP');
	if (palt == NULL && tlap == NULL) return 0;
	return 1;
}

int nclrIsValidIStudioCompressed(unsigned char *lpFile, unsigned int size) {
	if (!g2dIsValid(lpFile, size)) return 0;
	uint32_t magic = *(uint32_t *) lpFile;
	if (magic != 'NTPC' && magic != 'CPTN') return 0;

	unsigned char *palt = g2dGetSectionByMagic(lpFile, size, 'PALT');
	unsigned char *tlap = g2dGetSectionByMagic(lpFile, size, 'TLAP');
	if (palt == NULL && tlap == NULL) return 0;
	return 1;
}

int nclrIsValid(unsigned char *lpFile, unsigned int size) {
	if (nclrIsValidNclr(lpFile, size)) return NCLR_TYPE_NCLR;
	if (nclrIsValidNcl(lpFile, size)) return NCLR_TYPE_NC;
	if (nclrIsValidHudson(lpFile, size)) return NCLR_TYPE_HUDSON;
	if (nclrIsValidBin(lpFile, size)) return NCLR_TYPE_BIN;
	if (nclrIsValidNtfp(lpFile, size)) return NCLR_TYPE_NTFP;
	if (nclrIsValidIStudio(lpFile, size)) return NCLR_TYPE_ISTUDIO;
	if (nclrIsValidIStudioCompressed(lpFile, size)) return NCLR_TYPE_ISTUDIOC;
	return NCLR_TYPE_INVALID;
}

int hudsonPaletteRead(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	if (size < 4) return 1;

	int dataLength = *(uint16_t *) buffer;
	int nColors = *(uint16_t *) (buffer + 2);

	nclrInit(nclr, NCLR_TYPE_HUDSON);
	nclr->nColors = nColors;
	nclr->totalSize = nclr->nColors * sizeof(COLOR);
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	memcpy(nclr->colors, buffer + 4, nColors * 2);
	return 0;
}

int binPaletteRead(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	if (!nclrIsValidNtfp(buffer, size)) return 1; //this function is being reused for NTFP as well
	
	int nColors = size >> 1;

	nclrInit(nclr, nclrIsValidBin(buffer, size) ? NCLR_TYPE_BIN : NCLR_TYPE_NTFP);
	nclr->nColors = nColors;
	nclr->totalSize = nclr->nColors * sizeof(COLOR);
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	memcpy(nclr->colors, buffer, nColors * 2);
	return 0;
}

int comboReadPalette(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	int type = combo2dIsValid(buffer, size);

	nclrInit(nclr, NCLR_TYPE_COMBO);
	switch (type) {
		case COMBO2D_TYPE_TIMEACE:
			nclr->nColors = 256;
			nclr->extPalette = 0;
			nclr->idxTable = NULL;
			nclr->nBits = 4;
			nclr->nPalettes = 0;
			nclr->totalSize = 256 * sizeof(COLOR);
			nclr->colors = (COLOR *) calloc(256, sizeof(COLOR));
			memcpy(nclr->colors, buffer + 4, 512);
			break;
		case COMBO2D_TYPE_BANNER:
			nclr->nColors = 16;
			nclr->extPalette = 0;
			nclr->idxTable = NULL;
			nclr->nBits = 4;
			nclr->nPalettes = 0;
			nclr->totalSize = 16 * sizeof(COLOR);
			nclr->colors = (COLOR *) calloc(16, sizeof(COLOR));
			memcpy(nclr->colors, buffer + 0x220, 32);
			break;
		case COMBO2D_TYPE_5BG:
		{
			char *palt = g2dGetSectionByMagic(buffer, size, 'PALT');
			if (palt == NULL) palt = g2dGetSectionByMagic(buffer, size, 'TLAP');

			int nColors = *(uint32_t *) (palt + 0x08);
			nclr->nColors = nColors;
			nclr->extPalette = 0;
			nclr->idxTable = NULL;
			nclr->nBits = 4;
			nclr->nPalettes = 0;
			nclr->totalSize = nColors * sizeof(COLOR);
			nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
			memcpy(nclr->colors, palt + 0xC, nColors * sizeof(COLOR));
			break;
		}
	}

	return 0;
}

int ncPaletteRead(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	if (!nclrIsValidNcl(buffer, size)) return 1;

	nclrInit(nclr, NCLR_TYPE_NC);

	char *palt = g2dGetSectionByMagic(buffer, size, 'PALT');
	if (palt == NULL) palt = g2dGetSectionByMagic(buffer, size, 'TLAP');
	char *cmnt = g2dGetSectionByMagic(buffer, size, 'CMNT');
	if (cmnt == NULL) cmnt = g2dGetSectionByMagic(buffer, size, 'TNMC');

	nclr->nPalettes = *(uint32_t *) (palt + 0xC);
	nclr->nColors = nclr->nPalettes * *(uint32_t *) (palt + 0x8);
	nclr->extPalette = nclr->nColors > 256;
	nclr->nBits = *(uint32_t *) (palt + 0x8) > 16 ? 8 : 4;
	nclr->totalSize = nclr->nColors * sizeof(COLOR);
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	memcpy(nclr->colors, palt + 0x10, nclr->nColors * 2);
	if (cmnt != NULL) {
		int cmntLength = (*(uint32_t *) (cmnt + 4)) - 4;
		nclr->comment = (char *) calloc(cmntLength, 1);
		memcpy(nclr->comment, cmnt + 8, cmntLength);
	}

	return 0;
}

void istudioCommonReadColors(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	char *palt = g2dGetSectionByMagic(buffer, size, 'PALT');
	if (palt == NULL) palt = g2dGetSectionByMagic(buffer, size, 'TLAP');

	nclr->nColors = *(uint32_t *) (palt + 0x8);
	nclr->nPalettes = (nclr->nColors + 15) / 16;
	nclr->extPalette = nclr->nColors > 256;
	nclr->nBits = 4;
	nclr->totalSize = nclr->nColors;
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	memcpy(nclr->colors, palt + 0xC, nclr->nColors * 2);
}

int istudioPaletteRead(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	if (!nclrIsValidIStudio(buffer, size)) return 1;

	nclrInit(nclr, NCLR_TYPE_ISTUDIO);
	istudioCommonReadColors(nclr, buffer, size);
	return 0;
}

int istudioCompressedPaletteRead(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	if (!nclrIsValidIStudioCompressed(buffer, size)) return 1;

	nclrInit(nclr, NCLR_TYPE_ISTUDIOC);
	istudioCommonReadColors(nclr, buffer, size);
	return 0;
}

int nclrRead(NCLR *nclr, unsigned char *buffer, unsigned int size) {
	if (!nclrIsValidNclr(buffer, size)) {
		if (nclrIsValidNcl(buffer, size)) return ncPaletteRead(nclr, buffer, size);
		if (nclrIsValidIStudio(buffer, size)) return istudioPaletteRead(nclr, buffer, size);
		if (nclrIsValidIStudioCompressed(buffer, size)) return istudioCompressedPaletteRead(nclr, buffer, size);
		if (nclrIsValidHudson(buffer, size)) return hudsonPaletteRead(nclr, buffer, size);
		if (nclrIsValidBin(buffer, size)) return binPaletteRead(nclr, buffer, size);
		if (nclrIsValidNtfp(buffer, size)) return binPaletteRead(nclr, buffer, size);
		if (combo2dIsValid(buffer, size)) return comboReadPalette(nclr, buffer, size);
	}
	char *pltt = g2dGetSectionByMagic(buffer, size, 'PLTT');
	char *pcmp = g2dGetSectionByMagic(buffer, size, 'PCMP');
	if (pltt == NULL) return 1;

	int sectionSize = *(int *) (pltt + 0x4);
	if (g2dIsOld(buffer, size)) sectionSize += 8;

	int bits = *(int *) (pltt + 0x8);
	bits = 1 << (bits - 1);
	int dataSize = *(int *) (pltt + 0x10);
	int dataOffset = 8 + *(int *) (pltt + 0x14);
	int nColors = (sectionSize - dataOffset) >> 1;

	nclrInit(nclr, NCLR_TYPE_NCLR);
	nclr->nColors = nColors;
	nclr->nBits = bits;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	nclr->extPalette = *(int *) (pltt + 0xC);
	nclr->totalSize = *(int *) (pltt + 0x10);
	nclr->nPalettes = 0;
	nclr->idxTable = NULL;
	
	if (pcmp != NULL) {
		nclr->nPalettes = *(unsigned short *) (pcmp + 8);
		nclr->idxTable = (short *) calloc(nclr->nPalettes, 2);
		memcpy(nclr->idxTable, pcmp + 8 + *(unsigned int *) (pcmp + 0xC), nclr->nPalettes * 2);
	}
	memcpy(nclr->colors, pltt + dataOffset, nColors * 2);
	return 0;
}

int nclrReadFile(NCLR *nclr, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nclr, (OBJECT_READER) nclrRead);
}

int nclrSaveNclr(NCLR *nclr, BSTREAM *stream) {
	uint8_t fileHeader[] = { 'R', 'L', 'C', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
	uint8_t ttlpHeader[] = { 'T', 'T', 'L', 'P', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 };
	uint8_t pmcpHeader[] = { 'P', 'M', 'C', 'P', 0x12, 0, 0, 0, 0, 0, 0xEF, 0xBE, 0x8, 0, 0, 0 };

	int sectionSize = 0x18 + (nclr->nColors << 1);
	int pcmpSize = (nclr->nPalettes && nclr->idxTable != NULL) ? (0x10 + 2 * nclr->nPalettes) : 0;
	int nSections = 1 + (pcmpSize != 0);
	*(int *) (ttlpHeader + 0x4) = sectionSize;
	if (nclr->nBits == 8) *(int *) (ttlpHeader + 0x8) = 4;
	else *(int *) (ttlpHeader + 0x8) = 3;
	*(int *) (ttlpHeader + 0x10) = nclr->totalSize;
	*(int *) (ttlpHeader + 0xC) = nclr->extPalette;

	*(int *) (fileHeader + 0x8) = sectionSize + 0x10 + pcmpSize;
	*(short *) (fileHeader + 0xE) = nSections;

	*(short *) (pmcpHeader + 8) = nclr->nPalettes;
	*(int *) (pmcpHeader + 4) = 0x10 + 2 * nclr->nPalettes;

	bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	bstreamWrite(stream, ttlpHeader, sizeof(ttlpHeader));
	bstreamWrite(stream, nclr->colors, nclr->nColors * 2);
	if (pcmpSize) {
		bstreamWrite(stream, pmcpHeader, sizeof(pmcpHeader));
		bstreamWrite(stream, nclr->idxTable, nclr->nPalettes * 2);
	}
	return 0;
}

int nclrSaveNcl(NCLR *nclr, BSTREAM *stream) {
	uint8_t fileHeader[] = { 'N', 'C', 'C', 'L', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
	uint8_t paltHeader[] = { 'P', 'A', 'L', 'T', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t cmntHeader[] = { 'C', 'M', 'N', 'T', 0xC, 0, 0, 0 };

	int paltSize = nclr->nColors * 2 + sizeof(paltHeader);
	int commentLength = nclr->comment == NULL ? 0 : ((strlen(nclr->comment) + 4) & ~3);
	int cmntSize = commentLength ? (commentLength + sizeof(cmntHeader)) : 0;
	int fileSize = paltSize + cmntSize + sizeof(fileHeader);

	*(uint32_t *) (fileHeader + 8) = fileSize;
	*(uint16_t *) (fileHeader + 14) = 1 + (cmntSize != 0);
	*(uint32_t *) (paltHeader + 4) = paltSize;
	*(uint32_t *) (paltHeader + 8) = nclr->nColors / nclr->nPalettes;
	*(uint32_t *) (paltHeader + 12) = nclr->nPalettes;
	*(uint32_t *) (cmntHeader + 4) = cmntSize;

	bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	bstreamWrite(stream, paltHeader, sizeof(paltHeader));
	bstreamWrite(stream, nclr->colors, nclr->nColors * 2);
	if (cmntSize != 0) {
		uint32_t padding = 0;
		bstreamWrite(stream, cmntHeader, sizeof(cmntHeader));
		bstreamWrite(stream, nclr->comment, strlen(nclr->comment) + 1);
		bstreamWrite(stream, &padding, commentLength - (strlen(nclr->comment) + 1));
	}
	return 0;
}

int nclrSaveHudson(NCLR *nclr, BSTREAM *stream) {
	uint16_t fileHeader[] = { 0, 0 };
	fileHeader[0] = nclr->nColors * 2;
	fileHeader[1] = nclr->nColors;

	bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	bstreamWrite(stream, nclr->colors, nclr->nColors * 2);
	return 0;
}

int nclrSaveBin(NCLR *nclr, BSTREAM *stream) {
	bstreamWrite(stream, nclr->colors, nclr->nColors * 2);
	return 0;
}

int nclrSaveCombo(NCLR *nclr, BSTREAM *stream) {
	return combo2dWrite(nclr->combo2d, stream);
}

int nclrSaveIStudio(NCLR *nclr, BSTREAM *stream) {
	uint8_t fileHeader[] = { 'N', 'T', 'P', 'L', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 1, 0 };
	uint8_t paltHeader[] = { 'P', 'A', 'L', 'T', 0, 0, 0, 0, 0, 0, 0, 0 };
	if (nclr->header.format == NCLR_TYPE_ISTUDIOC) fileHeader[3] = 'C'; //format otherwise identical

	uint32_t paletteSize = nclr->nColors * sizeof(COLOR);
	uint32_t paltSize = paletteSize + sizeof(paltHeader);
	uint32_t fileSize = paltSize + sizeof(fileHeader);
	*(uint32_t *) (fileHeader + 0x08) = fileSize;
	*(uint32_t *) (paltHeader + 0x04) = paltSize;
	*(uint32_t *) (paltHeader + 0x08) = nclr->nColors;

	bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	bstreamWrite(stream, paltHeader, sizeof(paltHeader));
	bstreamWrite(stream, nclr->colors, paletteSize);

	return 0;
}

int nclrWrite(NCLR *nclr, BSTREAM *stream) {
	switch (nclr->header.format) {
		case NCLR_TYPE_NCLR:
			return nclrSaveNclr(nclr, stream);
		case NCLR_TYPE_NC:
			return nclrSaveNcl(nclr, stream);
		case NCLR_TYPE_ISTUDIO:
		case NCLR_TYPE_ISTUDIOC:
			return nclrSaveIStudio(nclr, stream);
		case NCLR_TYPE_HUDSON:
			return nclrSaveHudson(nclr, stream);
		case NCLR_TYPE_BIN:
		case NCLR_TYPE_NTFP:
			return nclrSaveBin(nclr, stream);
		case NCLR_TYPE_COMBO:
			return nclrSaveCombo(nclr, stream);
	}
	return 1;
}

int nclrWriteFile(NCLR *nclr, LPCWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) nclr, (OBJECT_WRITER) nclrWrite);
}
