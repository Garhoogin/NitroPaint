#include <Windows.h>
#include <CommCtrl.h>
#include <math.h>

#include "nitropaint.h"
#include "editor.h"
#include "nclrviewer.h"
#include "childwindow.h"
#include "ncgrviewer.h"
#include "nscrviewer.h"
#include "ncerviewer.h"
#include "colorchooser.h"
#include "resource.h"
#include "palette.h"
#include "gdip.h"
#include "ui.h"

#include "preview.h"

//size of a color in the editor
#define COLOR_SIZE_DEFAULT  16
#define COLOR_SIZE          sColorCellSize
static int sColorCellSize = COLOR_SIZE_DEFAULT; //size of one color cell entry

extern HICON g_appIcon;

//IS.Colors4
typedef struct NC_CLIPBOARD_PALETTE_HEADER_ {
	DWORD magic; //0xC208B8
	BOOL is1D;
	int originRow;
	int originCol;
	int unused[2];
	int nCols;
	int nRows;
} NC_CLIPBOARD_PALETTE_HEADER;

typedef struct NC_CLIPBOARD_PALETTE_FOOTER_ {
	uint8_t field0[8]; //no idea how these work
} NC_CLIPBOARD_PALETTE_FOOTER;

typedef struct AC_CLIPBOARD_PALETTE_HEADER_ {
	uint32_t nCols;
	uint32_t nRows;
} AC_CLIPBOARD_PALETTE_HEADER;

//OPX_PALETTE
typedef struct OP_CLIPBOARD_PALETTE_HEADER_ {
	uint16_t three; //3
	uint16_t nColors;
} OP_CLIPBOARD_PALETTE_HEADER;

static int g_acClipboardFormat = 0;
static int g_ncClipboardFormat = 0;
static int g_opClipboardFormat = 0;

static void PalViewerEnsureClipboardFormats(void) {
	if (g_ncClipboardFormat == 0) {
		g_acClipboardFormat = RegisterClipboardFormat(L"IS.Colors2");
		g_ncClipboardFormat = RegisterClipboardFormat(L"IS.Colors4");
		g_opClipboardFormat = RegisterClipboardFormat(L"OPX_PALETTE");
	}
}

static int PalViewerHasClipboard(HWND hWnd) {
	PalViewerEnsureClipboardFormats();

	//acquire clipboard and check if we have a palette on the clipboard
	BOOL b = OpenClipboard(hWnd);
	if (!b) return 0;

	//test all supported formats
	HGLOBAL hAc = GetClipboardData(g_acClipboardFormat);
	HGLOBAL hNc = GetClipboardData(g_ncClipboardFormat);
	HGLOBAL hOp = GetClipboardData(g_opClipboardFormat);
	if (hAc == NULL && hNc == NULL && hOp == NULL) {
		CloseClipboard();
		return 0;
	}

	CloseClipboard();
	return 1;
}

static HWND PalViewerGetAssociatedWindow(NCLRVIEWERDATA *data, int type) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	switch (type) {
		case FILE_TYPE_CHARACTER:
			return nitroPaintStruct->hWndNcgrViewer;
		case FILE_TYPE_SCREEN:
			return NULL; //TODO
		case FILE_TYPE_CELL:
			return nitroPaintStruct->hWndNcerViewer;
	}
	return NULL;
}

static int PalViewerGetSelectionSize(NCLRVIEWERDATA *data) {
	//if no selection return 0
	if (data->selStart == -1 || data->selEnd == -1) return 0;

	int selStart = min(data->selStart, data->selEnd);
	int selEnd = max(data->selStart, data->selEnd);

	if (data->selMode == PALVIEWER_SELMODE_1D) {
		//1D
		return selEnd - selStart + 1;
	} else {
		int startX = selStart % 16, startY = selStart / 16;
		int endX = selEnd % 16, endY = selEnd / 16;
		return (endX - startX + 1) * (endY - startY + 1);
	}
}

static void PalViewerGetSelectionDimensionsForRange(NCLRVIEWERDATA *data, int start, int end, int *selX, int *selY, int *outWidth, int *outHeight) {
	//if 1D, say it's a row
	if (data->selMode == PALVIEWER_SELMODE_1D) {
		int size = PalViewerGetSelectionSize(data);
		int startIndex = min(start, end);
		*selX = startIndex % 16;
		*selY = startIndex / 16;
		*outWidth = size;
		*outHeight = 1;
		return;
	}

	//else
	int selStart = min(start, end);
	int selEnd = max(start, end);
	int selStartX = selStart % 16, selStartY = selStart / 16;
	int selEndX = selEnd % 16, selEndY = selEnd / 16;
	int selMinX = min(selStartX, selEndX), selMinY = min(selStartY, selEndY);
	int selMaxX = max(selStartX, selEndX), selMaxY = max(selStartY, selEndY);
	int selWidth = selMaxX - selMinX + 1, selHeight = selMaxY - selMinY + 1;

	*selX = selMinX;
	*selY = selMinY;
	*outWidth = selWidth;
	*outHeight = selHeight;
}

static void PalViewerGetSelectionDimensions(NCLRVIEWERDATA *data, int *selX, int *selY, int *outWidth, int *outHeight) {
	PalViewerGetSelectionDimensionsForRange(data, data->selStart, data->selEnd, selX, selY, outWidth, outHeight);
}

static int PalViewerGetDragDelta(NCLRVIEWERDATA *data, int *pdx, int *pdy) {
	//if not dragging, no delta
	if (!data->dragging) {
		*pdx = *pdy = 0;
		return 0;
	}

	//get source and destination
	int srcX = data->dragStart.x / COLOR_SIZE, srcY = data->dragStart.y / COLOR_SIZE;
	int dstX = data->dragPoint.x / COLOR_SIZE, dstY = data->dragPoint.y / COLOR_SIZE;
	if (srcX == dstX && srcY == dstY) {
		*pdx = *pdy = 0;
		return 0;
	}

	int selStartX = 0, selStartY = 0, selWidth = 0, selHeight = 0, selStartIndex, selEndIndex, selSize;
	PalViewerGetSelectionDimensions(data, &selStartX, &selStartY, &selWidth, &selHeight);
	selSize = PalViewerGetSelectionSize(data);
	selStartIndex = selStartX + selStartY * 16;
	selEndIndex = (selStartX + selWidth - 1) + (selStartY + selHeight - 1) * 16;

	//delta X and delta Y
	int dx = dstX - srcX, dy = dstY - srcY;
	int delta = dx + dy * 16;
	if (!data->movingSelection) {
		*pdx = dx;
		*pdy = dy;
		return delta;
	}

	//handling for selection modes
	if (data->selMode == PALVIEWER_SELMODE_1D) {
		//only clamp 1D selection bounds
		int selDestIndex = selStartIndex;
		selDestIndex += delta;
		if (selDestIndex < 0) {
			delta -= selDestIndex;
			selDestIndex = 0;
		}
		if (selDestIndex + selSize > data->nclr->nColors) {
			delta -= ((selDestIndex + selSize) - data->nclr->nColors);
			selDestIndex -= ((selDestIndex + selSize) - data->nclr->nColors);
		}

		dx = delta;
		dy = 0; //TODO
	} else {
		//clamp 2D selection bounds
		int nRows = (data->nclr->nColors + 15) / 16;
		int destStartX = selStartX + dx, destStartY = selStartY + dy;

		if (destStartX < 0) {
			dx -= destStartX;
			destStartX = 0;
		} else if (destStartX + selWidth > 16) {
			dx -= (destStartX + selWidth - 16);
			destStartX -= (destStartX + selWidth - 16);
		}
		if (destStartY < 0) {
			dy -= destStartY;
			destStartY = 0;
		} else if (destStartY + selHeight > nRows) {
			dy -= (destStartY + selHeight - nRows);
			destStartY -= (destStartY + selHeight - nRows);
		}
		delta = dx + dy * 16;
	}

	//next: if we're in preserve drag mode, we'll need to restrict some motions.
	if (data->preserveDragging) {
		HWND hWndCharEditor = PalViewerGetAssociatedWindow(data, FILE_TYPE_CHAR);
		HWND hWndScreenEditor = PalViewerGetAssociatedWindow(data, FILE_TYPE_SCREEN);
		
		//get graphics depth
		int depth = data->nclr->nBits;
		if (hWndCharEditor != NULL) {
			NCGR *ncgr = (NCGR *) EditorGetObject(hWndCharEditor);
			depth = ncgr->nBits;
		} else if (hWndScreenEditor != NULL) {
			NSCR *nscr = (NSCR *) EditorGetObject(hWndScreenEditor);
			depth = (nscr->fmt == SCREENFORMAT_AFFINEEXT) ? 8 : 4;
		}

		//if we're in 4 bit mode, move can be only horizontal or vertical.
		if (depth == 4) {
			int magDx = max(dx, -dx);
			int magDy = max(dy, -dy);

			if (magDy > magDx) {
				delta -= dx;
				dx = 0;
			} else {
				delta -= dy * 16;
				dy = 0;
			}
		} else {
			//for 8-bit depth, we can move anywhere within the palette. If outside, then limit dy to multiples of 16.
			int destStart = selStartIndex + delta;
			int destEnd = selEndIndex + delta;

			int palSrcStart = selStartIndex / 256;
			int palSrcEnd = selEndIndex / 256;
			int palDestStart = destStart / 256;
			int palDestEnd = destEnd / 256;

			if (palSrcStart == palSrcEnd && (palSrcStart != palDestStart || palSrcStart != palDestEnd)) {
				dx = 0;
				if (dy >= 0) {
					dy = (dy + 8) / 16 * 16;
				} else {
					dy = (-dy + 8) / 16 * -16;
				}
				delta = dx + dy * 16;
			}
		}

	}

	*pdx = dx;
	*pdy = dy;
	return delta;
}

int PalViewerIndexInRange(int index, int start, int end, int is2d) {
	if (start == -1 || end == -1) return 0; //no selection

	if (!is2d) {
		//for 1D selection
		if (index >= start && index <= end) return 1;
		if (index >= end && index <= start) return 1;
		return 0;
	} else {
		//for 2D selection
		int x1 = start % 16, y1 = start / 16;
		int x2 = end % 16, y2 = end / 16;
		int x = index % 16, y = index / 16;

		if (x < min(x1, x2) || x > max(x1, x2)) return 0;
		if (y < min(y1, y2) || y > max(y1, y2)) return 0;
		return 1;
	}
}

static int PalViewerIndexInSelection(NCLRVIEWERDATA *data, int index) {
	return PalViewerIndexInRange(index, data->selStart, data->selEnd, data->selMode == PALVIEWER_SELMODE_2D);
}

static void PalViewerUnwrapSelection(NCLRVIEWERDATA *data, COLOR *dest) {
	//fill out linearly
	int destIndex = 0;
	for (int i = 0; i < data->nclr->nColors; i++) {
		if (!PalViewerIndexInSelection(data, i)) continue;
		dest[destIndex++] = data->nclr->colors[i];
	}
}

static void PalViewerWrapSelection(NCLRVIEWERDATA *data, COLOR *src, int nSrc) {
	//read out linearly
	int srcIndex = 0;
	for (int i = 0; i < data->nclr->nColors && srcIndex < nSrc; i++) {
		if (!PalViewerIndexInSelection(data, i)) continue;
		data->nclr->colors[i] = src[srcIndex++];
	}
}

static void PalViewerMapFillIdentity(int *map, int n, int baseval) {
	for (int i = 0; i < n; i++) map[i] = i + baseval;
}

static void PalViewerMapInvert(int *map, int *out, int size) {
	memset(out, 0, size * sizeof(int));
	for (int i = 0; i < size; i++) {
		out[map[i]] = i;
	}
}

static void PalViewerMapTransform(COLOR *pal, COLOR *out, int *map, int size) {
	for (int i = 0; i < size; i++) {
		out[i] = pal[map[i]];
	}
}

static void PalViewerMapCreateMove2D(int *map, int size, int srcX, int srcY, int srcWidth, int srcHeight, int destX, int destY) {
	//copy whole base palette out
	PalViewerMapFillIdentity(map, size, 0);
	if (srcX == destX && srcY == destY) return; //no further processing

	//if the two regions aren't overlapping, the move is simple. Just swap colors in the two regions.
	if (((srcX + srcWidth) <= destX || srcX >= (destX + srcWidth)) || ((srcY + srcHeight) <= destY || srcY >= (destY + srcHeight))) {
		for (int y = 0; y < srcHeight; y++) {
			PalViewerMapFillIdentity(map + destX + 16 * (destY + y), srcWidth, srcX + 16 * (srcY + y));
			PalViewerMapFillIdentity(map + srcX + 16 * (srcY + y), srcWidth, destX + 16 * (destY + y));
		}
		return;
	}

	//otherwise... more complicated. First, transfer the source area to the destination.
	for (int y = 0; y < srcHeight; y++) {
		PalViewerMapFillIdentity(map + destX + 16 * (destY + y), srcWidth, srcX + 16 * (srcY + y));
	}

	//for an axis-aligned move, transfer the last/first rows/columns to the other end.
	if (srcX == destX) {
		//vertical move. If we move up, copy rows down. If we move down, copy rows up.
		if (destY < srcY) {
			//up
			for (int y = 0; y < (srcY - destY); y++) {
				PalViewerMapFillIdentity(map + destX + (y + destY + srcHeight) * 16, srcWidth, destX + (y + destY) * 16);
			}
		} else {
			//down
			for (int y = 0; y < (destY - srcY); y++) {
				PalViewerMapFillIdentity(map + destX + (y + srcY) * 16, srcWidth, destX + (y + srcY + srcHeight) * 16);
			}
		}
		return;
	}
	if (srcY == destY) {
		//horizontal move. If we move left, copy columns right. Right, copy left.
		if (destX < srcX) {
			//left
			for (int x = 0; x < (srcX - destX); x++) {
				for (int y = 0; y < srcHeight; y++) {
					map[x + destX + srcWidth + (y + destY) * 16] = x + destX + (y + destY) * 16;
				}
			}
		} else {
			//right
			for (int x = 0; x < (destX - srcX); x++) {
				for (int y = 0; y < srcHeight; y++) {
					map[x + srcX + (y + destY) * 16] = x + srcX + srcWidth + (y + destY) * 16;
				}
			}
		}
		return;
	}

	//else... slice and rearrange a bit
	int remainderWidth   = (destX > srcX) ? (destX - srcX) : (srcX - destX);
	int remainderHeight  = (destY > srcY) ? (destY - srcY) : (srcY - destY);
	int overlappedWidth  = srcWidth  - remainderWidth;
	int overlappedHeight = srcHeight - remainderHeight;

	//don't worry about it (NC sure doesn't!)
	int q1dx = (destX > srcX) ? (destX)           : (srcX),             q1dy = (destY > srcY) ? (srcY)             : (destY + srcHeight);
	int q2dx = (destX > srcX) ? (srcX)            : (destX + srcWidth), q2dy = (destY > srcY) ? (srcY)             : (destY + srcHeight);
	int q3dx = (destX > srcX) ? (srcX)            : (destX + srcWidth), q3dy = (destY > srcY) ? (destY)            : (srcY);
	int q1sx = (destX > srcX) ? (destX)           : (srcX),             q1sy = (destY > srcY) ? (srcY + srcHeight) : (destY);
	int q2sx = (destX > srcX) ? (srcX + srcWidth) : (destX),            q2sy = (destY > srcY) ? (srcY + srcHeight) : (destY);
	int q3sx = (destX > srcX) ? (srcX + srcWidth) : (destX),            q3sy = (destY > srcY) ? (destY)            : (srcY);

	//Q1, Q2, Q3
	for (int y = 0; y < remainderHeight; y++) {
		PalViewerMapFillIdentity(map + q1dx + (q1dy + y) * 16, overlappedWidth, q1sx + (q1sy + y) * 16);
	}
	for (int y = 0; y < remainderHeight; y++) {
		PalViewerMapFillIdentity(map + q2dx + (q2dy + y) * 16, remainderWidth, q2sx + (q2sy + y) * 16);
	}
	for (int y = 0; y < overlappedHeight; y++) {
		PalViewerMapFillIdentity(map + q3dx + (q3dy + y) * 16, remainderWidth, q3sx + (q3sy + y) * 16);
	}
}

static void PalViewerMapCreateMove1D(int *map, int size, int start, int length, int dest) {
	//copy whole base palette out
	PalViewerMapFillIdentity(map, size, 0);
	if (start == dest) return; //no further processing

	//if the two regions aren't overlapping, the move is simple. Just swap colors in the two regions.
	if ((start + length) <= dest || start >= (dest + length)) {
		PalViewerMapFillIdentity(map + dest, length, start);
		PalViewerMapFillIdentity(map + start, length, dest);
		return;
	}

	//otherwise, copy src->dest. If we're moving colors backwrads, copy src->end.
	PalViewerMapFillIdentity(map + dest, length, start);
	if (dest < start) {
		PalViewerMapFillIdentity(map + dest + length, start - dest, dest);
	} else {
		PalViewerMapFillIdentity(map + start, dest - start, start + length);
	}
}

static void PalViewerMovePalette2D(COLOR *pal, COLOR *out, int size, int srcX, int srcY, int srcWidth, int srcHeight, int destX, int destY) {
	//create map to transform
	int *map = (int *) calloc(size, sizeof(int));
	PalViewerMapCreateMove2D(map, size, srcX, srcY, srcWidth, srcHeight, destX, destY);

	//apply transform
	PalViewerMapTransform(pal, out, map, size);
	free(map);
}

static void PalViewerMovePalette1D(COLOR *pal, COLOR *out, int size, int start, int length, int dest) {
	//create map to transform
	int *map = (int *) calloc(size, sizeof(int));
	PalViewerMapCreateMove1D(map, size, start, length, dest);

	//apply transform
	PalViewerMapTransform(pal, out, map, size);
	free(map);
}

static void PalViewerGetDragTransform(NCLRVIEWERDATA *data, int *map) {
	//the current palette, but taking the current select operation into account
	if (!data->movingSelection) {
		PalViewerMapFillIdentity(map, data->nclr->nColors, 0);
		return;
	}

	//calculate displacement
	int destX = data->dragPoint.x / COLOR_SIZE, destY = data->dragPoint.y / COLOR_SIZE;
	int dx, dy;
	int dragDisp = PalViewerGetDragDelta(data, &dx, &dy);

	//for 1D
	int selStart = min(data->selStart, data->selEnd);
	int selEnd = max(data->selStart, data->selEnd);
	int selSize = PalViewerGetSelectionSize(data);

	//for 2D
	int selMinX, selMinY, selWidth, selHeight;
	PalViewerGetSelectionDimensions(data, &selMinX, &selMinY, &selWidth, &selHeight);

	if (data->selMode == PALVIEWER_SELMODE_1D) {
		PalViewerMapCreateMove1D(map, data->nclr->nColors, selStart, selSize, selStart + dragDisp);
	} else {
		PalViewerMapCreateMove2D(map, data->nclr->nColors, selMinX, selMinY, selWidth, selHeight, selMinX + dx, selMinY + dy);
	}
}

static void PalViewerBaseCopyPalette(COLOR *palette, int paletteSize, int start, int end, int is2d) {
	PalViewerEnsureClipboardFormats();

	//get selection
	int selStart = min(start, end);
	int selEnd = max(start, end);

	//get bounds
	int nColors = selEnd - selStart + 1;
	int selWidth = nColors, selHeight = 1;
	if (is2d) {
		int selStartX = selStart % 16, selStartY = selStart / 16;
		int selEndX = selEnd % 16, selEndY = selEnd / 16;
		selWidth = max(selStartX, selEndX) + 1 - min(selStartX, selEndX);
		selHeight = max(selStartY, selEndY) + 1 - min(selStartY, selEndY);
		nColors = selWidth * selHeight;
	}

	//AC, NC and OPX formats
	int acSize = sizeof(AC_CLIPBOARD_PALETTE_HEADER) + nColors * 8;
	int ncSize = sizeof(NC_CLIPBOARD_PALETTE_HEADER) + nColors * 8 + sizeof(NC_CLIPBOARD_PALETTE_FOOTER);
	int opSize = sizeof(OP_CLIPBOARD_PALETTE_HEADER) + nColors * 4;

	HGLOBAL hAc = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, acSize);
	HGLOBAL hNc = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, ncSize);
	HGLOBAL hOp = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, opSize);

	AC_CLIPBOARD_PALETTE_HEADER *acData = (AC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hAc);
	NC_CLIPBOARD_PALETTE_HEADER *ncData = (NC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hNc);
	OP_CLIPBOARD_PALETTE_HEADER *opData = (OP_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hOp);

	COLOR32 *acPalette = (COLOR32 *) (acData + 1);
	COLOR32 *ncPalette = (COLOR32 *) (ncData + 1);
	COLOR32 *opPalette = (COLOR32 *) (opData + 1);

	acData->nCols = selWidth;
	acData->nRows = selHeight;
	ncData->magic = 0xC208B8;
	ncData->is1D = !is2d;
	ncData->nCols = selWidth;
	ncData->nRows = selHeight;
	opData->three = 3;
	opData->nColors = nColors;

	int outIndex = 0;
	for (int i = 0; i < paletteSize; i++) {
		if (!PalViewerIndexInRange(i, selStart, selEnd, is2d)) continue;

		acPalette[outIndex] = ColorConvertFromDS(palette[i]);
		ncPalette[outIndex] = ColorConvertFromDS(palette[i]);
		ncPalette[outIndex + nColors] = ColorConvertFromDS(palette[i]);
		acPalette[outIndex + nColors] = ColorConvertFromDS(palette[i]);
		opPalette[outIndex] = ColorConvertFromDS(palette[i]);
		outIndex++;
	}

	GlobalUnlock(hAc);
	GlobalUnlock(hNc);
	GlobalUnlock(hOp);

	SetClipboardData(g_acClipboardFormat, hAc);
	SetClipboardData(g_ncClipboardFormat, hNc);
	SetClipboardData(g_opClipboardFormat, hOp);
}

VOID CopyPalette(COLOR *palette, int nColors) {
	PalViewerBaseCopyPalette(palette, nColors, 0, nColors - 1, 0);
}

VOID PastePalette(COLOR *dest, int nMax) {
	PalViewerEnsureClipboardFormats();

	HGLOBAL hAc = GetClipboardData(g_acClipboardFormat);
	HGLOBAL hNc = GetClipboardData(g_ncClipboardFormat);
	HGLOBAL hOp = GetClipboardData(g_opClipboardFormat);
	COLOR32 *src = NULL;
	int nCols = 0;

	if (hAc != NULL) {
		AC_CLIPBOARD_PALETTE_HEADER *acData = (AC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hAc);
		nCols = acData->nRows * acData->nCols;
		src = (COLOR32 *) (acData + 1);
	} else if (hNc != NULL) {
		NC_CLIPBOARD_PALETTE_HEADER *ncData = (NC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hNc);
		nCols = ncData->nCols * ncData->nRows;
		src = (COLOR32 *) (ncData + 1);
	} else if (hOp != NULL) {
		OP_CLIPBOARD_PALETTE_HEADER *opData = (OP_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hOp);
		nCols = opData->nColors;
		src = (COLOR32 *) (opData + 1);
	} else {
		return;
	}

	if (nCols > nMax) nCols = nMax;
	for (int i = 0; i < nCols; i++) {
		dest[i] = ColorConvertToDS(src[i]);
	}
	if (hAc != NULL) GlobalUnlock(hAc);
	if (hNc != NULL) GlobalUnlock(hNc);
	if (hOp != NULL) GlobalUnlock(hOp);
}

static void PalViewerCopyPalette(NCLRVIEWERDATA *data) {
	PalViewerBaseCopyPalette(data->nclr->colors, data->nclr->nColors, data->selStart, data->selEnd, data->selMode == PALVIEWER_SELMODE_2D);
}

static void PalViewerPastePalette(NCLRVIEWERDATA *data) {
	PalViewerEnsureClipboardFormats();

	HGLOBAL hAc = GetClipboardData(g_acClipboardFormat);
	HGLOBAL hNc = GetClipboardData(g_ncClipboardFormat);
	HGLOBAL hOp = GetClipboardData(g_opClipboardFormat);
	COLOR32 *src = NULL;
	int nCols = 0, width = 0, height = 0, paste2d = 0;

	if (hNc != NULL) {
		//NITRO-CHARACTER clipboard
		NC_CLIPBOARD_PALETTE_HEADER *ncData = (NC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hNc);
		nCols = ncData->nCols * ncData->nRows;
		width = ncData->nCols;
		height = ncData->nRows;
		paste2d = !ncData->is1D;
		src = (COLOR32 *) (ncData + 1);
	} else if (hAc != NULL) {
		//IS-AGB-CHARACTER clipboard
		AC_CLIPBOARD_PALETTE_HEADER *acData = (AC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hAc);
		nCols = acData->nRows * acData->nCols;
		width = acData->nCols;
		height = acData->nRows;
		paste2d = 1;
		src = (COLOR32 *) (acData + 1);
	} else if (hOp != NULL) {
		//iMageStudio clipboard
		OP_CLIPBOARD_PALETTE_HEADER *opData = (OP_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hOp);
		nCols = opData->nColors;
		src = (COLOR32 *) (opData + 1);
	} else {
		return;
	}

	//paste
	int selStart = min(data->selStart, data->selEnd);
	if (selStart < 0) selStart = data->hoverIndex;
	if (selStart < 0) selStart = data->contextHoverX + 16 * data->contextHoverY;
	if (selStart < 0) selStart = 0;

	if (paste2d) {
		int selStartX = selStart % 16;
		int selStartY = selStart / 16;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				int destX = x + selStartX;
				int destY = y + selStartY;
				int destIndex = destX + destY * 16;
				if (destX >= 16 || destIndex >= data->nclr->nColors) continue;
				data->nclr->colors[destIndex] = ColorConvertToDS(src[x + y * width]);
			}
		}

		int nRows = (data->nclr->nColors + 15) / 16;
		int maxWidth = width, maxHeight = height;
		if (selStartX + maxWidth > 16) maxWidth = 16 - selStartX;
		if (selStartY + maxHeight > nRows) maxHeight = nRows - selStartY;

		//select destination
		data->selMode = PALVIEWER_SELMODE_2D;
		data->selStart = selStart;
		data->selEnd = selStart + (maxWidth - 1) + (maxHeight - 1) * 16;
	} else {
		for (int i = 0; i < nCols; i++) {
			int destIndex = i + selStart;
			if (destIndex >= data->nclr->nColors) break;
			data->nclr->colors[destIndex] = ColorConvertToDS(src[i]);
		}

		//select destination
		data->selMode = PALVIEWER_SELMODE_1D;
		data->selStart = selStart;
		data->selEnd = min(data->selStart + nCols - 1, data->nclr->nColors);
	}

	if (hAc != NULL) GlobalUnlock(hAc);
	if (hNc != NULL) GlobalUnlock(hNc);
	if (hOp != NULL) GlobalUnlock(hOp);
}

#define PALVIEWER_UPDATE_CHAR    1
#define PALVIEWER_UPDATE_SCREEN  2
#define PALVIEWER_UPDATE_CELL    4
#define PALVIEWER_UPDATE_ALL     (PALVIEWER_UPDATE_CHAR|PALVIEWER_UPDATE_SCREEN|PALVIEWER_UPDATE_CELL)

static void PalViewerUpdateNcerViewer(NCLRVIEWERDATA *data) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	HWND hWndNcerViewer = nitroPaintStruct->hWndNcerViewer;
	if (hWndNcerViewer != NULL) {
		CellViewerGraphicsUpdated(hWndNcerViewer);
	}
}

static void PalViewerUpdateViewers(NCLRVIEWERDATA *data, int updateMask) {
	HWND hWndMain = data->editorMgr->hWnd;

	//update viewers
	if (updateMask & PALVIEWER_UPDATE_CHAR) {
		//all character viewers
		EditorInvalidateAllByType(hWndMain, FILE_TYPE_CHAR);
	}
	if (updateMask & PALVIEWER_UPDATE_SCREEN) {
		//all screen viewers
		EditorInvalidateAllByType(hWndMain, FILE_TYPE_SCREEN);
	}
	if (updateMask & PALVIEWER_UPDATE_CELL) {
		//all cell viewers
		PalViewerUpdateNcerViewer(data);
	}
}

static int CountPaletteUsages(NCLRVIEWERDATA *data, int *counts) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	HWND hWndCharacterEditor = nitroPaintStruct->hWndNcgrViewer;
	if (hWndCharacterEditor == 0) return 0;

	//if no screen editor open, get use counts from character
	StList scrEditors;
	StListCreateInline(&scrEditors, NSCRVIEWERDATA *, NULL);
	EditorGetAllByType(data->editorMgr->hWnd, FILE_TYPE_SCREEN, &scrEditors);

	//character editor data
	NCGRVIEWERDATA *ncgrData = (NCGRVIEWERDATA *) EditorGetData(hWndCharacterEditor);
	int palSize = 1 << ncgrData->ncgr->nBits;

	if (scrEditors.length == 0) {
		//character exists. get graphics
		int palBase = ncgrData->selectedPalette;
		for (int i = 0; i < ncgrData->ncgr->nTiles; i++) {
			unsigned char *tile = ncgrData->ncgr->tiles[i];
			for (int j = 0; j < 64; j++) {
				int index = tile[j] + palBase * palSize;
				if (index < data->nclr->nColors) counts[index]++;
			}
		}
	} else {
		//measure every screen
		for (size_t i = 0; i < scrEditors.length; i++) {
			NSCRVIEWERDATA *nscrData = *(NSCRVIEWERDATA **) StListGetPtr(&scrEditors, i);
			for (unsigned int j = 0; j < nscrData->nscr->dataSize / 2; j++) {
				uint16_t tile = nscrData->nscr->data[j];
				int charIndex = (tile & 0x3FF) - nscrData->tileBase;
				int palBase = (tile >> 12) & 0xF;
				if (charIndex < 0) continue;

				//tally up palette indices
				if (charIndex < ncgrData->ncgr->nTiles) {
					for (int k = 0; k < 64; k++) {
						int index = ncgrData->ncgr->tiles[charIndex][k];
						index += palBase * palSize;
						if (index < data->nclr->nColors) counts[index]++;
					}
				}

			}
		}
	}
	StListFree(&scrEditors);
	return 1;
}

static COLOR32 MakeContrastingColor(COLOR32 c) {
	int r = (c >> 0) & 0xFF;
	int g = (c >> 8) & 0xFF;
	int b = (c >> 16) & 0xFF;
	int luma = (2 * r + 7 * g + 1 * b);

	return (luma > 1275) ? 0 : 0xFFFFFF;
}

static void PalViewerUpdatePreview(NCLRVIEWERDATA *data) {
	PreviewLoadBgPalette(data->nclr); //send to preview target
	PreviewLoadObjPalette(data->nclr);
	InvalidateRect(data->hWnd, NULL, FALSE); //redraw
}

static COLOR *PalViewerComputeViewPalette(NCLRVIEWERDATA *data) {
	//the current palette, but taking the current select operation into account
	COLOR *cols = (COLOR *) calloc(data->nclr->nColors, sizeof(COLOR));
	int *map = (int *) calloc(data->nclr->nColors, sizeof(int));

	//get transform
	PalViewerGetDragTransform(data, map);
	PalViewerMapTransform(data->nclr->colors, cols, map, data->nclr->nColors);

	free(map);
	return cols;
}

static int PalViewerCharUsedByScreens(int ch, NSCR **screens, int nScreens, uint16_t *pFoundTile) {
	for (int i = 0; i < nScreens; i++) {
		NSCR *nscr = screens[i];

		for (unsigned int j = 0; j < nscr->dataSize / 2; j++) {
			uint16_t d = nscr->data[j];
			int cno = d & 0x3FF;
			if (cno == ch) {
				*pFoundTile = d;
				return 1; //TODO: tile base setting in screen viewer
			}
		}
	}

	return 0;
}

static void PalViewerDoPreserveTransform(NCLRVIEWERDATA *data, NCGR *ncgr, NSCR **screens, int nScreens) {
	//if no character or screen, return
	if (ncgr == NULL && nScreens == 0) return;

	//get drag delta and selection mode.
	int dx, dy;
	int delta = PalViewerGetDragDelta(data, &dx, &dy);
	int deltaMag = max(delta, -delta);

	//get selection region
	int selX, selY, selWidth, selHeight, selSize;
	PalViewerGetSelectionDimensions(data, &selX, &selY, &selWidth, &selHeight);
	selSize = PalViewerGetSelectionSize(data);

	//get bit depth
	int depth = data->nclr->nBits;
	if (ncgr != NULL) {
		depth = ncgr->nBits;
	}
	int mask = (1 << depth) - 1;

	//compute index map.
	int *invmap = (int *) calloc(data->nclr->nColors, sizeof(int));
	int *map = (int *) calloc(data->nclr->nColors, sizeof(int));
	PalViewerGetDragTransform(data, invmap);
	PalViewerMapInvert(invmap, map, data->nclr->nColors);

	//if delta >= palette size, update screen and not character.
	if (deltaMag >= (1 << depth)) {
		if (nScreens == 0) return;

		int palSrc = (selX + selY * 16) >> depth;
		int palEnd = (selX + selWidth - 1 + 16 * (selY + selHeight - 1)) >> depth;
		int palDest = (selX + selY * 16 + delta) >> depth;
		int palDestEnd = (selX + selWidth - 1 + 16 * (selY + selHeight - 1) + delta) >> depth;
		for (int i = 0; i < nScreens; i++) {
			NSCR *nscr = screens[i];

			//for each tile in screen
			for (unsigned int j = 0; j < nscr->dataSize / 2; j++) {
				uint16_t d = nscr->data[j];
				int dpal = (d >> 12) & 0xF;
				int inSrc = (dpal >= palSrc && dpal <= palEnd);
				int inDst = (dpal >= palDest && dpal <= palDestEnd);
				if (!inSrc && !inDst) continue;

				//apply palette delta
				if (inSrc) {
					//in source
					dpal = (dpal - palSrc) + palDest;
				} else {
					//in destination & not in src
					if ((palEnd >= palDest && palEnd <= palDestEnd) || (palSrc >= palDest && palSrc <= palDestEnd)) {
						//regions overlap
						if (palDest > palSrc) {
							dpal -= (palEnd + 1 - palSrc);
						} else {
							dpal += (palEnd + 1 - palSrc);
						}
					} else {
						//no overlap
						dpal = (dpal - palDest) + palSrc;
					}
				}
				d = (d & 0xFFF) | (dpal << 12);
				nscr->data[j] = d;
			}
		}
		PalViewerUpdateViewers(data, PALVIEWER_UPDATE_SCREEN);
	} else {
		//update graphics data
		if (ncgr == NULL) return;

		//for each character of graphics, update the graphics indices.
		for (int i = 0; i < ncgr->nTiles; i++) {
			unsigned char *chr = ncgr->tiles[i];

			uint16_t foundTile = 0;
			if (nScreens == 0 || PalViewerCharUsedByScreens(i, screens, nScreens, &foundTile)) {
				//which palette this char was found with
				int usedPalette = foundTile >> 12;
				int palBaseIndex = usedPalette << depth;

				for (int j = 0; j < 64; j++) {
					int c = chr[j];
					c = map[c + palBaseIndex];
					chr[j] = c & mask; //cut off high bits to keep in palette
				}
			}
		}

		PalViewerUpdateViewers(data, PALVIEWER_UPDATE_ALL);
	}
	free(invmap);
	free(map);
}

static void PalViewerOutlineSelection(NCLRVIEWERDATA *data, HDC hDC, int selStart, int selEnd) {
	//use the current pen and current selection mode to highlight a selection region
	HBRUSH hHollowBrush = (HBRUSH) GetStockObject(HOLLOW_BRUSH);
	HBRUSH hOldBrush = (HBRUSH) SelectObject(hDC, hHollowBrush);

	int selStartX, selStartY, selWidth, selHeight;
	PalViewerGetSelectionDimensionsForRange(data, selStart, selEnd, &selStartX, &selStartY, &selWidth, &selHeight);

	if (data->selMode == PALVIEWER_SELMODE_2D || (selStartX + selWidth <= 16)) {
		//outline rectangle
		int rectX = selStartX * COLOR_SIZE, rectY = selStartY * COLOR_SIZE;
		int rectW = selWidth * COLOR_SIZE, rectH = selHeight * COLOR_SIZE;
		Rectangle(hDC, rectX, rectY, rectX + rectW, rectY + rectH);
	} else {
		//piecewise
		int selEndX = selEnd % 16;
		int selEndY = selEnd / 16;
		int nRows = selEndY - selStartY + 1;

		//top row
		{
			MoveToEx(hDC, 0, (selStartY + 1) * COLOR_SIZE, NULL);
			LineTo(hDC, selStartX * COLOR_SIZE, (selStartY + 1) * COLOR_SIZE);
			LineTo(hDC, selStartX * COLOR_SIZE, selStartY * COLOR_SIZE);
			LineTo(hDC, 16 * COLOR_SIZE - 1, selStartY * COLOR_SIZE);
			LineTo(hDC, 16 * COLOR_SIZE - 1, (selStartY + 1) * COLOR_SIZE);
		}

		//middle
		if (nRows >= 3) {
			MoveToEx(hDC, 0, (selStartY + 1) * COLOR_SIZE, NULL);
			LineTo(hDC, 0, selEndY * COLOR_SIZE - 1);
			MoveToEx(hDC, 16 * COLOR_SIZE - 1, (selStartY + 1) * COLOR_SIZE, NULL);
			LineTo(hDC, 16 * COLOR_SIZE - 1, selEndY * COLOR_SIZE);
		}

		//bottom row
		{
			MoveToEx(hDC, 0, selEndY * COLOR_SIZE - 1, NULL);
			LineTo(hDC, 0, (selEndY + 1) * COLOR_SIZE - 1);
			LineTo(hDC, (selEndX + 1) * COLOR_SIZE - 1, (selEndY + 1) * COLOR_SIZE - 1);
			LineTo(hDC, (selEndX + 1) * COLOR_SIZE - 1, selEndY * COLOR_SIZE - 1);
			LineTo(hDC, 16 * COLOR_SIZE - 1, selEndY * COLOR_SIZE - 1);
		}
	}

	SelectObject(hDC, hOldBrush);
}

static void PalViewerPaint(NCLRVIEWERDATA *data, HDC hDC, int xMin, int yMin, int xMax, int yMax) {
	COLOR *cols = data->nclr->colors;
	int nRows = (data->nclr->nColors + 15) / 16;

	//if we're dragging a selection, preview that here.
	if (data->movingSelection) {
		cols = PalViewerComputeViewPalette(data);
	}

	int previewPalette = -1;
	int nRowsPerPalette = (1 << data->nclr->nBits) / 16;

	HWND hWndNcgrViewer = PalViewerGetAssociatedWindow(data, FILE_TYPE_CHARACTER);
	HWND hWndNscrViewer = PalViewerGetAssociatedWindow(data, FILE_TYPE_SCREEN);
	if (hWndNcgrViewer != NULL) {
		NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer);
		previewPalette = ncgrViewerData->selectedPalette;
		nRowsPerPalette = (1 << ncgrViewerData->ncgr->nBits) / 16;
	}
	
	int highlightRowStart = previewPalette * nRowsPerPalette;
	int highlightRowEnd = highlightRowStart + nRowsPerPalette;
	
	int palOpSrcIndex = -1, palOpSrcLength = 0, palOpDstIndex = -1, palOpStrideLength = 0, palOpBlocks = 0;
	if (data->palOpDialog) {
		palOpSrcIndex = data->palOp.srcIndex;
		palOpSrcLength = data->palOp.srcLength;
		palOpDstIndex = data->palOp.dstOffset * data->palOp.dstStride + data->palOp.srcIndex;
		palOpStrideLength = data->palOp.dstStride;
		palOpBlocks = data->palOp.dstCount;
	}

	//get use counts
	unsigned int *freqs = NULL;
	int maxLevel = 0;
	if (data->showFrequency || data->showUnused) {
		freqs = (unsigned int *) calloc(data->nclr->nColors, sizeof(unsigned int));

		int hasCount = CountPaletteUsages(data, freqs);
		if (!hasCount) {
			free(freqs);
			freqs = NULL;
		}
	}

	//get max level
	if (freqs != NULL) {
		for (int i = 0; i < data->nclr->nColors; i++) {
			int count = freqs[i];
			if (count > maxLevel) maxLevel = count;
		}
	}

	SetBkColor(hDC, RGB(0, 0, 0));
	for (int y = yMin / COLOR_SIZE; y < nRows && y < (yMax + COLOR_SIZE - 1) / COLOR_SIZE; y++) {
		for (int x = 0; x < 16; x++) {
			int index = x + y * 16;
			int colorIndex = index;
			if (index >= data->nclr->nColors) break;

			COLOR col = cols[colorIndex];
			COLOR32 rgb = ColorConvertFromDS(col);

			HBRUSH hbr = CreateSolidBrush(rgb);
			SelectObject(hDC, hbr);

			//is in palette operation destination area?
			int isInPalOpDest = 0;
			if (data->palOpDialog && palOpStrideLength) {
				int dstRel = x + y * 16 - palOpDstIndex;
				if (dstRel >= 0 && dstRel < (palOpBlocks - 1) * palOpStrideLength + palOpSrcLength) {
					dstRel %= palOpStrideLength;
					if (dstRel < palOpSrcLength) {
						isInPalOpDest = 1;
					}
				}
			}

			//frequency
			int level = 0;
			if (freqs != NULL && data->showFrequency && maxLevel > 0) {
				level = (int) (255.0f * pow(((float) freqs[index]) / maxLevel, 0.5f));
			}

			HPEN hOutlinePen = NULL;
			COLORREF outlineColor = 0;
			int outlineStyle = PS_SOLID;
			if (y * 16 + x >= palOpSrcIndex && y * 16 + x < palOpSrcIndex + palOpSrcLength) {
				outlineColor = RGB(255, 255, 0);
			} else if (isInPalOpDest) {
				outlineColor = RGB(0, 192, 128);
				outlineStyle = PS_DOT;
			} else if (PalViewerIndexInSelection(data, index)) {
				outlineColor = RGB(255, 255, 255);
			} else if (index == data->hoverIndex && !data->movingSelection) {
				outlineColor = RGB(192, 192, 192);
			} else if (previewPalette != -1 && (y >= highlightRowStart && y < highlightRowEnd)) {
				if (!data->showFrequency) outlineColor = RGB(192, 0, 0); //
				else outlineColor = RGB(192, level, level);
			} else {
				if (!data->showFrequency) outlineColor = RGB(0, 0, 0);
				else outlineColor = RGB(level, level, level);
			}
			hOutlinePen = CreatePen(outlineStyle, 1, outlineColor);

			HPEN hOldPen = SelectObject(hDC, hOutlinePen);
			Rectangle(hDC, x * COLOR_SIZE, y * COLOR_SIZE, (x + 1) * COLOR_SIZE, (y + 1) * COLOR_SIZE);
			SelectObject(hDC, hOldPen);

			//if frequency is 0 and we're outlining frequencies, slash this color
			if (freqs != NULL && data->showUnused && freqs[index] == 0) {
				COLOR32 slashColor = MakeContrastingColor(rgb);
				HPEN hSlashPen = CreatePen(PS_SOLID, 1, slashColor);
				HPEN hOldPen = SelectObject(hDC, hSlashPen);

				MoveToEx(hDC, (x + 0) * COLOR_SIZE + 1, (y + 1) * COLOR_SIZE - 2, NULL);
				LineTo(hDC, (x + 1) * COLOR_SIZE - 1, (y + 0) * COLOR_SIZE);
				MoveToEx(hDC, (x + 0) * COLOR_SIZE + 1, (y + 0) * COLOR_SIZE + 1, NULL);
				LineTo(hDC, (x + 1) * COLOR_SIZE - 1, (y + 1) * COLOR_SIZE - 1);

				SelectObject(hDC, hOldPen);
				DeleteObject(hSlashPen);
			}

			DeleteObject(hbr);
			DeleteObject(hOutlinePen);
		}
	}

	//if dragging, outline drag target
	if (data->movingSelection) {
		HPEN hDotOutline = CreatePen(/*PS_DOT*/PS_SOLID, 1, RGB(0, 255, 255));
		HPEN hOldPen = SelectObject(hDC, hDotOutline);
		int dx, dy;
		int d = PalViewerGetDragDelta(data, &dx, &dy);
		PalViewerOutlineSelection(data, hDC, min(data->selStart, data->selEnd) + d, max(data->selStart, data->selEnd) + d);
		SelectObject(hDC, hOldPen);
		DeleteObject(hDotOutline);
	}

	if (freqs != NULL) {
		free(freqs);
	}
	if (cols != data->nclr->colors) {
		free(cols); //needs to free
	}
}

static void NclrViewerPalOpUpdateCallback(PAL_OP *palOp) {
	HWND hWnd = (HWND) palOp->param;
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) EditorGetData(hWnd);

	PalopRunOperation(data->tempPalette, data->nclr->colors, data->nclr->nColors, palOp);
	PalViewerUpdatePreview(data);
}

static int PalViewerLightness(COLOR col) {
	RxYiqColor yiq;
	RxConvertRgbToYiq(ColorConvertFromDS(col) | 0xFF000000, &yiq);
	
	return (int) (yiq.y + 0.5f);
}

typedef struct PalViewerSortEntry_ {
	COLOR col;
	int srcIndex;
	int dstIndex;
	int frequency;
} PalViewerSortEntry;

static int PalViewerSortLightness(const void *p1, const void *p2) {
	COLOR c1 = ((PalViewerSortEntry *) p1)->col;
	COLOR c2 = ((PalViewerSortEntry *) p2)->col;
	return PalViewerLightness(c1) - PalViewerLightness(c2);
}

static int PalViewerSortHue(const void *p1, const void *p2) {
	COLOR c1 = ((PalViewerSortEntry *) p1)->col;
	COLOR c2 = ((PalViewerSortEntry *) p2)->col;

	COLOR32 col1 = ColorConvertFromDS(c1);
	COLOR32 col2 = ColorConvertFromDS(c2);

	int h1, s1, v1, h2, s2, v2;
	ConvertRGBToHSV(col1, &h1, &s1, &v1);
	ConvertRGBToHSV(col2, &h2, &s2, &v2);
	return h1 - h2;
}

static int PalViewerSortFrequency(const void *p1, const void *p2) {
	int freq1 = ((PalViewerSortEntry *) p1)->frequency;
	int freq2 = ((PalViewerSortEntry *) p2)->frequency;
	return freq2 - freq1;
}

// callback for verify selection

typedef struct {
	int start;
	int end;
	int mode;
} NSCR_VERIFY_DATA;

BOOL ValidateColorsNscrProc(HWND hWnd, void *param) {
	NSCR_VERIFY_DATA *verif = (NSCR_VERIFY_DATA *) param;
	NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(hWnd);
	nscrViewerData->hlStart = verif->start;
	nscrViewerData->hlEnd = verif->end;
	nscrViewerData->hlMode = verif->mode;
	nscrViewerData->verifyFrames = 10;
	SetTimer(hWnd, 1, 100, NULL);
	return TRUE;
}

static void PalViewerSwapColors(COLOR *palette, int i1, int i2) {
	COLOR c1 = palette[i1];
	palette[i1] = palette[i2];
	palette[i2] = c1;
}

static void PalViewerConvertRgbToYuv(int r, int g, int b, double *y, double *u, double *v) {
	*y =  0.2990 * r + 0.5870 * g + 0.1140 * b;
	*u = -0.1684 * r - 0.3316 * g + 0.5000 * b;
	*v =  0.5000 * r - 0.4187 * g - 0.0813 * b;
}

static double PalViewerSortNeuroPermute(COLOR *palette, int nColors, double bestDiff) {
	double totalDiff = 0;
	for (int i = 1; i < nColors; i++) {
		COLOR32 last = ColorConvertFromDS(palette[i - 1]);
		int nextIndex = i;

		double minDiff = 1e32;
		for (int j = i; j < nColors; j++) {
			COLOR32 test = ColorConvertFromDS(palette[j]);
			
			double dy, du, dv;
			int dr = ((last >>  0) & 0xFF) - ((test >>  0) & 0xFF);
			int dg = ((last >>  8) & 0xFF) - ((test >>  8) & 0xFF);
			int db = ((last >> 16) & 0xFF) - ((test >> 16) & 0xFF);
			PalViewerConvertRgbToYuv(dr, dg, db, &dy, &du, &dv);

			double diff = 4.0 * dy * dy + du * du + dv * dv;
			if (diff < minDiff) {
				nextIndex = j;
				minDiff = diff;
			}
		}

		PalViewerSwapColors(palette, i, nextIndex);
		totalDiff += minDiff;
		if (totalDiff >= bestDiff) return totalDiff;
	}
	return totalDiff;
}

static DWORD CALLBACK PalViewerSortNeuro(LPVOID param) {
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) param;
	HWND hWnd = data->hWnd;
	int nColors = PalViewerGetSelectionSize(data);

	COLOR *palette = (COLOR *) calloc(nColors, sizeof(COLOR));
	PalViewerUnwrapSelection(data, palette);

	double best = 1e32;
	COLOR *tempBuf = (COLOR *) calloc(nColors, sizeof(COLOR));

	//iterate permutations
	for (int i = 0; i < nColors; i++) {
		memcpy(tempBuf, palette, nColors * sizeof(COLOR));
		PalViewerSwapColors(tempBuf, 0, i);

		double permutationError = PalViewerSortNeuroPermute(tempBuf, nColors, best);
		if (permutationError < best) {
			memcpy(palette, tempBuf, nColors * sizeof(COLOR));
			best = permutationError;
			PalViewerWrapSelection(data, palette, nColors);
			PostMessage(hWnd, NV_XTINVALIDATE, 0, 0);
		}
	}
	PostMessage(hWnd, NV_XTINVALIDATE, 0, 0);
	PostMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

	free(tempBuf);
	free(palette);
	return 0;
}

static void PalViewerGetClientCursorPosition(HWND hWnd, int *px, int *py) {
	POINT mousePos;
	GetCursorPos(&mousePos);
	ScreenToClient(hWnd, &mousePos);

	//adjust for scroll
	SCROLLINFO horiz, vert;
	horiz.cbSize = sizeof(horiz);
	vert.cbSize = sizeof(vert);
	horiz.fMask = SIF_ALL;
	vert.fMask = SIF_ALL;
	GetScrollInfo(hWnd, SB_HORZ, &horiz);
	GetScrollInfo(hWnd, SB_VERT, &vert);
	mousePos.x += horiz.nPos;
	mousePos.y += vert.nPos;
	
	*px = mousePos.x;
	*py = mousePos.y;
}

static void PalViewerSortNeuroThreadProc(NCLRVIEWERDATA *data) {
	DWORD tid;
	CreateThread(NULL, 0, PalViewerSortNeuro, (LPVOID) data, 0, &tid);
}

static void PalViewerSortSelection(HWND hWnd, NCLRVIEWERDATA *data, int command) {
	int type = command;
	if (type != ID_ARRANGEPALETTE_NEURO) {
		//fast palette sorts, can be done right here. First, unwrap the selection into a linear block.
		int selSize = PalViewerGetSelectionSize(data);
		PalViewerSortEntry *tmp = (PalViewerSortEntry *) calloc(selSize, sizeof(PalViewerSortEntry));

		//compute frequency list
		int *freqList = (int *) calloc(data->nclr->nColors, sizeof(int));
		int n = CountPaletteUsages(data, freqList);

		//unwrap the selection into an array to sort.
		int destIndex = 0;
		for (int i = 0; i < data->nclr->nColors; i++) {
			if (!PalViewerIndexInSelection(data, i)) continue;
			tmp[destIndex].col = data->nclr->colors[i];
			tmp[destIndex].srcIndex = i;
			tmp[destIndex].dstIndex = i;
			tmp[destIndex].frequency = freqList[i];

			destIndex++;
		}
		free(freqList);

		//sort the color array.
		int (*comparator) (const void *, const void *) = NULL;
		switch (command) {
			case ID_ARRANGEPALETTE_BYLIGHTNESS:
				comparator = PalViewerSortLightness;
				break;
			case ID_ARRANGEPALETTE_BYHUE:
				comparator = PalViewerSortHue;
				break;
			case ID_ARRANGEPALETTE_BYFREQUENCY:
				comparator = PalViewerSortFrequency;
				break;
		}
		qsort(tmp, selSize, sizeof(PalViewerSortEntry), comparator);

		//re-wrap the selection back into the palette.
		int srcIndex = 0;
		for (int i = 0; i < data->nclr->nColors && srcIndex < selSize; i++) {
			if (!PalViewerIndexInSelection(data, i)) continue;
			tmp[srcIndex].dstIndex = i;
			data->nclr->colors[i] = tmp[srcIndex++].col;
		}

		//if enabled: modify graphics data to preserve the picture
		HWND hWndMain = data->editorMgr->hWnd;
		{
			StList scrEditors;
			StListCreateInline(&scrEditors, NSCRVIEWERDATA *, NULL);
			EditorGetAllByType(hWndMain, FILE_TYPE_SCREEN, &scrEditors);

			HWND hWndNcgrViewer = PalViewerGetAssociatedWindow(data, FILE_TYPE_CHARACTER);

			if (hWndNcgrViewer != NULL) {
				NCGR *ncgr = (NCGR *) EditorGetObject(hWndNcgrViewer);

				uint8_t *tilePalettes = (uint8_t *) calloc(ncgr->nTiles, sizeof(int));
				memset(tilePalettes, 0, ncgr->nTiles);

				if (scrEditors.length > 0) {
					//do graphics transform with respect to screen data.
					for (size_t i = 0; i < scrEditors.length; i++) {
						//determine which palette each tile is using.
						NSCRVIEWERDATA *nscrViewerData = *(NSCRVIEWERDATA **) StListGetPtr(&scrEditors, i);
						NSCR *nscr = nscrViewerData->nscr;

						for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
							uint16_t scrd = nscr->data[i];
							int chrno = (scrd & 0x3FF) - nscrViewerData->tileBase;
							if (chrno >= 0 && chrno < ncgr->nTiles) tilePalettes[chrno] = scrd >> 12;
						}
					}
				} else {
					//do graphics transform with respect to only character data. Assume all characters 
					//use the selected palette.
					NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer);
					for (int i = 0; i < ncgr->nTiles; i++) {
						tilePalettes[i] = ncgrViewerData->selectedPalette;
					}
				}

				//apply transform
				for (int i = 0; i < ncgr->nTiles; i++) {
					int pltBase = tilePalettes[i] << ncgr->nBits;
					unsigned char *tile = ncgr->tiles[i];

					for (int i = 0; i < 64; i++) {
						int cidx = tile[i] + pltBase;
						
						//map to new index
						int to = cidx;
						for (int j = 0; j < selSize; j++) {
							if (cidx == tmp[j].srcIndex) {
								to = tmp[j].dstIndex;
								break;
							}
						}
						tile[i] = (to - pltBase) & ((1 << ncgr->nBits) - 1);
					}
				}

				free(tilePalettes);

			}
			StListFree(&scrEditors);
		}
		free(tmp);

		//update all editors dependent
		EditorInvalidateAllByType(hWndMain, FILE_TYPE_CHAR);
		EditorInvalidateAllByType(hWndMain, FILE_TYPE_SCREEN);
		PalViewerUpdateNcerViewer(data);
	} else {
		PalViewerSortNeuroThreadProc(data);
	}

	PalViewerUpdatePreview(data);
}

static LRESULT WINAPI PalViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
		{
			data->editMode = PALVIEWER_MODE_SELECTION;
			data->selMode = PALVIEWER_SELMODE_2D;
			data->selStart = -1;
			data->selEnd = -1;

			//get UI scale
			sColorCellSize = (int) (GetDpiScale() * COLOR_SIZE_DEFAULT + 0.5);

			data->frameData.contentWidth = 0; //prevent horizontal scrollbar
			data->frameData.contentHeight = 16 * COLOR_SIZE;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;

			PAL_OP *palOp = &data->palOp;
			palOp->hWndParent = data->editorMgr->hWnd;
			palOp->param = (void *) hWnd;
			palOp->dstOffset = 1;
			palOp->ignoreFirst = 0;
			palOp->dstCount = 1;
			palOp->dstStride = 16;
			palOp->srcLength = 16;
			palOp->updateCallback = NclrViewerPalOpUpdateCallback;

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentWidth;
			info.nPos = 0;
			info.nPage = rcClient.right - rcClient.left;
			info.nTrackPos = 0;
			info.fMask = SIF_POS | SIF_RANGE | SIF_POS | SIF_TRACKPOS | SIF_PAGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = data->frameData.contentHeight;
			info.nPage = rcClient.bottom - rcClient.top;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			break;
		}
		case NV_INITIALIZE:
		{
			data->nclr = (NCLR *) lParam;

			LPCWSTR path = (LPCWSTR) wParam;
			if (path != NULL) EditorSetFile(hWnd, path);

			PalViewerUpdatePreview(data);

			HWND hWndMain = data->editorMgr->hWnd;
			EditorInvalidateAllByType(hWndMain, FILE_TYPE_CHAR);
			EditorInvalidateAllByType(hWndMain, FILE_TYPE_SCREEN);
			PalViewerUpdateNcerViewer(data);

			if (data->nclr->header.format == NCLR_TYPE_HUDSON) {
				SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
			}

			//set appropriate height
			data->frameData.contentHeight = ((data->nclr->nColors + 15) / 16) * COLOR_SIZE;
			if (data->nclr->nColors > 256) {
				SetWindowSize(hWnd, 16 * COLOR_SIZE + 4 + GetSystemMetrics(SM_CXVSCROLL), 16 * COLOR_SIZE + 4);
			} else {
				SetWindowSize(hWnd, 16 * COLOR_SIZE + 4, 16 * COLOR_SIZE + 4);
			}

			//update scroll info
			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentHeight;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			InvalidateRect(hWnd, NULL, FALSE);
			return 1;
		}
		case NV_UPDATEPREVIEW:
			PalViewerUpdatePreview(data);
			break;
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		{
			POINT mousePos;
			PalViewerGetClientCursorPosition(hWnd, &mousePos.x, &mousePos.y);

			//if we're dragging, clamp position
			if (data->mouseDown) {
				int nRows = ((data->nclr->nColors + 15) / 16);
				if (mousePos.x < 0) mousePos.x = 0;
				else if (mousePos.x >= (16 * COLOR_SIZE)) mousePos.x = 16 * COLOR_SIZE - 1;
				if (mousePos.y < 0) mousePos.y = 0;
				else if (mousePos.y >= (nRows * COLOR_SIZE)) mousePos.y = nRows * COLOR_SIZE - 1;
			}

			int colorX = mousePos.x / COLOR_SIZE;
			int colorY = mousePos.y / COLOR_SIZE;
			if (data->makingSelection && data->rowSelection) {
				//row
				colorX = 15;
			}

			int colorIndex = colorX + colorY * 16;
			if (data->mouseDown && (data->dragStart.x / COLOR_SIZE != colorX || data->dragStart.y / COLOR_SIZE != colorY)) {
				data->dragging = 1;
			}
			data->dragPoint.x = mousePos.x;
			data->dragPoint.y = mousePos.y;

			//if dragging and we're in selection mode, move the selection end
			if (data->makingSelection && data->editMode == PALVIEWER_MODE_SELECTION) {
				data->selEnd = colorIndex;
			}

			TRACKMOUSEEVENT evt;
			evt.cbSize = sizeof(evt);
			evt.hwndTrack = hWnd;
			evt.dwHoverTime = 0;
			evt.dwFlags = TME_LEAVE;
			TrackMouseEvent(&evt);

		}
		case WM_MOUSELEAVE:
		{
			POINT mousePos;
			PalViewerGetClientCursorPosition(hWnd, &mousePos.x, &mousePos.y);

			int nRows = data->nclr->nColors / 16;

			int hoverX = -1, hoverY = -1, hoverIndex = -1;
			if (mousePos.x >= 0 && mousePos.x < (16 * COLOR_SIZE) && mousePos.y >= 0) {
				hoverX = mousePos.x / COLOR_SIZE;
				hoverY = mousePos.y / COLOR_SIZE;
				hoverIndex = hoverX + hoverY * 16;
				if (hoverY >= nRows) {
					hoverX = -1, hoverY = -1, hoverIndex = -1;
				}
			}
			if (msg == WM_MOUSELEAVE) {
				hoverX = -1, hoverY = -1, hoverIndex = -1;
			}
			data->hoverX = hoverX;
			data->hoverY = hoverY;
			data->hoverIndex = hoverIndex;
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_LBUTTONDOWN:
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			int shiftPressed = GetKeyState(VK_SHIFT) >> 15;
			int ctrlPressed = GetKeyState(VK_CONTROL) >> 15;

			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < (16 * COLOR_SIZE)) {
				PalViewerGetClientCursorPosition(hWnd, &mousePos.x, &mousePos.y);

				int x = mousePos.x / COLOR_SIZE;
				int y = mousePos.y / COLOR_SIZE;
				int index = y * 16 + x;
				if (index < data->nclr->nColors) {
					data->mouseDown = 1;
					data->dragging = 0;
					data->preserveDragging = !!shiftPressed;

					PalViewerGetClientCursorPosition(hWnd, &mousePos.x, &mousePos.y);

					data->dragStart.x = mousePos.x;
					data->dragStart.y = mousePos.y;

					SetCapture(hWnd);

					//if we're in selection mode and this color isn't in the selection, start a new selection
					if (data->editMode == PALVIEWER_MODE_SELECTION && !PalViewerIndexInSelection(data, index)) {
						data->selStart = index;
						data->selEnd = index;
						data->makingSelection = 1;
						if (ctrlPressed) {
							data->selStart &= ~0xF; //row mask
							data->selEnd = data->selStart + 0xF; //end of row
							data->rowSelection = 1;
							data->selMode = PALVIEWER_SELMODE_2D; //so it works both ways
						}
					} else if (PalViewerIndexInSelection(data, index)) {
						//start selection drag
						data->movingSelection = 1;
					}
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}

			break;
		}
		case WM_LBUTTONUP:
		{
			if (!data->mouseDown) break;
			ReleaseCapture();

			POINT mousePos;
			PalViewerGetClientCursorPosition(hWnd, &mousePos.x, &mousePos.y);

			if (!data->dragging && !data->makingSelection) {

				//if it is within the colors area, open a color chooser
				if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < (16 * COLOR_SIZE)) {
					int x = mousePos.x / COLOR_SIZE;
					int y = mousePos.y / COLOR_SIZE;
					int index = y * 16 + x;
					if (index < data->nclr->nColors && index >= 0) {
						data->selStart = data->selEnd = index;

						HWND hWndMain = data->editorMgr->hWnd;
						if (NpChooseColor15(hWndMain, hWndMain, &data->nclr->colors[index])) {
							EditorInvalidateAllByType(hWndMain, FILE_TYPE_CHAR);
							EditorInvalidateAllByType(hWndMain, FILE_TYPE_SCREEN);
							PalViewerUpdateNcerViewer(data);
							PalViewerUpdatePreview(data);
						}
					}
				}
			}

			if (data->dragging && data->movingSelection) {
				//complete drag operation
				COLOR *result = PalViewerComputeViewPalette(data);
				memcpy(data->nclr->colors, result, data->nclr->nColors * sizeof(COLOR));
				free(result);

				//now: if we use a preserve drag, update accordingly.
				if (data->preserveDragging) {
					NCGR *ncgr = NULL;
					HWND hWndNcgrViewer = PalViewerGetAssociatedWindow(data, FILE_TYPE_CHAR);
					if (hWndNcgrViewer != NULL) ncgr = (NCGR *) EditorGetObject(hWndNcgrViewer);

					//get all screen editors
					StList scrEditors;
					StListCreateInline(&scrEditors, NSCRVIEWERDATA *, NULL);
					EditorGetAllByType(data->editorMgr->hWnd, FILE_TYPE_SCREEN, &scrEditors);

					NSCR **nscrs = (NSCR **) calloc(scrEditors.length, sizeof(NSCR *));
					for (size_t i = 0; i < scrEditors.length; i++) {
						nscrs[i] = (*(NSCRVIEWERDATA **) StListGetPtr(&scrEditors, i))->nscr;
					}

					PalViewerDoPreserveTransform(data, ncgr, nscrs, scrEditors.length);
					StListFree(&scrEditors);
					free(nscrs);
				}

				//move drag selection
				int dx, dy;
				int delta = PalViewerGetDragDelta(data, &dx, &dy);
				data->selStart += delta;
				data->selEnd += delta;

				PalViewerUpdatePreview(data);
				PalViewerUpdateViewers(data, PALVIEWER_UPDATE_CHAR | PALVIEWER_UPDATE_CELL | PALVIEWER_UPDATE_SCREEN);
			}

			data->mouseDown = 0;
			data->makingSelection = 0;
			data->movingSelection = 0;
			data->dragging = 0;
			data->rowSelection = 0;
			break;
		}
		case WM_RBUTTONUP:
		{
			POINT mousePos;
			PalViewerGetClientCursorPosition(hWnd, &mousePos.x, &mousePos.y);

			int hoverY = data->hoverY;
			int hoverX = data->hoverX;

			//if it is within the colors area, open a color chooser
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < 16 * COLOR_SIZE) {
				int x = mousePos.x / 16;
				int y = mousePos.y / 16;
				int index = y * 16 + x;
				if (index < data->nclr->nColors) {
					HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 0);

					//set menu state
					const int cmdsForSelection[] = {
						ID_MENU_COPY, ID_MENU_CUT, ID_MENU_DESELECT,
						ID_MENU_IMPORT, ID_MENU_CREATE,
						ID_MENU_INVERTCOLOR, ID_MENU_MAKEGRAYSCALE,
						ID_ARRANGEPALETTE_BYHUE, ID_ARRANGEPALETTE_BYLIGHTNESS, ID_ARRANGEPALETTE_NEURO,
						ID_MENU_ANIMATEPALETTE, ID_MENU_GENERATE
					};
					for (int i = 0; i < sizeof(cmdsForSelection) / sizeof(int); i++) {
						EnableMenuItem(hPopup, cmdsForSelection[i], (data->selStart != -1) ? MF_ENABLED : MF_DISABLED);
					}
					CheckMenuItem(hPopup, ID_MENU_FREQUENCYHIGHLIGHT, data->showFrequency ? MF_CHECKED : MF_UNCHECKED);
					CheckMenuItem(hPopup, ID_MENU_SHOWUNUSED, data->showUnused ? MF_CHECKED : MF_UNCHECKED);
					CheckMenuItem(hPopup, ID_SELECTIONMODE_1D, (data->selMode == PALVIEWER_SELMODE_1D) ? MF_CHECKED : MF_UNCHECKED);
					CheckMenuItem(hPopup, ID_SELECTIONMODE_2D, (data->selMode == PALVIEWER_SELMODE_2D) ? MF_CHECKED : MF_UNCHECKED);
					EnableMenuItem(hPopup, ID_MENU_PASTE, PalViewerHasClipboard(hWnd) ? MF_ENABLED : MF_DISABLED);

					POINT mouse;
					GetCursorPos(&mouse);
					TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL);
					data->contextHoverY = hoverY;
					data->contextHoverX = hoverX;
				}
			}
			break;
		}
		case NV_XTINVALIDATE:
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		case WM_PAINT:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			PAINTSTRUCT ps;
			HDC hWindowDC = BeginPaint(hWnd, &ps);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);


			HDC hDC = CreateCompatibleDC(hWindowDC);
			HBITMAP hBitmap = CreateCompatibleBitmap(hWindowDC, max(data->frameData.contentWidth, horiz.nPos + rcClient.right), max(data->frameData.contentHeight, vert.nPos + rcClient.bottom));
			SelectObject(hDC, hBitmap);
			IntersectClipRect(hDC, horiz.nPos, vert.nPos, horiz.nPos + rcClient.right, vert.nPos + rcClient.bottom);
			DefMDIChildProc(hWnd, WM_ERASEBKGND, (WPARAM) hDC, 0);
			HPEN defaultPen = SelectObject(hDC, GetStockObject(NULL_PEN));
			HBRUSH defaultBrush = SelectObject(hDC, GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1));
			Rectangle(hDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			SelectObject(hDC, defaultPen);
			SelectObject(hDC, defaultBrush);

			PalViewerPaint(data, hDC, horiz.nPos, vert.nPos, horiz.nPos + rcClient.right, vert.nPos + rcClient.bottom);

			BitBlt(hWindowDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hDC, horiz.nPos, vert.nPos, SRCCOPY);
			EndPaint(hWnd, &ps);
			DeleteObject(hDC);
			DeleteObject(hBitmap);
			break;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_COMMAND:
		{
			if (lParam == 0 && HIWORD(wParam) == 1) {
				//accelerator
				WORD accel = LOWORD(wParam);
				switch (accel) {
					case ID_ACCELERATOR_CUT:
						PostMessage(hWnd, WM_COMMAND, ID_MENU_CUT, 0);
						break;
					case ID_ACCELERATOR_COPY:
						PostMessage(hWnd, WM_COMMAND, ID_MENU_COPY, 0);
						break;
					case ID_ACCELERATOR_PASTE:
						PostMessage(hWnd, WM_COMMAND, ID_MENU_PASTE, 0);
						break;
					case ID_ACCELERATOR_DESELECT:
						PostMessage(hWnd, WM_COMMAND, ID_MENU_DESELECT, 0);
						break;
					case ID_ACCELERATOR_SELECT_ALL:
						data->selStart = 0;
						data->selEnd = data->nclr->nColors - 1;
						if (data->nclr->nColors & 15) data->selMode = PALVIEWER_SELMODE_1D; //necessary
						InvalidateRect(hWnd, NULL, FALSE);
						break;
				}
			}
			if (lParam == 0 && HIWORD(wParam) == 0) {
				//menu
				switch (LOWORD(wParam)) {
					case ID_MENU_PASTE:
					{
						OpenClipboard(hWnd);
						PalViewerPastePalette(data);
						CloseClipboard();

						PalViewerUpdateViewers(data, PALVIEWER_UPDATE_ALL);
						PalViewerUpdatePreview(data);
						break;
					}
					case ID_MENU_COPY:
					{
						OpenClipboard(hWnd);
						EmptyClipboard();
						PalViewerCopyPalette(data);
						CloseClipboard();
						break;
					}
					case ID_MENU_CUT:
					{
						OpenClipboard(hWnd);
						EmptyClipboard();
						PalViewerCopyPalette(data);
						CloseClipboard();

						//erase all colors in selected region
						for (int i = 0; i < data->nclr->nColors; i++) {
							if (!PalViewerIndexInSelection(data, i)) continue;
							data->nclr->colors[i] = 0;
						}
						PalViewerUpdateViewers(data, PALVIEWER_UPDATE_ALL);
						PalViewerUpdatePreview(data);
						break;
					}
					case ID_MENU_INVERTCOLOR:
					{
						COLOR *pal = data->nclr->colors;
						for (int i = 0; i < data->nclr->nColors; i++) {
							if (!PalViewerIndexInSelection(data, i)) continue;
							pal[i] ^= 0x7FFF;
						}
						PalViewerUpdatePreview(data);
						break;
					}
					case ID_MENU_MAKEGRAYSCALE:
					{
						COLOR *pal = data->nclr->colors;
						for (int i = 0; i < data->nclr->nColors; i++) {
							if (!PalViewerIndexInSelection(data, i)) continue;

							int l = (PalViewerLightness(pal[i]) * 31 + 255) / 511;
							pal[i] = ColorCreate(l, l, l);
						}
						PalViewerUpdatePreview(data);
						break;
					}
					case ID_ARRANGEPALETTE_BYFREQUENCY:
					case ID_ARRANGEPALETTE_BYLIGHTNESS:
					case ID_ARRANGEPALETTE_BYHUE:
					case ID_ARRANGEPALETTE_NEURO:
						PalViewerSortSelection(hWnd, data, LOWORD(wParam));
						break;
					case ID_MENU_EDITCOMPRESSEDPALETTE:
					{
						HWND hWndMdi = data->editorMgr->hWnd;
						HWND h = CreateWindow(L"EditCompressedClass", L"Edit Compressed Palette",
							WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
							CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, NULL, NULL, NULL);
						SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
						DoModal(h);
						break;
					}
					case ID_FILE_SAVE:
						EditorSave(hWnd);
						break;
					case ID_FILE_SAVEAS:
						EditorSaveAs(hWnd);
						break;
					case ID_FILE_EXPORT:
					{
						LPWSTR path = saveFileDialog(data->editorMgr->hWnd, L"Export Palette",
							L"PNG Files (*.png)\0*.png\0ACT Files (*.act)\0*.act\0All Files\0*.*\0",
							L"png");
						if (path == NULL) break;

						wchar_t *ext = wcsrchr(path, L'.');

						if (ext != NULL && _wcsicmp(ext, L".act") == 0) {
							//ACT file
							if (data->nclr->nColors > 256) {
								MessageBox(hWnd, L"ACT file does not support more than 256 color palettes.", L"Warning", MB_ICONWARNING);
							}

							unsigned int bufSize = 256 * 3 + 4;
							unsigned char *buf = (unsigned char *) calloc(bufSize, 1);

							int nColorsWrite = data->nclr->nColors;
							if (nColorsWrite > 256) nColorsWrite = 256;
							for (int i = 0; i < nColorsWrite; i++) {
								COLOR32 c = ColorConvertFromDS(data->nclr->colors[i]);
								buf[3 * i + 0] = (c >>  0) & 0xFF;
								buf[3 * i + 1] = (c >>  8) & 0xFF;
								buf[3 * i + 2] = (c >> 16) & 0xFF;
							}
							*(uint16_t *) (buf + 3 * 0x100 + 0) = nColorsWrite;
							*(uint16_t *) (buf + 3 * 0x100 + 2) = 0;

							HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
							if (hFile != INVALID_HANDLE_VALUE) {
								DWORD dwWritten;
								WriteFile(hFile, buf, bufSize, &dwWritten, NULL);
								CloseHandle(hFile);
							}
							free(buf);
						} else {
							//construct bitmap
							int width = 16;
							int height = (data->nclr->nColors + 15) / 16;
							COLOR32 *bits = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
							for (int i = 0; i < data->nclr->nColors; i++) {
								COLOR32 as32 = ColorConvertFromDS(data->nclr->colors[i]) | 0xFF000000;
								bits[i] = as32;
							}
							ImgWrite(bits, width, height, path);
						}
						free(path);

						break;
					}
					case ID_MENU_IMPORT:
					{
						LPWSTR filter = L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0"
							L"Palette Files\0*.nclr;*.rlcn;*.5pl;*.5tx;*.ncl;*.icl;*.acl;*ncl.bin;*icl.bin;*ntfp;*.nbfp\0"
							L"Texture Files\0*.tga;*.5tx;*.tds;\0"
							L"All Files\0*.*\0";
						LPWSTR path = openFileDialog(data->editorMgr->hWnd, L"Import Palette", filter, L"");
						if (path == NULL) break;

						COLOR *colors = NULL;
						int nColors = 0;

						//try texture
						if (TxIdentifyFile(path) != TEXTURE_TYPE_INVALID) {
							TextureObject texture = { 0 };
							TxReadFile(&texture, path);

							if (texture.texture.palette.pal != NULL) {
								nColors = texture.texture.palette.nColors;
								colors = (COLOR *) calloc(nColors, sizeof(COLOR));
								memcpy(colors, texture.texture.palette.pal, nColors * sizeof(COLOR));
							}
							TxFree(&texture.header);
						} else {
							//try image
							int width, height;
							COLOR32 *src = ImgRead(path, &width, &height);
							if (src != NULL) {
								//convert to DS 15bpp colors
								nColors = width * height;
								colors = (COLOR *) calloc(nColors, sizeof(COLOR));
								for (int i = 0; i < nColors; i++) {
									colors[i] = ColorConvertToDS(src[i]);
								}
								free(src);
							} else {
								//try palette file
								NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
								PalReadFile(nclr, path);

								nColors = nclr->nColors;
								colors = (COLOR *) calloc(nColors, sizeof(COLOR));
								memcpy(colors, nclr->colors, nColors * sizeof(COLOR));

								ObjFree(&nclr->header);
								free(nclr);
							}
						}
						free(path);

						if (colors != NULL) {
							PalViewerWrapSelection(data, colors, nColors);
							PalViewerUpdatePreview(data);
							free(colors);
						}
						break;
					}
					case ID_MENU_VERIFYCOLOR:
					{
						int index = data->hoverX + data->hoverY * 16;
						if (data->hoverX == -1) index = data->contextHoverX + data->contextHoverY * 16;

						//if no selection, use index. Otherwise, use the selection.
						int selStart = index, selEnd = index;
						if (data->selStart != -1) {
							selStart = min(data->selStart, data->selEnd);
							selEnd = max(data->selStart, data->selEnd);
						}
						
						HWND hWndNcgrViewer = PalViewerGetAssociatedWindow(data, FILE_TYPE_CHARACTER);
						if (hWndNcgrViewer) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer);
							ncgrViewerData->verifyStart = selStart;
							ncgrViewerData->verifyEnd = selEnd;
							ncgrViewerData->verifySelMode = data->selMode;
							ncgrViewerData->verifyFrames = 10;
							SetTimer(hWndNcgrViewer, 1, 100, NULL);
						}

						NSCR_VERIFY_DATA verif = { 0 };
						verif.start = selStart;
						verif.end = selEnd;
						verif.mode = data->selMode;

						//update all screen editors
						StList scrEditors;
						StListCreateInline(&scrEditors, NSCRVIEWERDATA *, NULL);
						EditorGetAllByType(data->editorMgr->hWnd, FILE_TYPE_SCREEN, &scrEditors);

						for (size_t i = 0; i < scrEditors.length; i++) {
							EDITOR_DATA *ed = *(EDITOR_DATA **) StListGetPtr(&scrEditors, i);
							ValidateColorsNscrProc(ed->hWnd, &verif);
						}
						StListFree(&scrEditors);

						break;
					}
					case ID_MENU_FREQUENCYHIGHLIGHT:
					{
						//toggle
						int state = !data->showFrequency;
						data->showFrequency = state;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_MENU_SHOWUNUSED:
					{
						//toggle
						int state = !data->showUnused;
						data->showUnused = state;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_MENU_CREATE:
					{
						HWND hWndMain = data->editorMgr->hWnd;
						HWND hWndPaletteDialog = CreateWindow(L"PaletteGeneratorClass", L"Generate Palette",
							WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX), CW_USEDEFAULT, CW_USEDEFAULT,
							200, 200, hWndMain, NULL, NULL, NULL);
						SendMessage(hWndPaletteDialog, NV_INITIALIZE, 0, (LPARAM) data);
						DoModal(hWndPaletteDialog);
						break;
					}
					case ID_MENU_GENERATE:
					{
						HWND hWndMain = data->editorMgr->hWnd;
						HWND hWndGenerateDialog = CreateWindow(L"GeneratePaletteClass", L"Generate Palette",
							WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
							CW_USEDEFAULT, CW_USEDEFAULT, 200, 200, hWndMain, NULL, NULL, NULL);
						SendMessage(hWndGenerateDialog, NV_INITIALIZE, 0, (LPARAM) data);
						DoModal(hWndGenerateDialog);
						break;
					}
					case ID_MENU_ANIMATEPALETTE:
					{
						data->tempPalette = (COLOR *) calloc(data->nclr->nColors, sizeof(COLOR));
						memcpy(data->tempPalette, data->nclr->colors, data->nclr->nColors * sizeof(COLOR));

						int selStart = min(data->selStart, data->selEnd);
						int selEnd = max(data->selStart, data->selEnd);
						PAL_OP *palOp = &data->palOp;
						palOp->srcIndex = selStart;
						palOp->srcLength = selEnd - selStart + 1;
						data->palOpDialog = 1;
						int n = SelectPaletteOperation(palOp);
						data->palOpDialog = 0;

						memcpy(data->nclr->colors, data->tempPalette, data->nclr->nColors * sizeof(COLOR));
						free(data->tempPalette);
						data->tempPalette = NULL;

						//apply modifier
						if (n) {
							COLOR *cpy = (COLOR *) calloc(data->nclr->nColors, sizeof(COLOR));
							PalopRunOperation(data->nclr->colors, cpy, data->nclr->nColors, palOp);
							memcpy(data->nclr->colors, cpy, data->nclr->nColors * sizeof(COLOR));
							free(cpy);
						}
						PalViewerUpdateViewers(data, PALVIEWER_UPDATE_CHAR | PALVIEWER_UPDATE_CELL | PALVIEWER_UPDATE_SCREEN);
						break;
					}
					case ID_SELECTIONMODE_1D:
						//switch selection mode
						data->selMode = PALVIEWER_SELMODE_1D;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					case ID_SELECTIONMODE_2D:
						//switch selection mode
						data->selMode = PALVIEWER_SELMODE_2D;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					case ID_MENU_DESELECT:
						//reset selection
						data->selStart = data->selEnd = -1;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
				}
			}
			break;
		}
		case WM_KEYDOWN:
		{
			//process key commands
			int cc = wParam;
			switch (cc) {
				case VK_DELETE:
				{
					for (int i = 0; i < data->nclr->nColors; i++) {
						if (!PalViewerIndexInSelection(data, i)) continue;
						data->nclr->colors[i] = 0;
					}
					InvalidateRect(hWnd, NULL, FALSE);
					PalViewerUpdateViewers(data, PALVIEWER_UPDATE_CHAR | PALVIEWER_UPDATE_CELL | PALVIEWER_UPDATE_SCREEN);
					break;
				}
				case VK_ESCAPE:
					SendMessage(hWnd, WM_COMMAND, ID_MENU_DESELECT, 0);
					break;
				case '1':
					SendMessage(hWnd, WM_COMMAND, ID_SELECTIONMODE_1D, 0);
					break;
				case '2':
					SendMessage(hWnd, WM_COMMAND, ID_SELECTIONMODE_2D, 0);
					break;
				case 'V':
					SendMessage(hWnd, WM_COMMAND, ID_MENU_VERIFYCOLOR, 0);
					break;
			}
			break;
		}
		case WM_DESTROY:
		{
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
			nitroPaintStruct->hWndNclrViewer = NULL;
			PalViewerUpdateViewers(data, PALVIEWER_UPDATE_ALL);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK PaletteGeneratorDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case WM_CREATE:
			SetWindowSize(hWnd, 355, 214);	
			break;
		case NV_INITIALIZE:
		{
			data = (NCLRVIEWERDATA *) lParam;
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			//compute defaults
			int depth = data->nclr->nBits, baseIndex = data->selStart;
			int selSize = PalViewerGetSelectionSize(data);

			HWND hWndNcgrViewer = PalViewerGetAssociatedWindow(data, FILE_TYPE_CHARACTER);
			if (hWndNcgrViewer != NULL) {
				NCGR *ncgr = (NCGR *) EditorGetObject(hWndNcgrViewer);
				depth = ncgr->nBits;
			}

			int selStartsPalette = (baseIndex % (1 << depth)) == 0; //first index is a palette start index

			CreateStatic(hWnd, L"Bitmap:", 10, 10, 100, 22);
			data->hWndFileInput = CreateEdit(hWnd, L"", 120, 10, 200, 22, FALSE);
			data->hWndBrowse = CreateButton(hWnd, L"...", 320, 10, 25, 22, FALSE);
			CreateStatic(hWnd, L"Colors:", 10, 37, 100, 22);
			data->hWndColors = CreateEdit(hWnd, L"16", 120, 37, 100, 22, TRUE);
			CreateStatic(hWnd, L"Reserve First:", 10, 64, 100, 22);
			data->hWndReserve = CreateCheckbox(hWnd, L"", 120, 64, 22, 22, selStartsPalette);
			SetEditNumber(data->hWndColors, selSize);

			//palette options
			CreateStatic(hWnd, L"Balance:", 10, 96, 100, 22);
			CreateStaticAligned(hWnd, L"Lightness", 120, 96, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Color", 170 + 150, 95, 50, 22, SCA_LEFT);
			data->hWndBalance = CreateTrackbar(hWnd, 170, 96, 150, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			CreateStatic(hWnd, L"Color Balance:", 10, 123, 100, 22);
			CreateStaticAligned(hWnd, L"Green", 120, 123, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Red", 170 + 150, 123, 50, 22, SCA_LEFT);
			data->hWndColorBalance = CreateTrackbar(hWnd, 170, 123, 150, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			CreateStatic(hWnd, L"Enhance Colors:", 10, 150, 100, 22);
			data->hWndEnhanceColors = CreateCheckbox(hWnd, L"", 120, 150, 22, 22, FALSE);

			data->hWndGenerate = CreateButton(hWnd, L"Generate", 120, 182, 100, 22, TRUE);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			WORD notif = HIWORD(wParam);
			int idc = LOWORD(wParam);
			if (notif == BN_CLICKED && hWndControl == data->hWndBrowse) {
				LPWSTR path = openFilesDialog(hWnd, L"Select Bitmap", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
				if (path != NULL) {
					SendMessage(data->hWndFileInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
					free(path);
				}
			} else if (notif == BN_CLICKED && (hWndControl == data->hWndGenerate || idc == IDOK)) {
				int width, height;
				WCHAR bf[MAX_PATH + 1];
				LPWSTR paths = (LPWSTR) calloc((MAX_PATH + 1) * 32 + 1, sizeof(WCHAR));
				SendMessage(data->hWndFileInput, WM_GETTEXT, (MAX_PATH + 1) * 32 + 1, (LPARAM) paths);
				int nPaths = getPathCount(paths);

				int nColors = GetEditNumber(data->hWndColors);
				int balance = GetTrackbarPosition(data->hWndBalance);
				int colorBalance = GetTrackbarPosition(data->hWndColorBalance);
				if (nColors > 256) nColors = 256;

				BOOL enhanceColors = GetCheckboxChecked(data->hWndEnhanceColors);
				BOOL reserveFirst = GetCheckboxChecked(data->hWndReserve);

				//create palette copy
				int nTotalColors = data->nclr->nColors;
				int index = data->contextHoverX + data->contextHoverY * 16;
				COLOR32 *paletteCopy = (COLOR32 *) calloc(nColors, sizeof(COLOR32));

				//compute histogram
				RxReduction *reduction = RxNew(balance, colorBalance, enhanceColors, nColors - reserveFirst);
				for (int i = 0; i < nPaths; i++) {
					getPathFromPaths(paths, i, bf);
					COLOR32 *bits = ImgRead(bf, &width, &height);
					RxHistAdd(reduction, bits, width, height);
					free(bits);
				}
				RxHistFinalize(reduction);
				free(paths);

				//create and write palette
				RxComputePalette(reduction);
				for (int i = 0; i < nColors - reserveFirst; i++) {
					(paletteCopy + reserveFirst)[i] = reduction->paletteRgb[i];
				}
				qsort(paletteCopy + reserveFirst, nColors - reserveFirst, sizeof(COLOR32), RxColorLightnessComparator);
				RxFree(reduction);

				//convert to 15bpp
				COLOR *as15 = (COLOR *) calloc(nColors, sizeof(COLOR));
				for (int i = 0; i < nColors; i++) {
					as15[i] = ColorConvertToDS(paletteCopy[i]);
				}
				free(paletteCopy);

				//unwrap
				PalViewerWrapSelection(data, as15, nColors);
				free(as15);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
				InvalidateRect((HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT), NULL, FALSE);
			} else if (idc == IDCANCEL) {
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

typedef struct PalViewerFillPaletteData_ {
	HWND hWndType;
	HWND hWndChoose1;
	HWND hWndChoose2;
	HWND hWndOK;
	COLOR col1;
	COLOR col2;
	NCLRVIEWERDATA *nclrViewerData;
} PalViewerFillPaletteData;

static LRESULT CALLBACK GeneratePaletteDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	PalViewerFillPaletteData *data = (PalViewerFillPaletteData *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			data = (PalViewerFillPaletteData *) calloc(1, sizeof(PalViewerFillPaletteData));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			data->col1 = 0;
			data->col2 = 0;

			CreateStatic(hWnd, L"Type:", 10, 10, 50, 22);
			CreateStatic(hWnd, L"Color 1:", 10, 37, 50, 22);
			CreateStatic(hWnd, L"Color 2:", 10, 64, 50, 22);

			LPCWSTR typeStrs[] = { L"Single Color", L"Gradient" };
			data->hWndType = CreateCombobox(hWnd, typeStrs, sizeof(typeStrs) / sizeof(typeStrs[0]), 70, 10, 200, 100, 0);
			data->hWndChoose1 = CreateButton(hWnd, L"Choose", 70, 37, 100, 22, FALSE);
			data->hWndChoose2 = CreateButton(hWnd, L"Choose", 70, 64, 100, 22, FALSE);

			EnableWindow(data->hWndChoose2, FALSE);

			data->hWndOK = CreateButton(hWnd, L"OK", 70, 91, 100, 22, TRUE);

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 280, 123);
			break;
		}
		case NV_INITIALIZE:
		{
			data->nclrViewerData = (NCLRVIEWERDATA *) lParam;
			break;
		}
		case WM_COMMAND:
		{
			int idc = LOWORD(wParam);
			HWND hWndControl = (HWND) lParam;
			int cmd = HIWORD(wParam);
				
			if (hWndControl == data->hWndType && cmd == CBN_SELCHANGE) {
				int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
				EnableWindow(data->hWndChoose2, sel != 0);
				InvalidateRect(data->hWndChoose2, NULL, TRUE);
			} else if ((hWndControl == data->hWndChoose1 || hWndControl == data->hWndChoose2) && cmd == BN_CLICKED) {
				HWND hWndMain = data->nclrViewerData->editorMgr->hWnd;

				int idx = (hWndControl == data->hWndChoose1) ? 0 : 1;
				
				COLOR *result = idx == 0 ? &data->col1 : &data->col2;
				NpChooseColor15(hWndMain, hWnd, result);
			} else if ((hWndControl == data->hWndOK || idc == IDOK) && cmd == BN_CLICKED) {
				//OK button selected. get mode and colors
				int mode = SendMessage(data->hWndType, CB_GETCURSEL, 0, 0);
				COLOR32 col1 = ColorConvertFromDS(data->col1);
				COLOR32 col2 = ColorConvertFromDS(data->col2);

				NCLRVIEWERDATA *nclrViewerData = data->nclrViewerData;
				int size = PalViewerGetSelectionSize(nclrViewerData);
				if (mode == 0) {
					//single color
					for (int i = 0; i < nclrViewerData->nclr->nColors; i++) {
						if (PalViewerIndexInSelection(nclrViewerData, i)) {
							nclrViewerData->nclr->colors[i] = ColorConvertToDS(col1);
						}
					}
				} else if (mode == 1) {
					//gradient
					unsigned int j = 0;
					for (int i = 0; i < nclrViewerData->nclr->nColors; i++) {
						if (PalViewerIndexInSelection(nclrViewerData, i)) {
							//interpolate
							unsigned int r = ((((col1 >>  0) & 0xFF) * (size - 1 - j) + ((col2 >>  0) & 0xFF) * j) * 2 + size - 1) / (2 * (size - 1));
							unsigned int g = ((((col1 >>  8) & 0xFF) * (size - 1 - j) + ((col2 >>  8) & 0xFF) * j) * 2 + size - 1) / (2 * (size - 1));
							unsigned int b = ((((col1 >> 16) & 0xFF) * (size - 1 - j) + ((col2 >> 16) & 0xFF) * j) * 2 + size - 1) / (2 * (size - 1));

							nclrViewerData->nclr->colors[i] = ColorConvertToDS(r | (g << 8) | (b << 16));
							j++;
						}
					}
				}

				PalViewerUpdatePreview(nclrViewerData);
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_DESTROY:
		{
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}


static int PalViewerIsPaletteEmpty(NCLRVIEWERDATA *data, unsigned int n) {
	unsigned int nColsPalette = 1 << data->nclr->nBits;

	for (unsigned int j = 0; j < nColsPalette; j++) {
		unsigned int colI = (n * nColsPalette) + j;
		if (colI >= (unsigned int) data->nclr->nColors) break;

		COLOR c = data->nclr->colors[colI];
		if (c) {
			//nonzero color
			return 0;
		}
	}

	//empty
	return 1;
}

static uint16_t PalViewerGetPaletteEmptyBitmap(NCLRVIEWERDATA *data) {
	uint16_t bitmap = 0;

	for (unsigned int i = 0; i < 16; i++) {
		if (!PalViewerIsPaletteEmpty(data, i)) bitmap |= 1 << i;
	}

	return bitmap;
}

static void PalViewerSetCompressedPaletteSettings(NCLRVIEWERDATA *data, BOOL enabled, uint16_t newSelection) {
	unsigned int nColsPalette = 1 << data->nclr->nBits;

	//get old setting
	int wasEnabled = data->nclr->compressedPalette;

	uint16_t oldSelection = PalViewerGetPaletteEmptyBitmap(data);

	//if the compressed palette was and still is disabled, do nothing.
	if (!enabled && !wasEnabled) return;

	//if the compressed palette was enabled and is no longer, we will just disable the compresed palette flag.
	if (wasEnabled && !enabled) {
		data->nclr->compressedPalette = 0;
		return;
	}

	//otherwise, the compressed palette is going to be enabled. We'll allocate a new buffer and map it.
	COLOR *newbuf = (COLOR *) calloc(16 * nColsPalette, sizeof(COLOR));

	unsigned int iSrc = 0; // source palette index
	for (unsigned int i = 0; i < 16; i++) {
		//skip palettes not selected
		if (!(newSelection & (1 << i))) continue;

		//if the source index bit in the old selection is not set, increment.
		while (!(oldSelection & (1 << iSrc)) && iSrc < 16) iSrc++;
		if (iSrc >= 16) break; // out of palettes

		//copy to i from iSrc
		for (unsigned int j = 0; j < nColsPalette; j++) {
			if ((iSrc * nColsPalette + j) >= (unsigned int) data->nclr->nColors) break;
			newbuf[i * nColsPalette + j] = data->nclr->colors[iSrc * nColsPalette + j];
		}

		//increment source index
		iSrc++;
	}

	//update palette
	free(data->nclr->colors);
	data->nclr->colors = newbuf;
	data->nclr->nColors = 16 * nColsPalette;
	data->nclr->compressedPalette = 1;
}

static LRESULT CALLBACK EditCompressedPaletteWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case NV_INITIALIZE:
		{
			data = (NCLRVIEWERDATA *) lParam;
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			data->hWndEditCompressionCheckbox = CreateCheckbox(hWnd, L"Use Compressed Palette", 10, 10, 150, 22, data->nclr->compressedPalette);
			data->hWndEditCompressionList = CreateCheckedListView(hWnd, 10, 37, 200, 200);
			data->hWndEditCompressionOK = CreateButton(hWnd, L"OK", 110, 242, 100, 22, TRUE);

			uint16_t paletteEmptyBitmap = PalViewerGetPaletteEmptyBitmap(data);
			for (int i = 0; i < 16; i++) {
				WCHAR buf[16];
				wsprintfW(buf, L"Palette %d", i);
				AddCheckedListViewItem(data->hWndEditCompressionList, buf, i, (paletteEmptyBitmap >> i) & 1);
			}

			//if compressed palette is not used, then disable the palette list.
			if (!data->nclr->compressedPalette) {
				EnableWindow(data->hWndEditCompressionList, FALSE);
				UpdateWindow(data->hWndEditCompressionList);
			}

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 220, 274);

			break;
		}
		case WM_COMMAND:
		{
			HWND hWndCtl = (HWND) lParam;
			int notif = HIWORD(wParam);
			int idCtl = LOWORD(wParam);

			if (hWndCtl == data->hWndEditCompressionCheckbox && notif == BN_CLICKED) {

				//enabled state changed.
				int enabled = GetCheckboxChecked(hWndCtl);
				EnableWindow(data->hWndEditCompressionList, enabled);
				UpdateWindow(data->hWndEditCompressionList);

			} else if ((hWndCtl == data->hWndEditCompressionOK || idCtl == IDOK) && notif == BN_CLICKED) {

				//get new enable setting
				int enabled = GetCheckboxChecked(data->hWndEditCompressionCheckbox);

				//create a bitmap of palette indices
				uint16_t newSelection = 0;
				for (int i = 0; i < 16; i++) {
					if (CheckedListViewIsChecked(data->hWndEditCompressionList, i)) newSelection |= 1 << i;
				}

				//update
				PalViewerSetCompressedPaletteSettings(data, enabled, newSelection);
				PalViewerUpdatePreview(data);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if (idCtl == IDCANCEL && notif == BN_CLICKED) {
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}

			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void PalViewerRegisterPaletteGenerationClass(void) {
	RegisterGenericClass(L"PaletteGeneratorClass", PaletteGeneratorDialogProc, sizeof(LPVOID));
}

static void PalViewerRegisterPaletteFillClass(void) {
	RegisterGenericClass(L"GeneratePaletteClass", GeneratePaletteDialogProc, sizeof(LPVOID));
}

static void PalViewerRegisterEditCompressedPaletteClass(void) {
	RegisterGenericClass(L"EditCompressedClass", EditCompressedPaletteWndProc, sizeof(LPVOID));
}

void RegisterNclrViewerClass(void) {
	int features = 0;
	EDITOR_CLASS *cls = EditorRegister(L"NclrViewerClass", PalViewerWndProc, L"Palette Editor", sizeof(NCLRVIEWERDATA), features);
	EditorAddFilter(cls, NCLR_TYPE_NCLR, L"nclr", L"NCLR Files (*.nclr)\0*.nclr\0");
	EditorAddFilter(cls, NCLR_TYPE_BIN, L"bin", L"Palette Files (*.bin, *ncl.bin, *icl.bin, *.nbfp, *.icl, *.acl)\0*.bin;*.nbfp;*.icl;*.acl;\0");
	EditorAddFilter(cls, NCLR_TYPE_HUDSON, L"bin", L"Palette Files (*.bin, *ncl.bin, *icl.bin, *.nbfp, *.icl, *.acl)\0*.bin;*.nbfp;*.icl;*.acl;\0");
	EditorAddFilter(cls, NCLR_TYPE_COMBO, L"bin", L"Combination Files (*.dat, *.bnr, *.bin)\0*.dat;*.bnr;*.bin\0");
	EditorAddFilter(cls, NCLR_TYPE_NC, L"ncl", L"NCL Files (*.ncl)\0*.ncl\0");
	EditorAddFilter(cls, NCLR_TYPE_ISTUDIO, L"5pl", L"5PL Files (*.5pl)\0*.5pl\0");
	EditorAddFilter(cls, NCLR_TYPE_ISTUDIOC, L"5pc", L"5PC Files (*.5pc)\0*.5pc\0");
	EditorAddFilter(cls, NCLR_TYPE_SETOSA, L"splt", L"SPLT Files (*.splt)\0*.splt\0");
	PalViewerRegisterPaletteGenerationClass();
	PalViewerRegisterPaletteFillClass();
	PalViewerRegisterEditCompressedPaletteClass();
}

static HWND CreateNclrViewerInternal(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path, NCLR *nclr) {
	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4;
	}

	HWND hWnd = EditorCreate(L"NclrViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) path, (LPARAM) nclr);
	return hWnd;
}

HWND CreateNclrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
	if (PalReadFile(nclr, path)) {
		free(nclr);
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	return CreateNclrViewerInternal(x, y, width, height, hWndParent, path, nclr);
}

HWND CreateNclrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCLR *nclr) {
	return CreateNclrViewerInternal(x, y, width, height, hWndParent, NULL, nclr);
}
