#include "NitroCell.h"
#include "NitroPalette.h"
#include "NitroCharacter.h"
#include "nns.h"
#include "setosa.h"

#include <string.h>

static int CellIsValidHudson(const unsigned char *buffer, unsigned int size);
static int CellIsValidGhostTrick(const unsigned char *buffer, unsigned int size);
static int CellIsValidSetosa(const unsigned char *buffer, unsigned int size);
static int CellIsValidBomberman(const unsigned char *buffer, unsigned int size);
static int CellIsValidNcer(const unsigned char *buffer, unsigned int size);

static int CellReadNcer(NCER *ncer, const unsigned char *buffer, unsigned int size);
static int CellReadSetosa(NCER *ncer, const unsigned char *buffer, unsigned int size);
static int CellReadHudson(NCER *ncer, const unsigned char *buffer, unsigned int size);
static int CellReadBomberman(NCER *ncer, const unsigned char *buffer, unsigned int size);
static int CellReadGhostTrick(NCER *ncer, const unsigned char *buffer, unsigned int size);

static int CellWriteNcer(NCER *ncer, BSTREAM *stream);
static int CellWriteSetosa(NCER *ncer, BSTREAM *stream);
static int CellWriteBomberman(NCER *ncer, BSTREAM *stream);
static int CellWriteHudson(NCER *ncer, BSTREAM *stream);

static const ObjIdEntry sFormats[] = {
	{
		FILE_TYPE_CELL, NCER_TYPE_NCER, "NCER",
		OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_OFFSETS | OBJ_ID_VALIDATED,
		CellIsValidNcer,
		(ObjReader) CellReadNcer,
		(ObjWriter) CellWriteNcer
	}, {
		FILE_TYPE_CELL, NCER_TYPE_SETOSA, "Setosa",
		OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_OFFSETS | OBJ_ID_VALIDATED,
		CellIsValidSetosa,
		(ObjReader) CellReadSetosa,
		(ObjWriter) CellWriteSetosa
	}, {
		FILE_TYPE_CELL, NCER_TYPE_HUDSON, "Hudson",
		OBJ_ID_HEADER | OBJ_ID_OFFSETS,
		CellIsValidHudson,
		(ObjReader) CellReadHudson,
		(ObjWriter) CellWriteHudson
	}, {
		FILE_TYPE_CELL, NCER_TYPE_BOMBERMAN, "Bomberman",
		OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED,
		CellIsValidBomberman,
		(ObjReader) CellReadBomberman,
		(ObjWriter) CellWriteBomberman
	}, {
		FILE_TYPE_CELL, NCER_TYPE_GHOSTTRICK, "Ghost Trick",
		OBJ_ID_HEADER | OBJ_ID_OFFSETS,
		CellIsValidGhostTrick,
		(ObjReader) CellReadGhostTrick,
		NULL
	}
};

void CellRegisterFormats(void) {
	ObjRegisterType(FILE_TYPE_CELL, sizeof(NCER), "Cell Bank", NULL, CellFree);

	for (size_t i = 0; i < sizeof(sFormats) / sizeof(sFormats[0]); i++) {
		ObjRegisterFormat(&sFormats[i]);
	}
}


#define SEXT8(n)   (((n)<0x080)?(n):((n)-0x100))
#define SEXT9(n)   (((n)<0x100)?(n):((n)-0x200))

static int CellIsValidHudson(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;

	unsigned int nCells = *(const uint32_t *) buffer;
	if (nCells == 0) return 0;             // 0 cells -> reject
	if (nCells > (size - 4) / 4) return 0; // file not big enough for offset table

	unsigned int highestOffset = 4;
	for (unsigned int i = 0; i < nCells; i++) {
		uint32_t ofs = ((uint32_t *) (buffer + 4))[i] + 4;
		if (4 + i * 4 + 4 >= highestOffset) highestOffset = 8 + i * 4;
		if (ofs >= size) return 0;
		unsigned int nOAM = *(uint16_t *) (buffer + ofs);
		
		//attrs size: 0xA * nOAM
		unsigned int endOfs = ofs + 2 + 0xA * nOAM;
		if (endOfs > highestOffset) highestOffset = endOfs;
		if (endOfs > size) return 0;
	}

	//if there is much unused data at the end, this is probably not an actual cell file!
	if (highestOffset < size) return 0;
	return 1;
}

static int CellIsValidNcer(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "RECN", 4) != 0) return 0;

	//must have CEBK section
	const unsigned char *cebk = NnsG2dFindBlockBySignature(buffer, size, "CEBK", NNS_SIG_LE, NULL);
	if (cebk == NULL) return 0;

	return 1;
}

static int CellIsValidGhostTrick(const unsigned char *buffer, unsigned int size) {
	if (size < 2) return 0; //must contain at least 1 cell

	const uint16_t *cellOffsets = (uint16_t *) buffer;
	unsigned int nCells = cellOffsets[0]; //first offset is count
	unsigned int cellOffset = nCells * 2;
	if (cellOffset > size) return 0;

	//read cells
	unsigned int readSize = 2;
	for (unsigned int i = 0; i < nCells; i++) {
		unsigned int offset = cellOffsets[i] * 2;
		if ((offset + 2) >= size) return 0;

		const unsigned char *cell = buffer + offset;
		unsigned int nObj = *(uint16_t *) cell;

		//file must be big enough to fit contained OBJ
		unsigned int endOfs = offset + 2 + nObj * 6;
		if (endOfs > size) return 0;
		if (endOfs > readSize) readSize = endOfs;
	}
	if (readSize < size) return 0;
	return 1;
}

static int CellIsValidSetosa(const unsigned char *buffer, unsigned int size) {
	if (!SetIsValid(buffer, size)) return 0;

	const unsigned char *cellBlock = SetGetBlock(buffer, size, "CELL");
	const unsigned char *cbexBlock = SetGetBlock(buffer, size, "CBEX");
	return cellBlock != NULL || cbexBlock != NULL;
}

static int CellIsValidBomberman(const unsigned char *buffer, unsigned int size) {
	if (!BldtIsValid(buffer, size)) return 0;

	unsigned int unpackedSize;
	unsigned char *unpacked = BldtGetUncompressed(buffer, size, &unpackedSize);

	unsigned int ofs0 = *(const uint32_t *) (unpacked + 0x00);
	unsigned int ofsObj = *(const uint32_t *) (unpacked + 0x0C);
	if (ofs0 < 0x14 || ofs0 > size) goto Invalid;
	if (ofsObj < 0x14 || ofsObj > size) goto Invalid;

	//Check cell data
	unsigned int nCell = (ofs0 - 0x14) / 8;
	for (unsigned int i = 0; i < nCell; i++) {
		const unsigned char *cellInfo = unpacked + 0x14 + i * 8;
		unsigned int iObj = *(const uint16_t *) (cellInfo + 0x0);
		unsigned int nObj = *(const uint8_t *) (cellInfo + 0x2);

		unsigned int objStart = ofsObj + iObj * 0xC;
		if (objStart > size) return 0;                 // offset within file
		if ((size - objStart) < nObj * 0xC) return 0;  // enough space for OBJ
	}

	free(unpacked);
	return 1;

Invalid:
	free(unpacked);
	return 0;
}

int CellReadHudson(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	int nCells = *(uint32_t *) buffer;
	ncer->labl = NULL;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->uextSize = 0;
	ncer->nCells = nCells;
	ncer->bankAttribs = 0;
	
	NCER_CELL *cells = (NCER_CELL *) calloc(nCells, sizeof(NCER_CELL));
	ncer->cells = cells;
	for (int i = 0; i < nCells; i++) {
		uint32_t ofs = ((uint32_t *) (buffer + 4))[i] + 4;
		int nOAM = *(uint16_t *) (buffer + ofs);
		NCER_CELL *thisCell = cells + i;
		CellInitBankCell(ncer, thisCell, nOAM);

		int minX = 0x7FFF, maxX = -0x7FFF, minY = 0x7FFF, maxY = -0x7FFF;
		uint16_t *attrs = (uint16_t *) (buffer + ofs + 2);
		for (int j = 0; j < nOAM; j++) {
			memcpy(thisCell->attr + j * 3, attrs + j * 5, 6);
			GxOamAttrInfo info;
			CellDecodeOamAttributes(&info, thisCell, j);

			int16_t x = attrs[j * 5 + 3];
			int16_t y = attrs[j * 5 + 4];
			if (x < minX) minX = x;
			if (x + info.width > maxX) maxX = x + info.width;
			if (y < minY) minY = y;
			if (y + info.height > maxY) maxY = y + info.height;
		}

		thisCell->maxX = maxX;
		thisCell->minX = minX;
		thisCell->maxY = maxY;
		thisCell->minY = minY;
	}
	return 0;
}

int CellReadNcer(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	ncer->nCells = 0;
	ncer->uextSize = 0;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->labl = NULL;

	unsigned int cebkSize = 0, lablSize = 0, uextSize = 0;
	const unsigned char *cebk = NnsG2dFindBlockBySignature(buffer, size, "CEBK", NNS_SIG_LE, &cebkSize);
	const unsigned char *labl = NnsG2dFindBlockBySignature(buffer, size, "LABL", NNS_SIG_LE, &lablSize);
	const unsigned char *uext = NnsG2dFindBlockBySignature(buffer, size, "UEXT", NNS_SIG_LE, &uextSize);

	//bank
	if (cebk != NULL) {
		ncer->nCells      = *(const uint16_t *) (cebk + 0x00);  // number of cells this bank
		ncer->bankAttribs = *(const uint16_t *) (cebk + 0x02);  // 1 - with bounding rectangle, 0 - without
		ncer->cells       = (NCER_CELL *) calloc(ncer->nCells, sizeof(NCER_CELL));

		uint32_t ofsCells    = *(const uint32_t *) (cebk + 0x04);  // offset to cell data
		uint32_t mappingMode = *(const uint32_t *) (cebk + 0x08);  // mapping mode (0-4)
		uint32_t ofsVramTran = *(const uint32_t *) (cebk + 0x0C);  // offset to VRAM transfer info
		uint32_t ofsExtData  = *(const uint32_t *) (cebk + 0x14);  // offset to extended attribute data

		static const int mappingModes[] = {
			GX_OBJVRAMMODE_CHAR_1D_32K,
			GX_OBJVRAMMODE_CHAR_1D_64K,
			GX_OBJVRAMMODE_CHAR_1D_128K,
			GX_OBJVRAMMODE_CHAR_1D_256K,
			GX_OBJVRAMMODE_CHAR_2D
		};
		if (mappingMode < 5) ncer->mappingMode = mappingModes[mappingMode];
		else ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;

		//size of each cell entry in the bank (with vs. without bounding rect info)
		unsigned int                    perCellDataSize = 0x08;  // cell
		if (ncer->bankAttribs & 0x0001) perCellDataSize = 0x10;  // cell+BR

		const unsigned char *cellData = cebk + ofsCells;
		const unsigned char *oamData = cellData + (ncer->nCells * perCellDataSize);

		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = &ncer->cells[i];

			int nOBJ           = *(const uint16_t *) (cellData + 0x00);
			int cellAttr       = *(const uint16_t *) (cellData + 0x02);
			uint32_t pOamAttrs = *(const uint32_t *) (cellData + 0x04);
			const uint16_t *cellOam = (const uint16_t *) (oamData + pOamAttrs);

			CellInitBankCell(ncer, cell, nOBJ);
			memcpy(cell->attr, oamData + pOamAttrs, cell->nAttribs * 3 * sizeof(uint16_t));
			cell->cellAttr = cellAttr;

			if (perCellDataSize >= 16) {
				//if the bounding box info exits
				cell->maxX = *(const int16_t *) (cellData + 0x8);
				cell->maxY = *(const int16_t *) (cellData + 0xA);
				cell->minX = *(const int16_t *) (cellData + 0xC);
				cell->minY = *(const int16_t *) (cellData + 0xE);
			}

			cellData += perCellDataSize;
		}

		//VRAM transfer
		if (ofsVramTran && ofsVramTran != 0xFFFFFFFF) {
			const unsigned char *vramTransferData = (cebk + ofsVramTran);
			uint32_t maxTransfer        = *(const uint32_t *) (vramTransferData + 0x0);
			uint32_t transferDataOffset = ofsVramTran + *(uint32_t *) (vramTransferData + 0x04);

			ncer->useVramTransferCharacters = 1;
			//with VRAM transfer characters, we will simulate an extended addressing space, using the
			//transfer source address as the base. The size is useful for the runtime but discarded here.
			for (int i = 0; i < ncer->nCells; i++) {
				NCER_CELL *cell = &ncer->cells[i];

				uint32_t srcAddr = *(const uint32_t *) (cebk + transferDataOffset + i * 8 + 0x00);
				unsigned int charNameBase = srcAddr / NCGR_BYTE_BOUNDARY(ncer->mappingMode);

				cell->exCharNames = (uint32_t *) calloc(cell->nAttribs, sizeof(uint32_t));
				for (int j = 0; j < cell->nAttribs; j++) {
					cell->exCharNames[j] = (cell->attr[j * 3 + 2] & 0x03FF) + charNameBase;
				}
			}
		}

		//user extended attributes
		if (ofsExtData) {
			const unsigned char *userEx = cebk + ofsExtData;
			
			//search for UCAT block
			if (userEx[0] == 'T' && userEx[1] == 'A' && userEx[2] == 'C' && userEx[3] == 'U') {
				userEx += 8;

				unsigned int nCellEx = *(const uint16_t *) (userEx + 0x0);
				uint32_t offsStart   = *(const uint32_t *) (userEx + 0x4);

				const uint32_t *attroffs = (const uint32_t *) (userEx + offsStart);
				for (unsigned int i = 0; i < nCellEx; i++) {
					ncer->cells[i].attrEx = *(const uint32_t *) (userEx + attroffs[i]);
				}
				ncer->useExtAttr = 1;
			}
		}
	}

	//NC label
	if (labl != NULL) {
		ncer->lablSize = lablSize;
		ncer->labl = calloc(lablSize + 1, 1);
		memcpy(ncer->labl, labl, lablSize);
	}

	//user extended
	if (uext != NULL) {
		ncer->uextSize = uextSize;
		ncer->uext = calloc(uextSize, 1);
		memcpy(ncer->uext, uext, uextSize);
	}

	return 0;
}

int CellReadGhostTrick(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	const uint16_t *cellOffs =  (const uint16_t *) buffer;
	int nCells               = *(const uint16_t *) buffer;
	NCER_CELL *cells = (NCER_CELL *) calloc(nCells, sizeof(NCER_CELL));

	for (int i = 0; i < nCells; i++) {
		const unsigned char *cell = buffer + cellOffs[i] * 2;
		int nObj = *(uint16_t *) cell;
		CellInitBankCell(ncer, &cells[i], nObj);

		memcpy(cells[i].attr, cell + 2, nObj * 3 * 2);
	}

	ncer->nCells = nCells;
	ncer->cells = cells;
	ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_128K;
	return 0;
}

static int CellReadSetosa(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	const unsigned char *cellBlock = SetGetBlock(buffer, size, "CELL");
	const unsigned char *cbexBlock = SetGetBlock(buffer, size, "CBEX");

	const unsigned char *block = cellBlock;
	if (block == NULL) block = cbexBlock;

	ncer->nCells = *(const uint32_t *) (block + 0x0);
	ncer->mappingMode = *(const uint32_t *) (block + 0x4);
	ncer->labl = NULL;
	ncer->lablSize = 0;
	ncer->uext = NULL;
	ncer->uextSize = 0;
	ncer->isEx2d = (block == cbexBlock);
	ncer->ex2dBaseMappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	if (ncer->isEx2d) {
		ncer->ex2dBaseMappingMode = ncer->mappingMode;
		ncer->mappingMode = GX_OBJVRAMMODE_CHAR_2D;
	}

	const unsigned char *dir = block + 8;

	//read cells
	ncer->cells = (NCER_CELL *) calloc(ncer->nCells, sizeof(NCER_CELL));
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = &ncer->cells[i];

		const unsigned char *celldat = SetResDirGetByIndex(dir, i);
		cell->minX = *(const int16_t *) (celldat + 0x00);
		cell->minY = *(const int16_t *) (celldat + 0x02);
		cell->maxX = *(const int16_t *) (celldat + 0x04);
		cell->maxY = *(const int16_t *) (celldat + 0x06);
		cell->nAttribs = *(const uint16_t *) (celldat + 0x08);
		cell->forbidCompression = ((*(const uint16_t *) (celldat + 0x0A)) >> 6) & 1;

		cell->attr = (uint16_t *) calloc(cell->nAttribs, 3 * sizeof(uint16_t));
		memcpy(cell->attr, celldat + 0xC, cell->nAttribs * 3 * sizeof(uint16_t));

		if (ncer->isEx2d) {
			const uint16_t *exAttr = (const uint16_t *) (celldat + 0xC + 6 * cell->nAttribs);
			cell->exCharNames = (uint32_t *) calloc(cell->nAttribs, sizeof(uint32_t));
			for (int j = 0; j < cell->nAttribs; j++) {
				cell->exCharNames[j] = (cell->attr[j * 3 + 2] & 0x03FF) | (exAttr[j] << 10);
			}
		}
	}

	return 0;
}

static int CellReadBomberman(NCER *ncer, const unsigned char *buffer, unsigned int size) {
	unsigned int unpackedSize;
	unsigned char *unpacked = BldtGetUncompressed(buffer, size, &unpackedSize);

	unsigned int ofs0 = *(const uint32_t *) (unpacked + 0x00);
	unsigned int ofsObj = *(const uint32_t *) (unpacked + 0x0C);

	//Check cell data
	unsigned int nCell = (ofs0 - 0x14) / 8;
	ncer->nCells = nCell;
	ncer->cells = (NCER_CELL *) calloc(nCell, sizeof(NCER_CELL));
	ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;

	//read cell data
	for (unsigned int i = 0; i < nCell; i++) {
		const unsigned char *cellInfo = unpacked + 0x14 + i * 8;
		unsigned int iObj = *(const uint16_t *) (cellInfo + 0x0);
		unsigned int nObj = *(const uint8_t *) (cellInfo + 0x2);

		unsigned int objStart = ofsObj + iObj * 0xC;
		const unsigned char *objData = unpacked + objStart;

		NCER_CELL *cell = &ncer->cells[i];
		cell->nAttribs = nObj;
		cell->attr = (uint16_t *) calloc(nObj, 3 * sizeof(uint16_t));

		for (unsigned int j = 0; j < nObj; j++) {
			const unsigned char *thisObj = objData + 0xC * j;

			//TODO
			int x = *(const int16_t *) (thisObj + 0x0);
			int y = *(const int16_t *) (thisObj + 0x2);
			unsigned int charName = *(const uint16_t *) (thisObj + 0x4);
			unsigned int flags = *(const uint16_t *) (thisObj + 0x6);

			unsigned int shape = (flags >> 0) & 0x3;
			unsigned int size = (flags >> 2) & 0x3;

			int width, height;
			CellGetObjDimensions(shape, size, &width, &height);

			cell->attr[j * 3 + 0] = (y & 0x00FF) | (shape << 14);
			cell->attr[j * 3 + 1] = (x & 0x01FF) | (size << 14);
			cell->attr[j * 3 + 2] = (charName & 0x03FF);
		}
	}

	free(unpacked);
	return OBJ_STATUS_SUCCESS;
}

void CellInitBankCell(NCER *ncer, NCER_CELL *cell, int nObj) {
	memset(cell, 0, sizeof(NCER_CELL));

	cell->nAttribs = nObj;
	cell->attr = (uint16_t *) calloc(nObj, 3 * sizeof(uint16_t));
}

void CellGetObjDimensions(int shape, int size, int *width, int *height) {
	//on hardware, shape=3 gives an 8x8 OBJ
	int widths [4][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, {  8,  8, 16, 32 }, { 8, 8, 8, 8 } };
	int heights[4][4] = { { 8, 16, 32, 64 }, {  8,  8, 16, 32 }, { 16, 32, 32, 64 }, { 8, 8, 8, 8 } };

	*width = widths[shape][size];
	*height = heights[shape][size];
}

int CellDecodeOamAttributes(GxOamAttrInfo *info, NCER_CELL *cell, int oam) {
	if (oam >= cell->nAttribs) {
		return 1;
	}

	uint16_t attr0 = cell->attr[oam * 3 + 0];
	uint16_t attr1 = cell->attr[oam * 3 + 1];
	uint16_t attr2 = cell->attr[oam * 3 + 2];

	info->x = attr1 & 0x1FF;
	info->y = attr0 & 0xFF;
	int shape = attr0 >> 14;
	int size = attr1 >> 14;

	CellGetObjDimensions(shape, size, &info->width, &info->height);
	info->size = size;
	info->shape = shape;

	info->characterName = cell->exCharNames != NULL ? cell->exCharNames[oam] : attr2 & 0x3FF;
	info->priority = (attr2 >> 10) & 0x3;
	info->palette = (attr2 >> 12) & 0xF;
	info->mode = (attr0 >> 10) & 3;
	info->mosaic = (attr0 >> 12) & 1;

	int rotateScale = (attr0 >> 8) & 1;
	info->rotateScale = rotateScale;
	if (rotateScale) {
		info->flipX = 0;
		info->flipY = 0;
		info->doubleSize = (attr0 >> 9) & 1;
		info->disable = 0;
		info->matrix = (attr1 >> 9) & 0x1F;
	} else {
		info->flipX = (attr1 >> 12) & 1;
		info->flipY = (attr1 >> 13) & 1;
		info->doubleSize = 0;
		info->disable = (attr0 >> 9) & 1;
		info->matrix = 0;
	}

	int is8 = (attr0 >> 13) & 1;
	info->characterBits = 4;
	if (is8) {
		info->characterBits = 8;
		//info->palette = 0;
		//info->characterName <<= 1;
	}

	return 0;
}

void CellDeleteCell(NCER *ncer, int idx) {
	memmove(ncer->cells + idx, ncer->cells + idx + 1, (ncer->nCells - idx - 1) * sizeof(NCER_CELL));

	ncer->nCells--;
	ncer->cells = (NCER_CELL *) realloc(ncer->cells, ncer->nCells * sizeof(NCER_CELL));
}

void CellMoveCellIndex(NCER *ncer, int iSrc, int iDst) {
	if (iSrc == iDst) return; // no-op

	//copy temporarily
	NCER_CELL cellTmp;
	memcpy(&cellTmp, ncer->cells + iSrc, sizeof(cellTmp));

	//slide over the source
	memmove(ncer->cells + iSrc, ncer->cells + iSrc + 1, (ncer->nCells - iSrc - 1) * sizeof(NCER_CELL));
	
	//adjust destination index to account for changed indices
	if (iDst > iSrc) iDst--;

	//move items to make space
	memmove(ncer->cells + iDst + 1, ncer->cells + iDst, (ncer->nCells - iDst - 1) * sizeof(NCER_CELL));

	//copy from temp
	memcpy(ncer->cells + iDst, &cellTmp, sizeof(cellTmp));
}

int CellFree(ObjHeader *header) {
	NCER *ncer = (NCER *) header;
	if (ncer->uext) free(ncer->uext);
	if (ncer->labl) free(ncer->labl);
	for (int i = 0; i < ncer->nCells; i++) {
		free(ncer->cells[i].attr);
		if (ncer->cells[i].exCharNames) free(ncer->cells[i].exCharNames);
	}
	if (ncer->cells) free(ncer->cells);
	
	
	return 0;
}

static int CellWriteNcer(NCER *ncer, BSTREAM *stream) {
	int cellSize = 8;
	if (ncer->bankAttribs & 1) cellSize = 16;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NCER", 1, 0, NNS_TYPE_G2D, NNS_SIG_LE);

	//write CEBK
	{
		unsigned char cebkHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		NnsStreamStartBlock(&nnsStream, "CEBK");

		//mapping mode
		uint32_t mappingMode = 0;
		switch (ncer->mappingMode) {
			case GX_OBJVRAMMODE_CHAR_1D_32K:
				mappingMode = 0; break;
			case GX_OBJVRAMMODE_CHAR_1D_64K:
				mappingMode = 1; break;
			case GX_OBJVRAMMODE_CHAR_1D_128K:
				mappingMode = 2; break;
			case GX_OBJVRAMMODE_CHAR_1D_256K:
				mappingMode = 3; break;
			case GX_OBJVRAMMODE_CHAR_2D:
				mappingMode = 4; break;
		}

		unsigned int mappingShift = (ncer->mappingMode >> 20) & 0x7;  // shift amount for mapping mode
		unsigned int charNameBoundary = 0x20 << mappingShift;         // size of one character name unit in bytes

		//construct the VRAM transfer info
		uint32_t *vramTransInfo = NULL;  // src:size pairs
		if (ncer->useVramTransferCharacters) {
			//the VRAM transfer information. First source addr, second size
			vramTransInfo = (uint32_t *) calloc(ncer->nCells, 2 * sizeof(uint32_t));
			for (int i = 0; i < ncer->nCells; i++) {
				NCER_CELL *cell = &ncer->cells[i];

				unsigned int chLo = UINT_MAX, chHi = 0;
				for (int j = 0; j < cell->nAttribs; j++) {
					GxOamAttrInfo info;
					CellDecodeOamAttributes(&info, cell, j);

					unsigned int chName = info.characterName << mappingShift;
					if (chName < chLo) chLo = chName;

					//get high character
					unsigned int nCharOBJ = info.width * info.height / 64;
					if ((chName + nCharOBJ) > chHi) chHi = chName + nCharOBJ;
				}

				//if lo==UINT_MAX, not found OBJ
				if (chLo == UINT_MAX) chLo = 0;

				//conversion of the character name into a VRAM address.
				uint32_t addr = chLo * 0x20;
				uint32_t size = (chHi - chLo) * 0x20;

				//TODO: is this strictly necessary?
				size = (size + charNameBoundary - 1) & ~(charNameBoundary - 1);

				vramTransInfo[i * 2 + 0] = addr;
				vramTransInfo[i * 2 + 1] = size;
			}
		}

		unsigned int offsVramTransfer = 0;
		unsigned int offsUserExtended = 0;
		if (ncer->useVramTransferCharacters) {
			offsVramTransfer = sizeof(cebkHeader) + ncer->nCells * cellSize;
			for (int i = 0; i < ncer->nCells; i++) offsVramTransfer += ncer->cells[i].nAttribs * 6;
			offsVramTransfer = (offsVramTransfer + 3) & ~3;
		}
		if (ncer->useExtAttr) {
			offsUserExtended += sizeof(cebkHeader);
			offsUserExtended += ncer->nCells * cellSize;
			for (int i = 0; i < ncer->nCells; i++) offsUserExtended += ncer->cells[i].nAttribs * 6;
			offsUserExtended = (offsUserExtended + 3) & ~3;
			if (ncer->useVramTransferCharacters) {
				//add size of VRAM transfer information
				offsUserExtended += 8 + 8 * ncer->nCells;
			}
		}

		*(uint16_t *) (cebkHeader + 0x00) = ncer->nCells;
		*(uint16_t *) (cebkHeader + 0x02) = ncer->bankAttribs;
		*(uint32_t *) (cebkHeader + 0x04) = sizeof(cebkHeader);
		*(uint32_t *) (cebkHeader + 0x08) = mappingMode;
		*(uint32_t *) (cebkHeader + 0x0C) = offsVramTransfer;
		*(uint32_t *) (cebkHeader + 0x14) = offsUserExtended;
		NnsStreamWrite(&nnsStream, cebkHeader, sizeof(cebkHeader));

		//write out each cell. Keep track of the offsets of OAM data.
		int oamOffset = 0;
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = &ncer->cells[i];
			unsigned char data[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

			*(uint16_t *) data = cell->nAttribs;
			*(uint16_t *) (data + 2) = cell->cellAttr;
			*(uint32_t *) (data + 4) = oamOffset;
			if (cellSize > 8) {
				*(int16_t *) (data + 0x08) = cell->maxX;
				*(int16_t *) (data + 0x0A) = cell->maxY;
				*(int16_t *) (data + 0x0C) = cell->minX;
				*(int16_t *) (data + 0x0E) = cell->minY;
			}

			NnsStreamWrite(&nnsStream, data, cellSize);
			oamOffset += cell->nAttribs * 3 * 2;
		}

		//write each cell's OAM attributes
		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = &ncer->cells[i];

			//get the VRAM transfer base addres in character name units
			uint32_t baseAddr = 0;
			if (vramTransInfo != NULL) {
				baseAddr = vramTransInfo[i * 2 + 0] / charNameBoundary;
			}

			//we must account for the extended attributes for when VRAM transfer animations exist.
			for (int j = 0; j < cell->nAttribs; j++) {
				uint16_t tmp[3];
				memcpy(tmp, &cell->attr[j * 3], sizeof(tmp));

				unsigned int chName = CellGetCharacterName(cell, j);
				tmp[2] = (tmp[2] & ~0x03FF) | ((chName - baseAddr) & 0x03FF);

				NnsStreamWrite(&nnsStream, tmp, sizeof(tmp));
			}
		}

		//write VRAM transfer character information
		if (ncer->useVramTransferCharacters) {
			NnsStreamAlign(&nnsStream, 4);

			uint32_t transMaxSize = 0;
			for (int i = 0; i < ncer->nCells; i++) {
				uint32_t size = vramTransInfo[i * 2 + 1];
				if (size > transMaxSize) transMaxSize = size;
			}

			uint32_t vramTransferHeader[2];
			vramTransferHeader[0] = transMaxSize;
			vramTransferHeader[1] = sizeof(vramTransferHeader); // offset to VRAM transfer info
			NnsStreamWrite(&nnsStream, vramTransferHeader, sizeof(vramTransferHeader));
			NnsStreamWrite(&nnsStream, vramTransInfo, ncer->nCells * 2 * sizeof(uint32_t));
		}
		free(vramTransInfo);

		//write user extended attribute data
		if (ncer->useExtAttr) {
			NnsStreamAlign(&nnsStream, 4);

			unsigned char extHeader[8];
			*(uint16_t *) (extHeader + 0x0) = ncer->nCells;
			*(uint16_t *) (extHeader + 0x2) = 1; // 1 attribute
			*(uint32_t *) (extHeader + 0x4) = sizeof(extHeader);

			uint32_t blockHeader[2];
			memcpy(blockHeader, "TACU", 4); // UCAT block
			blockHeader[1] = sizeof(blockHeader) + sizeof(extHeader) + ncer->nCells * 8;

			NnsStreamWrite(&nnsStream, blockHeader, sizeof(blockHeader));
			NnsStreamWrite(&nnsStream, extHeader, sizeof(extHeader));
			for (int i = 0; i < ncer->nCells; i++) {
				uint32_t offs = sizeof(extHeader) + 4 * ncer->nCells + 4 * i;
				NnsStreamWrite(&nnsStream, &offs, sizeof(offs));
			}
			for (int i = 0; i < ncer->nCells; i++) {
				uint32_t attr = ncer->cells[i].attrEx;
				NnsStreamWrite(&nnsStream, &attr, sizeof(attr));
			}
		}

		NnsStreamEndBlock(&nnsStream);
	}

	//write LABL block
	if (ncer->labl != NULL) {
		NnsStreamStartBlock(&nnsStream, "LABL");
		NnsStreamWrite(&nnsStream, ncer->labl, ncer->lablSize);
		NnsStreamEndBlock(&nnsStream);
	}

	//write UEXT block
	if (ncer->uext != NULL) {
		NnsStreamStartBlock(&nnsStream, "UEXT");
		NnsStreamWrite(&nnsStream, ncer->uext, ncer->uextSize);
		NnsStreamEndBlock(&nnsStream);
	}

	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);
	return 0;
}

static int CellWriteHudson(NCER *ncer, BSTREAM *stream) {
	bstreamWrite(stream, &ncer->nCells, 4);
	int ofs = 4 * ncer->nCells;
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		int attrsSize = cell->nAttribs * 0xA + 2;
		bstreamWrite(stream, &ofs, 4);
		ofs += attrsSize;
	}

	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		bstreamWrite(stream, &cell->nAttribs, 2);
		for (int j = 0; j < cell->nAttribs; j++) {
			GxOamAttrInfo info;
			CellDecodeOamAttributes(&info, cell, j);

			uint16_t pos[2];
			pos[0] = info.x;
			pos[1] = info.y;
			if (pos[0] & 0x100) {
				pos[0] |= 0xFE00;
			}
			if (pos[1] & 0x80) {
				pos[1] |= 0xFF00;
			}
			bstreamWrite(stream, cell->attr + j * 3, 6);
			bstreamWrite(stream, pos, 4);
		}
	}
	return 0;
}

static int CellWriteSetosa(NCER *ncer, BSTREAM *stream) {
	SetStream setStream;
	SetStreamCreate(&setStream);

	//the standard block signature is 'CELL'.
	//when extended 2D is used in intermediate files, we use the block signature 'CBEX'.
	unsigned int objSize = 0x6; // size of data for a single OBJ
	if (!ncer->isEx2d) {
		SetStreamStartBlock(&setStream, "CELL");
	} else {
		SetStreamStartBlock(&setStream, "CBEX");
		objSize += 2; // add extra uint16_t for high 16-bit of character name for a 26-bit field
	}

	//write header
	uint32_t header[2];
	header[0] = ncer->nCells;
	header[1] = ncer->isEx2d ? ncer->ex2dBaseMappingMode : ncer->mappingMode;
	SetStreamWrite(&setStream, header, sizeof(header));

	//build data directory
	SetResDirectory dir;
	SetResDirCreate(&dir, 0);
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;

		//get cell attributes
		int cellAffine = 0, commonPalette = 0;
		for (int j = 0; j < cell->nAttribs; j++) {
			GxOamAttrInfo info;
			CellDecodeOamAttributes(&info, cell, j);

			if (info.rotateScale) cellAffine = 1;
			if (j == 0) commonPalette = info.palette;
			else if (commonPalette != info.palette) commonPalette = -1;
		}

		//create OBJ data
		unsigned char *cellData = calloc(0xC + objSize * cell->nAttribs, 1);
		*(int16_t *) (cellData + 0x0) = cell->minX;
		*(int16_t *) (cellData + 0x2) = cell->minY;
		*(int16_t *) (cellData + 0x4) = cell->maxX;
		*(int16_t *) (cellData + 0x6) = cell->maxY;
		*(uint16_t *) (cellData + 0x8) = cell->nAttribs;
		*(uint16_t *) (cellData + 0xA) = (commonPalette & 0xF) | ((commonPalette != -1) << 4) | (cellAffine << 5) | ((!!cell->forbidCompression) << 6);
		memcpy(cellData + 0xC, cell->attr, cell->nAttribs * 0x6);

		if (ncer->isEx2d) {
			//fil in OBJ extended attributes
			uint16_t *pCellAttr = (uint16_t *) (cellData + 0xC);
			uint16_t *pCellExAttr = (uint16_t *) (cellData + 0xC + cell->nAttribs * 0x6);

			for (int j = 0; j < cell->nAttribs; j++) {
				pCellAttr[3 * j + 2] = (pCellAttr[3 * j + 2] & ~0x03FF) | (cell->exCharNames[j] & 0x03FF);
				pCellExAttr[j] = cell->exCharNames[j] >> 10;
			}
		}

		SetResDirAdd(&dir, NULL, cellData, 0xC + objSize * cell->nAttribs);
		free(cellData);
	}

	SetResDirFinalize(&dir);
	SetResDirFlushOut(&dir, &setStream);
	SetResDirFree(&dir);

	SetStreamEndBlock(&setStream);

	SetStreamFinalize(&setStream);
	SetStreamFlushOut(&setStream, stream);
	SetStreamFree(&setStream);

	return 0;
}

static int CellWriteBomberman(NCER *ncer, BSTREAM *stream) {
	//TODO
	__debugbreak();
	return OBJ_STATUS_UNSUPPORTED;
}

// ----- cell rendering


unsigned int CellGetCharacterName(NCER_CELL *cell, int i) {
	if (cell->exCharNames != NULL) return cell->exCharNames[i];
	else                           return cell->attr[i * 3 + 2] & 0x03FF;
}

static int FloatToInt(double x) {
	return (int) (x + (x < 0.0f ? -0.5f : 0.5f));
}

static void CellRenderOBJ_Character(COLOR32 *out, GxOamAttrInfo *info, NCGR *ncgr, NCLR *nclr, int mapping) {
	int tilesX = info->width / 8;
	int tilesY = info->height / 8;

	if (ncgr == NULL) {
		//null NCGR, render opaque coverage by OBJ
		COLOR32 fill = 0xFF000000;
		if (nclr != NULL && nclr->nColors >= 1) {
			fill = 0xFF000000 | ColorConvertFromDS(nclr->colors[0]);
			fill = REVERSE(fill);
		}
		for (int i = 0; i < (tilesX * tilesY * 8 * 8); i++) out[i] = fill;
		return;
	}

	int ncgrStart = NCGR_CHNAME(info->characterName, mapping, ncgr->nBits);
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			COLOR32 block[64];

			int bitsOffset = x * 8 + (y * 8 * tilesX * 8);
			int index;
			if (NCGR_2D(mapping)) {
				int ncx = x + ncgrStart % ncgr->tilesX;
				int ncy = y + ncgrStart / ncgr->tilesX;
				index = ncx + ncgr->tilesX * ncy;
			} else {
				index = ncgrStart + x + y * tilesX;
			}

			ChrRenderCharacter(ncgr, nclr, index, block, info->palette);
			for (int i = 0; i < 8; i++) {
				memcpy(out + bitsOffset + tilesX * 8 * i, block + i * 8, 32);
			}
		}
	}
}

static void CellRenderOBJ_Bitmap(COLOR32 *out, GxOamAttrInfo *info, NCGR *ncgr, NCLR *nclr, int mapping) {
	//if the mapping mode is 2D mapping, then we can use the same logic as for the character type rendering
	//since we emulate the graphics layout in character order.
	if (mapping == GX_OBJVRAMMODE_CHAR_2D) {
		CellRenderOBJ_Character(out, info, ncgr, nclr, mapping);
		return;
	}

	//the handling for 1D bitmap graphics: bitmaps are placed sequentially, rather than
	//assuming a 2D sheet layout.
	unsigned int mapShift = (mapping >> 20) & 0x3;
	unsigned int iPx = ((info->characterName * 64) << mapShift) >> (ncgr->nBits == 8);  // initial starting index of bitmap data
	unsigned int pxW = 8 * ncgr->tilesX;                                                // width of the simulated sheet in dots

	for (int y = 0; y < info->height; y++) {
		for (int x = 0; x < info->width; x++, iPx++) {
			
			//convert into the internal character order
			unsigned int pxX     = iPx % pxW, pxY     = iPx / pxW;
			unsigned int charX   = pxX / 8,   charY   = pxY / 8;
			unsigned int inCharX = pxX % 8,   inCharY = pxY % 8;
			unsigned int iChar   = charX + charY * ncgr->tilesX;

			//color index lookup
			unsigned int pval = 0;
			if (ncgr != NULL && iChar < (unsigned int) ncgr->nTiles) {
				pval = ncgr->tiles[iChar][inCharX + 8 * inCharY];
			}

			unsigned int cidx = pval + (info->palette << ncgr->nBits);

			//color palette lookup
			COLOR c = 0;
			if (nclr != NULL && cidx < (unsigned int) nclr->nColors) {
				c = nclr->colors[cidx];
			}

			COLOR32 c32 = ColorConvertFromDS(c);
			if (pval > 0) c32 |= 0xFF000000;
			out[x + y * info->width] = REVERSE(c32);
		}
	}
}

static void CellRenderOBJ(COLOR32 *out, GxOamAttrInfo *info, NCGR *ncgr, NCLR *nclr, int mapping) {
	//use the rendering procedure for the type of graphics
	if (ncgr == NULL || !ncgr->bitmap) {
		//character graphics (use on the 2D graphics engine)
		CellRenderOBJ_Character(out, info, ncgr, nclr, mapping);
	} else {
		//bitmap graphics (use on the 3D graphics engine)
		CellRenderOBJ_Bitmap(out, info, ncgr, nclr, mapping);
	}
}

void CellRender(
	COLOR32   *px,
	int       *covbuf,
	NCER      *ncer,
	NCGR      *ncgr,
	NCLR      *nclr,
	int        cellIndex,
	NCER_CELL *cell,
	int        xOffs,
	int        yOffs,
	double     a,
	double     b,
	double     c,
	double     d,
	int        forceAffine,
	int        forceDoubleSize
) {
	//adjust (X,Y) offset to center of preview
	xOffs += 256;
	yOffs += 128;

	//if cell is NULL, we use cell at cellInex.
	if (cell == NULL) {
		cell = &ncer->cells[cellIndex];
	}

	//compute inverse matrix parameters.
	double invA = 1.0, invB = 0.0, invC = 0.0, invD = 1.0;
	int isMtxIdentity = 1;
	if (a != 1.0 || b != 0.0 || c != 0.0 || d != 1.0) {
		//not identity matrix
		double det = a * d - b * c; // DBCA
		if (det != 0.0) {
			invA =  d / det;
			invB = -b / det;
			invC = -c / det;
			invD =  a / det;
		} else {
			//max scale identity
			invA = 127.99609375;
			invB =   0.0;
			invC =   0.0;
			invD = 127.99609375;
		}
		isMtxIdentity = 0; // not identity
	}

	COLOR32 *block = (COLOR32 *) calloc(64 * 64, sizeof(COLOR32));
	for (int i = cell->nAttribs - 1; i >= 0; i--) {
		GxOamAttrInfo info;
		CellDecodeOamAttributes(&info, cell, i);

		//if OBJ is marked disabled, skip rendering
		if (info.disable) continue;

		CellRenderOBJ(block, &info, ncgr, nclr, ncer->mappingMode);

		//HV flip? Only if not affine!
		if (!(info.rotateScale || forceAffine)) {
			COLOR32 temp[64];
			if (info.flipY) {
				for (int i = 0; i < info.height / 2; i++) {
					memcpy(temp, block + i * info.width, info.width * 4);
					memcpy(block + i * info.width, block + (info.height - 1 - i) * info.width, info.width * 4);
					memcpy(block + (info.height - 1 - i) * info.width, temp, info.width * 4);

				}
			}
			if (info.flipX) {
				for (int i = 0; i < info.width / 2; i++) {
					for (int j = 0; j < info.height; j++) {
						COLOR32 left = block[i + j * info.width];
						block[i + j * info.width] = block[info.width - 1 - i + j * info.width];
						block[info.width - 1 - i + j * info.width] = left;
					}
				}
			}
		}

		int doubleSize = info.doubleSize;
		if ((info.rotateScale || forceAffine) && forceDoubleSize) doubleSize = 1;

		//apply transformation matrix to OBJ position
		int x = SEXT9(info.x);
		int y = SEXT8(info.y);

		//when forcing double size on an OBJ that isn't naturally double size, we'll correct its position.
		if ((forceDoubleSize && (info.rotateScale || forceAffine)) && !info.doubleSize) {
			x -= info.width / 2;
			y -= info.height / 2;
		}

		if (!isMtxIdentity) {
			//adjust coordinates by correction for double-size
			int realWidth = info.width << doubleSize;
			int realHeight = info.height << doubleSize;
			int movedX = x + realWidth / 2;
			int movedY = y + realHeight / 2;

			//un-correct moved position from center to top-left, un-correct for double-size
			x = FloatToInt(movedX * a + movedY * b) - realWidth / 2;
			y = FloatToInt(movedX * c + movedY * d) - realHeight / 2;
		}

		//copy data
		if (!(info.rotateScale || forceAffine)) {
			//adjust for double size
			if (doubleSize) {
				x += info.width / 2;
				y += info.height / 2;
			}

			//no rotate/scale enabled, copy output directly.
			for (int j = 0; j < info.height; j++) {
				int _y = (y + j + yOffs) & 0xFF;
				for (int k = 0; k < info.width; k++) {
					int _x = (x + k + xOffs) & 0x1FF;
					COLOR32 col = block[j * info.width + k];
					if (col >> 24) {
						px[_x + _y * 512] = col;
						if (covbuf != NULL) covbuf[_x + _y * 512] = i + 1; // 0=no OBJ
					}
				}
			}
		} else {
			//transform about center
			int realWidth = info.width << doubleSize;
			int realHeight = info.height << doubleSize;
			double cx = (realWidth - 1) * 0.5; // rotation center X in OBJ
			double cy = (realHeight - 1) * 0.5; // rotation center Y in OBJ

			for (int j = 0; j < realHeight; j++) {
				int destY = (y + j + yOffs) & 0xFF;
				for (int k = 0; k < realWidth; k++) {
					int destX = (x + k + xOffs) & 0x1FF;

					int srcX = FloatToInt(((((double) k) - cx) * invA + (((double) j) - cy) * invB) + cx);
					int srcY = FloatToInt(((((double) k) - cx) * invC + (((double) j) - cy) * invD) + cy);

					//if double size, adjust source coordinate by the excess size
					if (doubleSize) {
						srcX -= realWidth / 4;
						srcY -= realHeight / 4;
					}

					if (srcX >= 0 && srcY >= 0 && srcX < info.width && srcY < info.height) {
						COLOR32 src = block[srcY * info.width + srcX];
						if (src >> 24) {
							px[destX + destY * 512] = src;
							if (covbuf != NULL) covbuf[destX + destY * 512] = i + 1; // 0=no OBJ
						}
					}

				}
			}
		}
	}
	free(block);
}



// ----- cell operations

void CellInsertOBJ(NCER *ncer, NCER_CELL *cell, int index, int nObj) {
	int nMove = cell->nAttribs - index;

	cell->nAttribs += nObj;
	cell->attr = realloc(cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));
	memmove(cell->attr + 3 * (index + nObj), cell->attr + 3 * index, nMove * 3 * sizeof(uint16_t));
	memset(cell->attr + 3 * index, 0, nObj * 3 * sizeof(uint16_t));

	if (ncer->useVramTransferCharacters || ncer->isEx2d) {
		cell->exCharNames = realloc(cell->exCharNames, cell->nAttribs * sizeof(uint32_t));
		memmove(cell->exCharNames + index + nObj, cell->exCharNames + index, nMove * sizeof(uint32_t));
		memset(cell->exCharNames + index, 0, nObj * sizeof(uint32_t));
	}
}

void CellDeleteOBJ(NCER *ncer, NCER_CELL *cell, int index, int nObj) {
	int nMove = cell->nAttribs - (index + nObj);

	cell->nAttribs -= nObj;

	memmove(cell->attr + (index) * 3, cell->attr + (index + nObj) * 3, nMove * 3 * sizeof(uint16_t));
	cell->attr = realloc(cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));

	if (ncer->useVramTransferCharacters || ncer->isEx2d) {
		memmove(cell->exCharNames + index, cell->exCharNames + index + nObj, nMove * sizeof(uint32_t));
		cell->exCharNames = realloc(cell->exCharNames, cell->nAttribs * sizeof(uint32_t));
	}
}

