#include "nitropaint.h"
#include "ext/isdif.h"
#include "DumpGraphics.h"
#include "ui.h"
#include "resource.h"

#include "object/NitroPalette.h"
#include "object/NitroCharacter.h"
#include "object/NitroScreen.h"
#include "object/NitroCell.h"


// ----- VRAM defs

typedef enum GxVramBank_ {
	GX_VRAM_A,
	GX_VRAM_B,
	GX_VRAM_C,
	GX_VRAM_D,
	GX_VRAM_E,
	GX_VRAM_F,
	GX_VRAM_G,
	GX_VRAM_H,
	GX_VRAM_I
} GxVramBank;

#define GX_VRAM_A_SIZE 0x20000
#define GX_VRAM_B_SIZE 0x20000
#define GX_VRAM_C_SIZE 0x20000
#define GX_VRAM_D_SIZE 0x20000
#define GX_VRAM_E_SIZE 0x10000
#define GX_VRAM_F_SIZE 0x04000
#define GX_VRAM_G_SIZE 0x04000
#define GX_VRAM_H_SIZE 0x08000
#define GX_VRAM_I_SIZE 0x04000

#define GX_VRAM_A_ADDR 0x06800000
#define GX_VRAM_B_ADDR 0x06820000
#define GX_VRAM_C_ADDR 0x06840000
#define GX_VRAM_D_ADDR 0x06860000
#define GX_VRAM_E_ADDR 0x06880000
#define GX_VRAM_F_ADDR 0x06890000
#define GX_VRAM_G_ADDR 0x06894000
#define GX_VRAM_H_ADDR 0x06898000
#define GX_VRAM_I_ADDR 0x068A0000


#define VRAM_CNT_E_MASK           0x80
#define VRAM_CNT_OFS_MASK         0x18
#define VRAM_CNT_MST_MASK         0x07
#define VRAM_CNT_OFS_SHIFT           3


#define ADDR_STANDARD_PLTT  0x05000000
#define ADDR_PLTT_MAIN_BG   0x05000000
#define ADDR_PLTT_MAIN_OBJ  0x05000200
#define ADDR_PLTT_SUB_BG    0x05000400
#define ADDR_PLTT_SUB_OBJ   0x05000600

#define EXT_PLTT_SLOT_SIZE      0x2000






typedef struct GxExtPltt_ {
	union {
		struct {
			unsigned char mainBG [4 * EXT_PLTT_SLOT_SIZE];
			unsigned char mainOBJ[4 * EXT_PLTT_SLOT_SIZE];
			unsigned char subBG  [4 * EXT_PLTT_SLOT_SIZE];
			unsigned char subOBJ [4 * EXT_PLTT_SLOT_SIZE];
		};
		unsigned char pltt[2][2][4][EXT_PLTT_SLOT_SIZE];
	};
} GxExtPltt;


// ----- Graphics control

typedef struct GxRegs_ {
	uint32_t DISPCNT;         // 04000000
	uint16_t DISPSTAT;        // 04000004 (main only)
	uint16_t VCOUNT;          // 04000006 (main only)
	union {
		struct {
			uint16_t BG0CNT;  // 04000008
			uint16_t BG1CNT;  // 0400000A
			uint16_t BG2CNT;  // 0400000C
			uint16_t BG3CNT;  // 0400000E
		};
		uint16_t BGxCNT[4];
	};
} GxRegs;


// ----- VRAM control

typedef struct GxVramCnt_ {
	uint8_t VRAM_A_CNT;
	uint8_t VRAM_B_CNT;
	uint8_t VRAM_C_CNT;
	uint8_t VRAM_D_CNT;
	uint8_t VRAM_E_CNT;
	uint8_t VRAM_F_CNT;
	uint8_t VRAM_G_CNT;
	uint8_t WRAM_CNT;
	uint8_t VRAM_H_CNT;
	uint8_t VRAM_I_CNT;
} GxVramCnt;



static int GxdReadExtPltt(GxExtPltt *pExtPltt) {
	//default state is to read zeros when no banks are mapped.
	memset(pExtPltt, 0, sizeof(GxExtPltt));
	
	GxVramCnt vramCnt = { 0 }, newCnt = { 0 };
	if (!IsdifReadMem(&vramCnt, 0x04000240, sizeof(GxVramCnt))) return 0;

	//TODO: check VRAM before pulling them all down?

	//pull each bank E-I down to LCDC.
	memcpy(&newCnt, &vramCnt, sizeof(GxVramCnt));
	newCnt.VRAM_E_CNT = 0x80;
	newCnt.VRAM_F_CNT = 0x80;
	newCnt.VRAM_G_CNT = 0x80;
	newCnt.VRAM_H_CNT = 0x80;
	newCnt.VRAM_I_CNT = 0x80;
	if (!IsdifWriteMem(0x04000240, &newCnt, sizeof(GxVramCnt))) return 0;

	//check each bank for its extended palette configuration. If it's mapped to ext pltt,
	//copy it out from LCDC.
	unsigned int fSlot = (vramCnt.VRAM_F_CNT & VRAM_CNT_OFS_MASK) >> VRAM_CNT_OFS_SHIFT;
	unsigned int gSlot = (vramCnt.VRAM_G_CNT & VRAM_CNT_OFS_MASK) >> VRAM_CNT_OFS_SHIFT;

	//bank E
	switch (vramCnt.VRAM_E_CNT & 0x87) {
		case 0x84:  // E -> main BG ext pltt (low 32KB to all 4 slots)
			if (!IsdifReadMem(pExtPltt->mainBG, GX_VRAM_E_ADDR, GX_VRAM_E_SIZE / 2)) return 0;
			break;
	}

	//bank F
	switch (vramCnt.VRAM_F_CNT & 0x87) {
		case 0x84:  // F -> main BG ext pltt
			if (!IsdifReadMem(&pExtPltt->mainBG[(fSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_F_ADDR, GX_VRAM_F_SIZE)) return 0;
			break;
		case 0x85:  // F -> main OBJ ext pltt
			if (!IsdifReadMem(&pExtPltt->mainOBJ[(fSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_F_ADDR, GX_VRAM_F_SIZE)) return 0;
			break;
	}

	//bank G
	switch (vramCnt.VRAM_G_CNT & 0x87) {
		case 0x84:  // G -> main BG ext pltt
			if (!IsdifReadMem(&pExtPltt->mainBG[(gSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_G_ADDR, GX_VRAM_G_SIZE)) return 0;
			break;
		case 0x85:  // G -> main OBJ ext pltt
			if (!IsdifReadMem(&pExtPltt->mainOBJ[(gSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_G_ADDR, GX_VRAM_G_SIZE)) return 0;
			break;
	}

	//bank H
	switch (vramCnt.VRAM_H_CNT & 0x87) {
		case 0x82:  // H -> sub BG ext pltt
			if (!IsdifReadMem(pExtPltt->subBG, GX_VRAM_H_ADDR, GX_VRAM_H_SIZE)) return 0;
			break;
	}

	//bank I
	switch (vramCnt.VRAM_I_CNT & 0x87) {
		case 0x83:  // I -> sub OBJ ext pltt
			if (!IsdifReadMem(pExtPltt->subOBJ, GX_VRAM_I_ADDR, GX_VRAM_I_SIZE)) return 0;
			break;
	}

	//restore
	if (!IsdifWriteMem(0x04000240, &vramCnt, sizeof(GxVramCnt))) return 0;

	return 1;
}

static IsdStatus GxdDumpPalette(NCLR **pNclr, int sub, int obj, int extPltt, int slot) {
	NCLR *nclr = NULL;

	unsigned int plttSize = extPltt ? EXT_PLTT_SLOT_SIZE : (256 * sizeof(COLOR));
	COLOR *pltt = (COLOR *) calloc(plttSize, 1);

	if (!extPltt) {
		//standard palette use
		uint32_t srcAddr = ADDR_STANDARD_PLTT + ((sub << 1) | obj) * 0x200;
		(void) IsdifReadMem(pltt, srcAddr, plttSize);
	} else {
		//get ext palette slot
		GxExtPltt *extPltt = (GxExtPltt *) calloc(1, sizeof(GxExtPltt));
		GxdReadExtPltt(extPltt);

		memcpy(pltt, extPltt->pltt[sub][obj][slot], EXT_PLTT_SLOT_SIZE);

		free(extPltt);
	}

	ObjReadBuffer((ObjHeader **) &nclr, (unsigned char *) pltt, plttSize, FILE_TYPE_PALETTE, NCLR_TYPE_BIN, COMPRESSION_NONE);
	free(pltt);

	nclr->extPalette = extPltt;

	*pNclr = nclr;
	return ISD_STATUS_OK;
}

static IsdStatus GxdDumpCharacterOBJ(NCGR **pNcgr, int sub, uint32_t DISPCNT) {
	uint32_t mappingMode = DISPCNT & 0x300010;

	//based on the mapping mode, determine the size of graphics
	unsigned int sizeChars = 0x8000;
	switch (mappingMode) {
		case GX_OBJVRAMMODE_CHAR_2D      : sizeChars =  0x8000; break;
		case GX_OBJVRAMMODE_CHAR_1D_32K  : sizeChars =  0x8000; break;
		case GX_OBJVRAMMODE_CHAR_1D_64K  : sizeChars = 0x10000; break;
		case GX_OBJVRAMMODE_CHAR_1D_128K : sizeChars = 0x20000; break;
		case GX_OBJVRAMMODE_CHAR_1D_256K : sizeChars = 0x40000; break;
	}

	unsigned char *objChars = (unsigned char *) calloc(sizeChars, 1);
	IsdifReadMem(objChars, sub ? 0x06600000 : 0x06400000, sizeChars);

	NCGR *ncgr = NULL;
	ObjReadBuffer((ObjHeader **) &ncgr, objChars, sizeChars, FILE_TYPE_CHARACTER, NCGR_TYPE_BIN, COMPRESSION_NONE);
	free(objChars);

	*pNcgr = ncgr;
	return ISD_STATUS_OK;
}

static IsdStatus GxdDumpOAM(NCER **pNcer, int sub, uint32_t DISPCNT) {
	uint32_t mappingMode = DISPCNT & 0x300010;

	unsigned char *oam = (unsigned char *) calloc(0x400, 1);
	IsdifReadMem(oam, sub ? 0x07000400 : 0x07000000, 0x400);

	NCER *ncer = (NCER *) ObjAlloc(FILE_TYPE_CELL, NCER_TYPE_NCER);
	ncer->nCells = 1;
	ncer->cells = (NCER_CELL *) calloc(1, sizeof(NCER_CELL));
	ncer->mappingMode = mappingMode;

	NCER_CELL *pCell = &ncer->cells[0];
	pCell->nAttribs = 128;
	pCell->attr = (uint16_t *) calloc(128, 3 * sizeof(uint16_t));
	for (int i = 0; i < 128; i++) {
		uint16_t *pObj = &pCell->attr[i * 3];
		memcpy(pObj, oam + i * 8, 3 * sizeof(uint16_t));

		//adjust the OAM position (X minus 128, Y minus 96) to center the screen
		pObj[0] = (pObj[0] & 0xFF00) | (((pObj[0] & 0x00FF) - 0x60) & 0x00FF);
		pObj[1] = (pObj[1] & 0xFE00) | (((pObj[1] & 0x01FF) - 0x80) & 0x01FF);
	}
	free(oam);

	*pNcer = ncer;
	return ISD_STATUS_OK;
}

static unsigned int GxdGetBitDepthForOAM(NCER *ncer) {
	if (ncer == NULL) return 4;

	for (int i = 0; i < ncer->nCells; i++) {
		GxOamAttrInfo info;
		CellDecodeOamAttributes(&info, &ncer->cells[0], i);

		if (info.characterBits == 8) return 8;
	}
	return 4;
}

static void GxdSetAttributesForOAM(NCER *ncer, NCGR *ncgr, NCLR *nclr) {
	unsigned int depth = GxdGetBitDepthForOAM(ncer);

	if (ncgr != NULL) ChrSetDepth(ncgr, depth);
	if (nclr != NULL) nclr->nBits = depth;
}





typedef enum GxBgType_ {
	BGTYPE_TEXT,
	BGTYPE_AFFINE,
	BGTYPE_AFFINEEXT,
	BGTYPE_LARGEBITMAP,
	BGTYPE_INVALID
} GxBgType;

static GxBgType GxdGetBgType(int iLayer, int bgMode, int bg03d) {
	if (bgMode > 6) return BGTYPE_INVALID;

	static const unsigned char modes[7][4] = {
		{ BGTYPE_TEXT,    BGTYPE_TEXT,    BGTYPE_TEXT,        BGTYPE_TEXT      },
		{ BGTYPE_TEXT,    BGTYPE_TEXT,    BGTYPE_TEXT,        BGTYPE_AFFINE    },
		{ BGTYPE_TEXT,    BGTYPE_TEXT,    BGTYPE_AFFINE,      BGTYPE_AFFINE    },
		{ BGTYPE_TEXT,    BGTYPE_TEXT,    BGTYPE_TEXT,        BGTYPE_AFFINEEXT },
		{ BGTYPE_TEXT,    BGTYPE_TEXT,    BGTYPE_AFFINE,      BGTYPE_AFFINEEXT },
		{ BGTYPE_TEXT,    BGTYPE_TEXT,    BGTYPE_AFFINEEXT,   BGTYPE_AFFINEEXT },
		{ BGTYPE_INVALID, BGTYPE_INVALID, BGTYPE_LARGEBITMAP, BGTYPE_INVALID   }
	};
	return modes[bgMode][iLayer];
}

static void GxdGetBgSize(GxBgType bgType, int size, int colorMode, unsigned int *pWidth, unsigned int *pHeight) {
	*pWidth = *pHeight = 0;

	switch (bgType) {
		case BGTYPE_TEXT:
			switch (size) {
				case 0: *pWidth = 256; *pHeight = 256; break;
				case 1: *pWidth = 512; *pHeight = 256; break;
				case 2: *pWidth = 256; *pHeight = 512; break;
				case 3: *pWidth = 512; *pHeight = 512; break;
			}
			break;
		case BGTYPE_AFFINE:
			switch (size) {
				case 0: *pWidth = 128; *pHeight = 128; break;
				case 1: *pWidth = 256; *pHeight = 256; break;
				case 2: *pWidth = 512; *pHeight = 512; break;
				case 3: *pWidth = 1024; *pHeight = 1024; break;
			}
			break;
		case BGTYPE_AFFINEEXT:
			if (colorMode == 0) {
				//character BG
				switch (size) {
					case 0: *pWidth = 128; *pHeight = 128; break;
					case 1: *pWidth = 256; *pHeight = 256; break;
					case 2: *pWidth = 512; *pHeight = 512; break;
					case 3: *pWidth = 1024; *pHeight = 1024; break;
				}
			} else {
				//bitmap (256-color or direct color) BG
				switch (size) {
					case 0: *pWidth = 128; *pHeight = 128; break;
					case 1: *pWidth = 256; *pHeight = 256; break;
					case 2: *pWidth = 512; *pHeight = 256; break;
					case 3: *pWidth = 256; *pHeight = 512; break;
				}
			}
			break;
		case BGTYPE_LARGEBITMAP:
			switch (size) {
				case 0: *pWidth = 512; *pHeight = 1024; break;
				case 1: *pWidth = 1024; *pHeight = 512; break;
				case 2: *pWidth = 0; *pHeight = 0; break;  // invalid size
				case 3: *pWidth = 0; *pHeight = 0; break;  // invalid size
			}
			break;
	}
}

static int GxdGetBgScreenFormatAndColorMode(GxBgType bgType, uint16_t cnt, int *pFmt, int *pColorMode) {
	switch (bgType) {
		case BGTYPE_TEXT:
			*pFmt = SCREENFORMAT_TEXT;
			if (cnt & 0x80) {
				//color-mode: 8bpp
				*pColorMode = SCREENCOLORMODE_256x1;
			} else {
				//color-mode: 4bpp
				*pColorMode = SCREENCOLORMODE_16x16;
			}
			return 1;
		case BGTYPE_AFFINE:
			*pFmt = SCREENFORMAT_AFFINE;
			*pColorMode = SCREENCOLORMODE_256x1;
			return 1;
		case BGTYPE_AFFINEEXT:
			if (cnt & 0x80) {
				//bitmap, no BG screen data
				return 0;
			} else {
				//8bpp affine extended
				*pFmt = SCREENFORMAT_AFFINEEXT;
				*pColorMode = SCREENCOLORMODE_256x16;
			}
			return 1;
		case BGTYPE_LARGEBITMAP:
			return 0; // no BG screen data
	}

	// unsupported format
	return 0;
}

static unsigned int GxdGetBgGraphicsDepth(GxBgType bgType, uint16_t cnt) {
	switch (bgType) {
		case BGTYPE_TEXT:
			if (cnt & 0x80) return 8;
			else            return 4;
		case BGTYPE_AFFINE:
			return 8;
		case BGTYPE_AFFINEEXT:
			if (cnt & 0x80) {
				if (cnt & 0x04) return 16;
				else            return  8;
			} else {
				return 8;
			}
		case BGTYPE_LARGEBITMAP:
			return 8;
	}
	return 0; // not support
}

static int GxdGetBgExtPlttSlot(const GxRegs *regs, int bgLayer) {
	uint32_t bgMode     = (regs->DISPCNT >>  0) & 0x07;
	uint32_t useExtPltt = (regs->DISPCNT >> 30) & 0x01;

	//if ext pltt is enabled in DISPCNT
	if (!useExtPltt) return -1;

	uint16_t bgcnt = regs->BGxCNT[bgLayer];
	GxBgType bgType = GxdGetBgType(bgLayer, bgMode, 0);
	unsigned int bpp = GxdGetBgGraphicsDepth(bgType, bgcnt);

	//not ext pltt if the bit depth is not 8
	if (bpp != 8) return -1;

	int extPlttSlot = -1;
	switch (bgLayer) {
		case 0:
			if (bgcnt & 0x2000) extPlttSlot = 2;
			else                extPlttSlot = 0;
			break;
		case 1:
			if (bgcnt & 0x2000) extPlttSlot = 3;
			else                extPlttSlot = 1;
			break;
		case 2:
			extPlttSlot = 2;
			break;
		case 3:
			extPlttSlot = 3;
			break;
	}
	return extPlttSlot;
}

static IsdStatus GxdDumpCharacterBG(NCGR **pNcgr, const GxRegs *regs, int sub, int iLayer) {
	uint32_t bgMode         = (regs->DISPCNT >>  0) & 0x07;
	uint32_t charBaseOffset = (regs->DISPCNT >> 24) & 0x07;
	uint32_t scrBaseOffset  = (regs->DISPCNT >> 27) & 0x07;
	uint32_t charBaseBlock  = (regs->BGxCNT[iLayer] >> 2) & 0x0F;
	uint32_t scrBaseBlock   = (regs->BGxCNT[iLayer] >> 8) & 0x1F;

	uint32_t vramAddr = sub ? 0x06200000 : 0x06000000;
	uint32_t chrAddr = vramAddr + (charBaseOffset * 0x10000) + (charBaseBlock * 0x4000);
	uint32_t scrAddr = vramAddr + (scrBaseOffset  * 0x10000) + (scrBaseBlock  *  0x800);

	//get BG screen type
	int hasBgScreen, screenFormat, colorMode;
	GxBgType bgType = GxdGetBgType(iLayer, bgMode, 0);
	hasBgScreen = GxdGetBgScreenFormatAndColorMode(bgType, regs->BGxCNT[iLayer], &screenFormat, &colorMode);

	//get the bit depth and graphics size
	unsigned int bpp = 4, charX = 32, charY = 32;
	if (hasBgScreen) {
		//bit depth with BG screen
		switch (colorMode) {
			case SCREENCOLORMODE_16x16:
				bpp = 4;
				break;
			case SCREENCOLORMODE_256x1:
			case SCREENCOLORMODE_256x16:
				bpp = 8;
				break;
		}

		//graphics size
		switch (screenFormat) {
			case SCREENFORMAT_TEXT:
			case SCREENFORMAT_AFFINEEXT:
				charX = 32;
				charY = 32;
				break;
			case SCREENFORMAT_AFFINE:
				charX = 16;
				charY = 16;
				break;
		}
	} else {
		//bit depth with no BG screen
		switch (bgType) {
			case BGTYPE_LARGEBITMAP:
				bpp = 8;
				break;
			case BGTYPE_AFFINEEXT:
			{
				if (regs->BGxCNT[iLayer] & 0x80) {
					if (regs->BGxCNT[iLayer] & 0x04) bpp = 16;
					else                             bpp =  8;
				} else {
					bpp = 8;
				}
				break;
			}
		}

		int width, height;
		GxdGetBgSize(bgType, (regs->BGxCNT[iLayer] >> 14) & 0x03, (regs->BGxCNT[iLayer] >> 7) & 0x01, &width, &height);

		charX = width / 8;
		charY = height / 8;
	}

	//no support for 16bpp direct
	if (bpp == 16) {
		*pNcgr = NULL;
		return ISD_STATUS_OK;
	}

	unsigned int charSize = (charX * charY) * (8 * bpp);
	unsigned char *buf = (unsigned char *) calloc(charSize, 1);
	IsdifReadMem(buf, hasBgScreen ? chrAddr : scrAddr, charSize);

	NCGR *ncgr = (NCGR *) ObjAlloc(FILE_TYPE_CHARACTER, NCGR_TYPE_NCGR);
	ncgr->nBits = bpp;
	ncgr->tilesX = charX;
	ncgr->tilesY = charY;
	ncgr->nTiles = charX * charY;
	ncgr->bitmap = !hasBgScreen;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ChrAllocGraphics(ncgr);
	ChrReadGraphics(ncgr, buf);
	free(buf);

	*pNcgr = ncgr;
	return ISD_STATUS_OK;
}

static IsdStatus GxdDumpBgScreen(NSCR **pNscr, const GxRegs *regs, int sub, int iLayer) {
	uint32_t bgMode         = (regs->DISPCNT >>  0) & 0x07;
	uint32_t scrBaseOffset  = (regs->DISPCNT >> 27) & 0x07;
	uint32_t scrBaseBlock   = (regs->BGxCNT[iLayer] >> 8) & 0x1F;

	uint32_t vramAddr = sub ? 0x06200000 : 0x06000000;
	uint32_t scrAddr = vramAddr + (scrBaseOffset  * 0x10000) + (scrBaseBlock  *  0x800);

	//get BG type
	int hasBgScreen, screenFormat, colorMode;
	GxBgType bgType = GxdGetBgType(iLayer, bgMode, 0);
	hasBgScreen = GxdGetBgScreenFormatAndColorMode(bgType, regs->BGxCNT[iLayer], &screenFormat, &colorMode);
	if (!hasBgScreen) {
		//no BG screen
		*pNscr = NULL;
		return ISD_STATUS_OK;
	}

	//get size
	unsigned int width, height;
	GxdGetBgSize(bgType, (regs->BGxCNT[iLayer] >> 14) & 0x03, (regs->BGxCNT[iLayer] >> 7) & 0x01, &width, &height);

	unsigned int unitSize = (screenFormat == SCREENFORMAT_AFFINE) ? 1 : 2;
	unsigned int bufSize = (width / 8) * (height / 8) * (unitSize);
	unsigned char *buf = (unsigned char *) calloc(bufSize, 1);
	IsdifReadMem(buf, scrAddr, bufSize);

	NSCR *nscr = (NSCR *) ObjAlloc(FILE_TYPE_SCREEN, NSCR_TYPE_NSCR);
	nscr->fmt = screenFormat;
	nscr->colorMode = colorMode;
	nscr->tilesX = width / 8;
	nscr->tilesY = height / 8;
	nscr->dataSize = nscr->tilesX * nscr->tilesY * sizeof(uint16_t);
	nscr->data = (uint16_t *) calloc(nscr->dataSize, 1);
	ScrReadScreenData(nscr, buf, bufSize);
	free(buf);

	*pNscr = nscr;
	return ISD_STATUS_OK;
}

static void SetAttributesForBgScreen(NSCR *nscr, NCGR *ncgr, NCLR *nclr) {
	if (ncgr == NULL) return;

	unsigned int depth = ncgr->nBits;

	if (nclr != NULL) nclr->nBits = depth;
}



typedef struct DumpGraphicsDialogData_ {
	HWND hWndMain;
	HWND hWnd;

	HWND hWndStop;
	HWND hWndResume;
	HWND hWndCloseAll;
	HWND hWndSources[10];
	HWND hWndSourceTexts[10];

	HWND hWndDump;

	GxRegs regsMain;
	GxRegs regsSub;
	GxVramCnt vramCnt;
	IsdMonStatus initMonStatus;
} DumpGraphicsDialogData;

static void DumpGraphics(DumpGraphicsDialogData *data, int sub, int obj, int bgLayer) {
	GxRegs regs = { 0 };
	if (sub) {
		memcpy(&regs, &data->regsSub, sizeof(GxRegs));
	} else {
		memcpy(&regs, &data->regsMain, sizeof(GxRegs));
	}

	unsigned char enableObjExtPltt = (regs.DISPCNT >> 31) & 0x01;

	//construct the object
	NCLR *nclr = NULL;
	NCGR *ncgr = NULL;
	NCER *ncer = NULL;
	NSCR *nscr = NULL;

	if (obj) {
		//get the OAM
		GxdDumpOAM(&ncer, sub, regs.DISPCNT);

		//get the character
		GxdDumpCharacterOBJ(&ncgr, sub, regs.DISPCNT);

		//get the palette
		int useExtPltt = 0;
		if (GxdGetBitDepthForOAM(ncer) == 8 && enableObjExtPltt) {
			useExtPltt = 1;
		}
		GxdDumpPalette(&nclr, sub, 1, useExtPltt, 0);

		//setting the graphics bit depth based on OAM data
		GxdSetAttributesForOAM(ncer, ncgr, nclr);
	} else {
		int extPlttSlot = GxdGetBgExtPlttSlot(&regs, bgLayer);

		//get the palette
		GxdDumpPalette(&nclr, sub, 0, extPlttSlot != -1, extPlttSlot);

		//get the character
		GxdDumpCharacterBG(&ncgr, &regs, sub, bgLayer);

		//get the BG screen
		GxdDumpBgScreen(&nscr, &regs, sub, bgLayer);

		//setting the graphics bit depth based on screen data
		SetAttributesForBgScreen(nscr, ncgr, nclr);
	}

	//open
	if (nclr != NULL) NpOpenObject(data->hWndMain, &nclr->header);
	if (ncgr != NULL) NpOpenObject(data->hWndMain, &ncgr->header);
	if (ncer != NULL) NpOpenObject(data->hWndMain, &ncer->header);
	if (nscr != NULL) NpOpenObject(data->hWndMain, &nscr->header);
}




static void DumpGraphicsDialogMakeAllInvalid(DumpGraphicsDialogData *data) {
	EnableWindow(data->hWndStop, FALSE);
	EnableWindow(data->hWndResume, FALSE);
	EnableWindow(data->hWndDump, FALSE);

	for (int i = 0; i < 10; i++) {
		SendMessage(data->hWndSourceTexts[i], WM_SETTEXT, 0, (LPARAM) L"Target Disconnected.");
		EnableWindow(data->hWndSources[i], FALSE);
	}
}

static void DumpGraphicsDialogHandleError(DumpGraphicsDialogData *data, const wchar_t *genericMsg) {
	switch (IsdGetLastError()) {
		case ISD_STATUS_OK:
			break;
		case ISD_STATUS_COMM_CLOSE:
		case ISD_STATUS_IRREGULAR:
			MessageBox(data->hWnd, L"The target connection is interrupted.", L"NitroPaint", MB_ICONERROR);
			DumpGraphicsDialogMakeAllInvalid(data);
			break;
		default:
			MessageBox(data->hWnd, genericMsg, L"NitroPaint", MB_ICONERROR);
			break;
	}
}

static void DumpGraphicsGetLayerStatusText(wchar_t *buf, const GxRegs *regs, int bgLayer) {
	uint32_t DISPCNT = regs->DISPCNT;
	uint32_t bgMode    = (DISPCNT >> 0) & 0x07;
	uint32_t bg03d     = (DISPCNT >> 3) & 0x01;
	uint32_t layerMask = (DISPCNT >> 8) & 0x1F;

	if (bgLayer == -1) {
		//OBJ
		if (!(layerMask & 0x10)) wsprintfW(buf, L"Disabled");
		else wsprintfW(buf, L"Enabled");
	} else {
		//BGx
		uint16_t bgcnt = regs->BGxCNT[bgLayer];

		if (!(layerMask & (1 << bgLayer))) {
			//layer disable
			wsprintfW(buf, L"Disabled");
		} else if (bgLayer == 0 && bg03d) {
			//BG0 3D
			wsprintfW(buf, L"3D");
		} else {
			//normal BG
			unsigned int width, height;
			GxBgType bgType = GxdGetBgType(bgLayer, bgMode, bg03d);
			GxdGetBgSize(bgType, (bgcnt >> 14) & 0x03, (bgcnt >> 7) & 0x01, &width, &height);

			const char *name = "";
			switch (bgType) {
				case BGTYPE_TEXT:
					name = "Text";
					break;
				case BGTYPE_AFFINE:
					name = "Affine";
					break;
				case BGTYPE_AFFINEEXT:
					name = "Affine Ex";
					break;
				case BGTYPE_LARGEBITMAP:
					name = "Large Bitmap";
					break;
			}

			wsprintfW(buf, L"%S (%d x %d)", name, width, height);
		}
	}

}

static void DumpGraphicsDialogUpdateUiForMonStatus(DumpGraphicsDialogData *data, int stop) {
	if (stop) {
		//get layer texts
		for (int i = 0; i < 10; i++) {
			wchar_t buf[64];
			DumpGraphicsGetLayerStatusText(buf, (i >= 5) ? &data->regsSub : &data->regsMain, (i % 5) - 1);

			SendMessage(data->hWndSourceTexts[i], WM_SETTEXT, 0, (LPARAM) buf);
		}

		//the dump button is now enabled
		EnableWindow(data->hWndDump, TRUE);

		//the stop button is now disabled
		EnableWindow(data->hWndStop, FALSE);
		EnableWindow(data->hWndResume, TRUE);
	} else {
		//the dump button is disabled
		EnableWindow(data->hWndDump, FALSE);

		//disable the resume button
		EnableWindow(data->hWndResume, FALSE);
		EnableWindow(data->hWndStop, TRUE);

		//restore the program running text
		for (int i = 0; i < 10; i++) {
			SendMessage(data->hWndSourceTexts[i], WM_SETTEXT, 0, (LPARAM) L"Target Running...");
		}
	}
}

static void DumpGraphicsDialogOnStop(HWND hWnd, HWND hWndCtl, int cmd, void *param) {
	DumpGraphicsDialogData *data = (DumpGraphicsDialogData *) param;

	if (!IsdifStop()) goto Error;

	//get the state of graphics
	if (!IsdifReadMem(&data->regsMain, 0x04000000, sizeof(GxRegs   ))) goto Error;
	if (!IsdifReadMem(&data->regsSub,  0x04001000, sizeof(GxRegs   ))) goto Error;
	if (!IsdifReadMem(&data->vramCnt,  0x04000240, sizeof(GxVramCnt))) goto Error;

	DumpGraphicsDialogUpdateUiForMonStatus(data, 1);
	return;

Error:
	DumpGraphicsDialogHandleError(data, L"The program could not be halted.");
}

static void DumpGraphicsDialogOnResume(HWND hWnd, HWND hWndCtl, int cmd, void *param) {
	DumpGraphicsDialogData *data = (DumpGraphicsDialogData *) param;
	IsdMonStatus monStatus;
	
	if (!IsdifResume() || !IsdifGetMonStatus(&monStatus)) {
		//command send failure
		DumpGraphicsDialogHandleError(data, L"The program could not be resumed.");
		return;
	}

	DumpGraphicsDialogUpdateUiForMonStatus(data, 0);
}

static void DumpGraphicsDialogOnDump(HWND hWnd, HWND hWndCtl, int cmd, void *param) {
	DumpGraphicsDialogData *data = (DumpGraphicsDialogData *) param;

	//get checked item
	int iSource = 0;
	for (int i = 0; i < 10; i++) {
		if (GetCheckboxChecked(data->hWndSources[i])) {
			iSource = i;
			break;
		}
	}

	//first 5 items main, latter 5 sub
	int sub = (iSource >= 5);

	//OBJ, BG0, BG1, BG2, BG3
	int dumpOBJ = (iSource % 5) == 0;
	int bgLayer = dumpOBJ ? -1 : ((iSource % 5) - 1);

	//do dump
	DumpGraphics(data, sub, dumpOBJ, bgLayer);
}

static void DumpGraphicsDialogOnCloseAll(HWND hWnd, HWND hWndCtl, int cmd, void *param) {
	DumpGraphicsDialogData *data = (DumpGraphicsDialogData *) param;

	SendMessage(data->hWndMain, WM_COMMAND, ID_FILE_CLOSEALL, 0);
}

static LRESULT CALLBACK DumpGraphicsDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	DumpGraphicsDialogData *data = UiDlgGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
		{
			data->hWnd = hWnd;
			data->hWndStop = CreateButton(hWnd, L"Stop", 10, 10, 100, 22, FALSE);
			data->hWndResume = CreateButton(hWnd, L"Resume", 115, 10, 100, 22, FALSE);
			data->hWndCloseAll = CreateButton(hWnd, L"Close All", 220, 10, 100, 22, FALSE);

			int groupX = 10, groupY = 37;
			int listX = groupX + 11, listY = groupY + 17;
			CreateGroupbox(hWnd, L"Transfer Source", groupX, groupY, 310, 294);

			static const wchar_t *const names[] = {
				L"Main OBJ", L"Main BG 0", L"Main BG 1", L"Main BG 2", L"Main BG 3",
				L"Sub OBJ",  L"Sub BG 0",  L"Sub BG 1",  L"Sub BG 2",  L"Sub BG 3"
			};

			for (int i = 0; i < 10; i++) {
				data->hWndSources[i] = CreateWindow(L"BUTTON", names[i], WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
					listX, listY + i * 27, 100, 22, hWnd, NULL, NULL, NULL);
				data->hWndSourceTexts[i] = CreateStatic(hWnd, L"Target Running...", listX + 100, listY + i * 27, 150, 22);
			}
			SendMessage(data->hWndSources[0], BM_SETCHECK, BST_CHECKED, 0);

			data->hWndDump = CreateButton(hWnd, L"Transfer", 10, 336, 310, 22, TRUE);
			EnableWindow(data->hWndDump, FALSE); // disable until stop

			//register control callback
			UiDlgRegisterCtlCommand(hWnd, data->hWndStop,     BN_CLICKED, DumpGraphicsDialogOnStop);
			UiDlgRegisterCtlCommand(hWnd, data->hWndResume,   BN_CLICKED, DumpGraphicsDialogOnResume);
			UiDlgRegisterCtlCommand(hWnd, data->hWndDump,     BN_CLICKED, DumpGraphicsDialogOnDump);
			UiDlgRegisterCtlCommand(hWnd, data->hWndCloseAll, BN_CLICKED, DumpGraphicsDialogOnCloseAll);

			//check the initial monitor state.
			if (data->initMonStatus.fiqEnter) {			
				//get the state of graphics
				int status = 1;
				if (status) status = IsdifReadMem(&data->regsMain, 0x04000000, sizeof(GxRegs   ));
				if (status) status = IsdifReadMem(&data->regsSub,  0x04001000, sizeof(GxRegs   ));
				if (status) status = IsdifReadMem(&data->vramCnt,  0x04000240, sizeof(GxVramCnt));
				if (status) DumpGraphicsDialogUpdateUiForMonStatus(data, 1);

				//fail
				if (!status) {
					DumpGraphicsDialogHandleError(data, L"The program could not be halted.");
				}
			} else {
				DumpGraphicsDialogUpdateUiForMonStatus(data, 0);
			}

			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

void DumpGraphicsDialog(HWND hWnd) {
	DumpGraphicsDialogData data = { 0 };
	data.hWndMain = hWnd;

	//test
	if (!IsdifInit()) {
		MessageBox(hWnd, L"The library is unavailable.", L"NitroPaint", MB_ICONERROR);
		return;
	}

	if (!IsdifOpen()) {
		MessageBox(hWnd, L"The device could not be accessed.", L"NitroPaint", MB_ICONERROR);
		return;
	}

	if (!IsdifGetMonStatus(&data.initMonStatus)) {
		MessageBox(hWnd, L"The device could not be accessed.", L"NitroPaint", MB_ICONERROR);
		return;
	}

	UiDlgCreateModal(hWnd, DumpGraphicsDialogProc, L"Dump Graphics", 330, 368, &data);

	IsdifClose();
}
