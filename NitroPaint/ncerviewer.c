#include <Windows.h>
#include <CommCtrl.h>
#include <math.h>

#include "editor.h"
#include "ncerviewer.h"
#include "nitropaint.h"
#include "ncgr.h"
#include "nclr.h"
#include "resource.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "palette.h"
#include "gdip.h"
#include "preview.h"

#include "cellgen.h"

#define PREVIEW_ICON_WIDTH      64 // width of cell preview icon
#define PREVIEW_ICON_HEIGHT     64 // height of cell preview icon
#define PREVIEW_ICON_PADDING_V  10 // vertical padding of cell preview

#define ID_TIMER_GFX_UPDATE      1 // internal timer used to update graphics
#define GFX_UPDATE_TIMER       500 // deferment of updating from foreign graphics data (lessen CPU load)


#define CV_HIT_NOWHERE           0 // nowhere
#define CV_HIT_BACKGROUND        1 // background
#define CV_HIT_SELECTION         2 // selection region
#define CV_HIT_OBJ               3 // here and above for OBJ index


extern HICON g_appIcon;

static int sObjClipboardFormat = 0;


// ----- menu defines

static const unsigned short sMenuIdPalettes[] = {
		ID_OBJPALETTE_0,  ID_OBJPALETTE_1,  ID_OBJPALETTE_2,  ID_OBJPALETTE_3,
		ID_OBJPALETTE_4,  ID_OBJPALETTE_5,  ID_OBJPALETTE_6,  ID_OBJPALETTE_7,
		ID_OBJPALETTE_8,  ID_OBJPALETTE_9,  ID_OBJPALETTE_10, ID_OBJPALETTE_11,
		ID_OBJPALETTE_12, ID_OBJPALETTE_13, ID_OBJPALETTE_14, ID_OBJPALETTE_15,
};
static const unsigned short sMenuIdPrios[] = {
	ID_OBJPRIORITY_0, ID_OBJPRIORITY_1, ID_OBJPRIORITY_2, ID_OBJPRIORITY_3
};
static const unsigned short sMenuIdTypes[] = {
	ID_OBJTYPE_NORMAL, ID_OBJTYPE_TRANSLUCENT, ID_OBJTYPE_WINDOW, ID_OBJTYPE_BITMAP
};
static const unsigned short sMenuIdSizes[] = {
	ID_OBJSIZE_8X8,  ID_OBJSIZE_16X16, ID_OBJSIZE_32X32, ID_OBJSIZE_64X64,
	ID_OBJSIZE_16X8, ID_OBJSIZE_32X8,  ID_OBJSIZE_32X16, ID_OBJSIZE_64X32,
	ID_OBJSIZE_8X16, ID_OBJSIZE_8X32,  ID_OBJSIZE_16X32, ID_OBJSIZE_32X64
};

static void CellViewerRenderCell(COLOR32 *px, int *covbuf, NCER_CELL *cell, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, int xOffs, int yOffs, float a, float b, float c, float d);
static COLOR32 *CellViewerCropRenderedCell(COLOR32 *px, int width, int height, int *pMinX, int *pMinY, int *outWidth, int *outHeight);


// ----- basic editor routines

#define SEXT8(n)   (((n)<0x080)?(n):((n)-0x100))
#define SEXT9(n)   (((n)<0x100)?(n):((n)-0x200))

static NCLR *CellViewerGetAssociatedPalette(NCERVIEWERDATA *data) {
	HWND hWndMain = getMainWindow(data->hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
	if (nitroPaintStruct->hWndNclrViewer == NULL) return NULL;

	return (NCLR *) EditorGetObject(nitroPaintStruct->hWndNclrViewer);
}

static NCGR *CellViewerGetAssociatedCharacter(NCERVIEWERDATA *data) {
	HWND hWndMain = getMainWindow(data->hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
	if (nitroPaintStruct->hWndNcgrViewer == NULL) return NULL;

	return (NCGR *) EditorGetObject(nitroPaintStruct->hWndNcgrViewer);
}

static unsigned char *CellViewerMapGraphicsUsage(NCERVIEWERDATA *data, int excludeNonClearCharacters, unsigned int *pMapSize) {
	//plot a map of VRAM usage in character units.
	NCGR *ncgr = CellViewerGetAssociatedCharacter(data);
	if (ncgr == NULL) {
		//cannot construct map.
		*pMapSize = 0;
		return NULL;
	}

	unsigned int chrSize = (ncgr->nBits == 8) ? 64 : 32;
	unsigned int mapSize = ncgr->nTiles;
	unsigned char *map = (unsigned char *) calloc(1, mapSize);

	//check for VRAM transfer data
	CHAR_VRAM_TRANSFER *vramTransfer = data->ncer.vramTransfer;
	if (vramTransfer != NULL) {
		//VRAM transfer data present: carve blocks out of the available space
		for (int i = 0; i < data->ncer.nCells; i++) {
			CHAR_VRAM_TRANSFER *xfer = &vramTransfer[i];

			//get overlapping character index region
			unsigned int chrStart = xfer->srcAddr / chrSize; // round down
			unsigned int chrSize = (xfer->srcAddr + xfer->size + chrSize - 1) / chrSize - chrStart; // round up

			//fill use map
			unsigned int reqSize = chrStart + chrSize;
			if (reqSize > mapSize) {
				//expand map
				map = (unsigned char *) realloc(map, reqSize);
				memset(map + mapSize, 0, reqSize - mapSize);
				mapSize = reqSize;
			}
			memset(map + chrStart, 1, chrSize);
		}
	} else {
		//no VRAM transfer data present: iterate each cell and its constituent OBJ
		for (int i = 0; i < data->ncer.nCells; i++) {
			NCER_CELL *cell = &data->ncer.cells[i];

			for (int j = 0; j < cell->nAttribs; j++) {
				NCER_CELL_INFO info;
				CellDecodeOamAttributes(&info, cell, j);

				//compute VRAM destination address
				unsigned int sizeChars = (info.width * info.height) / 64;
				unsigned int vramAddr = NCGR_CHNAME(info.characterName, data->ncer.mappingMode, info.characterBits);

				//fill use map
				unsigned int reqSize = vramAddr + sizeChars;
				if (reqSize > mapSize) {
					//expand map
					map = (unsigned char *) realloc(map, reqSize);
					memset(map + mapSize, 0, reqSize - mapSize);
					mapSize = reqSize;
				}
				memset(map + vramAddr, 1, sizeChars);
			}
		}
	}

	//last: exclude non-clear character ranges
	if (excludeNonClearCharacters) {
		unsigned char zero[64] = { 0 };
		for (int i = 0; i < ncgr->nTiles; i++) {
			if (memcmp(ncgr->tiles[i], zero, sizeof(zero)) != 0) {
				map[i] = 1;
			}
		}
	}

	*pMapSize = mapSize;
	return map;
}

static unsigned int CellViewerAllocCharSpace(NCERVIEWERDATA *data, int ignoreFilledCharacters, unsigned int sizeChars) {
	NCGR *ncgr = CellViewerGetAssociatedCharacter(data);
	if (ncgr == NULL) {
		return UINT_MAX;
	}

	//allocate space in character graphics. Compute filled map.
	unsigned int mapSize;
	unsigned char *map = CellViewerMapGraphicsUsage(data, ignoreFilledCharacters, &mapSize);
	
	unsigned int granularity = 1;
	if (data->ncer.mappingMode == GX_OBJVRAMMODE_CHAR_2D) granularity = 1;
	else if (data->ncer.mappingMode == GX_OBJVRAMMODE_CHAR_1D_32K) granularity = 1;
	else if (data->ncer.mappingMode == GX_OBJVRAMMODE_CHAR_1D_64K) granularity = 2;
	else if (data->ncer.mappingMode == GX_OBJVRAMMODE_CHAR_1D_128K) granularity = 4;
	else if (data->ncer.mappingMode == GX_OBJVRAMMODE_CHAR_1D_256K) granularity = 8;

	//adjust for 8-bit graphics
	if (ncgr->nBits == 8) granularity /= 2;
	if (granularity == 0) granularity = 1;

	//find sizeChars continuous characters in the map equal to zero, accounting for alignment requirements by the
	//current cell bank's mapping mode.
	unsigned int foundAddr = UINT_MAX;
	for (unsigned int i = 0; i < mapSize; i += granularity) {
		//check upper bound
		if ((i + sizeChars) <= mapSize) {
			int hasSpace = 1;
			for (unsigned int j = 0; j < sizeChars; j++) {
				if (map[i + j]) {
					hasSpace = 0;
					break;
				}
			}

			if (hasSpace) {
				//found space.
				foundAddr = i;
				break;
			}
		}
	}

	free(map);
	return foundAddr;
}

static unsigned int CellViewerGetFirstUnusedCharacter(NCERVIEWERDATA *data, int excludeCell) {
	//find the first unused character at the end of all used ones, rounded up to the mapping mode's granularity.
	unsigned int end = 0;

	NCGR *ncgr = CellViewerGetAssociatedCharacter(data);
	unsigned int chrSize = 32; //fallback
	if (ncgr != NULL) {
		chrSize = (ncgr->nBits == 8) ? 64 : 32;
	}

	for (int i = 0; i < data->ncer.nCells; i++) {
		NCER_CELL *cell = &data->ncer.cells[i];
		if (i == excludeCell) continue;
		
		//process VRAM transfer animation
		if (data->ncer.vramTransfer != NULL) {
			CHAR_VRAM_TRANSFER *xfer = &data->ncer.vramTransfer[i];
			unsigned int vramAddr = xfer->srcAddr / chrSize;
			unsigned int sizeChars = xfer->size / chrSize;
			unsigned int thisEnd = vramAddr + sizeChars;
			if (thisEnd > end) end = thisEnd;

			//TODO: other formats with more complex VRAM transfer formats?
		} else {
			//process each OBJ in cell to find graphics usage
			for (int j = 0; j < cell->nAttribs; j++) {
				NCER_CELL_INFO info;
				CellDecodeOamAttributes(&info, cell, j);

				//compute VRAM destination address
				unsigned int sizeChars = (info.width * info.height) / 64;
				unsigned int vramAddr = NCGR_CHNAME(info.characterName, data->ncer.mappingMode, info.characterBits);

				unsigned int thisEnd = vramAddr + sizeChars;
				if (thisEnd > end) end = thisEnd;
			}
		}
	}

	//round up?
	return end;
}

static NCER_CELL *CellViewerGetCurrentCell(NCERVIEWERDATA *data) {
	if (data->cell < 0 || data->cell >= data->ncer.nCells) return NULL;
	return &data->ncer.cells[data->cell];
}

static int CellViewerIsDragging(NCERVIEWERDATA *data) {
	return data->dragStartX != -1 && data->dragStartY != -1;
}

static int CellViewerGetDragBounds(NCERVIEWERDATA *data, int *pMinX, int *pMinY, int *pWidth, int *pHeight) {
	if (!CellViewerIsDragging(data)) return 0;

	int x1 = data->dragStartX, y1 = data->dragStartY;
	int x2 = data->dragEndX, y2 = data->dragEndY;

	int xMin = min(x1, x2);
	int width = max(x1, x2) + 1 - xMin;
	int yMin = min(y1, y2);
	int height = max(y1, y2) + 1 - yMin;
	*pMinX = xMin;
	*pMinY = yMin;
	*pWidth = width;
	*pHeight = height;

	return 1;
}

static int CellViewerIsRectInSelection(NCERVIEWERDATA *data, int x, int y, int w, int h) {
	int selX, selY, selW, selH;
	int sel = CellViewerGetDragBounds(data, &selX, &selY, &selW, &selH);
	if (!sel) return 0;

	//bound check
	if ((x + w) < selX) return 0;
	if (x >= (selX + selW)) return 0;
	if ((y + h) < selY) return 0;
	if (y >= (selY + selH)) return 0;
	return 1;
}

static int *CellViewerGetSelectedOamObjects(NCERVIEWERDATA *data, int *pnSel) {
	//no cell viewing, no OBJ can be in selection
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) {
		*pnSel = 0;
		return NULL;
	}

	//check selection
	if (data->nSelectedOBJ > 0) {
		int *cpy = (int *) calloc(data->nSelectedOBJ, sizeof(int));
		memcpy(cpy, data->selectedOBJ, data->nSelectedOBJ * sizeof(int));
		*pnSel = data->nSelectedOBJ;
		return cpy;
	}

	int nOBJ = 0;
	int *indices = (int *) calloc(cell->nAttribs, sizeof(int));

	//test OBJ
	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		//get OBJ bounds
		int objX = SEXT9(info.x) + 256, objY = SEXT8(info.y) + 128;
		int objW = info.width << info.doubleSize, objH = info.height << info.doubleSize;

		if (CellViewerIsRectInSelection(data, objX, objY, objW, objH)) {
			indices[nOBJ++] = i;
		}
	}

	*pnSel = nOBJ;
	indices = realloc(indices, nOBJ * sizeof(int));
	return indices;
}


static void CellViewerPreviewGetScroll(NCERVIEWERDATA *data, int *scrollX, int *scrollY) {
	//get scroll info
	SCROLLINFO scrollH = { 0 }, scrollV = { 0 };
	scrollH.cbSize = scrollV.cbSize = sizeof(scrollH);
	scrollH.fMask = scrollV.fMask = SIF_ALL;
	GetScrollInfo(data->hWndViewer, SB_HORZ, &scrollH);
	GetScrollInfo(data->hWndViewer, SB_VERT, &scrollV);

	*scrollX = scrollH.nPos;
	*scrollY = scrollV.nPos;
}

static void CellViewerPreviewCenter(NCERVIEWERDATA *data) {
	//get client
	RECT rcClient;
	GetClientRect(data->hWndViewer, &rcClient);

	//get view size
	int viewWidth = 512 * data->scale;
	int viewHeight = 256 * data->scale;
	
	//check dimensions of view
	if (rcClient.right < viewWidth) {
		//set scroll H
		SCROLLINFO scroll = { 0 };
		scroll.cbSize = sizeof(scroll);
		scroll.fMask = SIF_POS;
		scroll.nPos = (viewWidth - rcClient.right) / 2;
		SetScrollInfo(data->hWndViewer, SB_HORZ, &scroll, TRUE);
	}
	if (rcClient.bottom < viewHeight) {
		//set scroll V
		SCROLLINFO scroll = { 0 };
		scroll.cbSize = sizeof(scroll);
		scroll.fMask = SIF_POS;
		scroll.nPos = (viewHeight - rcClient.bottom) / 2;
		SetScrollInfo(data->hWndViewer, SB_VERT, &scroll, TRUE);
	}
}

static int CellViewerGetOamObjFromPoint(NCER_CELL *cell, int x, int y) {
	int oam = -1;
	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		//take into account double size!
		int width = info.width << info.doubleSize;
		int height = info.height << info.doubleSize;

		//this is ugly, but it takes wrapping into account.
		if ((x >= info.x && y >= info.y && x < info.x + width && y < info.y + height) ||
			(x + 512 >= info.x && y >= info.y && x + 512 < info.x + width && y < info.y + height) ||
			(x >= info.x && y + 256 >= info.y && x < info.x + width && y + 256 < info.y + height) ||
			(x + 512 >= info.x && y + 256 >= info.y && x + 512 < info.x + width && y + 256 < info.y + height)) {
			oam = i;
			break;
		}
	}
	return oam;
}

static int CellViewerHasSelection(NCERVIEWERDATA *data) {
	return data->nSelectedOBJ > 0;
}

static int CellViewerHitTest(NCERVIEWERDATA *data, int x, int y) {
	int scrollX, scrollY;
	CellViewerPreviewGetScroll(data, &scrollX, &scrollY);

	//get view size
	int viewWidth = 512 * data->scale - scrollX;
	int viewHeight = 256 * data->scale - scrollY;
	if (x < 0 || x >= viewWidth || y < 0 || y >= viewHeight) return CV_HIT_NOWHERE;

	//test hit
	int hit = CV_HIT_BACKGROUND;
	if (data->cell >= 0 && data->cell < data->ncer.nCells) {
		NCER_CELL *cell = &data->ncer.cells[data->cell];
		int objHit = CellViewerGetOamObjFromPoint(cell, (x + scrollX) / data->scale - 256, (y + scrollY) / data->scale - 128);
		if (objHit != -1) hit = CV_HIT_OBJ + objHit;

		//do we have a selection? check this OBJ against selection
		if (objHit != -1 && CellViewerHasSelection(data)) {
			for (int i = 0; i < data->nSelectedOBJ; i++) {
				if (data->selectedOBJ[i] == objHit) {
					hit = CV_HIT_SELECTION;
					break;
				}
			}
		}
	}
	return hit;
}

static void CellViewerDeselect(NCERVIEWERDATA *data) {
	if (data->selectedOBJ != NULL) free(data->selectedOBJ);
	data->selectedOBJ = NULL;
	data->nSelectedOBJ = 0;
}

static void CellViewerSelectAll(NCERVIEWERDATA *data) {
	CellViewerDeselect(data);

	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	int *sel = (int *) calloc(cell->nAttribs, sizeof(int));
	for (int i = 0; i < cell->nAttribs; i++) sel[i] = i;

	data->selectedOBJ = sel;
	data->nSelectedOBJ = cell->nAttribs;
}

static void CellViewerCommitSelection(NCERVIEWERDATA *data) {
	CellViewerDeselect(data);

	int nSel;
	int *sel = CellViewerGetSelectedOamObjects(data, &nSel);

	data->selectedOBJ = sel;
	data->nSelectedOBJ = nSel;
}

static int CellViewerIsMakingSelection(NCERVIEWERDATA *data) {
	return data->mouseDown && data->mouseDownHit == CV_HIT_BACKGROUND;
}

static int CellViewerIsMovingOBJ(NCERVIEWERDATA *data) {
	return data->mouseDown && data->mouseDownHit >= CV_HIT_OBJ;
}

static void CellViewerSelectSingleOBJ(NCERVIEWERDATA *data, int i) {
	if (CellViewerHasSelection(data)) CellViewerDeselect(data);

	data->nSelectedOBJ = 1;
	data->selectedOBJ = (int *) calloc(1, sizeof(int));
	data->selectedOBJ[0] = i;
}

static int CellViewerSelectionComparator(const void *p1, const void *p2) {
	return (*(const int *) p1) - (*(const int *) p2);
}

static void CellViewerAddObjToSelection(NCERVIEWERDATA *data, int iObj) {
	//test for presence in selection
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		if (data->selectedOBJ[i] == iObj) {
			//OBJ in selection, remove it.
			memmove(data->selectedOBJ + i, data->selectedOBJ + i + 1, (data->nSelectedOBJ - i - 1) * sizeof(int));
			data->nSelectedOBJ--;
			data->selectedOBJ = (int *) realloc(data->selectedOBJ, data->nSelectedOBJ * sizeof(int));
			return;
		}
	}

	//add to selection
	data->nSelectedOBJ++;
	data->selectedOBJ = realloc(data->selectedOBJ, data->nSelectedOBJ * sizeof(int));
	data->selectedOBJ[data->nSelectedOBJ - 1] = iObj;
	qsort(data->selectedOBJ, data->nSelectedOBJ, sizeof(int), CellViewerSelectionComparator);
}

static void CellViewerSelectRange(NCERVIEWERDATA *data, int start, int n) {
	if (CellViewerHasSelection(data)) CellViewerDeselect(data);

	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;
	if (start >= cell->nAttribs) return;
	if ((start + n) > cell->nAttribs) n = cell->nAttribs - start;

	int *sel = (int *) calloc(n, sizeof(int));
	for (int i = 0; i < n; i++) sel[i] = start + i;
	data->selectedOBJ = sel;
	data->nSelectedOBJ = n;
}

static void CellViewerMoveSelection(NCERVIEWERDATA *data, int dx, int dy) {
	if (data->nSelectedOBJ == 0) return;

	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	for (int i = 0; i < data->nSelectedOBJ; i++) {
		int ii = data->selectedOBJ[i];
		uint16_t attr0 = cell->attr[ii * 3 + 0];
		uint16_t attr1 = cell->attr[ii * 3 + 1];

		attr1 = (attr1 & 0xFE00) | (((attr1 & 0x01FF) + dx) & 0x01FF);
		attr0 = (attr0 & 0xFF00) | (((attr0 & 0x00FF) + dy) & 0x00FF);
		cell->attr[ii * 3 + 0] = attr0;
		cell->attr[ii * 3 + 1] = attr1;
	}
}

static void CellViewerDeleteSelection(NCERVIEWERDATA *data) {
	if (!CellViewerHasSelection(data)) return;

	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	uint16_t *attr = cell->attr;
	while (data->nSelectedOBJ > 0) {
		int i = data->selectedOBJ[data->nSelectedOBJ - 1];

		//remove this OBJ
		memmove(attr + 3 * i, attr + 3 * (i + 1), (cell->nAttribs - i - 1) * 6);
		cell->nAttribs--;

		data->nSelectedOBJ--;
	}
	cell->attr = realloc(cell->attr, cell->nAttribs * 6);

	if (data->selectedOBJ != NULL) free(data->selectedOBJ);
	data->selectedOBJ = NULL;
	data->nSelectedOBJ = 0;
}

static float CellViewerComputeDistanceToCenter(int cx, int cy, int x, int y) {
	int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
	return (float) sqrt((float) d2);
}

static void CellViewerUpdateBounds(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	int xMin = 0, xMax = 0, yMin = 0, yMax = 0;

	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		int objX = SEXT9(info.x), objY = SEXT8(info.y);
		int objW = info.width << info.doubleSize, objH = info.height << info.doubleSize;
		if (i == 0 || objX < xMin) xMin = objX;
		if (i == 0 || objY < yMin) yMin = objY;
		if (i == 0 || (objX + objW) > xMax) xMax = objX + objW;
		if (i == 0 || (objY + objH) > yMax) yMax = objY + objH;
	}

	cell->minX = xMin;
	cell->minY = yMin;
	cell->maxX = xMax;
	cell->maxY = yMax;

	int centerX = (xMin + xMax) / 2;
	int centerY = (yMin + yMax) / 2;

	//find OBJ with furthest extent point
	float dMax = 0.0f;
	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		int objX = SEXT9(info.x), objY = SEXT8(info.y);
		int objW = info.width << info.doubleSize, objH = info.height << info.doubleSize;

		float d1 = CellViewerComputeDistanceToCenter(centerX, centerY, objX,        objY       );
		float d2 = CellViewerComputeDistanceToCenter(centerX, centerY, objX + objW, objY       );
		float d3 = CellViewerComputeDistanceToCenter(centerX, centerY, objX,        objY + objH);
		float d4 = CellViewerComputeDistanceToCenter(centerX, centerY, objX + objW, objY + objH);

		if (d1 > dMax) dMax = d1;
		if (d2 > dMax) dMax = d2;
		if (d3 > dMax) dMax = d3;
		if (d4 > dMax) dMax = d4;
	}

	int dInt = (int) ceil(dMax);
	dInt = (dInt + 3) >> 2;
	cell->cellAttr = (cell->cellAttr & ~0x3F) | (dInt & 0x3F);
}


// ----- copy/paste code

static int CellviewerGetObjClipboardFormat(void) {
	if (sObjClipboardFormat) return sObjClipboardFormat;

	sObjClipboardFormat = RegisterClipboardFormat(L"NP_OBJ");
	return sObjClipboardFormat;
}

static uint16_t *CellViewerGetSelectedOamAttributes(NCERVIEWERDATA *data, int *pnOBJ) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) {
		*pnOBJ = 0;
		return NULL;
	}

	uint16_t *attrs = (uint16_t *) calloc(data->nSelectedOBJ, 6);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		int ii = data->selectedOBJ[i];
		memcpy(attrs + i * 3, cell->attr + ii * 3, 6);
	}

	*pnOBJ = data->nSelectedOBJ;
	return attrs;
}

static void CellViewerCopyDIB(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	int nSel;
	uint16_t *selAttr = CellViewerGetSelectedOamAttributes(data, &nSel);

	COLOR32 *buf = (COLOR32 *) calloc(512 * 256, sizeof(COLOR32));

	//get render params
	NCGR *ncgr = CellViewerGetAssociatedCharacter(data);
	NCLR *nclr = CellViewerGetAssociatedPalette(data);
	CHAR_VRAM_TRANSFER *vramTransfer = NULL;
	if (data->ncer.vramTransfer != NULL) {
		vramTransfer = data->ncer.vramTransfer + data->cell;
	}
	
	//construct temporary cell
	NCER_CELL *tmpCell = (NCER_CELL *) calloc(1, sizeof(NCER_CELL));
	tmpCell->nAttribs = nSel;
	tmpCell->attr = selAttr;

	CellViewerRenderCell(buf, NULL, tmpCell, data->ncer.mappingMode, ncgr, nclr, vramTransfer, 256, 128, 1.0f, 0.0f, 0.0f, 1.0f);
	free(tmpCell);
	free(selAttr);

	//crop to view
	int minX, minY, width, height;
	COLOR32 *crop = CellViewerCropRenderedCell(buf, 512, 256, &minX, &minY, &width, &height);
	free(buf);

	if (width != 0 && height != 0) {
		copyBitmap(crop, width, height);
	}

	free(crop);
}

static void CellViewerCopy(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	//copy selected OBJ
	OpenClipboard(data->hWnd);
	EmptyClipboard();

	int nSel = 0;
	int *sel = CellViewerGetSelectedOamObjects(data, &nSel);
	
	NP_OBJ *cpy = (NP_OBJ *) calloc(sizeof(NP_OBJ) + nSel * 6, 1);
	for (int i = 0; i < nSel; i++) {
		int ii = sel[i];
		memcpy(cpy->attr + i * 3, cell->attr + ii * 3, 6);
	}
	cpy->nOBJ = nSel;

	//get bounding box size of selection
	int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
	for (int i = 0; i < nSel; i++) {
		int ii = sel[i];
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, ii);

		int objX = SEXT9(info.x), objY = SEXT8(info.y);
		int objW = info.width << info.doubleSize, objH = info.height << info.doubleSize;
		if (i == 0 || objX < xMin) xMin = objX;
		if (i == 0 || objY < yMin) yMin = objY;
		if (i == 0 || (objX + objW) > xMax) xMax = objX + objW;
		if (i == 0 || (objY + objH) > yMax) yMax = objY + objH;
	}
	cpy->xMin = xMin;
	cpy->yMin = yMin;
	cpy->width = xMax - xMin;
	cpy->height = yMax - yMin;

	CellViewerCopyObjData(cpy);
	free(cpy);

	//TODO: copy image
	CellViewerCopyDIB(data);

	CloseClipboard();
}

static void CellViewerPaste(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	//paste selected OBJ
	OpenClipboard(data->hWnd);

	NP_OBJ *attr = CellViewerGetCopiedObjData();
	
	if (attr != NULL) {
		//mouse in-bounds?
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(data->hWndViewer, &pt);

		RECT rcClient;
		GetClientRect(data->hWndViewer, &rcClient);

		int scrollX, scrollY;
		CellViewerPreviewGetScroll(data, &scrollX, &scrollY);

		//get paste center
		int pasteX = (pt.x + scrollX) / data->scale;
		int pasteY = (pt.y + scrollY) / data->scale;
		if (pasteX < 0 || pasteX >= 512 || pasteY < 0 || pasteY >= 256 || pt.x < 0 || pt.x >= rcClient.right || pt.y < 0 || pt.y >= rcClient.bottom) {
			//fall back to pasting where selection was copied
			pasteX = attr->xMin + attr->width / 2 + 256;
			pasteY = attr->yMin + attr->height / 2 + 128;
		}
		pasteX -= 256;
		pasteY -= 128;

		if (pasteX < -256 || pasteX >= 256 || pasteY < -128 || pasteY >= 128) {
			//fall back to center of canvas
			pasteX = 0;
			pasteY = 0;
		}

		//paste to beginning of OBJ list (brings to front)
		cell->nAttribs += attr->nOBJ;
		cell->attr = (uint16_t *) realloc(cell->attr, cell->nAttribs * 6);
		memmove(cell->attr + 3 * attr->nOBJ, cell->attr, (cell->nAttribs - attr->nOBJ) * 6);
		memcpy(cell->attr, attr->attr, 6 * attr->nOBJ);

		//select
		CellViewerDeselect(data);
		int *sel = (int *) calloc(attr->nOBJ, sizeof(int));
		for (int i = 0; i < attr->nOBJ; i++) sel[i] = i;
		data->selectedOBJ = sel;
		data->nSelectedOBJ = attr->nOBJ;

		//offset selection
		int selDx = pasteX - (attr->xMin + attr->width / 2), selDy = pasteY - (attr->yMin + attr->height / 2);
		CellViewerMoveSelection(data, selDx, selDy);
		if (data->autoCalcBounds) CellViewerUpdateBounds(data);

		free(attr);
	}
	CloseClipboard();
}


// ----- rendering helper routines


static void CellViewerRenderObj(COLOR32 *out, NCER_CELL_INFO *info, NCGR *ncgr, NCLR *nclr, int mapping, CHAR_VRAM_TRANSFER *vramTransfer) {
	int tilesX = info->width / 8;
	int tilesY = info->height / 8;

	if (ncgr == NULL) {
		//null NCGR, render opaque coverage by OBJ
		COLOR32 fill = 0xFF000000;
		if (nclr != NULL && nclr->nColors >= 1) {
			fill = 0xFF000000 | ColorConvertFromDS(nclr->colors[0]);
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

			ChrRenderCharacterTransfer(ncgr, nclr, index, vramTransfer, block, info->palette, TRUE);
			for (int i = 0; i < 8; i++) {
				memcpy(out + bitsOffset + tilesX * 8 * i, block + i * 8, 32);
			}
		}
	}
}

static void CellViewerRenderCell(COLOR32 *px, int *covbuf, NCER_CELL *cell, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, int xOffs, int yOffs, float a, float b, float c, float d) {
	COLOR32 *block = (COLOR32 *) calloc(64 * 64, sizeof(COLOR32));
	for (int i = cell->nAttribs - 1; i >= 0; i--) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		//if OBJ is marked disabled, skip rendering
		if (info.disable) continue;

		CellViewerRenderObj(block, &info, ncgr, nclr, mapping, vramTransfer);

		//HV flip? Only if not affine!
		if (!info.rotateScale) {
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

		int x = info.x;
		int y = info.y;

		//adjust for double size
		if (info.doubleSize) {
			x += info.width / 2;
			y += info.height / 2;
		}

		//copy data
		if (!info.rotateScale) {
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
			int realWidth = info.width << info.doubleSize;
			int realHeight = info.height << info.doubleSize;
			int cx = realWidth / 2;
			int cy = realHeight / 2;
			int realX = x - (realWidth - info.width) / 2;
			int realY = y - (realHeight - info.height) / 2;
			for (int j = 0; j < realHeight; j++) {
				int destY = (realY + j + yOffs) & 0xFF;
				for (int k = 0; k < realWidth; k++) {
					int destX = (realX + k + xOffs) & 0x1FF;

					int srcX = (int) ((k - cx) * a + (j - cy) * b) + cx;
					int srcY = (int) ((k - cx) * c + (j - cy) * d) + cy;

					if (info.doubleSize) {
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

static void CellViewerRenderCellByIndex(COLOR32 *buf, int *covbuf, NCER *ncer, NCGR *ncgr, NCLR *nclr, int cellno) {
	NCER_CELL *cell = ncer->cells + cellno;

	CHAR_VRAM_TRANSFER *vramTransfer = NULL;
	if (ncer->vramTransfer != NULL) vramTransfer = ncer->vramTransfer + cellno;

	CellViewerRenderCell(buf, covbuf, cell, ncer->mappingMode, ncgr, nclr, vramTransfer, 256, 128, 1.0f, 0.0f, 0.0f, 1.0f);
}

static void CellViewerUpdateCellRender(NCERVIEWERDATA *data) {
	NCLR *nclr = CellViewerGetAssociatedPalette(data);
	NCGR *ncgr = CellViewerGetAssociatedCharacter(data);

	memset(data->frameBuffer, 0, sizeof(data->frameBuffer));
	memset(data->covBuffer, 0, sizeof(data->covBuffer));
	if (data->cell != -1) {
		CellViewerRenderCellByIndex(data->frameBuffer, data->covBuffer, &data->ncer, ncgr, nclr, data->cell);
	}
}

static int CellViewerRowHasOpaque(COLOR32 *scan, int width) {
	for (int i = 0; i < width; i++) {
		COLOR32 c = scan[i];
		unsigned int a = c >> 24;
		if (a) return 1;
	}
	return 0;
}

static int CellViewerColHasOpaque(COLOR32 *px, int width, int height, int col) {
	for (int y = 0; y < height; y++) {
		COLOR32 c = px[col + y * width];
		unsigned int a = (c >> 24);
		if (a) return 1;
	}
	return 0;
}

static COLOR32 *CellViewerCropRenderedCell(COLOR32 *px, int width, int height, int *pMinX, int *pMinY, int *outWidth, int *outHeight) {
	//scan rows for pixel values
	int startY = height;
	for (int y = 0; y < height; y++) {
		if (CellViewerRowHasOpaque(px + y * width, width)) {
			startY = y;
			break;
		}
	}

	//check whole image transparent
	if (startY == height) {
		*pMinX = *pMinY = 0;
		*outWidth = *outHeight = 0;
		return NULL;
	}

	//scan horizontally
	int startX = 0;
	for (int x = 0; x < width; x++) {
		if (CellViewerColHasOpaque(px, width, height, x)) {
			startX = x;
			break;
		}
	}

	//scan for width and height
	int endX = startX + 1, endY = startY + 1;
	for (int y = startY + 1; y < height; y++) {
		if (CellViewerRowHasOpaque(px + y * width, width)) {
			endY = y + 1;
		}
	}
	for (int x = startX + 1; x < width; x++) {
		if (CellViewerColHasOpaque(px, width, height, x)) {
			endX = x + 1;
		}
	}

	//get crop
	COLOR32 *out = ImgCrop(px, width, height, startX, startY, endX - startX, endY - startY);
	*pMinX = startX;
	*pMinY = startY;
	*outWidth = endX - startX;
	*outHeight = endY - startY;
	return out;
}

static HBITMAP CellViewerRenderImageListBitmap(NCER *ncer, int cellno, NCGR *ncgr, NCLR *nclr, HBITMAP *pMaskBitmap) {
	//first, render the cell to a framebuffer.
	COLOR32 *pxbuf = (COLOR32 *) calloc(512 * 256, sizeof(COLOR32));
	CellViewerRenderCellByIndex(pxbuf, NULL, ncer, ncgr, nclr, cellno);

	//next, crop the rendered cell
	int minX, minY, cropW, cropH;
	COLOR32 *crop = CellViewerCropRenderedCell(pxbuf, 512, 256, &minX, &minY, &cropW, &cropH);
	free(pxbuf);

	//produce scaled+cropped image
	COLOR32 *scaled = ImgScaleEx(crop, cropW, cropH, PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, IMG_SCALE_FIT);
	free(crop);

	//render mask
	unsigned char *mask = ImgCreateAlphaMask(scaled, PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 0x80, NULL, NULL);

	//create bitmaps
	HBITMAP hBmColor = CreateBitmap(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 1, 32, scaled);
	HBITMAP hBmAlpha = CreateBitmap(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 1, 1, mask);
	free(scaled);
	free(mask);

	*pMaskBitmap = hBmAlpha;
	return hBmColor;
}

static void CellViewerUpdatePreview(HWND hWnd, int cellno) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);
	NCER *ncer = &data->ncer;
	PreviewLoadObjCell(ncer, NULL, cellno);
}

static HWND CellEditorGetAssociatedEditor(HWND hWnd, int type) {
	HWND hWndMain = getMainWindow(hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);

	switch (type) {
		case FILE_TYPE_PALETTE:
			return nitroPaintStruct->hWndNclrViewer;
		case FILE_TYPE_CHARACTER:
			return nitroPaintStruct->hWndNcgrViewer;
	}
	return NULL;
}


// ----- Cell list functions

static void CellViewerDeleteCell(NCERVIEWERDATA *data, int i);
static void CellViewerMoveCell(NCERVIEWERDATA *data, int iSrc, int iDst);

static LRESULT CALLBACK CellViewerCellListSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data_) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWndEditor);

	switch (msg) {
		case WM_LBUTTONDOWN:
			SetFocus(hWnd);
			break;
		case WM_LBUTTONUP:
		{
			if (!data->cellListDragging) break;

			//desktop owns drag
			ImageList_DragLeave(GetDesktopWindow());
			ImageList_EndDrag();
			ReleaseCapture();
			data->cellListDragging = 0;

			LVINSERTMARK lvim = { 0 };
			lvim.cbSize = sizeof(lvim);
			lvim.iItem = -1;
			ListView_SetInsertMark(hWnd, &lvim);

			//get dest
			LVHITTESTINFO htinfo = { 0 };
			htinfo.pt.x = (int) (short) LOWORD(lParam);
			htinfo.pt.y = (int) (short) HIWORD(lParam);
			ListView_HitTest(hWnd, &htinfo);
			if (htinfo.iItem == -1) break; // hit nowhere
			
			//move item
			CellViewerMoveCell(data, data->cellListDraggingItem, htinfo.iItem);
			break;
		}
		case WM_MOUSEMOVE:
		{
			if (!data->cellListDragging) break;

			//update drag move
			POINT pt;
			GetCursorPos(&pt);
			ImageList_DragMove(pt.x, pt.y);

			//hit test current destination
			LVHITTESTINFO htinfo = { 0 };
			htinfo.pt.x = (int) (short) LOWORD(lParam);
			htinfo.pt.y = (int) (short) HIWORD(lParam);
			ListView_HitTest(hWnd, &htinfo);
			
			LVINSERTMARK lvim = { 0 };
			lvim.cbSize = sizeof(lvim);
			lvim.iItem = htinfo.iItem;
			ListView_SetInsertMark(hWnd, &lvim);
			ListView_SetInsertMarkColor(hWnd, 0);
			break;
		}
		case WM_KEYDOWN:
		{
			if (wParam == VK_DELETE) {
				int sel = ListView_GetNextItem(hWnd, -1, LVIS_SELECTED);
				if (sel != -1) CellViewerDeleteCell(data, sel);
			}
			break;
		}
	}
	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void CellViewerSuppressRedraw(NCERVIEWERDATA *data) {
	if (data->cellListRedrawCount++ == 0) {
		//suppress redraw
		SendMessage(data->hWndCellList, WM_SETREDRAW, 0, 0);
	}
}

static void CellViewerRestoreRedraw(NCERVIEWERDATA *data) {
	if (--data->cellListRedrawCount == 0) {
		//restore redraw
		SendMessage(data->hWndCellList, WM_SETREDRAW, 1, 0);
		InvalidateRect(data->hWndCellList, NULL, FALSE);
	}
}

static void CellViewerSetCurrentCell(NCERVIEWERDATA *data, int cellno, BOOL updateList) {
	if (cellno != data->cell) CellViewerDeselect(data);
	data->cell = cellno;

	//edit focused state
	if (updateList) {
		CellViewerSuppressRedraw(data);
		ListView_SetItemState(data->hWndCellList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		if (cellno != -1) ListView_SetItemState(data->hWndCellList, cellno, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		CellViewerRestoreRedraw(data);
	}

	//update cell render
	CellViewerUpdateCellRender(data);
	InvalidateRect(data->hWndViewer, NULL, FALSE);
}

static HWND CellViewerCreateCellList(HWND hWndParent, int width) {
	DWORD lvStyle = WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VSCROLL | LVS_EDITLABELS | LVS_SINGLESEL | LVS_ICON | LVS_SHOWSELALWAYS;
	HWND h = CreateWindow(WC_LISTVIEW, L"", lvStyle, 0, 0, width, 300, hWndParent, NULL, NULL, NULL);

	//set extended style
	ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_JUSTIFYCOLUMNS | LVS_EX_SNAPTOGRID);
	SendMessage(h, LVM_SETVIEW, LV_VIEW_TILE, 0);

	RECT rcClient;
	GetClientRect(h, &rcClient);

	//set tile view info
	LVTILEVIEWINFO lvtvi = { 0 };
	lvtvi.cbSize = sizeof(lvtvi);
	lvtvi.dwMask = LVTVIM_COLUMNS | LVTVIM_TILESIZE;
	lvtvi.dwFlags = LVTVIF_FIXEDSIZE;
	lvtvi.cLines = 2;
	lvtvi.sizeTile.cx = rcClient.right - GetSystemMetrics(SM_CXVSCROLL);
	lvtvi.sizeTile.cy = PREVIEW_ICON_HEIGHT + PREVIEW_ICON_PADDING_V;
	ListView_SetTileViewInfo(h, &lvtvi);

	//init columns
	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	lvc.pszText = L"";
	lvc.fmt = LVCFMT_LEFT;
	ListView_InsertColumn(h, 0, &lvc);
	lvc.iSubItem = 1;
	ListView_InsertColumn(h, 1, &lvc);

	//create image list
	HIMAGELIST hLarge = ImageList_Create(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, ILC_MASK | ILC_COLOR24, 1, 1);
	ListView_SetImageList(h, hLarge, LVSIL_NORMAL);

	SetWindowSubclass(h, CellViewerCellListSubclassProc, 1, 0);

	return h;
}

static void CellViewerUpdateCellListPreview(NCERVIEWERDATA *data, int i) {
	//render cell preview to color and alpha mask
	NCLR *nclr = CellViewerGetAssociatedPalette(data);
	NCGR *ncgr = CellViewerGetAssociatedCharacter(data);
	HBITMAP hMaskbm;
	HBITMAP hColorbm = CellViewerRenderImageListBitmap(&data->ncer, i, ncgr, nclr, &hMaskbm);

	//
	HIMAGELIST himl = ListView_GetImageList(data->hWndCellList, LVSIL_NORMAL);

	//get original image list index
	LVITEM item;
	item.iItem = i;
	item.iSubItem = 0;
	item.mask = LVIF_IMAGE;
	ListView_GetItem(data->hWndCellList, &item);

	int imgidx = -1;
	if (item.iImage == -1) {
		//no image index, add to the list
		imgidx = ImageList_Add(himl, hColorbm, hMaskbm);

		if (imgidx != -1) {
			item.iImage = imgidx;
			ListView_SetItem(data->hWndCellList, &item);
		}
	} else {
		//item exists, replace it
		imgidx = ImageList_Replace(himl, item.iImage, hColorbm, hMaskbm);
	}

	//
	DeleteObject(hMaskbm);
	DeleteObject(hColorbm);
}

static void CellViewerInsertCellToCellList(NCERVIEWERDATA *data, int i, LPCWSTR name, NCER_CELL *cell) {
	HWND hWndList = data->hWndCellList;
	UINT subColIdxs[] = { 1 };

	WCHAR textbuf[64];
	wsprintfW(textbuf, L"[%d] %s", i, name);

	LVITEM lvi = { 0 };
	lvi.mask = LVIF_TEXT | LVIF_STATE | LVIF_IMAGE | LVIF_COLUMNS;
	lvi.pszText = textbuf; // cast away const for this struct
	lvi.state = 0;
	lvi.iSubItem = 0; // col
	lvi.iImage = -1; // filled in by CellViewerUpdateCellListPreview
	lvi.iItem = i;
	lvi.cColumns = 1; // num sub items
	lvi.puColumns = subColIdxs;
	ListView_InsertItem(hWndList, &lvi);

	wsprintfW(textbuf, L"%d OBJ", cell->nAttribs);

	lvi.mask = LVIF_TEXT | LVIF_STATE;
	lvi.pszText = textbuf;
	lvi.iSubItem = 1; // col
	lvi.iItem = i;
	ListView_SetItem(hWndList, &lvi);

	CellViewerUpdateCellListPreview(data, i);
}

static void CellViewerUpdateCellSubItemText(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	WCHAR textbuf[64];
	wsprintfW(textbuf, L"%d OBJ", cell->nAttribs);

	LVITEM lvi = { 0 };
	lvi.mask = LVIF_TEXT;
	lvi.pszText = textbuf;
	lvi.iSubItem = 1;
	lvi.iItem = data->cell;
	ListView_SetItem(data->hWndCellList, &lvi);
}

static void CellViewerAppendCellToCellList(NCERVIEWERDATA *data, LPCWSTR name, NCER_CELL *cell) {
	int n = ListView_GetItemCount(data->hWndCellList);
	CellViewerInsertCellToCellList(data, n, name, cell);
}

static void CellViewerPopulateCellList(NCERVIEWERDATA *data) {
	CellViewerSuppressRedraw(data);

	//add each cell
	for (int i = 0; i < data->ncer.nCells; i++) {
		WCHAR name[32];
		wsprintfW(name, L"Cell %d", i);
		NCER_CELL *celli = data->ncer.cells + i;
		CellViewerAppendCellToCellList(data, name, celli);
	}

	//set default selection
	if (data->ncer.nCells > 0) {
		data->cell = 0;
		CellViewerSetCurrentCell(data, 0, TRUE);
	}

	CellViewerRestoreRedraw(data);
}

static void CellViewerUpdateCellPreviews(NCERVIEWERDATA *data) {
	CellViewerSuppressRedraw(data);

	for (int i = 0; i < data->ncer.nCells; i++) {
		CellViewerUpdateCellListPreview(data, i);
	}

	CellViewerRestoreRedraw(data);
}

static void CellViewerMoveCell(NCERVIEWERDATA *data, int iSrc, int iDst) {
	if (iSrc == iDst) return; // do nothing

	CellViewerSuppressRedraw(data);

	//is source selected?
	BOOL srcSel = FALSE;
	int sel = ListView_GetNextItem(data->hWndCellList, -1, LVIS_SELECTED);
	if (sel != -1 && sel == iSrc) srcSel = TRUE;

	//rearrange
	CellMoveCellIndex(&data->ncer, iSrc, iDst);

	//move cell listing (TODO: a better way?)
	HIMAGELIST himl = ListView_GetImageList(data->hWndCellList, LVSIL_NORMAL);
	ListView_DeleteAllItems(data->hWndCellList);
	ImageList_RemoveAll(himl);
	CellViewerPopulateCellList(data);

	//if the source was selected, select the destination index
	if (iDst > iSrc) iDst--;
	CellViewerSetCurrentCell(data, iDst, TRUE);

	CellViewerRestoreRedraw(data);
}

static void CellViewerDeleteCell(NCERVIEWERDATA *data, int i) {
	CellViewerSuppressRedraw(data);

	//update cell bank structure
	int newsel = data->cell;
	CellDeleteCell(&data->ncer, i);

	//move cell listing (TODO: a better way?)
	HIMAGELIST himl = ListView_GetImageList(data->hWndCellList, LVSIL_NORMAL);
	ListView_DeleteAllItems(data->hWndCellList);
	ImageList_RemoveAll(himl);
	CellViewerPopulateCellList(data);

	//set new selection
	if (newsel >= data->ncer.nCells) newsel = data->ncer.nCells - 1;
	CellViewerSetCurrentCell(data, newsel, TRUE);
	
	CellViewerRestoreRedraw(data);
}

static void CellViewerSetMappingMode(NCERVIEWERDATA *data, int mapping) {
	CellViewerSuppressRedraw(data);

	//update mapping mode
	int sel = data->cell;
	data->ncer.mappingMode = mapping;

	//recalculate previews
	CellViewerUpdateCellPreviews(data);

	//set selection
	CellViewerSetCurrentCell(data, sel, TRUE);

	CellViewerRestoreRedraw(data);
}

static void CellViewerSetScale(NCERVIEWERDATA *data, int scale) {
	data->scale = scale;
	data->frameData.contentWidth = 512 * scale;
	data->frameData.contentHeight = 256 * scale;
	
	//update
	SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
	InvalidateRect(data->hWndViewer, NULL, FALSE);
}

static LRESULT CellViewerOnCellListNotify(NCERVIEWERDATA *data, HWND hWnd, WPARAM wParam, LPNMLISTVIEW nm) {
	switch (nm->hdr.code) {
		case LVN_ITEMCHANGED:
		{
			if (nm->uNewState & LVIS_SELECTED) {
				//selection changed
				CellViewerSetCurrentCell(data, nm->iItem, FALSE);
			}
			break;
		}
		case NM_CLICK:
		case NM_DBLCLK:
		{
			LPNMITEMACTIVATE nma = (LPNMITEMACTIVATE) nm;
			if (nma->iItem == -1) {
				//item being unselected. Mark variable to cancel the deselection.
				ListView_SetItemState(data->hWndCellList, data->cell, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
			break;
		}
		case LVN_ITEMCHANGING:
		{
			break;
		}
		case LVN_BEGINDRAG:
		{
			int sel = ListView_GetNextItem(data->hWndCellList, -1, LVNI_SELECTED);
			if (sel == -1) break;

			POINT *pt = &nm->ptAction;
			ClientToScreen(data->hWndCellList, pt);

			HIMAGELIST himl = ListView_GetImageList(data->hWndCellList, LVSIL_NORMAL);
			ImageList_BeginDrag(himl, sel, PREVIEW_ICON_WIDTH / 2, PREVIEW_ICON_HEIGHT / 2);
			ImageList_DragEnter(GetDesktopWindow(), pt->x, pt->y);
			SetCapture(data->hWndCellList);
			data->cellListDragging = 1;
			data->cellListDraggingItem = sel;
			break;
		}
		case LVN_BEGINLABELEDIT:
		{
			NMLVDISPINFO *nmd = (NMLVDISPINFO *) nm;
			int iItem = nmd->item.iItem;
			
			//get edit
			HWND hWndEdit = ListView_GetEditControl(data->hWndCellList);
			NCER_CELL *cell = data->ncer.cells + iItem;
			
			(void) cell;
			(void) hWndEdit;
			//TODO

			break;
		}
	}

	return DefWindowProc(hWnd, WM_NOTIFY, wParam, (LPARAM) nm);
}

static LRESULT CellViewerOnNotify(NCERVIEWERDATA *data, HWND hWnd, WPARAM wParam, LPARAM lParam) {
	LPNMHDR hdr = (LPNMHDR) lParam;
	
	if (data != NULL) {
		if (hdr->hwndFrom == data->hWndCellList) {
			return CellViewerOnCellListNotify(data, hWnd, wParam, (LPNMLISTVIEW) hdr);
		}
	}

	return DefWindowProc(hWnd, WM_NOTIFY, wParam, lParam);
}

static void CellViewerOnCtlCommand(NCERVIEWERDATA *data, HWND hWndControl, int notification) {
	HWND hWnd = data->hWnd;
	HWND hWndMain = getMainWindow(hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);

	int changed = 0;
	if (notification == BN_CLICKED && hWndControl == data->hWndCreateCell) {
		//check for palette and character open as well
		int nPalettes = GetAllEditors(hWndMain, FILE_TYPE_PALETTE, NULL, 0);
		int nChars = GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, NULL, 0);
		if (nPalettes == 0 || nChars == 0) {
			MessageBox(hWnd, L"Requires open palette and character.", L"Error", MB_ICONERROR);
			return;
		}

		HWND hWndNclrViewer = CellEditorGetAssociatedEditor(hWnd, FILE_TYPE_PALETTE);
		HWND hWndNcgrViewer = CellEditorGetAssociatedEditor(hWnd, FILE_TYPE_CHARACTER);
		NCGR *ncgr = &((NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer))->ncgr;
		if (data->ncer.mappingMode == GX_OBJVRAMMODE_CHAR_2D) {
			MessageBox(hWnd, L"Cannot be used with 2D mapping.", L"Error", MB_ICONERROR);
			return;
		}

		LPWSTR filter = L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0";
		LPWSTR path = openFileDialog(hWnd, L"Open Image", filter, L"");
		if (path == NULL) return;

		int width, height;
		COLOR32 *px = ImgRead(path, &width, &height);
		free(path);

		//reject images too large
		if (width > 512 || height > 256) {
			MessageBox(hWnd, L"Image too large.", L"Too large", MB_ICONERROR);
			free(px);
			return;
		}

		//create generator dialog
		HWND h = CreateWindow(L"NcerCreateCellClass", L"Generate Cell", WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT, hWndMain, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, width | (height << 16), (LPARAM) px);
		DoModal(h);

		//update UI elements
		changed = 1;

		//update palette and character window
		SendMessage(hWndNcgrViewer, NV_UPDATEPREVIEW, 0, 0);
		SendMessage(hWndNclrViewer, NV_UPDATEPREVIEW, 0, 0);

		//free px
		free(px);
	} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndMappingMode) {
		const int mappings[] = {
			GX_OBJVRAMMODE_CHAR_2D,
			GX_OBJVRAMMODE_CHAR_1D_32K,
			GX_OBJVRAMMODE_CHAR_1D_64K,
			GX_OBJVRAMMODE_CHAR_1D_128K,
			GX_OBJVRAMMODE_CHAR_1D_256K
		};
		int sel = mappings[SendMessage(data->hWndMappingMode, CB_GETCURSEL, 0, 0)];
		CellViewerSetMappingMode(data, sel);
		changed = 1;
	} else if (notification == BN_CLICKED && hWndControl == data->hWndShowBounds) {
		int state = GetCheckboxChecked(hWndControl);
		data->showCellBounds = state;
		InvalidateRect(data->hWndViewer, NULL, FALSE);
	} else if (notification == BN_CLICKED && hWndControl == data->hWndAutoCalcBounds) {
		data->autoCalcBounds = GetCheckboxChecked(hWndControl);
	} else if (notification == BN_CLICKED && hWndControl == data->hWndCellAdd) {
		WCHAR name[64];
		wsprintfW(name, L"Cell %d", data->ncer.nCells);

		data->ncer.nCells++;
		data->ncer.cells = (NCER_CELL *) realloc(data->ncer.cells, data->ncer.nCells * sizeof(NCER_CELL));

		NCER_CELL *cell = data->ncer.cells + data->ncer.nCells - 1;
		memset(cell, 0, sizeof(NCER_CELL));

		CellViewerAppendCellToCellList(data, name, cell);
		CellViewerSetCurrentCell(data, data->ncer.nCells - 1, TRUE);
	}

	//log a change
	if (changed) {
		SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
	}
}

static int CellViewerGetMenuIndexByID(int id, const unsigned short *pIds, int nIds) {
	for (int i = 0; i < nIds; i++) {
		if ((int) pIds[i] == id) return i;
	}
	return -1;
}


// ----- Cell manipulation helpers

static void CellViewerSetSelectionPalette(NCERVIEWERDATA *data, int palno) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		uint16_t *pAttr2 = &cell->attr[3 * data->selectedOBJ[i] + 2];
		*pAttr2 = (*pAttr2 & 0x0FFF) | (palno << 12);
	}
}

static void CellViewerSetSelectionShapeSize(NCERVIEWERDATA *data, int shape, int size) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		uint16_t *pAttr0 = &cell->attr[3 * data->selectedOBJ[i] + 0];
		uint16_t *pAttr1 = &cell->attr[3 * data->selectedOBJ[i] + 1];
		*pAttr0 = (*pAttr0 & 0x3FFF) | (shape << 14);
		*pAttr1 = (*pAttr1 & 0x3FFF) | (size << 14);
	}
}

static void CellViewerSetSelectionType(NCERVIEWERDATA *data, int type) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		uint16_t *pAttr0 = &cell->attr[3 * data->selectedOBJ[i] + 0];
		*pAttr0 = (*pAttr0 & 0xF3FF) | (type << 10);
	}
}

static void CellViewerSetSelectionPriority(NCERVIEWERDATA *data, int prio) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		uint16_t *pAttr2 = &cell->attr[3 * data->selectedOBJ[i] + 2];
		*pAttr2 = (*pAttr2 & 0xF3FF) | (prio << 10);
	}
}

static void CellViewerFlipSelection(NCERVIEWERDATA *data, int h, int v) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		uint16_t *pAttr0 = &cell->attr[3 * data->selectedOBJ[i] + 0];
		if (*pAttr0 & 0x0100) continue; //affine (cannot be flipped)

		uint16_t *pAttr1 = &cell->attr[3 * data->selectedOBJ[i] + 1];
		if (h) *pAttr1 ^= 0x1000;
		if (v) *pAttr1 ^= 0x2000;
	}
}

static void CellViewerToggleAffineSelection(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		uint16_t *pAttr0 = &cell->attr[3 * data->selectedOBJ[i] + 0];
		uint16_t *pAttr1 = &cell->attr[3 * data->selectedOBJ[i] + 1];
		*pAttr0 ^= 0x0100; // toggle affine
		*pAttr1 &= 0x3E00; // clear H/V and affine parameter
	}
}

static void CellViewerToggleDoubleSizeSelection(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	for (int i = 0; i < data->nSelectedOBJ; i++) {
		uint16_t *pAttr0 = &cell->attr[3 * data->selectedOBJ[i] + 0];
		if (!(*pAttr0 & 0x0100)) continue; //not affine (cannot be double size)
		
		*pAttr0 ^= 0x0200;
	}
}

static void CellViewerSendSelectionToFront(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	//create copy of selection and delete it
	int nSel;
	uint16_t *sel = CellViewerGetSelectedOamAttributes(data, &nSel);
	CellViewerDeleteSelection(data);

	//send to front: copy OBJ to front of list
	cell->nAttribs += nSel;
	cell->attr = (uint16_t *) realloc(cell->attr, cell->nAttribs * 6);
	memmove(cell->attr + 3 * nSel, cell->attr, (cell->nAttribs - nSel) * 6);
	memcpy(cell->attr, sel, nSel * 6);

	//re-select moved OBJ
	int *selidxs = (int *) calloc(nSel, sizeof(int));
	for (int i = 0; i < nSel; i++) selidxs[i] = i;
	CellViewerDeselect(data);
	data->nSelectedOBJ = nSel;
	data->selectedOBJ = selidxs;

	free(sel);
}

static void CellViewerSendSelectionToBack(NCERVIEWERDATA *data) {
	NCER_CELL *cell = CellViewerGetCurrentCell(data);
	if (cell == NULL) return;

	//create copy of selection and delete it
	int nSel;
	uint16_t *sel = CellViewerGetSelectedOamAttributes(data, &nSel);
	CellViewerDeleteSelection(data);

	//send to front: copy OBJ to end of list
	cell->nAttribs += nSel;
	cell->attr = (uint16_t *) realloc(cell->attr, cell->nAttribs * 6);
	memcpy(cell->attr + 3 * (cell->nAttribs - nSel), sel, nSel * 6);

	//re-select moved OBJ
	int *selidxs = (int *) calloc(nSel, sizeof(int));
	for (int i = 0; i < nSel; i++) selidxs[i] = i + cell->nAttribs - nSel;
	CellViewerDeselect(data);
	data->nSelectedOBJ = nSel;
	data->selectedOBJ = selidxs;

	free(sel);
}

static void CellViewerOnMenuCommand(NCERVIEWERDATA *data, int idMenu) {
	HWND hWnd = data->hWnd;
	switch (idMenu) {
		case ID_FILE_SAVEAS:
		case ID_FILE_SAVE:
		{
			if (data->szOpenFile[0] == L'\0' || idMenu == ID_FILE_SAVEAS) {
				LPCWSTR filter = L"NCER Files (*.ncer)\0*.ncer\0All Files\0*.*\0";
				switch (data->ncer.header.format) {
					case NCER_TYPE_HUDSON:
						filter = L"Cell Files (*.bin)\0*.bin;\0All Files\0*.*\0";
						break;
				}
				LPWSTR path = saveFileDialog(getMainWindow(hWnd), L"Save As...", filter, L"ncer");
				if (path != NULL) {
					EditorSetFile(hWnd, path);
					free(path);
				} else break;
			}
			CellWriteFile(&data->ncer, data->szOpenFile);
			break;
		}
		case ID_FILE_EXPORT:
		{
			LPWSTR location = saveFileDialog(hWnd, L"Save Bitmap", L"PNG Files (*.png)\0*.png\0All Files\0*.*\0", L"png");
			if (location == NULL) break;

			NCER *ncer = &data->ncer;
			NCLR *nclr = CellViewerGetAssociatedPalette(data);
			NCGR *ncgr = CellViewerGetAssociatedCharacter(data);

			COLOR32 *bits = (COLOR32 *) calloc(256 * 512, sizeof(COLOR32));
			CellViewerRenderCellByIndex(bits, NULL, ncer, ncgr, nclr, data->cell);
			ImgSwapRedBlue(bits, 512, 256);
			ImgWrite(bits, 512, 256, location);

			free(bits);
			free(location);
			break;
		}
		case ID_ZOOM_100:
		case ID_ZOOM_200:
		case ID_ZOOM_400:
		case ID_ZOOM_800:
		case ID_ZOOM_1600:
		case ID_VIEW_GRIDLINES:
			SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;

		case ID_CELLMENU_CUT:
			CellViewerCopy(data);
			CellViewerDeleteSelection(data);
			if (data->hWndAutoCalcBounds) CellViewerUpdateBounds(data);
			CellViewerGraphicsUpdated(data->hWnd);
			CellViewerUpdateCellSubItemText(data);
			break;
		case ID_CELLMENU_COPY:
			CellViewerCopy(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case ID_CELLMENU_PASTE:
			CellViewerPaste(data);
			CellViewerGraphicsUpdated(data->hWnd);
			CellViewerUpdateCellSubItemText(data);
			break;
		case ID_CELLMENU_SENDTOFRONT:
			CellViewerSendSelectionToFront(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case ID_CELLMENU_SENDTOBACK:
			CellViewerSendSelectionToBack(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case ID_OBJPALETTE_0:
		case ID_OBJPALETTE_1:
		case ID_OBJPALETTE_2:
		case ID_OBJPALETTE_3:
		case ID_OBJPALETTE_4:
		case ID_OBJPALETTE_5:
		case ID_OBJPALETTE_6:
		case ID_OBJPALETTE_7:
		case ID_OBJPALETTE_8:
		case ID_OBJPALETTE_9:
		case ID_OBJPALETTE_10:
		case ID_OBJPALETTE_11:
		case ID_OBJPALETTE_12:
		case ID_OBJPALETTE_13:
		case ID_OBJPALETTE_14:
		case ID_OBJPALETTE_15:
		{
			int palno = CellViewerGetMenuIndexByID(idMenu, sMenuIdPalettes, 16);
			CellViewerSetSelectionPalette(data, palno);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		}
		case ID_OBJSIZE_8X8:
		case ID_OBJSIZE_8X16:
		case ID_OBJSIZE_8X32:
		case ID_OBJSIZE_16X8:
		case ID_OBJSIZE_16X16:
		case ID_OBJSIZE_16X32:
		case ID_OBJSIZE_32X8:
		case ID_OBJSIZE_32X16:
		case ID_OBJSIZE_32X32:
		case ID_OBJSIZE_32X64:
		case ID_OBJSIZE_64X32:
		case ID_OBJSIZE_64X64:
		{
			int size = CellViewerGetMenuIndexByID(idMenu, sMenuIdSizes, 12);
			CellViewerSetSelectionShapeSize(data, size / 4, size % 4);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		}
		case ID_OBJTYPE_NORMAL:
		case ID_OBJTYPE_TRANSLUCENT:
		case ID_OBJTYPE_WINDOW:
		case ID_OBJTYPE_BITMAP:
		{
			int type = CellViewerGetMenuIndexByID(idMenu, sMenuIdTypes, 4);
			CellViewerSetSelectionType(data, type);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		}
		case ID_OBJPRIORITY_0:
		case ID_OBJPRIORITY_1:
		case ID_OBJPRIORITY_2:
		case ID_OBJPRIORITY_3:
		{
			int prio = CellViewerGetMenuIndexByID(idMenu, sMenuIdPrios, 4);
			CellViewerSetSelectionPriority(data, prio);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		}
		case ID_CELLMENU_HFLIP:
		case ID_CELLMENU_VFLIP:
			CellViewerFlipSelection(data, idMenu == ID_CELLMENU_HFLIP, idMenu == ID_CELLMENU_VFLIP);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case ID_CELLMENU_AFFINE:
			CellViewerToggleAffineSelection(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case ID_CELLMENU_DOUBLESIZE:
			CellViewerToggleDoubleSizeSelection(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
	}
}

static void CellViewerOnAccelerator(NCERVIEWERDATA *data, int idAccelerator) {
	switch (idAccelerator) {
		case ID_ACCELERATOR_CUT:
			CellViewerCopy(data);
			CellViewerDeleteSelection(data);
			if (data->hWndAutoCalcBounds) CellViewerUpdateBounds(data);
			CellViewerGraphicsUpdated(data->hWnd);
			CellViewerUpdateCellSubItemText(data);
			break;
		case ID_ACCELERATOR_COPY:
			CellViewerCopy(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case ID_ACCELERATOR_PASTE:
			CellViewerPaste(data);
			CellViewerGraphicsUpdated(data->hWnd);
			CellViewerUpdateCellSubItemText(data);
			break;
		case ID_ACCELERATOR_SELECT_ALL:
			CellViewerSelectAll(data);
			InvalidateRect(data->hWndViewer, NULL, FALSE);
			break;
		case ID_ACCELERATOR_DESELECT:
			CellViewerDeselect(data);
			InvalidateRect(data->hWndViewer, NULL, FALSE);
			break;
	}
}

static void CellViewerOnCommand(NCERVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	if (lParam) {
		CellViewerOnCtlCommand(data, (HWND) lParam, HIWORD(wParam));
	} else if (HIWORD(wParam) == 0) {
		CellViewerOnMenuCommand(data, LOWORD(wParam));
	} else if (HIWORD(wParam) == 1) {
		CellViewerOnAccelerator(data, LOWORD(wParam));
	}
}

static void CellViewerOnTimer(NCERVIEWERDATA *data, int idTimer) {
	switch (idTimer) {
		case ID_TIMER_GFX_UPDATE:
		{
			//timer for graphics update: re-render the cell previews
			CellViewerUpdateCellPreviews(data);
			CellViewerUpdateCellRender(data);
			InvalidateRect(data->hWndViewer, NULL, FALSE);

			//ACK event
			data->foreignDataUpdate = 0;
			KillTimer(data->hWnd, idTimer);
			break;
		}
	}
}

static LRESULT WINAPI CellViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
		{
			float dpiScale = GetDpiScale();

			data->scale = 2;
			data->showBorders = 1;
			data->showCellBounds = 1;
			data->cellListRedrawCount = 0;
			data->autoCalcBounds = 0;
			FbCreate(&data->fb, hWnd, 0, 0);
			data->hWndViewer = CreateWindow(L"CellPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS, 200, 0, 200, 20, hWnd, NULL, NULL, NULL);

			//mapping modes
			LPCWSTR mappingNames[] = {
				L"2D", L"1D 32K", L"1D 64K", L"1D 128K", L"1D 256K"
			};


			int ctlWidth = UI_SCALE_COORD(100, dpiScale);
			int ctlWidthNarrow = UI_SCALE_COORD(75, dpiScale);
			int ctlWidthWide = UI_SCALE_COORD(150, dpiScale);
			int ctlHeight = UI_SCALE_COORD(22, dpiScale);
			int cellListWidth = UI_SCALE_COORD(200, dpiScale);
			data->hWndCellList = CellViewerCreateCellList(hWnd, cellListWidth);
			data->hWndCellAdd = CreateButton(hWnd, L"New Cell", 175, 300, 25, 22, FALSE);
			data->hWndMappingModeLabel = CreateStatic(hWnd, L" Mapping Mode:", UI_SCALE_COORD(200, dpiScale), 0, ctlWidth, ctlHeight);
			data->hWndMappingMode = CreateCombobox(hWnd, mappingNames, 5, UI_SCALE_COORD(300, dpiScale), 0, ctlWidthNarrow, 100, 0);
			data->hWndCreateCell = CreateButton(hWnd, L"Generate Cell", UI_SCALE_COORD(385, dpiScale), 0, ctlWidth, ctlHeight, FALSE);
			data->hWndShowBounds = CreateCheckbox(hWnd, L"Show Bounds", UI_SCALE_COORD(495, dpiScale), 0, ctlWidth, ctlHeight, data->showCellBounds);
			data->hWndAutoCalcBounds = CreateCheckbox(hWnd, L"Auto-Calculate Bounds", UI_SCALE_COORD(595, dpiScale), 0, ctlWidthWide, ctlHeight, data->autoCalcBounds);
			break;
		}
		case NV_INITIALIZE_IMMEDIATE:
		case NV_INITIALIZE:
		{
			if (msg == NV_INITIALIZE) {
				LPWSTR path = (LPWSTR) wParam;
				memcpy(&data->ncer, (NCER *) lParam, sizeof(NCER));
				EditorSetFile(hWnd, path);
			} else {
				NCER *ncer = (NCER *) lParam;
				memcpy(&data->ncer, ncer, sizeof(NCER));
			}
			SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

			data->frameData.contentWidth = 512 * data->scale;
			data->frameData.contentHeight = 256 * data->scale;

			//set mapping mode selection
			int mappingIndex = 0;
			switch (data->ncer.mappingMode) {
				case GX_OBJVRAMMODE_CHAR_2D:
					mappingIndex = 0; break;
				case GX_OBJVRAMMODE_CHAR_1D_32K:
					mappingIndex = 1; break;
				case GX_OBJVRAMMODE_CHAR_1D_64K:
					mappingIndex = 2; break;
				case GX_OBJVRAMMODE_CHAR_1D_128K:
					mappingIndex = 3; break;
				case GX_OBJVRAMMODE_CHAR_1D_256K:
					mappingIndex = 4; break;
			}
			SendMessage(data->hWndMappingMode, CB_SETCURSEL, mappingIndex, 0);

			//init cell editor
			CellViewerPopulateCellList(data);

			data->showGuidelines = 1;
			SetFocus(data->hWndCellList);
			CellViewerPreviewCenter(data);
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			float dpiScale = GetDpiScale();
			int ctlHeight = UI_SCALE_COORD(22, dpiScale);
			int cellListWidth = UI_SCALE_COORD(200, dpiScale);

			int ctlY = rcClient.bottom - ctlHeight;
			MoveWindow(data->hWndCellList, 0, 0, cellListWidth, ctlY, TRUE);
			MoveWindow(data->hWndCellAdd, 0, ctlY, cellListWidth, ctlHeight, TRUE);
			MoveWindow(data->hWndViewer, cellListWidth, ctlHeight, rcClient.right - cellListWidth, rcClient.bottom - ctlHeight, TRUE);

			if (wParam == SIZE_RESTORED) InvalidateRect(hWnd, NULL, TRUE); //full update
			return DefMDIChildProc(hWnd, WM_SIZE, wParam, lParam);
		}
		case WM_TIMER:
			CellViewerOnTimer(data, wParam);
			break;
		case WM_PAINT:
			InvalidateRect(data->hWndViewer, NULL, FALSE);
			break;
		case NV_UPDATEPREVIEW:
			CellViewerUpdatePreview(hWnd, data->cell);
			break;
		case WM_NOTIFY:
			return CellViewerOnNotify(data, hWnd, wParam, lParam);
		case WM_COMMAND:
			CellViewerOnCommand(data, wParam, lParam);
			break;
		case WM_DESTROY:
		{
			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
			nitroPaintStruct->hWndNcerViewer = NULL;
			if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
			FbDestroy(&data->fb);
			CellViewerDeselect(data);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

typedef struct CELLGENDATA_ {
	int lastOptimization; //last optimization setting
	int lastBoundType;    //last bound type setting
	int lastAffine;       //last affine state

	int xMin, xMax;
	int yMin, yMax;

	HWND hWndAggressiveness;
	HWND hWndOk;
	HWND hWndCancel;
	HWND hWndAggressivenessLabel;
	HWND hWndObjLabel;
	HWND hWndCharLabel;
	HWND hWndCharacter;
	HWND hWndBoundType;

	//cell
	HWND hWndAffine;
	HWND hWndMatrixSlot;
	HWND hWndPriority;
	HWND hWndWriteMode;

	//palette
	HWND hWndWritePalette;
	HWND hWndPalette;
	HWND hWndPaletteOffset;
	HWND hWndPaletteLength;

	//position
	HWND hWndPosX;
	HWND hWndPosY;
	HWND hWndAnchorX;
	HWND hWndAnchorY;

	//graphics
	HWND hWndDither;
	HWND hWndDiffuse;
	HWND hWndOptimize;

	//color
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;
} CELLGENDATA;

static LRESULT CALLBACK NcerCreateCellWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CELLGENDATA *data = (CELLGENDATA *) GetWindowLongPtr(hWnd, 3 * sizeof(LONG_PTR));

	COLOR32 *px = (COLOR32 *) GetWindowLongPtr(hWnd, 0 * sizeof(LONG_PTR));
	int width = GetWindowLongPtr(hWnd, 1 * sizeof(LONG_PTR));
	int height = GetWindowLongPtr(hWnd, 2 * sizeof(LONG_PTR));

	//controls
	float dpiScale = GetDpiScale();
	int previewX = (int) (21 * dpiScale + 0.5f), previewY = (int) (59 * dpiScale + 0.5f);
	int previewWidth = (int) (512 * dpiScale + 0.5f);
	int previewHeight = (int) (256 * dpiScale + 0.5f);

	switch (msg) {
		case WM_NCCREATE:
			//allocate data
			data = (CELLGENDATA *) calloc(1, sizeof(CELLGENDATA));
			data->lastOptimization = 100;
			data->lastBoundType = 0;
			SetWindowLongPtr(hWnd, 3 * sizeof(LONG_PTR), (LONG_PTR) data);
			break;
		case WM_CREATE:
			SetWindowSize(hWnd, 988, 369);
			break;
		case WM_TIMER:
		{
			int aggressiveness = GetTrackbarPosition(data->hWndAggressiveness);
			int boundType = SendMessage(data->hWndBoundType, CB_GETCURSEL, 0, 0);
			int affine = GetCheckboxChecked(data->hWndAffine);

			//invalidate update part
			if (aggressiveness != data->lastOptimization || boundType != data->lastBoundType || affine != data->lastAffine) {
				RECT rc;
				rc.left = previewX;
				rc.top = previewY;
				rc.right = previewX + previewWidth;
				rc.bottom = previewY + previewHeight;
				InvalidateRect(hWnd, &rc, FALSE);
				SetEditNumber(data->hWndAggressivenessLabel, aggressiveness);

				data->lastOptimization = aggressiveness;
				data->lastBoundType = boundType;
				data->lastAffine = affine;
			}
			break;
		}
		case WM_PAINT:
		{
			int posX = GetEditNumber(data->hWndPosX);
			int posY = GetEditNumber(data->hWndPosY);
			int anchorX = SendMessage(data->hWndAnchorX, CB_GETCURSEL, 0, 0);
			int anchorY = SendMessage(data->hWndAnchorY, CB_GETCURSEL, 0, 0);
			int affine = GetCheckboxChecked(data->hWndAffine);

			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			//draw offscreen
			HDC hCompatibleDC = CreateCompatibleDC(hDC);
			HBITMAP hCompatibleBitmap = CreateCompatibleBitmap(hDC, previewWidth, previewHeight);
			SelectObject(hCompatibleDC, hCompatibleBitmap);

			//draw to hCompatibleDC
			Rectangle(hCompatibleDC, 0, 0, previewWidth, previewHeight);

			HPEN hBluePen = CreatePen(PS_SOLID, 1, RGB(0, 0, 255));
			HBRUSH hFillBrush = CreateSolidBrush(RGB(127, 127, 255));
			SelectObject(hCompatibleDC, hBluePen);
			SelectObject(hCompatibleDC, hFillBrush);

			//calculate offset of obj
			int boundingWidth = 0, boundingHeight = 0, ofsX = 0, ofsY = 0;
			if (data != NULL) {
				boundingWidth = data->xMax - data->xMin;
				boundingHeight = data->yMax - data->yMin;

				if (data->lastBoundType == 1) {
					//full
					boundingWidth = width;
					boundingHeight = height;
					ofsX = -boundingWidth / 2;
					ofsY = -boundingHeight / 2;
				} else {
					//opaque
					boundingWidth = data->xMax - data->xMin;
					boundingHeight = data->yMax - data->yMin;
					ofsX = -boundingWidth / 2 - data->xMin;
					ofsY = -boundingHeight / 2 - data->yMin;
				}
			}

			//offset by user setting
			switch (anchorX) {
				case 0: //left
					ofsX += boundingWidth / 2; break;
				case 2: //right
					ofsX -= boundingWidth / 2; break;
			}
			switch (anchorY) {
				case 0: //top
					ofsY += boundingHeight / 2; break;
				case 2: //bottom
					ofsY -= boundingHeight / 2; break;
			}
			ofsX += posX;
			ofsY += posY;
			
			//must have image loaded
			if (px != NULL) {
				int nObj, nChars = 0;
				int aggressiveness = GetTrackbarPosition(data->hWndAggressiveness);
				int boundType = SendMessage(data->hWndBoundType, CB_GETCURSEL, 0, 0);
				OBJ_BOUNDS *bounds = CellgenMakeCell(px, width, height, aggressiveness, boundType, affine, &nObj);

				for (int i = 0; i < nObj; i++) {
					OBJ_BOUNDS *b = bounds + i;
					b->x += ofsX;
					b->y += ofsY;

					int bx = b->x + 256;
					int by = b->y + 128;
					int br = bx + b->width;
					int bd = by + b->height;
					Rectangle(hCompatibleDC, (int) (bx * dpiScale + 0.5f), (int) (by * dpiScale + 0.5f),
						(int) (br * dpiScale + 0.5f), (int) (bd * dpiScale + 0.5f));

					//tally characters
					nChars += (b->width * b->height) / 64;
				}

				free(bounds);

				WCHAR objText[16];
				int len = wsprintfW(objText, L"%d OBJ", nObj);
				SendMessage(data->hWndObjLabel, WM_SETTEXT, len, (LPARAM) objText);

				len = wsprintfW(objText, L"%d characters", nChars);
				SendMessage(data->hWndCharLabel, WM_SETTEXT, len, (LPARAM) objText);
			}

			BitBlt(hDC, previewX, previewY, (int) (512 * dpiScale + 0.5f), (int) (256 * dpiScale + 0.5f), hCompatibleDC, 0, 0, SRCCOPY);
			DeleteObject(hCompatibleDC);
			DeleteObject(hCompatibleBitmap);
			DeleteObject(hBluePen);
			DeleteObject(hFillBrush);

			EndPaint(hWnd, &ps);
			break;
		}
		case NV_INITIALIZE:
		{
			px = (COLOR32 *) lParam;
			width = LOWORD(wParam);
			height = HIWORD(wParam);

			CellgenGetBounds(px, width, height, &data->xMin, &data->xMax, &data->yMin, &data->yMax);

			//try get initial Optimization setting. Balances VRAM use with OBJ usage with a simple heuristic.
			int bestUsage = 0, bestOptimization = 0;
			if (px != NULL) {
				for (int i = 0; i < 100; i++) {
					int nObj, nChars = 0;
					OBJ_BOUNDS *bounds = CellgenMakeCell(px, width, height, i, 0, 0, &nObj);
					for (int i = 0; i < nObj; i++) nChars += (bounds[i].width * bounds[i].height) / 64;
					if (bounds != NULL) free(bounds);

					//heuristic: proportion of OBJ used + proportion of character VRAM used (using 16K bank)
					int usage = nChars * 0x20 + (nObj * 16384 / (128));
					if (i == 0 || (usage <= bestUsage)) {
						bestOptimization = i;
						bestUsage = usage;
					}
				}
			}

			SetWindowLongPtr(hWnd, 0 * sizeof(LONG_PTR), (LONG_PTR) px);
			SetWindowLongPtr(hWnd, 1 * sizeof(LONG_PTR), width);
			SetWindowLongPtr(hWnd, 2 * sizeof(LONG_PTR), height);

			//setup controls
			int groupWidth = 207, groupHeight = 131, group2Height = 77, groupX = 554, groupY = 10;

			LPCWSTR boundModes[] = { L"Opaque", L"Full Image" };
			CreateStatic(hWnd, L"Optimization:", 10, 10, 70, 22);
			data->hWndAggressiveness = CreateTrackbar(hWnd, 90, 10, 150, 22, 0, 100, bestOptimization);
			data->hWndAggressivenessLabel = CreateStatic(hWnd, L"100", 250, 10, 30, 22);
			CreateStatic(hWnd, L"Character:", 290, 10, 60, 22);
			data->hWndCharacter = CreateEdit(hWnd, L"0", 350, 10, 40, 22, TRUE);
			CreateStatic(hWnd, L"Bounds:", 400, 10, 50, 22);
			data->hWndBoundType = CreateCombobox(hWnd, boundModes, 2, 460, 10, 75, 100, 0);
			CreateGroupbox(hWnd, L"Preview", 10, 42, 534, 285);
			data->hWndObjLabel = CreateStatic(hWnd, L"0 OBJ", 10, 337, 75, 22);
			data->hWndCharLabel = CreateStaticAligned(hWnd, L"0 characters", 10 + 534 - 75, 337, 75, 22, SCA_RIGHT);
			data->hWndOk = CreateButton(hWnd, L"Complete", 988 - 10 - 100, 337, 100, 22, TRUE);
			data->hWndCancel = CreateButton(hWnd, L"Cancel", 988 - 10 - 100 - 5 - 100, 337, 100, 22, FALSE);

			//Cell
			LPCWSTR genModes[] = { L"Replace", L"Prepend", L"Append" };
			LPCWSTR priorities[] = { L"0", L"1", L"2", L"3" };
			data->hWndAffine = CreateCheckbox(hWnd, L"Affine", groupX + 11, groupY + 17, 50, 22, FALSE);
			CreateStatic(hWnd, L"Matrix Slot:", groupX + 11, groupY + 17 + 27, 60, 22);
			data->hWndMatrixSlot = CreateEdit(hWnd, L"0", groupX + 11 + 65, groupY + 17 + 27, 75, 22, TRUE);
			CreateStatic(hWnd, L"Priority:", groupX + 11, groupY + 17 + 27 * 2, 60, 22);
			data->hWndPriority = CreateCombobox(hWnd, priorities, 4, groupX + 11 + 65, groupY + 17 + 27 * 2, 75, 100, 0);
			CreateStatic(hWnd, L"Mode:", groupX + 11, groupY + 17 + 27 * 3, 60, 22);
			data->hWndWriteMode = CreateCombobox(hWnd, genModes, 3, groupX + 11 + 65, groupY + 17 + 27 * 3, 75, 100, 0);

			//Palette
			data->hWndWritePalette = CreateCheckbox(hWnd, L"Write Palette", groupX + groupWidth + 10 + 11, groupY + 17, 100, 22, TRUE);
			data->hWndPalette = CreateCombobox(hWnd, NULL, 0, groupX + groupWidth + 10 + 11, groupY + 17 + 27, 100, 100, 0);
			CreateStatic(hWnd, L"Offset:", groupX + groupWidth + 10 + 11, groupY + 17 + 27 * 2, 75, 22);
			data->hWndPaletteOffset = CreateEdit(hWnd, L"0", groupX + groupWidth + 10 + 11 + 80, groupY + 17 + 27 * 2, 75, 22, TRUE);
			CreateStatic(hWnd, L"Length:", groupX + groupWidth + 10 + 11, groupY + 17 + 27 * 3, 75, 22);
			data->hWndPaletteLength = CreateEdit(hWnd, L"16", groupX + groupWidth + 10 + 11 + 80, groupY + 17 + 27 * 3, 75, 22, TRUE);

			//Position
			LPCWSTR anchorXs[] = { L"Left", L"Center", L"Right" };
			LPCWSTR anchorYs[] = { L"Top", L"Middle", L"Bottom" };
			CreateStatic(hWnd, L"Position:", groupX + 11, groupY + groupHeight + 3 + 17, 60, 22);
			data->hWndPosX = CreateEdit(hWnd, L"0", groupX + 11 + 65, groupY + groupHeight + 3 + 17, 60, 22, FALSE);
			data->hWndPosY = CreateEdit(hWnd, L"0", groupX + 11 + 125, groupY + groupHeight + 3 + 17, 60, 22, FALSE);
			CreateStatic(hWnd, L"Anchor:", groupX + 11, groupY + groupHeight + 3 + 17 + 27, 60, 22);
			data->hWndAnchorX = CreateCombobox(hWnd, anchorXs, 3, groupX + 11 + 65, groupY + groupHeight + 3 + 17 + 27, 60, 100, 1);
			data->hWndAnchorY = CreateCombobox(hWnd, anchorYs, 3, groupX + 11 + 65 + 60, groupY + groupHeight + 3 + 17 + 27, 60, 100, 1);

			//Graphics
			data->hWndDither = CreateCheckbox(hWnd, L"Dither", groupX + groupWidth + 10 + 11, groupY + groupHeight + 3 + 17, 60, 22, FALSE);
			CreateStatic(hWnd, L"Diffuse:", groupX + groupWidth + 10 + 11, groupY + groupHeight + 3 + 17 + 27, 60, 22);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", groupX + groupWidth + 10 + 11 + 65, groupY + groupHeight + 3 + 17 + 27, 60, 22, TRUE);
			data->hWndOptimize = CreateCheckbox(hWnd, L"Optimize", groupX + groupWidth + 10  + 11+ 65, groupY + groupHeight + 3 + 17, 60, 22, TRUE);

			//color
			int bottomY = groupY + groupHeight + group2Height + 6 + 17;
			CreateStatic(hWnd, L"Balance:", groupX + 11, bottomY, 100, 22);
			CreateStatic(hWnd, L"Color Balance:", groupX + 11, bottomY + 27, 100, 22);
			CreateStaticAligned(hWnd, L"Lightness", groupX + 11 + 110, bottomY, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Color", groupX + 11 + 110 + 50 + 200, bottomY, 50, 22, SCA_LEFT);
			CreateStaticAligned(hWnd, L"Green", groupX + 11 + 110, bottomY + 27, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Red", groupX + 11 + 110 + 50 + 200, bottomY + 27, 50, 22, SCA_LEFT);
			data->hWndBalance = CreateTrackbar(hWnd, groupX + 11 + 110 + 50, bottomY, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			data->hWndColorBalance = CreateTrackbar(hWnd, groupX + 11 + 110 + 50, bottomY + 27, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			data->hWndEnhanceColors = CreateCheckbox(hWnd, L"Enhance Colors", groupX + 11, bottomY + 27 * 2, 200, 22, FALSE);

			//groupboxes
			CreateGroupbox(hWnd, L"Cell", groupX, groupY, groupWidth, groupHeight);
			CreateGroupbox(hWnd, L"Palette", groupX + groupWidth + 10, groupY, groupWidth, groupHeight);
			CreateGroupbox(hWnd, L"Position", groupX, groupY + groupHeight + 3, groupWidth, group2Height);
			CreateGroupbox(hWnd, L"Graphics", groupX + groupWidth + 10, groupY + groupHeight + 3, groupWidth, group2Height);
			CreateGroupbox(hWnd, L"Color", groupX, groupY + groupHeight + group2Height + 6, groupWidth * 2 + 10, 103);

			//populate palette dropdown
			for (int i = 0; i < 16; i++) {
				WCHAR bf[16];
				wsprintfW(bf, L"Palette %d", i);
				SendMessage(data->hWndPalette, CB_ADDSTRING, 0, (LPARAM) bf);
			}
			SendMessage(data->hWndPalette, CB_SETCURSEL, 0, 0);

			//set timer
			SetTimer(hWnd, 1, 50, NULL);

			SetGUIFont(hWnd);

			//lastly, try populate character base
			HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT), hWndNcgrEditor;
			HWND hWndNcerViewer;
			GetAllEditors(hWndMain, FILE_TYPE_CELL, &hWndNcerViewer, 1);
			GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, &hWndNcgrEditor, 1);
			NCGR *ncgr = (NCGR *) EditorGetObject(hWndNcgrEditor);
			NCERVIEWERDATA *ncerViewerData = (NCERVIEWERDATA *) EditorGetData(hWndNcerViewer);

			/*int lastIndex = -1;
			unsigned char zeroChar[64] = { 0 };
			for (int i = 0; i < ncgr->nTiles; i++) {
				if (memcmp(ncgr->tiles[i], zeroChar, sizeof(zeroChar)) != 0) {
					lastIndex = i;
				}
			}*/
			unsigned int lastIndex = CellViewerGetFirstUnusedCharacter(ncerViewerData, -1);
			SetEditNumber(data->hWndCharacter, lastIndex);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
			int idc = LOWORD(wParam);

			//update preview for changed controls
			if ((hWndControl == data->hWndPosX || hWndControl == data->hWndPosY) && notif == EN_CHANGE) {
				InvalidateRect(hWnd, NULL, FALSE);
			}
			if ((hWndControl == data->hWndAnchorX || hWndControl == data->hWndAnchorY) && notif == CBN_SELCHANGE) {
				InvalidateRect(hWnd, NULL, FALSE);
			}

			if (notif == BN_CLICKED && (hWndControl == data->hWndOk || idc == IDOK)) {

				//cell params
				int affine = GetCheckboxChecked(data->hWndAffine);
				int affineIdx = GetEditNumber(data->hWndMatrixSlot);
				int prio = SendMessage(data->hWndPriority, CB_GETCURSEL, 0, 0);
				int insertMode = SendMessage(data->hWndWriteMode, CB_GETCURSEL, 0, 0);
				int boundType = SendMessage(data->hWndBoundType, CB_GETCURSEL, 0, 0);
				if (!affine) affineIdx = 0;

				//palette params
				int writePalette = GetCheckboxChecked(data->hWndWritePalette);
				int paletteIndex = SendMessage(data->hWndPalette, CB_GETCURSEL, 0, 0);
				int paletteOffset = GetEditNumber(data->hWndPaletteOffset);
				int paletteLength = GetEditNumber(data->hWndPaletteLength);
				if (paletteOffset == 0) {
					paletteOffset++;
					paletteLength--;
				}

				//position params
				int ofsX = GetEditNumber(data->hWndPosX);
				int ofsY = GetEditNumber(data->hWndPosY);
				int anchorX = SendMessage(data->hWndAnchorX, CB_GETCURSEL, 0, 0);
				int anchorY = SendMessage(data->hWndAnchorY, CB_GETCURSEL, 0, 0);

				//graphics
				int optimize = GetCheckboxChecked(data->hWndOptimize);
				int dither = GetCheckboxChecked(data->hWndDither);
				float diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;
				if (!dither) diffuse = 0.0f;

				//balance
				int balance = GetTrackbarPosition(data->hWndBalance);
				int colorBalance = GetTrackbarPosition(data->hWndColorBalance);
				int enhanceColors = GetCheckboxChecked(data->hWndEnhanceColors);

				//generate
				int nObj;
				int charBase = GetEditNumber(data->hWndCharacter);
				int charStart = charBase; //initial
				int aggressiveness = GetTrackbarPosition(data->hWndAggressiveness);
				OBJ_BOUNDS *bounds = CellgenMakeCell(px, width, height, aggressiveness, boundType, affine, &nObj);

				//bounding box of image
				int xMin, xMax, yMin, yMax, centerX, centerY;
				CellgenGetBounds(px, width, height, &xMin, &xMax, &yMin, &yMax);
				if (boundType == 1) {
					//full image
					xMin = yMin = 0;
					xMax = width;
					yMax = height;
				}

				centerX = (xMin + xMax) / 2, centerY = (yMin + yMax) / 2;

				//add to offset depending on anchor positions
				switch (anchorX) {
					case 0: //left
						ofsX += (xMax - xMin) / 2; break;
					case 2: //right
						ofsX -= (xMax - xMin) / 2; break;
				}
				switch (anchorY) {
					case 0: //top
						ofsY += (yMax - yMin) / 2; break;
					case 2: //bottom
						ofsY -= (yMax - yMin) / 2; break;
				}

				//chunk the image
				OBJ_IMAGE_SLICE *slices = CellgenSliceImage(px, width, height, bounds, nObj, !affine);
				free(bounds);

				//get NCER, NCGR, NCLR
				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *npStruct = NpGetData(hWndMain);

				HWND hWndNclrViewer =  NULL, hWndNcgrViewer = NULL, hWndNcerViewer = NULL;
				GetAllEditors(hWndMain, FILE_TYPE_PALETTE, &hWndNclrViewer, 1);
				GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, &hWndNcgrViewer, 1);
				GetAllEditors(hWndMain, FILE_TYPE_CELL, &hWndNcerViewer, 1);

				//get editor datas
				NCLR *nclr = &((NCLRVIEWERDATA *) EditorGetData(hWndNclrViewer))->nclr;
				NCGR *ncgr = &((NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer))->ncgr;
				NCER *ncer = &((NCERVIEWERDATA *) EditorGetData(hWndNcerViewer))->ncer;

				//get current cell
				NCERVIEWERDATA *ncerViewerData = (NCERVIEWERDATA *) EditorGetData(hWndNcerViewer);
				int currentCellIndex = ncerViewerData->cell;
				NCER_CELL *cell = ncer->cells + currentCellIndex;

				//depending on generation mode, determine how many OBJ and where to put them
				int attrBase = 0, nOldAttribs = cell->nAttribs;
				switch (insertMode) {
					case 0: //replace
						attrBase = 0;
						cell->nAttribs = nObj;
						cell->attr = (uint16_t *) realloc(cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));
						break;
					case 1: //prepend
						attrBase = 0;
						cell->nAttribs += nObj;
						cell->attr = (uint16_t *) realloc(cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));
						memmove(cell->attr + nObj * 3, cell->attr, (cell->nAttribs - nObj) * (3 * sizeof(uint16_t))); //slide over
						break;
					case 2: //append
						attrBase = cell->nAttribs * 3;
						cell->nAttribs += nObj;
						cell->attr = (uint16_t *) realloc(cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));
						break;
				}

				//warn for excess OBJ
				if (cell->nAttribs > 128 && nOldAttribs <= 128) {
					MessageBox(hWnd, L"Cell generation results in >128 OBJ.", L"Too many OBJ", MB_ICONWARNING);
				}

				//OBJ VRAM granularity
				int granularity = ncgr->mappingMode;
				int charRShift = 0;
				switch (ncer->mappingMode) {
					case GX_OBJVRAMMODE_CHAR_1D_128K:
						granularity = 4; break;
					case GX_OBJVRAMMODE_CHAR_1D_64K:
						granularity = 2; break;
					case GX_OBJVRAMMODE_CHAR_1D_32K:
						granularity = 1; break;
				}

				//align starting character to granularity
				charStart = (charStart + granularity - 1) / granularity * granularity;

				//for 8-bit: since each character increment is 32*granularity bytes, 
				//fix the low bit 0. Do this by incrementing the left-shift.
				if (ncgr->nBits == 8) {
					charRShift++;
				}

				//set bounding box
				if (insertMode == 0) { //replace
					cell->minX = xMin - centerX + ofsX;
					cell->minY = yMin - centerY + ofsY;
					cell->maxX = xMax - centerX + ofsX;
					cell->maxY = yMax - centerY + ofsY;
				} else { //prepend or append
					cell->minX = min(xMin - centerX + ofsX, cell->minX);
					cell->minY = min(yMin - centerY + ofsY, cell->minY);
					cell->maxX = max(xMax - centerX + ofsX, cell->maxX);
					cell->maxY = max(yMax - centerY + ofsY, cell->maxY);
				}

				//sanity check palette setting
				int depth = ncgr->nBits;
				int paletteSize = (1 << depth);
				if (paletteLength == 0) {
					paletteLength = 1;
				}
				if (paletteLength > paletteSize) {
					paletteLength = paletteSize - paletteOffset;
				}

				//create palette
				COLOR32 *palette = (COLOR32 *) calloc(paletteSize, sizeof(COLOR32));

				if (writePalette) {
					//compute palette from pixels
					palette[0] = 0xFF00FF;
					RxCreatePalette(px, width, height, palette + paletteOffset, paletteLength);

					//write palette
					for (int i = paletteOffset; i < paletteOffset + paletteLength; i++) {
						nclr->colors[i + (paletteIndex << depth)] = ColorConvertToDS(palette[i]);
					}
				} else {
					for (int i = paletteOffset; i < paletteOffset + paletteLength; i++) {
						palette[i] = ColorConvertFromDS(nclr->colors[i + (paletteIndex << depth)]);
					}
				}

				//fill out character
				int *indicesBuffer = (int *) calloc(64 * 64, sizeof(int));
				unsigned char *indicesBuffer8 = (unsigned char *) calloc(64 * 64, sizeof(unsigned char));
				for (int i = 0; i < nObj; i++) {
					OBJ_IMAGE_SLICE *slice = slices + i;
					int width = slice->bounds.width, height = slice->bounds.height;
					int nChars = slice->bounds.width * slice->bounds.height / 8 / 8;

					RxReduceImageEx(slice->px, indicesBuffer, width, height, palette + paletteOffset, paletteLength, 
						1, 1, 0, diffuse, balance, colorBalance, enhanceColors);

					//convert to character array in indicesBuffer8
					for (int j = 0; j < nChars; j++) {
						unsigned char *ch = indicesBuffer8 + j * 64;
						int objX = (j * 8) % slice->bounds.width;
						int objY = (j * 8) / slice->bounds.width * 8;

						for (int y = 0; y < 8; y++) {
							for (int x = 0; x < 8; x++) {
								int index = indicesBuffer[objX + x + (objY + y) * slice->bounds.width] + paletteOffset;
								if ((slice->px[objX + x + (objY + y) * slice->bounds.width] >> 24) < 128) index = 0;
								ch[x + y * 8] = index;
							}
						}
					}

					//search chars for a match
					int foundStart = charBase, nFoundChars = 0;
					for (int j = charStart; optimize && j < charBase; j += granularity) {
						int nCharsCompare = nChars;
						if (j + nCharsCompare > charBase) nCharsCompare = charBase - j;

						//compare nCharsCompare chars
						int differed = 0;
						for (int k = 0; k < nCharsCompare; k++) {
							if (memcmp(ncgr->tiles[j + k], indicesBuffer8 + k * 64, 64) != 0) {
								differed = 1;
								break;
							}
						}

						//if differed, then no match
						//if !differed, we matched nCharsCompare characters
						if (!differed) {
							foundStart = j;
							nFoundChars = nCharsCompare;
							break;
						}
					}

					//enough space?
					int nCharsAdd = nChars - nFoundChars;
					if (charBase + nCharsAdd > ncgr->nTiles) {
						MessageBox(hWnd, L"Not enough graphics space.", L"Out of space.", MB_ICONERROR);
						break;
					}

					//read out character
					for (int j = nFoundChars; j < nChars; j++) {
						unsigned char *ch = ncgr->tiles[foundStart + j];

						memcpy(ch, indicesBuffer8 + 64 * j, 64);
					}

					//get shape/size
					int shape = 0, size = 0;
					if (width == height) {
						shape = 0; //square

						if (width == 8) size = 0; //8
						else if (width == 16) size = 1; //16
						else if (width == 32) size = 2; //32
						else if (width == 64) size = 3; //64
					} else if (width > height) {
						shape = 1; //wide

						if (width == 16) size = 0; //16x8
						else if (height == 8) size = 1; //32x8
						else if (width == 32) size = 2; //32x16
						else if (width == 64) size = 3; //64x32
					} else if (width < height) {
						shape = 2; //tall

						if (height == 16) size = 0; //8x16
						else if (width == 8) size = 1; //8x32
						else if (height == 32) size = 2; //16x32
						else if (height == 64) size = 3; //32x64
					}

					slice->bounds.x -= centerX;
					slice->bounds.y -= centerY;
					if (affine) {
						slice->bounds.x -= slice->bounds.width / 2;
						slice->bounds.y -= slice->bounds.height / 2;
					}

					//offset position
					slice->bounds.x += ofsX;
					slice->bounds.y += ofsY;

					//add OBJ
					int charName = (foundStart / granularity) << charRShift;
					cell->attr[attrBase + i * 3 + 0] = (slice->bounds.y & 0x0FF) | (affine << 8) | (affine << 9) | ((depth == 8) << 13) | (shape << 14);
					cell->attr[attrBase + i * 3 + 1] = (slice->bounds.x & 0x1FF) | ((affineIdx & 0x1F) << 9) | (size << 14);
					cell->attr[attrBase + i * 3 + 2] = (paletteIndex << 12) | (charName) | (prio << 10);

					//increment
					charBase += nCharsAdd;
					charBase = (charBase + granularity - 1) / granularity * granularity;
				}
				free(indicesBuffer8);
				free(indicesBuffer);

				free(palette);

				free(slices);

				//import complete, update UIs
				InvalidateAllEditors(hWndMain, FILE_TYPE_PALETTE);
				InvalidateAllEditors(hWndMain, FILE_TYPE_CHARACTER);
				
				if (hWndNcerViewer != NULL) {
					NCERVIEWERDATA *ncerViewerData = (NCERVIEWERDATA *) EditorGetData(hWndNcerViewer);
					CellViewerGraphicsUpdated(ncerViewerData->hWnd);
					CellViewerUpdateCellSubItemText(ncerViewerData);

					//select imported graphics
					CellViewerSelectRange(ncerViewerData, attrBase, nObj);

					InvalidateRect(ncerViewerData->hWndViewer, NULL, FALSE);
				}

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if (notif == BN_CLICKED && (hWndControl == data->hWndCancel || idc == IDCANCEL)) {
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_DESTROY:
			free(data);
			SetWindowLongPtr(hWnd, 3 * sizeof(LONG_PTR), 0);
			break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}


// ----- preview code

static void CellViewerPutPixel(FrameBuffer *fb, int x, int y, COLOR32 col) {
	if (x < 0 || x >= fb->width) return;
	if (y < 0 || y >= fb->height) return;
	
	fb->px[x + y * fb->width] = col;
}

static void CellViewerRenderHighContrastPixel(FrameBuffer *fb, int x, int y, int chno) {
	if (x < 0 || x >= fb->width) return;
	if (y < 0 || y >= fb->height) return;

	COLOR32 *cdest = &fb->px[x + y * fb->width];
	if ((*cdest >> 24) == 0) {
		unsigned int val = (*cdest >> (8 * chno)) & 0xFF;
		if (val < 0x80) val = 0xFF;
		else val = 0x00;

		*cdest = (*cdest & ~(0xFF << (8 * chno))) | (val << (8 * chno));
		*cdest |= 0xFF000000;
	}
}

static void CellViewerRenderDottedLineH(FrameBuffer *fb, int y, int ch, int dotted) {
	for (int x = 0; x < fb->width; x++) {
		if (((x ^ y) & 1) || !dotted) CellViewerRenderHighContrastPixel(fb, x, y, ch);
	}
}

static void CellViewerRenderDottedLineV(FrameBuffer *fb, int x, int ch, int dotted) {
	for (int y = 0; y < fb->height; y++) {
		if (((x ^ y) & 1) || !dotted) CellViewerRenderHighContrastPixel(fb, x, y, ch);
	}
}

static void CellViewerRenderSolidLineH(FrameBuffer *fb, int y, int width, COLOR32 c) {
	if (y < 0 || y >= fb->height) return;
	for (int x = 0; x < fb->width && x < width; x++) {
		fb->px[x + y * fb->width] = REVERSE(c);
	}
}

static void CellViewerRenderSolidLineV(FrameBuffer *fb, int x, int height, COLOR32 c) {
	if (x < 0 || x >= fb->width) return;
	for (int y = 0; y < fb->height && y < height; y++) {
		fb->px[x + y * fb->width] = REVERSE(c);
	}
}

static void CellViewerRenderInvertPixel(FrameBuffer *fb, int x, int y) {
	if (x < 0 || x >= fb->width) return;
	if (y < 0 || y >= fb->height) return;

	COLOR32 c = fb->px[x + y * fb->width];
	if ((c >> 24) != 0x80) {
		fb->px[x + y * fb->width] = ((c ^ 0xFFFFFF) & 0xFFFFFF) | 0x80000000;
	}
}

static void CellViewerRenderDottedRect(FrameBuffer *fb, int x, int y, int w, int h) {
	for (int pxX = x; pxX < (x + w); pxX++) {
		if (pxX >= 0 && pxX < fb->width) {
			CellViewerRenderInvertPixel(fb, pxX, y);
			CellViewerRenderInvertPixel(fb, pxX, y + h - 1);
		}
	}
	for (int pxY = y + 1; pxY < (y + h - 1); pxY++) {
		if (pxY >= 0 && pxY < fb->height) {
			CellViewerRenderInvertPixel(fb, x, pxY);
			CellViewerRenderInvertPixel(fb, x + w - 1, pxY);
		}
	}
}

static void CellViewerRenderSolidCircle(FrameBuffer *fb, int cx, int cy, int cr, COLOR32 col) {
	int r2 = cr * cr;
	col = REVERSE(col);

	//use midpoint circle algorithm
	int nStep = (int) ceil(((float) cr) * 0.7071f);
	for (int x = 0; x < nStep; x++) {
		//compute intersection
		int y = (int) (sqrt(r2 - x * x) + 0.5f);
		CellViewerPutPixel(fb, cx + x, cy + y, col);
		CellViewerPutPixel(fb, cx - x, cy + y, col);
		CellViewerPutPixel(fb, cx + x, cy - y, col);
		CellViewerPutPixel(fb, cx - x, cy - y, col);
		CellViewerPutPixel(fb, cx + y, cy + x, col);
		CellViewerPutPixel(fb, cx - y, cy + x, col);
		CellViewerPutPixel(fb, cx + y, cy - x, col);
		CellViewerPutPixel(fb, cx - y, cy - x, col);
	}
}

static void CellViewerPreviewOnPaint(NCERVIEWERDATA *data) {
	PAINTSTRUCT ps;
	HWND hWnd = data->hWndViewer;
	HDC hDC = BeginPaint(hWnd, &ps);

	NCLR *nclr = CellViewerGetAssociatedPalette(data);

	NCER_CELL *cell = NULL;
	if (data->cell >= 0 && data->cell < data->ncer.nCells) {
		cell = &data->ncer.cells[data->cell];
	}

	//ensure framebuffer size
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	FbSetSize(&data->fb, rcClient.right, rcClient.bottom);

	int scrollX = 0, scrollY = 0;
	CellViewerPreviewGetScroll(data, &scrollX, &scrollY);

	int viewWidth = 512 * data->scale - scrollX;
	int viewHeight = 256 * data->scale - scrollY;
	if (viewWidth > rcClient.right) viewWidth = rcClient.right;
	if (viewHeight > rcClient.bottom) viewHeight = rcClient.bottom;

	int nSelectedOBJ = 0;
	int *selectedOBJ = NULL;
	if (CellViewerIsMakingSelection(data) || CellViewerHasSelection(data)) {
		selectedOBJ = CellViewerGetSelectedOamObjects(data, &nSelectedOBJ);
	}

	//render graphics
	if (data->cell < data->ncer.nCells) {
		for (int y = 0; y < rcClient.bottom; y++) {
			for (int x = 0; x < rcClient.right; x++) {
				int srcX = (x + scrollX) / data->scale, srcY = (y + scrollY) / data->scale;

				//sample coordinate
				COLOR32 sample = 0xFFF0F0F0;
				if (srcX < 512 && srcY < 256) {
					sample = data->frameBuffer[srcX + srcY * 512];
					sample = REVERSE(sample); // internal framebuffer is reversed color order
				}

				if ((sample >> 24) == 0) {
					if (g_configuration.renderTransparent) {
						//render transparent checkerboard
						COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };
						sample = checker[((x ^ y) >> 2) & 1];
					} else {
						//render backdrop color
						if (nclr != NULL && nclr->nColors > 0) sample = ColorConvertFromDS(nclr->colors[0]);
						else sample = 0;
					}
				}
				data->fb.px[x + y * data->fb.width] = REVERSE(sample);
			}
		}
	}

	//render guidelines
	if (data->showBorders) {
		//main borders: red (X=256, Y=128)
		CellViewerRenderDottedLineV(&data->fb, 256 * data->scale - scrollX, 0, 1);
		CellViewerRenderDottedLineH(&data->fb, 128 * data->scale - scrollY, 0, 1);

		//secondary (screen) borders: (X=128, X=384, Y=64, Y=192)
		CellViewerRenderDottedLineV(&data->fb, 128 * data->scale - scrollX, 1, 1);
		CellViewerRenderDottedLineV(&data->fb, 384 * data->scale - scrollX, 1, 1);
		CellViewerRenderDottedLineH(&data->fb,  64 * data->scale - scrollY, 1, 1);
		CellViewerRenderDottedLineH(&data->fb, 192 * data->scale - scrollY, 1, 1);

		//tertiary lines (16x16 boundaries)
		for (int y = 0; y < 256; y += 16) {
			CellViewerRenderDottedLineH(&data->fb, y * data->scale - scrollY, 2, 1);
		}
		for (int x = 0; x < 512; x += 16) {
			CellViewerRenderDottedLineV(&data->fb, x * data->scale - scrollX, 2, 1);
		}
	}

	//show bounding box if available
	if (data->showCellBounds && cell != NULL && !(cell->minX == cell->maxX && cell->minY == cell->maxY)) {
		CellViewerRenderSolidLineV(&data->fb, (cell->minX + 0 + 256) * data->scale - scrollX - 0, viewHeight, 0xFFFF00);
		CellViewerRenderSolidLineH(&data->fb, (cell->minY + 0 + 128) * data->scale - scrollY - 0, viewWidth,  0xFFFF00);
		CellViewerRenderSolidLineV(&data->fb, (cell->maxX + 1 + 256) * data->scale - scrollX - 1, viewHeight, 0xFFFF00);
		CellViewerRenderSolidLineH(&data->fb, (cell->maxY + 1 + 128) * data->scale - scrollY - 1, viewWidth,  0xFFFF00);

		//get mid point
		int midX = (cell->minX + cell->maxX) / 2;
		int midY = (cell->minY + cell->maxY) / 2;
		CellViewerRenderSolidCircle(&data->fb, (midX + 256) * data->scale - scrollX, (midY + 128) * data->scale - scrollY, 
			((cell->cellAttr & 0x3F) << 2) * data->scale, 0x00FFFF);
	}

	//draw bounding boxes for selected OAM objects, but hide the borders and marking when dragging a selection
	if (nSelectedOBJ > 0 && !(data->mouseDown && data->mouseDownHit == CV_HIT_SELECTION && data->selMoved)) {
		//mark selected OBJ (highlight white)
		for (int y = 0; y < viewHeight; y++) {
			for (int x = 0; x < viewWidth; x++) {
				int pxX = (x + scrollX) / data->scale;
				int pxY = (y + scrollY) / data->scale;

				int objno = data->covBuffer[pxX + pxY * 512] - 1;
				if (objno != -1 && ((x ^ y) & 1)) {
					int doMark = 0;
					for (int i = 0; i < nSelectedOBJ; i++) {
						if (selectedOBJ[i] == objno) {
							doMark = 1;
							break;
						}
					}
					if (doMark) data->fb.px[x + y * data->fb.width] = 0xFFFFFFFF;
				}
			}
		}

		//highlight OBJ in selection
		for (int i = 0; i < nSelectedOBJ; i++) {
			NCER_CELL_INFO info;
			CellDecodeOamAttributes(&info, &data->ncer.cells[data->cell], selectedOBJ[i]);

			int objX = SEXT9(info.x) + 256, objY = SEXT8(info.y) + 128;
			int objW = info.width, objH = info.height;
			if (info.doubleSize) {
				objW *= 2;
				objH *= 2;
			}

			CellViewerRenderDottedRect(&data->fb, objX * data->scale - scrollX, objY * data->scale - scrollY,
				objW * data->scale, objH * data->scale);
		}
	}

	//render selection
	if (CellViewerIsMakingSelection(data)) {
		int selX, selY, selW, selH;
		CellViewerGetDragBounds(data, &selX, &selY, &selW, &selH);

		selX *= data->scale;
		selY *= data->scale;
		selW *= data->scale;
		selH *= data->scale;
		CellViewerRenderDottedRect(&data->fb, selX - scrollX, selY - scrollY, selW, selH);
	}

	if (selectedOBJ != NULL) {
		free(selectedOBJ);
	}

	//finalize
	FbDraw(&data->fb, hDC, 0, 0, rcClient.right, rcClient.bottom, 0, 0);
	EndPaint(hWnd, &ps);
}

static void CellViewerPreviewOnRecalculate(NCERVIEWERDATA *data) {
	int contentWidth = 512 * data->scale, contentHeight = 256 * data->scale;

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

static void CellViewerOnLButtonDown(NCERVIEWERDATA *data) {
	SetFocus(data->hWndViewer);
	SetCapture(data->hWndViewer);
	data->mouseDown = 1;
	data->selMoved = 0;

	int scrollX, scrollY;
	CellViewerPreviewGetScroll(data, &scrollX, &scrollY);
	
	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(data->hWndViewer, &pt);
	data->mouseDownHit = CellViewerHitTest(data, pt.x, pt.y);

	switch (data->mouseDownHit) {
		case CV_HIT_BACKGROUND:
		{
			//discard selection
			CellViewerDeselect(data);

			//hit background: start selection
			data->dragStartX = (pt.x + scrollX) / data->scale;
			data->dragStartY = (pt.y + scrollY) / data->scale;
			data->dragEndX = data->dragStartX;
			data->dragEndY = data->dragStartY;
			InvalidateRect(data->hWndViewer, NULL, FALSE);
			break;
		}
		case CV_HIT_NOWHERE:
			//do nothing
			break;
		case CV_HIT_SELECTION:
		default: // >= CV_HIT_OBJ
		{
			if (data->mouseDownHit >= CV_HIT_OBJ) {
				//Ctrl key pressed?
				if (GetKeyState(VK_CONTROL) < 0) {
					//add this OBJ to selection
					CellViewerAddObjToSelection(data, data->mouseDownHit - CV_HIT_OBJ);
				} else {
					//select only the OBJ
					CellViewerSelectSingleOBJ(data, data->mouseDownHit - CV_HIT_OBJ);
				}
			}

			//start dragging selection
			data->dragStartX = (pt.x + scrollX) / data->scale;
			data->dragStartY = (pt.y + scrollY) / data->scale;
			data->dragEndX = data->dragStartX;
			data->dragEndY = data->dragStartY;
			data->mouseDownHit = CV_HIT_SELECTION;
			InvalidateRect(data->hWndViewer, NULL, FALSE);
			break;
		}
	}
}

static void CellViewerOnLButtonUp(NCERVIEWERDATA *data) {
	ReleaseCapture();

	if (data->mouseDown) {
		switch (data->mouseDownHit) {
			case CV_HIT_BACKGROUND:
			{
				CellViewerCommitSelection(data);

				//free selection
				data->dragStartX = data->dragEndX = -1;
				data->dragStartY = data->dragEndY = -1;
				InvalidateRect(data->hWndViewer, NULL, FALSE);
				break;
			}
			case CV_HIT_NOWHERE:
				//do nothing
				break;
		}
		InvalidateRect(data->hWndViewer, NULL, FALSE);
	}

	data->mouseDown = 0;
	data->mouseDownHit = CV_HIT_NOWHERE;
	data->dragStartX = -1;
	data->dragStartY = -1;
}

static void CellViewerOnMouseMove(NCERVIEWERDATA *data) {
	int scrollX, scrollY;
	CellViewerPreviewGetScroll(data, &scrollX, &scrollY);

	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(data->hWndViewer, &pt);

	if (data->mouseDown) {

		switch (data->mouseDownHit) {
			case CV_HIT_BACKGROUND:
			{
				//continue selection
				data->dragEndX = (pt.x + scrollX) / data->scale;
				data->dragEndY = (pt.y + scrollY) / data->scale;
				InvalidateRect(data->hWndViewer, NULL, FALSE);
				break;
			}
			case CV_HIT_NOWHERE:
				//do nothing
				break;
			case CV_HIT_SELECTION:
			{
				//compute move
				int curX = (pt.x + scrollX) / data->scale;
				int curY = (pt.y + scrollY) / data->scale;

				int dx = curX - data->dragEndX;
				int dy = curY - data->dragEndY;
				if (dx != 0 || dy != 0) {
					//update selection movement
					CellViewerMoveSelection(data, dx, dy);
					if (data->autoCalcBounds) CellViewerUpdateBounds(data);
					CellViewerUpdateCellRender(data);
					CellViewerGraphicsUpdated(data->hWnd);
					data->selMoved = 1;
				}

				data->dragEndX = curX;
				data->dragEndY = curY;
				InvalidateRect(data->hWndViewer, NULL, FALSE);

				break;
			}
		}
	}

	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = data->hWndViewer;
	TrackMouseEvent(&tme);
}

static void CellViewerOnKeyDown(NCERVIEWERDATA *data, int cc) {
	switch (cc) {
		case VK_LEFT:
			CellViewerMoveSelection(data, -1, 0);
			if (data->autoCalcBounds) CellViewerUpdateBounds(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case VK_RIGHT:
			CellViewerMoveSelection(data, 1, 0);
			if (data->autoCalcBounds) CellViewerUpdateBounds(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case VK_UP:
			CellViewerMoveSelection(data, 0, -1);
			if (data->autoCalcBounds) CellViewerUpdateBounds(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case VK_DOWN:
			CellViewerMoveSelection(data, 0, 1);
			if (data->autoCalcBounds) CellViewerUpdateBounds(data);
			CellViewerGraphicsUpdated(data->hWnd);
			break;
		case VK_DELETE:
			CellViewerDeleteSelection(data);
			if (data->autoCalcBounds) CellViewerUpdateBounds(data);
			CellViewerGraphicsUpdated(data->hWnd);
			CellViewerUpdateCellSubItemText(data);
			break;
		case VK_ESCAPE:
			CellViewerDeselect(data);
			InvalidateRect(data->hWndViewer, NULL, FALSE);
			break;
	}
}

static void CellViewerOnRButtonDown(NCERVIEWERDATA *data) {
	HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 5);

	//init menu state
	int nSel;
	int *sel = CellViewerGetSelectedOamObjects(data, &nSel);

	NCER_CELL *cell = CellViewerGetCurrentCell(data);

	if (nSel == 0 || cell == NULL) {
		//no selection: disable all OBJ options, and cut/copy
		EnableMenuItem(hPopup, ID_CELLMENU_CUT, MF_DISABLED);
		EnableMenuItem(hPopup, ID_CELLMENU_COPY, MF_DISABLED);
		EnableMenuItem(hPopup, ID_CELLMENU_SENDTOFRONT, MF_DISABLED);
		EnableMenuItem(hPopup, ID_CELLMENU_SENDTOBACK, MF_DISABLED);

		EnableMenuItem(hPopup, ID_CELLMENU_AFFINE, MF_DISABLED);
		EnableMenuItem(hPopup, ID_CELLMENU_DOUBLESIZE, MF_DISABLED);
		EnableMenuItem(hPopup, ID_CELLMENU_HFLIP, MF_DISABLED);
		EnableMenuItem(hPopup, ID_CELLMENU_VFLIP, MF_DISABLED);

		for (int i = 0; i < 16; i++) EnableMenuItem(hPopup, sMenuIdPalettes[i], MF_DISABLED);
		for (int i = 0; i < 12; i++) EnableMenuItem(hPopup, sMenuIdSizes[i], MF_DISABLED);
		for (int i = 0; i < 4; i++) EnableMenuItem(hPopup, sMenuIdPrios[i], MF_DISABLED);
		for (int i = 0; i < 4; i++) EnableMenuItem(hPopup, sMenuIdTypes[i], MF_DISABLED);
	} else {
		//has selection: items Cut and Copy are enabled by default.
		int allAffine = 1, allNotAffine = 1, commonPalette = 0, commonShape = 0, commonSize = 0, commonPrio = 0, commonType = 0, commonDoubleSize = 1;
		for (int i = 0; i < nSel; i++) {
			NCER_CELL_INFO info;
			CellDecodeOamAttributes(&info, cell, sel[i]);

			if (i == 0) {
				commonPalette = info.palette;
				commonShape = info.shape;
				commonSize = info.size;
				commonPrio = info.priority;
				commonType = info.mode;
				commonDoubleSize = info.doubleSize;
			} else {
				//check each attribute against the current "common" attribute values
				if (commonPalette != info.palette) commonPalette = -1;
				if (commonShape != info.shape) commonShape = -1;
				if (commonSize != info.size) commonSize = -1;
				if (commonPrio != info.priority) commonPrio = -1;
				if (commonType != info.mode) commonType = -1;
				if (!info.doubleSize) commonDoubleSize = 0;
			}

			allAffine = allAffine && info.rotateScale;
			allNotAffine = allNotAffine && !info.rotateScale;
		}

		//if all OBJ are affine, disable H/V flip options.
		if (allAffine) {
			EnableMenuItem(hPopup, ID_CELLMENU_HFLIP, MF_DISABLED);
			EnableMenuItem(hPopup, ID_CELLMENU_VFLIP, MF_DISABLED);
		}

		//if all OBJ are not affine, disable double size.
		if (allNotAffine) {
			EnableMenuItem(hPopup, ID_CELLMENU_DOUBLESIZE, MF_DISABLED);
		}

		//set common attributes
		if (commonPalette != -1) CheckMenuItem(hPopup, sMenuIdPalettes[commonPalette], MF_CHECKED);
		if (commonShape != -1 && commonSize != -1) CheckMenuItem(hPopup, sMenuIdSizes[commonShape * 4 + commonSize], MF_CHECKED);
		if (commonPrio != -1) CheckMenuItem(hPopup, sMenuIdPrios[commonPrio], MF_CHECKED);
		if (commonType != -1) CheckMenuItem(hPopup, sMenuIdTypes[commonType], MF_CHECKED);
		if (commonDoubleSize) CheckMenuItem(hPopup, ID_CELLMENU_DOUBLESIZE, MF_CHECKED);

	}
	free(sel);

	POINT mouse;
	GetCursorPos(&mouse);
	TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, data->hWnd, NULL);
	DeleteObject(hPopup);
}

static LRESULT CALLBACK CellViewerPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWndEditor);
	if (GetWindowLongPtr(hWnd, 0) == (LONG_PTR) NULL) {
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}

	if (data != NULL) {
		data->frameData.contentWidth = 512 * data->scale;
		data->frameData.contentHeight = 256 * data->scale;
	}

	switch (msg) {
		case WM_PAINT:
			CellViewerPreviewOnPaint(data);
			break;
		case WM_ERASEBKGND:
			return 1;
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			return DefChildProc(hWnd, msg, wParam, lParam);
		case NV_RECALCULATE:
			CellViewerPreviewOnRecalculate(data);
			break;
		case WM_LBUTTONDOWN:
			CellViewerOnLButtonDown(data);
			break;
		case WM_MOUSEMOVE:
			CellViewerOnMouseMove(data);
			break;
		case WM_LBUTTONUP:
			CellViewerOnLButtonUp(data);
			break;
		case WM_RBUTTONDOWN:
			CellViewerOnRButtonDown(data);
			break;
		case WM_KEYDOWN:
			CellViewerOnKeyDown(data, wParam);
			break;
		case WM_SIZE:
		{
			UpdateScrollbarVisibility(hWnd);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = data->frameData.contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterNcerViewerClass(void) {
	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EditorRegister(L"NcerViewerClass", CellViewerWndProc, L"Cell Editor", sizeof(NCERVIEWERDATA), features);
	RegisterGenericClass(L"NcerCreateCellClass", NcerCreateCellWndProc, 12 * sizeof(void *));
	RegisterGenericClass(L"CellPreviewClass", CellViewerPreviewWndProc, sizeof(void *));
}



// ----- public API

void CellViewerGraphicsUpdated(HWND hWndEditor) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWndEditor);

	//mark graphics update
	KillTimer(hWndEditor, ID_TIMER_GFX_UPDATE);
	data->foreignDataUpdate = 1;
	SetTimer(hWndEditor, ID_TIMER_GFX_UPDATE, GFX_UPDATE_TIMER, NULL);

	//invalidate editor
	CellViewerUpdateCellRender(data);
	InvalidateRect(data->hWndViewer, NULL, FALSE);
}

void CellViewerCopyObjData(NP_OBJ *obj) {
	int fmt = CellviewerGetObjClipboardFormat();

	HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(NP_OBJ) + obj->nOBJ * 6);
	NP_OBJ *clip = (NP_OBJ *) GlobalLock(hGlobal);
	memcpy(clip, obj, sizeof(NP_OBJ) + obj->nOBJ * 6);

	GlobalUnlock(hGlobal);
	SetClipboardData(fmt, hGlobal);
}

NP_OBJ *CellViewerGetCopiedObjData(void) {
	int fmt = CellviewerGetObjClipboardFormat();
	if (!fmt) return NULL;

	HGLOBAL hGlobal = GetClipboardData(fmt);
	if (hGlobal == NULL) {
		return NULL;
	}
	NP_OBJ *clip = (NP_OBJ *) GlobalLock(hGlobal);

	NP_OBJ *cpy = (NP_OBJ *) calloc(sizeof(NP_OBJ) + clip->nOBJ * 6, 1);
	memcpy(cpy, clip, sizeof(NP_OBJ) + clip->nOBJ * 6);

	GlobalUnlock(hGlobal);
	return cpy;
}


HWND CreateNcerViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NCER ncer;
	if (CellReadFile(&ncer, path)) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		if (width < 150) width = 150;
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4;
	}

	HWND hWnd = EditorCreate(L"NcerViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncer);
	if (ncer.header.format == NCER_TYPE_HUDSON) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}

HWND CreateNcerViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCER *ncer) {
	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		if (width < 150) width = 150;
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4;
	}

	HWND hWnd = EditorCreate(L"NcerViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) ncer);
	if (ncer->header.format == NCER_TYPE_HUDSON) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}
