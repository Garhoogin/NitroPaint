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
	HWND hWnd = data->hWnd;
	HWND hWndMain = getMainWindow(hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
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
	if (data->ncgr.attr == NULL) return 0;

	return data->ncgr.attr[x + y * data->ncgr.tilesX] & 0xF;
}

static void ChrViewerSetAttribute(NCGRVIEWERDATA *data, int x, int y, int attr) {
	if (data->ncgr.attr == NULL) return; //cannot
	if (x < 0 || y < 0 || x >= data->ncgr.tilesX || y >= data->ncgr.tilesY) return; //cannot

	data->ncgr.attr[x + y * data->ncgr.tilesX] = attr & 0xF;
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

static void ChrViewerInvalidateAllDependents(HWND hWnd) {
	HWND hWndMain = getMainWindow(hWnd);
	InvalidateAllEditors(hWndMain, FILE_TYPE_CELL);
	InvalidateAllEditors(hWndMain, FILE_TYPE_NANR);
	InvalidateAllEditors(hWndMain, FILE_TYPE_NMCR);
	InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);
}

static void ChrViewerGraphicsUpdated(NCGRVIEWERDATA *data) {
	//graphics data updated, so invalidate the view window.
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);

	//invalidate every open cell and screen viewer.
	ChrViewerInvalidateAllDependents(data->hWnd);

	//update preview
	SendMessage(data->hWnd, NV_UPDATEPREVIEW, 0, 0);
}

static void ChrViewerFill(NCGRVIEWERDATA *data, int x, int y, int w, int h, const unsigned char *pat) {
	for (int i = 0; i < h; i++) {
		for (int j = 0; j < w; j++) {
			memcpy(data->ncgr.tiles[(j + x) + (i + y) * data->ncgr.tilesX], pat, 8 * 8);
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
	NCGR *ncgr = &data->ncgr;
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
	opx->format = (data->ncgr.nBits == 4) ? 0x29 : 0x2A;
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
		nclr = &nclrViewerData->nclr;
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
	npc->depth = data->ncgr.nBits;
	npc->useAttr = data->useAttribute;
	npc->paletteSize = clipPaletteSize / sizeof(COLOR);

	//copy palette
	if (nclr != NULL) memcpy(npc->data, nclr->colors, clipPaletteSize);

	//copy chars
	unsigned char *chrDest = npc->data + clipPaletteSize;
	for (int y = 0; y < selH; y++) {
		for (int x = 0; x < selW; x++) {
			memcpy(chrDest + 64 * (x + y * selW), data->ncgr.tiles[(selX + x) + (selY + y) * data->ncgr.tilesX], 64);
		}
	}

	//copy attr
	unsigned char *attrDest = chrDest + charSize;
	if (data->ncgr.attr != NULL) {
		npc->pltMin = 0xF;
		npc->pltMax = 0x0;
		for (int y = 0; y < selH; y++) {
			for (int x = 0; x < selW; x++) {
				int attr = data->ncgr.attr[(selX + x) + (selY + y) * data->ncgr.tilesX];
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
			int chrno = (x + selX) + (y + selY) * data->ncgr.tilesX;
			bgdat[x + y * selW] = (attr << 12) | (chrno & 0x3FF);
		}
	}

	ScrViewerCopyNP_SCRN(selW, selH, bgdat);
	free(bgdat);
}

static void ChrViewerCopy(NCGRVIEWERDATA *data) {
	HWND hWnd = data->hWnd;
	OpenClipboard(hWnd);
	EmptyClipboard();

	//copy data to clipboard
	ChrViewerCopyDIB(data);      // DIB data
	ChrViewerCopyOPX(data);      // OPTPiX data
	ChrViewerCopyNP_CHARS(data); // NitroPaint data
	ChrViewerCopyNP_SCRN(data);  // NitroPaint data

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
	NCGR *ncgr = &data->ncgr;
	NCLRVIEWERDATA *nclrViewerData = ChrViewerGetAssociatedPaletteViewerData(data);
	if (nclrViewerData != NULL) {
		nclr = &nclrViewerData->nclr;
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
				memcpy(ncgr->tiles[(pasteX + x) + (pasteY + y) * data->ncgr.tilesX], chars + i * 64, 64);
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
			for (int i = 0; i < nColsDest; i++) {
				if ((palFirst + i) >= nclr->nColors) break;
				if (i >= pltSize) break;

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
	colidx &= (1 << (data->ncgr.nBits + 4)) - 1;
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
	if (x >= (data->ncgr.tilesX * 8) || y >= (data->ncgr.tilesY * 8)) return;

	ChrViewerPutPixelInternal(&data->ncgr, x, y, col & ((1 << data->ncgr.nBits) - 1));
	ChrViewerSetAttribute(data, x / 8, y / 8, col >> data->ncgr.nBits);
}

static int ChrViewerGetPixel(NCGRVIEWERDATA *data, int x, int y) {
	if (x < 0 || y < 0) return -1;
	if (x >= (data->ncgr.tilesX * 8) || y >= (data->ncgr.tilesY * 8)) return -1;

	return ChrViewerGetPixelInternal(&data->ncgr, x, y) | (ChrViewerGetCharPalette(data, x / 8, y / 8) << data->ncgr.nBits);
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
	int gfxW = data->ncgr.tilesX * 8, gfxH = data->ncgr.tilesY * 8;

	//bounds check coordinate
	if (x < 0 || y < 0 || x >= gfxW || y >= gfxH) return;
	pcol &= ((1 << data->ncgr.nBits) - 1);

	//get color of interest
	NCGR *ncgr = &data->ncgr;
	int bkcol = ChrViewerGetPixel(data, x, y);
	if (pcol == bkcol) return; //no-op (stop a huge memory spiral)

	//keep a stack of colors. push the first color. Each time we pop a color from the stack, 
	//check all 8 neighbors. If they match the initial color, push them to the stack. Fill
	//the pixel with the new color. Repeat until stack is empty.
	int stackCap = 16, stackSize = 0;
	POINT *stack = (POINT *) calloc(stackCap, sizeof(POINT));
	{
		POINT *first = &stack[stackSize++];
		first->x = x;
		first->y = y;
	}

	while (stackSize > 0) {
		POINT *pt = &stack[--stackSize];
		int ptX = pt->x, ptY = pt->y;
		ChrViewerPutPixel(data, ptX, ptY, pcol);
		
		//search vicinity
		for (int y_ = 0; y_ < 3; y_++) {
			for (int x_ = 0; x_ < 3; x_++) {
				//skip corners and center
				if (x_ != 1 && y_ != 1) continue;
				if (x_ == 1 && y_ == 1) continue;

				int drawX = ptX + (x_ - 1);
				int drawY = ptY + (y_ - 1);
				if (drawX >= 0 && drawY >= 0 && drawX < gfxW && drawY < gfxH) {
					int c = ChrViewerGetPixel(data, drawX, drawY);
					if (c == bkcol) {
						//increase stack
						if (stackSize >= stackCap) {
							stackCap = (stackCap + 2) * 3 / 2;
							POINT *newstack = realloc(stack, stackCap * sizeof(POINT));
							if (newstack == NULL) goto AllocFail;

							stack = newstack;
						}

						POINT *next = &stack[stackSize++];
						next->x = drawX;
						next->y = drawY;
					}
				}
			}
		}
	}

AllocFail:
	free(stack);
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
				unsigned char **p1 = &data->ncgr.tiles[(selX + x) + (selY + y) * data->ncgr.tilesX];
				unsigned char **p2 = &data->ncgr.tiles[(selX + selW - 1 - x) + (selY + y) * data->ncgr.tilesX];
				unsigned char *tmp = *p1;
				*p1 = *p2;
				*p2 = tmp;

				//flip attributes
				if (data->ncgr.attr != NULL) {
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
				unsigned char **p1 = &data->ncgr.tiles[(selX + x) + (selY + y) * data->ncgr.tilesX];
				unsigned char **p2 = &data->ncgr.tiles[(selX + x) + (selY + selH - 1 - y) * data->ncgr.tilesX];
				unsigned char *tmp = *p1;
				*p1 = *p2;
				*p2 = tmp;

				//flip attributes
				if (data->ncgr.attr != NULL) {
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
			unsigned char *chr = data->ncgr.tiles[(x + selX) + (y + selY) * data->ncgr.tilesX];
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

	unsigned char *src = data->ncgr.tiles[srcTileX + srcTileY * data->ncgr.tilesX];
	unsigned char *dst = data->ncgr.tiles[x + y * data->ncgr.tilesX];
	memcpy(dst, src, 64);

	ChrViewerGraphicsUpdated(data);
}



// ----- functions for handling the margins



static void ChrViewerGetGraphicsSize(NCGRVIEWERDATA *data, int *width, int *height) {
	*width = data->ncgr.tilesX * (8 * data->scale);
	*height = data->ncgr.tilesY * (8 * data->scale);
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

static void ChrViewerPopulateWidthField(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	SendMessage(data->hWndWidthDropdown, CB_RESETCONTENT, 0, 0);
	int nTiles = data->ncgr.nTiles;
	int nStrings = 0;
	WCHAR bf[16];

	for (int i = 1; i <= nTiles; i++) {
		if (nTiles % i) continue;
		wsprintfW(bf, L"%d", i);
		SendMessage(data->hWndWidthDropdown, CB_ADDSTRING, 0, (LPARAM) bf);
		if (i == data->ncgr.tilesX) {
			SendMessage(data->hWndWidthDropdown, CB_SETCURSEL, (WPARAM) nStrings, 0);
		}
		nStrings++;
	}
}

static void ChrViewerUpdateCharacterLabel(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	WCHAR buffer[64];
	if (!TedHasSelection(&data->ted)) {
		wsprintf(buffer, L" Character %d", data->ted.hoverIndex);
	} else {
		int selX, selY, selW, selH;
		TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);
		wsprintf(buffer, L" Character %d  Selection: (%d, %d), (%d, %d)", data->ted.hoverIndex, selX, selY, selW, selH);
	}
	SendMessage(data->hWndCharacterLabel, WM_SETTEXT, wcslen(buffer), (LPARAM) buffer);
}

static void ChrViewerSetWidth(HWND hWnd, int width) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	//update width
	ChrSetWidth(&data->ncgr, width);
	TedUpdateSize((EDITOR_DATA *) data, &data->ted, data->ncgr.tilesX, data->ncgr.tilesY);

	//update
	ChrViewerPopulateWidthField(hWnd);
	SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
	ChrViewerGraphicsUpdated(data);
}

static void ChrViewerSetDepth(HWND hWnd, int depth) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	//set depth and update UI
	ChrSetDepth(&data->ncgr, depth);
	TedUpdateSize((EDITOR_DATA *) data, &data->ted, data->ncgr.tilesX, data->ncgr.tilesY);
	
	ChrViewerPopulateWidthField(hWnd);
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
	HWND hWndImport;
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;
} CHARIMPORTDATA;


static void ChrViewerOnCreate(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);
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
		SendMessage(data->hWndPaletteDropdown, CB_ADDSTRING, 0, (LPARAM) bf);
	}
	SendMessage(data->hWndPaletteDropdown, CB_SETCURSEL, 0, 0);

	//read config data
	if (!g_configuration.ncgrViewerConfiguration.gridlines) {
		HWND hWndMain = getMainWindow(hWnd);
		data->showBorders = 0;
		CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
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

	int maxHeight = rcMdi.bottom * 4 / 5; // 80% of client height
	int maxWidth = rcMdi.right * 4 / 5;  // 80% of client height
	if (height >= maxHeight) height = maxHeight;
	if (width >= maxWidth) width = maxWidth;

	SetWindowSize(data->hWnd, width, height);
}

static void ChrViewerOnInitialize(HWND hWnd, LPCWSTR path, NCGR *ncgr, int immediate) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);
	float dpiScale = GetDpiScale();

	if (!immediate) {
		memcpy(&data->ncgr, ncgr, sizeof(NCGR));
		EditorSetFile(hWnd, path);
	} else {
		memcpy(&data->ncgr, ncgr, sizeof(NCGR));
	}
	data->ted.tilesX = data->ncgr.tilesX;
	data->ted.tilesY = data->ncgr.tilesY;
	SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

	ChrViewerSetPreferredSize(data);
	
	ChrViewerPopulateWidthField(hWnd);
	if (data->ncgr.nBits == 8) SendMessage(data->hWnd8bpp, BM_SETCHECK, 1, 0);

	//guess a tile base for open NSCR (if any)
	HWND hWndMain = getMainWindow(hWnd);
	int nNscrEditors = GetAllEditors(hWndMain, FILE_TYPE_SCREEN, NULL, 0);
	if (nNscrEditors > 0) {
		//for each editor
		HWND *nscrEditors = (HWND *) calloc(nNscrEditors, sizeof(HWND));
		GetAllEditors(hWndMain, FILE_TYPE_SCREEN, nscrEditors, nNscrEditors);
		for (int i = 0; i < nNscrEditors; i++) {
			HWND hWndNscrViewer = nscrEditors[i];
			NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(hWndNscrViewer);
			NSCR *nscr = &nscrViewerData->nscr;
			if (nscr->nHighestIndex >= data->ncgr.nTiles) {
				NscrViewerSetTileBase(hWndNscrViewer, nscr->nHighestIndex + 1 - data->ncgr.nTiles);
			} else {
				NscrViewerSetTileBase(hWndNscrViewer, 0);
			}
		}
		free(nscrEditors);
	}
	ShowWindow(hWnd, SW_SHOW);
}

static LRESULT ChrViewerOnSize(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);
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

static void ChrViewerOnDestroy(HWND hWnd) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	HWND hWndMain = getMainWindow(hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	nitroPaintStruct->hWndNcgrViewer = NULL;

	HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
	if (hWndNclrViewer != NULL) InvalidateRect(hWndNclrViewer, NULL, FALSE);
	TedDestroy(&data->ted);
}

static int ChrViewerOnTimer(HWND hWnd, int idTimer) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	if (idTimer == 1) {
		data->verifyFrames--;
		if (!data->verifyFrames) {
			KillTimer(hWnd, idTimer);
		}
		InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
	}
	return 0;
}


static void ChrViewerOnCtlCommand(HWND hWnd, HWND hWndControl, int notification) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	if (notification == CBN_SELCHANGE && hWndControl == data->hWndPaletteDropdown) {
		int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
		data->selectedPalette = sel;
		InvalidateRect(hWnd, NULL, FALSE);

		HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
		if (hWndNclrViewer != NULL) InvalidateRect(hWndNclrViewer, NULL, FALSE);
	} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndWidthDropdown) {
		WCHAR text[16];
		int selected = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
		SendMessage(hWndControl, CB_GETLBTEXT, (WPARAM) selected, (LPARAM) text);
		int width = _wtol(text);
		ChrViewerSetWidth(hWnd, width);
	} else if (notification == BN_CLICKED && hWndControl == data->hWndExpand) {
		HWND hWndMain = getMainWindow(hWnd);
		HWND h = CreateWindow(L"ExpandNcgrClass", L"Resize Graphics", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
			CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
		DoModal(h);
		SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
	} else if (notification == BN_CLICKED && hWndControl == data->hWnd8bpp) {
		int state = GetCheckboxChecked(hWndControl);
		int depth = (state) ? 8 : 4;
		ChrViewerSetDepth(hWnd, depth);
		SetFocus(data->hWnd);
	} else if (notification == BN_CLICKED && hWndControl == data->hWndUseAttribute) {
		data->useAttribute = GetCheckboxChecked(hWndControl);
		ChrViewerGraphicsUpdated(data);
		SetFocus(data->hWnd);
	}
}

static void ChrViewerImportDialog(NCGRVIEWERDATA *data, BOOL createPalette, int pasteX, int pasteY, COLOR32 *px, int width, int height) {
	HWND hWndMain = getMainWindow(data->hWnd);
	HWND h = CreateWindow(L"CharImportDialog", L"Import Bitmap", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
	CHARIMPORTDATA *cidata = (CHARIMPORTDATA *) GetWindowLongPtr(h, 0);

	//populate import params
	cidata->px = px;
	cidata->width = width;
	cidata->height = height;
	if (createPalette) SendMessage(cidata->hWndOverwritePalette, BM_SETCHECK, BST_CHECKED, 0);
	if (data->ncgr.nBits == 4) SetEditNumber(cidata->hWndPaletteSize, 16);
	SetEditNumber(cidata->hWndMaxChars, data->ncgr.nTiles - (data->ted.contextHoverX + pasteY * pasteX));

	HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
	NCLR *nclr = NULL;
	NCGR *ncgr = &data->ncgr;
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
	NCGR *ncgr = &data->ncgr;
	NSCR *nscr = &nscrViewerData->nscr;

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
	NCGR *ncgr = &data->ncgr;
	NCER *ncer = &ncerViewerData->ncer;

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
	if (data->ncgr.attr == NULL) {
		data->ncgr.attr = (unsigned char *) calloc(data->ncgr.tilesX * data->ncgr.tilesY, 1);
	}

	HWND hWndMain = getMainWindow(data->hWnd);
	int nScrEditors = GetAllEditors(hWndMain, FILE_TYPE_SCREEN, NULL, 0);
	int nCellEditors = GetAllEditors(hWndMain, FILE_TYPE_CELL, NULL, 0);

	if (nScrEditors > 0) {
		HWND *scrEditors = (HWND *) calloc(nScrEditors, sizeof(HWND));
		GetAllEditors(hWndMain, FILE_TYPE_SCREEN, scrEditors, nScrEditors);
		for (int i = 0; i < nScrEditors; i++) {
			NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(scrEditors[i]);
			ChrViewerImportAttributesFromScreen(data, nscrViewerData);
		}
		free(scrEditors);
	}
	if (nCellEditors > 0) {
		HWND *cellEditors = (HWND *) calloc(nCellEditors, sizeof(HWND));
		GetAllEditors(hWndMain, FILE_TYPE_CELL, cellEditors, nCellEditors);
		for (int i = 0; i < nCellEditors; i++) {
			NCERVIEWERDATA *ncerViewerData = (NCERVIEWERDATA *) EditorGetData(cellEditors[i]);
			ChrViewerImportAttributesFromCell(data, ncerViewerData);
		}
		free(cellEditors);
	}
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
}

static void ChrViewerOnMenuCommand(HWND hWnd, int idMenu) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
		case ID_ZOOM_100:
		case ID_ZOOM_200:
		case ID_ZOOM_400:
		case ID_ZOOM_800:
		case ID_ZOOM_1600:
			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			TedUpdateMargins(&data->ted);
			break;
		case ID_NCGRMENU_IMPORTBITMAPHERE:
		{
			LPWSTR path = openFileDialog(hWnd, L"Select Bitmap", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
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
			EditorSaveAs(hWnd);
			break;
		case ID_FILE_SAVE:
			EditorSave(hWnd);
			break;
		case ID_FILE_EXPORT:
		{
			LPWSTR location = saveFileDialog(hWnd, L"Save Bitmap", L"PNG Files (*.png)\0*.png\0All Files\0*.*\0", L"png");
			if (!location) break;

			HWND hWndNclrViewer = ChrViewerGetAssociatedPaletteViewer(data);
			HWND hWndNcgrViewer = hWnd;

			NCGR *ncgr = &data->ncgr;
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
	}
}

static void ChrViewerOnAccelerator(HWND hWnd, int idAccel) {
	NCGRVIEWERDATA *data = EditorGetData(hWnd);

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
	ChrViewerUpdateCharacterLabel(data->hWnd);
}

static void ChrViewerOnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	if (lParam) {
		ChrViewerOnCtlCommand(hWnd, (HWND) lParam, HIWORD(wParam));
	} else if (HIWORD(wParam) == 0) {
		ChrViewerOnMenuCommand(hWnd, LOWORD(wParam));
	} else if (HIWORD(wParam) == 1) {
		ChrViewerOnAccelerator(hWnd, LOWORD(wParam));
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

	ChrViewerUpdateCharacterLabel(hWnd);

	//update screen viewers
	int nScrViewers = GetAllEditors(getMainWindow(hWnd), FILE_TYPE_SCREEN, NULL, 0);
	if (nScrViewers > 0) {
		HWND *hWnds = (HWND *) calloc(nScrViewers, sizeof(HWND));
		GetAllEditors(getMainWindow(hWnd), FILE_TYPE_SCREEN, hWnds, nScrViewers);

		//invalidate viewer region of each BG screen editor
		for (int i = 0; i < nScrViewers; i++) {
			NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(hWnds[i]);
			InvalidateRect(nscrViewerData->ted.hWndViewer, NULL, FALSE);
		}

		free(hWnds);
	}
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
			ChrViewerUpdateCharacterLabel(data->hWnd);
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
			ChrViewerOnCreate(hWnd);
			break;
		case NV_INITIALIZE:
		case NV_INITIALIZE_IMMEDIATE:
			ChrViewerOnInitialize(hWnd, (LPCWSTR) wParam, (NCGR *) lParam, msg == NV_INITIALIZE_IMMEDIATE);
			break;
		case NV_UPDATEPREVIEW:
			PreviewLoadBgCharacter(&data->ncgr);
			PreviewLoadObjCharacter(&data->ncgr);
			break;
		case WM_COMMAND:
			ChrViewerOnCommand(hWnd, wParam, lParam);
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
		case WM_TIMER:
			return ChrViewerOnTimer(hWnd, wParam);
		case WM_DESTROY:
			ChrViewerOnDestroy(hWnd);
			break;
		case WM_SIZE:
			return ChrViewerOnSize(hWnd, wParam, lParam);
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

	NCGR *ncgr = &data->ncgr;
	NCLR *nclr = NULL;

	HWND hWndMain = getMainWindow(data->hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
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

			COLOR32 col;
			if (!data->transparent || rawIdx > 0) {
				//do not render transparent background
				if (nclr != NULL && idx < nclr->nColors) {
					//color from palette
					col = ColorConvertFromDS(nclr->colors[idx]);
				} else {
					//color out of palette bounds: fill with black
					col = 0;
				}
			} else {
				//render transparent
				COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };
				col = checker[((x ^ y) >> 2) & 1];
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
			return 1;
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
			UpdateScrollbarVisibility(hWnd);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
		case WM_DESTROY:
			free(frameData);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
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
			SetEditNumber(data->hWndExpandRowsInput, data->ncgr.tilesY);
			SetEditNumber(data->hWndExpandColsInput, data->ncgr.tilesX);
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
				ChrResize(&data->ncgr, nCols, nRows);
				ChrViewerPopulateWidthField(data->hWnd);
				data->ted.tilesX = data->ncgr.tilesX;
				data->ted.tilesY = data->ncgr.tilesY;
				SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);

				//invalidate viewers
				ChrViewerGraphicsUpdated(data);
				TedUpdateMargins(&data->ted);

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
	HWND hWndMain = getMainWindow(data->hWnd);
	BOOL import1D = cim->import1D;

	InvalidateAllEditors(hWndMain, FILE_TYPE_PALETTE);
	InvalidateAllEditors(hWndMain, FILE_TYPE_CHAR);
	InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);
	InvalidateAllEditors(hWndMain, FILE_TYPE_CELL);

	setStyle(hWndMain, FALSE, WS_DISABLED);
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
		int origin = cim->originX + cim->originY * data->ncgr.tilesX;
		int nChars = (cim->width / 8) * (cim->height / 8);
		if (cim->charCompression) nChars = cim->nMaxChars;
		for (int i = 0; i < nChars; i++) {
			ChrViewerSetAttribute(data, (i + origin) % data->ncgr.tilesX, (i + origin) / data->ncgr.tilesX, data->selectedPalette);
		}
	}

	free(cbdata);

	return 0;
}

static void charImport(NCLR *nclr, NCGR *ncgr, COLOR32 *pixels, int width, int height, BOOL createPalette, int paletteNumber, int paletteSize, int paletteBase, 
	BOOL dither, float diffuse, BOOL import1D, BOOL charCompression, int nMaxChars, int originX, int originY, 
	int balance, int colorBalance, int enhanceColors, int *progress) {
	int maxPaletteSize = 1 << ncgr->nBits;

	//if we start at base 0, increment by 1. We'll put a placeholder color in slot 0.
	if (paletteBase == 0) {
		paletteBase = 1;
		paletteSize--;
		if (createPalette) nclr->colors[paletteNumber << ncgr->nBits] = ColorConvertToDS(0xFF00FF);
	}

	int firstColorIndex = (paletteNumber << ncgr->nBits) + paletteBase;
	if(paletteSize > maxPaletteSize) paletteSize = maxPaletteSize;
	if (firstColorIndex + paletteSize >= nclr->nColors) {
		paletteSize = nclr->nColors - firstColorIndex;
	}

	COLOR *nitroPalette = nclr->colors + firstColorIndex;
	COLOR32 *palette = (COLOR32 *) calloc(paletteSize, 4);

	//if we use an existing palette, decode the palette values.
	//if we do not use an existing palette, generate one.
	if (!createPalette) {
		//decode the palette
		for (int i = 0; i < paletteSize; i++) {
			COLOR32 col = ColorConvertFromDS(nitroPalette[i]);
			palette[i] = col;
		}
	} else {
		//create a palette, then encode them to the nclr
		RxCreatePaletteEx(pixels, width, height, palette, paletteSize, balance, colorBalance, enhanceColors, 0);
		for (int i = 0; i < paletteSize; i++) {
			COLOR32 d = palette[i];
			COLOR ds = ColorConvertToDS(d);
			nitroPalette[i] = ds;
			palette[i] = ColorConvertFromDS(ds);
		}
	}

	//index image with given parameters.
	if (!dither) diffuse = 0.0f;
	RxReduceImageEx(pixels, NULL, width, height, palette, paletteSize, 0, 1, 0, diffuse, balance, colorBalance, enhanceColors);

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
				BYTE *tile = ncgr->tiles[offset];

				//write out this tile using the palette. Diffuse any error accordingly.
				for (int i = 0; i < 64; i++) {
					int offsetX = i & 0x7;
					int offsetY = i >> 3;
					int poffset = x * 8 + offsetX + (y * 8 + offsetY) * width;
					COLOR32 pixel = pixels[poffset];

					int closest = RxPaletteFindClosestColorSimple(pixel, palette, paletteSize) + paletteBase;
					if ((pixel >> 24) < 127) closest = 0;
					tile[i] = closest;
				}
			}
		}
	} else {
		//1D import, start at index and continue linearly.
		COLOR32 *tiles = (COLOR32 *) calloc(tilesX * tilesY, 64 * sizeof(COLOR32));
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				int imgX = x * 8, imgY = y * 8;
				int tileIndex = x + y * tilesX;
				int srcIndex = imgX + imgY * width;
				COLOR32 *src = pixels + srcIndex;
				for (int i = 0; i < 8; i++) {
					memcpy(tiles + 64 * tileIndex + 8 * i, src + i * width, 32);
				}
			}
		}

		//character compression
		int nChars = tilesX * tilesY;
		if (charCompression) {
			//create dummy whole palette that the character compression functions expect
			COLOR32 dummyFull[256] = { 0 };
			memcpy(dummyFull + paletteBase, palette, paletteSize * 4);

			BgTile *bgTiles = (BgTile *) calloc(nChars, sizeof(BgTile));

			//split image into 8x8 tiles.
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					int srcOffset = x * 8 + y * 8 * (width);
					COLOR32 *block = bgTiles[x + y * tilesX].px;

					int index = x + y * tilesX;
					memcpy(block, tiles + index * 64, 64 * 4);

					for (int i = 0; i < 8 * 8; i++) {
						int a = (block[i] >> 24) & 0xFF;
						if (a < 128) block[i] = 0; //make transparent pixels transparent black
						else block[i] |= 0xFF000000; //opaque
					}
				}
			}
			int nTiles = nChars;
			BgSetupTiles(bgTiles, nChars, ncgr->nBits, dummyFull, paletteSize, 1, 0, paletteBase, 0, 0.0f, balance, colorBalance, enhanceColors);
			nChars = BgPerformCharacterCompression(bgTiles, nChars, ncgr->nBits, nMaxChars, dummyFull, paletteSize, 1, 0, paletteBase, 
				balance, colorBalance, progress);

			//read back result
			int outIndex = 0;
			for (int i = 0; i < nTiles; i++) {
				if (bgTiles[i].masterTile != i) continue;
				BgTile *t = bgTiles + i;

				COLOR32 *dest = tiles + outIndex * 64;
				for (int j = 0; j < 64; j++) {
					int index = t->indices[j];
					if (index) dest[j] = dummyFull[index] | 0xFF000000;
					else dest[j] = 0;
				}
				outIndex++;
			}
			free(bgTiles);
		}

		//break into tiles and write
		int destBaseIndex = originX + originY * ncgr->tilesX;
		int nWriteChars = min(nChars, ncgr->nTiles - destBaseIndex);
		for (int i = 0; i < nWriteChars; i++) {
			BYTE *tile = ncgr->tiles[i + destBaseIndex];
			COLOR32 *srcTile = tiles + i * 64;

			for (int j = 0; j < 64; j++) {
				COLOR32 pixel = srcTile[j];

				int closest = RxPaletteFindClosestColorSimple(pixel, palette, paletteSize) + paletteBase;
				if ((pixel >> 24) < 127) closest = 0;
				tile[j] = closest;
			}
		}
		free(tiles);
	}

	free(palette);
}

static DWORD WINAPI ChrImportInternal(LPVOID lpParameter) {
	PROGRESSDATA *progress = (PROGRESSDATA *) lpParameter;
	ChrImportData *cim = (ChrImportData *) progress->data;
	progress->progress1Max = 100;
	progress->progress1 = 100;
	progress->progress2Max = 1000;
	charImport(cim->nclr, cim->ncgr, cim->px, cim->width, cim->height, cim->createPalette, cim->paletteNumber, cim->paletteSize, cim->paletteBase, 
			   cim->dither, cim->diffuse, cim->import1D, cim->charCompression, cim->nMaxChars, cim->originX, cim->originY, 
			   cim->balance, cim->colorBalance, cim->enhanceColors, &progress->progress2);
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

			data->hWndOverwritePalette = CreateCheckbox(hWnd, L"Write Palette", leftX, topY, 150, 22, FALSE);
			CreateStatic(hWnd, L"Palette Base:", leftX, topY + 27, 75, 22);
			data->hWndPaletteBase = CreateEdit(hWnd, L"0", leftX + 85, topY + 27, 100, 22, TRUE);
			CreateStatic(hWnd, L"Palette Size:", leftX, topY + 27 * 2, 75, 22);
			data->hWndPaletteSize = CreateEdit(hWnd, L"256", leftX + 85, topY + 27 * 2, 100, 22, TRUE);

			data->hWndDither = CreateCheckbox(hWnd, L"Dither", rightX, topY, 150, 22, FALSE);
			CreateStatic(hWnd, L"Diffuse:", rightX, topY + 27, 75, 22);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", rightX + 85, topY + 27, 100, 22, TRUE);

			data->hWnd1D = CreateCheckbox(hWnd, L"1D Import", leftX, middleY, 150, 22, FALSE);
			data->hWndCompression = CreateCheckbox(hWnd, L"Compress Character", leftX, middleY + 27, 150, 22, FALSE);
			CreateStatic(hWnd, L"Max Chars:", leftX, middleY + 27 * 2, 75, 22);
			data->hWndMaxChars = CreateEdit(hWnd, L"1024", leftX + 85, middleY + 27 * 2, 100, 22, TRUE);

			CreateStatic(hWnd, L"Balance:", leftX, bottomY, 100, 22);
			CreateStatic(hWnd, L"Color Balance:", leftX, bottomY + 27, 100, 22);
			data->hWndEnhanceColors = CreateCheckbox(hWnd, L"Enhance Colors", leftX, bottomY + 27 * 2, 200, 22, FALSE);
			CreateStaticAligned(hWnd, L"Lightness", leftX + 110, bottomY, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Color", leftX + 110 + 50 + 200, bottomY, 50, 22, SCA_LEFT);
			CreateStaticAligned(hWnd, L"Green", leftX + 110, bottomY + 27, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Red", leftX + 110 + 50 + 200, bottomY + 27, 50, 22, SCA_LEFT);
			data->hWndBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			data->hWndColorBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY + 27, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);

			data->hWndImport = CreateButton(hWnd, L"Import", width / 2 - 100, height - 32, 200, 22, TRUE);

			CreateGroupbox(hWnd, L"Palette", 10, 10, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Graphics", 10 + boxWidth + 10, 10, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Dimension", 10, 10 + boxHeight + 10, boxWidth * 2 + 10, boxHeight2);
			CreateGroupbox(hWnd, L"Color", 10, 10 + boxHeight + 10 + boxHeight2 + 10, 10 + 2 * boxWidth, boxHeight3);

			SetGUIFont(hWnd);
			setStyle(data->hWndDiffuse, TRUE, WS_DISABLED);
			setStyle(data->hWndCompression, TRUE, WS_DISABLED);
			setStyle(data->hWndMaxChars, TRUE, WS_DISABLED);
			SetWindowSize(hWnd, width, height);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				if (hWndControl == data->hWndDither) {
					int state = GetCheckboxChecked(hWndControl);
					setStyle(data->hWndDiffuse, !state, WS_DISABLED);
					InvalidateRect(hWnd, NULL, TRUE);
				} else if (hWndControl == data->hWnd1D) {
					int state = GetCheckboxChecked(hWndControl);
					int ccState = GetCheckboxChecked(data->hWndCompression);
					if (state) {
						setStyle(data->hWndCompression, FALSE, WS_DISABLED);
						if (ccState) setStyle(data->hWndMaxChars, FALSE, WS_DISABLED);
					} else {
						setStyle(data->hWndCompression, TRUE, WS_DISABLED);
						setStyle(data->hWndMaxChars, TRUE, WS_DISABLED);
					}
					InvalidateRect(hWnd, NULL, TRUE);
				} else if(hWndControl == data->hWndCompression){
					int state = GetCheckboxChecked(hWndControl);
					setStyle(data->hWndMaxChars, !state, WS_DISABLED);
					InvalidateRect(hWnd, NULL, TRUE);
				} else if (hWndControl == data->hWndImport) {
					BOOL createPalette = GetCheckboxChecked(data->hWndOverwritePalette);
					BOOL dither = GetCheckboxChecked(data->hWndDither);
					BOOL import1D = GetCheckboxChecked(data->hWnd1D);
					BOOL charCompression = GetCheckboxChecked(data->hWndCompression);
					float diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;
					if (!dither) diffuse = 0.0f;
					int paletteBase = GetEditNumber(data->hWndPaletteBase);
					int paletteSize = GetEditNumber(data->hWndPaletteSize);
					int nMaxChars = GetEditNumber(data->hWndMaxChars);
					int balance = GetTrackbarPosition(data->hWndBalance);
					int colorBalance = GetTrackbarPosition(data->hWndColorBalance);
					BOOL enhanceColors = GetCheckboxChecked(data->hWndEnhanceColors);

					NCLR *nclr = data->nclr;
					NCGR *ncgr = data->ncgr;

					HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
					ChrImportData *cimport = (ChrImportData *) calloc(1, sizeof(ChrImportData));
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					cimport->createPalette = createPalette;
					cimport->dither = dither;
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
					cimport->balance = balance;
					cimport->colorBalance = colorBalance;
					cimport->enhanceColors = enhanceColors;
					cimport->ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(GetEditorFromObject(hWndMain, &ncgr->header));
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
	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EDITOR_CLASS *cls = EditorRegister(L"NcgrViewerClass", ChrViewerWndProc, L"Character Editor", sizeof(NCGRVIEWERDATA), features);
	EditorAddFilter(cls, NCGR_TYPE_NCGR, L"ncgr", L"NCGR Files (*.ncgr)\0*.ncgr\0");
	EditorAddFilter(cls, NCGR_TYPE_NC, L"ncg", L"NCG Files (*.ncg)\0*.ncg\0");
	EditorAddFilter(cls, NCGR_TYPE_IC, L"icg", L"ICG Files (*.icg)\0*.icg\0");
	EditorAddFilter(cls, NCGR_TYPE_AC, L"acg", L"ACG Files (*.acg)\0*.acg\0");
	EditorAddFilter(cls, NCGR_TYPE_HUDSON, L"bin", L"Character Files (*.bin, *ncg.bin, *icg.bin, *.nbfc)\0*.bin;*.nbfc\0");
	EditorAddFilter(cls, NCGR_TYPE_HUDSON2, L"bin", L"Character Files (*.bin, *ncg.bin, *icg.bin, *.nbfc)\0*.bin;*.nbfc\0");
	EditorAddFilter(cls, NCGR_TYPE_BIN, L"bin", L"Character Files (*.bin, *ncg.bin, *icg.bin, *.nbfc)\0*.bin;*.nbfc\0");
	EditorAddFilter(cls, NCGR_TYPE_COMBO, L"bin", L"Combination Files (*.dat, *.bnr, *.bin)\0*.dat;*.bnr;*.bin\0");
	
	RegisterNcgrPreviewClass();
	RegisterNcgrExpandClass();
	RegisterCharImportClass();
}

HWND CreateNcgrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NCGR ncgr;
	int n = ChrReadFile(&ncgr, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	HWND hWnd = EditorCreate(L"NcgrViewerClass", x, y, 0, 0, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
	if (ncgr.header.format == NCGR_TYPE_HUDSON || ncgr.header.format == NCGR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}

HWND CreateNcgrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCGR *ncgr) {
	HWND hWnd = EditorCreate(L"NcgrViewerClass", x, y, 0, 0, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) ncgr);
	if (ncgr->header.format == NCGR_TYPE_HUDSON || ncgr->header.format == NCGR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}
