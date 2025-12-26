#include <Windows.h>
#include "color.h"
#include "combo2d.h"
#include "nns.h"

#include "nclr.h"
#include "ncgr.h"
#include "ncer.h"
#include "nscr.h"
#include "nanr.h"

extern const wchar_t *gComboFormats[] = {
	L"Invalid",
	L"Time Ace",
	L"Banner",
	L"Data File",
	L"5BG",
	L"5BG OBJ",
	L"MBB",
	L"BNCD",
	L"AOB",
	L"GRF",
	NULL
};

typedef struct BANNER_INFO_ {
	int version;
	WCHAR titleJp[128];
	WCHAR titleEn[128];
	WCHAR titleFr[128];
	WCHAR titleGe[128];
	WCHAR titleIt[128];
	WCHAR titleSp[128];
	WCHAR titleCn[128];
	WCHAR titleHn[128];
} BANNER_INFO;

void combo2dInit(COMBO2D *combo, int format) {
	ObjInit(&combo->header, FILE_TYPE_COMBO2D, format);
	StListCreateInline(&combo->links, OBJECT_HEADER *, NULL);

	combo->header.writer = (OBJECT_WRITER) combo2dWrite;
	combo->header.dispose = combo2dFree;
}

int combo2dCount(COMBO2D *combo, int type) {
	int count = 0;
	for (unsigned int i = 0; i < combo->links.length; i++) {
		OBJECT_HEADER *header;
		StListGet(&combo->links, i, &header);
		if (header->type == type) count++;
	}
	return count;
}

OBJECT_HEADER *combo2dGet(COMBO2D *combo, int type, int index) {
	//keep track of number of objects of this type we've counted
	int nCounted = 0;
	for (unsigned int i = 0; i < combo->links.length; i++) {
		OBJECT_HEADER *object;
		StListGet(&combo->links, i, &object);
		if (object->type != type) continue;

		if (nCounted == index) return object;
		nCounted++;
	}
	return NULL;
}

void combo2dLink(COMBO2D *combo, OBJECT_HEADER *object) {
	StListAdd(&combo->links, &object);
	object->combo = (void *) combo;
}

void combo2dUnlink(COMBO2D *combo, OBJECT_HEADER *object) {
	object->combo = NULL;
	object->format = 0;
	for (unsigned int i = 0; i < combo->links.length; i++) {
		OBJECT_HEADER *objI;
		StListGet(&combo->links, i, &objI);
		if (objI != object) continue;

		//remove
		StListRemove(&combo->links, i);
		break;
	}

	//free combo if all links are freed
	if (combo->links.length == 0) {
		combo2dFree(combo);
		free(combo);
	}
}

int combo2dGetObjMinCount(int comboType, int objType) {
	switch (comboType) {
		case COMBO2D_TYPE_BANNER:
			//requires exactly one palette + one character
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			return 0;
		case COMBO2D_TYPE_5BG:
			//requires exactly one palette + one character + one screen (OBJ type not supported)
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_SCREEN) return 0;
			return 0;
		case COMBO2D_TYPE_5BG_OBJ:
			//requires exactly one palette + one character + one cell
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_CELL) return 1;
			return 0;
		case COMBO2D_TYPE_MBB:
			//requires exactly one palette, one character, and 0-4 screens
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_SCREEN) return 0;
			return 0;
		case COMBO2D_TYPE_TIMEACE:
			//requires exactly one palette, one character, one screen
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_SCREEN) return 1;
			return 0;
		case COMBO2D_TYPE_BNCD:
			//requires one charcater, one cell
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_CELL) return 1;
			return 0;
		case COMBO2D_TYPE_AOB:
			if (objType == FILE_TYPE_CELL) return 1;
			if (objType == FILE_TYPE_NANR) return 1;
			return 0;
		case COMBO2D_TYPE_GRF_BG:
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			return 0;
		case COMBO2D_TYPE_DATAFILE:
			//no particular requirements
			return 0;
	}
	return 0;
}

int combo2dGetObjMaxCount(int comboType, int objType) {
	switch (comboType) {
		case COMBO2D_TYPE_BANNER:
			//requires exactly one palette + one character
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			return 0;
		case COMBO2D_TYPE_5BG:
			//requires exactly one palette + one character + one screen (OBJ type not supported)
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_SCREEN) return 1;
			return 0;
		case COMBO2D_TYPE_5BG_OBJ:
			//requires exactly one palette + one character + one cell
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_CELL) return 1;
			return 0;
		case COMBO2D_TYPE_MBB:
			//requires exactly one palette, one character, and 0-4 screens
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_SCREEN) return 4;
		case COMBO2D_TYPE_TIMEACE:
			//requires exactly one palette, one character, one screen
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_SCREEN) return 1;
			return 0;
		case COMBO2D_TYPE_BNCD:
			//requires exactly one character, one cell
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_CELL) return 1;
			return 0;
		case COMBO2D_TYPE_AOB:
			if (objType == FILE_TYPE_CELL) return 1;
			if (objType == FILE_TYPE_NANR) return 1;
			return 0;
		case COMBO2D_TYPE_GRF_BG:
			if (objType == FILE_TYPE_PALETTE) return 1;
			if (objType == FILE_TYPE_CHARACTER) return 1;
			if (objType == FILE_TYPE_SCREEN) return 1;
			if (objType == FILE_TYPE_CELL) return 1;
			return 0;
		case COMBO2D_TYPE_DATAFILE:
			//no particular requirements
			return INT_MAX;
	}
	return 0;
}

int combo2dCanSave(COMBO2D *combo) {
	//count objects
	for (int type = 0; type < FILE_TYPE_MAX; type++) {
		int count = combo2dCount(combo, type);
		if (count > combo2dGetObjMaxCount(combo->header.format, type)) return 0;
		if (count < combo2dGetObjMinCount(combo->header.format, type)) return 0;
	}
	
	//object count requirements satisfied
	return 1;
}

void combo2dFree(COMBO2D *combo) {
	//free all links
	for (unsigned int i = 0; i < combo->links.length; i++) {
		OBJECT_HEADER *object;
		StListGet(&combo->links, i, &object);
		ObjFree(object);
		free(object);
	}
	StListFree(&combo->links);

	if (combo->extraData != NULL) {
		if (combo->header.format == COMBO2D_TYPE_DATAFILE) {
			DATAFILECOMBO *dfc = (DATAFILECOMBO *) combo->extraData;
			if (dfc->data != NULL) free(dfc->data);
			dfc->data = NULL;
		}
		free(combo->extraData);
		combo->extraData = NULL;
	}
}

int combo2dIsValidTimeAce(const unsigned char *file, unsigned int size) {
	//file must be big enough for 12 bytes plus palette (512 bytes) and screen (2048 bytes).
	if (size < 0xA0C) return 0;

	//validate fields
	int bitness = *(int *) file;
	if (bitness != 0 && bitness != 1) return 0;
	int nChars = *(int *) (file + 0xA08);
	int charSize = bitness ? 0x40 : 0x20;
	if (0xA0C + nChars * charSize != size) return 0;

	for (int i = 0; i < 256; i++) {
		COLOR c = ((COLOR *) (file + 4))[i];
		if (c & 0x8000) return 0;
	}
	return 1;
}

int combo2dIsValidMbb(const unsigned char *file, unsigned int size) {
	if (size < 0x74) return 0;

	//check that offsets aren't out of bounds
	uint32_t palofs = *(uint32_t *) (file + 0x00);
	uint32_t chrofs = *(uint32_t *) (file + 0x04);
	uint32_t *scrofs = (uint32_t *) (file + 0x08);
	if (palofs < 0x70 || palofs >= size) return 0;
	if (chrofs < 0x70 || chrofs >= size) return 0;

	//check palette size
	if (palofs + 0x200 > size) return 0; //palette always 256 colors

	//check that at least one screen is valid
	int scrBitmap = 0;
	for (int i = 0; i < 4; i++) {
		uint32_t scrnofs = scrofs[i];
		if (scrnofs >= size) return 0;

		if (scrnofs == 0) continue;
		scrBitmap |= 1 << i;
	}
	if (scrBitmap == 0) return 0;

	//validate screen datas
	for (int i = 0; i < 4; i++) {
		if (!(scrBitmap & (1 << i))) continue;

		uint16_t scrWidth = *(uint16_t *) (file + 0x18 + i * 0x10 + 0x8);
		uint16_t scrHeight = *(uint16_t *) (file + 0x18 + i * 0x10 + 0xA);
		if ((scrWidth & 7) || (scrHeight & 7) || (scrWidth == 0) || (scrHeight == 0)) return 0;

		int screenSize = (scrWidth / 8) * (scrHeight / 8) * 2;
		if (scrofs[i] + screenSize > size) return 0;
	}

	return 1;
}

int combo2dIsValidBanner(const unsigned char *file, unsigned int size) {
	if (size < 0x840) return 0;

	int version = *(unsigned short *) file;
	int crcA = *(unsigned short *) (file + 2);
	int crcB = *(unsigned short *) (file + 4);
	int crcC = *(unsigned short *) (file + 6);
	int crcD = *(unsigned short *) (file + 8);
	if (version != 1 && version != 2 && version != 3 && version != 0x0103) return 0;
	if (crcA != ObjComputeCrc16(file + 0x20, 0x820, 0xFFFF)) return 0;

	//at 0xA, 0x16 bytes should be 0.
	for (int i = 0; i < 0x16; i++) if (file[i + 0xA] != 0) return 0;

	COLOR *palette = (COLOR *) (file + 0x220);
	for (int i = 0; i < 16; i++) if (palette[i] & 0x8000) return 0;
	
	if (version == 1 && (size != 0x840 && size != 0xA00)) return 0;
	if (version == 2 && (size != 0x940 && size != 0xA00)) return 0;
	if (version == 3 && (size != 0xA40 && size != 0xC00)) return 0;

	return 1;
}

int combo2dIsValid5bg(const unsigned char *file, unsigned int size) {
	//must be a valid G2D structured file
	if (!NnsG2dIsValid(file, size)) return 0;
	if (memcmp(file, "NTBG", 4) != 0) return 0;

	//must have PALT section
	const unsigned char *palt = NnsG2dFindBlockBySignature(file, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	//must have BGDT section
	const unsigned char *bgdt = NnsG2dFindBlockBySignature(file, size, "BGDT", NNS_SIG_BE, NULL);
	if (bgdt == NULL) return 0;

	//may have DFPL section
	return 1;
}

int combo2dIsValid5bgObj(const unsigned char *file, unsigned int size) {
	//must be a valid G2D structured file
	if (!NnsG2dIsValid(file, size)) return 0;
	if (memcmp(file, "NTBG", 4) != 0) return 0;

	//must have PALT section
	const unsigned char *palt = NnsG2dFindBlockBySignature(file, size, "PALT", NNS_SIG_BE, NULL);
	if (palt == NULL) return 0;

	//must have BGDT section
	const unsigned char *objd = NnsG2dFindBlockBySignature(file, size, "OBJD", NNS_SIG_BE, NULL);
	if (objd == NULL) return 0;

	return 1;
}

static int combo2dBncdGetBitDepth(const unsigned char *file) {
	//count OBJ bit depths
	int depth = 4;

	uint16_t nCell = *(uint16_t *) (file + 0x06);
	uint32_t offsCell = *(uint32_t *) (file + 0x08);
	uint32_t offsObj = *(uint32_t *) (file + 0x0C);
	for (unsigned int i = 0; i < nCell; i++) {
		const unsigned char *cellInfo = file + offsCell + i * 0x6;
		uint16_t objIndex = *(uint16_t *) (cellInfo + 0x2);
		uint16_t nObj = *(uint16_t *) (cellInfo + 0x4);

		unsigned int thisCellObjOffset = offsObj + objIndex * 0xC;
		const unsigned char *cellObj = file + thisCellObjOffset;
		for (unsigned int j = 0; j < nObj; j++) {
			const unsigned char *thisObj = cellObj + j * 0xC;
			const uint16_t *attr = (const uint16_t *) thisObj;
			uint16_t attr0 = attr[0];
			
			if (attr0 & 0x2000) depth = 8;
		}
	}

	return depth;
}

static int combo2dBncdGetMappingMode(const unsigned char *file) {
	uint32_t mappingShift = *(uint32_t *) (file + 0x18);
	if (mappingShift > 3) return -1;

	const int mappings[] = {
		GX_OBJVRAMMODE_CHAR_1D_32K,
		GX_OBJVRAMMODE_CHAR_1D_64K,
		GX_OBJVRAMMODE_CHAR_1D_128K,
		GX_OBJVRAMMODE_CHAR_1D_256K
	};
	return mappings[mappingShift];
}

int combo2dIsValidBncd(const unsigned char *file, unsigned int size) {
	if (size < 0x1C) return 0; // size of header
	if (memcmp(file, "JNCD", 4) != 0) return 0;

	uint16_t ver = *(uint16_t *) (file + 0x04);
	uint16_t nCell = *(uint16_t *) (file + 0x06);
	uint32_t offsCell = *(uint32_t *) (file + 0x08);
	uint32_t offsObj = *(uint32_t *) (file + 0x0C);
	uint32_t offsChar = *(uint32_t *) (file + 0x10);
	uint32_t sizeChar = *(uint32_t *) (file + 0x14);
	uint32_t mappingShift = *(uint32_t *) (file + 0x18);

	if (offsCell > size) return 0;
	if ((offsCell + nCell * 0x6) > size) return 0;
	if (offsObj > size) return 0;
	if (offsChar > size) return 0;
	if ((offsChar + sizeChar) > size) return 0;
	if (mappingShift > 3) return 0;

	//verify cell and OBJ data
	for (unsigned i = 0; i < nCell; i++) {
		const unsigned char *cellInfo = file + offsCell + i * 0x6;
		uint16_t objIndex = *(uint16_t *) (cellInfo + 0x2);
		uint16_t nObj = *(uint16_t *) (cellInfo + 0x4);

		unsigned int thisCellObjOffset = offsObj + objIndex * 0xC;
		if ((thisCellObjOffset + nObj * 0xC) > size) return 0;
	}

	return 1;
}

static int combo2dIsValidAob(const unsigned char *file, unsigned int size) {
	if (!IscadIsValidFooter(file, size)) return 0;

	unsigned int agbSize, animSize, grpSize;
	const unsigned char *agb = IscadFindBlockBySignature(file, size, "AGB ", &agbSize);
	const unsigned char *anim = IscadFindBlockBySignature(file, size, "ANIM", &animSize);
	const unsigned char *grp = IscadFindBlockBySignature(file, size, "GRP ", &grpSize);
	if (agb == NULL || anim == NULL || grp == NULL) return 0;

	return 1;
}

static int combo2dIsValidGrf(const unsigned char *buffer, unsigned int size) {
	int type = GrfIsValid(buffer, size);
	if (type == GRF_TYPE_INVALID) return 0;

	unsigned int palSize, gfxSize, scrSize, headerSize;
	unsigned char *gfx = GrfReadBlockUncompressed(buffer, size, "GFX ", &gfxSize);
	unsigned char *pal = GrfReadBlockUncompressed(buffer, size, "PAL ", &palSize);
	unsigned char *scr = GrfReadBlockUncompressed(buffer, size, "MAP ", &scrSize);
	unsigned char *hdr = GrfGetHeader(buffer, size, &headerSize);

	int valid = 0;
	if (hdr == NULL || gfx == NULL || pal == NULL) goto Invalid;

	int gfxAttr = *(const uint16_t *) (hdr + 0x2);
	int scrType = *(const uint16_t *) (hdr + 0x4);
	if (gfxAttr != 4 && gfxAttr != 8) goto Invalid;
	if (scrType > 4 || (scrType != 0 && scr == NULL)) goto Invalid;

	valid = 1;
Invalid:
	if (gfx != NULL) free(gfx);
	if (pal != NULL) free(pal);
	if (scr != NULL) free(scr);
	return valid;
}

int combo2dIsValid(const unsigned char *file, unsigned int size) {
	if (combo2dIsValid5bg(file, size)) return COMBO2D_TYPE_5BG;
	if (combo2dIsValid5bgObj(file, size)) return COMBO2D_TYPE_5BG_OBJ;
	if (combo2dIsValidGrf(file, size)) return COMBO2D_TYPE_GRF_BG;
	if (combo2dIsValidAob(file, size)) return COMBO2D_TYPE_AOB;
	if (combo2dIsValidBncd(file, size)) return COMBO2D_TYPE_BNCD;
	if (combo2dIsValidTimeAce(file, size)) return COMBO2D_TYPE_TIMEACE;
	if (combo2dIsValidBanner(file, size)) return COMBO2D_TYPE_BANNER;
	if (combo2dIsValidMbb(file, size)) return COMBO2D_TYPE_MBB;
	return 0;
}

int combo2dReadTimeAce(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	//add palette
	NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
	PalInit(nclr, NCLR_TYPE_COMBO);
	nclr->nColors = 256;
	nclr->extPalette = 0;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(256, sizeof(COLOR));
	memcpy(nclr->colors, buffer + 4, 512);
	combo2dLink(combo, &nclr->header);

	//add character
	NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
	ChrInit(ncgr, NCGR_TYPE_COMBO);
	ncgr->nTiles = *(int *) (buffer + 0xA08);
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_2D;
	ncgr->nBits = *(int *) buffer == 0 ? 4 : 8;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ChrReadChars(ncgr, buffer + 0xA0C);
	combo2dLink(combo, &ncgr->header);

	//add screen
	NSCR *nscr = (NSCR *) calloc(1, sizeof(NSCR));
	ScrInit(nscr, NSCR_TYPE_COMBO);
	nscr->tilesX = 256 / 8;
	nscr->tilesY = 256 / 8;
	nscr->dataSize = nscr->tilesX * nscr->tilesY * 2;
	nscr->data = (uint16_t *) calloc(nscr->dataSize, 1);
	memcpy(nscr->data, buffer + 0x208, nscr->dataSize);
	ScrComputeHighestCharacter(nscr);
	combo2dLink(combo, &nscr->header);

	return 0;
}

int combo2dReadBncd(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	uint16_t ver = *(uint16_t *) (buffer + 0x04);
	uint16_t nCell = *(uint16_t *) (buffer + 0x06);
	uint32_t offsCell = *(uint32_t *) (buffer + 0x08);
	uint32_t offsObj = *(uint32_t *) (buffer + 0x0C);
	uint32_t offsChar = *(uint32_t *) (buffer + 0x10);
	uint32_t sizeChar = *(uint32_t *) (buffer + 0x14);

	NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
	NCER *ncer = (NCER *) calloc(1, sizeof(NCER));
	ChrInit(ncgr, NCGR_TYPE_COMBO);
	CellInit(ncer, NCER_TYPE_COMBO);

	int depth = combo2dBncdGetBitDepth(buffer);
	int mappingMode = combo2dBncdGetMappingMode(buffer);

	NCER_CELL *cells = (NCER_CELL *) calloc(nCell, sizeof(NCER_CELL));

	const unsigned char *objData = buffer + offsObj;

	unsigned int i;
	for (i = 0; i < nCell; i++) {
		const unsigned char *cellInfo = buffer + offsCell + i * 0x6;
		uint16_t objIndex = *(uint16_t *) (cellInfo + 0x2);
		uint16_t nObj = *(uint16_t *) (cellInfo + 0x4);
		const unsigned char *cellObj = objData + objIndex * 0xC;

		NCER_CELL *cell = cells + i;
		CellInitBankCell(ncer, cell, nObj);
		cell->maxX = cell->minX + *(uint8_t *) (cellInfo + 0x0);
		cell->maxY = cell->minY + *(uint8_t *) (cellInfo + 0x1);
		for (unsigned int j = 0; j < nObj; j++) {
			const unsigned char *thisObj = cellObj + j * 0xC;
			memcpy(cell->attr + j * 3, thisObj, 6);

			//OBJ character name must be relocated
			uint16_t attr2 = cell->attr[j * 3 + 2];
			cell->attr[j * 3 + 2] = (attr2 & ~0x3FF) | (*(uint16_t *) (thisObj + 0x8) & 0x3FF);
		}
	}

	//init graphics
	unsigned int nTiles = sizeChar / (8 * depth);
	ncgr->nBits = depth;
	ncgr->mappingMode = mappingMode;
	ncgr->nTiles = nTiles;
	ncgr->tilesX = ChrGuessWidth(nTiles);
	ncgr->tilesY = nTiles / ncgr->tilesX;
	ChrReadChars(ncgr, buffer + offsChar);

	//init cells
	ncer->nCells = 0;
	ncer->mappingMode = mappingMode;
	ncer->nCells = nCell;
	ncer->cells = cells;

	combo2dLink(combo, &ncgr->header);
	combo2dLink(combo, &ncer->header);
	return 0;
}

int combo2dRead5bg(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	const unsigned char *bgdt = NnsG2dFindBlockBySignature(buffer, size, "BGDT", NNS_SIG_BE, NULL);
	const unsigned char *dfpl = NnsG2dFindBlockBySignature(buffer, size, "DFPL", NNS_SIG_BE, NULL);

	int nColors = *(uint32_t *) (palt + 0x00);

	int chrWidth = *(uint16_t *) (bgdt + 0xC);
	int chrHeight = *(uint16_t *) (bgdt + 0xE);
	int scrSize = *(uint32_t *) (bgdt + 0x04);
	int mapping = *(uint32_t *) (bgdt + 0x00);
	int charSize = *(uint32_t *) (bgdt + 0x10);
	int isBitmap = scrSize == 0; // screen size=0 => bitmap mode BG
	int charOffset = 0x14 + scrSize;
	int nBits = dfpl == NULL ? 8 : 4; //8-bit if no DFPL present

	int scrX = *(uint16_t *) (bgdt + 0x8);
	int scrY = *(uint16_t *) (bgdt + 0xA);
	int scrDataSize = scrX * scrY * 2;

	//addpalette
	NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
	PalInit(nclr, NCLR_TYPE_COMBO);
	nclr->nColors = nColors;
	nclr->extPalette = 0;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
	memcpy(nclr->colors, palt + 0x4, nColors * sizeof(COLOR));
	combo2dLink(combo, &nclr->header);

	//add character
	NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
	ChrInit(ncgr, NCGR_TYPE_COMBO);
	ncgr->nTiles = chrWidth * chrHeight;
	ncgr->tilesX = chrWidth;
	ncgr->tilesY = chrHeight;
	ncgr->nBits = nBits;
	ncgr->bitmap = isBitmap;
	ncgr->mappingMode = mapping;
	ChrReadGraphics(ncgr, bgdt + charOffset);
	combo2dLink(combo, &ncgr->header);

	if (!isBitmap) {
		//add screen (only if not bitmap mode BG)
		NSCR *nscr = (NSCR *) calloc(1, sizeof(NSCR));
		ScrInit(nscr, NSCR_TYPE_COMBO);
		nscr->tilesX = scrX;
		nscr->tilesY = scrY;
		nscr->dataSize = scrDataSize;
		nscr->data = (uint16_t *) calloc(scrDataSize, 1);
		memcpy(nscr->data, bgdt + 0x14, scrDataSize);
		ScrComputeHighestCharacter(nscr);
		combo2dLink(combo, &nscr->header);
	}

	return 0;
}

int combo2dRead5bgObj(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	const unsigned char *palt = NnsG2dFindBlockBySignature(buffer, size, "PALT", NNS_SIG_BE, NULL);
	const unsigned char *objd = NnsG2dFindBlockBySignature(buffer, size, "OBJD", NNS_SIG_BE, NULL);
	const unsigned char *dfpl = NnsG2dFindBlockBySignature(buffer, size, "DFPL", NNS_SIG_BE, NULL);

	int nBits = dfpl == NULL ? 8 : 4; //8-bit if no DFPL present
	int nColors = *(uint32_t *) (palt + 0x00);

	uint32_t objParam = *(const uint32_t *) (objd + 0x00);
	uint32_t charSize = *(const uint32_t *) (objd + 0x14);
	int nObjX = *(const uint16_t *) (objd + 0x10);
	int nObjY = *(const uint16_t *) (objd + 0x12);

	int objShape = (objParam >> 10) & 0x3;
	int objSize = (objParam >> 8) & 0x3;
	int objWidth, objHeight;
	CellGetObjDimensions(objShape, objSize, &objWidth, &objHeight);
	objWidth /= 8;
	objHeight /= 8;

	int chrWidth = nObjX * objWidth;
	int chrHeight = nObjY * objHeight;
	int nObj = nObjX * nObjY;
	int charOffset = 0x18 + 4 * nObj;

	//get size of character data
	int nCharsX, nCharsY;
	if (charSize != 0) {
		//guess size
		int nChars = charSize / (nBits * 8);
		nCharsX = ChrGuessWidth(nChars);
		nCharsY = nChars / nCharsX;
	} else {
		//size is same as indicated by the image dimension
		nCharsX = chrWidth;
		nCharsY = chrHeight;
	}
	int nChars = nCharsX * nCharsY;

	const uint32_t *extAttr = (const uint32_t *) (objd + 0x18);

	//create attribute and character data. The format stores character data 1D mapped.
	uint16_t *cellAttr = (uint16_t *) calloc(nObj, 3 * sizeof(uint16_t));
	unsigned char *charAttr = (unsigned char *) calloc(nChars, 1);
	if (dfpl != NULL) {
		//write attribute data (stored per-OBJ)
		for (int i = 0; i < nChars / (objWidth * objHeight); i++) {
			int pltt = (dfpl + 4)[i];
			for (int j = 0; j < objWidth * objHeight; j++) {
				charAttr[i * objWidth * objHeight + j] = pltt;
			}
		}
	}

	for (int y = 0; y < nObjY; y++) {
		for (int x = 0; x < nObjX; x++) {
			uint32_t thisAttr = extAttr[x + y * nObjX];
			unsigned int charName = thisAttr & 0x0000FFFF;
			int mtxno  = (thisAttr >> 16) & 0x1F;
			int hFlip  = (thisAttr >> 21) & 0x01;
			int vFlip  = (thisAttr >> 22) & 0x01;
			int affine = (thisAttr >> 23) & 0x01;
			int dsize  = (thisAttr >> 24) & 0x01;
			int prio   = (thisAttr >> 25) & 0x03;
			int mosaic = (thisAttr >> 27) & 0x01;
			int plttno = (thisAttr >> 28) & 0x0F;

			int objX = x * objWidth * 8 - 256;
			int objY = y * objHeight * 8 - 128;
			if (affine && dsize) {
				//adjust position to fit into grid when double size affine is enabled
				objX -= objWidth * 8 / 2;
				objY -= objHeight * 8 / 2;
			}

			uint16_t *obj = cellAttr + 3 * (x + y * nObjX);
			obj[0] = (objY & 0x00FF) | (objShape << 14) | ((nBits == 8) << 13) | (affine << 8) | (dsize << 9) | (mosaic << 12);
			obj[1] = (objX & 0x01FF) | (objSize << 14) | (hFlip << 12) | (vFlip << 13) | (mtxno << 9);
			obj[2] = (charName & 0x03FF) | (prio << 10) | (plttno << 12);
		}
	}

	//addpalette
	NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
	PalInit(nclr, NCLR_TYPE_COMBO);
	nclr->nColors = nColors;
	nclr->extPalette = 0;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
	memcpy(nclr->colors, palt + 0x4, nColors * sizeof(COLOR));
	combo2dLink(combo, &nclr->header);

	//add character
	NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
	ChrInit(ncgr, NCGR_TYPE_COMBO);
	ncgr->nTiles = nChars;
	ncgr->tilesX = nCharsX;
	ncgr->tilesY = nCharsY;
	ncgr->nBits = nBits;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncgr->attr = charAttr;
	ChrReadChars(ncgr, objd + charOffset);
	combo2dLink(combo, &ncgr->header);

	NCER *ncer = (NCER *) calloc(1, sizeof(NCER));
	CellInit(ncer, NCER_TYPE_COMBO);
	ncer->cells = (NCER_CELL *) calloc(1, sizeof(NCER_CELL));
	ncer->nCells = 1;
	ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ncer->cells[0].nAttribs = nObj;
	ncer->cells[0].attr = cellAttr;
	combo2dLink(combo, &ncer->header);

	return OBJ_STATUS_SUCCESS;
}

int combo2dReadBanner(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	BANNER_INFO *info = (BANNER_INFO *) calloc(1, sizeof(BANNER_INFO));
	combo->extraData = (void *) info;
	info->version = *(unsigned short *) buffer;
	memcpy(info->titleJp, buffer + 0x240, 0x600);
	if (info->version >= 2) memcpy(info->titleCn, buffer + 0x840, 0x100);
	if (info->version >= 3) memcpy(info->titleHn, buffer + 0x940, 0x100);

	//add palette
	NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
	PalInit(nclr, NCLR_TYPE_COMBO);
	nclr->nColors = 16;
	nclr->extPalette = 0;
	nclr->nBits = 4;
	nclr->colors = (COLOR *) calloc(16, sizeof(COLOR));
	memcpy(nclr->colors, buffer + 0x220, 32);
	combo2dLink(combo, &nclr->header);

	//add character
	NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
	ChrInit(ncgr, FILE_TYPE_CHARACTER);
	ncgr->nTiles = 16;
	ncgr->tilesX = 4;
	ncgr->tilesY = 4;
	ncgr->nBits = 4;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ChrReadChars(ncgr, buffer + 0x20);
	combo2dLink(combo, &ncgr->header);

	return 0;
}

int combo2dReadMbb(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	MBBCOMBO *mbbInfo = (MBBCOMBO *) calloc(1, sizeof(MBBCOMBO));
	combo->extraData = mbbInfo;
	mbbInfo->screenBitmap = 0;

	uint32_t palofs = *(uint32_t *) (buffer + 0x00);
	uint32_t chrofs = *(uint32_t *) (buffer + 0x04);
	uint32_t charSize = *(uint16_t *) (buffer + 0x60);
	uint32_t *scrofs = (uint32_t *) (buffer + 0x08);

	int nBits = (((unsigned char) buffer[0x59]) == 0x80) ? 8 : 4;
	int nChars = (charSize * 0x20) / (8 * nBits);

	//add palette
	NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
	PalInit(nclr, NCLR_TYPE_COMBO);
	nclr->nColors = 256;
	nclr->extPalette = 0;
	nclr->nBits = nBits;
	nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
	memcpy(nclr->colors, buffer + palofs, nclr->nColors * sizeof(COLOR));
	combo2dLink(combo, &nclr->header);

	//add character
	NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
	ChrInit(ncgr, NCGR_TYPE_COMBO);
	ncgr->nBits = nBits;
	ncgr->nTiles = nChars;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ChrReadChars(ncgr, buffer + chrofs);
	combo2dLink(combo, &ncgr->header);

	//add screen
	for (int i = 0; i < 4; i++) {
		uint32_t scrnofs = scrofs[i];
		if (scrnofs == 0) continue;

		int scrWidth = *(uint16_t *) (buffer + 0x18 + i * 0x10 + 0x8);
		int scrHeight = *(uint16_t *) (buffer + 0x18 + i * 0x10 + 0xA);
		int scrDataSize = (scrWidth / 8) * (scrHeight / 8) * 2;

		//add screen
		NSCR *nscr = (NSCR *) calloc(1, sizeof(NSCR));
		ScrInit(nscr, NSCR_TYPE_COMBO);
		nscr->tilesX = scrWidth / 8;
		nscr->tilesY = scrHeight / 8;
		nscr->dataSize = scrDataSize;
		nscr->data = (uint16_t *) calloc(scrDataSize, 1);
		memcpy(nscr->data, buffer + scrnofs, scrDataSize);
		ScrComputeHighestCharacter(nscr);
		combo2dLink(combo, &nscr->header);

		mbbInfo->screenBitmap |= (1 << i);
	}

	return 0;
}

static int combo2dReadAob(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	unsigned int agbSize, animSize, achrSize, objSize, grpSize, cmntSize, anmcSize, p2dmSize, ccamSize, linkSize;
	unsigned char *agb = IscadFindBlockBySignature(buffer, size, "AGB ", &agbSize);  // read
	unsigned char *anim = IscadFindBlockBySignature(buffer, size, "ANIM", &animSize);
	unsigned char *achr = IscadFindBlockBySignature(buffer, size, "ACHR", &achrSize);
	unsigned char *obj = IscadFindBlockBySignature(buffer, size, "OBJ ", &objSize);
	unsigned char *grp = IscadFindBlockBySignature(buffer, size, "GRP ", &grpSize);
	unsigned char *cmnt = IscadFindBlockBySignature(buffer, size, "CMNT", &cmntSize); // read
	unsigned char *anmc = IscadFindBlockBySignature(buffer, size, "ANMC", &anmcSize); // read
	unsigned char *p2dm = IscadFindBlockBySignature(buffer, size, "2DM ", &p2dmSize); // read
	unsigned char *ccam = IscadFindBlockBySignature(buffer, size, "CCAM", &ccamSize);
	unsigned char *link = IscadFindBlockBySignature(buffer, size, "LINK", &linkSize); // read

	/*
	TODO: handling character compressed animation mode
		byte 0 of CCAM will be set nonzero, and an ACHR block will be present with 1D compressed character data
		OBJ block may be present
	*/

	int mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	if (p2dm != NULL && p2dmSize > 0 && *p2dm) mappingMode = GX_OBJVRAMMODE_CHAR_2D;

	unsigned int nObjEntry = *(const unsigned char *) (agb + 0x0);

	NCER *ncer = (NCER *) calloc(1, sizeof(NCER));
	CellInit(ncer, NCER_TYPE_COMBO);
	ncer->mappingMode = mappingMode;
	ncer->nCells = nObjEntry;
	ncer->cells = (NCER_CELL *) calloc(ncer->nCells, sizeof(NCER_CELL));
	if (cmnt != NULL) {
		unsigned int commentLength = cmnt[1];
		ncer->header.comment = calloc(commentLength + 1, 1);
		memcpy(ncer->header.comment, cmnt + 2, commentLength);
	}

	//read cell OBJ data
	const unsigned char *pCell = agb + 1;
	ANIM_DATA_SRT *trans = (ANIM_DATA_SRT *) calloc(nObjEntry, sizeof(ANIM_DATA_SRT));
	for (unsigned int i = 0; i < nObjEntry; i++) {
		trans[i].sx = trans[i].sy = 4096;
	}

	for (unsigned int curCellCount = 0; curCellCount < nObjEntry;) {
		uint16_t nObj = *(const uint16_t *) (pCell + 0);
		pCell += 2;

		if (nObj & 0x8000) {
			//msb=1: SRT data
			nObj &= ~0x8000;

			for (unsigned int j = 0; j < nObj; j++) {
				int deg = *(const int16_t *) (pCell + 0);
				int grad = (deg * 65536 + (deg >= 0 ? 180 : -180)) / 360;

				//TODO: support multiple transforms per cell?
				trans[curCellCount].rotZ = grad;
				trans[curCellCount].sx = (*(const int16_t *) (pCell + 2)) * 16; // 8-bit fraction -> 12-bit fraction
				trans[curCellCount].sy = (*(const int16_t *) (pCell + 4)) * 16; // 8-bit fraction -> 12-bit fraction
				pCell += 0x6;
			}
		} else {
			//msb=0: OBJ data
			int cellI = curCellCount;

			ncer->cells[cellI].nAttribs = nObj;
			ncer->cells[cellI].attr = (uint16_t *) calloc(nObj, 6);

			for (unsigned int j = 0; j < nObj; j++) {
				memcpy(ncer->cells[cellI].attr + j * 3, pCell, 6);

				pCell += 0x6;
			}
			curCellCount++;
		}

	}

	NANR *nanr = (NANR *) calloc(1, sizeof(NANR));
	AnmInit(nanr, NANR_TYPE_COMBO);

	//read animation sequences
	unsigned int nSeq = *(anim++);
	
	nanr->nSequences = nSeq;
	nanr->sequences = (NANR_SEQUENCE *) calloc(nanr->nSequences, sizeof(NANR_SEQUENCE));

	for (unsigned int i = 0; i < nSeq; i++) {
		//sequence properties
		NANR_SEQUENCE *seq = &nanr->sequences[i];
		int offsX = *(const int16_t *) (anim + 0x0);
		int offsY = *(const int16_t *) (anim + 0x2);
		unsigned int nFrame = *(const uint8_t *) (anim + 0x4);
		anim += 0x5;

		seq->startFrameIndex = 0;
		seq->type = (NANR_SEQ_TYPE_CELL << 16) | NANR_SEQ_TYPE_INDEX_SRT;
		seq->mode = NANR_SEQ_MODE_FORWARD_LOOP;
		seq->nFrames = nFrame;
		seq->frames = (FRAME_DATA *) calloc(seq->nFrames, sizeof(FRAME_DATA));

		for (unsigned int j = 0; j < nFrame; j++) {
			unsigned int cellno = *(const uint16_t *) (anim + 0x0);
			unsigned int dur = *(const uint16_t *) (anim + 0x2);

			seq->frames[j].nFrames = dur;
			seq->frames[j].animationData = calloc(1, sizeof(ANIM_DATA_SRT));

			ANIM_DATA_SRT *pSrt = (ANIM_DATA_SRT *) seq->frames[j].animationData;
			pSrt->index = cellno;
			pSrt->px = offsX;
			pSrt->py = offsY;
			pSrt->sx = trans[cellno].sx;
			pSrt->sy = trans[cellno].sy;
			pSrt->rotZ = trans[cellno].rotZ;

			anim += 4;
		}
	}
	free(trans);

	if (anmc != NULL) {
		unsigned int commentLength = anmc[1];
		nanr->header.comment = calloc(commentLength + 1, 1);
		memcpy(nanr->header.comment, anmc + 2, commentLength);
	}

	combo2dLink(combo, &ncer->header);
	combo2dLink(combo, &nanr->header);

	if (link != NULL) {
		unsigned int linkLength = link[1];
		combo->header.fileLink = calloc(linkLength + 1, 1);
		memcpy(combo->header.fileLink, link + 2, linkLength);
	}

	return 0;
}

static int combo2dReadGrf(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	//int type = GrfIsValid(buffer, size);

	unsigned int palSize, gfxSize, scrSize, objSize, headerSize;
	unsigned char *gfx = GrfReadBlockUncompressed(buffer, size, "GFX ", &gfxSize);
	unsigned char *pal = GrfReadBlockUncompressed(buffer, size, "PAL ", &palSize);
	unsigned char *scr = GrfReadBlockUncompressed(buffer, size, "MAP ", &scrSize);
	unsigned char *obj = GrfReadBlockUncompressed(buffer, size, "CELL", &objSize);
	unsigned char *hdr = GrfGetHeader(buffer, size, &headerSize);

	int gfxAttr = *(const uint16_t *) (hdr + 0x2);
	int scrType = *(const uint16_t *) (hdr + 0x4);
	uint16_t flags = *(const uint16_t *) (hdr + 0xE);
	unsigned int width = *(const uint32_t *) (hdr + 0x10);
	unsigned int height = *(const uint32_t *) (hdr + 0x14);

	unsigned int sizeChar = 8 * gfxAttr;

	NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
	PalInit(nclr, NCLR_TYPE_COMBO);
	nclr->nBits = gfxAttr;
	nclr->nColors = palSize / sizeof(COLOR);
	nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
	memcpy(nclr->colors, pal, nclr->nColors * sizeof(COLOR));

	NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
	ChrInit(ncgr, NCGR_TYPE_COMBO);
	ncgr->nBits = gfxAttr;
	ncgr->nTiles = gfxSize / sizeChar;
	ncgr->tilesX = ChrGuessWidth(ncgr->nTiles);
	ncgr->tilesY = ncgr->nTiles / ncgr->tilesX;
	ChrReadGraphics(ncgr, gfx);

	if (scrType != 0) {
		NSCR *nscr = (NSCR *) calloc(1, sizeof(NSCR));
		ScrInit(nscr, NSCR_TYPE_COMBO);
		nscr->tilesX = width / 8;
		nscr->tilesY = height / 8;
		nscr->dataSize = nscr->tilesX * nscr->tilesY * sizeof(uint16_t);
		nscr->data = (uint16_t *) calloc(nscr->dataSize, 1);

		switch (scrType) {
			case 1: nscr->colorMode = SCREENCOLORMODE_16x16;  nscr->fmt = SCREENFORMAT_TEXT;      break; // text
			case 2: nscr->colorMode = SCREENCOLORMODE_256x1;  nscr->fmt = SCREENFORMAT_TEXT;      break; // text
			case 3: nscr->colorMode = SCREENCOLORMODE_256x1;  nscr->fmt = SCREENFORMAT_AFFINE;    break; // affine
			case 4: nscr->colorMode = SCREENCOLORMODE_256x16; nscr->fmt = SCREENFORMAT_AFFINEEXT; break; // affine extended
		}

		if (nscr->fmt == SCREENFORMAT_AFFINE) {
			//copy bytes
			for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
				nscr->data[i] = (uint16_t) scr[i];
			}
		} else {
			//copy direct
			memcpy(nscr->data, scr, nscr->dataSize);
		}
		ScrComputeHighestCharacter(nscr);

		combo2dLink(combo, &nscr->header);
	}

	//cell data?
	if ((flags >> 14) == 2) {
		NCER *ncer = (NCER *) calloc(1, sizeof(NCER));
		CellInit(ncer, NCER_TYPE_COMBO);

		int nCell = *(const uint16_t *) (obj + 0x2);
		int mapping = *(const uint16_t *) (obj + 0x0);
		const uint32_t *ofsTbl = (const uint32_t *) (obj + 0x4);

		switch (mapping) {
			case 0: ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;  break;
			case 1: ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_64K;  break;
			case 2: ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_128K; break;
			case 3: ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_256K; break;
			case 4: ncer->mappingMode = GX_OBJVRAMMODE_CHAR_2D;      break;
		}
		ncer->nCells = nCell;

		ncer->cells = (NCER_CELL *) calloc(nCell, sizeof(NCER_CELL));
		for (int i = 0; i < nCell; i++) {
			NCER_CELL *cell = &ncer->cells[i];
			const unsigned char *cellData = obj + ofsTbl[i];

			cell->nAttribs = *(const uint16_t *) (cellData + 0x0);
			cell->minX = *(const int16_t *) (cellData + 0x2);
			cell->minY = *(const int16_t *) (cellData + 0x4);
			cell->maxX = (*(const uint16_t *) (cellData + 0x6)) + cell->minX;
			cell->maxY = (*(const uint16_t *) (cellData + 0x8)) + cell->minY;
			cell->attr = (uint16_t *) calloc(cell->nAttribs, 3 * sizeof(uint16_t));
			memcpy(cell->attr, cellData + 0xA, cell->nAttribs * 6);
		}

		combo2dLink(combo, &ncer->header);
	}

	combo2dLink(combo, &nclr->header);
	combo2dLink(combo, &ncgr->header);

	if (gfx != NULL) free(gfx);
	if (pal != NULL) free(pal);
	if (scr != NULL) free(scr);
	if (obj != NULL) free(obj);
	return OBJ_STATUS_SUCCESS;
}

int combo2dRead(COMBO2D *combo, const unsigned char *buffer, unsigned int size) {
	int format = combo2dIsValid(buffer, size);
	if (format == COMBO2D_TYPE_INVALID) return 1;

	combo2dInit(combo, format);
	switch (format) {
		case COMBO2D_TYPE_TIMEACE:
			return combo2dReadTimeAce(combo, buffer, size);
		case COMBO2D_TYPE_5BG:
			return combo2dRead5bg(combo, buffer, size);
		case COMBO2D_TYPE_5BG_OBJ:
			return combo2dRead5bgObj(combo, buffer, size);
		case COMBO2D_TYPE_BANNER:
			return combo2dReadBanner(combo, buffer, size);
		case COMBO2D_TYPE_MBB:
			return combo2dReadMbb(combo, buffer, size);
		case COMBO2D_TYPE_BNCD:
			return combo2dReadBncd(combo, buffer, size);
		case COMBO2D_TYPE_AOB:
			return combo2dReadAob(combo, buffer, size);
		case COMBO2D_TYPE_GRF_BG:
			return combo2dReadGrf(combo, buffer, size);
	}
	return 1;
}

static int combo2dWriteTimeAce(COMBO2D *combo, BSTREAM *stream) {
	NCLR *nclr = (NCLR *) combo2dGet(combo, FILE_TYPE_PALETTE, 0);
	NCGR *ncgr = (NCGR *) combo2dGet(combo, FILE_TYPE_CHARACTER, 0);
	NSCR *nscr = (NSCR *) combo2dGet(combo, FILE_TYPE_SCREEN, 0);
	BOOL is8bpp = ncgr->nBits == 8;
	int dummy = 0;

	bstreamWrite(stream, &is8bpp, sizeof(is8bpp));
	bstreamWrite(stream, nclr->colors, 2 * nclr->nColors);
	bstreamWrite(stream, &dummy, sizeof(dummy));
	bstreamWrite(stream, nscr->data, nscr->dataSize);
	bstreamWrite(stream, &ncgr->nTiles, 4);

	ChrWriteChars(ncgr, stream);
	return 0;
}

static int combo2dWriteBanner(COMBO2D *combo, BSTREAM *stream) {
	NCLR *nclr = (NCLR *) combo2dGet(combo, FILE_TYPE_PALETTE, 0);
	NCGR *ncgr = (NCGR *) combo2dGet(combo, FILE_TYPE_CHARACTER, 0);

	BANNER_INFO *info = (BANNER_INFO *) combo->extraData;
	unsigned short header[16] = { 0 };
	bstreamWrite(stream, header, sizeof(header));
	ChrWriteChars(ncgr, stream);

	//write palette
	bstreamWrite(stream, nclr->colors, 32);

	//write titles
	bstreamWrite(stream, info->titleJp, 0x600);
	if (info->version >= 2) bstreamWrite(stream, info->titleCn, 0x100);
	if (info->version >= 3) bstreamWrite(stream, info->titleHn, 0x100);

	//go back and write the CRCs
	bstreamSeek(stream, 0, 0);
	header[0] = info->version;
	header[1] = ObjComputeCrc16(stream->buffer + 0x20, 0x820, 0xFFFF);
	if (info->version >= 2) header[2] = ObjComputeCrc16(stream->buffer + 0x20, 0x920, 0xFFFF);
	if (info->version >= 3) header[3] = ObjComputeCrc16(stream->buffer + 0x20, 0xA20, 0xFFFF);
	bstreamWrite(stream, header, sizeof(header));
	return 0;
}

static int combo2dWriteBncd(COMBO2D *combo, BSTREAM *stream) {
	NCGR *ncgr = (NCGR *) combo2dGet(combo, FILE_TYPE_CHARACTER, 0);
	NCER *ncer = (NCER *) combo2dGet(combo, FILE_TYPE_CELL, 0);

	//header
	unsigned char header[] = { 'J', 'N', 'C', 'D', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	unsigned int nOam = 0;
	for (int i = 0; i < ncer->nCells; i++) nOam += ncer->cells[i].nAttribs;

	unsigned int offsCells = sizeof(header);
	unsigned int sizeCells = ncer->nCells * 6;
	unsigned int offsOam = (offsCells + sizeCells + 3) & ~3;
	unsigned int sizeOam = nOam * 0xC;
	unsigned int offsChars = (offsOam + sizeOam + 3) & ~3;
	unsigned int sizeChars = ncgr->nTiles * (8 * ncgr->nBits);

	unsigned int mappingShift = 0;
	switch (ncer->mappingMode) {
		case GX_OBJVRAMMODE_CHAR_1D_32K:
			mappingShift = 0; break;
		case GX_OBJVRAMMODE_CHAR_1D_64K:
			mappingShift = 1; break;
		case GX_OBJVRAMMODE_CHAR_1D_128K:
			mappingShift = 2; break;
		case GX_OBJVRAMMODE_CHAR_1D_256K:
			mappingShift = 3; break;
	}

	*(uint16_t *) (header + 0x04) = 0x0101;       // version 1.1
	*(uint16_t *) (header + 0x06) = ncer->nCells; // number of cells
	*(uint32_t *) (header + 0x08) = offsCells;    // offset to cell info
	*(uint32_t *) (header + 0x0C) = offsOam;      // offset to OAM attribute data
	*(uint32_t *) (header + 0x10) = offsChars;    // offset to OBJ character data
	*(uint32_t *) (header + 0x14) = sizeChars;    // OBJ character size
	*(uint32_t *) (header + 0x18) = mappingShift; // mapping mode

	bstreamWrite(stream, header, sizeof(header));

	//cell data
	unsigned int curObjIndex = 0;
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		unsigned char cellData[6] = { 0 };

		*(uint8_t *) (cellData + 0x0) = cell->maxX - cell->minX;
		*(uint8_t *) (cellData + 0x1) = cell->maxY - cell->minY;
		*(uint16_t *) (cellData + 0x2) = curObjIndex;
		*(uint16_t *) (cellData + 0x4) = cell->nAttribs;
		bstreamWrite(stream, cellData, sizeof(cellData));

		curObjIndex += cell->nAttribs;
	}

	//OBJ data
	bstreamAlign(stream, 4);
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		for (int j = 0; j < cell->nAttribs; j++) {
			NCER_CELL_INFO info;
			CellDecodeOamAttributes(&info, cell, j);

			unsigned int nCharsSize = info.width * info.height / 64;
			unsigned int nCharUnits = nCharsSize;
			if (info.characterBits == 8) nCharUnits <<= 1;
			if (ncer->mappingMode == GX_OBJVRAMMODE_CHAR_1D_32K) nCharUnits >>= 0;
			if (ncer->mappingMode == GX_OBJVRAMMODE_CHAR_1D_64K) nCharUnits >>= 1;
			if (ncer->mappingMode == GX_OBJVRAMMODE_CHAR_1D_128K) nCharUnits >>= 2;
			if (ncer->mappingMode == GX_OBJVRAMMODE_CHAR_1D_256K) nCharUnits >>= 3;


			unsigned char objData[0xC] = { 0 };
			*(uint16_t *) (objData + 0x00) = cell->attr[j * 3 + 0];           // attr 0
			*(uint16_t *) (objData + 0x02) = cell->attr[j * 3 + 1];           // attr 1
			*(uint16_t *) (objData + 0x04) = cell->attr[j * 3 + 2] & ~0x3FF;  // attr 2 (sans character name)
			*(uint16_t *) (objData + 0x06) = 0;
			*(uint16_t *) (objData + 0x08) = cell->attr[j * 3 + 2] & 0x3FF;   // character name
			*(uint16_t *) (objData + 0x0A) = nCharUnits;                      // character name units

			bstreamWrite(stream, objData, sizeof(objData));
		}
	}

	//graphics data
	bstreamAlign(stream, 4);
	ChrWriteChars(ncgr, stream);
	
	return 0;
}

static int combo2dWriteDataFile(COMBO2D *combo, BSTREAM *stream) {
	//the original data is in combo->extraData->data, but has key replacements
	DATAFILECOMBO *dfc = (DATAFILECOMBO *) combo->extraData;
	char *copy = (char *) malloc(dfc->size);
	memcpy(copy, dfc->data, dfc->size);

	//process all contained objects
	for (unsigned int i = 0; i < combo->links.length; i++) {
		OBJECT_HEADER *object;
		StListGet(&combo->links, i, &object);
		int type = object->type;

		//write object
		switch (type) {
			case FILE_TYPE_PALETTE:
			{
				NCLR *nclr = (NCLR *) object;

				int palDataSize = nclr->nColors * 2;
				if (palDataSize > dfc->pltSize) palDataSize = dfc->pltSize;
				memcpy(copy + dfc->pltOffset, nclr->colors, 2 * nclr->nColors);
				break;
			}
			case FILE_TYPE_CHARACTER:
			{
				NCGR *ncgr = (NCGR *) object;

				BSTREAM chrStream;
				bstreamCreate(&chrStream, NULL, 0);
				ChrWriteChars(ncgr, &chrStream);

				bstreamTruncate(&chrStream, dfc->chrSize);
				memcpy(copy + dfc->chrOffset, chrStream.buffer, chrStream.size);
				bstreamFree(&chrStream);
				break;
			}
			case FILE_TYPE_SCREEN:
			{
				NSCR *nscr = (NSCR *) object;

				int scrDataSize = nscr->dataSize;
				if (scrDataSize > dfc->scrSize) scrDataSize = dfc->scrSize;
				memcpy(copy + dfc->scrOffset, nscr->data, scrDataSize);
				break;
			}
		}
	}

	bstreamWrite(stream, copy, dfc->size);
	free(copy);
	return 0;
}

static int combo2dWrite5bg(COMBO2D *combo, BSTREAM *stream) {
	NCLR *nclr = (NCLR *) combo2dGet(combo, FILE_TYPE_PALETTE, 0);
	NCGR *ncgr = (NCGR *) combo2dGet(combo, FILE_TYPE_CHARACTER, 0);
	NSCR *nscr = (NSCR *) combo2dGet(combo, FILE_TYPE_SCREEN, 0);

	//screen size
	int scrWidth = ncgr->tilesX, scrHeight = ncgr->tilesY;
	if (!ncgr->bitmap) {
		//not bitmap, use BG screen dimensions
		scrWidth = nscr->tilesX;
		scrHeight = nscr->tilesY;
	}

	NnsStream nns;
	NnsStreamCreate(&nns, "NTBG", 1, 0, NNS_TYPE_G2D, NNS_SIG_BE);

	//write PALT
	uint32_t paltHeader = nclr->nColors;
	NnsStreamStartBlock(&nns, "PALT");
	NnsStreamWrite(&nns, &paltHeader, sizeof(paltHeader));
	NnsStreamWrite(&nns, nclr->colors, nclr->nColors * sizeof(COLOR));
	NnsStreamEndBlock(&nns);

	//write BGDT
	unsigned char bgdtHeader[0x14] = { 0 };
	*(uint32_t *) (bgdtHeader + 0x00) = ncgr->mappingMode;
	*(uint32_t *) (bgdtHeader + 0x04) = ncgr->bitmap ? 0 : nscr->dataSize;
	*(uint16_t *) (bgdtHeader + 0x08) = scrWidth;
	*(uint16_t *) (bgdtHeader + 0x0A) = scrHeight;
	*(uint16_t *) (bgdtHeader + 0x0C) = ncgr->tilesX;
	*(uint16_t *) (bgdtHeader + 0x0E) = ncgr->tilesY;
	*(uint32_t *) (bgdtHeader + 0x10) = ncgr->nTiles * (8 * ncgr->nBits);
	NnsStreamStartBlock(&nns, "BGDT");
	NnsStreamWrite(&nns, bgdtHeader, sizeof(bgdtHeader));
	if (!ncgr->bitmap) NnsStreamWrite(&nns, nscr->data, nscr->dataSize);

	//write graphics
	ChrWriteGraphics(ncgr, NnsStreamGetBlockStream(&nns));
	NnsStreamEndBlock(&nns);

	//write DFPL
	if (ncgr->nBits == 4) {
		uint32_t dfplHeader = ncgr->nTiles;

		//iterate the screen and set attributes
		uint8_t *attr = (uint8_t *) calloc(ncgr->nTiles, 1);
		if (ncgr->attr != NULL) memcpy(attr, ncgr->attr, ncgr->nTiles);
		NnsStreamStartBlock(&nns, "DFPL");
		NnsStreamWrite(&nns, &dfplHeader, sizeof(dfplHeader));
		NnsStreamWrite(&nns, attr, ncgr->nTiles);
		NnsStreamEndBlock(&nns);
		free(attr);
	}

	NnsStreamFinalize(&nns);
	NnsStreamFlushOut(&nns, stream);
	NnsStreamFree(&nns);
	return OBJ_STATUS_SUCCESS;
}

static int combo2dWriteMbb(COMBO2D *combo, BSTREAM *stream) {
	MBBCOMBO *mbbInfo = (MBBCOMBO *) combo->extraData;
	NCLR *nclr = (NCLR *) combo2dGet(combo, FILE_TYPE_PALETTE, 0);
	NCGR *ncgr = (NCGR *) combo2dGet(combo, FILE_TYPE_CHARACTER, 0);

	unsigned char header[0x74] = { 0 };
	*(uint32_t *) (header + 0x00) = sizeof(header);
	*(uint32_t *) (header + 0x04) = sizeof(header) + 0x200;
	*(uint16_t *) (header + 0x60) = ncgr->nTiles * (8 * ncgr->nBits) / 0x20;
	header[0x59] = (ncgr->nBits == 8) ? 0x80 : 0;

	//write screen offset infos
	int nScreensWritten = 0;
	uint32_t currentScreenOffset = sizeof(header) + 0x200 + ncgr->nTiles * (8 * ncgr->nBits);
	for (int i = 0; i < 4; i++) {
		if (!(mbbInfo->screenBitmap & (1 << i))) continue;

		NSCR *nscr = (NSCR *) combo2dGet(combo, FILE_TYPE_SCREEN, nScreensWritten);
		*(uint32_t *) (header + 0x08 + i * 4) = currentScreenOffset;
		*(uint16_t *) (header + 0x18 + i * 0x10 + 0x8) = nscr->tilesX * 8;
		*(uint16_t *) (header + 0x18 + i * 0x10 + 0xA) = nscr->tilesY * 8;
		currentScreenOffset += nscr->dataSize;
	}

	bstreamWrite(stream, header, sizeof(header));
	bstreamWrite(stream, nclr->colors, nclr->nColors * sizeof(COLOR));
	ChrWriteChars(ncgr, stream);

	//write screens
	nScreensWritten = 0;
	for (int i = 0; i < 4; i++) {
		if (!(mbbInfo->screenBitmap & (1 << i))) continue;
		NSCR *nscr = (NSCR *) combo2dGet(combo, FILE_TYPE_SCREEN, nScreensWritten);

		bstreamWrite(stream, nscr->data, nscr->dataSize);

		nScreensWritten++;
	}
	return 0;
}

static int combo2dWriteGrf(COMBO2D *combo, BSTREAM *stream) {
	NCLR *nclr = (NCLR *) combo2dGet(combo, FILE_TYPE_PALETTE, 0);
	NCGR *ncgr = (NCGR *) combo2dGet(combo, FILE_TYPE_CHARACTER, 0);
	NSCR *nscr = (NSCR *) combo2dGet(combo, FILE_TYPE_SCREEN, 0);
	NCER *ncer = (NCER *) combo2dGet(combo, FILE_TYPE_CELL, 0);

	int type = 2; // OBJ graphics
	if (nscr != NULL) {
		type = 1;
	}

	//image dimension
	int width = ncgr->tilesX * 8, height = ncgr->tilesY * 8, screenType = 0;
	if (nscr != NULL) {
		width = nscr->tilesX * 8;
		height = nscr->tilesY * 8;

		switch (nscr->fmt) {
			case SCREENFORMAT_TEXT:
				if (nscr->colorMode == SCREENCOLORMODE_16x16) screenType = 1;
				else screenType = 2;
				break;
			case SCREENFORMAT_AFFINE:
				screenType = 3;
				break;
			case SCREENFORMAT_AFFINEEXT:
				screenType = 4;
				break;
		}
	}

	unsigned int gfxSize;
	BSTREAM stmChars;
	bstreamCreate(&stmChars, NULL, 0);
	ChrWriteGraphics(ncgr, &stmChars);
	unsigned char *gfxData = bstreamToByteArray(&stmChars, &gfxSize);

	BSTREAM grf;
	GrfStreamCreate(&grf);

	unsigned char hdr[0x18] = { 0 };
	*(uint16_t *) (hdr + 0x00) = 2; // version 2
	*(uint16_t *) (hdr + 0x02) = ncgr->nBits;
	*(uint16_t *) (hdr + 0x04) = screenType;
	*(uint16_t *) (hdr + 0x08) = nclr->nColors;
	*(uint8_t *) (hdr + 0x0A) = 8; // character width
	*(uint8_t *) (hdr + 0x0B) = 8; // character height
	*(uint16_t *) (hdr + 0x0E) = type << 14;
	*(uint32_t *) (hdr + 0x10) = width;
	*(uint32_t *) (hdr + 0x14) = height;
	GrfStreamWriteBlock(&grf, "HDRX", hdr, sizeof(hdr));

	GrfStreamWriteBlockCompressed(&grf, "PAL ", nclr->colors, nclr->nColors * sizeof(COLOR), COMPRESSION_NONE);
	GrfStreamWriteBlockCompressed(&grf, "GFX ", gfxData, gfxSize, COMPRESSION_LZ77);

	if (nscr != NULL) {
		if (screenType == 3) {
			unsigned char *scr = (unsigned char *) calloc(1, nscr->dataSize / 2);
			for (unsigned int i = 0; i < nscr->dataSize / 2; i++) scr[i] = (unsigned char) nscr->data[i];
			GrfStreamWriteBlockCompressed(&grf, "MAP ", scr, nscr->dataSize / 2, COMPRESSION_LZ77);
			free(scr);
		} else {
			GrfStreamWriteBlockCompressed(&grf, "MAP ", nscr->data, nscr->dataSize, COMPRESSION_LZ77);
		}
	}

	if (ncer != NULL) {
		BSTREAM stmCell;
		bstreamCreate(&stmCell, NULL, 0);

		int mapping = 0;
		switch (ncer->mappingMode) {
			case GX_OBJVRAMMODE_CHAR_1D_32K:  mapping = 0; break;
			case GX_OBJVRAMMODE_CHAR_1D_64K:  mapping = 1; break;
			case GX_OBJVRAMMODE_CHAR_1D_128K: mapping = 2; break;
			case GX_OBJVRAMMODE_CHAR_1D_256K: mapping = 3; break;
			case GX_OBJVRAMMODE_CHAR_2D:      mapping = 4; break;
		}

		//header
		unsigned char cellHdr[4] = { 0 };
		*(uint16_t *) (cellHdr + 0x0) = mapping;
		*(uint16_t *) (cellHdr + 0x2) = ncer->nCells;
		bstreamWrite(&stmCell, cellHdr, sizeof(cellHdr));

		//cell data
		uint32_t dummy = 0;
		for (int i = 0; i < ncer->nCells; i++) bstreamWrite(&stmCell, &dummy, sizeof(dummy));

		for (int i = 0; i < ncer->nCells; i++) {
			NCER_CELL *cell = &ncer->cells[i];

			//put cell data offset
			uint32_t offs = stmCell.size;
			*(uint32_t *) (stmCell.buffer + 4 + 4 * i) = offs;

			//put cell data
			unsigned char cellInfo[0xA] = { 0 };
			*(uint16_t *) (cellInfo + 0x0) = cell->nAttribs;
			*(int16_t  *) (cellInfo + 0x2) = cell->minX;
			*(int16_t  *) (cellInfo + 0x4) = cell->minY;
			*(uint16_t *) (cellInfo + 0x6) = cell->maxX - cell->minX;
			*(uint16_t *) (cellInfo + 0x8) = cell->maxY - cell->minY;
			bstreamWrite(&stmCell, cellInfo, sizeof(cellInfo));
			bstreamWrite(&stmCell, cell->attr, cell->nAttribs * 6);
		}

		GrfStreamWriteBlockCompressed(&grf, "CELL", stmCell.buffer, stmCell.size, COMPRESSION_NONE);
		bstreamFree(&stmCell);
	}

	GrfStreamFinalize(&grf);
	GrfStreamFlushOut(&grf, stream);
	GrfStreamFree(&grf);

	free(gfxData);
	return OBJ_STATUS_SUCCESS;
}

int combo2dWrite(COMBO2D *combo, BSTREAM *stream) {
	if (!combo2dCanSave(combo)) return 1;

	//write out 
	switch (combo->header.format) {
		case COMBO2D_TYPE_BANNER:
			return combo2dWriteBanner(combo, stream);
		case COMBO2D_TYPE_BNCD:
			return combo2dWriteBncd(combo, stream);
		case COMBO2D_TYPE_5BG:
			return combo2dWrite5bg(combo, stream);
		case COMBO2D_TYPE_TIMEACE:
			return combo2dWriteTimeAce(combo, stream);
		case COMBO2D_TYPE_DATAFILE:
			return combo2dWriteDataFile(combo, stream);
		case COMBO2D_TYPE_MBB:
			return combo2dWriteMbb(combo, stream);
		case COMBO2D_TYPE_GRF_BG:
			return combo2dWriteGrf(combo, stream);
	}

	return OBJ_STATUS_INVALID;
}

int combo2dWriteFile(COMBO2D *combo, LPWSTR path) {
	return ObjWriteFile(&combo->header, path);
}
