#include <stdio.h>
#include <stdint.h>

#include "nclr.h"
#include "nns.h"
#include "setosa.h"

static int PalIsValidNclr(const unsigned char *buffer, unsigned int size);
static int PalIsValidNcl(const unsigned char *buffer, unsigned int size);
static int PalIsValidIStudio(const unsigned char *buffer, unsigned int size);
static int PalIsValidIStudioCompressed(const unsigned char *buffer, unsigned int size);
static int PalIsValidTose(const unsigned char *buffer, unsigned int size);
static int PalIsValidHudson(const unsigned char *lpFile, unsigned int size);
static int PalIsValidSetosa(const unsigned char *buffer, unsigned int size);

static void PalFree(ObjHeader *obj);

static void PaliRegisterFormat(int format, const wchar_t *name, ObjIdFlag flag, ObjIdProc proc) {
	ObjRegisterFormat(FILE_TYPE_PALETTE, format, name, flag, proc);
}

void PalRegisterFormats(void) {
	ObjRegisterType(FILE_TYPE_PALETTE, sizeof(NCLR), L"Palette", (ObjReader) PalRead, (ObjWriter) PalWrite, NULL, PalFree);
	PaliRegisterFormat(NCLR_TYPE_NCLR, L"NCLR", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_VALIDATED | OBJ_ID_OFFSETS, PalIsValidNclr);
	PaliRegisterFormat(NCLR_TYPE_NC, L"NCL", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_VALIDATED, PalIsValidNcl);
	PaliRegisterFormat(NCLR_TYPE_ISTUDIO, L"5PL", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_VALIDATED, PalIsValidIStudio);
	PaliRegisterFormat(NCLR_TYPE_ISTUDIOC, L"5PC", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_VALIDATED, PalIsValidIStudioCompressed);
	PaliRegisterFormat(NCLR_TYPE_TOSE, L"Tose", OBJ_ID_HEADER | OBJ_ID_SIGNATURE, PalIsValidTose);
	PaliRegisterFormat(NCLR_TYPE_HUDSON, L"Hudson", OBJ_ID_HEADER, PalIsValidHudson);
	PaliRegisterFormat(NCLR_TYPE_SETOSA, L"Setosa", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED, PalIsValidSetosa);
	PaliRegisterFormat(NCLR_TYPE_BIN, L"Binary", 0, PalIsValidBin);
	PaliRegisterFormat(NCLR_TYPE_NTFP, L"NTFP", 0, PalIsValidNtfp);
}

int PalIdentify(const unsigned char *buffer, unsigned int size) {
	int fmt = NCLR_TYPE_INVALID;
	ObjIdentifyExByType(buffer, size, FILE_TYPE_PALETTE, &fmt);
	return fmt;
}


void PalFree(ObjHeader *header) {
	NCLR *nclr = (NCLR *) header;
	if (nclr->colors != NULL) free(nclr->colors);
	nclr->colors = NULL;

	COMBO2D *combo2d = (COMBO2D *) nclr->header.combo;
	if (combo2d != NULL) {
		combo2dUnlink(combo2d, &nclr->header);
	}
}

static int PalIsValidHudson(const unsigned char *lpFile, unsigned int size) {
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

static int PalIsValidNclr(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "RLCN", 4) != 0 && memcmp(buffer, "RPCN", 4) != 0) return 0;

	const unsigned char *pltt = NnsG2dFindBlockBySignature(buffer, size, "PLTT", NNS_SIG_LE, NULL);
	if (pltt == NULL) return 0;

	return 1;
}

static int PalIsValidNcl(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NCCL", 4) != 0) return 0;

	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	return 1;
}

static int PalIsValidIStudio(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NTPL", 4) != 0) return 0;

	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	return 1;
}

static int PalIsValidIStudioCompressed(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "NTPC", 4) != 0) return 0;

	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	return 1;
}

static int PalIsValidSetosa(const unsigned char *buffer, unsigned int size) {
	if (!SetIsValid(buffer, size)) return 0;

	const unsigned char *pPltt = SetGetBlock(buffer, size, "PLTT");
	if (pPltt == NULL) return 0;

	return 1;
}

static int PalIsValidTose(const unsigned char *buffer, unsigned int size) {
	if (size < 8) return 0;                        // size of file header
	if (memcmp(buffer, "NCL\0", 4) != 0) return 0; // file signature

	unsigned int nCol = *(const uint32_t *) (buffer + 0x4);
	if (nCol != (size - 8) / 2) return 0;

	return 1;
}

static int PalReadHudson(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	int dataLength = *(uint16_t *) buffer;
	int nColors = *(uint16_t *) (buffer + 2);

	nclr->nColors = nColors;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	memcpy(nclr->colors, buffer + 4, nColors * 2);
	return OBJ_STATUS_SUCCESS;
}

static int PalReadBin(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	if (!PalIsValidNtfp(buffer, size)) return 1; //this function is being reused for NTFP as well
	
	int nColors = size >> 1;

	nclr->nColors = nColors;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nColors, 2);
	memcpy(nclr->colors, buffer, nColors * 2);
	return OBJ_STATUS_SUCCESS;
}

static int PalReadNcl(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	unsigned int paltSize = 0, cmntSize = 0;
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, &paltSize);
	const unsigned char *cmnt = NnsG2dFindBlockBySignature(buffer, size, "CMNT", NNS_SIG_BE, &cmntSize);

	unsigned int nPalettes = *(uint32_t *) (palt + 0x4);
	nclr->nColors = nPalettes * *(uint32_t *) (palt + 0x0);
	nclr->extPalette = nclr->nColors > 256;
	nclr->nBits = *(uint32_t *) (palt + 0x0) > 16 ? 8 : 4;
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	memcpy(nclr->colors, palt + 0x8, nclr->nColors * 2);
	if (cmnt != NULL) {
		nclr->header.comment = (char *) malloc(cmntSize);
		memcpy(nclr->header.comment, cmnt, cmntSize);
	}

	return OBJ_STATUS_SUCCESS;
}

static void PaliReadIStudio(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	unsigned int paltSize = 0;
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, &paltSize);

	nclr->nColors = *(uint32_t *) (palt + 0x0);
	nclr->extPalette = nclr->nColors > 256;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nclr->nColors, 2);
	memcpy(nclr->colors, palt + 0x4, nclr->nColors * 2);
}

static int PalReadIStudio(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	PaliReadIStudio(nclr, buffer, size);
	return OBJ_STATUS_SUCCESS;
}

static int PalReadIStudioCompressed(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	PaliReadIStudio(nclr, buffer, size);
	return OBJ_STATUS_SUCCESS;
}

static int PalReadNclr(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	unsigned int plttSize = 0, pcmpSize = 0;
	const unsigned char *pltt = NnsG2dFindBlockBySignature(buffer, size, "PLTT", NNS_SIG_LE, &plttSize);
	const unsigned char *pcmp = NnsG2dFindBlockBySignature(buffer, size, "PCMP", NNS_SIG_LE, &pcmpSize);

	int bits = *(uint32_t *) (pltt + 0x0);
	bits = 1 << (bits - 1);
	int dataOffset = *(uint32_t *) (pltt + 0xC);
	int nColors = (plttSize - dataOffset) / sizeof(COLOR);

	nclr->nBits = bits;
	nclr->extPalette = *(uint32_t *) (pltt + 0x4);
	nclr->nColors = nColors;

	unsigned int sizePerPalette = 1 << nclr->nBits;
	const uint16_t *plttSrc = (const uint16_t *) (pltt + dataOffset);
	if (pcmp == NULL) {
		//no palette compression used: write colors directly
		nclr->nColors = (*(uint32_t *) (pltt + 0x8)) / sizeof(COLOR);
		nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
		nclr->compressedPalette = 0;
		memcpy(nclr->colors, plttSrc, nclr->nColors * sizeof(COLOR));
	} else {
		//palette compression used, load using PCMP index table
		const uint16_t *idxTable = (const uint16_t *) (pcmp + *(const uint32_t *) (pcmp + 0x4));
		int nPalettes = *(uint16_t *) (pcmp + 0x0);

		if (nclr->nBits == 4 || nclr->extPalette) {
			nclr->nColors = 16 << nclr->nBits; // size of full 16 palettes
		} else {
			nclr->nColors = 256; // size of one 256-color palette
		}
		nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));

		unsigned int sizePerPalette = 1 << nclr->nBits;
		for (int i = 0; i < nPalettes; i++) {
			memcpy(nclr->colors + (idxTable[i] << nclr->nBits), plttSrc + (i << nclr->nBits), sizePerPalette * sizeof(COLOR));
		}

		unsigned int dataSize = (*(uint32_t *) (pltt + 0x8)) / sizeof(COLOR);
		unsigned int realDataSize = nPalettes << nclr->nBits;
		if (dataSize != realDataSize && dataSize == (nclr->nColors - realDataSize)) nclr->g2dBug = 1;
		nclr->compressedPalette = 1;
	}

	return OBJ_STATUS_SUCCESS;
}

static int PalReadSetosa(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	const unsigned char *pPltt = SetGetBlock(buffer, size, "PLTT");
	unsigned int nColors = (*(const uint32_t *) (pPltt + 0x0)) / sizeof(COLOR);

	nclr->nBits = 4;
	nclr->nColors = nColors;
	nclr->colors = (COLOR *) calloc(nColors, sizeof(COLOR));
	memcpy(nclr->colors, pPltt + 4, nColors * sizeof(COLOR));

	return OBJ_STATUS_SUCCESS;
}

static int PalReadTose(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	unsigned int nCol = *(const uint32_t *) (buffer + 0x4);
	nclr->nColors = nCol;
	nclr->nBits = nCol <= 0x100 ? 4 : 8; // heuristic
	nclr->colors = (COLOR *) calloc(nCol, sizeof(COLOR));
	memcpy(nclr->colors, buffer + 8, nCol * sizeof(COLOR));

	return OBJ_STATUS_SUCCESS;
}

int PalRead(NCLR *nclr, const unsigned char *buffer, unsigned int size) {
	switch (nclr->header.format) {
		case NCLR_TYPE_NCLR:
			return PalReadNclr(nclr, buffer, size);
		case NCLR_TYPE_NC:
			return PalReadNcl(nclr, buffer, size);
		case NCLR_TYPE_ISTUDIO:
			return PalReadIStudio(nclr, buffer, size);
		case NCLR_TYPE_ISTUDIOC:
			return PalReadIStudioCompressed(nclr, buffer, size);
		case NCLR_TYPE_TOSE:
			return PalReadTose(nclr, buffer, size);
		case NCLR_TYPE_HUDSON:
			return PalReadHudson(nclr, buffer, size);
		case NCLR_TYPE_SETOSA:
			return PalReadSetosa(nclr, buffer, size);
		case NCLR_TYPE_BIN:
			return PalReadBin(nclr, buffer, size);
		case NCLR_TYPE_NTFP:
			return PalReadBin(nclr, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

static uint16_t *PalConstructDataOutput(NCLR *nclr, unsigned int *size, uint16_t **pOutIndexTable, unsigned int *pSizeIndexTable) {
	if (!nclr->compressedPalette) {
		//palette compression not used, write directly
		uint16_t *buf = (uint16_t *) calloc(nclr->nColors, sizeof(COLOR));
		memcpy(buf, nclr->colors, nclr->nColors * sizeof(COLOR));
		*size = nclr->nColors;

		if (pOutIndexTable != NULL) *pOutIndexTable = NULL;
		if (pSizeIndexTable != NULL) *pSizeIndexTable = 0;
		return buf;
	}

	//count number of palettes
	unsigned int sizePalette = 1 << nclr->nBits;
	unsigned int nPalettes = (nclr->nColors + sizePalette - 1) / sizePalette;

	unsigned char *plttUsed = (unsigned char *) calloc(nPalettes, 1);
	unsigned int nUsedPalettes = 0, dataBlockSize = 0;
	for (unsigned int i = 0; i < nPalettes; i++) {
		unsigned int nColsThisPalette = sizePalette;
		if ((i << nclr->nBits) + nColsThisPalette > (unsigned int) nclr->nColors) {
			nColsThisPalette = nclr->nColors - (i << nclr->nBits);
		}

		//determine used status by checking that the whole palette is zeroed. This is how
		//official converters perform this check.
		int used = 0;
		for (unsigned int j = 0; j < nColsThisPalette; j++) {
			if (nclr->colors[(i << nclr->nBits) + j]) {
				used = 1;
				break;
			}
		}
		plttUsed[i] = used;
		nUsedPalettes += used;
		if (used) dataBlockSize += nColsThisPalette;
	}

	uint16_t *data = (uint16_t *) calloc(dataBlockSize, sizeof(COLOR));
	unsigned int destI = 0;
	for (unsigned int i = 0; i < nPalettes; i++) {
		if (!plttUsed[i]) continue;

		unsigned int thisPlttSize = 1 << nclr->nBits;
		if ((i << nclr->nBits) + thisPlttSize > (unsigned int) nclr->nColors) {
			thisPlttSize = nclr->nColors - (i << nclr->nBits);
		}
		memcpy(data + (destI << nclr->nBits), nclr->colors + (i << nclr->nBits), thisPlttSize * sizeof(COLOR));
		destI++;
	}
	*size = dataBlockSize;

	//construct index table
	if (pOutIndexTable != NULL) {
		uint16_t *table = (uint16_t *) calloc(nUsedPalettes, sizeof(uint16_t));
		destI = 0;
		for (unsigned int i = 0; i < nPalettes; i++) {
			if (!plttUsed[i]) continue;

			table[destI++] = i;
		}

		*pSizeIndexTable = nUsedPalettes;
		*pOutIndexTable = table;
	}
	free(plttUsed);

	return data;
}

int PalWriteNclr(NCLR *nclr, BSTREAM *stream) {
	unsigned char plttHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 };

	unsigned int palDataSize, sizeIndexTable;
	uint16_t *palIndexTable = NULL;
	uint16_t *data = PalConstructDataOutput(nclr, &palDataSize, &palIndexTable, &sizeIndexTable);

	//count number of palettes
	unsigned int sizePalette = 1 << nclr->nBits;
	unsigned int nPalettes = (nclr->nColors + sizePalette - 1) / sizePalette;

	unsigned int usedPaletteSize = nclr->nColors;
	if (nclr->compressedPalette && nclr->g2dBug) {
		//replicate converter bug
		usedPaletteSize = nclr->nColors - (sizeIndexTable << nclr->nBits);
	}

	if (nclr->nBits == 8) *(uint32_t *) (plttHeader + 0x0) = 4;
	else *(uint32_t *) (plttHeader + 0x0) = 3;
	*(uint32_t *) (plttHeader + 0x4) = nclr->extPalette;
	*(uint32_t *) (plttHeader + 0x8) = usedPaletteSize * sizeof(COLOR);

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCLR", 1, 0, NNS_TYPE_G2D, NNS_SIG_LE);
	NnsStreamStartBlock(&nnsStream, "PLTT");
	NnsStreamWrite(&nnsStream, plttHeader, sizeof(plttHeader));
	NnsStreamWrite(&nnsStream, data, palDataSize * sizeof(COLOR));
	NnsStreamEndBlock(&nnsStream);

	if (nclr->compressedPalette) {
		unsigned char pcmpHeader[] = { 0, 0, 0xEF, 0xBE, 0x8, 0, 0, 0 };
		*(uint16_t *) (pcmpHeader + 0) = sizeIndexTable;

		NnsStreamStartBlock(&nnsStream, "PCMP");
		NnsStreamWrite(&nnsStream, pcmpHeader, sizeof(pcmpHeader));
		NnsStreamWrite(&nnsStream, palIndexTable, sizeIndexTable * sizeof(uint16_t));
		NnsStreamEndBlock(&nnsStream);
	}

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);

	free(data);
	if (palIndexTable != NULL) free(palIndexTable);
	return 0;
}

int PalWriteNcl(NCLR *nclr, BSTREAM *stream) {
	uint8_t paltHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint32_t *) (paltHeader + 0x0) = 1 << nclr->nBits;
	*(uint32_t *) (paltHeader + 0x4) = nclr->nColors >> nclr->nBits;

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
	unsigned int dataSize = 0;
	uint16_t *data = PalConstructDataOutput(nclr, &dataSize, NULL, NULL);
	bstreamWrite(stream, data, dataSize * sizeof(COLOR));
	free(data);
	return 0;
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

static int PalWriteSetosa(NCLR *nclr, BSTREAM *stream) {
	SetStream setStream;
	SetStreamCreate(&setStream);

	uint32_t nBytesPltt = nclr->nColors * sizeof(COLOR);
	SetStreamStartBlock(&setStream, "PLTT");
	SetStreamWrite(&setStream, &nBytesPltt, sizeof(nBytesPltt));
	SetStreamWrite(&setStream, nclr->colors, nBytesPltt);
	SetStreamEndBlock(&setStream);

	SetStreamFinalize(&setStream);
	SetStreamFlushOut(&setStream, stream);
	SetStreamFree(&setStream);

	return OBJ_STATUS_SUCCESS;
}

static int PalWriteTose(NCLR *nclr, BSTREAM *stream) {
	uint32_t nCol = nclr->nColors;
	bstreamWrite(stream, "NCL\0", 4);
	bstreamWrite(stream, &nCol, sizeof(nCol));
	bstreamWrite(stream, nclr->colors, nclr->nColors * sizeof(COLOR));

	return OBJ_STATUS_SUCCESS;
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
		case NCLR_TYPE_TOSE:
			return PalWriteTose(nclr, stream);
		case NCLR_TYPE_HUDSON:
			return PalWriteHudson(nclr, stream);
		case NCLR_TYPE_BIN:
		case NCLR_TYPE_NTFP:
			return PalWriteBin(nclr, stream);
		case NCLR_TYPE_SETOSA:
			return PalWriteSetosa(nclr, stream);
	}
	return OBJ_STATUS_INVALID;
}

int PalWriteFile(NCLR *nclr, LPCWSTR name) {
	return ObjWriteFile(&nclr->header, name);
}
