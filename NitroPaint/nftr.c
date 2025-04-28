#include "nftr.h"
#include "struct.h"
#include "nns.h"

#define NFTR_MAP_DIRECT     0
#define NFTR_MAP_TABLE      1
#define NFTR_MAP_SCAN       2

extern LPCWSTR fontFormatNames[] = {
	L"Invalid", L"NFTR 0.1", L"NFTR 1.0", L"NFTR 1.1", L"NFTR 1.2", L"BNFR 1.1", L"BNFR 1.2", L"BNFR 2.0", NULL
};



void NftrFree(OBJECT_HEADER *obj) {
	NFTR *nftr = (NFTR*) obj;
	if (nftr->glyphs != NULL) {
		for (int i = 0; i < nftr->nGlyph; i++) {
			if (nftr->glyphs[i].px != NULL) free(nftr->glyphs[i].px);
		}
		free(nftr->glyphs);
		nftr->glyphs = NULL;
	}
}

void NftrInit(NFTR *nftr, int format) {
	nftr->header.size = sizeof(NFTR);
	ObjInit(&nftr->header, FILE_TYPE_FONT, format);
	nftr->header.dispose = NftrFree;
	nftr->header.writer = (OBJECT_WRITER) NftrWrite;
}


// ----- JFont common routines

static int JFntValidateContinuousMapping(const uint16_t *pCon, unsigned int nCon) {
	//validate jnlib font file continuous mapping: pairs of code points marking a range
	//of code points, second code point must not be less than the first, and pairs must
	//be in ascending order without range overlap
	for (unsigned int i = 0; i < nCon; i++) {
		uint16_t clo = pCon[i * 2 + 0];
		uint16_t chi = pCon[i * 2 + 1];
		if (chi < clo) return 0;

		if (i > 0) {
			//low code point must not be less than the last range's high code point
			uint16_t chiprev = pCon[(i - 1) * 2 + 1];
			if (clo < chiprev) return 0;
		}
	}
	return 1;
}

static int JFntValidateDirectMapping(const uint16_t *pDir, unsigned int nDir) {
	//validate jnlib font file direct mapping: array of code points in increasing order.
	for (unsigned int i = 1; i < nDir; i++) {
		uint16_t prev = pDir[i - 1];
		uint16_t curr = pDir[i - 0];
		if (curr <= prev) return 0;
	}
	return 1;
}

static unsigned int JFntCountMappings(const uint16_t *pCon, unsigned int nCon, const uint16_t *pDir, unsigned int nDir) {
	unsigned int nCP = nDir;

	//count continuous
	for (unsigned int i = 0; i < nCon; i++) {
		nCP += (pCon[2 * i + 1] + 1 - pCon[2 * i + 0]);
	}
	return nCP;
}

static int JFntCheckHeader(const unsigned char *buffer, unsigned int size, int verHi, int verLo, const char *sig) {
	if (size < 8) return 0;
	if (memcmp(buffer, sig, 4) != 0) return 0;

	//fields in common with all versions of the format
	if (buffer[4] != verLo) return 0;
	if (buffer[5] != verHi) return 0;
	return 1;
}

static void JFntWriteHeader(BSTREAM *stream, int verHi, int verLo, int nEntries, const char *sig) {
	unsigned char header[8] = { 0 };
	memcpy(header + 0x00, sig, 4);
	header[4] = verLo;
	header[5] = verHi;
	*(uint16_t *) (header + 0x06) = nEntries;
	bstreamWrite(stream, header, sizeof(header));
}


// ----- font routines

static int NftrIsValidBnfr1x(const unsigned char *buffer, unsigned int size) {
	if (size < 0xA) return 0;
	uint16_t numGlyphs = *(const uint16_t *) (buffer + 0x06);
	uint16_t glyphInfo = *(const uint16_t *) (buffer + 0x08);

	//check size of glyph width array
	if ((size - 0xA) / 2 < numGlyphs) return 0;

	//check size of glyph bitmaps
	unsigned int offsBitmap = 0xA + 2 * numGlyphs;
	unsigned int sizeBitmap = size - offsBitmap;
	unsigned int glyphW = (glyphInfo >> 0) & 0x1F;
	unsigned int glyphH = (glyphInfo >> 5) & 0x1F;
	unsigned int glyphSize = (glyphW * glyphH + 15) / 16 * 2;
	unsigned int numBitmaps = sizeBitmap / glyphSize;
	if (numGlyphs > numBitmaps) return 0;

	return 1;
}

int NftrIsValidBnfr11(const unsigned char *buffer, unsigned int size) {
	//check header
	if (!JFntCheckHeader(buffer, size, 1, 1, "JNFR")) return 0;
	return NftrIsValidBnfr1x(buffer, size);
}

int NftrIsValidBnfr12(const unsigned char *buffer, unsigned int size) {
	//check header
	if (!JFntCheckHeader(buffer, size, 1, 2, "JNFR")) return 0;
	return NftrIsValidBnfr1x(buffer, size);
}

int NftrIsValidBnfr20(const unsigned char *buffer, unsigned int size) {
	//check header
	if (!JFntCheckHeader(buffer, size, 2, 0, "JNFR")) return 0;
	if (size < 0xC) return 0;
	if (buffer[6] != 0xFF) return 0;
	if (buffer[7] != 0xFE) return 0;

	const unsigned char *img1 = NULL, *cmp1 = NULL, *wid1 = NULL;
	uint32_t img1Size = 0, cmp1Size = 0, wid1Size = 0;

	uint32_t nBlocks = *(const uint16_t *) (buffer + 0x08);
	uint32_t offs = 0xC;
	for (unsigned int i = 0; i < nBlocks; i++) {
		if (offs >= size || (size - offs) < 8) return 0;

		const unsigned char *block = buffer + offs;
		uint32_t blockSize = *(const uint32_t *) (block + 4);
		if ((size - offs) < blockSize) return 0;
		if (blockSize < 8) return 0; // block size must include block header
		if (blockSize & 3) return 0; // block size must align to a 4-byte

		if (memcmp(block, "IMG1", 4) == 0) {
			if (blockSize < 0x14) return 0;
			img1 = block;
			img1Size = blockSize;
		} else if (memcmp(block, "CMP1", 4) == 0) {
			if (blockSize < 0x14) return 0;
			cmp1 = block;
			cmp1Size = blockSize;
		} else if (memcmp(block, "WID1", 4) == 0) {
			if (blockSize < 0x10) return 0;
			wid1 = block;
			wid1Size = blockSize;
		}

		offs += blockSize;
	}

	//BNFR 2.0 must have IMG1, CMP1, WID1 blocks
	if (img1 == NULL) return 0;
	if (cmp1 == NULL) return 0;
	if (wid1 == NULL) return 0;

	uint32_t img1OffsBitmap = *(const uint32_t *) (img1 + 0x08);
	uint8_t img1CellWidth = *(const uint8_t *) (img1 + 0x0C);
	uint8_t img1CellHeight = *(const uint8_t *) (img1 + 0x0D);
	uint16_t img1Invalid = *(const uint16_t *) (img1 + 0x12);
	uint32_t wid1OffsWidths = *(const uint32_t *) (wid1 + 0x08);
	uint16_t wid1NumGlyphs = *(const uint16_t *) (wid1 + 0x0C);
	uint32_t cmp1OffsCont = *(const uint32_t *) (cmp1 + 0x08);
	uint32_t cmp1OffsDir = *(const uint32_t *) (cmp1 + 0x0C);
	uint16_t cmp1NumCont = *(const uint16_t *) (cmp1 + 0x10);
	uint16_t cmp1NumDir = *(const uint16_t *) (cmp1 + 0x12);

	if (img1OffsBitmap > img1Size) return 0; // IMG1: points out of bounds
	if (img1OffsBitmap < 0x14) return 0;     // IMG1: points too low
	if (wid1OffsWidths > wid1Size) return 0; // WID1: points out of bounds
	if (wid1OffsWidths < 0x10) return 0;     // WID1: points too low
	if (cmp1NumCont && cmp1OffsCont > cmp1Size) return 0; // CMP1: points out of bounds
	if (cmp1NumCont && cmp1OffsCont < 0x14) return 0;     // CMP1: points too low
	if (cmp1NumDir && cmp1OffsDir > cmp1Size) return 0;   // CMP1: points out of bounds
	if (cmp1NumDir && cmp1OffsDir < 0x14) return 0;       // CMP1: points too low

	//check number of glyphs from WID1 matches IMG1
	unsigned int glyphSizePx = img1CellWidth * img1CellHeight;
	unsigned int glyphSizeBytes = (glyphSizePx + 15) / 16 * 2;
	unsigned int numGlyphsImg1 = (img1Size - img1OffsBitmap) / glyphSizeBytes;
	if (wid1NumGlyphs > numGlyphsImg1) return 0;
	if (img1Invalid >= numGlyphsImg1) return 0;

	//check continuous and direct mapping blocks
	const uint16_t *pCon = (const uint16_t *) (cmp1 + cmp1OffsCont);
	const uint16_t *pDir = (const uint16_t *) (cmp1 + cmp1OffsDir);
	if (!JFntValidateContinuousMapping(pCon, cmp1NumCont)) return 0;
	if (!JFntValidateDirectMapping(pDir, cmp1NumDir)) return 0;
	
	unsigned int nMappedCP = JFntCountMappings(pCon, cmp1NumCont, pDir, cmp1NumDir);
	if (nMappedCP > numGlyphsImg1) return 0;

	return 1;
}

static int NftrIsValidNftr(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;

	unsigned int finfSize, cglpSize;
	const unsigned char *finf = NnsG2dFindBlockBySignature(buffer, size, "FINF", NNS_SIG_LE, &finfSize);
	const unsigned char *cglp = NnsG2dFindBlockBySignature(buffer, size, "CGLP", NNS_SIG_LE, &cglpSize);
	if (finf == NULL || cglp == NULL) return 0;

	return 1;
}

static int NftrIsValidNftr01(const unsigned char *buffer, unsigned int size) {
	if (!NftrIsValidNftr(buffer, size)) return 0;
	return (*(const uint16_t *) (buffer + 0x06)) == 0x0001;
}

static int NftrIsValidNftr10(const unsigned char *buffer, unsigned int size) {
	if (!NftrIsValidNftr(buffer, size)) return 0;
	return (*(const uint16_t *) (buffer + 0x06)) == 0x0100;
}

static int NftrIsValidNftr11(const unsigned char *buffer, unsigned int size) {
	if (!NftrIsValidNftr(buffer, size)) return 0;
	return (*(const uint16_t *) (buffer + 0x06)) == 0x0101;
}

static int NftrIsValidNftr12(const unsigned char *buffer, unsigned int size) {
	if (!NftrIsValidNftr(buffer, size)) return 0;
	return (*(const uint16_t *) (buffer + 0x06)) == 0x0102;
}

int NftrIdentify(const unsigned char *buffer, unsigned int size) {
	if (NftrIsValidNftr01(buffer, size)) return NFTR_TYPE_NFTR_01;
	if (NftrIsValidNftr10(buffer, size)) return NFTR_TYPE_NFTR_10;
	if (NftrIsValidNftr11(buffer, size)) return NFTR_TYPE_NFTR_11;
	if (NftrIsValidNftr12(buffer, size)) return NFTR_TYPE_NFTR_12;
	if (NftrIsValidBnfr20(buffer, size)) return NFTR_TYPE_BNFR_20;
	if (NftrIsValidBnfr12(buffer, size)) return NFTR_TYPE_BNFR_12;
	if (NftrIsValidBnfr11(buffer, size)) return NFTR_TYPE_BNFR_11;
	return NFTR_TYPE_INVALID;
}

static int NftrCodePointComparator(const void *p1, const void *p2) {
	const NFTR_GLYPH *g1 = (const NFTR_GLYPH *) p1;
	const NFTR_GLYPH *g2 = (const NFTR_GLYPH *) p2;

	if (g1->cp < g2->cp) return -1;
	if (g1->cp > g2->cp) return 1;
	return 0;
}

static int NftrReadBnfr20(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	NftrInit(nftr, NFTR_TYPE_BNFR_20);

	//get blocks
	const unsigned char *img1 = NULL, *cmp1 = NULL, *wid1 = NULL;

	uint32_t nBlocks = *(const uint16_t *) (buffer + 0x08);
	uint32_t offs = 0xC;
	for (unsigned int i = 0; i < nBlocks; i++) {
		const unsigned char *block = buffer + offs;
		uint32_t blockSize = *(const uint32_t *) (block + 4);

		if (memcmp(block, "IMG1", 4) == 0) {
			img1 = block;
		} else if (memcmp(block, "CMP1", 4) == 0) {
			cmp1 = block;
		} else if (memcmp(block, "WID1", 4) == 0) {
			wid1 = block;
		}

		offs += blockSize;
	}

	//get number of code units
	uint32_t img1OffsBitmap = *(const uint32_t *) (img1 + 0x08);
	uint8_t img1CellWidth = *(const uint8_t *) (img1 + 0x0C);
	uint8_t img1CellHeight = *(const uint8_t *) (img1 + 0x0D);
	uint16_t img1Invalid = *(const uint16_t *) (img1 + 0x12);
	uint32_t wid1OffsWidths = *(const uint32_t *) (wid1 + 0x08);
	uint32_t cmp1OffsCont = *(const uint32_t *) (cmp1 + 0x08);
	uint32_t cmp1OffsDir = *(const uint32_t *) (cmp1 + 0x0C);
	uint16_t cmp1NumCont = *(const uint16_t *) (cmp1 + 0x10);
	uint16_t cmp1NumDir = *(const uint16_t *) (cmp1 + 0x12);
	const uint8_t *pWid = (const uint8_t *) (wid1 + wid1OffsWidths);
	const uint16_t *pCon = (const uint16_t *) (cmp1 + cmp1OffsCont);
	const uint16_t *pDir = (const uint16_t *) (cmp1 + cmp1OffsDir);

	unsigned int nMappedCP = JFntCountMappings(pCon, cmp1NumCont, pDir, cmp1NumDir);

	nftr->bpp = 1;
	nftr->hasCodeMap = 1;
	nftr->charset = FONT_CHARSET_UTF16;
	nftr->cellWidth = img1CellWidth;
	nftr->cellHeight = img1CellHeight;
	nftr->lineHeight = img1CellHeight;
	nftr->pxLeading = *(const uint8_t *) (img1 + 0x0F);
	nftr->pxAscent = *(const uint8_t *) (img1 + 0x10);
	nftr->pxDescent = *(const uint8_t *) (img1 + 0x11);
	nftr->nGlyph = nMappedCP;
	nftr->rotation = FONT_ROTATION_0;
	nftr->vertical = 0;
	nftr->glyphs = (NFTR_GLYPH *) calloc(nftr->nGlyph, sizeof(NFTR_GLYPH));

	//read glyph forms
	const unsigned char *bmps = img1 + img1OffsBitmap;
	unsigned int glyphSize = (img1CellWidth * img1CellHeight + 15) / 16 * 2;
	for (int i = 0; i < nftr->nGlyph; i++) {
		nftr->glyphs[i].width = pWid[2 * i + 1] + pWid[2 * i + 0];
		nftr->glyphs[i].spaceLeft = -pWid[2 * i + 0];
		nftr->glyphs[i].cp = 0;
		nftr->glyphs[i].isInvalid = 0;

		const unsigned char *bmp = bmps + i * glyphSize;
		nftr->glyphs[i].px = (unsigned char *) calloc(img1CellWidth * img1CellHeight, 1);
		for (unsigned int y = 0; y < img1CellHeight; y++) {
			for (unsigned int x = 0; x < img1CellWidth; x++) {
				unsigned int pxno = x + y * img1CellWidth;
				nftr->glyphs[i].px[pxno] = ((bmp[pxno >> 3] >> (pxno & 7)) & 1) ^ 1;
			}
		}
	}

	//mark invalid glyph
	nftr->glyphs[img1Invalid].isInvalid = 1;

	//read code map
	unsigned int curCP = 0;
	for (unsigned int i = 0; i < cmp1NumCont; i++) {
		uint16_t cpLo = pCon[2 * i + 0];
		uint16_t cpHi = pCon[2 * i + 1];

		for (unsigned int j = cpLo; j <= cpHi; j++) {
			nftr->glyphs[curCP++].cp = j;
		}
	}
	for (unsigned int i = 0; i < cmp1NumDir; i++) {
		nftr->glyphs[curCP++].cp = pDir[i];
	}

	//sort code map
	qsort(nftr->glyphs, nftr->nGlyph, sizeof(NFTR_GLYPH), NftrCodePointComparator);

	//get index of invalid glyph
	nftr->invalid = -1;
	for (int i = 0; i < nftr->nGlyph; i++) {
		if (nftr->glyphs[i].isInvalid) {
			nftr->invalid = i;
			break;
		}
	}

	return 0;
}

static int NftrReadBnfr1x(NFTR *nftr, const unsigned char *buffer, unsigned int size) {

	uint16_t numGlyphs = *(const uint16_t *) (buffer + 0x06);
	uint16_t cellInfo = *(const uint16_t *) (buffer + 0x08);
	unsigned int cellW = (cellInfo >> 0) & 0x1F;
	unsigned int cellH = (cellInfo >> 5) & 0x1F;
	unsigned int cellSize = (cellW * cellH + 15) / 16 * 2;

	nftr->bpp = 1;
	nftr->hasCodeMap = 0;
	nftr->charset = FONT_CHARSET_UTF16;
	nftr->nGlyph = numGlyphs;
	nftr->cellWidth = cellW;
	nftr->cellHeight = cellH;
	nftr->lineHeight = cellH;
	nftr->pxDescent = 0;
	nftr->pxAscent = nftr->cellHeight;
	nftr->pxLeading = 0;
	nftr->rotation = FONT_ROTATION_0;
	nftr->vertical = 0;
	nftr->glyphs = (NFTR_GLYPH *) calloc(nftr->nGlyph, sizeof(NFTR_GLYPH));

	const uint8_t *pWid = (const uint8_t *) (buffer + 0x0A);
	const unsigned char *pBmp = (const unsigned char *) (pWid + numGlyphs * 2);
	for (int i = 0; i < nftr->nGlyph; i++) {
		nftr->glyphs[i].isInvalid = 0;
		nftr->glyphs[i].cp = i; // dummy
		nftr->glyphs[i].width = pWid[i * 2 + 1] + pWid[i * 2 + 0];
		nftr->glyphs[i].spaceLeft = -pWid[i * 2 + 0]; // subtract padding space
		nftr->glyphs[i].px = (unsigned char *) calloc(nftr->cellWidth * nftr->cellHeight, 1);

		const unsigned char *bmp = pBmp + i * cellSize;
		for (unsigned int y = 0; y < cellH; y++) {
			for (unsigned int x = 0; x < cellW; x++) {
				unsigned int pxno = x + y * cellW;
				nftr->glyphs[i].px[pxno] = ((bmp[pxno >> 3] >> (pxno & 7)) & 1) ^ 1;
			}
		}
	}

	return 0;
}

static int NftrReadBnfr12(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	NftrInit(nftr, NFTR_TYPE_BNFR_12);
	return NftrReadBnfr1x(nftr, buffer, size);
}

static int NftrReadBnfr11(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	NftrInit(nftr, NFTR_TYPE_BNFR_11);
	return NftrReadBnfr1x(nftr, buffer, size);
}

static unsigned char *NftrLookupNftrGlyphWidth(const unsigned char *buffer, uint32_t offsWidth, uint16_t gidx, unsigned int widSize, const unsigned char *finf) {
	while (offsWidth) {
		const unsigned char *cwdh = buffer + offsWidth;
		uint32_t nextWidth = *(const uint32_t *) (cwdh + 0x04);

		uint16_t glo = *(const uint16_t *) (cwdh + 0x00);
		uint16_t ghi = *(const uint16_t *) (cwdh + 0x02);
		if (gidx >= glo && gidx <= ghi) {
			return (unsigned char *) (cwdh + 8 + (gidx - glo) * widSize);
		}

		offsWidth = nextWidth;
	}

	//no mapping found, return default glyph info
	return (unsigned char *) (finf + 4);
}

static void NftrReadNftrGlyph(NFTR_GLYPH *glyph, NFTR *nftr, const unsigned char *buffer, uint32_t offsWidth, const unsigned char *glyphs, unsigned int glyphSize, const unsigned char *finf, int cp, int gidx) {
	unsigned int widSize = 3;
	if (nftr->header.format == NFTR_TYPE_NFTR_01) widSize = 2; // no trailing information
	const unsigned char *wid = NftrLookupNftrGlyphWidth(buffer, offsWidth, gidx, widSize, finf);

	glyph->cp = cp;
	glyph->isInvalid = gidx == nftr->invalid;
	glyph->width = 0;
	glyph->spaceLeft = 0;
	glyph->spaceRight = 0;
	glyph->px = (unsigned char *) calloc(nftr->cellWidth * nftr->cellHeight, 1);

	//set glyph width
	if (wid != NULL) {
		glyph->spaceLeft = *(const int8_t *) (wid + 0);
		glyph->width = *(const uint8_t *) (wid + 1);
		if (widSize >= 3) {
			glyph->spaceRight = (*(const int8_t *) (wid + 2)) - (glyph->spaceLeft + glyph->width);
		} else {
			glyph->spaceRight = 0; // NFTR 0.1 has no trailing pixel information
		}
	}

	//read glyph bits
	unsigned int pxPerByte = 8 / (nftr->bpp);
	const unsigned char *bmp = glyphs + glyphSize * gidx;
	for (int y = 0; y < nftr->cellHeight; y++) {
		for (int x = 0; x < nftr->cellWidth; x++) {
			unsigned int pxno = x + y * nftr->cellWidth;

			unsigned int byteno = pxno / pxPerByte;
			unsigned int bitno = (pxPerByte - 1 - (pxno % pxPerByte)) * nftr->bpp;
			glyph->px[x + y * nftr->cellWidth] = (bmp[byteno] >> bitno) & ((1 << nftr->bpp) - 1);
		}
	}
}

static int NftrReadNftrCommon(NFTR *nftr, const unsigned char *buffer, unsigned int size) {

	unsigned int finfSize, cglpSize;
	const unsigned char *finf = NnsG2dFindBlockBySignature(buffer, size, "FINF", NNS_SIG_LE, &finfSize);
	const unsigned char *cglp = NnsG2dFindBlockBySignature(buffer, size, "CGLP", NNS_SIG_LE, &cglpSize);

	//
	uint32_t offsGlyph = *(const uint32_t *) (finf + 0x08);
	uint32_t offsWidth = *(const uint32_t *) (finf + 0x0C);
	uint32_t offsCmap = *(const uint32_t *) (finf + 0x10);
	unsigned int glyphSize = *(const uint16_t *) (cglp + 0x02);

	nftr->cellWidth = *(const uint8_t *) (cglp + 0x00);
	nftr->cellHeight = *(const uint8_t *) (cglp + 0x01);
	nftr->lineHeight = *(const uint8_t *) (finf + 0x01);
	nftr->bpp = *(const uint8_t *) (cglp + 0x06);
	nftr->invalid = *(const uint16_t *) (finf + 0x02);
	nftr->hasCodeMap = 1;
	nftr->pxAscent = *(const int8_t *) (cglp + 0x04);
	nftr->pxDescent = nftr->cellHeight - nftr->pxAscent;
	nftr->pxLeading = 0;

	if (nftr->header.format == NFTR_TYPE_NFTR_01) {
		//NFTR 0.1: character encoding at FINF+6
		nftr->charset = (FontCharacterSet) *(const uint8_t *) (finf + 0x06);
	} else {
		//NFTR 1.0+: character encoding at FINF+7
		nftr->charset = (FontCharacterSet) *(const uint8_t *) (finf + 0x07);
	}

	if (nftr->header.format == NFTR_TYPE_NFTR_11 || nftr->header.format == NFTR_TYPE_NFTR_12) {
		//NFTR 1.1, 1.2: rotation information
		uint8_t flg = *(const uint8_t *) (cglp + 0x07);
		nftr->rotation = (FontRotation) ((flg >> 1) & 3);
		nftr->vertical = flg & 1;
	} else {
		//NFTR 0.1, 1.0: no rotation information
		nftr->rotation = FONT_ROTATION_0;
		nftr->vertical = 0;
	}

	if (nftr->header.format == NFTR_TYPE_NFTR_12) {
		//NFTR 1.2: store height, width, ascent at +14, +15, +16
	}
	
	//construct glyph list
	StList list;
	StListCreateInline(&list, NFTR_GLYPH, NULL);

	//process code to glyph map
	while (offsCmap) {
		const unsigned char *cmap = buffer + offsCmap;
		uint32_t nextCmap = *(const uint32_t *) (cmap + 0x08);

		uint16_t cclo = *(const uint16_t *) (cmap + 0x00);
		uint16_t cchi = *(const uint16_t *) (cmap + 0x02);
		int mappingType = *(const uint16_t *) (cmap + 0x04);
		const unsigned char *mappingData = (const unsigned char *) (cmap + 0xC);

		NFTR_GLYPH dummy = { 0 };

		switch (mappingType) {
			case NFTR_MAP_DIRECT: // continuous block (direct)
			{
				uint16_t base = *(const uint16_t *) (mappingData + 0);
				for (unsigned int i = 0; i < (cchi + 1u - cclo); i++) {
					NftrReadNftrGlyph(&dummy, nftr, buffer, offsWidth, cglp + 8, glyphSize, finf, cclo + i, base + i);
					StListAdd(&list, &dummy);
				}
				break;
			}
			case NFTR_MAP_TABLE: // table lookup
			{
				const uint16_t *table = (const uint16_t *) (mappingData + 0x00);
				for (unsigned int i = 0; i < (cchi - cclo + 1u); i++) {
					if (table[i] == 0xFFFF) continue; // glyph not found

					NftrReadNftrGlyph(&dummy, nftr, buffer, offsWidth, cglp + 8, glyphSize, finf, cclo + i, table[i]);
					StListAdd(&list, &dummy);
				}
				break;
			}
			case NFTR_MAP_SCAN: // direct block (scan)
			{
				unsigned int nEntries = *(const uint16_t *) (mappingData + 0x00);
				const uint16_t *table = (const uint16_t *) (mappingData + 0x02);
				for (unsigned int i = 0; i < nEntries; i++) {
					unsigned int glyphIndex = table[i * 2 + 1];
					NftrReadNftrGlyph(&dummy, nftr, buffer, offsWidth, cglp + 8, glyphSize, finf, table[i * 2 + 0], glyphIndex);
					StListAdd(&list, &dummy);
				}
				break;
			}
		}

		offsCmap = nextCmap;
	}
	StListMakeSorted(&list, NftrCodePointComparator);

	size_t count;
	nftr->glyphs = StListDecapsulate(&list, &count);
	nftr->nGlyph = count;

	//locate invalid glyph
	nftr->invalid = -1;
	for (int i = 0; i < nftr->nGlyph; i++) {
		if (nftr->glyphs[i].isInvalid) {
			nftr->invalid = i;
			break;
		}
	}

	return 0;
}

static int NftrReadNftr01(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	NftrInit(nftr, NFTR_TYPE_NFTR_01);
	return NftrReadNftrCommon(nftr, buffer, size);
}

static int NftrReadNftr10(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	NftrInit(nftr, NFTR_TYPE_NFTR_10);
	return NftrReadNftrCommon(nftr, buffer, size);
}

static int NftrReadNftr11(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	NftrInit(nftr, NFTR_TYPE_NFTR_11);
	return NftrReadNftrCommon(nftr, buffer, size);
}

static int NftrReadNftr12(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	NftrInit(nftr, NFTR_TYPE_NFTR_12);
	return NftrReadNftrCommon(nftr, buffer, size);
}

int NftrRead(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	switch (NftrIdentify(buffer, size)) {
		case NFTR_TYPE_NFTR_01:
			return NftrReadNftr01(nftr, buffer, size);
		case NFTR_TYPE_NFTR_10:
			return NftrReadNftr10(nftr, buffer, size);
		case NFTR_TYPE_NFTR_11:
			return NftrReadNftr11(nftr, buffer, size);
		case NFTR_TYPE_NFTR_12:
			return NftrReadNftr12(nftr, buffer, size);
		case NFTR_TYPE_BNFR_20:
			return NftrReadBnfr20(nftr, buffer, size);
		case NFTR_TYPE_BNFR_12:
			return NftrReadBnfr12(nftr, buffer, size);
		case NFTR_TYPE_BNFR_11:
			return NftrReadBnfr11(nftr, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

int NftrReadFile(NFTR *nftr, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) nftr, (OBJECT_READER) NftrRead);
}

// ----- Font operations

int NftrGetGlyphIndexByCP(NFTR *nftr, uint16_t cp) {
	if (nftr->nGlyph == 0) return -1;

	//search binary for glyph by code point
	int lo = 0, hi = nftr->nGlyph;
	while ((hi - lo) >= 1) {
		int med = lo + (hi - lo) / 2;
		NFTR_GLYPH *atmed = &nftr->glyphs[med];

		if (atmed->cp > cp) {
			//keep bottom half of search zone, discarding med
			hi = med;
		} else if (atmed->cp < cp) {
			//keep top half of search zone, discarding med
			lo = med + 1;
		} else {
			//found
			return med;
		}
	}

	if (lo < nftr->nGlyph && nftr->glyphs[lo].cp == cp) return lo;
	return -1;
}

int NftrGetInvalidGlyphIndex(NFTR *nftr) {
	//linear search
	for (int i = 0; i < nftr->nGlyph; i++) {
		if (nftr->glyphs[i].isInvalid) return i;
	}
	return -1;
}

NFTR_GLYPH *NftrGetGlyphByCP(NFTR *nftr, uint16_t cp) {
	int i = NftrGetGlyphIndexByCP(nftr, cp);
	if (i == -1) return NULL;

	return &nftr->glyphs[i];
}

NFTR_GLYPH *NftrGetInvalidGlyph(NFTR *nftr) {
	int i = NftrGetInvalidGlyphIndex(nftr);
	if (i == -1) return NULL;

	return &nftr->glyphs[i];
}

void NftrEnsureSorted(NFTR *nftr) {
	if (nftr->hasCodeMap) {
		qsort(nftr->glyphs, nftr->nGlyph, sizeof(NFTR_GLYPH), NftrCodePointComparator);
	}
}

void NftrSetBitDepth(NFTR *nftr, int depth) {
	//check invalid depth or same depth
	if (depth != 1 && depth != 2 && depth != 4) return;
	if (depth == nftr->bpp) return;

	//update pixel data
	for (int i = 0; i < nftr->nGlyph; i++) {
		NFTR_GLYPH *glyph = &nftr->glyphs[i];
		unsigned char *px = glyph->px;
		int nPx = nftr->cellWidth * nftr->cellHeight;

		for (int j = 0; j < nPx; j++) {
			int curdepth = nftr->bpp;
			while (depth < curdepth) {
				curdepth /= 2;
				px[j] = (px[j] >> curdepth);
			}
			while (depth > curdepth) {
				px[j] = (px[j] << curdepth) | (px[j]);
				curdepth *= 2;
			}
		}
	}

	//update depth
	nftr->bpp = depth;
}

void NftrSetCellSize(NFTR *nftr, int width, int height) {
	//if size unchanged, nothing
	if (nftr->cellWidth == width && nftr->cellHeight == height) return;

	int copyW = min(nftr->cellWidth, width);
	int copyH = min(nftr->cellHeight, height);

	//rearrange glyphs
	for (int i = 0; i < nftr->nGlyph; i++) {
		NFTR_GLYPH *glyph = &nftr->glyphs[i];
		unsigned char *newbuf = (unsigned char *) calloc(width * height, 1);

		//copy
		for (int y = 0; y < copyH; y++) {
			for (int x = 0; x < copyW; x++) {
				newbuf[x + y * width] = glyph->px[x + y * nftr->cellWidth];
			}
		}

		free(glyph->px);
		glyph->px = newbuf;
	}

	//update info
	nftr->cellWidth = width;
	nftr->cellHeight = height;
}


// ----- Font write routines

static void NftrWriteGlyphToBytes(unsigned char *buf, NFTR *nftr, NFTR_GLYPH *glyph, int isJNFR) {
	unsigned char pxmask = (1 << nftr->bpp) - 1;
	unsigned int pxPerByte = 8 / nftr->bpp;
	unsigned char pxXor = 0;
	if (isJNFR) pxXor = 0xFF; // invert pixel color for JNFR

	for (int y = 0; y < nftr->cellHeight; y++) {
		for (int x = 0; x < nftr->cellWidth; x++) {
			unsigned int pxno = x + y * nftr->cellWidth;

			unsigned char pxval = (glyph->px[pxno] ^ pxXor) & pxmask;
			if (isJNFR) {
				//JNFR format glyphs: store at least significant bits first
				buf[pxno / pxPerByte] |= pxval << ((pxno % pxPerByte) * nftr->bpp);
			} else {
				//else: store as most significant bits first
				buf[pxno / pxPerByte] |= pxval << ((pxPerByte - 1 - (pxno % pxPerByte)) * nftr->bpp);
			}
		}
	}
}

static void NftrWriteGlyphToStream(BSTREAM *stream, NFTR *nftr, NFTR_GLYPH *glyph, int isJNFR) {
	//create buffer
	unsigned int nBytes = (nftr->cellWidth * nftr->cellHeight * nftr->bpp + 7) / 8;
	if (isJNFR) {
		//round up glyph size to a halfword alignment
		nBytes = (nBytes + 1) & ~1;
	}

	unsigned char *buf = (unsigned char *) calloc(nBytes, 1);
	NftrWriteGlyphToBytes(buf, nftr, glyph, isJNFR);
	bstreamWrite(stream, buf, nBytes);
	free(buf);
}

static int NftrWriteBnfr20(NFTR *nftr, BSTREAM *stream) {
	//decide on the glyph ordering based on a code map we construct. We'll make one pass over the glyph
	//list, looking for 3 or more consecutive glyphs by code point. We'll write those glyphs first, then
	//afterwards we run back over and catch all missed glyphs.
	unsigned char *glyphsWritten = (unsigned char *) calloc(nftr->nGlyph, 1);
	BSTREAM mapConStream, mapDirStream, conGlyphStream, dirGlyphStream, conWidStream, dirWidStream;
	bstreamCreate(&mapConStream, NULL, 0); bstreamCreate(&conGlyphStream, NULL, 0); bstreamCreate(&conWidStream, NULL, 0);
	bstreamCreate(&mapDirStream, NULL, 0); bstreamCreate(&dirGlyphStream, NULL, 0); bstreamCreate(&dirWidStream, NULL, 0);

	for (int i = 0; i < nftr->nGlyph;) {
		uint16_t cc = nftr->glyphs[i].cp;

		//search forwards
		int nRun = 1;
		for (int j = i + 1; j < nftr->nGlyph; j++) {
			if (nftr->glyphs[j].cp == (nftr->glyphs[j - 1].cp + 1)) nRun++;
			else break;
		}

		//if run >= 3, we'll emit a continuous block. if run < 3, we'll emit them directly.
		//a run of 2 costs the same either continuous or direct, but making them direct
		//allows the continuous lookup to complete quicker.
		int isContinuous = 0;
		if (nRun >= 3) {
			uint16_t con[] = { cc, nftr->glyphs[i + nRun - 1].cp };
			bstreamWrite(&mapConStream, con, sizeof(con));
			isContinuous = 1;
		} else {
			for (int j = 0; j < nRun; j++) {
				uint16_t cc2 = cc + j;
				bstreamWrite(&mapDirStream, &cc2, sizeof(cc2));
			}
			isContinuous = 0;
		}

		//write nRun glyphs to glyph buffer
		for (int j = 0; j < nRun; j++) {
			//glyph
			BSTREAM *glyphStream = isContinuous ? &conGlyphStream : &dirGlyphStream;
			NftrWriteGlyphToStream(glyphStream, nftr, &nftr->glyphs[i + j], 1);

			//width data
			BSTREAM *widStream = isContinuous ? &conWidStream : &dirWidStream;
			uint8_t widData[2];
			widData[0] = nftr->glyphs[i + j].spaceLeft;
			widData[1] = nftr->glyphs[i + j].width;
			bstreamWrite(widStream, widData, sizeof(widData));
		}

		i += nRun;
	}

	//write file header
	unsigned char fileHeader[] = { 'J', 'N', 'F', 'R', 0, 2, 0xFF, 0xFE, 3, 0, 0, 0 };
	bstreamWrite(stream, fileHeader, sizeof(fileHeader));

	//IMG1 block
	unsigned char img1Header[] = { 'I', 'M', 'G', '1', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint32_t *) (img1Header + 0x04) = sizeof(img1Header) + ((conGlyphStream.size + dirGlyphStream.size + 3) & ~3);
	*(uint32_t *) (img1Header + 0x08) = sizeof(img1Header);
	*(uint8_t *) (img1Header + 0x0C) = nftr->cellWidth;
	*(uint8_t *) (img1Header + 0x0D) = nftr->cellHeight;
	*(uint8_t *) (img1Header + 0x0E) = 0; // pixel info TOOD
	*(uint8_t *) (img1Header + 0x0F) = nftr->pxLeading;
	*(uint8_t *) (img1Header + 0x10) = nftr->pxAscent;
	*(uint8_t *) (img1Header + 0x11) = nftr->pxDescent;
	*(uint16_t *) (img1Header + 0x12) = nftr->invalid;
	bstreamWrite(stream, img1Header, sizeof(img1Header));
	bstreamWrite(stream, conGlyphStream.buffer, conGlyphStream.size);
	bstreamWrite(stream, dirGlyphStream.buffer, dirGlyphStream.size);
	bstreamAlign(stream, 4);

	//WID1 block
	unsigned char wid1Header[] = { 'W', 'I', 'D', '1', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint32_t *) (wid1Header + 0x04) = sizeof(wid1Header) + ((conWidStream.size + dirWidStream.size + 3) & ~3);
	*(uint32_t *) (wid1Header + 0x08) = sizeof(wid1Header);
	*(uint16_t *) (wid1Header + 0x0C) = nftr->nGlyph;
	bstreamWrite(stream, wid1Header, sizeof(wid1Header));
	bstreamWrite(stream, conWidStream.buffer, conWidStream.size);
	bstreamWrite(stream, dirWidStream.buffer, dirWidStream.size);
	bstreamAlign(stream, 4);

	//CMP1 block
	unsigned char cmp1Header[] = { 'C', 'M', 'P', '1', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint32_t *) (cmp1Header + 0x04) = sizeof(cmp1Header) + ((mapConStream.size + mapDirStream.size + 3) & ~3);
	*(uint32_t *) (cmp1Header + 0x08) = sizeof(cmp1Header);
	*(uint32_t *) (cmp1Header + 0x0C) = sizeof(cmp1Header) + mapConStream.size;
	*(uint16_t *) (cmp1Header + 0x10) = mapConStream.size / 4;
	*(uint16_t *) (cmp1Header + 0x12) = mapDirStream.size / 2;
	bstreamWrite(stream, cmp1Header, sizeof(cmp1Header));
	bstreamWrite(stream, mapConStream.buffer, mapConStream.size);
	bstreamWrite(stream, mapDirStream.buffer, mapDirStream.size);
	bstreamAlign(stream, 4);

	bstreamFree(&mapConStream); bstreamFree(&conGlyphStream); bstreamFree(&conWidStream);
	bstreamFree(&mapDirStream); bstreamFree(&dirGlyphStream); bstreamFree(&dirWidStream);

	free(glyphsWritten);

	return OBJ_STATUS_SUCCESS;
}


// ----- NFTR code mapping routines

//structure used to sort the font glyphs by their width bytes
typedef struct FontGlyphCpWidth_ {
	uint16_t cp;
	uint16_t i;
	int pxLeading;
	int pxWidth;
	int pxTrailing;
} FontGlyphCpWidth;

typedef struct FontGlyphWidBlock_ {
	uint16_t cplo;
	uint16_t cphi;
	unsigned char *wid;
} FontGlyphWidBlock;

static int FontGlyphWidthComparator(const void *v1, const void *v2) {
	const FontGlyphCpWidth *w1 = (const FontGlyphCpWidth *) v1;
	const FontGlyphCpWidth *w2 = (const FontGlyphCpWidth *) v2;

	//compare data corresponding to first 2 bytes of the width data
	if ((w1->pxLeading & 0xFF) < (w2->pxLeading & 0xFF)) return -1;
	if ((w1->pxLeading & 0xFF) > (w2->pxLeading & 0xFF)) return 1;
	if ((w1->pxWidth   & 0xFF) < (w2->pxWidth   & 0xFF)) return -1;
	if ((w1->pxWidth   & 0xFF) > (w2->pxWidth   & 0xFF)) return 1;

	//compare data for the last byte of width data
	int total1 = w1->pxLeading + w1->pxWidth + w1->pxTrailing;
	int total2 = w2->pxLeading + w2->pxWidth + w2->pxTrailing;
	if ((total1 & 0xFF) < (total2 & 0xFF)) return -1;
	if ((total1 & 0xFF) < (total2 & 0xFF)) return 1;
	
	//compare by code point as final determiner
	if (w1->cp < w2->cp) return -1;
	if (w1->cp > w2->cp) return 1;
	return 0;
}

static int FontGlyphWidthComparatorCP(const void *v1, const void *v2) {
	const FontGlyphCpWidth *w1 = (const FontGlyphCpWidth *) v1;
	const FontGlyphCpWidth *w2 = (const FontGlyphCpWidth *) v2;

	if (w1->cp < w2->cp) return -1;
	if (w1->cp > w2->cp) return 1;
	return 0;
}

static unsigned int NftrGetCodeMapCost(int type, unsigned int nCP, unsigned int rangeCP) {
	switch (type) {
		case NFTR_MAP_DIRECT:
			if (nCP < rangeCP) return UINT_MAX; // not allowed
			return 0x18;
		case NFTR_MAP_TABLE:
			//array lookup
			return ((0x14 + 2 * rangeCP) + 3) & ~3;
		case NFTR_MAP_SCAN:
			//dictionary lookup
			return 0x18 + 4 * nCP;
	}
	//invalid
	return UINT_MAX;
}

static int NftrGetBestFitCodeMap(unsigned int nCP, unsigned int rangeCP) {
	unsigned int costD = NftrGetCodeMapCost(NFTR_MAP_DIRECT, nCP, rangeCP);
	unsigned int costT = NftrGetCodeMapCost(NFTR_MAP_TABLE, nCP, rangeCP);
	unsigned int costS = NftrGetCodeMapCost(NFTR_MAP_SCAN, nCP, rangeCP);

	if (costD <= costT && costD <= costS) return NFTR_MAP_DIRECT;
	if (costT <= costD && costT <= costS) return NFTR_MAP_TABLE;
	if (costS <= costD && costS <= costT) return NFTR_MAP_SCAN;
	return NFTR_MAP_SCAN;
}

static unsigned int NftrGetDivCost(NFTR *nftr, unsigned int i, unsigned int len) {
	//count used code points and range of code points
	uint16_t cpMin = nftr->glyphs[i].cp;
	uint16_t cpMax = nftr->glyphs[i + len - 1].cp;
	unsigned int cpRange = cpMax + 1 - cpMin;
	unsigned int cpUsed = len;

	int mapType = NftrGetBestFitCodeMap(cpUsed, cpRange);
	unsigned int mapCost = NftrGetCodeMapCost(mapType, cpUsed, cpRange);
	return mapCost;
}

static void NftrCreateNftrDivions(NFTR *nftr, StList *divs, StList *tailCP, StList *tailGlyphCP, StList *tailGlyph) {
	//create the initial groupings by collecting adjacent code points
	for (int i = 0; i < nftr->nGlyph;) {
		uint16_t cp1 = nftr->glyphs[i].cp;
		uint16_t cpLast = cp1;

		unsigned int nRun = 1;
		for (int j = i + 1; j < nftr->nGlyph; j++) {
			if (nftr->glyphs[j].cp == (cpLast + 1)) nRun++;
			else break;

			cpLast = nftr->glyphs[j].cp;
		}

		StListAdd(divs, &nRun);
		i += nRun;
	}

	//<= 1 glyphs, cannot be optimized further
	if (nftr->nGlyph <= 1) return;

	//begin merging divisions.
	do {
		//search for merge of most savings
		unsigned int bestSaving = 0, bestI = UINT_MAX;

		unsigned int curGlyph = 0;
		for (unsigned int i = 0; i < divs->length - 1; i++) {
			unsigned int *pLenDiv1 = StListGetPtr(divs, i);
			unsigned int *pLenDiv2 = StListGetPtr(divs, i + 1);
			unsigned int lenDiv1 = *pLenDiv1, lenDiv2 = *pLenDiv2;

			unsigned int nCP1 = lenDiv1, nCP2 = lenDiv2;
			unsigned int rangeCP1 = nftr->glyphs[curGlyph + lenDiv1 - 1].cp + 1 - nftr->glyphs[curGlyph].cp;
			unsigned int rangeCP2 = nftr->glyphs[curGlyph + lenDiv1 + lenDiv2 - 1].cp + 1 - nftr->glyphs[curGlyph + lenDiv1].cp;
			unsigned int rangeCPNew = nftr->glyphs[curGlyph + lenDiv1 + lenDiv2 - 1].cp + 1 - nftr->glyphs[curGlyph].cp;

			//get types and costs
			int type1 = NftrGetBestFitCodeMap(nCP1, rangeCP1);
			int type2 = NftrGetBestFitCodeMap(nCP2, rangeCP2);
			unsigned int cost1 = NftrGetCodeMapCost(type1, nCP1, rangeCP1);
			unsigned int cost2 = NftrGetCodeMapCost(type2, nCP2, rangeCP2);
			unsigned int costOld = cost1 + cost2;

			int typeNew = NftrGetBestFitCodeMap(nCP1 + nCP2, rangeCPNew);
			unsigned int costNew = NftrGetCodeMapCost(typeNew, nCP1 + nCP2, rangeCPNew);
			if (costNew <= costOld) {
				unsigned int saving = costOld - costNew;
				if (saving >= bestSaving) {
					bestSaving = saving;
					bestI = i;
				}
			}

			curGlyph += lenDiv1;
		}

		//check merge found
		if (bestI == UINT_MAX) break;

		//merge
		unsigned int *pLen1 = StListGetPtr(divs, bestI);
		unsigned int *pLen2 = StListGetPtr(divs, bestI + 1);
		*pLen1 += *pLen2;
		StListRemove(divs, bestI + 1);

	} while (1);

	// next: collect a set of loose code points and scan mapping blocks and put them at the end
	// we'll create the tail scan if:
	//  1. at least one scan block exists
	//  2. at least 2 mapping blocks have just 2 code point contained
	int createTailScan = 0;
	int isTailFree = 0; // is the tail block overhead accounted for
	unsigned int curGlyph = 0, nSmallBlock = 0;
	for (unsigned int i = 0; i < divs->length; i++) {
		unsigned int *pLenDiv = StListGetPtr(divs, i);
		unsigned int lenDiv = *pLenDiv;

		unsigned int nCP = lenDiv;
		unsigned int rangeCP = nftr->glyphs[curGlyph + lenDiv - 1].cp + 1 - nftr->glyphs[curGlyph].cp;
		int type = NftrGetBestFitCodeMap(nCP, rangeCP);

		//scan block: create tail scan
		if (type == NFTR_MAP_SCAN) {
			createTailScan = 1;
			isTailFree = 1; // we have at least one scan block already, thus the fixed cost is "free"
		}

		//"small" blocks: if we have at least 2, create tail block
		if (nCP <= 2) {
			nSmallBlock++;
			if (nSmallBlock >= 2) {
				createTailScan = 1;
			}
		}

		curGlyph += lenDiv;
	}

	if (!createTailScan) return;

	StList listCpWidth;
	StListCreateInline(&listCpWidth, FontGlyphCpWidth, NULL);

	curGlyph = 0;
	for (unsigned int i = 0; i < divs->length; i++) {
		unsigned int *pLenDiv = StListGetPtr(divs, i);
		unsigned int lenDiv = *pLenDiv;

		unsigned int nCP = lenDiv;
		unsigned int rangeCP = nftr->glyphs[curGlyph + lenDiv - 1].cp + 1 - nftr->glyphs[curGlyph].cp;
		int type = NftrGetBestFitCodeMap(nCP, rangeCP);
		unsigned int cost = NftrGetCodeMapCost(type, nCP, rangeCP);

		int makeTail = 0;
		if (type == NFTR_MAP_SCAN) {
			//scan: always put to tail
			makeTail = 1;
		} else if (nCP <= 2) {
			//small block: 
			makeTail = 1;
		} else if (isTailFree) {
			//if the tail block is already accounted for, see what code points we can move there as well
			unsigned int movedCost = 4 * nCP;
			if (movedCost <= cost) {
				makeTail = 1;
			}
		}

		if (makeTail) {
			//add all code points in this block to the tail block
			for (unsigned int j = 0; j < lenDiv; j++) {
				//add CP-width data mapping
				FontGlyphCpWidth gcpw = { 0 };
				gcpw.cp = nftr->glyphs[curGlyph + j].cp;
				gcpw.pxLeading = nftr->glyphs[curGlyph + j].spaceLeft;
				gcpw.pxWidth = nftr->glyphs[curGlyph + j].width;
				gcpw.pxTrailing = nftr->glyphs[curGlyph + j].spaceRight;
				gcpw.i = curGlyph + j;
				StListAdd(&listCpWidth, &gcpw);
			}

			//mark as skip
			*pLenDiv |= 0x80000000;
			isTailFree = 1;
		}

		curGlyph += lenDiv;
	}

	StListSort(&listCpWidth, FontGlyphWidthComparator);

	//find most repeated width in the block
	unsigned int longestRun = 1, longestRunI = 0;
	for (unsigned int i = 0; i < listCpWidth.length;) {
		FontGlyphCpWidth *gcpw = StListGetPtr(&listCpWidth, i);
		
		unsigned int nRun = 1;
		for (unsigned int j = i + 1; j < listCpWidth.length; j++) {
			FontGlyphCpWidth *gcpw2 = StListGetPtr(&listCpWidth, j);

			if (gcpw2->pxLeading != gcpw->pxLeading) break;
			if (gcpw2->pxWidth != gcpw->pxWidth) break;
			if (gcpw2->pxTrailing != gcpw->pxTrailing) break;
			nRun++;
		}

		if (nRun > longestRun) {
			longestRun = nRun;
			longestRunI = i;
		}

		i += nRun;
	}

	//move repeated block to the end of the list
	{
		void *blockSrc = StListGetPtr(&listCpWidth, longestRunI);
		void *blockDst = StListGetPtr(&listCpWidth, listCpWidth.length - longestRun);

		void *cpy = malloc(longestRun * sizeof(FontGlyphCpWidth));
		memcpy(cpy, blockSrc, longestRun * sizeof(FontGlyphCpWidth));

		memmove(blockSrc, StListGetPtr(&listCpWidth, longestRunI + longestRun), (listCpWidth.length - longestRunI - longestRun) * sizeof(FontGlyphCpWidth));
		memcpy(blockDst, cpy, longestRun * sizeof(FontGlyphCpWidth));
		free(cpy);
	}

	//sort the tail list by the width data.
	for (unsigned int i = 0; i < listCpWidth.length; i++) {
		uint16_t cp;
		FontGlyphCpWidth *gcpw = StListGetPtr(&listCpWidth, i);
		gcpw->i = i; // assign final index

		cp = gcpw->cp;
		StListAdd(tailCP, &cp); // code points in glyph order
	}

	StListSort(&listCpWidth, FontGlyphWidthComparatorCP);

	//width data is in code point order, write out tail glyph list
	for (unsigned int i = 0; i < listCpWidth.length; i++) {
		FontGlyphCpWidth *gcpw = StListGetPtr(&listCpWidth, i);
		uint16_t cp = gcpw->cp;
		uint16_t glyphi = gcpw->i;

		StListAdd(tailGlyph, &glyphi); // glyphs in code point order
		StListAdd(tailGlyphCP, &cp);
	}

	StListFree(&listCpWidth);
}

static int NftrSearchMatchingNftrGlyph(
	const unsigned char *bmp, 
	unsigned int glyphSize, 
	const unsigned char *wid,
	unsigned int widSize,
	const unsigned char *glyphs,
	unsigned int glyphsSize,
	const unsigned char *wids,
	unsigned int widsSize
) {
	//linear search
	unsigned int nGlyph = glyphsSize / glyphSize;
	if (nGlyph > widsSize / widSize) nGlyph = widsSize / widSize;

	for (unsigned int i = 0; i < nGlyph; i++) {
		const unsigned char *cmpGlp = glyphs + glyphSize * i;
		const unsigned char *cmpWid = wids + widSize * i;
		if (memcmp(bmp, cmpGlp, glyphSize)) continue;
		if (memcmp(wid, cmpWid, widSize)) continue;

		//found
		return (int) i;
	}

	return -1;
}

static int NftrWriteNftrCommon(NFTR *nftr, BSTREAM *stream) {
	int verMajor = 1, verMinor = 0;
	switch (nftr->header.format) {
		case NFTR_TYPE_NFTR_01: verMajor = 0; verMinor = 1; break;
		case NFTR_TYPE_NFTR_10: verMajor = 1; verMinor = 0; break;
		case NFTR_TYPE_NFTR_11: verMajor = 1; verMinor = 1; break;
		case NFTR_TYPE_NFTR_12: verMajor = 1; verMinor = 2; break;
	}

	//NFTR 0.1: does not store trailing pixels
	unsigned int widEntrySize = (nftr->header.format == NFTR_TYPE_NFTR_01) ? 2 : 3;

	NnsStream nns;
	NnsStreamCreate(&nns, "NFTR", verMajor, verMinor, NNS_TYPE_G2D, NNS_SIG_LE);

	//we'll create divisions. We will create a list of lengths of glyph runs.
	StList divList, tailListCP, tailListGlyphCP, tailListGlyph;
	StListCreateInline(&divList, unsigned int, NULL);      // list of code map division lengths
	StListCreateInline(&tailListCP, uint16_t, NULL);       // list of tail block code points in ascending order of glyph
	StListCreateInline(&tailListGlyphCP, uint16_t, NULL);  // list of tail block code points in ascending order of code point
	StListCreateInline(&tailListGlyph, uint16_t, NULL);    // list of tail block glyph indices in ascending order of code point
	NftrCreateNftrDivions(nftr, &divList, &tailListCP, &tailListGlyphCP, &tailListGlyph);

	//write glyph data to streams
	unsigned int iInvalid = 0, iOutGlyph = 0;
	BSTREAM glyphStream, widStream;
	bstreamCreate(&glyphStream, NULL, 0);
	bstreamCreate(&widStream, NULL, 0);
	{
		//process amin blocks
		unsigned int curGlyphIdx = 0;
		for (unsigned int i = 0; i < divList.length; i++) {
			unsigned int *pLen = StListGetPtr(&divList, i);
			unsigned int len = *pLen & ~0x80000000;

			if (!(*pLen & 0x80000000)) {
				for (unsigned int j = 0; j < len; j++) {
					//write glyph and width
					unsigned int gidx = curGlyphIdx + j;
					unsigned char wid[3];
					wid[0] = nftr->glyphs[gidx].spaceLeft;
					wid[1] = nftr->glyphs[gidx].width;
					wid[2] = nftr->glyphs[gidx].width + nftr->glyphs[gidx].spaceLeft + nftr->glyphs[gidx].spaceRight;

					NftrWriteGlyphToStream(&glyphStream, nftr, &nftr->glyphs[gidx], 0);
					bstreamWrite(&widStream, wid, widEntrySize);
					if (nftr->glyphs[gidx].isInvalid) iInvalid = iOutGlyph;
					iOutGlyph++;
				}
			}

			curGlyphIdx += len;
		}

		//process tail mapping block
		if (tailListCP.length > 0) {
			unsigned int glyphSize = (nftr->cellWidth * nftr->cellHeight * nftr->bpp + 7) / 8;
			unsigned char *tmpbuf = (unsigned char *) calloc(glyphSize, 1);

			//we process the glyphs in this particular order determined by the division process that is
			//out of order of code point to ensure a good ordering of the width data. Identical widths are
			//clustered together to maximize runs at the end of the scan-mapped glyph width.
			for (unsigned int i = 0; i < tailListCP.length; i++) {
				uint16_t cp;
				StListGet(&tailListCP, i, &cp);

				unsigned char wid[3];
				NFTR_GLYPH *glyph = NftrGetGlyphByCP(nftr, cp);
				wid[0] = glyph->spaceLeft;
				wid[1] = glyph->width;
				wid[2] = glyph->width + glyph->spaceLeft + glyph->spaceRight;

				//test: search for the glyph bitmap before
				NftrWriteGlyphToBytes(tmpbuf, nftr, glyph, 0);
				int found = NftrSearchMatchingNftrGlyph(tmpbuf, glyphSize, wid, widEntrySize, glyphStream.buffer, glyphStream.size,
					widStream.buffer, widStream.size);

				int mapIndex = StListIndexOf(&tailListGlyphCP, &cp);
				uint16_t *pMappedToGlyph = StListGetPtr(&tailListGlyph, mapIndex);

				if (!glyph->isInvalid && found != -1) {
					//glyph match was found
					*pMappedToGlyph = found;
				} else {
					//glyph was not found, write out
					NftrWriteGlyphToStream(&glyphStream, nftr, glyph, 0);
					bstreamWrite(&widStream, wid, widEntrySize);
					if (glyph->isInvalid) iInvalid = iOutGlyph;
					*pMappedToGlyph = iOutGlyph++;
				}
			}
			free(tmpbuf);
		}
	}

	//optimize width block
	unsigned char defWid[3] = { 0 };
	uint16_t widFirst = 0, widLast = iOutGlyph - 1;

	StList listWidBlock;
	StListCreateInline(&listWidBlock, FontGlyphWidBlock, NULL);

	if (iOutGlyph > 0) {
		//We'll optimize the size of the width data block by cutting repeated width data.
		//We must find the width data that saves the most space when cut, so we'll do that
		//here.

		//compute cost savings for cutting width values
		StMap savingMap;
		StMapCreate(&savingMap, widEntrySize, sizeof(unsigned int));

		for (unsigned int i = 0; i < iOutGlyph;) {
			//ensure an entry for the width data
			const unsigned char *entryI = widStream.buffer + i * widEntrySize;
			if (StMapGetPtr(&savingMap, entryI) == NULL) {
				unsigned int dummy = 0;
				StMapPut(&savingMap, entryI, &dummy);
			}

			//find the number of identical contiguous entries
			unsigned int nRun = 1;
			for (unsigned int j = i + 1; j < iOutGlyph; j++) {
				const unsigned char *entryJ = widStream.buffer + j * widEntrySize;
				if (memcmp(entryI, entryJ, widEntrySize) != 0) break;

				nRun++;
			}
			
			//determine cost savings by cutting this range
			unsigned int runCost = nRun * widEntrySize;
			unsigned int splitCost = 0x10;
			if (i == 0 || (i + nRun) == iOutGlyph) splitCost = 0; // no added block overhead to cut beginning/end

			if (runCost > splitCost) {
				//compute amount saved (bytes omitted, less the size of block overhead)
				unsigned int saved = runCost - splitCost;
				unsigned int curSaved = 0x12345678;
				StMapGet(&savingMap, entryI, &curSaved);
				curSaved += saved;
				StMapPut(&savingMap, entryI, &curSaved);
			}

			i += nRun;
		}

		//determine default width by the entry that save the most space.
		unsigned int mostSavings = 0;
		unsigned char bestWidth[3];
		for (unsigned int i = 0; i < savingMap.list.length; i++) {
			unsigned char key[3];
			unsigned int value;
			StMapGetKeyValue(&savingMap, i, key, &value);

			if (value > mostSavings) {
				mostSavings = value;
				memcpy(bestWidth, key, widEntrySize);
			}
		}
		StMapFree(&savingMap);

		//set default glyph
		memcpy(defWid, bestWidth, widEntrySize);

		//create width blocks
		for (unsigned int i = 0; i < iOutGlyph;) {
			const unsigned char *entryI = widStream.buffer + i * widEntrySize;

			//if the start of this block would be a default width, skip them.
			if (memcmp(entryI, defWid, widEntrySize) == 0) {
				while (memcmp(entryI, defWid, widEntrySize) == 0 && i < iOutGlyph) {
					entryI += widEntrySize;
					i++;
				}
				continue;
			}

			//scan forward
			unsigned int iStart = i, iEnd = i;
			unsigned int defCount = 0;
			while (i < iOutGlyph) {
				const unsigned char *entryI2 = widStream.buffer + i * widEntrySize;

				if (memcmp(entryI2, defWid, widEntrySize) == 0) {
					//default width
					defCount++;
					if (defCount > (0x10 / widEntrySize)) {
						break;
					}
				} else {
					//normal width
					defCount = 0;
					iEnd = i;
				}

				i++;
			}

			//create a mapping from iStart to iEnd.
			FontGlyphWidBlock block;
			block.cplo = iStart;
			block.cphi = iEnd;
			block.wid = (unsigned char *) calloc(iEnd + 1 - iStart, widEntrySize);
			memcpy(block.wid, widStream.buffer + iStart * widEntrySize, (iEnd + 1 - iStart) * widEntrySize);
			StListAdd(&listWidBlock, &block);
		}
	}

	bstreamFree(&widStream);

	unsigned int finfSize = 0x1C; // constant
	unsigned int cglpSize = (0x10 + glyphStream.size + 3) & ~3;
	unsigned int cwdhSize = 0x10 * listWidBlock.length;
	for (unsigned int i = 0; i < listWidBlock.length; i++) {
		FontGlyphWidBlock block;
		StListGet(&listWidBlock, i, &block);

		cwdhSize += (((block.cphi + 1 - block.cplo) * widEntrySize) + 3) & ~3;
	}
	
	unsigned int offsCglp = 0x10 + finfSize + 8;
	unsigned int offsCwdh = 0x10 + finfSize + cglpSize + 8;
	unsigned int offsCmap = 0x10 + finfSize + cglpSize + cwdhSize + 8;
	if (nftr->header.format == NFTR_TYPE_NFTR_12) {
		//increase size of FINF
		offsCglp += 4;
		offsCwdh += 4;
		offsCmap += 4;
	}

	//FINF block
	{
		unsigned char finf[20] = { 0 };
		*(uint8_t *) (finf + 0x00) = 0; // bitmap
		*(uint8_t *) (finf + 0x01) = nftr->lineHeight;
		*(uint16_t *) (finf + 0x02) = iInvalid;     // index of invalid glyph
		*(uint8_t *) (finf + 0x04) = defWid[0];     // default leading
		*(uint8_t *) (finf + 0x05) = defWid[1];     // default width
		*(uint8_t *) (finf + 0x06) = defWid[2];     // default total width
		*(uint8_t *) (finf + 0x07) = (uint8_t) nftr->charset;
		*(uint32_t *) (finf + 0x08) = offsCglp;
		*(uint32_t *) (finf + 0x0C) = offsCwdh;
		*(uint32_t *) (finf + 0x10) = offsCmap;

		NnsStreamStartBlock(&nns, "FINF");
		NnsStreamWrite(&nns, finf, sizeof(finf));

		//extra data for NFTR 1.2
		if (nftr->header.format == NFTR_TYPE_NFTR_12) {
			unsigned char exFinf[4];
			exFinf[0] = nftr->cellHeight; // height
			exFinf[1] = nftr->cellWidth;  // width
			exFinf[2] = nftr->pxAscent;   // ascent
			exFinf[3] = 0; //padding
			NnsStreamWrite(&nns, exFinf, sizeof(exFinf));
		}
		NnsStreamEndBlock(&nns);
	}

	//CGLP block
	{
		int maxWidth = 0;
		for (int i = 0; i < nftr->nGlyph; i++) {
			NFTR_GLYPH *glyph = &nftr->glyphs[i];
			int width = glyph->width + glyph->spaceLeft + glyph->spaceRight;

			if (width > maxWidth) {
				maxWidth = width;
			}
		}

		unsigned char flag = 0;
		if (nftr->header.format == NFTR_TYPE_NFTR_11 || nftr->header.format == NFTR_TYPE_NFTR_12) {
			if (nftr->vertical) flag |= 0x01; // vertical flag
			flag |= ((unsigned int) nftr->rotation) << 1;
		}

		unsigned char cglp[8] = { 0 };
		cglp[0] = nftr->cellWidth;   // cell width
		cglp[1] = nftr->cellHeight;  // cell height
		*(uint16_t *) (cglp + 2) = ((nftr->cellWidth * nftr->cellHeight * nftr->bpp) + 7) / 8;
		cglp[4] = nftr->pxAscent;    // glyph ascent
		cglp[5] = maxWidth;          // glyph max width
		cglp[6] = nftr->bpp;         // glyph bit depth
		cglp[7] = flag;              // glyph flags (NFTR 1.1, 1.2)

		NnsStreamStartBlock(&nns, "CGLP");
		NnsStreamWrite(&nns, cglp, sizeof(cglp));
		NnsStreamWrite(&nns, glyphStream.buffer, glyphStream.size);
		NnsStreamEndBlock(&nns);
	}

	//CWDH block
	unsigned int curWidOffs = offsCwdh;
	for (unsigned int i = 0; i < listWidBlock.length; i++) {
		FontGlyphWidBlock *wid = StListGetPtr(&listWidBlock, i);
		unsigned int blockSize = (wid->cphi + 1 - wid->cplo) * widEntrySize;
		unsigned int blockSizeRound = (blockSize + 3) & ~3;

		unsigned char wid1[8] = { 0 };
		*(uint16_t *) (wid1 + 0x00) = wid->cplo; // first glyph
		*(uint16_t *) (wid1 + 0x02) = wid->cphi; // last glyph
		*(uint32_t *) (wid1 + 0x04) = 0;         // no next block
		if (i < (listWidBlock.length - 1)) {
			*(uint32_t *) (wid1 + 0x04) = curWidOffs + 0x10 + blockSizeRound;
		}

		NnsStreamStartBlock(&nns, "CWDH");
		NnsStreamWrite(&nns, wid1, sizeof(wid1));
		NnsStreamWrite(&nns, wid->wid, blockSize);
		NnsStreamEndBlock(&nns);
		curWidOffs += 0x10 + blockSizeRound;
	}

	//mapping blocks
	unsigned int curGlyphIdx = 0;    // current glyph index in NFTR we're writing mapping for
	unsigned int curMapGlyphIdx = 0; // current glyph map index we're writing
	unsigned int offsCurCmap = offsCmap;
	for (unsigned int i = 0; i < divList.length; i++) {
		unsigned int *pLen = StListGetPtr(&divList, i);
		unsigned int len = *pLen & ~0x80000000;

		if (!(*pLen & 0x80000000)) {
			NnsStreamStartBlock(&nns, "CMAP");

			//get type
			uint16_t cpLo = nftr->glyphs[curGlyphIdx].cp;
			uint16_t cpHi = nftr->glyphs[curGlyphIdx + len - 1].cp;
			unsigned int nCP = len;
			unsigned int rangeCP = cpHi + 1 - cpLo;
			int type = NftrGetBestFitCodeMap(nCP, rangeCP);
			unsigned int cost = NftrGetCodeMapCost(type, nCP, rangeCP);

			unsigned int offsNext = offsCurCmap + cost;
			if (i == (divList.length - 1) && (tailListCP.length == 0)) {
				offsNext = 0; // tail block (no next block pointer)
			}

			unsigned char mapHeader[12];
			*(uint16_t *) (mapHeader + 0x00) = cpLo;
			*(uint16_t *) (mapHeader + 0x02) = cpHi;
			*(uint16_t *) (mapHeader + 0x04) = type;
			*(uint16_t *) (mapHeader + 0x06) = 0;
			*(uint32_t *) (mapHeader + 0x08) = offsNext;
			NnsStreamWrite(&nns, mapHeader, sizeof(mapHeader));

			switch (type) {
				case NFTR_MAP_DIRECT:
				{
					uint16_t directData = curMapGlyphIdx;
					NnsStreamWrite(&nns, &directData, sizeof(directData));
					break;
				}
				case NFTR_MAP_TABLE:
				{
					uint16_t *table = (uint16_t *) calloc(rangeCP, sizeof(uint16_t));
					for (unsigned int j = 0; j < rangeCP; j++) table[j] = 0xFFFF;

					for (unsigned int j = 0; j < nCP; j++) {
						table[nftr->glyphs[j + curGlyphIdx].cp - cpLo] = curMapGlyphIdx + j;
					}

					NnsStreamWrite(&nns, table, rangeCP * sizeof(uint16_t));
					free(table);
					break;
				}
				case NFTR_MAP_SCAN:
					//any scan block are moved to the end.
					break;
			}

			NnsStreamEndBlock(&nns);
			curMapGlyphIdx += len;
			offsCurCmap += cost;
		}
		curGlyphIdx += len;
	}

	if (tailListCP.length > 0) {
		//tail scan block
		unsigned char cmapHeader[14] = { 0 };
		*(uint16_t *) (cmapHeader + 0x00) = 0x0000; // entire code range (consistent with Nintendo)
		*(uint16_t *) (cmapHeader + 0x02) = 0xFFFF;
		*(uint16_t *) (cmapHeader + 0x04) = NFTR_MAP_SCAN;
		*(uint16_t *) (cmapHeader + 0x06) = 0;
		*(uint32_t *) (cmapHeader + 0x08) = 0; // next=NULL
		*(uint16_t *) (cmapHeader + 0x0C) = (uint16_t) tailListCP.length;

		NnsStreamStartBlock(&nns, "CMAP");
		NnsStreamWrite(&nns, cmapHeader, sizeof(cmapHeader));
		for (unsigned int i = 0; i < tailListCP.length; i++) {
			uint16_t map[2];
			StListGet(&tailListGlyphCP, i, &map[0]); // get the code point i
			StListGet(&tailListGlyph, i, &map[1]);   // get the glyph index i

			NnsStreamWrite(&nns, map, sizeof(map));
		}
		NnsStreamEndBlock(&nns);
	}

	StListFree(&divList);
	StListFree(&tailListCP);
	StListFree(&tailListGlyphCP);
	StListFree(&tailListGlyph);
	StListFree(&listWidBlock);
	NnsStreamFinalize(&nns);
	NnsStreamFlushOut(&nns, stream);
	NnsStreamFree(&nns);
	bstreamFree(&glyphStream);

	return OBJ_STATUS_SUCCESS;
}

static void NftrWriteBnfrBncmp1x(NFTR *nftr, int verHi, int verLo, BSTREAM *streamFnt, BSTREAM *streamCmp) {
	//write file header
	if (streamFnt != NULL) JFntWriteHeader(streamFnt, verHi, verLo, nftr->nGlyph, "JNFR");
	if (streamCmp != NULL) JFntWriteHeader(streamCmp, verHi, verLo, nftr->nGlyph, "JNCM");

	//JNFR info
	if (streamFnt != NULL) {
		uint16_t info = nftr->cellWidth | (nftr->cellHeight << 5);
		bstreamWrite(streamFnt, &info, sizeof(info));
	}

	//determine code mapping
	StList listCon, listDir;
	StListCreateInline(&listCon, uint16_t[2], NULL);
	StListCreateInline(&listDir, uint16_t, NULL);

	if (verHi == 1 && verLo == 1) {
		//version 1.1: list all code points in the direct mapping block
		for (int i = 0; i < nftr->nGlyph; i++) {
			uint16_t cp = nftr->glyphs[i].cp;
			StListAdd(&listDir, &cp);
		}
	} else if (verHi == 1 && verLo == 2) {
		//version 1.2: create continuous and direct mappings
		for (int i = 0; i < nftr->nGlyph;) {
			//check continuous run of code point assignment
			int j;
			for (j = i + 1; j < nftr->nGlyph; j++) {
				if (nftr->glyphs[j].cp != (nftr->glyphs[j - 1].cp + 1)) break;
			}
			int nRun = j - i;

			//a run of 2 glyphs or more is the threshold for size optimization. However since for
			//both continuous or direct mapping blocks this would be the same size, we will create
			//a continuous mapping block only if it exceeds this. This creates a bias towards more
			//direct mapping entries, and hopefully faster O(logn) lookup.
			if (nRun >= 3) {
				//run
				uint16_t rangeCP[2];
				rangeCP[0] = nftr->glyphs[i].cp;
				rangeCP[1] = rangeCP[0] + nRun - 1;
				StListAdd(&listCon, rangeCP);
				i += nRun;
			} else {
				//no run
				uint16_t cp = nftr->glyphs[i].cp;
				StListAdd(&listDir, &cp);
				i++;
			}
		}
	}

	BSTREAM stmWid, stmGlp;
	bstreamCreate(&stmWid, NULL, 0);
	bstreamCreate(&stmGlp, NULL, 0);

	//write mapping header
	if (streamCmp != NULL) {
		if (verHi == 1 && verLo == 1) {
			//BNCMP 1.1: no additional header info
		} else if (verHi == 1 && verLo == 2) {
			//BNCMP 1.2: write offsets to continuous and direct mapping
			unsigned char hdr[8] = { 0 };
			*(uint32_t *) (hdr + 0x0) = 0x8; // +8 bytes (continuous mapping)
			*(uint32_t *) (hdr + 0x4) = ((0x8 + listCon.length * 4 + 2) + 3) & ~3;
			bstreamWrite(streamCmp, hdr, sizeof(hdr));

			//before writing continuous mapping: write u16 number of continuous mapping entries
			uint16_t nCon = listCon.length;
			bstreamWrite(streamCmp, &nCon, sizeof(nCon));
		}
	}

	//write continuous mapping glyphs
	for (unsigned int i = 0; i < listCon.length; i++) {
		uint16_t rangeCP[2];
		StListGet(&listCon, i, rangeCP);
		for (unsigned int j = rangeCP[0]; j <= rangeCP[1]; j++) {
			NFTR_GLYPH *glyph = NftrGetGlyphByCP(nftr, j);

			//write glyph image
			NftrWriteGlyphToStream(&stmGlp, nftr, glyph, 1);

			//write glyph width
			unsigned char wid[2];
			wid[0] = -glyph->spaceLeft;
			wid[1] = glyph->width + glyph->spaceLeft;
			bstreamWrite(&stmWid, wid, sizeof(wid));
		}

		//write code map entry
		if (streamCmp != NULL) {
			bstreamWrite(streamCmp, rangeCP, sizeof(rangeCP));
		}
	}

	//direct mapping header for BNCMP 1.2
	if (streamCmp != NULL && verHi == 1 && verLo == 2) {
		bstreamAlign(streamCmp, 4);

		uint16_t dirHdr[2];
		dirHdr[0] = listDir.length;
		dirHdr[1] = ((unsigned int) nftr->nGlyph) - listDir.length;
		bstreamWrite(streamCmp, dirHdr, sizeof(dirHdr));
	}

	//write direct mapping glyphs
	for (unsigned int i = 0; i < listDir.length; i++) {
		uint16_t cp;
		StListGet(&listDir, i, &cp);
		NFTR_GLYPH *glyph = NftrGetGlyphByCP(nftr, cp);

		//write glyph image
		NftrWriteGlyphToStream(&stmGlp, nftr, glyph, 1);

		//write glyph width
		unsigned char wid[2];
		wid[0] = -glyph->spaceLeft;
		wid[1] = glyph->width + glyph->spaceLeft;
		bstreamWrite(&stmWid, wid, sizeof(wid));

		//write code map entry
		if (streamCmp != NULL) {
			bstreamWrite(streamCmp, &cp, sizeof(cp));
		}
	}

	//write font glyph data
	if (streamFnt != NULL) {
		bstreamWrite(streamFnt, stmWid.buffer, stmWid.size);
		bstreamWrite(streamFnt, stmGlp.buffer, stmGlp.size);
	}
	bstreamFree(&stmWid);
	bstreamFree(&stmGlp);

	StListFree(&listCon);
	StListFree(&listDir);
}

static int NftrWriteBnfr11(NFTR *nftr, BSTREAM *stream) {
	NftrWriteBnfrBncmp1x(nftr, 1, 1, stream, NULL);
	return OBJ_STATUS_SUCCESS;
}

static int NftrWriteBnfr12(NFTR *nftr, BSTREAM *stream) {
	NftrWriteBnfrBncmp1x(nftr, 1, 2, stream, NULL);
	return OBJ_STATUS_SUCCESS;
}

int NftrWrite(NFTR *nftr, BSTREAM *stream) {
	switch (nftr->header.format) {
		case NFTR_TYPE_BNFR_11:
			return NftrWriteBnfr11(nftr, stream);
		case NFTR_TYPE_BNFR_12:
			return NftrWriteBnfr12(nftr, stream);
		case NFTR_TYPE_BNFR_20:
			return NftrWriteBnfr20(nftr, stream);
		case NFTR_TYPE_NFTR_01:
		case NFTR_TYPE_NFTR_10:
		case NFTR_TYPE_NFTR_11:
		case NFTR_TYPE_NFTR_12:
			return NftrWriteNftrCommon(nftr, stream);
	}
	return OBJ_STATUS_UNSUPPORTED;
}

int NftrWriteFile(NFTR *nftr, LPWSTR name) {
	return ObjWriteFile(name, &nftr->header, (OBJECT_WRITER) NftrWrite);
}


// ----- BNCMP Routines -- for use with BNFR 1.x files

LPCWSTR codeMapFormatNames[] = {
	L"Invalid", L"BNCMP 1.1", L"BNCMP 1.2", NULL
};

static int BncmpIsValidBncmp11(const unsigned char *buffer, unsigned int size) {
	if (!JFntCheckHeader(buffer, size, 1, 1, "JNCM")) return 0;
	
	//check entry data size
	uint16_t nEntry = *(const uint16_t *) (buffer + 6);
	if ((size - 8) / 2 < nEntry) return 0;

	//BNCMP 1.1 does not have sorted code list
	return 1;
}

static int BncmpIsValidBncmp12(const unsigned char *buffer, unsigned int size) {
	if (!JFntCheckHeader(buffer, size, 1, 2, "JNCM")) return 0;
	
	//check entries
	unsigned int nEntry = *(const uint16_t *) (buffer + 0x06);
	unsigned int offsCon = *(const uint32_t *) (buffer + 0x08) + 0x08;
	unsigned int offsDir = *(const uint32_t *) (buffer + 0x0C) + 0x08;

	// -- check continuous entries
	if (offsCon >= size) return 0;      // must point in file
	if ((size - offsCon) < 2) return 0; // need space for u16 mapping count

	unsigned int nCon = *(const uint16_t *) (buffer + offsCon);
	if ((size - offsCon - 2) / 4 < nCon) return 0;  // whole range of mappings in file

	const uint16_t *pCon = (const uint16_t *) (buffer + offsCon + 2);
	if (!JFntValidateContinuousMapping(pCon, nCon)) return 0;
	// --

	// -- check direct entries
	if (offsDir >= size) return 0;      // must point in file
	if ((size - offsDir) < 4) return 0; // need space for u16 mapping count and u16 unmapped count

	unsigned int nDir = *(const uint16_t *) (buffer + offsDir + 0);
	unsigned int startGlyph = *(const uint16_t *) (buffer + offsDir + 2);
	if ((size - offsDir - 4) / 2 < nDir) return 0;  // ensure all direct mapping entries fit
	if (startGlyph != (nEntry - nDir)) return 0;    // ensure that the starting glyph is correct

	const uint16_t *pDir = (const uint16_t *) (buffer + offsDir + 4);
	if (!JFntValidateDirectMapping(pDir, nDir)) return 0;
	// -- 

	//count mapped code values
	unsigned int nMap = JFntCountMappings(pCon, nCon, pDir, nDir);
	if (nMap != nEntry) return 0;

	return 1;
}

int BncmpIdentify(const unsigned char *buffer, unsigned int size) {
	if (BncmpIsValidBncmp11(buffer, size)) return BNCMP_TYPE_BNCMP_11;
	if (BncmpIsValidBncmp12(buffer, size)) return BNCMP_TYPE_BNCMP_12;
	return BNCMP_TYPE_INVALID;
}

static int BncmpReadBncmp11(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	//map glyphs
	unsigned int nMapped = *(const uint16_t *) (buffer + 0x06);
	if (nMapped != nftr->nGlyph) {
		return OBJ_STATUS_MISMATCH;
	}

	const uint16_t *pDir = (const uint16_t *) (buffer + 8);

	//assign code points
	for (unsigned int i = 0; i < nMapped; i++) {
		nftr->glyphs[i].cp = pDir[i];
	}

	nftr->hasCodeMap = 1;
	NftrEnsureSorted(nftr);

	return OBJ_STATUS_SUCCESS;
}

static int BncmpReadBncmp12(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	//map glyphs
	unsigned int nMapped = *(const uint16_t *) (buffer + 0x06);
	if (nMapped != nftr->nGlyph) {
		return OBJ_STATUS_MISMATCH;
	}

	//assign code points
	unsigned int offsCon = *(const uint32_t *) (buffer + 0x08) + 0x08;
	unsigned int offsDir = *(const uint32_t *) (buffer + 0x0C) + 0x08;
	unsigned int nCon = *(const uint16_t *) (buffer + offsCon);
	unsigned int nDir = *(const uint16_t *) (buffer + offsDir);

	//continuous
	unsigned int curGlyph = 0;
	const uint16_t *con = (const uint16_t *) (buffer + offsCon + 2);
	for (unsigned int i = 0; i < nCon; i++) {
		uint16_t cpLo = con[2 * i + 0];
		uint16_t cpHi = con[2 * i + 1];
		for (uint16_t j = cpLo; j <= cpHi; j++) {
			nftr->glyphs[curGlyph++].cp = j;
		}
	}


	//direct
	const uint16_t *dir = (const uint16_t *) (buffer + offsDir + 4);
	for (unsigned int i = 0; i < nDir; i++) {
		nftr->glyphs[curGlyph++].cp = dir[i];
	}

	nftr->hasCodeMap = 1;
	NftrEnsureSorted(nftr);

	return OBJ_STATUS_SUCCESS;
}

int BncmpRead(NFTR *nftr, const unsigned char *buffer, unsigned int size) {
	int format = BncmpIdentify(buffer, size);
	switch (format) {
		case BNCMP_TYPE_BNCMP_11:
			return BncmpReadBncmp11(nftr, buffer, size);
		case BNCMP_TYPE_BNCMP_12:
			return BncmpReadBncmp12(nftr, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

static int BncmpWriteBncmp11(NFTR *nftr, BSTREAM *stream) {
	NftrWriteBnfrBncmp1x(nftr, 1, 1, NULL, stream);
	return OBJ_STATUS_SUCCESS;
}

static int BncmpWriteBncmp12(NFTR *nftr, BSTREAM *stream) {
	NftrWriteBnfrBncmp1x(nftr, 1, 2, NULL, stream);
	return OBJ_STATUS_SUCCESS;
}

int BncmpWrite(NFTR *nftr, BSTREAM *stream) {
	switch (nftr->header.format) {
		case NFTR_TYPE_BNFR_11:
			return BncmpWriteBncmp11(nftr, stream);
		case NFTR_TYPE_BNFR_12:
			return BncmpWriteBncmp12(nftr, stream);
	}
	return OBJ_STATUS_INVALID;
}
