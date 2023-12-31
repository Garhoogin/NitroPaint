#include <stdio.h>
#include <stdint.h>

#include "nclr.h"
#include "nns.h"

LPCWSTR paletteFormatNames[] = { L"Invalid", L"NCLR", L"NCL", L"5PL", L"5PC", L"Hudson", L"Binary", L"NTFP", NULL };

void PalFree(OBJECT_HEADER *header) {
	NCLR *nclr = (NCLR *) header;
	if (nclr->colors != NULL) free(nclr->colors);
	nclr->colors = NULL;
	if (nclr->idxTable != NULL) free(nclr->idxTable);
	nclr->idxTable = NULL;

	COMBO2D *combo2d = nclr->combo2d;
	if (combo2d != NULL) {
		combo2dUnlink(combo2d, &nclr->header);
		if (combo2d->nLinks == 0) {
			combo2dFree(combo2d);
			free(combo2d);
		}
	}
	nclr->combo2d = NULL;
}

void PalInit(NCLR *nclr, int format) {
	nclr->header.size = sizeof(NCLR);
	ObjInit((OBJECT_HEADER *) nclr, FILE_TYPE_PALETTE, format);
	nclr->header.dispose = PalFree;
	nclr->header.writer = (OBJECT_WRITER) PalWrite;
	nclr->combo2d = NULL;
}

int PalIsValidHudson(const unsigned char *lpFile, unsigned int size) {
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

int PalIsValidBin(const unsigned char *lpFile, unsigned int size) {
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

int PalIsValidNtfp(const unsigned char *lpFile, unsigned int size) {
	if (size & 1) return 0;
	for (unsigned int i = 0; i < size >> 1; i++) {
		COLOR c = *(COLOR *) (lpFile + i * 2);
		//if (c & 0x8000) return 0;
	}
	return 1;
}

int PalIsValidNclr(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "RLCN", 4) != 0 && memcmp(buffer, "RPCN", 4) != 0) return 0;

	const unsigned char *pltt = NnsG2dFindBlockBySignature(buffer, size, "PLTT", NNS_SIG_LE, NULL);
	if (pltt == NULL) return 0;

	return 1;
}

int PalIsValidNcl(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NCCL", 4) != 0) return 0;

	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	return 1;
}

int PalIsValidIStudio(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NTPL", 4) != 0) return 0;

	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	return 1;
}

int PalIsValidIStudioCompressed(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NTPC", 4) != 0) return 0;

	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	return 1;
}

int PalIdentify(const unsigned char *lpFile, unsigned int size) {
	if (PalIsValidNclr(lpFile, size)) return NCLR_TYPE_NCLR;
	if (PalIsValidNcl(lpFile, size)) return NCLR_TYPE_NC;
	if (PalIsValidIStudio(lpFile, size)) return NCLR_TYPE_ISTUDIO;
	if (PalIsValidIStudioCompressed(lpFile, size)) return NCLR_TYPE_ISTUDIOC;
	if (PalIsValidHudson(lpFile, size)) return NCLR_TYPE_HUDSON;
	if (PalIsValidBin(lpFile, size)) return NCLR_TYPE_BIN;
	if (PalIsValidNtfp(lpFile, size)) return NCLR_TYPE_NTFP;
	return NCLR_TYPE_INVALID;
}

int PalReadHudson(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 1;

	int dataLength = *(uint16_t *) buffer;
	int nColors = *(uint16_t *) (buffer + 2);

	PalInit(nclr, NCLR_TYPE_HUDSON);
	nclr->nColors = nColors;
	nclr->totalSize = nclr->nColors * sizeof(COLOR);
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	memcpy(nclr->colors, buffer + 4, nColors * 2);
	return 0;
}

int PalReadBin(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	if (!PalIsValidNtfp(buffer, size)) return 1; //this function is being reused for NTFP as well
	
	int nColors = size >> 1;

	PalInit(nclr, PalIsValidBin(buffer, size) ? NCLR_TYPE_BIN : NCLR_TYPE_NTFP);
	nclr->nColors = nColors;
	nclr->totalSize = nclr->nColors * sizeof(COLOR);
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	memcpy(nclr->colors, buffer, nColors * 2);
	return 0;
}

int PalReadNcl(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	if (!PalIsValidNcl(buffer, size)) return 1;

	PalInit(nclr, NCLR_TYPE_NC);

	unsigned int paltSize = 0, cmntSize = 0;
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, &paltSize);
	const unsigned char *cmnt = NnsG2dFindBlockBySignature(buffer, size, "CMNT", NNS_SIG_BE, &cmntSize);

	nclr->nPalettes = *(uint32_t *) (palt + 0x4);
	nclr->nColors = nclr->nPalettes * *(uint32_t *) (palt + 0x0);
	nclr->extPalette = nclr->nColors > 256;
	nclr->nBits = *(uint32_t *) (palt + 0x0) > 16 ? 8 : 4;
	nclr->totalSize = nclr->nColors * sizeof(COLOR);
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	memcpy(nclr->colors, palt + 0x8, nclr->nColors * 2);
	if (cmnt != NULL) {
		nclr->header.comment = (char *) malloc(cmntSize);
		memcpy(nclr->header.comment, cmnt, cmntSize);
	}

	return 0;
}

void PaliReadIStudio(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	unsigned int paltSize = 0;
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, &paltSize);

	nclr->nColors = *(uint32_t *) (palt + 0x0);
	nclr->nPalettes = (nclr->nColors + 15) / 16;
	nclr->extPalette = nclr->nColors > 256;
	nclr->nBits = 4;
	nclr->totalSize = nclr->nColors;
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	memcpy(nclr->colors, palt + 0x4, nclr->nColors * 2);
}

int PalReadIStudio(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	if (!PalIsValidIStudio(buffer, size)) return 1;

	PalInit(nclr, NCLR_TYPE_ISTUDIO);
	PaliReadIStudio(nclr, buffer, size);
	return 0;
}

int PalReadIStudioCompressed(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	if (!PalIsValidIStudioCompressed(buffer, size)) return 1;

	PalInit(nclr, NCLR_TYPE_ISTUDIOC);
	PaliReadIStudio(nclr, buffer, size);
	return 0;
}

int PalReadNclr(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	unsigned int plttSize = 0, pcmpSize = 0;
	const unsigned char *pltt = NnsG2dFindBlockBySignature(buffer, size, "PLTT", NNS_SIG_LE, &plttSize);
	const unsigned char *pcmp = NnsG2dFindBlockBySignature(buffer, size, "PCMP", NNS_SIG_LE, &pcmpSize);
	if (pltt == NULL) return 1;

	int bits = *(uint32_t *) (pltt + 0x0);
	bits = 1 << (bits - 1);
	int dataSize = *(uint32_t *) (pltt + 0x8);
	int dataOffset = *(uint32_t *) (pltt + 0xC);
	int nColors = (plttSize - dataOffset) >> 1;

	PalInit(nclr, NCLR_TYPE_NCLR);
	nclr->nColors = nColors;
	nclr->nBits = bits;
	nclr->colors = (COLOR *) calloc(nColors, sizeof(COLOR));
	nclr->extPalette = *(uint32_t *) (pltt + 0x4);
	nclr->totalSize = *(uint32_t *) (pltt + 0x8);
	nclr->nPalettes = 0;
	nclr->idxTable = NULL;

	if (pcmp != NULL) {
		nclr->nPalettes = *(uint16_t *) (pcmp + 0);
		nclr->idxTable = (short *) calloc(nclr->nPalettes, 2);
		memcpy(nclr->idxTable, pcmp + *(uint32_t *) (pcmp + 0x4), nclr->nPalettes * 2);
	}
	memcpy(nclr->colors, pltt + dataOffset, nColors * 2);
	return 0;
}

int PalRead(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	int type = PalIdentify(buffer, size);
	switch (type) {
		case NCLR_TYPE_NCLR:
			return PalReadNclr(nclr, buffer, size);
		case NCLR_TYPE_NC:
			return PalReadNcl(nclr, buffer, size);
		case NCLR_TYPE_ISTUDIO:
			return PalReadIStudio(nclr, buffer, size);
		case NCLR_TYPE_ISTUDIOC:
			return PalReadIStudioCompressed(nclr, buffer, size);
		case NCLR_TYPE_HUDSON:
			return PalReadHudson(nclr, buffer, size);
		case NCLR_TYPE_BIN:
			return PalReadBin(nclr, buffer, size);
		case NCLR_TYPE_NTFP:
			return PalReadBin(nclr, buffer, size);
	}
	return 1;
}

int PalReadFile(NCLR *nclr, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) nclr, (OBJECT_READER) PalRead);
}

int PalWriteNclr(NCLR *nclr, BSTREAM *stream) {
	uint8_t plttHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 };
	uint8_t pcmpHeader[] = { 0, 0, 0xEF, 0xBE, 0x8, 0, 0, 0 };

	if (nclr->nBits == 8) *(uint32_t *) (plttHeader + 0x0) = 4;
	else *(uint32_t *) (plttHeader + 0x0) = 3;
	*(uint32_t *) (plttHeader + 0x4) = nclr->extPalette;
	*(uint32_t *) (plttHeader + 0x8) = nclr->totalSize;

	*(uint16_t *) (pcmpHeader + 0) = nclr->nPalettes;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCLR", 1, 0, NNS_TYPE_G2D, NNS_SIG_LE);
	NnsStreamStartBlock(&nnsStream, "PLTT");
	NnsStreamWrite(&nnsStream, plttHeader, sizeof(plttHeader));
	NnsStreamWrite(&nnsStream, nclr->colors, nclr->nColors * sizeof(COLOR));
	NnsStreamEndBlock(&nnsStream);

	if (nclr->nPalettes && nclr->idxTable != NULL) {
		NnsStreamStartBlock(&nnsStream, "PCMP");
		NnsStreamWrite(&nnsStream, pcmpHeader, sizeof(pcmpHeader));
		NnsStreamWrite(&nnsStream, nclr->idxTable, nclr->nPalettes * 2);
		NnsStreamEndBlock(&nnsStream);
	}

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

int PalWriteNcl(NCLR *nclr, BSTREAM *stream) {
	uint8_t paltHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint32_t *) (paltHeader + 0x0) = nclr->nColors / nclr->nPalettes;
	*(uint32_t *) (paltHeader + 0x4) = nclr->nPalettes;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCCL", 1, 0, NNS_TYPE_G2D, NNS_SIG_BE);
	NnsStreamStartBlock(&nnsStream, "PALT");
	NnsStreamWrite(&nnsStream, paltHeader, sizeof(paltHeader));
	NnsStreamWrite(&nnsStream, nclr->colors, nclr->nColors * sizeof(COLOR));
	NnsStreamEndBlock(&nnsStream);
	if (nclr->header.comment != NULL) {
		NnsStreamStartBlock(&nnsStream, "CMNT");
		NnsStreamWrite(&nnsStream, nclr->header.comment, strlen(nclr->header.comment));
		NnsStreamEndBlock(&nnsStream);
	}
	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

int PalWriteHudson(NCLR *nclr, BSTREAM *stream) {
	uint16_t fileHeader[] = { 0, 0 };
	fileHeader[0] = nclr->nColors * 2;
	fileHeader[1] = nclr->nColors;

	bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	bstreamWrite(stream, nclr->colors, nclr->nColors * 2);
	return 0;
}

int PalWriteBin(NCLR *nclr, BSTREAM *stream) {
	bstreamWrite(stream, nclr->colors, nclr->nColors * 2);
	return 0;
}

int PalWriteCombo(NCLR *nclr, BSTREAM *stream) {
	return combo2dWrite(nclr->combo2d, stream);
}

int PalWriteIStudio(NCLR *nclr, BSTREAM *stream) {
	uint8_t paltHeader[] = { 0, 0, 0, 0 };
	*(uint32_t *) (paltHeader + 0x0) = nclr->nColors;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, nclr->header.format == NCLR_TYPE_ISTUDIOC ? "NTPC" : "NTPL", 1, 0, NNS_TYPE_G2D, NNS_SIG_BE);
	NnsStreamStartBlock(&nnsStream, "PALT");
	NnsStreamWrite(&nnsStream, paltHeader, sizeof(paltHeader));
	NnsStreamWrite(&nnsStream, nclr->colors, nclr->nColors * sizeof(COLOR));
	NnsStreamEndBlock(&nnsStream);
	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

int PalWrite(NCLR *nclr, BSTREAM *stream) {
	switch (nclr->header.format) {
		case NCLR_TYPE_NCLR:
			return PalWriteNclr(nclr, stream);
		case NCLR_TYPE_NC:
			return PalWriteNcl(nclr, stream);
		case NCLR_TYPE_ISTUDIO:
		case NCLR_TYPE_ISTUDIOC:
			return PalWriteIStudio(nclr, stream);
		case NCLR_TYPE_HUDSON:
			return PalWriteHudson(nclr, stream);
		case NCLR_TYPE_BIN:
		case NCLR_TYPE_NTFP:
			return PalWriteBin(nclr, stream);
		case NCLR_TYPE_COMBO:
			return PalWriteCombo(nclr, stream);
	}
	return 1;
}

int PalWriteFile(NCLR *nclr, LPCWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER *) nclr, (OBJECT_WRITER) PalWrite);
}
