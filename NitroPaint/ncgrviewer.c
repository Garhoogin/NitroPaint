#include <Windows.h>
#include <CommCtrl.h>

#include "editor.h"
#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "nscrviewer.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "resource.h"
#include "tiler.h"
#include "gdip.h"
#include "palette.h"
#include "bggen.h"
#include "ui.h"

#include "preview.h"


#define MARGIN_SIZE          8
#define MARGIN_BORDER_SIZE   2
#define MARGIN_TOTAL_SIZE    (MARGIN_SIZE+MARGIN_BORDER_SIZE)
#define SEL_BORDER_THICKNESS 8


//hit test constants for selection
#define HIT_SEL_LEFT            0x0001
#define HIT_SEL_RIGHT           0x0002
#define HIT_SEL_TOP             0x0004
#define HIT_SEL_BOTTOM          0x0008
#define HIT_SEL_CONTENT         0x0010

//hit test constants for margin
#define HIT_MARGIN_LEFT         0x0001
#define HIT_MARGIN_TOP          0x0002
#define HIT_MARGIN_SEL          0x0004

//hit test contents
#define HIT_FLAGS_MASK          0x000F
#define HIT_TYPE_MASK           0x7000

#define HIT_SEL                 0x4000
#define HIT_CONTENT             0x5000
#define HIT_NOWHERE             0x6000
#define HIT_MARGIN              0x7000


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


extern HICON g_appIcon;


static int sOpxCharmapFormat = 0;
static int sPngFormat = 0;

static void ChrViewerReleaseCursor(NCGRVIEWERDATA *data);
static void ChrViewerUpdateCursor(NCGRVIEWERDATA *data);
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

static int ChrViewerHasSelection(NCGRVIEWERDATA *data) {
	if (data->selStartX == -1 || data->selStartY == -1) return 0;
	return 1;
}

static void ChrViewerDeselect(NCGRVIEWERDATA *data) {
	data->selStartX = -1;
	data->selEndX = -1;
	data->selStartY = -1;
	data->selEndY = -1;
}

static int ChrViewerGetSelectionBounds(NCGRVIEWERDATA *data, int *x, int *y, int *width, int *height) {
	//get bounds
	int x1 = min(data->selStartX, data->selEndX);
	int x2 = max(data->selStartX, data->selEndX) + 1;
	int y1 = min(data->selStartY, data->selEndY);
	int y2 = max(data->selStartY, data->selEndY) + 1;

	*x = x1;
	*y = y1;
	*width = x2 - x1;
	*height = y2 - y1;
	return 1;
}

static int ChrViewerIsSelectedAll(NCGRVIEWERDATA *data) {
	if (!ChrViewerHasSelection(data)) return 0;

	int selX, selY, selW, selH;
	ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);

	return (selX == 0 && selY == 0) && (selW == data->ncgr.tilesX && selH == data->ncgr.tilesY);
}

static void ChrViewerSelectAll(NCGRVIEWERDATA *data) {
	data->selStartX = 0;
	data->selStartY = 0;
	data->selEndX = data->ncgr.tilesX - 1;
	data->selEndY = data->ncgr.tilesY - 1;
}

static void ChrViewerMakeSelectionCornerEnd(NCGRVIEWERDATA *data, int hit) {
	//if hit test hits top, make min Y first
	if (hit & HIT_SEL_TOP && data->selEndY > data->selStartY) {
		SwapInts(&data->selStartY, &data->selEndY);
	}
	if (hit & HIT_SEL_BOTTOM && data->selEndY < data->selStartY) {
		SwapInts(&data->selStartY, &data->selEndY);
	}

	//if hit test hits left, make min X first
	if (hit & HIT_SEL_LEFT && data->selEndX > data->selStartX) {
		SwapInts(&data->selStartX, &data->selEndX);
	}
	if (hit & HIT_SEL_RIGHT && data->selEndX < data->selStartX) {
		SwapInts(&data->selStartX, &data->selEndX);
	}
}

static void ChrViewerOffsetSelection(NCGRVIEWERDATA *data, int dx, int dy) {
	data->selStartX += dx;
	data->selStartY += dy;
	data->selEndX += dx;
	data->selEndY += dy;
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
	InvalidateRect(data->hWndViewer, NULL, FALSE);

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
	ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);

	//TODO: handle attribute data?
	int nBits = data->ncgr.nBits;
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

			unsigned char idx = chr[(x % 8) + (y % 8) * 8];

			if (nBits == 4) {
				unsigned char v = scan[x / 2];
				v &= ~(0xF << (((x & 1) ^ 1) * 4));
				v |=  (idx << (((x & 1) ^ 1) * 4));
				scan[x / 2] = v;
			} else if (nBits == 8) {
				scan[x] = idx;
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

static void ChrViewerCopyOPX(NCGRVIEWERDATA *data) {
	if (!ChrViewerEnsureClipboardFormatOPX()) return;

	int selX, selY, selW, selH;
	ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);

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

static void ChrViewerCopy(NCGRVIEWERDATA *data) {
	HWND hWnd = data->hWnd;
	OpenClipboard(hWnd);
	EmptyClipboard();

	//copy data to clipboard
	ChrViewerCopyDIB(data);  // DIB data
	ChrViewerCopyOPX(data);  // OPTPiX data

	CloseClipboard();
}

static int PopCount(DWORD d) {
	//bit population count
	d = ((d & 0xAAAAAAAA) >> 1) + (d & 0x55555555);
	d = ((d & 0xCCCCCCCC) >> 2) + (d & 0x33333333);
	d = ((d & 0xF0F0F0F0) >> 4) + (d & 0x0F0F0F0F);
	d = ((d & 0xFF00FF00) >> 8) + (d & 0x00FF00FF);
	d = ((d & 0xFFFF0000) >> 16) + ((d & 0x0000FFFF));
	return d;
}

static int Ctz(DWORD d) {
	//count trailing zeros
	int i = 0;
	while (!(d & 1)) {
		d >>= 1;
		i++;
	}
	return i;
}

static COLOR32 GetBitmapPixel(BITMAPINFO *bmi, int x, int y) {
	if (x < 0 || y < 0 || x >= bmi->bmiHeader.biWidth) return -1;

	//check height
	int height = bmi->bmiHeader.biHeight;
	if (height < 0) height = -height;
	if (y >= height) return -1;

	int fmt = bmi->bmiHeader.biCompression;

	//get stride
	int bitCount = bmi->bmiHeader.biBitCount;
	int stride = ((bmi->bmiHeader.biWidth * bitCount + 7) / 8 + 3) & ~3;

	int nPalEntries = (fmt == BI_BITFIELDS) ? 3 : bmi->bmiHeader.biClrUsed;
	RGBQUAD *pal = bmi->bmiColors;
	unsigned char *scan = (unsigned char *) (pal + nPalEntries);

	if (bmi->bmiHeader.biHeight >= 0) {
		//flip upside down
		y = bmi->bmiHeader.biHeight - 1 - y;
	}

	unsigned char *row = scan + (y * stride);
	if (bitCount <= 8) {
		//return index from row
		if (bitCount == 8) return row[x];
		else return (row[x / 2] >> (((x & 1) ^ 1) * 4)) & 0xF;
	} else {
		//get color from scan
		if (fmt == BI_RGB) {
			//direct RGB scan
			unsigned char *px = &row[x * (bitCount / 8)];
			return px[2] | (px[1] << 8) | (px[0] << 16);
		} else if (fmt == BI_BITFIELDS) {
			DWORD rMask = *(DWORD *) (pal + 0);
			DWORD gMask = *(DWORD *) (pal + 1);
			DWORD bMask = *(DWORD *) (pal + 2);

			int nRBits = PopCount(rMask), nGBits = PopCount(gMask), nBBits = PopCount(bMask);
			int rShift = Ctz(rMask), gShift = Ctz(gMask), bShift = Ctz(bMask);
			DWORD px = *(DWORD *) (row + x * 4);

			DWORD r = ((px & rMask) >> rShift) * 255 / ((1 << nRBits) - 1);
			DWORD g = ((px & gMask) >> gShift) * 255 / ((1 << nGBits) - 1);
			DWORD b = ((px & bMask) >> bShift) * 255 / ((1 << nBBits) - 1);
			return r | (g << 8) | (b << 16);
		} else {
			//TODO: invalid
			return 0;
		}
	}
}

static COLOR32 GetBitmapPixel32(BITMAPINFO *bmi, int x, int y) {
	//if depth > 8, format is already truecolor.
	if (bmi->bmiHeader.biBitCount > 8) return GetBitmapPixel(bmi, x, y) | 0xFF000000;

	//else, return color from palette
	RGBQUAD *quad = &bmi->bmiColors[GetBitmapPixel(bmi, x, y)];
	return quad->rgbRed | (quad->rgbGreen << 8) | (quad->rgbBlue << 16) | 0xFF000000;
}

static void ChrViewerPaste(NCGRVIEWERDATA *data) {
	//get selection paste point
	int pasteX = -1, pasteY = -1;
	if (ChrViewerHasSelection(data)) {
		int selW, selH;
		ChrViewerGetSelectionBounds(data, &pasteX, &pasteY, &selW, &selH);
	} else if (data->mouseOver) {
		pasteX = data->hoverX;
		pasteY = data->hoverY;
	} else {
		pasteX = data->contextHoverX;
		pasteY = data->contextHoverY;
	}

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
	ChrViewerEnsureClipboardFormatPNG();
	OpenClipboard(hWnd);

	HGLOBAL hDib = GetClipboardData(CF_DIB);
	HGLOBAL hPng = GetClipboardData(sPngFormat);
	if (hPng != NULL || hDib != NULL) {
		//read DIB and/or PNG from the clipboard
		BITMAPINFO *pbmi = NULL;
		void *ppng = NULL;
		unsigned int pngSize = 0;
		COLOR32 *pngPx = NULL;
		int width = 0, height = 0, nBits = 0;

		//read data available to us
		if (hDib != NULL) {
			pbmi = GlobalLock(hDib);
			nBits = pbmi->bmiHeader.biBitCount;
			width = pbmi->bmiHeader.biWidth;
			height = pbmi->bmiHeader.biHeight;
		}
		if (hPng != NULL) {
			ppng = GlobalLock(hPng);
			pngSize = GlobalSize(hPng);
			pngPx = ImgReadMem(ppng, pngSize, &width, &height);
			if (pbmi == NULL) nBits = 32;
		}

		//decode bitmap
		int nColsDest = 1 << ncgr->nBits;
		int palFirst = nColsDest * data->selectedPalette;

		//check palette for matching the current data
		int matchesPalette = ((nBits > 8) || pbmi == NULL) ? FALSE : TRUE; //cannot have a matching palette if there's no palette!
		if (pbmi != NULL && matchesPalette && nclr != NULL) {
			//check matching from the start of the currently selected palette in the viewer
			RGBQUAD *bmpal = pbmi->bmiColors;
			int bmpalSize = pbmi->bmiHeader.biClrUsed;
			for (int i = 0; i < nColsDest; i++) {
				if ((palFirst + i) >= nclr->nColors) break;
				if (i >= bmpalSize) break;

				COLOR32 c = bmpal[i].rgbRed | (bmpal[i].rgbGreen << 8) | (bmpal[i].rgbBlue << 16);
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
			COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
			if (pngPx != NULL) {
				//copy PNG clipboard
				memcpy(px, pngPx, width * height * sizeof(COLOR32));
			} else {
				//no PNG clipboard available, fall back to bitmap data
				for (int y = 0; y < height; y++) {
					for (int x = 0; x < width; x++) {
						px[y * width + x] = GetBitmapPixel32(pbmi, x, y);
					}
				}
			}

			ChrViewerImportDialog(data, FALSE, pasteX, pasteY, px, width, height);
			//pixel array owned by the conversion process now
		} else {

			//scan pixels from bitmap into the graphics editor
			int tilesY = height / 8;
			int tilesX = width / 8;
			for (int y = 0; y < tilesY * 8; y++) {
				for (int x = 0; x < tilesX * 8; x++) {
					//copy bits directly
					COLOR32 c = 0;
					if (pbmi != NULL) {
						c = GetBitmapPixel(pbmi, x, y);
					} else {
						c = pngPx[x + y * width];
					}
					ChrViewerPutPixel(data, pasteX * 8 + x, pasteY * 8 + y, c);
				}
			}

			//next, mark the pasted region as selected.
			data->selStartX = pasteX;
			data->selStartY = pasteY;
			data->selEndX = pasteX + tilesX - 1;
			data->selEndY = pasteY + tilesY - 1;
			if (data->selEndX >= ncgr->tilesX) data->selEndX = ncgr->tilesX - 1;
			if (data->selEndY >= ncgr->tilesY) data->selEndY = ncgr->tilesY - 1;
		}

	Done:
		if (pngPx != NULL) free(pngPx);
		if (hDib != NULL) GlobalUnlock(hDib);
		if (hPng != NULL) GlobalUnlock(hPng);
	}

	CloseClipboard();
	ChrViewerGraphicsUpdated(data);
}

static void ChrViewerCut(NCGRVIEWERDATA *data) {
	if (!ChrViewerHasSelection(data)) return;

	unsigned char erased[64] = { 0 };

	int selX, selY, selW, selH;
	ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);

	ChrViewerCopy(data);
	ChrViewerFill(data, selX, selY, selW, selH, erased);
}


// ----- tool function

static void ChrViewerSetMode(NCGRVIEWERDATA *data, ChrViewerMode mode) {
	data->lastMode = data->mode;
	data->mode = mode;

	//update cursor
	ChrViewerSetCursor(data, 0, HTCLIENT);
	ChrViewerUpdateCursor(data);
}

static int ChrViewerGetSelectedColor(NCGRVIEWERDATA *data) {
	//get open palette file to probe for selection.
	NCLRVIEWERDATA *nclrViewerData = ChrViewerGetAssociatedPaletteViewerData(data);
	if (nclrViewerData == NULL) return -1;
	if (nclrViewerData->selStart == -1 || nclrViewerData->selEnd == -1) return -1;

	return min(nclrViewerData->selStart, nclrViewerData->selEnd);
}

static int ChrViewerSetSelectedColor(NCGRVIEWERDATA *data, int col) {
	NCLRVIEWERDATA *nclrViewerData = ChrViewerGetAssociatedPaletteViewerData(data);
	if (nclrViewerData == NULL) return 0;

	nclrViewerData->selStart = nclrViewerData->selEnd = col + (data->selectedPalette << data->ncgr.nBits);
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

	col &= ((1 << data->ncgr.nBits) - 1);
	ChrViewerPutPixelInternal(&data->ncgr, x, y, col);
}

static int ChrViewerGetPixel(NCGRVIEWERDATA *data, int x, int y) {
	if (x < 0 || y < 0) return -1;
	if (x >= (data->ncgr.tilesX * 8) || y >= (data->ncgr.tilesY * 8)) return -1;

	return ChrViewerGetPixelInternal(&data->ncgr, x, y);
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
	int bkcol = ChrViewerGetPixelInternal(ncgr, x, y);
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
		ChrViewerPutPixelInternal(ncgr, ptX, ptY, pcol);
		
		//search vicinity
		for (int y_ = 0; y_ < 3; y_++) {
			for (int x_ = 0; x_ < 3; x_++) {
				//skip corners and center
				if (x_ != 1 && y_ != 1) continue;
				if (x_ == 1 && y_ == 1) continue;

				int drawX = ptX + (x_ - 1);
				int drawY = ptY + (y_ - 1);
				if (drawX >= 0 && drawY >= 0 && drawX < gfxW && drawY < gfxH) {
					int c = ChrViewerGetPixelInternal(ncgr, drawX, drawY);
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
	if (!ChrViewerHasSelection(data)) return;

	int selX, selY, selW, selH;
	ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);

	//1: swap whole character blocks
	if (flipH) {
		for (int y = 0; y < selH; y++) {
			for (int x = 0; x < selW / 2; x++) {
				unsigned char **p1 = &data->ncgr.tiles[(selX + x) + (selY + y) * data->ncgr.tilesX];
				unsigned char **p2 = &data->ncgr.tiles[(selX + selW - 1 - x) + (selY + y) * data->ncgr.tilesX];
				unsigned char *tmp = *p1;
				*p1 = *p2;
				*p2 = tmp;
			}
		}
	}
	if (flipV) {
		for (int y = 0; y < selH / 2; y++) {
			for (int x = 0; x < selW; x++) {
				unsigned char **p1 = &data->ncgr.tiles[(selX + x) + (selY + y) * data->ncgr.tilesX];
				unsigned char **p2 = &data->ncgr.tiles[(selX + x) + (selY + selH - 1 - y) * data->ncgr.tilesX];
				unsigned char *tmp = *p1;
				*p1 = *p2;
				*p2 = tmp;
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
	if (!ChrViewerHasSelection(data)) return;

	int selX, selY, selW, selH;
	ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);

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

static void ChrViewerGetScroll(NCGRVIEWERDATA *data, int *scrollX, int *scrollY) {
	//get scroll info
	SCROLLINFO scrollH = { 0 }, scrollV = { 0 };
	scrollH.cbSize = scrollV.cbSize = sizeof(scrollH);
	scrollH.fMask = scrollV.fMask = SIF_ALL;
	GetScrollInfo(data->hWndViewer, SB_HORZ, &scrollH);
	GetScrollInfo(data->hWndViewer, SB_VERT, &scrollV);

	*scrollX = scrollH.nPos;
	*scrollY = scrollV.nPos;
}

static void ChrViewerRefreshMargins(HWND hWnd) {
	//get client rect
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	//trigger update only for margin painting
	RECT rcLeft = { 0 }, rcTop = { 0 };
	rcLeft.right = MARGIN_TOTAL_SIZE;
	rcLeft.bottom = rcClient.bottom;
	rcTop.right = rcClient.right;
	rcTop.bottom = MARGIN_TOTAL_SIZE;
	InvalidateRect(hWnd, &rcLeft, FALSE);
	InvalidateRect(hWnd, &rcTop, FALSE);
}

static void ChrViewerDrawLine(FrameBuffer *fb, COLOR32 col, int x1, int y1, int x2, int y2) {
	//compute deltas
	int dx = x2 - x1, dy = y2 - y1;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	//if dx and dy are zero, put one pixel (avoid divide by zero)
	if (dx == 0 && dy == 0) {
		if (x1 >= 0 && y1 >= 0 && x1 < fb->width && y1 < fb->height) {
			fb->px[x1 + y1 * fb->width] = col;
		}
		return;
	}

	//draw horizontally or vertically
	if (dx >= dy) {
		//draw left->right
		if (x2 < x1) SwapPoints(&x1, &y1, &x2, &y2);

		//scan
		for (int i = 0; i <= dx; i++) {
			int px = i + x1;
			int py = ((i * (y2 - y1)) * 2 + dx) / (dx * 2) + y1;
			if (px >= 0 && py >= 0 && px < fb->width && py < fb->height) {
				fb->px[px + py * fb->width] = col;
			}
		}
	} else {
		//draw top->bottom. ensure top point first
		if (y2 < y1) SwapPoints(&x1, &y1, &x2, &y2);

		//scan
		for (int i = 0; i <= dy; i++) {
			int px = ((i * (x2 - x1)) * 2 + dy) / (dy * 2) + x1;
			int py = i + y1;
			if (px >= 0 && py >= 0 && px < fb->width && py < fb->height) {
				fb->px[px + py * fb->width] = col;
			}
		}
	}
}



static void ChrViewerGetGraphicsSize(NCGRVIEWERDATA *data, int *width, int *height) {
	*width = data->ncgr.tilesX * (8 * data->scale);
	*height = data->ncgr.tilesY * (8 * data->scale);
}

static int ChrViewerHitTest(NCGRVIEWERDATA *data, int x, int y) {
	//check margins
	if (x < 0 || y < 0) {
		if (x == -1 && y == -1) return HIT_NOWHERE;

		//if both less than zero, hit corner
		if (x < 0 && y < 0) return HIT_MARGIN | HIT_MARGIN_LEFT | HIT_MARGIN_TOP;

		//sides
		if (x < 0) return HIT_MARGIN | HIT_MARGIN_LEFT;
		if (y < 0) return HIT_MARGIN | HIT_MARGIN_TOP;

		return HIT_MARGIN;
	}

	//if the point is outside of the client area of the viewer, hit test should return nowhere.
	RECT rcClientViewer;
	GetClientRect(data->hWndViewer, &rcClientViewer);
	if (x >= (rcClientViewer.right) || y >= (rcClientViewer.bottom)) {
		return HIT_NOWHERE;
	}

	int tileSize = 8 * data->scale;

	//get scroll info
	int scrollX, scrollY;
	ChrViewerGetScroll(data, &scrollX, &scrollY);

	//if we have a selection, test for hits.
	if (ChrViewerHasSelection(data)) {

		//get selection bounds in client area
		int tileSize = 8 * data->scale;
		int selX1 = (min(data->selStartX, data->selEndX) + 0) * tileSize - scrollX - SEL_BORDER_THICKNESS/2; //padding for convenience
		int selX2 = (max(data->selStartX, data->selEndX) + 1) * tileSize - scrollX + SEL_BORDER_THICKNESS/2;
		int selY1 = (min(data->selStartY, data->selEndY) + 0) * tileSize - scrollY - SEL_BORDER_THICKNESS/2;
		int selY2 = (max(data->selStartY, data->selEndY) + 1) * tileSize - scrollY + SEL_BORDER_THICKNESS/2;
		if (x >= selX1 && x < selX2 && y >= selY1 && y < selY2) {
			//within selection bounds
			int hit = HIT_SEL;

			if (x < (selX1 + SEL_BORDER_THICKNESS)) hit |= HIT_SEL_LEFT;
			if (x >= (selX2 - SEL_BORDER_THICKNESS)) hit |= HIT_SEL_RIGHT;
			if (y < (selY1 + SEL_BORDER_THICKNESS)) hit |= HIT_SEL_TOP;
			if (y >= (selY2 - SEL_BORDER_THICKNESS)) hit |= HIT_SEL_BOTTOM;

			if (x >= (selX1 + SEL_BORDER_THICKNESS) && x < (selX2 - SEL_BORDER_THICKNESS)
				&& y >= (selY1 + SEL_BORDER_THICKNESS) && y < (selY2 - SEL_BORDER_THICKNESS)) {
				hit |= HIT_SEL_CONTENT;
			}

			return hit;
		}
	}

	//no selection hit. try content hit.
	int contentWidth = data->ncgr.tilesX * tileSize;
	int contentHeight = data->ncgr.tilesY * tileSize;
	if ((x + scrollX) < contentWidth && (y + scrollY) < contentHeight) {
		//content hit
		return HIT_CONTENT;
	}

	return HIT_NOWHERE;
}

static void ChrViewerExportBitmap(NCGR *ncgr, NCLR *nclr, int paletteIndex, LPCWSTR path) {
	//convert to bitmap layout
	int tilesX = ncgr->tilesX, tilesY = ncgr->tilesY;
	int width = tilesX * 8, height = tilesY * 8;
	unsigned char *bits = (unsigned char *) calloc(width * height, 1);
	for (int tileY = 0; tileY < tilesY; tileY++) {
		for (int tileX = 0; tileX < tilesX; tileX++) {
			unsigned char *tile = ncgr->tiles[tileX + tileY * tilesX];
			for (int y = 0; y < 8; y++) {
				memcpy(bits + tileX * 8 + (tileY * 8 + y) * width, tile + y * 8, 8);
			}
		}
	}

	//convert palette
	int depth = ncgr->nBits;
	int paletteSize = 1 << depth;
	int paletteStart = paletteIndex << depth;
	if (paletteStart + paletteSize > nclr->nColors) {
		paletteSize = nclr->nColors - paletteStart;
	}
	if (paletteSize < 0) paletteSize = 0;
	COLOR32 *pal = (COLOR32 *) calloc(paletteSize, sizeof(COLOR32));
	for (int i = 0; i < paletteSize; i++) {
		COLOR32 c = ColorConvertFromDS(nclr->colors[paletteStart + i]);
		if (i) c |= 0xFF000000;
		pal[i] = c;
	}

	ImgWriteIndexed(bits, width, height, pal, paletteSize, path);

	free(bits);
	free(pal);
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
	if (!ChrViewerHasSelection(data)) {
		wsprintf(buffer, L" Character %d", data->hoverIndex);
	} else {
		int selX, selY, selW, selH;
		ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);
		wsprintf(buffer, L" Character %d  Selection: (%d, %d), (%d, %d)", data->hoverIndex, selX, selY, selW, selH);
	}
	SendMessage(data->hWndCharacterLabel, WM_SETTEXT, wcslen(buffer), (LPARAM) buffer);
}

static void ChrViewerSetWidth(HWND hWnd, int width) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	//update width
	ChrSetWidth(&data->ncgr, width);

	//update UI and check bounds
	if (data->selStartX >= data->ncgr.tilesX) data->selStartX = data->ncgr.tilesX - 1;
	if (data->selEndX >= data->ncgr.tilesX) data->selEndX = data->ncgr.tilesX - 1;
	if (data->selStartY >= data->ncgr.tilesY) data->selStartY = data->ncgr.tilesY - 1;
	if (data->selEndY >= data->ncgr.tilesY) data->selEndY = data->ncgr.tilesY - 1;
	ChrViewerRefreshMargins(data->hWnd);
	ChrViewerUpdateCursor(data);

	//update
	ChrViewerPopulateWidthField(hWnd);
	SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
	ChrViewerGraphicsUpdated(data);
}

static void ChrViewerSetDepth(HWND hWnd, int depth) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	//set depth and update UI
	ChrSetDepth(&data->ncgr, depth);
	ChrViewerPopulateWidthField(hWnd);
	SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
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
	data->frameData.paddingBottom = 21 * 2;
	data->mode = CHRVIEWER_MODE_SELECT;
	data->lastMode = CHRVIEWER_MODE_PEN;
	data->showBorders = 1;
	data->scale = 2; //default 200%
	data->selectedPalette = 0;
	data->hoverX = -1;
	data->hoverY = -1;
	data->mouseX = data->lastMouseX = -1;
	data->mouseY = data->lastMouseY = -1;
	data->hoverIndex = -1;
	data->selStartX = -1;
	data->selStartY = -1;
	data->selEndX = -1;
	data->selEndY = -1;
	data->transparent = g_configuration.renderTransparent;

	data->hWndViewer = CreateWindow(L"NcgrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 
		MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, 256, 256, hWnd, NULL, NULL, NULL);
	data->hWndCharacterLabel = CreateStatic(hWnd, L" Character 0", 0, 0, 100, 22);
	data->hWndPaletteDropdown = CreateCombobox(hWnd, NULL, 0, 0, 0, 200, 100, 0);
	data->hWndWidthDropdown = CreateCombobox(hWnd, NULL, 0, 0, 0, 200, 100, 0);
	data->hWndWidthLabel = CreateStatic(hWnd, L" Width:", 0, 0, 100, 21);
	data->hWndExpand = CreateButton(hWnd, L"Resize", 0, 0, 100, 22, FALSE);
	data->hWnd8bpp = CreateCheckbox(hWnd, L"8bpp", 0, 0, 100, 22, FALSE);

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

static void ChrViewerOnInitialize(HWND hWnd, LPCWSTR path, NCGR *ncgr, int immediate) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);
	float dpiScale = GetDpiScale();

	if (!immediate) {
		memcpy(&data->ncgr, ncgr, sizeof(NCGR));
		EditorSetFile(hWnd, path);
	} else {
		memcpy(&data->ncgr, ncgr, sizeof(NCGR));
	}
	SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	int controlHeight = (int) (dpiScale * 21.0f + 0.5f);
	int controlWidth = (int) (dpiScale * 100.0f + 0.5f);
	ChrViewerGetGraphicsSize(data, &data->frameData.contentWidth, &data->frameData.contentHeight);
	FbCreate(&data->fb, hWnd, data->frameData.contentWidth, data->frameData.contentHeight);
	FbCreate(&data->fbMargin, hWnd, rcClient.right, rcClient.bottom);

	int width = data->frameData.contentWidth + GetSystemMetrics(SM_CXVSCROLL) + 4 + MARGIN_TOTAL_SIZE;
	int height = data->frameData.contentHeight + 3 * controlHeight + GetSystemMetrics(SM_CYHSCROLL) + 4 + MARGIN_TOTAL_SIZE;
	if (width < 255 + 4) width = 255 + 4; //min width for controls
	SetWindowSize(hWnd, width, height);


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

	MoveWindow(data->hWndViewer, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, rcClient.right - MARGIN_TOTAL_SIZE, viewHeight - MARGIN_TOTAL_SIZE, FALSE);
	MoveWindow(data->hWndCharacterLabel, 0, viewHeight, rcClient.right, controlHeight, TRUE);
	MoveWindow(data->hWndPaletteDropdown, 0, viewHeight + controlHeight * 2, controlWidth * 3 / 2, controlHeight, TRUE);
	MoveWindow(data->hWndWidthDropdown, controlWidth / 2, viewHeight + controlHeight, controlWidth, controlHeight, TRUE);
	MoveWindow(data->hWndWidthLabel, 0, viewHeight + controlHeight, controlWidth / 2, controlHeight, FALSE);
	MoveWindow(data->hWndExpand, 5 + controlWidth * 3 / 2, viewHeight + controlHeight, controlWidth, controlHeight, TRUE);
	MoveWindow(data->hWnd8bpp, 5 + controlWidth * 3 / 2, viewHeight + controlHeight * 2, controlWidth, controlHeight, TRUE);

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
	FbDestroy(&data->fb);
	FbDestroy(&data->fbMargin);
}

static int ChrViewerOnTimer(HWND hWnd, int idTimer) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	if (idTimer == 1) {
		data->verifyFrames--;
		if (!data->verifyFrames) {
			KillTimer(hWnd, idTimer);
		}
		InvalidateRect(data->hWndViewer, NULL, FALSE);
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
		SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
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
	SetEditNumber(cidata->hWndMaxChars, data->ncgr.nTiles - (data->contextHoverX + pasteY * pasteX));

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

static void ChrViewerOnMenuCommand(HWND hWnd, int idMenu) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) EditorGetData(hWnd);

	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
		case ID_ZOOM_100:
		case ID_ZOOM_200:
		case ID_ZOOM_400:
		case ID_ZOOM_800:
		case ID_ZOOM_1600:
			SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			ChrViewerRefreshMargins(data->hWnd);
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
			if (ChrViewerHasSelection(data)) {
				int selW, selH;
				ChrViewerGetSelectionBounds(data, &pasteX, &pasteY, &selW, &selH);
			} else {
				pasteX = data->contextHoverX;
				pasteY = data->contextHoverY;
			}

			ChrViewerImportDialog(data, TRUE, pasteX, pasteY, px, width, height);

			//do not free px: owned by the convert process now
			break;
		}
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

			ChrViewerExportBitmap(ncgr, nclr, data->selectedPalette, location);
			free(location);
			break;
		}
		case ID_NCGRMENU_COPY:
			ChrViewerCopy(data);
			break;
		case ID_NCGRMENU_PASTE:
			ChrViewerPaste(data);
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
			ChrViewerPaste(data);
			ChrViewerGraphicsUpdated(data);
			break;
		case ID_ACCELERATOR_CUT:
			ChrViewerCut(data);
			ChrViewerGraphicsUpdated(data);
			break;
		case ID_ACCELERATOR_DESELECT:
			ChrViewerDeselect(data);
			break;
		case ID_ACCELERATOR_SELECT_ALL:
			ChrViewerSelectAll(data);
			break;
	}
	InvalidateRect(data->hWndViewer, NULL, FALSE);
	ChrViewerRefreshMargins(hWnd);
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
	InvalidateRect(data->hWndViewer, NULL, FALSE);

	//margin dimensions
	int marginSize = MARGIN_SIZE;
	int marginBorderSize = MARGIN_BORDER_SIZE;
	int tileSize = 8 * data->scale;

	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(hWnd, &ps);

	//exclude clip rect
	ExcludeClipRect(hDC, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, rcClient.right, rcClient.bottom);
	
	//get render size
	int renderWidth = rcClient.right;
	int renderHeight = rcClient.bottom;
	int viewWidth = renderWidth - MARGIN_TOTAL_SIZE;
	int viewHeight = renderHeight - MARGIN_TOTAL_SIZE;

	//create framebuffer
	FbSetSize(&data->fbMargin, renderWidth, renderHeight);

	//get mouse coord
	POINT mouse;
	mouse.x = data->mouseX;
	mouse.y = data->mouseY;

	//get scroll pos
	int scrollX, scrollY;
	ChrViewerGetScroll(data, &scrollX, &scrollY);

	//get hovered row/column
	int hovRow = data->hoverY, hovCol = data->hoverX;

	//get hit test
	int hit = ChrViewerHitTest(data, data->mouseX, data->mouseY);

	//render guide margins
	{
		//draw top margin
		for (int y = 0; y < MARGIN_TOTAL_SIZE; y++) {
			for (int x = marginSize; x < renderWidth; x++) {
				COLOR32 col = 0x000000;

				if (x >= MARGIN_TOTAL_SIZE) {
					int curCol = (x - MARGIN_TOTAL_SIZE + scrollX) / (8 * data->scale);
					BOOL inSel = (curCol >= min(data->selStartX, data->selEndX)) && (curCol <= max(data->selStartX, data->selEndX));

					if (inSel) col = 0x808000; //indicate selection
					if (curCol == hovCol) {
						if (!inSel) col = 0x800000; //indicate hovered
						else col = 0xC04000;
					}
				}

				//border pixels
				if (y >= marginSize) {
					col = RGB(160, 160, 160);
					if (y == (marginSize + 1)) col = RGB(105, 105, 105);
				}

				if (x < renderWidth && y < renderHeight) {
					data->fbMargin.px[y * renderWidth + x] = col;
				}
			}
		}

		//draw left margin
		for (int y = marginSize; y < renderHeight; y++) {
			for (int x = 0; x < MARGIN_TOTAL_SIZE; x++) {
				if (x >= y) continue;
				COLOR32 col = 0x000000;

				if (y >= MARGIN_TOTAL_SIZE) {
					int curRow = (y - MARGIN_TOTAL_SIZE + scrollY) / (8 * data->scale);
					BOOL inSel = (curRow >= min(data->selStartY, data->selEndY)) && (curRow <= max(data->selStartY, data->selEndY));

					if (inSel) col = 0x808000; //indicate selection
					if (curRow == hovRow) {
						if (!inSel) col = 0x800000; //indicate hovered
						else col = 0xC04000;
					}
				}

				//border pixels
				if (x >= marginSize) {
					col = RGB(160, 1160, 160);
					if (x == (marginSize + 1)) col = RGB(105, 105, 105);
				}

				if (x < renderWidth && y < renderHeight) {
					data->fbMargin.px[y * renderWidth + x] = col;
				}
			}
		}

		//draw ticks
		int tickHeight = 4;
		for (int x = 0; x < viewWidth; x++) {
			if (((x + scrollX) % (8 * data->scale)) == 0) {
				//tick
				ChrViewerDrawLine(&data->fbMargin, 0xFFFFFF, x + MARGIN_TOTAL_SIZE, 0, x + MARGIN_TOTAL_SIZE, tickHeight - 1);
			}
		}
		for (int y = 0; y < viewHeight; y++) {
			if (((y + scrollY) % (8 * data->scale)) == 0) {
				ChrViewerDrawLine(&data->fbMargin, 0xFFFFFF, 0, y + MARGIN_TOTAL_SIZE, tickHeight - 1, y + MARGIN_TOTAL_SIZE);
			}
		}

		//draw selection edges
		if (data->selStartX != -1 && data->selStartY != -1) {
			int selX1 = (min(data->selStartX, data->selEndX) + 0) * tileSize + MARGIN_TOTAL_SIZE - scrollX;
			int selX2 = (max(data->selStartX, data->selEndX) + 1) * tileSize + MARGIN_TOTAL_SIZE - scrollX;
			int selY1 = (min(data->selStartY, data->selEndY) + 0) * tileSize + MARGIN_TOTAL_SIZE - scrollY;
			int selY2 = (max(data->selStartY, data->selEndY) + 1) * tileSize + MARGIN_TOTAL_SIZE - scrollY;
			ChrViewerDrawLine(&data->fbMargin, 0xFFFF00, selX1, 0, selX1, MARGIN_SIZE - 1);
			ChrViewerDrawLine(&data->fbMargin, 0xFFFF00, selX2, 0, selX2, MARGIN_SIZE - 1);
			ChrViewerDrawLine(&data->fbMargin, 0xFFFF00, 0, selY1, MARGIN_SIZE - 1, selY1);
			ChrViewerDrawLine(&data->fbMargin, 0xFFFF00, 0, selY2, MARGIN_SIZE - 1, selY2);
		}

		//draw mouse pos?
		if (mouse.x != -1 && mouse.y != -1) {
			ChrViewerDrawLine(&data->fbMargin, 0xFF0000, mouse.x + MARGIN_TOTAL_SIZE, 0, mouse.x + MARGIN_TOTAL_SIZE, marginSize - 1);
			ChrViewerDrawLine(&data->fbMargin, 0xFF0000, 0, mouse.y + MARGIN_TOTAL_SIZE, marginSize - 1, mouse.y + MARGIN_TOTAL_SIZE);
		}
	}

	//fill top-left corner
	COLOR32 cornerColor = 0x000000;
	if ((hit & HIT_TYPE_MASK) == HIT_MARGIN && (hit & HIT_MARGIN_LEFT) && (hit & HIT_MARGIN_TOP)) cornerColor = 0x800000;

	for (int y = 0; y < MARGIN_TOTAL_SIZE; y++) {
		for (int x = 0; x < MARGIN_TOTAL_SIZE; x++) {
			if (x < MARGIN_SIZE || y < MARGIN_SIZE) {
				if (x < renderWidth && y < renderHeight) {
					data->fbMargin.px[x + y * renderWidth] = cornerColor;
				}
			}
		}
	}

	//blit
	FbDraw(&data->fbMargin, hDC, 0, 0, renderWidth, MARGIN_TOTAL_SIZE, 0, 0);
	FbDraw(&data->fbMargin, hDC, 0, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, renderHeight - MARGIN_TOTAL_SIZE, 0, MARGIN_TOTAL_SIZE);
	EndPaint(hWnd, &ps);
}

static void ChrViewerUpdateCursor(NCGRVIEWERDATA *data) {
	HWND hWnd = data->hWndViewer;

	//get scroll pos
	int scrollX, scrollY;
	ChrViewerGetScroll(data, &scrollX, &scrollY);

	//get pixel coordinate
	int pxX = -1, pxY = -1;
	if (data->mouseOver) {
		pxX = (data->mouseX + scrollX) / data->scale;
		pxY = (data->mouseY + scrollY) / data->scale;
	}

	int curHit = ChrViewerHitTest(data, data->mouseX, data->mouseY);

	//if mouse is hovered, get hoeverd character pos
	if (data->mouseOver && (curHit & HIT_FLAGS_MASK) != HIT_NOWHERE) {
		data->hoverX = (data->mouseX + scrollX) / (8 * data->scale);
		data->hoverY = (data->mouseY + scrollY) / (8 * data->scale);
	} else {
		//un-hover
		data->hoverX = -1;
		data->hoverY = -1;
	}

	//if a tile is hovered, set hovered index.
	int lastHoveredIndex = data->hoverIndex;
	if (data->hoverX != -1 && data->hoverY != -1 && curHit != HIT_NOWHERE) {
		data->hoverIndex = data->hoverX + data->hoverY * data->ncgr.tilesX;
	} else {
		data->hoverIndex = -1;
	}

	if (data->mouseDown) {

		//get mouse movement if mouse down
		int dx = data->mouseX - data->dragStartX;
		int dy = data->mouseY - data->dragStartY;

		//get mouse drag start position if mouse down
		int dragStartTileX = (data->dragStartX + scrollX) / (8 * data->scale);
		int dragStartTileY = (data->dragStartY + scrollY) / (8 * data->scale);

		//check hit type for mouse-down.
		int hit = data->mouseDownHit;
		int hitType = hit & HIT_TYPE_MASK;

		//if the mouse hits the selection, handle selection gesture.
		if (hitType == HIT_SEL) {
			//if the mouse is down, check hit flags.
			if (hit & HIT_SEL_CONTENT) {
				//mouse-down on content, carry out drag procedure.
				int nextX = data->hoverX - dragStartTileX + data->selDragStartX;
				int nextY = data->hoverY - dragStartTileY + data->selDragStartY;

				data->selEndX = nextX + data->selEndX - data->selStartX;
				data->selEndY = nextY + data->selEndY - data->selStartY;
				data->selStartX = nextX;
				data->selStartY = nextY;

				//get selection bound
				int selX, selY, selWidth, selHeight;
				ChrViewerGetSelectionBounds(data, &selX, &selY, &selWidth, &selHeight);

				//check bounds of movement
				int dx = 0, dy = 0;
				if (selX < 0) dx = -selX;
				if ((selX + selWidth) > data->ncgr.tilesX) dx = -(selX + selWidth - data->ncgr.tilesX);
				if (selY < 0) dy = -selY;
				if ((selY + selHeight) > data->ncgr.tilesY) dy = -(selY + selHeight - data->ncgr.tilesY);

				ChrViewerOffsetSelection(data, dx, dy);
			} else {
				//hit on selection border.

				int moveX = (hit & HIT_SEL_LEFT) || (hit & HIT_SEL_RIGHT);
				int moveY = (hit & HIT_SEL_TOP) || (hit & HIT_SEL_BOTTOM);
				if (moveX) data->selEndX = data->hoverX;
				if (moveY) data->selEndY = data->hoverY;
			}
		} else {

			switch (data->mode) {
				case CHRVIEWER_MODE_SELECT:
				{
					//if in content, start or continue a selection.
					if (hitType == HIT_CONTENT) {
						//if the mouse is down, start or continue a selection.
						if (data->selStartX == -1 || data->selStartY == -1) {
							data->selStartX = data->hoverX;
							data->selStartY = data->hoverY;

							//set cursor to crosshair
							SetCursor(LoadCursor(NULL, IDC_CROSS));
						}

						data->selEndX = data->hoverX;
						data->selEndY = data->hoverY;
					}
					break;
				}
				case CHRVIEWER_MODE_PEN:
				{
					int pcol = ChrViewerGetSelectedColor(data);
					if (pcol != -1) {
						if (data->lastMouseX != -1 && data->lastMouseY != -1) {
							//connect last point
							int lastPxX = (data->lastMouseX + scrollX) / data->scale;
							int lastPxY = (data->lastMouseY + scrollY) / data->scale;
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
					if (ChrViewerHasSelection(data)) {
						ChrViewerStampTile(data, data->hoverX, data->hoverY);
					}
					break;
				}
			}
		}
	}

	if (data->hoverIndex != lastHoveredIndex) {
		InvalidateAllEditors(getMainWindow(data->hWnd), FILE_TYPE_SCREEN); //update screen viewers
		ChrViewerUpdateCharacterLabel(data->hWnd);
	}

	//repaint viewer and update margin rendering
	InvalidateRect(hWnd, NULL, FALSE);
	ChrViewerRefreshMargins(data->hWnd);
}

static void ChrViewerReleaseCursor(NCGRVIEWERDATA *data) {
	data->mouseDown = FALSE;
	data->mouseDownTop = FALSE;
	data->mouseDownLeft = FALSE;

	ReleaseCapture();
	ChrViewerUpdateCursor(data);
}

static void ChrViewerMainOnMouseMove(NCGRVIEWERDATA *data, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	//if mouse left, update
	if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE) {
		//check the last mouse event window handle. This makes sure the mouse can move from parent
		//to child seamlessly without one message interfering with the other.
		if (data->hWndLastMouse == hWnd || data->hWndLastMouse == NULL) {
			data->mouseX = data->lastMouseX = -1;
			data->mouseY = data->lastMouseY = -1;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;
			data->hWndLastMouse = NULL;
			data->mouseOver = FALSE;
			ChrViewerUpdateCharacterLabel(data->hWnd);
		}
	} else {
		data->hWndLastMouse = hWnd;
		data->mouseOver = TRUE;
		data->lastMouseX = data->mouseX;
		data->lastMouseY = data->mouseY;
		data->mouseX = ((short) LOWORD(lParam)) - MARGIN_TOTAL_SIZE;
		data->mouseY = ((short) HIWORD(lParam)) - MARGIN_TOTAL_SIZE;
		data->hoverIndex = -1;

		//get client rect
		RECT rcClient;
		GetClientRect(hWnd, &rcClient);

		//get scroll info
		int scrollX, scrollY;
		ChrViewerGetScroll(data, &scrollX, &scrollY);

		if (data->mouseDown && (data->mouseDownTop || data->mouseDownLeft)) {
			//clamp mouse pos to editor area
			if (data->mouseX < 0) data->mouseX = 0;
			if (data->mouseY < 0) data->mouseY = 0;
		}
		int curCol = (data->mouseX + scrollX) / (8 * data->scale);
		int curRow = (data->mouseY + scrollY) / (8 * data->scale);

		//if the mouse is down, handle gesture
		if (data->mouseDown) {
			if (data->mouseDownTop) {
				//clamp mouse position
				if (data->mouseY >= 0) data->mouseY = -2;
				if (data->mouseY < -MARGIN_TOTAL_SIZE) data->mouseY = -MARGIN_TOTAL_SIZE;
				if (data->mouseX < 0) data->mouseX = 0;

				//update selection
				data->selEndX = curCol;
			}
			if (data->mouseDownLeft) {
				//clamp mouse position
				if (data->mouseX >= 0) data->mouseX = -2;
				if (data->mouseX < -MARGIN_TOTAL_SIZE) data->mouseX = -MARGIN_TOTAL_SIZE;
				if (data->mouseY < 0) data->mouseY = 0;

				//update selection
				data->selEndY = curRow;
			}
		}

		//check the mouse over the margins. If so, hover that row and/or column.
		BOOL inLeft = data->mouseX >= -MARGIN_TOTAL_SIZE && data->mouseX < 0;
		BOOL inTop = data->mouseY >= -MARGIN_TOTAL_SIZE && data->mouseY < 0;

		if (inLeft && !inTop) {
			//on left margin
			data->hoverY = (data->mouseY + scrollY) / (8 * data->scale);
			data->hoverX = -1;
		}
		if (inTop && !inLeft) {
			//on top margin
			data->hoverX = (data->mouseX + scrollX) / (8 * data->scale);
			data->hoverY = -1;
		}
		if (inTop && inLeft) {
			//on topleft corner
			data->hoverX = -1;
			data->hoverY = -1;
		}
	}

	//notify of mouse leave if we're not already processing a mouse leave
	if (msg != WM_MOUSELEAVE && msg != WM_NCMOUSELEAVE) {
		TRACKMOUSEEVENT tme = { 0 };
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = hWnd;
		TrackMouseEvent(&tme);
	}

	ChrViewerRefreshMargins(hWnd);
}

static void ChrViewerMainOnLButtonDown(NCGRVIEWERDATA *data) {
	data->mouseDown = TRUE;
	data->dragStartX = data->mouseX;
	data->dragStartY = data->mouseY;
	data->selDragStartX = data->selStartX;
	data->selDragStartY = data->selStartY;
	data->mouseDownHit = ChrViewerHitTest(data, data->mouseX, data->mouseY);

	int hit = data->mouseDownHit;
	if (hit == HIT_NOWHERE) {
		//if in selection mode, clear selection.
		if (data->mode == CHRVIEWER_MODE_SELECT) ChrViewerDeselect(data);
		ChrViewerReleaseCursor(data);
		ChrViewerUpdateCharacterLabel(data->hWnd);
		return;
	}

	//get scroll info
	int scrollX, scrollY;
	ChrViewerGetScroll(data, &scrollX, &scrollY);

	//get content view size
	int contentW, contentH;
	ChrViewerGetGraphicsSize(data, &contentW, &contentH);
	contentW -= scrollX;
	contentH -= scrollY;

	int curRow = (data->mouseY + scrollY) / (8 * data->scale);
	int curCol = (data->mouseX + scrollX) / (8 * data->scale);

	if ((hit & HIT_TYPE_MASK) == HIT_MARGIN) {
		int hitWhere = hit & HIT_FLAGS_MASK;
		if (hitWhere == HIT_MARGIN_LEFT && data->mouseX < contentH) {
			data->mouseDownLeft = TRUE;

			//start or edit a selection
			data->selStartY = curRow;
			data->selEndY = curRow;
			if (data->selStartX == -1 || data->selEndX == -1) {
				data->selStartX = 0;
				data->selEndX = data->ncgr.tilesX - 1;
			}
		}
		if (hitWhere == HIT_MARGIN_TOP && data->mouseX < contentW) {
			data->mouseDownTop = TRUE;

			//start or edit a selection
			data->selStartX = curCol;
			data->selEndX = curCol;
			if (data->selStartY == -1 || data->selEndY == -1) {
				data->selStartY = 0;
				data->selEndY = data->ncgr.tilesY - 1;
			}
		}
		if (hitWhere == (HIT_MARGIN_TOP | HIT_MARGIN_LEFT)) {
			//select all
			if (!ChrViewerIsSelectedAll(data)) {
				ChrViewerSelectAll(data);
			} else {
				ChrViewerDeselect(data);
			}
		}
		ChrViewerUpdateCharacterLabel(data->hWnd);
	}

	//set capture
	SetCapture(data->hWnd);
}

static void ChrViewerMainOnLButtonUp(NCGRVIEWERDATA *data) {
	ChrViewerReleaseCursor(data);
}

static BOOL ChrViewerSetCursor(NCGRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	if (LOWORD(lParam) != HTCLIENT) {
		//nonclient area: default processing
		return DefWindowProc(data->hWndViewer, WM_SETCURSOR, wParam, lParam);
	}

	//get mouse coordinates current: prevent outdated mouse coordinates from message order
	POINT mouse;
	GetCursorPos(&mouse);
	ScreenToClient(data->hWndViewer, &mouse);

	//test hit
	int hit = ChrViewerHitTest(data, mouse.x, mouse.y);
	int type = hit & HIT_TYPE_MASK;

	//nowhere: default processing
	if (type == HIT_NOWHERE) return DefWindowProc(data->hWndViewer, WM_SETCURSOR, wParam, lParam);

	//content: decide based on edit mode
	if (type == HIT_CONTENT) {
		HCURSOR hCursor = NULL;
		switch (data->mode) {
			case CHRVIEWER_MODE_SELECT: hCursor = LoadCursor(NULL, IDC_ARROW); break;
			case CHRVIEWER_MODE_PEN: hCursor = LoadCursor(NULL, MAKEINTRESOURCE(32631)); break; //pencil cursor
			case CHRVIEWER_MODE_FILL: hCursor = LoadCursor(NULL, IDC_CROSS); break;
			case CHRVIEWER_MODE_EYEDROP: hCursor = LoadCursor(NULL, IDC_UPARROW);  break;
			case CHRVIEWER_MODE_STAMP: hCursor = LoadCursor(NULL, IDC_HAND);  break;
			default: hCursor = LoadCursor(NULL, IDC_NO); break; //error
		}
		SetCursor(hCursor);
		return TRUE;
	}

	//selection: set cursor
	if (type == HIT_SEL) {
		if (hit & HIT_SEL_CONTENT) {
			//content, set cursor to move cursor
			SetCursor(LoadCursor(NULL, IDC_SIZEALL));
			return TRUE;
		}

		int winHit = HTCLIENT;

		//convert to win32 hit type
		int border = hit & HIT_FLAGS_MASK;
		switch (border) {
			case HIT_SEL_TOP: winHit = HTTOP; break;
			case HIT_SEL_LEFT: winHit = HTLEFT; break;
			case HIT_SEL_RIGHT: winHit = HTRIGHT; break;
			case HIT_SEL_BOTTOM: winHit = HTBOTTOM; break;
			case HIT_SEL_TOP | HIT_SEL_LEFT: winHit = HTTOPLEFT; break;
			case HIT_SEL_TOP | HIT_SEL_RIGHT: winHit = HTTOPRIGHT; break;
			case HIT_SEL_BOTTOM | HIT_SEL_LEFT: winHit = HTBOTTOMLEFT; break;
			case HIT_SEL_BOTTOM | HIT_SEL_RIGHT: winHit = HTBOTTOMRIGHT; break;
		}

		//pass off default behavior for a sizing border hit
		return DefWindowProc(data->hWndViewer, WM_SETCURSOR, wParam, MAKELONG(winHit, HIWORD(lParam)));
	}

	//default processing
	return DefWindowProc(data->hWndViewer, WM_SETCURSOR, wParam, lParam);
}

static void ChrViewerOnKeyDown(NCGRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	int selX, selY, selW, selH;
	ChrViewerGetSelectionBounds(data, &selX, &selY, &selW, &selH);

	switch (wParam) {
		case VK_RETURN:
			SendMessage(data->hWnd, WM_COMMAND, ID_NCGRMENU_IMPORTBITMAPHERE, 0);
			break;
		case VK_DELETE:
		{
			if (ChrViewerHasSelection(data)) {
				unsigned char fill[64] = { 0 };
				ChrViewerFill(data, selX, selY, selW, selH, fill);
			}
			ChrViewerDeselect(data);
			ChrViewerRefreshMargins(data->hWnd);
			ChrViewerGraphicsUpdated(data);
			ChrViewerUpdateCharacterLabel(data->hWnd);
			break;
		}
		case VK_ESCAPE:
		{
			//deselect
			ChrViewerDeselect(data);
			ChrViewerRefreshMargins(data->hWnd);
			ChrViewerGraphicsUpdated(data);
			ChrViewerUpdateCharacterLabel(data->hWnd);
			break;
		}
		case ' ':
		{
			if (ChrViewerHasSelection(data)) {
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
		case VK_UP:
		case VK_DOWN:
		case VK_LEFT:
		case VK_RIGHT:
		{
			if (ChrViewerHasSelection(data)) {
				int shift = GetKeyState(VK_SHIFT) < 0;

				int dx = 0, dy = 0;
				switch (wParam) {
					case VK_UP: dy = -1; break;
					case VK_DOWN: dy = 1; break;
					case VK_LEFT: dx = -1; break;
					case VK_RIGHT: dx = 1; break;
				}

				if (!shift) {
					//offset whole selection
					int newX = selX + dx, newY = selY + dy;
					if (newX >= 0 && newY >= 0 && (newX + selW) <= data->ncgr.tilesX && (newY + selH) <= data->ncgr.tilesY) {
						ChrViewerOffsetSelection(data, dx, dy);
					}
				} else {
					//offset selection end
					int newX = data->selEndX + dx, newY = data->selEndY + dy;
					if (newX >= 0 && newX < data->ncgr.tilesX && newY >= 0 && newY < data->ncgr.tilesY) {
						data->selEndX += dx;
						data->selEndY += dy;
					}
				}

				InvalidateRect(data->hWndViewer, NULL, FALSE);
				ChrViewerRefreshMargins(data->hWnd);
				ChrViewerUpdateCursor(data);
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
			ChrViewerMainOnLButtonDown(data);
			break;
		case WM_LBUTTONUP:
			ChrViewerMainOnLButtonUp(data);
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





static void ChrViewerPaintView(NCGRVIEWERDATA *data, HWND hWnd) {
	//margin dimensions
	int marginSize = MARGIN_SIZE;
	int marginBorderSize = MARGIN_BORDER_SIZE;
	int tileSize = 8 * data->scale;

	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	NCGR *ncgr = &data->ncgr;
	NCLR *nclr = NULL;

	HWND hWndMain = getMainWindow(data->hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	if (nitroPaintStruct->hWndNclrViewer != NULL) {
		nclr = (NCLR *) EditorGetObject(nitroPaintStruct->hWndNclrViewer);
	}

	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(hWnd, &ps);

	int viewWidth = rcClient.right;
	int viewHeight = rcClient.bottom;
	FbSetSize(&data->fb, viewWidth, viewHeight);

	//get mouse coord
	POINT mouse;
	mouse.x = data->mouseX;
	mouse.y = data->mouseY;

	//hit test
	int hit = ChrViewerHitTest(data, mouse.x, mouse.y);
	int hitType = hit & HIT_TYPE_MASK;

	//get scroll pos
	int scrollX, scrollY;
	ChrViewerGetScroll(data, &scrollX, &scrollY);

	//get graphics bounding size
	int tilesX = ncgr->tilesX, tilesY = ncgr->tilesY;
	int renderWidth = tilesX * tileSize - scrollX;
	int renderHeight = tilesY * tileSize - scrollY;
	if (renderWidth > viewWidth) renderWidth = viewWidth;
	if (renderHeight > viewHeight) renderHeight = viewHeight;

	//get hovered row/column
	int hovRow = data->hoverY, hovCol = data->hoverX;

	//render character graphics
	int hlStart = data->verifyStart;
	int hlEnd = data->verifyEnd;
	int hlMode = data->verifySelMode;
	if ((data->verifyFrames & 1) == 0) hlStart = hlEnd = -1;

	{
		for (int y = 0; y < renderHeight; y++) {
			for (int x = 0; x < renderWidth; x++) {
				int srcX = (x + scrollX) / data->scale;
				int srcY = (y + scrollY) / data->scale;

				int srcTileX = srcX / 8;
				int srcTileY = srcY / 8;
				unsigned char *chr = ncgr->tiles[srcTileX + srcTileY * tilesX];
				int rawIdx = chr[(srcX % 8) + (srcY % 8) * 8];
				int idx = rawIdx + (data->selectedPalette << ncgr->nBits);

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

				data->fb.px[x + y * viewWidth] = REVERSE(col);
			}
		}
	}

	//mark highlighted tiles
	int selStartX = min(data->selStartX, data->selEndX);
	int selEndX = max(data->selStartX, data->selEndX);
	int selStartY = min(data->selStartY, data->selEndY);
	int selEndY = max(data->selStartY, data->selEndY);
	if (selStartX != -1 && selStartY != -1) {
		//tint selection tiles
		int blendR = 255, blendG = 255, blendB = 0, blendW = 1;
		if (hitType == HIT_SEL) {
			blendB = 127;
			blendW = 2;
		}

		for (int y = 0; y < renderHeight; y++) {
			for (int x = 0; x < renderWidth; x++) {
				int curRow = (y + scrollY) / tileSize;
				int curCol = (x + scrollX) / tileSize;

				if (curCol >= selStartX && curCol <= selEndX && curRow >= selStartY && curRow <= selEndY) {
					//mark selected
					COLOR32 col = data->fb.px[x + y * viewWidth];

					int b = (col >> 0) & 0xFF;
					int g = (col >> 8) & 0xFF;
					int r = (col >> 16) & 0xFF;
					r = (r + blendR * blendW + blendW / 2) / (blendW + 1);
					g = (g + blendG * blendW + blendW / 2) / (blendW + 1);
					b = (b + blendB * blendW + blendW / 2) / (blendW + 1);
					data->fb.px[x + y * viewWidth] = b | (g << 8) | (r << 16);
				}

			}
		}
	}

	//mark hovered tile
	if (hovRow != -1 && hovCol != -1 && data->mouseOver) {
		//mark tile
		if (data->mode == CHRVIEWER_MODE_SELECT || data->mode == CHRVIEWER_MODE_STAMP) {
			if (hitType != HIT_SEL) {
				for (int y = 0; y < tileSize; y++) {
					for (int x = 0; x < tileSize; x++) {
						int pxX = x - scrollX + hovCol * tileSize;
						int pxY = y - scrollY + hovRow * tileSize;

						if (pxX >= 0 && pxY >= 0 && pxX < renderWidth && pxY < renderHeight) {
							COLOR32 col = data->fb.px[pxX + pxY * viewWidth];

							//bit trick: average with white
							col = (col >> 1) | 0x808080;
							data->fb.px[pxX + pxY * viewWidth] = col;
						}
					}
				}
			}
		}
	} else if (hovRow != -1 || hovCol != -1) {
		//mark hovered row/column
		for (int y = 0; y < renderHeight; y++) {
			for (int x = 0; x < renderWidth; x++) {
				int curRow = (y + scrollY) / tileSize;
				int curCol = (x + scrollX) / tileSize;

				if (curRow == hovRow || curCol == hovCol) {
					COLOR32 col = data->fb.px[x + y * viewWidth];

					//bit trick: average with white
					col = (col >> 1) | 0x808080;
					data->fb.px[x + y * viewWidth] = col;
				}
			}
		}
	}

	//render gridlines
	if (data->showBorders) {
		//mark tile boundaries (deliberately do not mark row/col 0)
		for (int y = tileSize - (scrollY % tileSize); y < renderHeight; y += tileSize) {
			for (int x = 0; x < renderWidth; x++) {
				//invert the pixel if (x^y) is even
				if (((x ^ y) & 1) == 0) {
					data->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}
		for (int y = 0; y < renderHeight; y++) {
			for (int x = tileSize - (scrollX % tileSize); x < renderWidth; x += tileSize) {
				//invert the pixel if (x^y) is even
				if (((x ^ y) & 1) == 0) {
					data->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}
		for (int y = tileSize - (scrollY % tileSize); y < renderHeight; y += tileSize) {
			for (int x = tileSize - (scrollX % tileSize); x < renderWidth; x += tileSize) {
				//since we did the gridlines in two passes, pass over the intersections to flip them once more
				if (((x ^ y) & 1) == 0) {
					data->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}

		//if scale is >= 16x, mark each pixel
		if (data->scale >= 16) {
			int pxSize = data->scale;
			for (int y = pxSize - (scrollY % pxSize); y < renderHeight; y += pxSize) {
				if ((y + scrollY) % tileSize == 0) continue; //skip grid-marked rows

				for (int x = pxSize - (scrollX % pxSize); x < renderWidth; x += pxSize) {
					if ((x + scrollX) % tileSize == 0) continue; //skip grid-marked columns

					data->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}
	}


	//draw selection border
	if (selStartX != -1 && selStartY != -1) {
		int dx = -scrollX;
		int dy = -scrollY;
		ChrViewerDrawLine(&data->fb, 0xFFFF00, selStartX * tileSize + dx, selStartY * tileSize + dy,
			(selEndX + 1) * tileSize + dx, selStartY * tileSize + dy);
		ChrViewerDrawLine(&data->fb, 0xFFFF00, selStartX * tileSize + dx, selStartY * tileSize + dy,
			selStartX * tileSize + dx, (selEndY + 1) * tileSize + dy);
		ChrViewerDrawLine(&data->fb, 0xFFFF00, (selEndX + 1) * tileSize + dx, selStartY * tileSize + dy,
			(selEndX + 1) * tileSize + dx, (selEndY + 1) * tileSize + dy);
		ChrViewerDrawLine(&data->fb, 0xFFFF00, selStartX * tileSize + dx, (selEndY + 1) * tileSize + dy,
			(selEndX + 1) * tileSize + dx, (selEndY + 1) * tileSize + dy);
	}

	//draw background color
	if (renderHeight < viewHeight) {
		for (int y = renderHeight; y < viewHeight; y++) {
			for (int x = 0; x < viewWidth; x++) {
				data->fb.px[x + y * viewWidth] = 0xF0F0F0;
			}
		}
	}
	if (renderWidth < viewWidth) {
		for (int y = 0; y < renderHeight; y++) {
			for (int x = renderWidth; x < viewWidth; x++) {
				data->fb.px[x + y * viewWidth] = 0xF0F0F0;
			}
		}
	}

	FbDraw(&data->fb, hDC, 0, 0, viewWidth, viewHeight, 0, 0);
	EndPaint(hWnd, &ps);
}

static void ChrViewerOnMouseMove(NCGRVIEWERDATA *data, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	//update mouse coords
	if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE) {
		if (data->hWndLastMouse == hWnd || data->hWndLastMouse == NULL) {
			//mouse left client area: set pos to (-1, -1).
			data->mouseX = data->lastMouseX = -1;
			data->mouseY = data->lastMouseY = -1;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;
			data->hWndLastMouse = NULL;
			data->mouseOver = FALSE;
			ChrViewerUpdateCharacterLabel(data->hWnd);
		}
	} else {
		//mouse moved in client area.
		data->lastMouseX = data->mouseX;
		data->lastMouseY = data->mouseY;
		data->mouseX = (short) LOWORD(lParam);
		data->mouseY = (short) HIWORD(lParam);
		data->hWndLastMouse = hWnd;
		data->mouseOver = TRUE;

		//bounds check mouse position to client area if mouse down
		if (data->mouseDown) {
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			if (data->mouseX < 0) data->mouseX = 0;
			if (data->mouseY < 0) data->mouseY = 0;
			if (data->mouseX >= rcClient.right) data->mouseX = rcClient.right - 1;
			if (data->mouseY >= rcClient.bottom) data->mouseY = rcClient.bottom - 1;

			//clamp additionally to the valid content.
			int scrollX, scrollY, contentW, contentH;
			ChrViewerGetScroll(data, &scrollX, &scrollY);
			ChrViewerGetGraphicsSize(data, &contentW, &contentH);

			contentW -= scrollX;
			contentH -= scrollY;
			if (data->mouseX >= contentW) data->mouseX = contentW - 1;
			if (data->mouseY >= contentH) data->mouseY = contentH - 1;
		}
	}

	//notify of mouse leave if we're not already processing a mouse leave
	if (msg != WM_MOUSELEAVE && msg != WM_NCMOUSELEAVE) {
		TRACKMOUSEEVENT tme = { 0 };
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = hWnd;
		TrackMouseEvent(&tme);
	}

	ChrViewerUpdateCursor(data);
}

static void ChrViewerOnLButtonDown(NCGRVIEWERDATA *data) {
	data->mouseDown = TRUE;
	SetCapture(data->hWndViewer);

	//hit test
	int hit = ChrViewerHitTest(data, data->mouseX, data->mouseY);
	int hitType = hit & HIT_TYPE_MASK;
	data->mouseDownHit = hit;

	if (hit == HIT_NOWHERE) {
		//hit nowhere. If in selection mode, clear selection.
		if (data->mode == CHRVIEWER_MODE_SELECT) ChrViewerDeselect(data);
		ChrViewerReleaseCursor(data);
		ChrViewerUpdateCharacterLabel(data->hWnd);
		return;
	}

	//get scroll info
	int scrollX, scrollY;
	ChrViewerGetScroll(data, &scrollX, &scrollY);

	//get smouse pixel coordinate
	int pxX = -1, pxY = -1;
	if (hitType == HIT_CONTENT) {
		pxX = (data->mouseX + scrollX) / data->scale;
		pxY = (data->mouseY + scrollY) / data->scale;
	}

	switch (data->mode) {
		case CHRVIEWER_MODE_SELECT:
		{
			data->dragStartX = data->mouseX;
			data->dragStartY = data->mouseY;
			data->selDragStartX = data->selStartX;
			data->selDragStartY = data->selStartY;

			if (hitType == HIT_NOWHERE || hitType == HIT_CONTENT) {
				if (ChrViewerHasSelection(data)) {
					//discard selection
					ChrViewerDeselect(data);
				} else {
					//make selection of click point
					data->selStartX = data->selEndX = data->hoverX;
					data->selStartY = data->selEndY = data->hoverY;
				}
				ChrViewerUpdateCharacterLabel(data->hWnd);
			} else if (hitType == HIT_SEL) {
				//mouse-down on selection, do not clear selection.
				//make the corner/edge of the hit the end point.
				ChrViewerMakeSelectionCornerEnd(data, hit);
			}
			break;
		}
		case CHRVIEWER_MODE_PEN:
		{
			int pcol = ChrViewerGetSelectedColor(data);
			if (pcol == -1) {
				//must have a selected color to paint with.
				ChrViewerReleaseCursor(data); //necessary to prevent UI issues with mouse capture
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
			int pcol = ChrViewerGetSelectedColor(data);
			if (pcol == -1) {
				//must have a selected color to fill with.
				ChrViewerReleaseCursor(data); //necessary to prevent UI issues with mouse capture
				MessageBox(data->hWnd, L"Must have a color selected in the palette window to draw with.", L"No color selected", MB_ICONERROR);
			} else {
				ChrViewerFloodFill(data, pxX, pxY, pcol);
				ChrViewerGraphicsUpdated(data);
			}
			break;
		}
		case CHRVIEWER_MODE_EYEDROP:
		{
			int col = ChrViewerGetPixel(data, pxX, pxY);
			if (!ChrViewerSetSelectedColor(data, col)) {
				MessageBox(data->hWnd, L"Must have a color palette open to use this tool.", L"No palette", MB_ICONERROR);
			} else {
				//release mouse capture
				ChrViewerReleaseCursor(data);

				//switch to previous tool
				ChrViewerSetMode(data, data->lastMode);
			}
			break;
		}
		case CHRVIEWER_MODE_STAMP:
		{
			if (!ChrViewerHasSelection(data)) {
				ChrViewerReleaseCursor(data);
				MessageBox(data->hWnd, L"Must have a selection to use this tool.", L"No selection", MB_ICONERROR);
			} else {
				ChrViewerStampTile(data, data->hoverX, data->hoverY);
			}
			break;
		}
	}

	InvalidateRect(data->hWndViewer, NULL, FALSE);
	ChrViewerRefreshMargins(data->hWnd);
}

static void ChrViewerOnLButtonUp(NCGRVIEWERDATA *data) {
	ChrViewerReleaseCursor(data);
}

static void ChrViewerOnRButtonDown(NCGRVIEWERDATA *data) {
	//mark hovered tile
	data->contextHoverX = data->hoverX;
	data->contextHoverY = data->hoverY;

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
	if (ChrViewerHasSelection(data)) {
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

	//release mouse to prevent input issues
	ChrViewerReleaseCursor(data);

	POINT mouse;
	GetCursorPos(&mouse);
	TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, data->hWnd, NULL);
}

static void ChrViewerUpdateContentSize(NCGRVIEWERDATA *data) {
	int contentWidth, contentHeight;
	ChrViewerGetGraphicsSize(data, &contentWidth, &contentHeight);

	SCROLLINFO info;
	info.cbSize = sizeof(info);
	info.nMin = 0;
	info.nMax = contentWidth;
	info.fMask = SIF_RANGE;
	SetScrollInfo(data->hWndViewer, SB_HORZ, &info, TRUE);

	info.nMax = contentHeight;
	SetScrollInfo(data->hWndViewer, SB_VERT, &info, TRUE);
	RECT rcClient;
	GetClientRect(data->hWndViewer, &rcClient);
	SendMessage(data->hWndViewer, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
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
	if (!frameData) {
		frameData = calloc(1, sizeof(FRAMEDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) frameData);
	}
	frameData->contentWidth = contentWidth;
	frameData->contentHeight = contentHeight;

	UpdateScrollbarVisibility(hWnd);

	switch (msg) {
		case WM_CREATE:
			ShowScrollBar(hWnd, SB_BOTH, FALSE);
			break;
		case WM_PAINT:
			ChrViewerPaintView(data, hWnd);
			break;
		case WM_ERASEBKGND:
			return 1;
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			ChrViewerOnMouseMove(data, hWnd, msg, wParam, lParam);
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
			ChrViewerRefreshMargins(data->hWnd);
			ChrViewerUpdateCursor(data);
			return DefChildProc(hWnd, msg, wParam, lParam);
		case WM_RBUTTONDOWN:
			ChrViewerOnRButtonDown(data);
			break;
		case NV_RECALCULATE:
			ChrViewerUpdateContentSize(data);
			ChrViewerUpdateCursor(data);
			break;
		case WM_SIZE:
		{
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
				SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);

				//invalidate viewers
				ChrViewerGraphicsUpdated(data);
				ChrViewerRefreshMargins(data->hWnd);

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
	free(cbdata);

	setStyle(hWndMain, FALSE, WS_DISABLED);
	SetForegroundWindow(hWndMain);

	//select the import region
	if (import1D) {
		data->selStartX = -1;
		data->selEndX = -1;
		data->selStartY = -1;
		data->selEndY = -1;
	} else {
		data->selStartX = cim->originX;
		data->selStartY = cim->originY;
		data->selEndX = cim->originX + cim->width / 8 - 1;
		data->selEndY = cim->originY + cim->height / 8 - 1;
		if (data->selEndX >= data->ncgr.tilesX) data->selEndX = data->ncgr.tilesX - 1;
		if (data->selEndY >= data->ncgr.tilesY) data->selEndY = data->ncgr.tilesY - 1;
	}

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
	width = ncgr.tilesX * 8;
	height = ncgr.tilesY * 8;

	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		if (width < 150) width = 150;
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL); //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + 42 + 22 + GetSystemMetrics(SM_CYHSCROLL); //+42 to account for the combobox
	}

	HWND hWnd = EditorCreate(L"NcgrViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
	if (ncgr.header.format == NCGR_TYPE_HUDSON || ncgr.header.format == NCGR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}

HWND CreateNcgrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCGR *ncgr) {
	width = ncgr->tilesX * 8;
	height = ncgr->tilesY * 8;

	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		if (width < 150) width = 150;
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL); //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + 42 + 22 + GetSystemMetrics(SM_CYHSCROLL); //+42 to account for the combobox
	}

	HWND hWnd = EditorCreate(L"NcgrViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) ncgr);
	if (ncgr->header.format == NCGR_TYPE_HUDSON || ncgr->header.format == NCGR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}
