#include <Windows.h>
#include <CommCtrl.h>

#include "editor.h"
#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "nscrviewer.h"
#include "ncerviewer.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "tilededitor.h"
#include "resource.h"
#include "gdip.h"
#include "palette.h"
#include "bggen.h"
#include "ui.h"
#include "struct.h"
#include "cellgen.h"

#include "preview.h"



//OPX_CHARMAP clipboard format
typedef struct OPX_CHARMAP_ {
	uint32_t timestamp;
	uint32_t nonce;
	uint32_t field8;
	uint32_t format;
	uint32_t objSize;
	uint32_t field14;
	uint32_t selStartX;
	uint32_t selStartY;
	uint32_t selEndX;
	uint32_t selEndY;
	uint32_t field28;
	uint32_t mode;                // 1=BG, 2=OBJ, 3=EXT
	unsigned char field30[0x1C];
} OPX_CHARMAP;

//NitroPaint character data clipboard format
typedef struct NP_CHARS_ {
	uint32_t size;                // size of clipboard data
	uint16_t width;               // width (chars)
	uint16_t height;              // height (chars)
	uint8_t depth;                // bit depth (4, 8)
	uint8_t useAttr;              // use attribute data (1/0)
	uint8_t pltMin;               // smallest palette index of clipboard
	uint8_t pltMax;               // largest palette index of clipboard
	uint32_t paletteSize;         // number of palette colors on clipboard
	unsigned char data[0];        // clipboard data
} NP_CHARS;


extern HICON g_appIcon;


static int sOpxCharmapFormat = 0;
static int sPngFormat = 0;
static int sNpCharsFormat = 0;

static HMENU ChrViewerGetPopupMenu(HWND hWnd);
static void ChrViewerUpdateCursorCallback(HWND hWnd, int pxX, int pxY);
static void ChrViewerRender(HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight);
static int ChrViewerShouldSuppressHighlight(HWND hWnd);
static HCURSOR ChrViewerGetCursor(HWND hWnd, int hit);
static void ChrViewerOnHoverChange(HWND hWnd, int tileX, int tileY);
static BOOL ChrViewerSetCursor(NCGRVIEWERDATA *data, WPARAM wParam, LPARAM lParam);
static void ChrViewerPutPixel(NCGRVIEWERDATA *data, int x, int y, int col);
static void ChrViewerImportDialog(NCGRVIEWERDATA *data, BOOL createPalette, int pasteX, int pasteY, COLOR32 *px, int width, int height);


// ----- routines for operations with other editors

HWND ChrViewerGetAssociatedPaletteViewer(NCGRVIEWERDATA *data) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	return nitroPaintStruct->hWndNclrViewer;
}

NCLRVIEWERDATA *ChrViewerGetAssociatedPaletteViewerData(NCGRVIEWERDATA *data) {
	HWND hWnd = ChrViewerGetAssociatedPaletteViewer(data);
	if (hWnd == NULL) return NULL;

	return (NCLRVIEWERDATA *) EditorGetData(hWnd);
}


// ----- routines for handling attribute data

static int ChrViewerGetCharPalette(NCGRVIEWERDATA *data, int x, int y) {
	if (!data->useAttribute) return data->selectedPalette;
	if (data->ncgr->attr == NULL) return 0;

	return data->ncgr->attr[x + y * data->ncgr->tilesX] & 0xF;
}

static void ChrViewerSetAttribute(NCGRVIEWERDATA *data, int x, int y, int attr) {
	if (data->ncgr->attr == NULL) return; //cannot
	if (x < 0 || y < 0 || x >= data->ncgr->tilesX || y >= data->ncgr->tilesY) return; //cannot

	data->ncgr->attr[x + y * data->ncgr->tilesX] = attr & 0xF;
}



// ----- routines for handling selection

static void SwapInts(int *i1, int *i2) {
	int temp = *i1;
	*i1 = *i2;
	*i2 = temp;
}

static void SwapPoints(int *x1, int *y1, int *x2, int *y2) {
	SwapInts(x1, x2);
	SwapInts(y1, y2);
}


// ----- data manipulation functions

static void ChrViewerInvalidateAllDependents(NCGRVIEWERDATA *data) {
	HWND hWndMain = data->editorMgr->hWnd;
	EditorInvalidateAllByType(hWndMain, FILE_TYPE_NANR);
	EditorInvalidateAllByType(hWndMain, FILE_TYPE_NMCR);
	EditorInvalidateAllByType(hWndMain, FILE_TYPE_SCREEN);

	//update cell editor
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	if (nitroPaintStruct->hWndNcerViewer != NULL) {
		CellViewerGraphicsUpdated(nitroPaintStruct->hWndNcerViewer);
	}
}

static void ChrViewerGraphicsUpdated(NCGRVIEWERDATA *data) {
	//graphics data updated, so invalidate the view window.
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);

	//invalidate every open cell and screen viewer.
	ChrViewerInvalidateAllDependents(data);

	//update preview
	SendMessage(data->hWnd, NV_UPDATEPREVIEW, 0, 0);
}

static void ChrViewerFill(NCGRVIEWERDATA *data, int x, int y, int w, int h, const unsigned char *pat) {
	for (int i = 0; i < h; i++) {
		for (int j = 0; j < w; j++) {
			memcpy(data->ncgr->tiles[(j + x) + (i + y) * data->ncgr->tilesX], pat, 8 * 8);
		}
	}
}

static void ChrViewerFillAttr(NCGRVIEWERDATA *data, int selX, int selY, int selW, int selH, int fill) {
	for (int y = 0; y < selH; y++) {
		for (int x = 0; x < selW; x++) {
			ChrViewerSetAttribute(data, selX + x, selY + y, fill);
		}
	}
}


// ----- functions for handling clipboard

static void ChrViewerCopyDIB(NCGRVIEWERDATA *data) {
	//get objects
	NCGR *ncgr = data->ncgr;
	NCLR *nclr = NULL;

	HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
	if (hWndNclrViewer != NULL) {
		nclr = (NCLR *) EditorGetObject(hWndNclrViewer);
	}

	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	//compute bit depth and palette size of clipboard DIB
	int nBits;
	if (data->useAttribute && ncgr->nBits == 8) {
		nBits = 24;
	} else {
		if (data->useAttribute) nBits = ncgr->nBits + 4;
		else nBits = ncgr->nBits;
	}
	int nColors = (nBits == 32 || nBits == 24) ? 0 : (1 << nBits);
	
	
	int bmWidth = selW * 8;
	int bmHeight = selH * 8;
	int stride = ((bmWidth * nBits + 7) / 8 + 3) & ~3;

	HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + nColors * sizeof(RGBQUAD) + stride * bmHeight);
	BITMAPINFO *pbmi = (BITMAPINFO *) GlobalLock(hGlobal);

	//fill header
	pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pbmi->bmiHeader.biWidth = bmWidth;
	pbmi->bmiHeader.biHeight = bmHeight;
	pbmi->bmiHeader.biPlanes = 1;
	pbmi->bmiHeader.biBitCount = nBits;
	pbmi->bmiHeader.biCompression = DIB_RGB_COLORS;
	pbmi->bmiHeader.biSizeImage = 0;
	pbmi->bmiHeader.biClrUsed = nColors;
	pbmi->bmiHeader.biClrImportant = 0;
	pbmi->bmiHeader.biXPelsPerMeter = 0;
	pbmi->bmiHeader.biYPelsPerMeter = 0;

	//fill palette
	RGBQUAD *pbmpal = pbmi->bmiColors;
	memset(pbmpal, 0, nColors * sizeof(RGBQUAD));
	if (nColors > 0 && nclr != NULL) {
		int palBase = data->selectedPalette << ncgr->nBits;
		if (data->useAttribute) palBase = 0;

		for (int i = 0; i < nColors; i++) {
			COLOR col = 0;
			if ((i + palBase) < nclr->nColors) col = nclr->colors[i + palBase];

			COLOR32 col32 = ColorConvertFromDS(col);
			pbmpal[i].rgbRed = (col32 >> 0) & 0xFF;
			pbmpal[i].rgbGreen = (col32 >> 8) & 0xFF;
			pbmpal[i].rgbBlue = (col32 >> 16) & 0xFF;
		}
	}

	//fill graphics
	unsigned char *px = (unsigned char *) (pbmpal + nColors);
	for (int y = 0; y < bmHeight; y++) {
		int tileY = y / 8;

		//pointer to row
		unsigned char *scan = px + ((bmHeight - 1 - y) * stride);
		for (int x = 0; x < bmWidth; x++) {
			int tileX = x / 8;
			unsigned char *chr = ncgr->tiles[(selX + tileX) + (selY + tileY) * ncgr->tilesX];

			int idx = chr[(x % 8) + (y % 8) * 8];
			if (data->useAttribute) {
				idx |= ChrViewerGetCharPalette(data, selX + tileX, selY + tileY) << ncgr->nBits;
			}

			if (nBits == 4) {
				unsigned char v = scan[x / 2];
				v &= ~(0xF << (((x & 1) ^ 1) * 4));
				v |=  (idx << (((x & 1) ^ 1) * 4));
				scan[x / 2] = v;
			} else if (nBits == 8) {
				scan[x] = idx;
			} else if (nBits == 24) {
				COLOR32 c = 0;
				if (nclr != NULL && idx < nclr->nColors) {
					c = ColorConvertFromDS(nclr->colors[idx]);
				}
				scan[x * 3 + 0] = (c >> 16) & 0xFF;
				scan[x * 3 + 1] = (c >>  8) & 0xFF;
				scan[x * 3 + 2] = (c >>  0) & 0xFF;
			} else {
				//TODO
			}
		}
	}

	GlobalUnlock(hGlobal);

	SetClipboardData(CF_DIB, hGlobal);
}

static int ChrViewerEnsureClipboardFormatOPX(void) {
	if (sOpxCharmapFormat) return sOpxCharmapFormat;

	sOpxCharmapFormat = RegisterClipboardFormat(L"OPX_CHARMAP");
	return sOpxCharmapFormat;
}

static int ChrViewerEnsureClipboardFormatPNG(void) {
	if (sPngFormat) return sPngFormat;

	sPngFormat = RegisterClipboardFormat(L"PNG");
	return sPngFormat;
}

static int ChrViewerEnsureClipboardFormatNP_CHARS(void) {
	if (sNpCharsFormat) return sNpCharsFormat;

	sNpCharsFormat = RegisterClipboardFormat(L"NP_CHARS");
	return sNpCharsFormat;
}

static void ChrViewerCopyOPX(NCGRVIEWERDATA *data) {
	if (!ChrViewerEnsureClipboardFormatOPX()) return;

	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(OPX_CHARMAP));
	OPX_CHARMAP *opx = (OPX_CHARMAP *) GlobalLock(hGlobal);

	memset(opx, 0, sizeof(*opx));
	opx->format = (data->ncgr->nBits == 4) ? 0x29 : 0x2A;
	opx->mode = 1;    // BG
	opx->objSize = 0; // 8x8
	opx->selStartX = selX;
	opx->selStartY = selY;
	opx->selEndX = selX + selW;
	opx->selEndY = selY + selH;

	GlobalUnlock(hGlobal);
	SetClipboardData(sOpxCharmapFormat, hGlobal);
}

static void ChrViewerCopyNP_CHARS(NCGRVIEWERDATA *data) {
	if (!ChrViewerEnsureClipboardFormatNP_CHARS()) return;
	
	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	NCLRVIEWERDATA *nclrViewerData = ChrViewerGetAssociatedPaletteViewerData(data);
	NCLR *nclr = NULL;
	if (nclrViewerData != NULL) {
		nclr = nclrViewerData->nclr;
	}

	//get size to copy
	int clipPaletteSize = (nclr != NULL) ? nclr->nColors * sizeof(COLOR) : 0;
	int attrSize = selW * selH;
	int charSize = selW * selH * 64;

	int size = sizeof(NP_CHARS) + clipPaletteSize + attrSize + charSize;
	HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
	NP_CHARS *npc = (NP_CHARS *) GlobalLock(hGlobal);

	npc->size = size;
	npc->width = selW;
	npc->height = selH;
	npc->depth = data->ncgr->nBits;
	npc->useAttr = data->useAttribute;
	npc->paletteSize = clipPaletteSize / sizeof(COLOR);

	//copy palette
	if (nclr != NULL) memcpy(npc->data, nclr->colors, clipPaletteSize);

	//copy chars
	unsigned char *chrDest = npc->data + clipPaletteSize;
	for (int y = 0; y < selH; y++) {
		for (int x = 0; x < selW; x++) {
			memcpy(chrDest + 64 * (x + y * selW), data->ncgr->tiles[(selX + x) + (selY + y) * data->ncgr->tilesX], 64);
		}
	}

	//copy attr
	unsigned char *attrDest = chrDest + charSize;
	if (data->ncgr->attr != NULL) {
		npc->pltMin = 0xF;
		npc->pltMax = 0x0;
		for (int y = 0; y < selH; y++) {
			for (int x = 0; x < selW; x++) {
				int attr = data->ncgr->attr[(selX + x) + (selY + y) * data->ncgr->tilesX];
				attrDest[x + y * selW] = attr;

				if (attr > npc->pltMax) npc->pltMax = attr;
				if (attr < npc->pltMin) npc->pltMin = attr;
			}
		}
	}

	GlobalUnlock(hGlobal);
	SetClipboardData(sNpCharsFormat, hGlobal);
}

static void ChrViewerCopyNP_SCRN(NCGRVIEWERDATA *data) {
	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	uint16_t *bgdat = (uint16_t *) calloc(selW * selH, sizeof(uint16_t));

	for (int y = 0; y < selH; y++) {
		for (int x = 0; x < selW; x++) {
			int attr = ChrViewerGetCharPalette(data, x + selX, y + selY);
			int chrno = (x + selX) + (y + selY) * data->ncgr->tilesX;
			bgdat[x + y * selW] = (attr << 12) | (chrno & 0x3FF);
		}
	}

	ScrViewerCopyNP_SCRN(selW, selH, bgdat);
	free(bgdat);
}

static int ChViewerObjDimensionToShape(int width, int height) {
	if (width == height) return 0; // square
	if (width > height) return 1;  // wide rectangle
	if (height > width) return 2;  // tall rectangle
	return 0; // invalid shape?
}

static int ChrViewerObjDimensionToSize(int width, int height) {
	if ((width == 1 && height == 1) || (width == 1 && height == 2) || (width == 2 && height == 1)) return 0;
	if ((width == 2 && height == 2) || (width == 1 && height == 4) || (width == 4 && height == 1)) return 1;
	if ((width == 4 && height == 4) || (width == 2 && height == 4) || (width == 4 && height == 2)) return 2;
	if ((width == 8 && height == 8) || (width == 4 && height == 8) || (width == 8 && height == 4)) return 3;
	return 0; // invalid shape?
}

static OBJ_BOUNDS *ChrViewerGenObjSplit(int nChrX, int nChrY, int xMin, int yMin, int maxW, int maxH, int consider1D, int *pnObj) {
	//split region into OBJ.
	int maxObj = nChrX * nChrY, nOBJ = 0;
	OBJ_BOUNDS *bounds = (OBJ_BOUNDS *) calloc(maxObj, sizeof(OBJ_BOUNDS));
	
	for (int y = 0; y < nChrY;) {
		//compute height of row
		int rowY = yMin + y * 8;
		int rowHeight = maxH;
		if ((y + rowHeight) > nChrY) rowHeight = 4;
		if ((y + rowHeight) > nChrY) rowHeight = 2;
		if ((y + rowHeight) > nChrY) rowHeight = 1;

		for (int x = 0; x < nChrX;) {
			//compute width of column
			int colX = xMin + x * 8;
			int colWidth = maxW;
			if ((x + colWidth) > nChrX) colWidth = 4;
			if ((x + colWidth) > nChrX) colWidth = 2;
			if ((x + colWidth) > nChrX) colWidth = 1;

			//put oBJ
			OBJ_BOUNDS *bound = &bounds[nOBJ++];
			bound->x = colX;
			bound->y = rowY;
			bound->width = colWidth;
			bound->height = rowHeight;

			//check valid size
			if (colWidth <= 4 && rowHeight <= 4 || colWidth == rowHeight || (colWidth == 8 && rowHeight == 4) || (colWidth == 4 && rowHeight == 8)) {
				//valid OBJ size, no further action needed
			} else {
				//invalid OBJ size, divide OBJ in half to make it more square-like
				if (colWidth > rowHeight) {
					bound->width /= 2;
				} else {
					bound->height /= 2;
				}
				
				//add OBJ for second half
				OBJ_BOUNDS *bound2 = &bounds[nOBJ++];
				bound2->x = bound->x;
				bound2->y = bound->y;
				bound2->width = bound->width;
				bound2->height = bound->height;
				if (colWidth > rowHeight) {
					bound2->x += bound->width * 8;
				} else {
					bound2->y += bound->height * 8;
				}

				if (consider1D && colWidth == 8 && rowHeight == 2) {
					//since OBJ data is being created for 1D mapping, when splitting, we must ensure that
					//we do not break a 64x16 OBJ into two 32x16 OBJ, since this would break the reconstruction
					//of graphics data. In this case, we must break it into 4 OBJ of 32x8.
					bound->height /= 2;
					bound2->height /= 2;

					OBJ_BOUNDS *bound3 = &bounds[nOBJ++];
					bound3->x = bound->x;
					bound3->y = bound->y + bound->height * 8;
					bound3->width = bound->width;
					bound3->height = bound->height;

					OBJ_BOUNDS *bound4 = &bounds[nOBJ++];
					bound4->x = bound2->x;
					bound4->y = bound2->y + bound2->height * 8;
					bound4->width = bound3->width;
					bound4->height = bound3->height;
				}
			}

			x += colWidth;
		}
		y += rowHeight;
	}

	//shrink alloc
	bounds = realloc(bounds, nOBJ * sizeof(OBJ_BOUNDS));
	*pnObj = nOBJ;
	return bounds;
}

static void ChrViewerCopyNP_OBJ(NCGRVIEWERDATA *data) {
	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	NCGR *ncgr = data->ncgr;

	//OBJ origin
	int startX = -(selW * 8 / 2);
	int startY = -(selH * 8 / 2);

	int nTotalObj = 0;
	uint16_t *attrForMapping[5] = { 0 };
	uint16_t nObjForMapping[5] = { 0 };
	uint16_t presenceMask = 0;

	//get OBJ split
	for (int j = 0; j < 5; j++) {
		//for 2D mapping, we can freely copy OBJ sizes of any dimension.
		int maxObjW = 8, maxObjH = 8;
		if (j != MAPPING_2D) {
			//when 1D mapping is used, though, graphics do not follow the required order. They may, however,
			//if we select the whole width of the graphics area, and that width is the size of a valid OBJ.

			if (selW == ncgr->tilesX && (ncgr->tilesX == 1 || ncgr->tilesX == 2 || ncgr->tilesX == 4 || ncgr->tilesX == 8)) {
				//good in all scenarios except when the width is 8 (64px) and an OBJ of 16px height is created.
				//This 64x16 OBJ will need to be split due to being an invalid size, thus breaking the mapping
				//because it is more than 8px (1chr) tall. This will be accounted for when creating the split.
			} else {
				//limit OBJ height to 1 to preserve graphics
				maxObjH = 1;
			}
		}

		int granularity = 1;
		switch (j) {
			case MAPPING_1D_64K:
				granularity = 2;
				break;
			case MAPPING_1D_128K:
				granularity = 4;
				break;
			case MAPPING_1D_256K:
				granularity = 8;
				break;
		}

		int nObj = 0, badObj = 0;
		OBJ_BOUNDS *bounds = ChrViewerGenObjSplit(selW, selH, startX, startY, maxObjW, maxObjH, j != MAPPING_2D, &nObj);
		uint16_t *obj = (uint16_t *) calloc(nObj, 8 * sizeof(uint16_t));
		for (int i = 0; i < nObj; i++) {
			OBJ_BOUNDS *bound = &bounds[i];
			int x = (bound->x - startX) / 8;
			int y = (bound->y - startY) / 8;

			int chno = (selX + x) + (selY + y) * data->ncgr->tilesX;
			int pltt = ChrViewerGetCharPalette(data, selX + x, selY + y);
			if (data->ncgr->nBits == 8) chno <<= 1;

			if (chno % granularity) {
				//cannot reproduce cell
				badObj = 1;
				break;
			}
			chno /= granularity;

			int shape = ChViewerObjDimensionToShape(bound->width, bound->height);
			int size = ChrViewerObjDimensionToSize(bound->width, bound->height);

			uint16_t *attr = obj + i * 4;
			attr[0] = 0x0000 | (bound->y & 0x00FF) | (shape << 14) | ((data->ncgr->nBits == 8) << 13);
			attr[1] = 0x0000 | (bound->x & 0x01FF) | (size << 14);
			attr[2] = 0x0000 | (chno & 0x03FF) | (pltt << 12);
			attr[3] = chno >> 10; // fake OAM attribute but used in intermediate representation
		}
		free(bounds);

		if (!badObj) {
			nTotalObj += nObj;
			nObjForMapping[j] = nObj;
			attrForMapping[j] = obj;
			presenceMask |= 1 << j;
		} else {
			free(obj);
		}
	}

	//create clipboard data
	NP_OBJ *cpy = (NP_OBJ *) calloc(sizeof(NP_OBJ) + 8 * nTotalObj, 1);
	cpy->xMin = startX;
	cpy->yMin = startY;
	cpy->width = selW * 8;
	cpy->height = selH * 8;
	memcpy(cpy->nObj, nObjForMapping, sizeof(nObjForMapping));
	cpy->presenceMask = presenceMask;

	unsigned int offsDest = 0;
	for (int i = 0; i < 5; i++) {
		if (attrForMapping[i] != NULL) {
			memcpy(cpy->attr + offsDest * 4, attrForMapping[i], nObjForMapping[i] * 8);
			cpy->offsObjData[i] = offsDest;

			free(attrForMapping[i]);
			offsDest += nObjForMapping[i];
		}
	}

	CellViewerCopyObjData(cpy);
	free(cpy);
}

static void ChrViewerCopy(NCGRVIEWERDATA *data) {
	if (!TedHasSelection(&data->ted)) return;

	HWND hWnd = data->hWnd;
	OpenClipboard(hWnd);
	EmptyClipboard();

	//copy data to clipboard
	ChrViewerCopyDIB(data);      // DIB data
	ChrViewerCopyOPX(data);      // OPTPiX data
	ChrViewerCopyNP_CHARS(data); // NitroPaint data
	ChrViewerCopyNP_SCRN(data);  // NitroPaint data
	ChrViewerCopyNP_OBJ(data);   // NitroPaint data

	CloseClipboard();
}

static void ChrViewerPaste(NCGRVIEWERDATA *data, BOOL contextMenu) {
	//get selection paste point
	int pasteX = -1, pasteY = -1;
	TedGetPasteLocation(&data->ted, contextMenu, &pasteX, &pasteY);

	//has point?
	if (pasteX == -1 || pasteY == -1) {
		return; //cannot paste
	}

	NCLR *nclr = NULL;
	NCGR *ncgr = data->ncgr;
	NCLRVIEWERDATA *nclrViewerData = ChrViewerGetAssociatedPaletteViewerData(data);
	if (nclrViewerData != NULL) {
		nclr = nclrViewerData->nclr;
	}
	if (nclr == NULL) return;

	HWND hWnd = data->hWnd;
	OpenClipboard(hWnd);

	//get NP_CHARS data from clipboard if we're using attribute mode
	ChrViewerEnsureClipboardFormatNP_CHARS();
	HGLOBAL hNpChars = GetClipboardData(sNpCharsFormat);
	if (data->useAttribute && hNpChars != NULL) {
		NP_CHARS *npc = (NP_CHARS *) GlobalLock(hNpChars);
		unsigned char *chars = npc->data + npc->paletteSize * sizeof(COLOR);
		unsigned char *attrs = chars + npc->width * npc->height * 64;

		//paste char data
		for (int y = 0; y < npc->height; y++) {
			for (int x = 0; x < npc->width; x++) {
				if ((pasteX + x) >= ncgr->tilesX) continue;
				if ((pasteY + y) >= ncgr->tilesY) continue;

				int i = x + y * npc->width;
				memcpy(ncgr->tiles[(pasteX + x) + (pasteY + y) * data->ncgr->tilesX], chars + i * 64, 64);
				ChrViewerSetAttribute(data, pasteX + x, pasteY + y, attrs[i]);
			}
		}

		//mark selection
		TedSelect(&data->ted, pasteX, pasteY, npc->width, npc->height);

		GlobalUnlock(hNpChars);
		goto ReleaseClipboard;
	}

	//read bitmap off the clipboard
	int width, height, pltSize;
	COLOR32 *imgPalette;
	unsigned char *indexed;
	COLOR32 *px = GetClipboardBitmap(&width, &height, &indexed, &imgPalette, &pltSize);

	if (px != NULL) {
		//decode bitmap
		int nColsDest = 1 << ncgr->nBits;
		int palFirst = nColsDest * data->selectedPalette;

		//check palette for matching the current data
		int matchesPalette = (indexed == NULL) ? FALSE : TRUE; //cannot have a matching palette if there's no palette!
		if (matchesPalette && nclr != NULL) {
			//check matching from the start of the currently selected palette in the viewer
			for (int iPx = 0; iPx < width * height; iPx++) {
				int i = indexed[iPx];
				if (i > nColsDest || i >= pltSize || (palFirst + i) >= nclr->nColors) {
					//if the indexed value is out of bounds of either the source or destination
					//palette, mark it as unmatching.
					matchesPalette = FALSE;
					break;
				}

				COLOR32 c = imgPalette[i];
				if (ColorConvertToDS(c) != nclr->colors[palFirst + i]) {
					matchesPalette = FALSE;
					break;
				}
			}
		}

		BOOL reindex = FALSE;
		if (!matchesPalette) {
			//palette doesn't match. Ask user if we want to re-index the graphics.
			int id = MessageBox(data->hWnd, L"Palettes do not match. Reindex graphics?", L"Palette mismatch", MB_ICONQUESTION | MB_YESNOCANCEL);
			if (id == IDCANCEL) goto Done; //cancel operation
			if (id == IDYES) reindex = TRUE;
		}

		//if reindex is selected, finsih paste via the bitmap import dialog.
		if (reindex) {
			//create pixel copy
			COLOR32 *pxCopy = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
			memcpy(pxCopy, px, width * height * sizeof(COLOR32));

			ChrViewerImportDialog(data, FALSE, pasteX, pasteY, pxCopy, width, height);
			//pixel array owned by the conversion process now
		} else {

			//scan pixels from bitmap into the graphics editor
			int tilesY = height / 8;
			int tilesX = width / 8;
			for (int y = 0; y < tilesY * 8; y++) {
				for (int x = 0; x < tilesX * 8; x++) {
					//copy bits directly
					COLOR32 c = px[x + y * width];
					if (indexed != NULL) {
						c = indexed[x + y * width];
					}
					ChrViewerPutPixel(data, pasteX * 8 + x, pasteY * 8 + y, c);
				}
			}

			//next, mark the pasted region as selected.
			TedSelect(&data->ted, pasteX, pasteY, tilesX, tilesY);
		}

	Done:
		if (px != NULL) free(px);
		if (indexed != NULL) free(indexed);
		if (imgPalette != NULL) free(imgPalette);
	}

ReleaseClipboard:
	CloseClipboard();
	ChrViewerGraphicsUpdated(data);
}

static void ChrViewerCut(NCGRVIEWERDATA *data) {
	if (!TedHasSelection(&data->ted)) return;

	unsigned char erased[64] = { 0 };

	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	ChrViewerCopy(data);
	ChrViewerFill(data, selX, selY, selW, selH, erased);
}


// ----- tool function

static int ChrViewerIsSelectionMode(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);
	return data->mode == CHRVIEWER_MODE_SELECT;
}

static void ChrViewerSetMode(NCGRVIEWERDATA *data, ChrViewerMode mode) {
	data->lastMode = data->mode;
	data->mode = mode;

	//update cursor
	ChrViewerSetCursor(data, 0, HTCLIENT);
	TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
}

static int ChrViewerGetSelectedColor(NCGRVIEWERDATA *data) {
	//get open palette file to probe for selection.
	NCLRVIEWERDATA *nclrViewerData = ChrViewerGetAssociatedPaletteViewerData(data);
	if (nclrViewerData == NULL) return -1;
	if (nclrViewerData->selStart == -1 || nclrViewerData->selEnd == -1) return -1;

	int colidx = min(nclrViewerData->selStart, nclrViewerData->selEnd);
	colidx &= (1 << (data->ncgr->nBits + 4)) - 1;
	return colidx;
}

static int ChrViewerSetSelectedColor(NCGRVIEWERDATA *data, int col) {
	NCLRVIEWERDATA *nclrViewerData = ChrViewerGetAssociatedPaletteViewerData(data);
	if (nclrViewerData == NULL) return 0;
	
	nclrViewerData->selStart = nclrViewerData->selEnd = col;
	InvalidateRect(nclrViewerData->hWnd, NULL, FALSE);
	return 1;
}

static void ChrViewerPutPixelInternal(NCGR *ncgr, int x, int y, int col) {
	int chrX = x / 8, chrY = y / 8;
	unsigned char *chr = ncgr->tiles[chrX + chrY * ncgr->tilesX];
	chr[(x & 7) + (y & 7) * 8] = col;
}

static int ChrViewerGetPixelInternal(NCGR *ncgr, int x, int y) {
	int chrX = x / 8, chrY = y / 8;
	unsigned char *chr = ncgr->tiles[chrX + chrY * ncgr->tilesX];
	return chr[(x & 7) + (y & 7) * 8];
}

static void ChrViewerPutPixel(NCGRVIEWERDATA *data, int x, int y, int col) {
	if (x < 0 || y < 0) return;
	if (x >= (data->ncgr->tilesX * 8) || y >= (data->ncgr->tilesY * 8)) return;

	ChrViewerPutPixelInternal(data->ncgr, x, y, col & ((1 << data->ncgr->nBits) - 1));
	ChrViewerSetAttribute(data, x / 8, y / 8, col >> data->ncgr->nBits);
}

static int ChrViewerGetPixel(NCGRVIEWERDATA *data, int x, int y) {
	if (x < 0 || y < 0) return -1;
	if (x >= (data->ncgr->tilesX * 8) || y >= (data->ncgr->tilesY * 8)) return -1;

	return ChrViewerGetPixelInternal(data->ncgr, x, y) | (ChrViewerGetCharPalette(data, x / 8, y / 8) << data->ncgr->nBits);
}

static void ChrViewerConnectLine(NCGRVIEWERDATA *data, int lastX, int lastY, int pxX, int pxY, int pcol) {
	//compute deltas
	int x1 = lastX, x2 = pxX, y1 = lastY, y2 = pxY;
	int dx = x2 - x1, dy = y2 - y1;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	//if dx and dy are zero, put one pixel (avoid divide by zero)
	if (dx == 0 && dy == 0) {
		ChrViewerPutPixel(data, x1, y1, pcol);
		return;
	}

	//draw horizontally or vertically
	if (dx >= dy) {
		//draw left->right
		if (x2 < x1) SwapPoints(&x1, &y1, &x2, &y2);

		//scan
		for (int i = 0; i <= dx; i++) {
			int py = ((i * (y2 - y1)) * 2 + dx) / (dx * 2) + y1;
			ChrViewerPutPixel(data, i + x1, py, pcol);
		}
	} else {
		//draw top->bottom. ensure top point first
		if (y2 < y1) SwapPoints(&x1, &y1, &x2, &y2);

		//scan
		for (int i = 0; i <= dy; i++) {
			int px = ((i * (x2 - x1)) * 2 + dy) / (dy * 2) + x1;
			ChrViewerPutPixel(data, px, i + y1, pcol);
		}
	}
}

static void ChrViewerFloodFill(NCGRVIEWERDATA *data, int x, int y, int pcol) {
	int gfxW = data->ncgr->tilesX * 8, gfxH = data->ncgr->tilesY * 8;

	//bounds check coordinate
	if (x < 0 || y < 0 || x >= gfxW || y >= gfxH) return;
	pcol &= ((1 << data->ncgr->nBits) - 1);

	//get color of interest
	NCGR *ncgr = data->ncgr;
	int bkcol = ChrViewerGetPixel(data, x, y);
	if (pcol == bkcol) return; //no-op (stop a huge memory spiral)

	//keep a stack of colors. push the first color. Each time we pop a color from the stack, 
	//check all 8 neighbors. If they match the initial color, push them to the stack. Fill
	//the pixel with the new color. Repeat until stack is empty.
	StStack stack;
	StStatus s = StStackCreateInline(&stack, POINT);
	if (!ST_SUCCEEDED(s)) return; //fail
	
	POINT first = { x, y };
	s = StStackPush(&stack, &first);
	if (!ST_SUCCEEDED(s)) goto AllocFail;

	while (stack.length) {
		POINT pt;
		StStackPop(&stack, &pt);
		ChrViewerPutPixel(data, pt.x, pt.y, pcol);
		
		//search vicinity
		for (int y_ = 0; y_ < 3; y_++) {
			for (int x_ = 0; x_ < 3; x_++) {
				//skip corners and center
				if (x_ != 1 && y_ != 1) continue;
				if (x_ == 1 && y_ == 1) continue;

				int drawX = pt.x + (x_ - 1);
				int drawY = pt.y + (y_ - 1);
				if (drawX >= 0 && drawY >= 0 && drawX < gfxW && drawY < gfxH) {
					int c = ChrViewerGetPixel(data, drawX, drawY);
					if (c == bkcol) {
						//push
						POINT next = { drawX, drawY };
						s = StStackPush(&stack, &next);
						if (!ST_SUCCEEDED(s)) goto AllocFail;
					}
				}
			}
		}
	}

AllocFail:
	StStackFree(&stack);
}

static void ChrViewerFlipSelection(NCGRVIEWERDATA *data, BOOL flipH, BOOL flipV) {
	if (!TedHasSelection(&data->ted)) return;

	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	//1: swap whole character blocks
	if (flipH) {
		for (int y = 0; y < selH; y++) {
			for (int x = 0; x < selW / 2; x++) {
				//flip graphics positions
				unsigned char **p1 = &data->ncgr->tiles[(selX + x) + (selY + y) * data->ncgr->tilesX];
				unsigned char **p2 = &data->ncgr->tiles[(selX + selW - 1 - x) + (selY + y) * data->ncgr->tilesX];
				unsigned char *tmp = *p1;
				*p1 = *p2;
				*p2 = tmp;

				//flip attributes
				if (data->ncgr->attr != NULL) {
					unsigned char tmp2 = ChrViewerGetCharPalette(data, selX + x, selY + y);
					ChrViewerSetAttribute(data, selX + x, selY + y, ChrViewerGetCharPalette(data, selX + selW - 1 - x, selY + y));
					ChrViewerSetAttribute(data, selX + selW - 1 - x, selY + y, tmp2);
				}
			}
		}
	}
	if (flipV) {
		for (int y = 0; y < selH / 2; y++) {
			for (int x = 0; x < selW; x++) {
				//flip graphics positions
				unsigned char **p1 = &data->ncgr->tiles[(selX + x) + (selY + y) * data->ncgr->tilesX];
				unsigned char **p2 = &data->ncgr->tiles[(selX + x) + (selY + selH - 1 - y) * data->ncgr->tilesX];
				unsigned char *tmp = *p1;
				*p1 = *p2;
				*p2 = tmp;

				//flip attributes
				if (data->ncgr->attr != NULL) {
					unsigned char tmp2 = ChrViewerGetCharPalette(data, selX + x, selY + y);
					ChrViewerSetAttribute(data, selX + x, selY + y, ChrViewerGetCharPalette(data, selX + x, selY + selH - 1 - y));
					ChrViewerSetAttribute(data, selX + x, selY + selH - 1 - y, tmp2);
				}
			}
		}
	}

	//2: flip individual pixels
	for (int y = 0; y < selH; y++) {
		for (int x = 0; x < selW; x++) {
			unsigned char *chr = data->ncgr->tiles[(x + selX) + (y + selY) * data->ncgr->tilesX];
			if (flipH) {
				for (int i = 0; i < 32; i++) {
					int idx = (i % 4) + 8 * (i / 4);
					unsigned char tmp = chr[idx];
					chr[idx] = chr[idx ^ 0x7];
					chr[idx ^ 0x7] = tmp;
				}
			}
			if (flipV) {
				for (int i = 0; i < 32; i++) {
					int idx = i ^ (0x7 << 3);
					unsigned char tmp = chr[i];
					chr[i] = chr[idx];
					chr[idx] = tmp;
				}
			}
		}
	}
	ChrViewerGraphicsUpdated(data);
}

static void ChrViewerStampTile(NCGRVIEWERDATA *data, int x, int y) {
	if (!TedHasSelection(&data->ted)) return;

	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	//if in selection, do nothing
	if (x >= selX && y >= selY && x < (selX + selW) && y < (selY + selH)) return;

	int srcTileX = (x - selX) % selW; if (srcTileX < 0) srcTileX += selW;
	int srcTileY = (y - selY) % selH; if (srcTileY < 0) srcTileY += selH;
	srcTileX += selX;
	srcTileY += selY;

	unsigned char *src = data->ncgr->tiles[srcTileX + srcTileY * data->ncgr->tilesX];
	unsigned char *dst = data->ncgr->tiles[x + y * data->ncgr->tilesX];
	memcpy(dst, src, 64);

	ChrViewerGraphicsUpdated(data);
}



// ----- functions for handling the margins



static void ChrViewerGetGraphicsSize(NCGRVIEWERDATA *data, int *width, int *height) {
	if (data->ncgr != NULL) {
		*width = data->ncgr->tilesX * (8 * data->scale);
		*height = data->ncgr->tilesY * (8 * data->scale);
	}
}

static void ChrViewerExportBitmap(NCGRVIEWERDATA *data, NCGR *ncgr, NCLR *nclr, int paletteIndex, LPCWSTR path) {
	//convert to bitmap layout
	int tilesX = ncgr->tilesX, tilesY = ncgr->tilesY;
	int width = tilesX * 8, height = tilesY * 8;

	//can only output indexed bitmap if no attribute information is used, or bit depth < 8.
	if (!data->useAttribute || ncgr->nBits < 8) {
		unsigned char *bits = (unsigned char *) calloc(width * height, 1);

		if (!data->useAttribute) {
			//no attribute usage
			for (int tileY = 0; tileY < tilesY; tileY++) {
				for (int tileX = 0; tileX < tilesX; tileX++) {
					unsigned char *tile = ncgr->tiles[tileX + tileY * tilesX];
					for (int y = 0; y < 8; y++) {
						memcpy(bits + tileX * 8 + (tileY * 8 + y) * width, tile + y * 8, 8);
					}
				}
			}
		} else {
			//attribute usage. for 4-bit graphics, most significant 4 bits are the palette number.
			for (int tileY = 0; tileY < tilesY; tileY++) {
				for (int tileX = 0; tileX < tilesX; tileX++) {
					unsigned char *tile = ncgr->tiles[tileX + tileY * tilesX];
					int attr = ChrViewerGetCharPalette(data, tileX, tileY);
					for (int y = 0; y < 8; y++) {
						memcpy(bits + tileX * 8 + (tileY * 8 + y) * width, tile + y * 8, 8);
						for (int i = 0; i < 8; i++) bits[tileX * 8 + i + (tileY * 8 + y) * width] |= attr << ncgr->nBits;
					}
				}
			}
		}

		//convert palette
		int depth = ncgr->nBits;
		int paletteSize = 1 << depth;
		int paletteStart = paletteIndex << depth;
		if (data->useAttribute) {
			paletteSize *= 16; // attribute data allows up to 16 palettes
			paletteStart = 0;  // include full palette
		}

		COLOR32 *pal = (COLOR32 *) calloc(paletteSize, sizeof(COLOR32));
		for (int i = 0; i < paletteSize; i++) {
			COLOR32 c = 0;
			if (paletteStart + i < nclr->nColors) {
				c = ColorConvertFromDS(nclr->colors[paletteStart + i]);
			}
			if (i & ((1 << depth) - 1)) c |= 0xFF000000;
			pal[i] = c;
		}

		ImgWriteIndexed(bits, width, height, pal, paletteSize, path);

		free(bits);
		free(pal);
	} else {
		//must output direct color bitmap.
		COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				int tileX = x / 8, tileY = y / 8;
				unsigned char *chr = ncgr->tiles[tileX + tileY * ncgr->tilesX];
				int idx = chr[(x % 8) + (y % 8) * 8];
				idx |= ChrViewerGetCharPalette(data, tileX, tileY) << 8;

				COLOR32 c = 0;
				if (idx < nclr->nColors) {
					c = ColorConvertFromDS(nclr->colors[idx]);
				}
				if ((idx % 256) != 0) c |= 0xFF000000;
				px[x + y * width] = c;
			}
		}

		ImgWrite(px, width, height, path);
		free(px);
	}
}

static void ChrViewerPopulateWidthField(NCGRVIEWERDATA *data) {
	SendMessage(data->hWndWidthDropdown, CB_RESETCONTENT, 0, 0);
	int nTiles = data->ncgr->nTiles;
	int nStrings = 0;
	WCHAR bf[16];

	for (int i = 1; i <= nTiles; i++) {
		if (nTiles % i) continue;
		wsprintfW(bf, L"%d", i);
		UiCbAddString(data->hWndWidthDropdown, bf);
		if (i == data->ncgr->tilesX) {
			UiCbSetCurSel(data->hWndWidthDropdown, (WPARAM) nStrings);
		}
		nStrings++;
	}
}

static void ChrViewerUpdateCharacterLabel(NCGRVIEWERDATA *data) {
	wchar_t buffer[64] = L" ";
	unsigned int pos = 1;

	if (data->ted.hoverIndex != -1) {
		pos = wsprintf(buffer + pos, L"Character %d  ", data->ted.hoverIndex);
	}

	if (TedHasSelection(&data->ted)) {
		int selX, selY, selW, selH;
		TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);
		wsprintf(buffer + pos, L"Selection: (%d, %d), (%d, %d)", selX, selY, selW, selH);
	}
	SendMessage(data->hWndCharacterLabel, WM_SETTEXT, wcslen(buffer), (LPARAM) buffer);
}

static void ChrViewerSetWidth(NCGRVIEWERDATA *data, int width) {
	//update width
	ChrSetWidth(data->ncgr, width);
	TedUpdateSize((EDITOR_DATA *) data, &data->ted, data->ncgr->tilesX, data->ncgr->tilesY);

	//update
	ChrViewerPopulateWidthField(data);
	SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
	ChrViewerGraphicsUpdated(data);
}

static void ChrViewerSetDepth(NCGRVIEWERDATA *data, int depth) {
	//set depth and update UI
	ChrSetDepth(data->ncgr, depth);
	TedUpdateSize((EDITOR_DATA *) data, &data->ted, data->ncgr->tilesX, data->ncgr->tilesY);
	
	ChrViewerPopulateWidthField(data);
	SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
	ChrViewerGraphicsUpdated(data);

	//update palette editor view
	HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
	if (hWndNclrViewer != NULL) {
		InvalidateRect(hWndNclrViewer, NULL, FALSE);
	}
}

typedef struct CHARIMPORTDATA_ {
	COLOR32 *px;
	int width;
	int height;
	NCLR *nclr;
	NCGR *ncgr;
	int contextHoverX;
	int contextHoverY;
	int selectedPalette;
	HWND hWndOverwritePalette;
	HWND hWndPaletteBase;
	HWND hWndPaletteSize;
	HWND hWndDither;
	HWND hWndDiffuse;
	HWND hWnd1D;
	HWND hWndCompression;
	HWND hWndMaxChars;
	HWND hWndObjMode;
	HWND hWndObjSize;
	HWND hWndMappingGranularity;
	HWND hWndImport;
	NpBalanceControl balance;
} CHARIMPORTDATA;


static void ChrViewerOnCreate(NCGRVIEWERDATA *data) {
	HWND hWnd = data->hWnd;
	float dpiScale = GetDpiScale();

	data->frameData.contentWidth = 256;
	data->frameData.contentHeight = 256;
	data->mode = CHRVIEWER_MODE_SELECT;
	data->lastMode = CHRVIEWER_MODE_PEN;
	data->showBorders = 1;
	data->scale = 2; //default 200%
	data->selectedPalette = 0;
	data->useAttribute = 0;
	data->transparent = g_configuration.renderTransparent;

	HWND hWndViewer = CreateWindow(L"NcgrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL,
		MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, 256, 256, hWnd, NULL, NULL, NULL);
	TedInit(&data->ted, hWnd, hWndViewer, 8, 8);
	data->ted.getCursorProc = ChrViewerGetCursor;
	data->ted.tileHoverCallback = ChrViewerOnHoverChange;
	data->ted.renderCallback = ChrViewerRender;
	data->ted.suppressHighlightCallback = ChrViewerShouldSuppressHighlight;
	data->ted.isSelectionModeCallback = ChrViewerIsSelectionMode;
	data->ted.updateCursorCallback = ChrViewerUpdateCursorCallback;
	data->ted.getPopupMenuCallback = ChrViewerGetPopupMenu;

	data->hWndCharacterLabel = CreateStatic(hWnd, L" Character 0", 0, 0, 100, 22);
	data->hWndPaletteDropdown = CreateCombobox(hWnd, NULL, 0, 0, 0, 200, 100, 0);
	data->hWndWidthDropdown = CreateCombobox(hWnd, NULL, 0, 0, 0, 200, 100, 0);
	data->hWndWidthLabel = CreateStatic(hWnd, L" Width:", 0, 0, 100, 21);
	data->hWndExpand = CreateButton(hWnd, L"Resize", 0, 0, 100, 22, FALSE);
	data->hWnd8bpp = CreateCheckbox(hWnd, L"8bpp", 0, 0, 50, 22, FALSE);
	data->hWndUseAttribute = CreateCheckbox(hWnd, L"Use Attributes", 0, 0, 100, 22, FALSE);

	WCHAR bf[] = L"Palette 00";
	for (int i = 0; i < 16; i++) {
		wsprintfW(bf, L"Palette %02d", i);
		UiCbAddString(data->hWndPaletteDropdown, bf);
	}
	UiCbSetCurSel(data->hWndPaletteDropdown, 0);

	//read config data
	if (!g_configuration.ncgrViewerConfiguration.gridlines) {
		data->showBorders = 0;
		CheckMenuItem(GetMenu(data->editorMgr->hWnd), ID_VIEW_GRIDLINES, MF_UNCHECKED);
	}
	SetGUIFont(hWnd);
}

static void ChrViewerSetPreferredSize(NCGRVIEWERDATA *data) {
	float dpiScale = GetDpiScale();
	int controlHeight = (int) (dpiScale * 21.0f + 0.5f);
	int controlWidth = (int) (dpiScale * 100.0f + 0.5f);
	ChrViewerGetGraphicsSize(data, &data->frameData.contentWidth, &data->frameData.contentHeight);

	int width = data->frameData.contentWidth + GetSystemMetrics(SM_CXVSCROLL) + 4 + MARGIN_TOTAL_SIZE;
	int height = data->frameData.contentHeight + 3 * controlHeight + GetSystemMetrics(SM_CYHSCROLL) + 4 + MARGIN_TOTAL_SIZE;
	if (width < 255 + 4) width = 255 + 4; //min width for controls

	//get parent size
	RECT rcMdi;
	HWND hWndMdi = (HWND) GetWindowLongPtr(data->hWnd, GWL_HWNDPARENT);
	GetClientRect(hWndMdi, &rcMdi);

	int maxHeight = rcMdi.bottom * 9 / 10; // 90% of client height
	int maxWidth = rcMdi.right * 9 / 10;  // 90% of client height
	if (height >= maxHeight) height = maxHeight;
	if (width >= maxWidth) width = maxWidth;

	SetWindowSize(data->hWnd, width, height);
}

static void ChrViewerOnInitialize(NCGRVIEWERDATA *data, LPCWSTR path, NCGR *ncgr) {
	HWND hWnd = data->hWnd;
	float dpiScale = GetDpiScale();

	data->ncgr = ncgr;
	if (path != NULL) EditorSetFile(hWnd, path);
	data->ted.tilesX = data->ncgr->tilesX;
	data->ted.tilesY = data->ncgr->tilesY;
	SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

	ChrViewerSetPreferredSize(data);
	
	ChrViewerPopulateWidthField(data);
	if (data->ncgr->nBits == 8) SendMessage(data->hWnd8bpp, BM_SETCHECK, 1, 0);

	//guess a tile base for open NSCR (if any)
	StList nscrEditorList;
	StListCreateInline(&nscrEditorList, EDITOR_DATA *, NULL);
	EditorGetAllByType(data->editorMgr->hWnd, FILE_TYPE_SCREEN, &nscrEditorList);

	//for each editor
	for (size_t i = 0; i < nscrEditorList.length; i++) {
		NSCRVIEWERDATA *nscrViewerData = *(NSCRVIEWERDATA **) StListGetPtr(&nscrEditorList, i);
		NSCR *nscr = nscrViewerData->nscr;
		if (nscr->nHighestIndex >= data->ncgr->nTiles) {
			NscrViewerSetTileBase(nscrViewerData->hWnd, nscr->nHighestIndex + 1 - data->ncgr->nTiles);
		} else {
			NscrViewerSetTileBase(nscrViewerData->hWnd, 0);
		}
	}
	StListFree(&nscrEditorList);

	ShowWindow(hWnd, SW_SHOW);
	ChrViewerInvalidateAllDependents(data);
}

static LRESULT ChrViewerOnSize(NCGRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	HWND hWnd = data->hWnd;
	float dpiScale = GetDpiScale();

	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	int controlHeight = (int) (dpiScale * 21.0f + 0.5f);
	int controlWidth = (int) (dpiScale * 100.0f + 0.5f);
	int height = rcClient.bottom - rcClient.top;
	int viewHeight = height - 3 * controlHeight;

	MoveWindow(data->ted.hWndViewer, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, rcClient.right - MARGIN_TOTAL_SIZE, viewHeight - MARGIN_TOTAL_SIZE, FALSE);
	MoveWindow(data->hWndCharacterLabel, 0, viewHeight, rcClient.right, controlHeight, TRUE);
	MoveWindow(data->hWndPaletteDropdown, 0, viewHeight + controlHeight * 2, controlWidth * 3 / 2, controlHeight, TRUE);
	MoveWindow(data->hWndWidthDropdown, controlWidth / 2, viewHeight + controlHeight, controlWidth, controlHeight, TRUE);
	MoveWindow(data->hWndWidthLabel, 0, viewHeight + controlHeight, controlWidth / 2, controlHeight, FALSE);
	MoveWindow(data->hWndExpand, 5 + controlWidth * 3 / 2, viewHeight + controlHeight, controlWidth, controlHeight, TRUE);
	MoveWindow(data->hWnd8bpp, 5 + controlWidth * 3 / 2, viewHeight + controlHeight * 2, controlWidth / 2, controlHeight, TRUE);
	MoveWindow(data->hWndUseAttribute, 5 + controlWidth * 4 / 2, viewHeight + controlHeight * 2, controlWidth, controlHeight, TRUE);

	if (wParam == SIZE_RESTORED) InvalidateRect(hWnd, NULL, TRUE); //full update
	return DefMDIChildProc(hWnd, WM_SIZE, wParam, lParam);
}

static void ChrViewerOnDestroy(NCGRVIEWERDATA *data) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	nitroPaintStruct->hWndNcgrViewer = NULL;

	HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
	if (hWndNclrViewer != NULL) InvalidateRect(hWndNclrViewer, NULL, FALSE);
	ChrViewerInvalidateAllDependents(data);
	TedDestroy(&data->ted);
}

static int ChrViewerOnTimer(NCGRVIEWERDATA *data, int idTimer) {
	if (idTimer == 1) {
		data->verifyFrames--;
		if (!data->verifyFrames) {
			KillTimer(data->hWnd, idTimer);
		}
		InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
	}
	return 0;
}


static void ChrViewerOnCtlCommand(NCGRVIEWERDATA *data, HWND hWndControl, int notification) {
	if (notification == CBN_SELCHANGE && hWndControl == data->hWndPaletteDropdown) {
		int sel = UiCbGetCurSel(hWndControl);
		data->selectedPalette = sel;
		InvalidateRect(data->hWnd, NULL, FALSE);

		HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
		if (hWndNclrViewer != NULL) InvalidateRect(hWndNclrViewer, NULL, FALSE);
	} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndWidthDropdown) {
		WCHAR text[16];
		int selected = UiCbGetCurSel(hWndControl);
		SendMessage(hWndControl, CB_GETLBTEXT, (WPARAM) selected, (LPARAM) text);
		
		ChrViewerSetWidth(data, _wtol(text));
	} else if (notification == BN_CLICKED && hWndControl == data->hWndExpand) {
		HWND hWndMain = data->editorMgr->hWnd;
		HWND h = CreateWindow(L"ExpandNcgrClass", L"Resize Graphics", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
			CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
		DoModal(h);
		SendMessage(data->hWnd, NV_UPDATEPREVIEW, 0, 0);
	} else if (notification == BN_CLICKED && hWndControl == data->hWnd8bpp) {
		int state = GetCheckboxChecked(hWndControl);
		int depth = (state) ? 8 : 4;
		ChrViewerSetDepth(data, depth);
		SetFocus(data->hWnd);
	} else if (notification == BN_CLICKED && hWndControl == data->hWndUseAttribute) {
		data->useAttribute = GetCheckboxChecked(hWndControl);
		ChrViewerGraphicsUpdated(data);
		SetFocus(data->hWnd);
	}
}

static void ChrViewerImportDialog(NCGRVIEWERDATA *data, BOOL createPalette, int pasteX, int pasteY, COLOR32 *px, int width, int height) {
	HWND hWndMain = data->editorMgr->hWnd;
	HWND h = CreateWindow(L"CharImportDialog", L"Import Bitmap", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
	CHARIMPORTDATA *cidata = (CHARIMPORTDATA *) GetWindowLongPtr(h, 0);

	//populate import params
	cidata->px = px;
	cidata->width = width;
	cidata->height = height;
	if (createPalette) SendMessage(cidata->hWndOverwritePalette, BM_SETCHECK, BST_CHECKED, 0);
	if (data->ncgr->nBits == 4) SetEditNumber(cidata->hWndPaletteSize, 16);
	SetEditNumber(cidata->hWndMaxChars, data->ncgr->nTiles - (data->ted.contextHoverX + pasteY * pasteX));

	HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
	NCLR *nclr = NULL;
	NCGR *ncgr = data->ncgr;
	if (hWndNclrViewer != NULL) {
		nclr = (NCLR *) EditorGetObject(hWndNclrViewer);
	}
	cidata->nclr = nclr;
	cidata->ncgr = ncgr;

	//get selection in palette editor. If there is a valid selection of more than one color,
	//then take this as the palette range to import with respect to.
	if (hWndNclrViewer != NULL) {
		NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) EditorGetData(hWndNclrViewer);
		int selStart = nclrViewerData->selStart;
		int selEnd = nclrViewerData->selEnd;
		if (selStart > selEnd) {
			int tmp = selStart;
			selStart = selEnd;
			selEnd = tmp;
		}

		if (selStart != -1 && selEnd != -1 && selStart != selEnd) {
			selStart -= (data->selectedPalette << ncgr->nBits);
			selEnd -= (data->selectedPalette << ncgr->nBits);

			if (selStart >= 0 && selEnd >= 0 && selEnd < (1 << ncgr->nBits)) {
				SetEditNumber(cidata->hWndPaletteBase, selStart);
				SetEditNumber(cidata->hWndPaletteSize, selEnd - selStart + 1);
			}
		}
	}

	cidata->contextHoverX = pasteX;
	cidata->contextHoverY = pasteY;
	cidata->selectedPalette = data->selectedPalette;

	//do modal
	DoModal(h);
}

static void ChrViewerImportAttributesFromScreen(NCGRVIEWERDATA *data, NSCRVIEWERDATA *nscrViewerData) {
	NCGR *ncgr = data->ncgr;
	NSCR *nscr = nscrViewerData->nscr;

	int nTiles = nscr->tilesX * nscr->tilesY;
	for (int i = 0; i < nTiles; i++) {
		uint16_t d = nscr->data[i];
		int chrno = (d >>  0) & 0x03FF;
		int palno = (d >> 12) & 0x000F;
		chrno -= nscrViewerData->tileBase;

		ChrViewerSetAttribute(data, chrno % ncgr->tilesX, chrno / ncgr->tilesX, palno);
	}
}

static void ChrViewerImportAttributesFromCell(NCGRVIEWERDATA *data, NCERVIEWERDATA *ncerViewerData) {
	NCGR *ncgr = data->ncgr;
	NCER *ncer = ncerViewerData->ncer;

	int nCells = ncer->nCells;
	int mapping = ncer->mappingMode;
	for (int i = 0; i < nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		
		//for each OBJ...
		for (int j = 0; j < cell->nAttribs; j++) {
			NCER_CELL_INFO info;
			CellDecodeOamAttributes(&info, cell, j);

			int objPlt = info.palette;
			int objW = info.width / 8;
			int objH = info.height / 8;
			int nObjChar = objW * objH;

			//process w.r.t. mapping mode
			int chStart = NCGR_CHNAME(info.characterName, mapping, ncgr->nBits);
			int chStartX = chStart % ncgr->tilesX;
			int chStartY = chStart / ncgr->tilesX;
			if (NCGR_2D(mapping)) {
				//2D mapping, process graphically
				for (int y = 0; y < objH; y++) {
					for (int x = 0; x < objW; x++) {
						ChrViewerSetAttribute(data, chStartX + x, chStartY + y, objPlt);
					}
				}
			} else {
				//1D mapping, process linearly
				for (int k = 0; k < nObjChar; k++) {
					ChrViewerSetAttribute(data, (chStart + k) % ncgr->tilesX, (chStart + k) / ncgr->tilesX, objPlt);
				}
			}
		}
	}
}

static void ChrViewerImportAttributes(NCGRVIEWERDATA *data) {
	//if no attribute data, create it.
	if (data->ncgr->attr == NULL) {
		data->ncgr->attr = (unsigned char *) calloc(data->ncgr->tilesX * data->ncgr->tilesY, 1);
	}

	HWND hWndMain = data->editorMgr->hWnd;
	
	//get all BG screen, cell data editors
	StList editors;
	StListCreateInline(&editors, EDITOR_DATA *, NULL);
	EditorGetAllByType(hWndMain, FILE_TYPE_SCREEN, &editors);
	EditorGetAllByType(hWndMain, FILE_TYPE_CELL, &editors);

	for (size_t i = 0; i < editors.length; i++) {
		EDITOR_DATA *editor = *(EDITOR_DATA **) StListGetPtr(&editors, i);

		switch (editor->file->type) {
			case FILE_TYPE_SCREEN:
				ChrViewerImportAttributesFromScreen(data, (NSCRVIEWERDATA *) editor);
				break;
			case FILE_TYPE_CELL:
				ChrViewerImportAttributesFromCell(data, (NCERVIEWERDATA *) editor);
				break;
		}
	}

	StListFree(&editors);
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
}

static void ChrViewerOnMenuCommand(NCGRVIEWERDATA *data, int idMenu) {
	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
			InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
			break;
		case ID_NCGRMENU_IMPORTBITMAPHERE:
		{
			LPWSTR path = openFileDialog(data->hWnd, L"Select Bitmap", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
			if (!path) break;

			int width, height;
			COLOR32 *px = ImgRead(path, &width, &height);
			free(path);

			//get import point
			int pasteX = -1, pasteY = -1;
			if (TedHasSelection(&data->ted)) {
				int selW, selH;
				TedGetSelectionBounds(&data->ted, &pasteX, &pasteY, &selW, &selH);
			} else {
				pasteX = data->ted.contextHoverX;
				pasteY = data->ted.contextHoverY;
			}

			ChrViewerImportDialog(data, TRUE, pasteX, pasteY, px, width, height);

			//do not free px: owned by the convert process now
			break;
		}
		case ID_NCGRMENU_GENERATEATTRIBUTES:
			ChrViewerImportAttributes(data);
			break;
		case ID_FILE_SAVEAS:
			EditorSaveAs(data->hWnd);
			break;
		case ID_FILE_SAVE:
			EditorSave(data->hWnd);
			break;
		case ID_FILE_EXPORT:
		{
			LPWSTR location = saveFileDialog(data->hWnd, L"Save Bitmap", L"PNG Files (*.png)\0*.png\0All Files\0*.*\0", L"png");
			if (!location) break;

			HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);

			NCGR *ncgr = data->ncgr;
			NCLR *nclr = NULL;
			if (hWndNclrViewer != NULL) nclr = (NCLR *) EditorGetObject(hWndNclrViewer);

			ChrViewerExportBitmap(data, ncgr, nclr, data->selectedPalette, location);
			free(location);
			break;
		}
		case ID_NCGRMENU_COPY:
			ChrViewerCopy(data);
			break;
		case ID_NCGRMENU_PASTE:
			ChrViewerPaste(data, TRUE);
			ChrViewerGraphicsUpdated(data);
			break;
		case ID_NCGRMENU_CUT:
			ChrViewerCut(data);
			ChrViewerGraphicsUpdated(data);
			break;
		case ID_EDITMODE_SELECTION:
			ChrViewerSetMode(data, CHRVIEWER_MODE_SELECT);
			break;
		case ID_EDITMODE_PEN:
			ChrViewerSetMode(data, CHRVIEWER_MODE_PEN);
			break;
		case ID_EDITMODE_FILL:
			ChrViewerSetMode(data, CHRVIEWER_MODE_FILL);
			break;
		case ID_EDITMODE_EYEDROPPER:
			ChrViewerSetMode(data, CHRVIEWER_MODE_EYEDROP);
			break;
		case ID_EDITMODE_STAMP:
			ChrViewerSetMode(data, CHRVIEWER_MODE_STAMP);
			break;
		case ID_NCGRMENU_FLIPHORIZONTALLY:
			ChrViewerFlipSelection(data, TRUE, FALSE);
			break;
		case ID_NCGRMENU_FLIPVERTICALLY:
			ChrViewerFlipSelection(data, FALSE, TRUE);
			break;
	}
}

static void ChrViewerOnAccelerator(NCGRVIEWERDATA *data, int idAccel) {
	switch (idAccel) {
		case ID_ACCELERATOR_COPY:
			ChrViewerCopy(data);
			break;
		case ID_ACCELERATOR_PASTE:
			ChrViewerPaste(data, FALSE);
			ChrViewerGraphicsUpdated(data);
			break;
		case ID_ACCELERATOR_CUT:
			ChrViewerCut(data);
			ChrViewerGraphicsUpdated(data);
			break;
		case ID_ACCELERATOR_DESELECT:
			TedDeselect(&data->ted);
			break;
		case ID_ACCELERATOR_SELECT_ALL:
			TedSelectAll(&data->ted);
			break;
	}
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
	TedUpdateMargins(&data->ted);
	ChrViewerUpdateCharacterLabel(data);
}

static void ChrViewerOnCommand(NCGRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	if (lParam) {
		ChrViewerOnCtlCommand(data, (HWND) lParam, HIWORD(wParam));
	} else if (HIWORD(wParam) == 0) {
		ChrViewerOnMenuCommand(data, LOWORD(wParam));
	} else if (HIWORD(wParam) == 1) {
		ChrViewerOnAccelerator(data, LOWORD(wParam));
	}
}

static void ChrViewerOnMainPaint(NCGRVIEWERDATA *data) {
	HWND hWnd = data->hWnd;
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);

	TedMarginPaint(hWnd, (EDITOR_DATA *) data, &data->ted);
}

static void ChrViewerUpdateCursorCallback(HWND hWnd, int pxX, int pxY) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	int hit = data->ted.mouseDownHit;
	int hitType = hit & HIT_TYPE_MASK;

	int scrollX, scrollY;
	TedGetScroll(&data->ted, &scrollX, &scrollY);

	switch (data->mode) {
		case CHRVIEWER_MODE_PEN:
		{
			int pcol = ChrViewerGetSelectedColor(data);
			if (pcol != -1) {
				if (data->ted.lastMouseX != -1 && data->ted.lastMouseY != -1) {
					//connect last point
					int lastPxX = (data->ted.lastMouseX + scrollX) / data->scale;
					int lastPxY = (data->ted.lastMouseY + scrollY) / data->scale;
					ChrViewerConnectLine(data, lastPxX, lastPxY, pxX, pxY, pcol);
				} else {
					//draw single pixel
					ChrViewerPutPixel(data, pxX, pxY, pcol);
				}
				ChrViewerInvalidateAllDependents(data);
			}
			break;
		}
		case CHRVIEWER_MODE_FILL:
		case CHRVIEWER_MODE_EYEDROP:
		{
			//do nothing (only click event)
			break;
		}
		case CHRVIEWER_MODE_STAMP:
		{
			if (TedHasSelection(&data->ted)) {
				ChrViewerStampTile(data, data->ted.hoverX, data->ted.hoverY);
			}
			break;
		}
	}
}

static void ChrViewerMainOnMouseMove(NCGRVIEWERDATA *data, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TedMainOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
}

static HCURSOR ChrViewerGetCursor(HWND hWnd, int hit) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	HCURSOR hCursor = NULL;
	switch (data->mode) {
		case CHRVIEWER_MODE_SELECT: hCursor = LoadCursor(NULL, IDC_ARROW); break;
		case CHRVIEWER_MODE_PEN: hCursor = LoadCursor(NULL, MAKEINTRESOURCE(32631)); break; //pencil cursor
		case CHRVIEWER_MODE_FILL: hCursor = LoadCursor(NULL, IDC_CROSS); break;
		case CHRVIEWER_MODE_EYEDROP: hCursor = LoadCursor(NULL, IDC_UPARROW);  break;
		case CHRVIEWER_MODE_STAMP: hCursor = LoadCursor(NULL, IDC_HAND);  break;
		default: hCursor = LoadCursor(NULL, IDC_NO); break; //error
	}
	return hCursor;
}

static void ChrViewerOnHoverChange(HWND hWnd, int tileX, int tileY) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	ChrViewerUpdateCharacterLabel(data);

	StList scrEditors;
	StListCreateInline(&scrEditors, NSCRVIEWERDATA *, NULL);
	EditorGetAllByType(data->editorMgr->hWnd, FILE_TYPE_SCREEN, &scrEditors);

	//update screen viewers
	for (size_t i = 0; i < scrEditors.length; i++) {
		//invalidate viewer portion
		NSCRVIEWERDATA *nscrViewerData = *(NSCRVIEWERDATA **) StListGetPtr(&scrEditors, i);
		InvalidateRect(nscrViewerData->ted.hWndViewer, NULL, FALSE);
	}
	StListFree(&scrEditors);
}

static BOOL ChrViewerSetCursor(NCGRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	return TedSetCursor((EDITOR_DATA *) data, &data->ted, wParam, lParam);
}

static void ChrViewerOnKeyDown(NCGRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	//pass message
	TedViewerOnKeyDown((EDITOR_DATA *) data, &data->ted, wParam, lParam);

	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	//extra handling
	switch (wParam) {
		case VK_RETURN:
			SendMessage(data->hWnd, WM_COMMAND, ID_NCGRMENU_IMPORTBITMAPHERE, 0);
			break;
		case VK_DELETE:
		{
			if (TedHasSelection(&data->ted)) {
				unsigned char fill[64] = { 0 };
				ChrViewerFill(data, selX, selY, selW, selH, fill);
				ChrViewerFillAttr(data, selX, selY, selW, selH, 0);
			}
			TedDeselect(&data->ted);
			TedUpdateMargins(&data->ted);
			ChrViewerGraphicsUpdated(data);
			ChrViewerUpdateCharacterLabel(data);
			break;
		}
		case ' ':
		{
			if (TedHasSelection(&data->ted)) {
				//create 'X' pattern
				unsigned char fill[64] = { 0 };
				for (int i = 1; i < 7; i++) {
					fill[i + i * 8] = 0xF;
					fill[(7 - i) + i * 8] = 0xF;
				}
				ChrViewerFill(data, selX, selY, selW, selH, fill);
				ChrViewerGraphicsUpdated(data);
			}
			break;
		}
		case 'S': // select
			ChrViewerSetMode(data, CHRVIEWER_MODE_SELECT);
			break;
		case 'P': // pen
			ChrViewerSetMode(data, CHRVIEWER_MODE_PEN);
			break;
		case 'F': // fill
		case 'B': // bucket (synonym)
			ChrViewerSetMode(data, CHRVIEWER_MODE_FILL);
			break;
		case 'E': // eyedropper
			ChrViewerSetMode(data, CHRVIEWER_MODE_EYEDROP);
			break;
		case 'T': //stamp
			ChrViewerSetMode(data, CHRVIEWER_MODE_STAMP);
			break;
		case 'H':
			ChrViewerFlipSelection(data, TRUE, FALSE);
			break;
		case 'V':
			ChrViewerFlipSelection(data, FALSE, TRUE);
			break;
	}
}

static LRESULT WINAPI ChrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
			ChrViewerOnCreate(data);
			break;
		case NV_INITIALIZE:
			ChrViewerOnInitialize(data, (LPCWSTR) wParam, (NCGR *) lParam);
			break;
		case NV_UPDATEPREVIEW:
			PreviewLoadBgCharacter(data->ncgr);
			PreviewLoadObjCharacter(data->ncgr);
			break;
		case NV_ZOOMUPDATED:
			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			TedUpdateMargins(&data->ted);
			break;
		case WM_COMMAND:
			ChrViewerOnCommand(data, wParam, lParam);
			break;
		case WM_KEYDOWN:
			ChrViewerOnKeyDown(data, wParam, lParam);
			break;
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			ChrViewerMainOnMouseMove(data, hWnd, msg, wParam, lParam);
			break;
		case WM_SETCURSOR:
			return ChrViewerSetCursor(data, wParam, lParam);
		case WM_LBUTTONDOWN:
			TedOnLButtonDown((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_LBUTTONUP:
			TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_PAINT:
			ChrViewerOnMainPaint(data);
			break;
		case WM_ERASEBKGND:
			return TedMainOnEraseBkgnd((EDITOR_DATA *) data, &data->ted, wParam, lParam);
		case WM_TIMER:
			return ChrViewerOnTimer(data, wParam);
		case WM_DESTROY:
			ChrViewerOnDestroy(data);
			break;
		case WM_SIZE:
			return ChrViewerOnSize(data, wParam, lParam);
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}




static int ChrViewerShouldSuppressHighlight(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);
	return !(data->mode == CHRVIEWER_MODE_SELECT || data->mode == CHRVIEWER_MODE_STAMP);
}

static void ChrViewerRender(HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	int hlStart = data->verifyStart;
	int hlEnd = data->verifyEnd;
	int hlMode = data->verifySelMode;
	if ((data->verifyFrames & 1) == 0) hlStart = hlEnd = -1;

	NCGR *ncgr = data->ncgr;
	NCLR *nclr = NULL;

	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	if (nitroPaintStruct->hWndNclrViewer != NULL) {
		nclr = (NCLR *) EditorGetObject(nitroPaintStruct->hWndNclrViewer);
	}

	int tilesX = ncgr->tilesX, tilesY = ncgr->tilesY;

	for (int y = 0; y < renderHeight; y++) {
		for (int x = 0; x < renderWidth; x++) {
			int srcX = (x + scrollX) / data->scale;
			int srcY = (y + scrollY) / data->scale;

			int srcTileX = srcX / 8;
			int srcTileY = srcY / 8;
			unsigned char *chr = ncgr->tiles[srcTileX + srcTileY * tilesX];
			int plt = ChrViewerGetCharPalette(data, srcTileX, srcTileY);
			int rawIdx = chr[(srcX % 8) + (srcY % 8) * 8];
			int idx = rawIdx + (plt << ncgr->nBits);

			COLOR32 col = 0;
			if (nclr != NULL && idx < nclr->nColors) {
				//color from palette
				col = ColorConvertFromDS(nclr->colors[idx]);
			}
			if (rawIdx) col |= 0xFF000000;
			if (data->transparent) {
				//alpha blend
				col = TedAlphaBlendColor(col, x, y);
			}

			//process verify color indication
			if (hlStart != -1 && hlEnd != -1) {
				if (PalViewerIndexInRange(idx, hlStart, hlEnd, hlMode == PALVIEWER_SELMODE_2D)) {
					int lightness = (col & 0xFF) + ((col >> 8) & 0xFF) + ((col >> 16) & 0xFF);
					if (lightness < 383) col = 0xFFFFFFFF;
					else col = 0xFF000000;
				}
			}

			data->ted.fb.px[x + y * data->ted.fb.width] = REVERSE(col);
		}
	}
}

static void ChrViewerOnLButtonDown(NCGRVIEWERDATA *data) {
	TedViewerOnLButtonDown((EDITOR_DATA *) data, &data->ted);

	//hit test
	int hit =  data->ted.mouseDownHit;
	int hitType = hit & HIT_TYPE_MASK;

	//get scroll info
	int scrollX, scrollY;
	TedGetScroll(&data->ted, &scrollX, &scrollY);

	//get smouse pixel coordinate
	int pxX = -1, pxY = -1;
	if (hitType == HIT_CONTENT) {
		pxX = (data->ted.mouseX + scrollX) / data->scale;
		pxY = (data->ted.mouseY + scrollY) / data->scale;
	}

	switch (data->mode) {
		case CHRVIEWER_MODE_PEN:
		{
			if (hit != HIT_CONTENT) break;
			int pcol = ChrViewerGetSelectedColor(data);
			if (pcol == -1) {
				//must have a selected color to paint with.
				TedReleaseCursor((EDITOR_DATA *) data, &data->ted); //necessary to prevent UI issues with mouse capture
				MessageBox(data->hWnd, L"Must have a color selected in the palette window to draw with.", L"No color selected", MB_ICONERROR);
			} else {
				//put pixel at coordinates
				ChrViewerPutPixel(data, pxX, pxY, pcol);
				ChrViewerGraphicsUpdated(data);
			}
			break;
		}
		case CHRVIEWER_MODE_FILL:
		{
			if (hit != HIT_CONTENT) break;
			int pcol = ChrViewerGetSelectedColor(data);
			if (pcol == -1) {
				//must have a selected color to fill with.
				TedReleaseCursor((EDITOR_DATA *) data, &data->ted); //necessary to prevent UI issues with mouse capture
				MessageBox(data->hWnd, L"Must have a color selected in the palette window to draw with.", L"No color selected", MB_ICONERROR);
			} else {
				ChrViewerFloodFill(data, pxX, pxY, pcol);
				ChrViewerGraphicsUpdated(data);
			}
			break;
		}
		case CHRVIEWER_MODE_EYEDROP:
		{
			if (hit != HIT_CONTENT) break;
			int col = ChrViewerGetPixel(data, pxX, pxY);
			if (!ChrViewerSetSelectedColor(data, col)) {
				MessageBox(data->hWnd, L"Must have a color palette open to use this tool.", L"No palette", MB_ICONERROR);
			} else {
				//release mouse capture
				TedReleaseCursor((EDITOR_DATA *) data, &data->ted);

				//switch to previous tool
				ChrViewerSetMode(data, data->lastMode);
			}
			break;
		}
		case CHRVIEWER_MODE_STAMP:
		{
			if (hit != HIT_CONTENT) break;
			if (!TedHasSelection(&data->ted)) {
				TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
				MessageBox(data->hWnd, L"Must have a selection to use this tool.", L"No selection", MB_ICONERROR);
			} else {
				ChrViewerStampTile(data, data->ted.hoverX, data->ted.hoverY);
			}
			break;
		}
	}

	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
	TedUpdateMargins(&data->ted);
}

static void ChrViewerOnLButtonUp(NCGRVIEWERDATA *data) {
	TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
}

static HMENU ChrViewerGetPopupMenu(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);
	HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 1);

	//set current tool in popup menu
	int id = 0;
	switch (data->mode) {
		case CHRVIEWER_MODE_SELECT: id = ID_EDITMODE_SELECTION; break;
		case CHRVIEWER_MODE_PEN: id = ID_EDITMODE_PEN; break;
		case CHRVIEWER_MODE_FILL: id = ID_EDITMODE_FILL; break;
		case CHRVIEWER_MODE_EYEDROP: id = ID_EDITMODE_EYEDROPPER; break;
		case CHRVIEWER_MODE_STAMP: id = ID_EDITMODE_STAMP; break;
	}

	int editModeIds[] = { ID_EDITMODE_SELECTION, ID_EDITMODE_PEN, ID_EDITMODE_FILL, ID_EDITMODE_EYEDROPPER, ID_EDITMODE_STAMP };
	for (int i = 0; i < sizeof(editModeIds) / sizeof(editModeIds[0]); i++) {
		if (editModeIds[i] == id) CheckMenuItem(hPopup, editModeIds[i], MF_CHECKED);
		else CheckMenuItem(hPopup, editModeIds[i], MF_UNCHECKED);
	}

	//enable/disable relevant menu items
	if (TedHasSelection(&data->ted)) {
		EnableMenuItem(hPopup, ID_NCGRMENU_CUT, MF_ENABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_COPY, MF_ENABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_DESELECT, MF_ENABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_FLIPHORIZONTALLY, MF_ENABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_FLIPVERTICALLY, MF_ENABLED);
	} else {
		EnableMenuItem(hPopup, ID_NCGRMENU_CUT, MF_DISABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_COPY, MF_DISABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_DESELECT, MF_DISABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_FLIPHORIZONTALLY, MF_DISABLED);
		EnableMenuItem(hPopup, ID_NCGRMENU_FLIPVERTICALLY, MF_DISABLED);
	}
	return hPopup;
}

static void ChrViewerOnRButtonDown(NCGRVIEWERDATA *data) {
	//mark hovered tile
	TedOnRButtonDown(&data->ted);

	//context menu
	TedTrackPopup((EDITOR_DATA *) data, &data->ted);
}

static void ChrViewerUpdateContentSize(NCGRVIEWERDATA *data) {
	int contentWidth, contentHeight;
	ChrViewerGetGraphicsSize(data, &contentWidth, &contentHeight);

	SCROLLINFO info;
	info.cbSize = sizeof(info);
	info.nMin = 0;
	info.nMax = contentWidth;
	info.fMask = SIF_RANGE;
	SetScrollInfo(data->ted.hWndViewer, SB_HORZ, &info, TRUE);

	info.nMax = contentHeight;
	SetScrollInfo(data->ted.hWndViewer, SB_VERT, &info, TRUE);
	RECT rcClient;
	GetClientRect(data->ted.hWndViewer, &rcClient);
	SendMessage(data->ted.hWndViewer, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
}

static LRESULT WINAPI ChrViewerPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndNcgrViewer = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer);
	int contentWidth = 0, contentHeight = 0;
	if (data != NULL) {
		ChrViewerGetGraphicsSize(data, &contentWidth, &contentHeight);
	}

	//little hack for code reuse >:)
	FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
	if (frameData == NULL) {
		frameData = calloc(1, sizeof(FRAMEDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) frameData);
	}
	frameData->contentWidth = contentWidth;
	frameData->contentHeight = contentHeight;

	switch (msg) {
		case WM_CREATE:
			SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_HSCROLL | WS_VSCROLL);
			break;
		case WM_PAINT:
			TedOnViewerPaint((EDITOR_DATA *) data, &data->ted);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			TedViewerOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
			break;
		case WM_LBUTTONDOWN:
			ChrViewerOnLButtonDown(data);
			break;
		case WM_LBUTTONUP:
			ChrViewerOnLButtonUp(data);
			break;
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			TedUpdateMargins(&data->ted);
			TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
			return DefChildProc(hWnd, msg, wParam, lParam);
		case WM_RBUTTONDOWN:
			ChrViewerOnRButtonDown(data);
			break;
		case NV_RECALCULATE:
			ChrViewerUpdateContentSize(data);
			TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_SIZE:
		{
			if (data->ncgr != NULL && ObjIsValid(&data->ncgr->header)) {
				UpdateScrollbarVisibility(hWnd);

				SCROLLINFO info;
				info.cbSize = sizeof(info);
				info.nMin = 0;
				info.nMax = contentWidth;
				info.fMask = SIF_RANGE;
				SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

				info.nMax = contentHeight;
				SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			}
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
		case WM_DESTROY:
			free(frameData);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void ChrViewerGraphicsSizeUpdated(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	ChrViewerPopulateWidthField(data);
	data->ted.tilesX = data->ncgr->tilesX;
	data->ted.tilesY = data->ncgr->tilesY;
	SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);

	//invalidate viewers
	ChrViewerGraphicsUpdated(data);
	TedUpdateMargins(&data->ted);
}

static LRESULT CALLBACK NcgrExpandProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case NV_INITIALIZE:
		{
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) lParam);
			data = (NCGRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

			CreateStatic(hWnd, L"Width:", 10, 10, 75, 22);
			CreateStatic(hWnd, L"Height:", 10, 37, 75, 22);
			data->hWndExpandColsInput = CreateEdit(hWnd, L"", 90, 10, 75, 22, TRUE);
			data->hWndExpandRowsInput = CreateEdit(hWnd, L"", 90, 37, 75, 22, TRUE);
			data->hWndExpandButton = CreateButton(hWnd, L"Set", 90, 64, 75, 22, TRUE);
			SetEditNumber(data->hWndExpandRowsInput, data->ncgr->tilesY);
			SetEditNumber(data->hWndExpandColsInput, data->ncgr->tilesX);
			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 175, 96);
			SetFocus(data->hWndExpandRowsInput);
			break;
		}
		case WM_COMMAND:
		{
			int notification = LOWORD(wParam);
			HWND hWndControl = (HWND) lParam;
			if (notification == BN_CLICKED && hWndControl == data->hWndExpandButton) {
				int nRows = GetEditNumber(data->hWndExpandRowsInput);
				int nCols = GetEditNumber(data->hWndExpandColsInput);
				ChrResize(data->ncgr, nCols, nRows);
				ChrViewerGraphicsSizeUpdated(data->hWnd);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

typedef struct ChrImportData_ {
	BOOL createPalette;
	BOOL dither;
	BOOL import1D;
	BOOL charCompression;
	float diffuse;
	int paletteBase;
	int paletteSize;
	int nMaxChars;
	int originX;
	int originY;
	int paletteNumber;
	BOOL objMode;
	int objCharsX;
	int objCharsY;
	int objMapGranularity;
	int balance;
	int colorBalance;
	int enhanceColors;
	NCLR *nclr;
	NCGR *ncgr;
	NCGRVIEWERDATA *ncgrViewerData;
	COLOR32 *px;
	int width;
	int height;
} ChrImportData;

static int ChrImportCallback(void *cbdata) {
	ChrImportData *cim = (ChrImportData *) cbdata;
	NCGRVIEWERDATA *data = cim->ncgrViewerData;
	HWND hWndMain = data->editorMgr->hWnd;
	BOOL import1D = cim->import1D;

	EditorInvalidateAllByType(hWndMain, FILE_TYPE_PALETTE);
	EditorInvalidateAllByType(hWndMain, FILE_TYPE_CHAR);
	EditorInvalidateAllByType(hWndMain, FILE_TYPE_SCREEN);
	EditorInvalidateAllByType(hWndMain, FILE_TYPE_CELL);

	EnableWindow(hWndMain, TRUE);
	SetForegroundWindow(hWndMain);

	//select the import region
	if (import1D) {
		TedDeselect(&data->ted);
	} else {
		TedSelect(&data->ted, cim->originX, cim->originY, cim->width / 8, cim->height / 8);
	}

	//set attribute of import region
	if (!import1D) {
		for (int y = 0; y < cim->height / 8; y++) {
			for (int x = 0; x < cim->width / 8; x++) {
				ChrViewerSetAttribute(data, x + cim->originX, y + cim->originY, data->selectedPalette);
			}
		}
	} else {
		int origin = cim->originX + cim->originY * data->ncgr->tilesX;
		int nChars = (cim->width / 8) * (cim->height / 8);
		if (cim->charCompression) nChars = cim->nMaxChars;
		for (int i = 0; i < nChars; i++) {
			ChrViewerSetAttribute(data, (i + origin) % data->ncgr->tilesX, (i + origin) / data->ncgr->tilesX, data->selectedPalette);
		}
	}

	free(cbdata);

	return 0;
}

static void charImport(
	NCLR    *nclr,
	NCGR    *ncgr,
	COLOR32 *pixels,
	int      width,
	int      height,
	BOOL     createPalette,
	int      paletteNumber,
	int      paletteSize,
	int      paletteBase, 
	BOOL     dither,
	float    diffuse,
	BOOL     objMode,
	int      objCharsX,
	int      objCharsY,
	int      objMapGranularity,
	BOOL     import1D,
	BOOL     charCompression,
	int      nMaxChars,
	int      originX,
	int      originY, 
	int      balance,
	int      colorBalance,
	int      enhanceColors,
	int     *progress
) {
	int maxPaletteSize = 1 << ncgr->nBits;
	int bReleaseImage = FALSE; // do not release the image buffer
	
	//for OBJ mode import, we'll swizzle the input bitmap.
	if (objMode) {
		bReleaseImage = TRUE;    // we will replace the image buffer
		import1D = TRUE;         // import in 1D orientation
		charCompression = FALSE; // do not character compress

		//derive the image dimensions. (round up)
		int objPxX = objCharsX * 8;
		int objPxY = objCharsY * 8;
		int nObjX = (width + objPxX - 1) / objPxX;
		int nObjY = (height + objPxY - 1) / objPxY;

		//account for the mapping granularity in the temp width.
		int nCharsObj = objCharsX * objCharsY;
		if (nCharsObj < objMapGranularity) nCharsObj = objMapGranularity;

		int tmpWidth = nCharsObj * 8;
		int tmpHeight = nObjX * nObjY * 8;
		COLOR32 *tmpbuf = (COLOR32 *) calloc(tmpWidth * tmpHeight, sizeof(COLOR32));

		//copy bits
		for (int objY = 0; objY < nObjY; objY++) {
			for (int objX = 0; objX < nObjX; objX++) {
				int iObj = objX + objY * nObjX; // index of current OBJ
				int yObj = iObj * 8;            // Y-coordinate in temp buffer of current OBJ

				//in OBJ
				for (int x_ = 0; x_ < objPxX; x_++) {
					for (int y_ = 0; y_ < objPxY; y_++) {

						//source bitmap coordinates
						int x = objX * objPxX + x_;
						int y = objY * objPxY + y_;
						COLOR32 srcCol = 0;
						if (x < width && y < height) srcCol = pixels[y * width + x];

						//character index
						int charIndex = x_ / 8 + (y_ / 8) * objCharsX;
						int inCharX = x_ % 8;
						int inCharY = y_ % 8;
						
						tmpbuf[(yObj + inCharY) * tmpWidth + (8 * charIndex + inCharX)] = srcCol;
					}
				}

			}
		}

		//replace buffer
		width = tmpWidth;
		height = tmpHeight;
		pixels = tmpbuf;
	}

	//if we start at base 0, increment by 1. We'll put a placeholder color in slot 0.
	if (paletteBase == 0) {
		paletteBase = 1;
		paletteSize--;
		if (createPalette) nclr->colors[paletteNumber << ncgr->nBits] = ColorConvertToDS(0xFF00FF);
	}

	int firstColorIndex = (paletteNumber << ncgr->nBits) + paletteBase;
	if (paletteSize > maxPaletteSize) paletteSize = maxPaletteSize;
	if (firstColorIndex + paletteSize >= nclr->nColors) {
		paletteSize = nclr->nColors - firstColorIndex;
	}

	COLOR *nitroPalette = nclr->colors + firstColorIndex;
	COLOR32 *palette = (COLOR32 *) calloc(paletteSize + 1, sizeof(COLOR32));

	//if we use an existing palette, decode the palette values.
	//if we do not use an existing palette, generate one.
	if (!createPalette) {
		//decode the palette
		for (int i = 0; i < paletteSize; i++) {
			COLOR32 col = ColorConvertFromDS(nitroPalette[i]) | 0xFF000000;
			palette[i + 1] = col;
		}
	} else {
		//create a palette, then encode them to the color palette
		RxCreatePaletteEx(pixels, width, height, palette + 1, paletteSize, balance, colorBalance, enhanceColors, RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE, NULL);
		for (int i = 0; i < paletteSize; i++) {
			nitroPalette[i] = ColorConvertToDS(palette[i + 1]);
		}
	}

	//index image with given parameters.
	int *idxs = (int *) calloc(width * height, sizeof(int));
	if (!dither) diffuse = 0.0f;
	RxReduceImageEx(pixels, idxs, width, height, palette, paletteSize + 1, RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_PRESERVE_ALPHA | RX_FLAG_NO_ALPHA_DITHER,
		diffuse, balance, colorBalance, enhanceColors);

	//now, write out indices. 
	int originOffset = originX + originY * ncgr->tilesX;
	//determine how many tiles the bitmap needs
	int tilesX = width >> 3;
	int tilesY = height >> 3;

	//perform the write. 1D or 2D?
	if (!import1D) {
		//clip the bitmap so it doesn't go over the edges.
		if (tilesX + originX > ncgr->tilesX) tilesX = ncgr->tilesX - originX;
		if (tilesY + originY > ncgr->tilesY) tilesY = ncgr->tilesY - originY;

		//write out each tile
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				int offset = (y + originY) * ncgr->tilesX + x + originX;
				unsigned char *tile = ncgr->tiles[offset];

				//write out this tile using the palette. Diffuse any error accordingly.
				for (unsigned int i = 0; i < 64; i++) {
					unsigned int offsetX = i % 8;
					unsigned int offsetY = i / 8;
					unsigned int poffset = (x * 8 + offsetX) + (y * 8 + offsetY) * width;
					
					unsigned int idx = idxs[poffset];
					if (idx > 0) idx += paletteBase - !!paletteBase;
					tile[i] = idx;
				}
			}
		}
	} else {
		//1D import, start at index and continue linearly.
		int *tiles = (int *) calloc(tilesX * tilesY, 64 * sizeof(int));
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				int imgX = x * 8, imgY = y * 8;
				int tileIndex = x + y * tilesX;
				int srcIndex = imgX + imgY * width;
				int *src = idxs + srcIndex;
				for (int i = 0; i < 8; i++) {
					memcpy(tiles + 64 * tileIndex + 8 * i, src + i * width, 8 * sizeof(int));
				}
			}
		}

		//character compression
		int nChars = tilesX * tilesY;
		if (charCompression) {
			//create dummy whole palette that the character compression functions expect
			COLOR32 dummyFull[256] = { 0 };
			memcpy(dummyFull + paletteBase, palette + 1, paletteSize * sizeof(COLOR32));

			BgTile *bgTiles = (BgTile *) calloc(nChars, sizeof(BgTile));

			//split image into 8x8 tiles.
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					int srcOffset = (x * 8) + (y * 8) * width;
					BgTile *t = &bgTiles[x + y * tilesX];

					int index = x + y * tilesX;
					int *srcBlock = tiles + index * 64;
					for (int i = 0; i < 8 * 8; i++) {
						int idx = srcBlock[i];
						if (idx == 0) t->px[i] = 0;   // index=0 -> transparent
						else t->px[i] = palette[idx]; // lookup in color palette
					}
				}
			}

			int nTiles = nChars;
			int allowFlip = 1;
			BgSetupTiles(bgTiles, nChars, ncgr->nBits, dummyFull, paletteSize, 1, 0, paletteBase, 0, 0.0f, balance, colorBalance, enhanceColors);
			nChars = BgPerformCharacterCompression(bgTiles, nChars, ncgr->nBits, nMaxChars, allowFlip, dummyFull, paletteSize, 1, 0, paletteBase, 
				balance, colorBalance, progress);

			//read back result
			int outIndex = 0;
			for (int i = 0; i < nTiles; i++) {
				BgTile *t = &bgTiles[i];
				if (t->masterTile != i) continue;

				int *dest = tiles + outIndex * 64;
				for (int j = 0; j < 64; j++) {
					dest[j] = t->indices[j];
				}

				outIndex++;
			}
			free(bgTiles);
		}

		//break into tiles and write
		int destBaseIndex = originX + originY * ncgr->tilesX;
		int nWriteChars = min(nChars, ncgr->nTiles - destBaseIndex);
		for (int i = 0; i < nWriteChars; i++) {
			unsigned char *tile = ncgr->tiles[i + destBaseIndex];
			int *srcTile = tiles + i * 64;

			for (int j = 0; j < 64; j++) {
				unsigned int idx = srcTile[j];
				if (idx > 0) idx += paletteBase - !!paletteBase;
				tile[j] = idx;
			}
		}
		free(tiles);
	}
	free(idxs);

	free(palette);

	//if needed, free the image buffer.
	if (bReleaseImage) free(pixels);
}

static DWORD WINAPI ChrImportInternal(LPVOID lpParameter) {
	PROGRESSDATA *progress = (PROGRESSDATA *) lpParameter;
	ChrImportData *cim = (ChrImportData *) progress->data;
	progress->progress1Max = 100;
	progress->progress1 = 100;
	progress->progress2Max = 1000;
	charImport(
		cim->nclr,
		cim->ncgr,
		cim->px,
		cim->width,
		cim->height,
		cim->createPalette,
		cim->paletteNumber,
		cim->paletteSize,
		cim->paletteBase, 	   
		cim->dither,
		cim->diffuse,
		cim->objMode,
		cim->objCharsX,
		cim->objCharsY,
		cim->objMapGranularity,
		cim->import1D,
		cim->charCompression,
		cim->nMaxChars,
		cim->originX,
		cim->originY, 
		cim->balance,
		cim->colorBalance,
		cim->enhanceColors,
		&progress->progress2
	);
	free(cim->px);
	progress->waitOn = 1;
	return 0;
}

static void ChrImportThreaded(PROGRESSDATA *progress) {
	ChrImportData *cim = (ChrImportData *) progress->data;

	CreateThread(NULL, 0, ChrImportInternal, progress, 0, NULL);
}

static LRESULT CALLBACK CharImportProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CHARIMPORTDATA *data = (CHARIMPORTDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (CHARIMPORTDATA *) calloc(1, sizeof(CHARIMPORTDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			int boxWidth = 100 + 100 + 10 + 10 + 10; //box width
			int boxHeight = 3 * 27 - 5 + 10 + 10 + 10; //first row height
			int boxHeight2 = 3 * 27 - 5 + 10 + 10 + 10; //second row height
			int boxHeight3 = 3 * 27 - 5 + 10 + 10 + 10; //third row height
			int width = 30 + 2 * boxWidth; //window width
			int height = 10 + boxHeight + 10 + boxHeight2 + 10 + boxHeight3 + 10 + 22 + 10; //window height

			int leftX = 10 + 10; //left box X
			int rightX = 10 + boxWidth + 10 + 10; //right box X
			int topY = 10 + 10 + 8; //top box Y
			int middleY = 10 + boxHeight + 10 + 10 + 8; //middle box Y
			int bottomY = 10 + boxHeight + 10 + boxHeight2 + 10 + 10 + 8; //bottom box Y

			//Palette
			data->hWndOverwritePalette = CreateCheckbox(hWnd, L"Write Palette", leftX, topY, 150, 22, FALSE);
			CreateStatic(hWnd, L"Palette Base:", leftX, topY + 27, 75, 22);
			data->hWndPaletteBase = CreateEdit(hWnd, L"0", leftX + 85, topY + 27, 100, 22, TRUE);
			CreateStatic(hWnd, L"Palette Size:", leftX, topY + 27 * 2, 75, 22);
			data->hWndPaletteSize = CreateEdit(hWnd, L"256", leftX + 85, topY + 27 * 2, 100, 22, TRUE);

			//Graphics
			data->hWndDither = CreateCheckbox(hWnd, L"Dither", rightX, topY, 150, 22, FALSE);
			CreateStatic(hWnd, L"Diffuse:", rightX, topY + 27, 75, 22);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", rightX + 85, topY + 27, 100, 22, TRUE);

			//Dimension
			LPCWSTR objSizes[] = { L"8x8", L"8x16", L"8x32", L"16x8", L"16x16", L"16x32", L"32x8", L"32x16", L"32x32", L"32x64", L"64x32", L"64x64" };
			LPCWSTR mappings[] = { L"1D 32K", L"1D 64K", L"1D 128K", L"1D 256K" };
			data->hWndObjMode = CreateCheckbox(hWnd, L"OBJ Mode", leftX, middleY, 75, 22, FALSE);
			data->hWnd1D = CreateCheckbox(hWnd, L"1D Import", leftX + 85, middleY, 150, 22, FALSE);
			data->hWndCompression = CreateCheckbox(hWnd, L"Compress Character", leftX + 85, middleY + 27, 150, 22, FALSE);
			CreateStatic(hWnd, L"Max Chars:", leftX + 85, middleY + 27 * 2, 75, 22);
			data->hWndMaxChars = CreateEdit(hWnd, L"1024", leftX + 170, middleY + 27 * 2, 100, 22, TRUE);
			CreateStatic(hWnd, L"OBJ Size:", leftX + 280, middleY, 75, 22);
			data->hWndObjSize = CreateCombobox(hWnd, objSizes, 12, leftX + 355, middleY, 75, 22, 0);
			CreateStatic(hWnd, L"Mapping:", leftX + 280, middleY + 27, 75, 22);
			data->hWndMappingGranularity = CreateCombobox(hWnd, mappings, 4, leftX + 355, middleY + 27, 75, 22, 0);

			//Balance
			NpCreateBalanceInput(&data->balance, hWnd, 10, 10 + boxHeight + 10 + boxHeight2 + 10, 10 + 2 * boxWidth);

			data->hWndImport = CreateButton(hWnd, L"Import", width / 2 - 100, height - 32, 200, 22, TRUE);

			CreateGroupbox(hWnd, L"Palette", 10, 10, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Graphics", 10 + boxWidth + 10, 10, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Dimension", 10, 10 + boxHeight + 10, boxWidth * 2 + 10, boxHeight2);

			SetGUIFont(hWnd);
			EnableWindow(data->hWndDiffuse, FALSE);
			EnableWindow(data->hWndCompression, FALSE);
			EnableWindow(data->hWndMaxChars, FALSE);
			EnableWindow(data->hWndObjSize, FALSE);
			SetWindowSize(hWnd, width, height);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				if (hWndControl == data->hWndDither) {
					//toggle dither
					EnableWindow(data->hWndDiffuse, GetCheckboxChecked(hWndControl));
				} else if (hWndControl == data->hWnd1D) {
					//toggle 1D mode
					int ccState = GetCheckboxChecked(data->hWndCompression);
					if (GetCheckboxChecked(hWndControl)) {
						EnableWindow(data->hWndCompression, TRUE);
						if (ccState) EnableWindow(data->hWndMaxChars, TRUE);
					} else {
						EnableWindow(data->hWndCompression, FALSE);
						EnableWindow(data->hWndMaxChars, FALSE);
					}
				} else if (hWndControl == data->hWndCompression) {
					//toggle character compression
					EnableWindow(data->hWndMaxChars, GetCheckboxChecked(hWndControl));
				} else if (hWndControl == data->hWndObjMode) {
					//OBJ mode toggle
					int state = GetCheckboxChecked(hWndControl);
					if (state) {
						//OBJ mode enable
						EnableWindow(data->hWndCompression, FALSE);
						EnableWindow(data->hWnd1D, FALSE);
						EnableWindow(data->hWndMaxChars, FALSE);
						EnableWindow(data->hWndObjSize, TRUE);
					} else {
						int ena1D = GetCheckboxChecked(data->hWnd1D);
						int enaComp = GetCheckboxChecked(data->hWndCompression);
						EnableWindow(data->hWnd1D, TRUE);
						EnableWindow(data->hWndCompression, ena1D);
						EnableWindow(data->hWndMaxChars, ena1D && enaComp);
						EnableWindow(data->hWndObjSize, FALSE);
					}
				} else if (hWndControl == data->hWndImport) {
					RxBalanceSetting balance;
					BOOL createPalette = GetCheckboxChecked(data->hWndOverwritePalette);
					BOOL dither = GetCheckboxChecked(data->hWndDither);
					BOOL objMode = GetCheckboxChecked(data->hWndObjMode);
					BOOL import1D = GetCheckboxChecked(data->hWnd1D);
					BOOL charCompression = GetCheckboxChecked(data->hWndCompression);
					float diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;
					if (!dither) diffuse = 0.0f;
					int paletteBase = GetEditNumber(data->hWndPaletteBase);
					int paletteSize = GetEditNumber(data->hWndPaletteSize);
					int nMaxChars = GetEditNumber(data->hWndMaxChars);
					NpGetBalanceSetting(&data->balance, &balance);

					//get OBJ size
					const int objWidths[] = { 8, 8, 8, 16, 16, 16, 32, 32, 32, 32, 64, 64 };
					const int objHeights[] = { 8, 16, 32, 8, 16, 32, 8, 16, 32, 64, 32, 64 };
					int objSizeSelection = UiCbGetCurSel(data->hWndObjSize);

					//get mapping setting
					const int mapGranularities[] = { 32, 64, 128, 256 };
					int mappingGranularity = mapGranularities[UiCbGetCurSel(data->hWndMappingGranularity)];
					mappingGranularity /= (8 * data->ncgr->nBits);
					if (mappingGranularity == 0) mappingGranularity = 1;

					NCLR *nclr = data->nclr;
					NCGR *ncgr = data->ncgr;

					HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
					ChrImportData *cimport = (ChrImportData *) calloc(1, sizeof(ChrImportData));
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					cimport->createPalette = createPalette;
					cimport->dither = dither;
					cimport->objMode = objMode;
					cimport->objCharsX = objWidths[objSizeSelection] / 8;
					cimport->objCharsY = objHeights[objSizeSelection] / 8;
					cimport->objMapGranularity = mappingGranularity;
					cimport->import1D = import1D;
					cimport->charCompression = charCompression;
					cimport->diffuse = diffuse;
					cimport->paletteBase = paletteBase;
					cimport->paletteSize = paletteSize;
					cimport->nMaxChars = nMaxChars;
					cimport->nclr = nclr;
					cimport->ncgr = ncgr;
					cimport->originX = data->contextHoverX;
					cimport->originY = data->contextHoverY;
					cimport->paletteNumber = data->selectedPalette;
					cimport->balance = balance.balance;
					cimport->colorBalance = balance.colorBalance;
					cimport->enhanceColors = balance.enhanceColors;
					cimport->ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(EditorFindByObject(hWndMain, &ncgr->header));
					cimport->px = data->px;
					cimport->width = data->width;
					cimport->height = data->height;
					progressData->data = cimport;
					progressData->callback = ChrImportCallback;

					HWND hWndProgress = CreateWindow(L"ProgressWindowClass", L"In Progress...", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 100, hWndMain, NULL, NULL, NULL);
					SendMessage(hWndProgress, NV_SETDATA, 0, (LPARAM) progressData);
					ChrImportThreaded(progressData);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					DoModalEx(hWndProgress, FALSE);
				}
			}
			break;
		}
		case WM_DESTROY:
			free(data);
			break;
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

static void RegisterNcgrPreviewClass(void) {
	RegisterGenericClass(L"NcgrPreviewClass", ChrViewerPreviewWndProc, sizeof(LPVOID));
}

static void RegisterNcgrExpandClass(void) {
	RegisterGenericClass(L"ExpandNcgrClass", NcgrExpandProc, sizeof(LPVOID));
}

static void RegisterCharImportClass(void) {
	RegisterGenericClass(L"CharImportDialog", CharImportProc, sizeof(LPVOID));
}

void RegisterNcgrViewerClass(void) {
	ChrRegisterFormats();

	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EDITOR_CLASS *cls = EditorRegister(L"NcgrViewerClass", ChrViewerWndProc, L"Character Editor", sizeof(NCGRVIEWERDATA), features);
	EditorAddFilter(cls, NCGR_TYPE_NCGR, L"ncgr", L"NCGR Files (*.ncgr)\0*.ncgr\0");
	EditorAddFilter(cls, NCGR_TYPE_NC, L"ncg", L"NCG Files (*.ncg)\0*.ncg\0");
	EditorAddFilter(cls, NCGR_TYPE_IC, L"icg", L"ICG Files (*.icg)\0*.icg\0");
	EditorAddFilter(cls, NCGR_TYPE_AC, L"acg", L"ACG Files (*.acg)\0*.acg\0");
	EditorAddFilter(cls, NCGR_TYPE_HUDSON, L"bin", L"Character Files (*.bin, *ncg.bin, *icg.bin, *.nbfc)\0*.bin;*.nbfc\0");
	EditorAddFilter(cls, NCGR_TYPE_HUDSON2, L"bin", L"Character Files (*.bin, *ncg.bin, *icg.bin, *.nbfc)\0*.bin;*.nbfc\0");
	EditorAddFilter(cls, NCGR_TYPE_SETOSA, L"schr", L"Character Files (*.schr)\0*.schr\0");
	EditorAddFilter(cls, NCGR_TYPE_BIN, L"bin", L"Character Files (*.bin, *ncg.bin, *icg.bin, *.nbfc)\0*.bin;*.nbfc\0");
	EditorAddFilter(cls, NCGR_TYPE_COMBO, L"bin", L"Combination Files (*.dat, *.bnr, *.bin)\0*.dat;*.bnr;*.bin\0");
	
	RegisterNcgrPreviewClass();
	RegisterNcgrExpandClass();
	RegisterCharImportClass();
}

static HWND CreateNcgrViewerInternal(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path, NCGR *ncgr) {
	HWND hWnd = EditorCreate(L"NcgrViewerClass", x, y, 0, 0, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) path, (LPARAM) ncgr);
	if (ncgr->header.format == NCGR_TYPE_HUDSON || ncgr->header.format == NCGR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}

HWND CreateNcgrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCGR *ncgr) {
	return CreateNcgrViewerInternal(x, y, width, height, hWndParent, NULL, ncgr);
}
