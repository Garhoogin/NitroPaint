#include "nitropaint.h"
#include "ext/isdif.h"
#include "DumpGraphics.h"
#include "ui.h"
#include "resource.h"

#include "object/NitroPalette.h"
#include "object/NitroCharacter.h"
#include "object/NitroScreen.h"
#include "object/NitroCell.h"


#define ISD_POLLING_INTERVAL   200  // polling timer interval
#define ISD_POLLING_TIMER        1  // polling timer ID


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


#define VRAM_CNT_ENABLE_MASK      0x80
#define VRAM_CNT_OFS_MASK         0x18
#define VRAM_CNT_MST_MASK         0x07
#define VRAM_CNT_ENABLE_SHIFT        7
#define VRAM_CNT_OFS_SHIFT           3
#define VRAM_CNT_MST_SHIFT           0

#define VRAM_CNT_ENABLE            0x80
#define VRAM_CNT_DISABLE           0x00
#define VRAM_CNT_MST_LCDC          0x00


#define ADDR_GXREGS         0x04000000
#define ADDR_GXREGS_SUB     0x04001000
#define ADDR_VRAMCNT        0x04000240
#define ADDR_STANDARD_PLTT  0x05000000
#define ADDR_PLTT_MAIN_BG   0x05000000
#define ADDR_PLTT_MAIN_OBJ  0x05000200
#define ADDR_PLTT_SUB_BG    0x05000400
#define ADDR_PLTT_SUB_OBJ   0x05000600
#define ADDR_OAM            0x07000000
#define ADDR_OAM_SUB        0x07000400

#define EXT_PLTT_SLOT_SIZE      0x2000
#define OAM_SIZE                 0x400






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
	if (!IsdifReadMem(&vramCnt, ADDR_VRAMCNT, sizeof(GxVramCnt))) return 0;

	//TODO: check VRAM before pulling them all down?

	//pull each bank E-I down to LCDC.
	memcpy(&newCnt, &vramCnt, sizeof(GxVramCnt));
	newCnt.VRAM_E_CNT = VRAM_CNT_ENABLE | VRAM_CNT_MST_LCDC;
	newCnt.VRAM_F_CNT = VRAM_CNT_ENABLE | VRAM_CNT_MST_LCDC;
	newCnt.VRAM_G_CNT = VRAM_CNT_ENABLE | VRAM_CNT_MST_LCDC;
	newCnt.VRAM_H_CNT = VRAM_CNT_ENABLE | VRAM_CNT_MST_LCDC;
	newCnt.VRAM_I_CNT = VRAM_CNT_ENABLE | VRAM_CNT_MST_LCDC;
	if (!IsdifWriteMem(ADDR_VRAMCNT, &newCnt, sizeof(GxVramCnt))) return 0;

	//check each bank for its extended palette configuration. If it's mapped to ext pltt,
	//copy it out from LCDC.
	unsigned int fSlot = (vramCnt.VRAM_F_CNT & VRAM_CNT_OFS_MASK) >> VRAM_CNT_OFS_SHIFT;
	unsigned int gSlot = (vramCnt.VRAM_G_CNT & VRAM_CNT_OFS_MASK) >> VRAM_CNT_OFS_SHIFT;

	//bank E
	switch (vramCnt.VRAM_E_CNT & (VRAM_CNT_ENABLE_MASK | VRAM_CNT_MST_MASK)) {
		case 0x84:  // E -> main BG ext pltt (low 32KB to all 4 slots)
			if (!IsdifReadMem(pExtPltt->mainBG, GX_VRAM_E_ADDR, GX_VRAM_E_SIZE / 2)) return 0;
			break;
	}

	//bank F
	switch (vramCnt.VRAM_F_CNT & (VRAM_CNT_ENABLE_MASK | VRAM_CNT_MST_MASK)) {
		case 0x84:  // F -> main BG ext pltt
			if (!IsdifReadMem(&pExtPltt->mainBG[(fSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_F_ADDR, GX_VRAM_F_SIZE)) return 0;
			break;
		case 0x85:  // F -> main OBJ ext pltt
			if (!IsdifReadMem(&pExtPltt->mainOBJ[(fSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_F_ADDR, GX_VRAM_F_SIZE)) return 0;
			break;
	}

	//bank G
	switch (vramCnt.VRAM_G_CNT & (VRAM_CNT_ENABLE_MASK | VRAM_CNT_MST_MASK)) {
		case 0x84:  // G -> main BG ext pltt
			if (!IsdifReadMem(&pExtPltt->mainBG[(gSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_G_ADDR, GX_VRAM_G_SIZE)) return 0;
			break;
		case 0x85:  // G -> main OBJ ext pltt
			if (!IsdifReadMem(&pExtPltt->mainOBJ[(gSlot & 1) * EXT_PLTT_SLOT_SIZE], GX_VRAM_G_ADDR, GX_VRAM_G_SIZE)) return 0;
			break;
	}

	//bank H
	switch (vramCnt.VRAM_H_CNT & (VRAM_CNT_ENABLE_MASK | VRAM_CNT_MST_MASK)) {
		case 0x82:  // H -> sub BG ext pltt
			if (!IsdifReadMem(pExtPltt->subBG, GX_VRAM_H_ADDR, GX_VRAM_H_SIZE)) return 0;
			break;
	}

	//bank I
	switch (vramCnt.VRAM_I_CNT & (VRAM_CNT_ENABLE_MASK | VRAM_CNT_MST_MASK)) {
		case 0x83:  // I -> sub OBJ ext pltt
			if (!IsdifReadMem(pExtPltt->subOBJ, GX_VRAM_I_ADDR, GX_VRAM_I_SIZE)) return 0;
			break;
	}

	//restore
	if (!IsdifWriteMem(ADDR_VRAMCNT, &vramCnt, sizeof(GxVramCnt))) return 0;

	return 1;
}

static IsdStatus GxdDumpPalette(NCLR **pNclr, int sub, int obj, int extPltt, int slot) {
	NCLR *nclr = NULL;

	unsigned int plttSize = extPltt ? EXT_PLTT_SLOT_SIZE : (256 * sizeof(COLOR));
	COLOR *pltt = (COLOR *) calloc(plttSize, 1);
	IsdStatus status = ISD_STATUS_OK;

	if (!extPltt) {
		//standard palette use
		uint32_t srcAddr = ADDR_STANDARD_PLTT + ((sub << 1) | obj) * 0x200;
		if (!IsdifReadMem(pltt, srcAddr, plttSize)) goto Error;
	} else {
		//get ext palette slot
		GxExtPltt *extPltt = (GxExtPltt *) calloc(1, sizeof(GxExtPltt));

		if (GxdReadExtPltt(extPltt)) {
			memcpy(pltt, extPltt->pltt[sub][obj][slot], EXT_PLTT_SLOT_SIZE);
			free(extPltt);
		} else {
			status = IsdGetLastError();
			free(extPltt);
			goto Error;
		}
	}

	ObjReadBuffer((ObjHeader **) &nclr, (unsigned char *) pltt, plttSize, FILE_TYPE_PALETTE, NCLR_TYPE_BIN, COMPRESSION_NONE);
	nclr->extPalette = extPltt;
	*pNclr = nclr;

Error:
	free(pltt);
	return status;
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

	IsdStatus status = ISD_STATUS_OK;
	unsigned char *oam = (unsigned char *) calloc(OAM_SIZE, 1);
	if (!IsdifReadMem(oam, sub ? ADDR_OAM_SUB : ADDR_OAM, OAM_SIZE)) {
		status = IsdGetLastError();
		goto Error;
	}

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
	*pNcer = ncer;

Error:
	free(oam);
	return status;
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
	BGTYPE_INVALID,
	BGTYPE_3D
} GxBgType;

typedef struct GxBgInfo_ {
	GxBgType type;        // broad type of BG
	unsigned int width;   // height, in pixels
	unsigned int height;  // width, in pixels

	int hasBgScreen;      // whether the BG has BG screen data
	int screenFormat;     // ^ BG screen format
	int colorMode;        // ^ BG color mode

	int extPlttSlot;      // extended palette slot (or -1 if not used)

	unsigned int depth;   // bit depth of graphics data
	uint32_t ofsChar;     // byte address in BG VRAM of character/bitmap data
	uint32_t ofsScrn;     // byte address in BG VRAM of screen data (if exists)
} GxBgInfo;

static GxBgType GxdGetBgType(uint32_t DISPCNT, int iLayer) {
	uint32_t bgMode = (DISPCNT >> 0) & 0x07;
	uint32_t bg03d  = (DISPCNT >> 3) & 0x01;
	if (bgMode > 6) return BGTYPE_INVALID;

	if (iLayer == 0 && bg03d) return BGTYPE_3D;

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

static void GxdGetBgInfo(const GxRegs *regs, int iLayer, GxBgInfo *info) {
	uint32_t DISPCNT = regs->DISPCNT;
	uint16_t bgcnt = regs->BGxCNT[iLayer];

	uint32_t charBaseOffset = (DISPCNT >> 24) & 0x07;
	uint32_t scrBaseOffset  = (DISPCNT >> 27) & 0x07;
	uint32_t useExtPltt     = (DISPCNT >> 30) & 0x01;
	
	uint32_t charBaseBlock  = (bgcnt >>  2) & 0x0F;
	uint32_t scrBaseBlock   = (bgcnt >>  8) & 0x1F;
	uint32_t colorMode      = (bgcnt >>  7) & 0x01;
	uint32_t size           = (bgcnt >> 14) & 0x03;

	static const uint16_t sizeText[4][2] = {
		{ 256, 256 }, { 512, 256 }, { 256, 512 }, { 512, 512 }
	};
	static const uint16_t sizeAffine[4][2] = {
		{ 128, 128 }, { 256, 256 }, { 512, 512 }, { 1024, 1024 }
	};
	static const uint16_t sizeBitmap[4][2] = {
		{ 128, 128 }, { 256, 256 }, { 512, 256 }, { 256, 512 }
	};
	static const uint16_t sizeLargeBitmap[4][2] = {
		{ 512, 1024 }, { 1024, 512 }, { 0, 0 }, { 0, 0 }
	};

	info->type = GxdGetBgType(DISPCNT, iLayer);

	//decode BG screen format and size
	switch (info->type) {
		case BGTYPE_TEXT:
			info->hasBgScreen = 1;
			info->screenFormat = SCREENFORMAT_TEXT;
			if (colorMode) {
				//color-mode: 8bpp
				info->colorMode = SCREENCOLORMODE_256x1;
				info->depth = 8;
			} else {
				//color-mode: 4bpp
				info->colorMode = SCREENCOLORMODE_16x16;
				info->depth = 4;
			}

			info->width  = sizeText[size][0];
			info->height = sizeText[size][1];
			break;

		case BGTYPE_AFFINE:
			info->hasBgScreen = 1;
			info->screenFormat = SCREENFORMAT_AFFINE;
			info->colorMode    = SCREENCOLORMODE_256x1;
			info->depth = 8;
			
			info->width  = sizeAffine[size][0];
			info->height = sizeAffine[size][1];
			break;

		case BGTYPE_AFFINEEXT:
			if (colorMode) {
				//bitmap, no BG screen data
				info->hasBgScreen = 0;
				
				if (bgcnt & 0x04) {
					//direct-color bitmap
					info->depth = 16;
				} else {
					//8bpp bitmap
					info->depth = 8;
				}

				info->width  = sizeBitmap[size][0];
				info->height = sizeBitmap[size][1];
			} else {
				//8bpp affine extended character BG
				info->hasBgScreen = 1;
				info->screenFormat = SCREENFORMAT_AFFINEEXT;
				info->colorMode    = SCREENCOLORMODE_256x16;
				info->depth = 8;

				info->width  = sizeAffine[size][0];
				info->height = sizeAffine[size][1];
			}
			break;

		case BGTYPE_LARGEBITMAP:
			//large bitmap: 8bpp, no BG screen
			info->hasBgScreen = 0;
			info->depth = 8;

			info->width  = sizeLargeBitmap[size][0];
			info->height = sizeLargeBitmap[size][1];
			break;

		default:
			//not supported
			info->hasBgScreen = 0;
			info->depth = 0;

			info->width  = 0;
			info->height = 0;
			break;
	}

	//get graphics location info
	uint32_t chrAddr = (charBaseOffset * 0x10000) + (charBaseBlock * 0x4000);
	uint32_t scrAddr = (scrBaseOffset  * 0x10000) + (scrBaseBlock  *  0x800);

	if (info->hasBgScreen) {
		//fields as normal
		info->ofsChar = chrAddr;
		info->ofsScrn = scrAddr;
	} else {
		//screen data points to graphics
		info->ofsChar = scrAddr;
	}

	//get the extended palette slot info
	if (!useExtPltt || info->depth != 8) {
		//the extended palette is only applicable in 8 bits depth.
		info->extPlttSlot = -1;
	} else {
		//BG0,1 have slot select bit. BG2,3 have fixed slot.
		info->extPlttSlot = iLayer;

		if (iLayer == 0 || iLayer == 1) {
			if (bgcnt & 0x2000) info->extPlttSlot += 2;
		}
	}
}

static IsdStatus GxdDumpCharacterBG(NCGR **pNcgr, const GxBgInfo *info, int sub) {
	//no support for 16bpp direct
	if (info->depth == 16 || info->depth == 0) {
		*pNcgr = NULL;
		return ISD_STATUS_OK;
	}

	IsdStatus status = ISD_STATUS_OK;

	//determine the graphics size of export character
	unsigned int charX = 32, charY = 32;
	if (info->hasBgScreen) {
		//dimensions of character export based on the BG screen format
		switch (info->screenFormat) {
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
		//dimensions of character export are the BG size
		charX = info->width  / 8;
		charY = info->height / 8;
	}

	uint32_t vramAddr = sub ? 0x06200000 : 0x06000000;

	unsigned int charSize = (charX * charY) * (8 * info->depth);
	unsigned char *buf = (unsigned char *) calloc(charSize, 1);
	if (!IsdifReadMem(buf, vramAddr + info->ofsChar, charSize)) {
		status = IsdGetLastError();
		goto Error;
	}

	NCGR *ncgr = (NCGR *) ObjAlloc(FILE_TYPE_CHARACTER, NCGR_TYPE_NCGR);
	ncgr->nBits = info->depth;
	ncgr->tilesX = charX;
	ncgr->tilesY = charY;
	ncgr->nTiles = charX * charY;
	ncgr->bitmap = !info->hasBgScreen;
	ncgr->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
	ChrAllocGraphics(ncgr);
	ChrReadGraphics(ncgr, buf);
	*pNcgr = ncgr;

Error:
	free(buf);
	return status;
}

static IsdStatus GxdDumpBgScreen(NSCR **pNscr, const GxBgInfo *info, int sub) {
	if (!info->hasBgScreen) {
		//no BG screen
		*pNscr = NULL;
		return ISD_STATUS_OK;
	}

	IsdStatus status = ISD_STATUS_OK;

	uint32_t vramAddr = sub ? 0x06200000 : 0x06000000;

	unsigned int unitSize = (info->screenFormat == SCREENFORMAT_AFFINE) ? 1 : 2;
	unsigned int bufSize = (info->width / 8) * (info->height / 8) * (unitSize);
	unsigned char *buf = (unsigned char *) calloc(bufSize, 1);
	if (!IsdifReadMem(buf, vramAddr + info->ofsScrn, bufSize)) {
		status = IsdGetLastError();
		goto Error;
	}

	NSCR *nscr = (NSCR *) ObjAlloc(FILE_TYPE_SCREEN, NSCR_TYPE_NSCR);
	nscr->fmt = info->screenFormat;
	nscr->colorMode = info->colorMode;
	nscr->tilesX = info->width  / 8;
	nscr->tilesY = info->height / 8;
	nscr->dataSize = nscr->tilesX * nscr->tilesY * sizeof(uint16_t);
	nscr->data = (uint16_t *) calloc(nscr->dataSize, 1);
	ScrReadScreenData(nscr, buf, bufSize);
	*pNscr = nscr;

Error:
	free(buf);
	return status;
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
	IsdMonStatus initMonStatus;
	int stop;
	int connected;
} DumpGraphicsDialogData;

static void DumpGraphics(DumpGraphicsDialogData *data, int sub, int obj, int bgLayer) {
	GxRegs *regs = sub ? &data->regsSub : &data->regsMain;

	//construct the object
	NCLR *nclr = NULL;
	NCGR *ncgr = NULL;
	NCER *ncer = NULL;
	NSCR *nscr = NULL;

	if (obj) {
		//get the OAM
		GxdDumpOAM(&ncer, sub, regs->DISPCNT);

		//get the character
		GxdDumpCharacterOBJ(&ncgr, sub, regs->DISPCNT);

		//get the palette
		int useExtPltt = 0;
		unsigned char enableObjExtPltt = (regs->DISPCNT >> 31) & 0x01;
		if (GxdGetBitDepthForOAM(ncer) == 8 && enableObjExtPltt) {
			useExtPltt = 1;
		}
		GxdDumpPalette(&nclr, sub, 1, useExtPltt, 0);

		//setting the graphics bit depth based on OAM data
		GxdSetAttributesForOAM(ncer, ncgr, nclr);
	} else {
		GxBgInfo bgInfo;
		GxdGetBgInfo(regs, bgLayer, &bgInfo);

		//get the palette
		GxdDumpPalette(&nclr, sub, 0, bgInfo.extPlttSlot != -1, bgInfo.extPlttSlot);
		if (nclr != NULL) nclr->nBits = bgInfo.depth;

		//get the character
		GxdDumpCharacterBG(&ncgr, &bgInfo, sub);

		//get the BG screen
		GxdDumpBgScreen(&nscr, &bgInfo, sub);
	}

	//open
	if (nclr != NULL) NpOpenObject(data->hWndMain, &nclr->header);
	if (ncgr != NULL) NpOpenObject(data->hWndMain, &ncgr->header);
	if (ncer != NULL) NpOpenObject(data->hWndMain, &ncer->header);
	if (nscr != NULL) NpOpenObject(data->hWndMain, &nscr->header);
}




static void DumpGraphicsDialogOnTargetDisconnect(DumpGraphicsDialogData *data) {
	KillTimer(data->hWnd, ISD_POLLING_TIMER);
	data->connected = 0;

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
			DumpGraphicsDialogOnTargetDisconnect(data);
			break;
		default:
			MessageBox(data->hWnd, genericMsg, L"NitroPaint", MB_ICONERROR);
			break;
	}
}

static void DumpGraphicsGetLayerStatusText(wchar_t *buf, const GxRegs *regs, int bgLayer) {
	uint32_t layerMask = (regs->DISPCNT >> 8) & 0x1F;

	if (bgLayer == -1) {
		//OBJ
		if (!(layerMask & 0x10)) wsprintfW(buf, L"Disabled");
		else                     wsprintfW(buf, L"Enabled");
	} else {
		//BGx
		GxBgInfo info;
		GxdGetBgInfo(regs, bgLayer, &info);

		if (!(layerMask & (1 << bgLayer))) {
			//layer disable
			wsprintfW(buf, L"Disabled");
		} else if (info.type == BGTYPE_3D) {
			//BG0 3D
			wsprintfW(buf, L"3D");
		} else {
			//normal BG

			const char *name = "";
			switch (info.type) {
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

			wsprintfW(buf, L"%S (%d x %d)", name, info.width, info.height);
		}
	}

}

static void DumpGraphicsDialogUpdateUiForMonStatus(DumpGraphicsDialogData *data, int stop) {
	data->stop = stop;

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

static int DumpGraphicsDialogReadGxRegs(DumpGraphicsDialogData *data) {
	if (!IsdifReadMem(&data->regsMain, 0x04000000, sizeof(GxRegs))) return 0;
	if (!IsdifReadMem(&data->regsSub,  0x04001000, sizeof(GxRegs))) return 0;
	return 1;
}

static void DumpGraphicsDialogOnStop(HWND hWnd, HWND hWndCtl, int cmd, void *param) {
	DumpGraphicsDialogData *data = (DumpGraphicsDialogData *) param;

	if (!IsdifStop()) goto Error;

	//get the state of graphics
	if (!DumpGraphicsDialogReadGxRegs(data)) goto Error;

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
			data->connected = 1;
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
				if (DumpGraphicsDialogReadGxRegs(data)) {
					DumpGraphicsDialogUpdateUiForMonStatus(data, 1);
				} else {
					//fail
					DumpGraphicsDialogHandleError(data, L"The program could not be halted.");
				}
			} else {
				DumpGraphicsDialogUpdateUiForMonStatus(data, 0);
			}

			//set polling timer
			SetTimer(hWnd, ISD_POLLING_TIMER, ISD_POLLING_INTERVAL, NULL);

			break;
		}
		case WM_TIMER:
		{
			IsdMonStatus status;
			if (!IsdifGetMonStatus(&status)) {
				//target disconnect
				DumpGraphicsDialogOnTargetDisconnect(data);
				break;
			}

			//check break status updated
			if (status.fiqEnter && !data->stop) {
				if (DumpGraphicsDialogReadGxRegs(data)) {
					DumpGraphicsDialogUpdateUiForMonStatus(data, 1);
				}
			} else if (!status.fiqEnter && data->stop) {
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

	unsigned char serial[ISD_SERIAL_SIZE];
	IsdifGetSerial(serial);

	wchar_t title[32];
	wsprintfW(title, L"Dump Graphics - %S", serial);

	UiDlgCreateModal(hWnd, DumpGraphicsDialogProc, title, 330, 368, &data);

	IsdifClose();
}
