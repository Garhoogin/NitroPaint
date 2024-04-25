#include "editor.h"
#include "ncerviewer.h"
#include "nitropaint.h"
#include "ncgr.h"
#include "nclr.h"
#include "tiler.h"
#include "resource.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "palette.h"
#include "gdip.h"
#include "preview.h"

#include "cellgen.h"

extern HICON g_appIcon;

VOID PaintNcerViewer(HWND hWnd) {
	PAINTSTRUCT ps;
	HDC hWindowDC = BeginPaint(hWnd, &ps);
	
	NCGR *ncgr = NULL;
	NCLR *nclr = NULL;
	HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	if (nitroPaintStruct->hWndNclrViewer) {
		NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(nitroPaintStruct->hWndNclrViewer, 0);
		nclr = &nclrViewerData->nclr;
	}
	if (nitroPaintStruct->hWndNcgrViewer) {
		NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(nitroPaintStruct->hWndNcgrViewer, 0);
		ncgr = &ncgrViewerData->ncgr;
	}
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	
	NCER_CELL *cell = data->ncer.cells + data->cell;
	NCER_CELL_INFO info;
	CellDecodeOamAttributes(&info, cell, data->oam);

	memset(data->frameBuffer, 0, sizeof(data->frameBuffer));
	CHAR_VRAM_TRANSFER *transferEntry = NULL;
	if (data->ncer.vramTransfer != NULL)
		transferEntry = data->ncer.vramTransfer + data->cell;
	DWORD *bits = CellRenderCell(data->frameBuffer, data->ncer.cells + data->cell, data->ncer.mappingMode, ncgr, nclr, transferEntry, 
		256, 128, g_configuration.renderTransparent, data->showObjOutline ? data->oam : -1, 1.0f, 0.0f, 0.0f, 1.0f);

	//draw lines if needed
	if (data->showCellBounds) {
		int minX = cell->minX + 256, maxX = cell->maxX + 256 - 1;
		int minY = cell->minY + 128, maxY = cell->maxY + 128 - 1;
		minX &= 0x1FF, maxX &= 0x1FF, minY &= 0xFF, maxY &= 0xFF;

		for (int i = 0; i < 256; i++) {
			if (bits[i * 512 + minX] >> 24 != 0xFE) bits[i * 512 + minX] = 0xFF0000FF;
			if (bits[i * 512 + maxX] >> 24 != 0xFE) bits[i * 512 + maxX] = 0xFF0000FF;
		}
		for (int i = 0; i < 512; i++) {
			if (bits[minY * 512 + i] >> 24 != 0xFE) bits[minY * 512 + i] = 0xFF0000FF;
			if (bits[maxY * 512 + i] >> 24 != 0xFE) bits[maxY * 512 + i] = 0xFF0000FF;
		}
	}

	//draw solid color background if transparency disabled
	if (!g_configuration.renderTransparent) {
		COLOR32 bgColor = 0;
		if (nclr!= NULL) bgColor = ColorConvertFromDS(nclr->colors[0]);
		bgColor = REVERSE(bgColor);
		for (int i = 0; i < 256 * 512; i++) {
			COLOR32 c = bits[i];
			if ((c >> 24) == 0) bits[i] = bgColor;
			else if ((c >> 24) == 0xFE) bits[i] = ((bgColor + 0x808080) & 0xFFFFFF) | 0xFE000000;
		}
	}

	//draw editor guidelines if enabled
	if (data->showGuidelines) {
		//dotted lines at X=0 an Y=0
		COLOR32 centerColor = 0xFF0000; //red
		COLOR32 auxColor = 0x00FF00; //green
		COLOR32 minorColor = 0x002F00;

		for (int i = 0; i < 512; i++) {
			//major guideline
			COLOR32 c = bits[i + 128 * 512];
			if ((c >> 24) != 0xFE) if (i & 1) bits[i + 128 * 512] ^= centerColor;

			//auxiliary guidelines
			c = bits[i + 64 * 512];
			if ((c >> 24) != 0xFE) if (i & 1) bits[i + 64 * 512] ^= auxColor;
			c = bits[i + 192 * 512];
			if ((c >> 24) != 0xFE) if (i & 1) bits[i + 192 * 512] ^= auxColor;

			//minor guidelines
			for (int j = 0; j < 256; j += 8) {
				if (j == 64 || j == 128 || j == 192) continue;

				c = bits[i + j * 512];
				if ((c >> 24) != 0xFE) if (i & 1) bits[i + j * 512] ^= minorColor;
			}
		}
		for (int i = 0; i < 256; i++) {
			//major guideline
			COLOR32 c = bits[256 + i * 512];
			if ((c >> 24) != 0xFE) if (i & 1) bits[256 + i * 512] ^= centerColor;

			//auxiliary guidelines
			c = bits[128 + i * 512];
			if ((c >> 24) != 0xFE) if (i & 1) bits[128 + i * 512] ^= auxColor;
			c = bits[384 + i * 512];
			if ((c >> 24) != 0xFE) if (i & 1) bits[384 + i * 512] ^= auxColor;

			//minor guidelines
			for (int j = 0; j < 512; j += 8) {
				if (j == 128 || j == 256 || j == 384) continue;

				c = bits[j + i * 512];
				if ((c >> 24) != 0xFE) if (i & 1) bits[j + i * 512] ^= minorColor;
			}
		}
	}

	HBITMAP hbm = CreateBitmap(512, 256, 1, 32, bits);
	HDC hCompatibleDC = CreateCompatibleDC(hWindowDC);
	SelectObject(hCompatibleDC, hbm);
	BitBlt(hWindowDC, 0, 0, 512, 256, hCompatibleDC, 0, 0, SRCCOPY);
	DeleteObject(hbm);

	int width, height;
	bits = (COLOR32 *) calloc(info.width * info.height, sizeof(COLOR32));
	CellRenderObj(&info, data->ncer.mappingMode, ncgr, nclr, NULL, bits, &width, &height, 1);
	hbm = CreateBitmap(width, height, 1, 32, bits);
	SelectObject(hCompatibleDC, hbm);
	BitBlt(hWindowDC, 512 - 69, 256 + 5, width, height, hCompatibleDC, 0, 0, SRCCOPY);
	DeleteObject(hbm);
	free(bits);

	DeleteObject(hCompatibleDC);

	EndPaint(hWnd, &ps);
}

void UpdateEnabledControls(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);

	NCER_CELL *cell = data->ncer.cells + data->cell;
	NCER_CELL_INFO info;
	CellDecodeOamAttributes(&info, cell, data->oam);

	//if rotate/scale, disable HV Flip and Disable, enable matrix.
	if (info.rotateScale) {
		setStyle(data->hWndHFlip, TRUE, WS_DISABLED);
		setStyle(data->hWndVFlip, TRUE, WS_DISABLED);
		setStyle(data->hWndDisable, TRUE, WS_DISABLED);
		setStyle(data->hWndMatrix, FALSE, WS_DISABLED);
		setStyle(data->hWndDoubleSize, FALSE, WS_DISABLED);
	} else {
		setStyle(data->hWndHFlip, FALSE, WS_DISABLED);
		setStyle(data->hWndVFlip, FALSE, WS_DISABLED);
		setStyle(data->hWndDisable, FALSE, WS_DISABLED);
		setStyle(data->hWndMatrix, TRUE, WS_DISABLED);
		setStyle(data->hWndDoubleSize, TRUE, WS_DISABLED);
	}
	RedrawWindow(hWnd, NULL, NULL, RDW_ALLCHILDREN | RDW_INVALIDATE);
}

void UpdateControls(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);

	NCER_CELL *cell = data->ncer.cells + data->cell;
	NCER_CELL_INFO info;
	CellDecodeOamAttributes(&info, cell, data->oam);

	if (!data->mouseDown) {
		SendMessage(data->hWnd8bpp, BM_SETCHECK, info.characterBits == 8, 0);
		SendMessage(data->hWndRotateScale, BM_SETCHECK, info.rotateScale, 0);
		SendMessage(data->hWndHFlip, BM_SETCHECK, info.flipX, 0);
		SendMessage(data->hWndVFlip, BM_SETCHECK, info.flipY, 0);
		SendMessage(data->hWndMosaic, BM_SETCHECK, info.mosaic, 0);
		SendMessage(data->hWndDisable, BM_SETCHECK, info.disable, 0);
		SendMessage(data->hWndPaletteDropdown, CB_SETCURSEL, info.palette, 0);
		SendMessage(data->hWndDoubleSize, BM_SETCHECK, info.doubleSize, 0);
		SendMessage(data->hWndPriority, CB_SETCURSEL, info.priority, 0);
		SendMessage(data->hWndType, CB_SETCURSEL, info.mode, 0);

		SetEditNumber(data->hWndMatrix, info.matrix);
		SetEditNumber(data->hWndCharacterOffset, info.characterName);
	}
	SetEditNumber(data->hWndXInput, info.x >= 256 ? (info.x - 512) : info.x);
	SetEditNumber(data->hWndYInput, info.y >= 128 ? (info.y - 256) : info.y);

	int sizes[] = { 0, 0, 1, 0, 1, 2, 1, 2, 2, 3, 3, 3 };
	int shapes[] = { 0, 2, 2, 1, 0, 2, 1, 1, 0, 2, 1, 0 };
	int sizeIndex = 0;
	for (int i = 0; i < 12; i++) {
		if (sizes[i] == info.size && shapes[i] == info.shape) {
			sizeIndex = i;
			break;
		}
	}
	SendMessage(data->hWndSizeDropdown, CB_SETCURSEL, sizeIndex, 0);

	UpdateEnabledControls(hWnd);
}

void UpdateOamDropdown(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);
	SendMessage(data->hWndOamDropdown, CB_RESETCONTENT, 0, 0);

	WCHAR name[13];
	for (int i = 0; i < data->ncer.cells[data->cell].nAttribs; i++) {
		wsprintf(name, L"OAM %d", i);
		SendMessage(data->hWndOamDropdown, CB_ADDSTRING, 0, (LPARAM) name);
	}
	SendMessage(data->hWndOamDropdown, CB_SETCURSEL, 0, 0);
}

int getOamFromPoint(NCER_CELL *cell, int x, int y) {
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

void ncerCreateCopy(NCER *dest, NCER *src) {
	memcpy(dest, src, sizeof(NCER));
	if (src->labl != NULL) {
		dest->labl = (char *) malloc(src->lablSize);
		memcpy(dest->labl, src->labl, src->lablSize);
	}
	if (src->uext != NULL) {
		dest->uext = (char *) malloc(src->uextSize);
		memcpy(dest->uext, src->uext, src->uextSize);
	}
	dest->cells = (NCER_CELL *) malloc(src->nCells * sizeof(NCER_CELL));
	memcpy(dest->cells, src->cells, src->nCells * sizeof(NCER_CELL));
	for (int i = 0; i < dest->nCells; i++) {
		NCER_CELL *cell = dest->cells + i;
		WORD *attr = cell->attr;
		cell->attr = (WORD *) malloc(cell->nAttribs * 3 * 2);
		memcpy(cell->attr, attr, cell->nAttribs * 3 * 2);
	}
	if (src->vramTransfer != NULL) {
		dest->vramTransfer = (CHAR_VRAM_TRANSFER *) calloc(src->nCells, sizeof(CHAR_VRAM_TRANSFER));
		memcpy(dest->vramTransfer, src->vramTransfer, src->nCells * sizeof(CHAR_VRAM_TRANSFER));
	}
}

void ncerEditorUndoRedo(NCERVIEWERDATA *data) {
	//write fields into main NCER copy. 
	NCER *ncer = &data->ncer;
	NCER *newState = (NCER *) undoGetStackPosition(&data->undo);

	//free attributes for all cells
	for (int i = 0; i < ncer->nCells; i++) {
		free(ncer->cells[i].attr);
		ncer->cells[i].attr = NULL;
	}

	ncer->bankAttribs = newState->bankAttribs;
	ncer->lablSize = newState->lablSize;
	ncer->uextSize = newState->uextSize;
	ncer->nCells = newState->nCells;

	//now make new allocations for UEXT and LABL
	ncer->uext = realloc(ncer->uext, ncer->uextSize);
	ncer->labl = realloc(ncer->labl, ncer->lablSize);
	memcpy(ncer->uext, newState->uext, ncer->uextSize);
	memcpy(ncer->labl, newState->labl, ncer->lablSize);

	//reallocate cell data
	ncer->cells = realloc(ncer->cells, ncer->nCells * sizeof(NCER_CELL));
	memcpy(ncer->cells, newState->cells, newState->nCells * sizeof(NCER_CELL));
	//fix it up so that the attribute pointers are duplicates. Don't want to mess up undo states here.
	for (int i = 0; i < ncer->nCells; i++) {
		NCER_CELL *cell = ncer->cells + i;
		WORD *oldAttr = cell->attr;
		cell->attr = malloc(cell->nAttribs * 3 * 2);
		memcpy(cell->attr, oldAttr, cell->nAttribs * 3 * 2);
	}

	//if the selected OAM or cell is out of bounds, bring it back in-bounds.
	if (data->cell >= data->ncer.nCells) {
		data->cell = data->ncer.nCells - 1;
	}
	SendMessage(data->hWndCellDropdown, CB_SETCURSEL, data->cell, 0);
	if (data->oam >= data->ncer.cells[data->cell].nAttribs) {
		data->oam = data->ncer.cells[data->cell].nAttribs - 1;
	}
	SendMessage(data->hWndOamDropdown, CB_SETCURSEL, data->oam, 0);
}

void ncerEditorUndo(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);
	undo(&data->undo);

	ncerEditorUndoRedo(data);
	SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

	UpdateControls(hWnd);
}

void ncerEditorRedo(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);
	redo(&data->undo);

	ncerEditorUndoRedo(data);
	SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

	UpdateControls(hWnd);
}

static void CellPreviewUpdate(HWND hWnd, int cellno) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);
	NCER *ncer = &data->ncer;
	PreviewLoadObjCell(ncer, NULL, cellno);
}

static HWND CellEditorGetAssociatedEditor(HWND hWnd, int type) {
	HWND hWndMain = getMainWindow(hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);

	switch (type) {
		case FILE_TYPE_PALETTE:
			return nitroPaintStruct->hWndNclrViewer;
		case FILE_TYPE_CHARACTER:
			return nitroPaintStruct->hWndNcgrViewer;
	}
	return NULL;
}

LRESULT WINAPI NcerViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
		{

			//mapping modes
			LPCWSTR mappingNames[] = {
				L"2D", L"1D 32K", L"1D 64K", L"1D 128K", L"1D 256K"
			};

			//OBJ types
			LPCWSTR objTypes[] = {
				L"Normal", L"Translucent", L"Window"
			};

			//OBJ priorities
			LPCWSTR objPriorities[] = {
				L"0", L"1", L"2", L"3"
			};

			data->hWndCellDropdown = CreateCombobox(hWnd, NULL, 0, 0, 256, 132, 100, 0);
			data->hWndOamDropdown = CreateCombobox(hWnd, NULL, 0, 512, 0, 68, 100, 0);
			CreateStatic(hWnd, L"Character:", 512, 21, 50, 21);
			data->hWndCharacterOffset = CreateEdit(hWnd, L"0", 512 + 50, 21, 30, 21, TRUE);
			data->hWndCharacterOffsetButton = CreateButton(hWnd, L"Set", 512 + 50 + 30, 21, 20, 21, FALSE);
			data->hWndPaletteDropdown = CreateCombobox(hWnd, NULL, 0, 512, 42, 100, 100, 0);
			data->hWndCreateCell = CreateButton(hWnd, L"Generate Cell", 0, 282, 164, 22, FALSE);
			data->hWndDuplicateCell = CreateButton(hWnd, L"Duplicate Cell", 0, 304, 164, 22, FALSE);
			data->hWndMappingMode = CreateCombobox(hWnd, mappingNames, 5, 169, 256 + 22 + 5, 75, 100, 0);
			CreateStatic(hWnd, L"Size:", 512, 63, 25, 21);
			CreateStatic(hWnd, L"OBJ:", 418, 261, 25, 22);

			CreateStatic(hWnd, L"X:", 512, 85, 25, 22);
			CreateStatic(hWnd, L"Y:", 512, 107, 25, 22);
			data->hWndXInput = CreateEdit(hWnd, L"0", 537, 85, 75, 22, FALSE);
			data->hWndYInput = CreateEdit(hWnd, L"0", 537, 107, 75, 22, FALSE);
			data->hWndRotateScale = CreateCheckbox(hWnd, L"Rotate/Scale", 512, 129, 100, 22, FALSE);
			data->hWndHFlip = CreateCheckbox(hWnd, L"H Flip", 512, 151, 50, 22, FALSE);
			data->hWndVFlip = CreateCheckbox(hWnd, L"V Flip", 562, 151, 50, 22, FALSE);
			data->hWndDisable = CreateCheckbox(hWnd, L"Disable", 512, 173, 100, 22, FALSE);
			CreateStatic(hWnd, L"Matrix:", 512, 195, 50, 22);
			data->hWndMatrix = CreateEdit(hWnd, L"0", 562, 195, 50, 22, TRUE);
			data->hWnd8bpp = CreateCheckbox(hWnd, L"8bpp", 512, 217 + 22, 25 + 20, 22, FALSE);
			data->hWndMosaic = CreateCheckbox(hWnd, L"Mosaic", 537 + 20, 217 + 22, 75 - 20, 22, FALSE);

			data->hWndOamRemove = CreateButton(hWnd, L"-", 580, 0, 16, 21, FALSE);
			data->hWndOamAdd = CreateButton(hWnd, L"+", 596, 0, 16, 21, FALSE);
			data->hWndCellRemove = CreateButton(hWnd, L"-", 132, 256, 16, 21, FALSE);
			data->hWndCellAdd = CreateButton(hWnd, L"+", 148, 256, 16, 21, FALSE);

			data->hWndDoubleSize = CreateCheckbox(hWnd, L"Double Size", 512, 239 - 22, 100, 22, FALSE);
			CreateStatic(hWnd, L"Priority:", 512, 261, 50, 21);
			data->hWndPriority = CreateCombobox(hWnd, objPriorities, 4, 512 + 50, 261, 50, 100, 0);
			CreateStatic(hWnd, L"Type:", 512, 282, 30, 21);
			data->hWndType = CreateCombobox(hWnd, objTypes, 3, 512 + 30, 282, 70, 100, 0);

			data->hWndSizeDropdown = CreateCombobox(hWnd, NULL, 0, 537, 63, 75, 100, 0);
			data->hWndCellBoundsCheckbox = CreateCheckbox(hWnd, L"Show cell bounds", 169, 256, 100, 22, FALSE);
			data->hWndGuidelines = CreateCheckbox(hWnd, L"Show Guidelines", 274, 256, 100, 22, TRUE);
			data->hWndOutlineObj = CreateCheckbox(hWnd, L"Outline OBJ", 274, 256 + 27, 100, 22, TRUE);
			break;
		}
		case WM_NCHITTEST:	//make the border non-sizeable
		{
			LRESULT ht = DefMDIChildProc(hWnd, msg, wParam, lParam);
			if (ht == HTTOP || ht == HTBOTTOM || ht == HTLEFT || ht == HTRIGHT || ht == HTTOPLEFT || ht == HTTOPRIGHT || ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT)
				return HTBORDER;
			return ht;
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

			data->frameData.contentWidth = 612;
			data->frameData.contentHeight = 326;

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

			RECT rc = { 0 };
			rc.right = data->frameData.contentWidth;
			rc.bottom = data->frameData.contentHeight;
			if (rc.right < 150) rc.right = 612;
			AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
			int width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
			int height = rc.bottom - rc.top + 4;
			SetWindowPos(hWnd, HWND_TOP, 0, 0, width, height, SWP_NOMOVE);

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentWidth;
			info.nPos = 0;
			info.nPage = rcClient.right - rcClient.left + 1;
			info.nTrackPos = 0;
			info.fMask = SIF_POS | SIF_RANGE | SIF_POS | SIF_TRACKPOS | SIF_PAGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = data->frameData.contentHeight;
			info.nPage = rcClient.bottom - rcClient.top + 1;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);

			WCHAR palStr[] = L"Palette 00";
			int nPalettes = 16;
			
			for (int i = 0; i < 16; i++) {
				wsprintf(palStr, L"Palette %02d", i);
				SendMessage(data->hWndPaletteDropdown, CB_ADDSTRING, 0, (LPARAM) palStr);
			}
			SendMessage(data->hWndPaletteDropdown, CB_SETCURSEL, 0, 0);
			
			NCER_CELL_INFO cinfo;
			CellDecodeOamAttributes(&cinfo, data->ncer.cells, 0);
			
			WCHAR size[13];
			int sizes[] = { 0, 0, 1, 0, 1, 2, 1, 2, 2, 3, 3, 3 };
			int shapes[] = { 0, 2, 2, 1, 0, 2, 1, 1, 0, 2, 1, 0 };

			for (int i = 0; i < 12; i++) {
				int shape = shapes[i];
				int objSize = sizes[i];
				int objWidth, objHeight;
				CellGetObjDimensions(shape, objSize, &objWidth, &objHeight);
				wsprintfW(size, L"%dx%d", objWidth, objHeight);
				SendMessage(data->hWndSizeDropdown, CB_ADDSTRING, 0, (LPARAM) size);
			}

			int sizeIndex = 0;
			for (int i = 0; i < 12; i++) {
				if (sizes[i] == cinfo.size && shapes[i] == cinfo.shape) {
					sizeIndex = i;
					break;
				}
			}
			SendMessage(data->hWndSizeDropdown, CB_SETCURSEL, sizeIndex, 0);

			for (int i = 0; i < data->ncer.nCells; i++) {
				wsprintf(size, L"Cell %02d", i);
				SendMessage(data->hWndCellDropdown, CB_ADDSTRING, 0, (LPARAM) size);
			}
			SendMessage(data->hWndCellDropdown, CB_SETCURSEL, 0, 0);

			UpdateOamDropdown(hWnd);
			UpdateControls(hWnd);

			undoInitialize(&data->undo, sizeof(NCER));
			data->undo.freeFunction = (void (*) (void *)) CellFree;
			NCER copy;
			ncerCreateCopy(&copy, &data->ncer);
			undoAdd(&data->undo, &copy);

			data->showGuidelines = 1;
			data->showObjOutline = 1;
			break;
		}
		case NV_UPDATEPREVIEW:
			CellPreviewUpdate(hWnd, data->cell);
			break;
		case WM_LBUTTONDOWN:
		{
			POINT pos;
			GetCursorPos(&pos);
			ScreenToClient(hWnd, &pos);
			SetFocus(hWnd);
			if (pos.x >= 0 && pos.y >= 0 && pos.x < 512 && pos.y < 256) {
				NCER_CELL *cell = data->ncer.cells + data->cell;
				int x = (pos.x - 256) & 0x1FF;
				int y = (pos.y - 128) & 0xFF;
				int oam = getOamFromPoint(cell, x, y);
				if (oam != -1) {
					NCER_CELL_INFO info;
					CellDecodeOamAttributes(&info, cell, oam);
					data->oam = oam;
					data->dragStartX = pos.x;
					data->dragStartY = pos.y;
					data->oamStartX = info.x;
					data->oamStartY = info.y;
					SendMessage(data->hWndOamDropdown, CB_SETCURSEL, data->oam, 0);
					UpdateControls(hWnd);
					InvalidateRect(hWnd, NULL, TRUE);
					SetCapture(hWnd);
					data->mouseDown = 1;

					NCER copy;
					ncerCreateCopy(&copy, &data->ncer);
					undoAdd(&data->undo, &copy);
				}
			}
			break;
		}
		case WM_LBUTTONUP:
		{
			data->mouseDown = 0;
			ReleaseCapture();
			SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
			break;
		}
		case WM_MOUSEMOVE:
		{
			if (data->mouseDown) {
				POINT pos;
				WORD *attribs = data->ncer.cells[data->cell].attr + (3 * data->oam);
				GetCursorPos(&pos);
				ScreenToClient(hWnd, &pos);
				int dx = pos.x - data->dragStartX;
				int dy = pos.y - data->dragStartY;
				int x = (data->oamStartX + dx) & 0x1FF;
				int y = (data->oamStartY + dy) & 0xFF;
				attribs[0] = (attribs[0] & 0xFF00) | y;
				attribs[1] = (attribs[1] & 0xFE00) | x;

				NCER *currentSlot = undoGetStackPosition(&data->undo);
				memcpy(currentSlot->cells[data->cell].attr, data->ncer.cells[data->cell].attr, currentSlot->cells[data->cell].nAttribs * 3 * 2);

				UpdateControls(hWnd);
				InvalidateRect(hWnd, NULL, FALSE);
			}
			break;
		}
		case WM_KEYDOWN:
		{
			if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT) {
				NCER_CELL *cell = data->ncer.cells + data->cell;
				WORD *attr = cell->attr + (3 * data->oam);
				int change = 1;
				switch (wParam) {
					case VK_UP:
						attr[0] = (attr[0] & 0xFF00) | (((attr[0] & 0xFF) - 1) & 0xFF);
						break;
					case VK_DOWN:
						attr[0] = (attr[0] & 0xFF00) | (((attr[0] & 0xFF) + 1) & 0xFF);
						break;
					case VK_LEFT:
						attr[1] = (attr[1] & 0xFE00) | (((attr[1] & 0x1FF) - 1) & 0x1FF);
						break;
					case VK_RIGHT:
						attr[1] = (attr[1] & 0xFE00) | (((attr[1] & 0x1FF) + 1) & 0x1FF);
						break;
					default:
						change = 0;
						break;
				}
				if (change) {
					if (!(HIWORD(lParam) & KF_REPEAT)) {
						NCER copy;
						ncerCreateCopy(&copy, &data->ncer);
						undoAdd(&data->undo, &copy);
					} else {
						NCER *currentSlot = undoGetStackPosition(&data->undo);
						memcpy(currentSlot->cells[data->cell].attr, data->ncer.cells[data->cell].attr, currentSlot->cells[data->cell].nAttribs * 3 * 2);
					}
				}
				UpdateControls(hWnd);
				InvalidateRect(hWnd, NULL, FALSE);
			}
			break;
		}
		case WM_PAINT:
		{
			PaintNcerViewer(hWnd);
			return 0;
		}
		case WM_COMMAND:
		{
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				WORD notification = HIWORD(wParam);

				HWND hWndMain = getMainWindow(hWnd);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);

				int changed = 0;

				if (notification == CBN_SELCHANGE && hWndControl == data->hWndCellDropdown) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					data->cell = sel;
					data->oam = 0;
					NCER_CELL_INFO cinfo;
					NCER_CELL *cell = data->ncer.cells + sel;
					CellDecodeOamAttributes(&cinfo, cell, data->oam);
					
					UpdateOamDropdown(hWnd);

					UpdateControls(hWnd);
					InvalidateRect(hWnd, NULL, TRUE);
					SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);

					if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndPaletteDropdown) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					NCER_CELL *cell = data->ncer.cells + data->cell;
					WORD attr2 = cell->attr[2 + 3 * data->oam];
					cell->attr[2 + 3 * data->oam] = (attr2 & 0x0FFF) | (sel << 12);
					InvalidateRect(hWnd, NULL, TRUE);

					if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
					changed = 1;
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndOamDropdown){
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					data->oam = sel;

					InvalidateRect(hWnd, NULL, TRUE);
					UpdateControls(hWnd);

					if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndCharacterOffsetButton) {
					int chr = GetEditNumber(data->hWndCharacterOffset);
					NCER_CELL *cell = data->ncer.cells + data->cell;
					uint16_t attr2 = cell->attr[2 + 3 * data->oam];
					uint16_t attr0 = cell->attr[0 + 3 * data->oam];
					if ((attr0 >> 13) & 1) chr &= ~1;
					attr2 = attr2 & 0xFC00;
					attr2 |= chr & 0x3FF;
					cell->attr[2 + 3 * data->oam] = attr2;
					InvalidateRect(hWnd, NULL, TRUE);
					changed = 1;
				} else if (notification == BN_CLICKED && (
					hWndControl == data->hWnd8bpp || hWndControl == data->hWndDisable || hWndControl == data->hWndHFlip
					|| hWndControl == data->hWndVFlip || hWndControl == data->hWndMosaic || hWndControl == data->hWndRotateScale
					|| hWndControl == data->hWndDoubleSize)) {
					int state = GetCheckboxChecked(hWndControl);

					int bit = 0, attr = 0;
					//really wish I could've used a switch/case here :(
					if (hWndControl == data->hWnd8bpp) {
						bit = 13;
						attr = 0;
					} else if (hWndControl == data->hWndDisable || hWndControl == data->hWndDoubleSize) {
						bit = 9;
						attr = 0;
					} else if (hWndControl == data->hWndHFlip) {
						bit = 12;
						attr = 1;
					} else if (hWndControl == data->hWndVFlip) {
						bit = 13;
						attr = 1;
					} else if (hWndControl == data->hWndMosaic) {
						bit = 12;
						attr = 0;
					} else if (hWndControl == data->hWndRotateScale) {
						bit = 8;
						attr = 0;
					}

					if (attr != -1) {
						WORD *dest = data->ncer.cells[data->cell].attr + (attr + 3 * data->oam);

						WORD mask = (-!state) ^ (1 << bit);
						if (state) *dest |= mask;
						else *dest &= mask;
					}
					InvalidateRect(hWnd, NULL, FALSE);
					UpdateControls(hWnd);
					changed = 1;
				} else if (notification == EN_CHANGE && hWndControl == data->hWndXInput) {
					int x = GetEditNumber(hWndControl);
					uint16_t *dest = data->ncer.cells[data->cell].attr + (data->oam * 3 + 1);
					*dest = (*dest & 0xFE00) | (x & 0x1FF);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == EN_CHANGE && hWndControl == data->hWndYInput) {
					int y = GetEditNumber(hWndControl);
					uint16_t *dest = data->ncer.cells[data->cell].attr + (data->oam * 3);
					*dest = (*dest & 0xFF00) | (y & 0xFF);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == EN_CHANGE && hWndControl == data->hWndMatrix) {
					int mtx = GetEditNumber(hWndControl);
					uint16_t *dest = data->ncer.cells[data->cell].attr + (data->oam * 3 + 1);
					uint16_t attr0 = dest[-1];
					if (attr0 & 0x100) {
						*dest = (*dest & 0xC1FF) | ((mtx & 0x1F) << 9);
						InvalidateRect(hWnd, NULL, FALSE);
					}
				} else if (notification == BN_CLICKED && hWndControl == data->hWndOamRemove) {
					NCER_CELL *cell = data->ncer.cells + data->cell;

					//don't remove an entry if it's the only entry!
					if (cell->nAttribs > 1) {
						memmove(cell->attr + (data->oam * 3), cell->attr + ((data->oam + 1) * 3), (cell->nAttribs - data->oam - 1) * 6);
						cell->nAttribs--;
						if (data->oam) data->oam--;
						SendMessage(data->hWndOamDropdown, CB_SETCURSEL, data->oam, 0);
						SendMessage(data->hWndOamDropdown, CB_DELETESTRING, cell->nAttribs, 0);
						UpdateControls(hWnd);
						InvalidateRect(hWnd, NULL, TRUE);
						changed = 1;
					}
				} else if (notification == BN_CLICKED && hWndControl == data->hWndOamAdd) {
					//reallocate the attribute buffer
					NCER_CELL *cell = data->ncer.cells + data->cell;
					WCHAR name[16];
					WORD *attributes = cell->attr;
					attributes = realloc(attributes, (cell->nAttribs + 1) * 6);
					memset(attributes + (3 * cell->nAttribs), 0, 6);
					cell->attr = attributes;
					cell->nAttribs++;
					data->oam = cell->nAttribs - 1;
					wsprintfW(name, L"OAM %d", cell->nAttribs - 1);
					SendMessage(data->hWndOamDropdown, CB_ADDSTRING, 0, (LPARAM) name);
					SendMessage(data->hWndOamDropdown, CB_SETCURSEL, cell->nAttribs - 1, 0);
					UpdateControls(hWnd);
					InvalidateRect(hWnd, NULL, TRUE);
					changed = 1;
				} else if (notification == BN_CLICKED && hWndControl == data->hWndCellRemove) {
					NCER *ncer = &data->ncer;
					//don't delete the last cell
					if (ncer->nCells > 1) {
						NCER_CELL *cell = ncer->cells + data->cell;
						WORD *attr = cell->attr;
						memmove(ncer->cells + data->cell, ncer->cells + data->cell + 1, (ncer->nCells - data->cell - 1) * sizeof(NCER_CELL));
						ncer->nCells--;
						data->oam = 0;
						SendMessage(data->hWndCellDropdown, CB_DELETESTRING, ncer->nCells, 0);
						if (data->cell) {
							data->cell--;
							SendMessage(data->hWndCellDropdown, CB_SETCURSEL, data->cell, 0);
						}
						free(attr);
						UpdateOamDropdown(hWnd);
						UpdateControls(hWnd);
						InvalidateRect(hWnd, NULL, TRUE);
						changed = 1;
					}
				} else if (notification == BN_CLICKED && hWndControl == data->hWndCellAdd) {
					NCER *ncer = &data->ncer;

					//add the cell
					ncer->nCells++;
					ncer->cells = realloc(ncer->cells, ncer->nCells * sizeof(NCER_CELL));
					NCER_CELL *cell = ncer->cells + ncer->nCells - 1;
					memset(cell, 0, sizeof(NCER_CELL));
					cell->nAttribs = 1;
					cell->attr = (WORD *) calloc(1, 6);
					data->cell = ncer->nCells - 1;
					data->oam = 0;

					WCHAR name[16];
					wsprintfW(name, L"Cell %02d", data->cell);

					UpdateOamDropdown(hWnd);
					SendMessage(data->hWndCellDropdown, CB_ADDSTRING, 0, (LPARAM) name);
					SendMessage(data->hWndCellDropdown, CB_SETCURSEL, data->cell, 0);
					UpdateControls(hWnd);
					InvalidateRect(hWnd, NULL, TRUE);
					changed = 1;
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndSizeDropdown) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					int sizes[] = { 0, 0, 1, 0, 1, 2, 1, 2, 2, 3, 3, 3 };
					int shapes[] = { 0, 2, 2, 1, 0, 2, 1, 1, 0, 2, 1, 0 };
					int shape = shapes[sel], size = sizes[sel];

					NCER_CELL *cell = data->ncer.cells + data->cell;
					WORD *attr = cell->attr + (3 * data->oam);
					attr[0] = (attr[0] & 0x3FFF) | (shape << 14);
					attr[1] = (attr[1] & 0x3FFF) | (size << 14);
					UpdateControls(hWnd);
					InvalidateRect(hWnd, NULL, TRUE);
					changed = 1;
				} else if (notification == BN_CLICKED && hWndControl == data->hWndCellBoundsCheckbox) {
					int state = GetCheckboxChecked(hWndControl);
					data->showCellBounds = state;
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndGuidelines) {
					int state = GetCheckboxChecked(hWndControl);
					data->showGuidelines = state;
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndOutlineObj) {
					int state = GetCheckboxChecked(hWndControl);
					data->showObjOutline = state;
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndType) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					NCER_CELL *cell = data->ncer.cells + data->cell;
					WORD *attr = cell->attr + (3 * data->oam);
					attr[0] = (attr[0] & 0xF3FF) | (sel << 10);
					InvalidateRect(hWnd, NULL, FALSE);
					changed = 1;
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndPriority) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					NCER_CELL *cell = data->ncer.cells + data->cell;
					WORD *attr = cell->attr + (3 * data->oam);
					attr[2] = (attr[2] & 0xF3FF) | (sel << 10);
					InvalidateRect(hWnd, NULL, FALSE);
					changed = 1;
				} else if (notification == BN_CLICKED && hWndControl == data->hWndCreateCell) {
					//check for palette and character open as well
					int nPalettes = GetAllEditors(hWndMain, FILE_TYPE_PALETTE, NULL, 0);
					int nChars = GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, NULL, 0);
					if (nPalettes == 0 || nChars == 0) {
						MessageBox(hWnd, L"Requires open palette and character.", L"Error", MB_ICONERROR);
						break;
					}

					HWND hWndNclrViewer = CellEditorGetAssociatedEditor(hWnd, FILE_TYPE_PALETTE);
					HWND hWndNcgrViewer = CellEditorGetAssociatedEditor(hWnd, FILE_TYPE_CHARACTER);
					NCGR *ncgr = &((NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer))->ncgr;
					if (data->ncer.mappingMode == GX_OBJVRAMMODE_CHAR_2D) {
						MessageBox(hWnd, L"Cannot be used with 2D mapping.", L"Error", MB_ICONERROR);
						break;
					}

					LPWSTR filter = L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0";
					LPWSTR path = openFileDialog(hWnd, L"Open Image", filter, L"");
					if (path == NULL) break;

					int width, height;
					COLOR32 *px = ImgRead(path, &width, &height);
					free(path);

					//reject images too large
					if (width > 512 || height > 256) {
						MessageBox(hWnd, L"Image too large.", L"Too large", MB_ICONERROR);
						free(px);
						break;
					}

					//create generator dialog
					HWND h = CreateWindow(L"NcerCreateCellClass", L"Generate Cell", WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
						CW_USEDEFAULT, CW_USEDEFAULT, hWndMain, NULL, NULL, NULL);
					SendMessage(h, NV_INITIALIZE, width | (height << 16), (LPARAM) px);
					DoModal(h);

					//update UI elements
					SendMessage(data->hWndCellDropdown, CB_SETCURSEL, data->cell, 0);
					UpdateOamDropdown(hWnd);
					UpdateControls(hWnd);
					changed = 1;

					//update palette and character window
					SendMessage(hWndNcgrViewer, NV_UPDATEPREVIEW, 0, 0);
					SendMessage(hWndNclrViewer, NV_UPDATEPREVIEW, 0, 0);

					//free px
					free(px);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndDuplicateCell) {
					//duplicate cell
					data->ncer.nCells++;
					data->ncer.cells = realloc(data->ncer.cells, data->ncer.nCells * sizeof(NCER_CELL));

					NCER_CELL *cell = data->ncer.cells + data->cell;
					NCER_CELL *dup = data->ncer.cells + data->ncer.nCells - 1;

					//copy
					dup->cellAttr = cell->cellAttr;
					dup->minX = cell->minX, dup->maxX = cell->maxX;
					dup->minY = cell->minY, dup->maxY = cell->maxY;
					dup->nAttribs = cell->nAttribs;
					dup->attr = (uint16_t *) calloc(cell->nAttribs, 3 * sizeof(uint16_t));
					memcpy(dup->attr, cell->attr, cell->nAttribs * 3 * sizeof(uint16_t));

					data->cell = data->ncer.nCells - 1;
					data->oam = 0;
					changed = 1;

					//select
					WCHAR name[16];
					wsprintfW(name, L"Cell %02d", data->cell);
					SendMessage(data->hWndCellDropdown, CB_ADDSTRING, 0, (LPARAM) name);
					SendMessage(data->hWndCellDropdown, CB_SETCURSEL, data->cell, 0);

					UpdateOamDropdown(hWnd);
					UpdateControls(hWnd);
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndMappingMode) {
					const int mappings[] = {
						GX_OBJVRAMMODE_CHAR_2D,
						GX_OBJVRAMMODE_CHAR_1D_32K,
						GX_OBJVRAMMODE_CHAR_1D_64K,
						GX_OBJVRAMMODE_CHAR_1D_128K,
						GX_OBJVRAMMODE_CHAR_1D_256K
					};
					int sel = mappings[SendMessage(data->hWndMappingMode, CB_GETCURSEL, 0, 0)];
					data->ncer.mappingMode = sel;
					changed = 1;

					InvalidateRect(hWnd, NULL, FALSE);
				}

				//log a change
				if (changed) {
					NCER copy;
					ncerCreateCopy(&copy, &data->ncer);
					undoAdd(&data->undo, &copy);
					SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
				}
			}
			if (lParam == 0 && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
					case ID_FILE_SAVEAS:
					case ID_FILE_SAVE:
					{
						if (data->szOpenFile[0] == L'\0' || LOWORD(wParam) == ID_FILE_SAVEAS) {
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
					case ID_EDIT_UNDO:
					{
						ncerEditorUndo(hWnd);
						break;
					}
					case ID_EDIT_REDO:
					{
						ncerEditorRedo(hWnd);
						break;
					}
					case ID_FILE_EXPORT:
					{
						LPWSTR location = saveFileDialog(hWnd, L"Save Bitmap", L"PNG Files (*.png)\0*.png\0All Files\0*.*\0", L"png");
						if (!location) break;

						HWND hWndMain = getMainWindow(hWnd);
						NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
						HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
						HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;

						NCGR *ncgr = NULL;
						NCLR *nclr = NULL;
						NCER *ncer = &data->ncer;
						NCER_CELL *cell = ncer->cells + data->cell;
						NCER_CELL_INFO info;
						CellDecodeOamAttributes(&info, cell, data->oam);

						if (hWndNclrViewer) {
							nclr = (NCLR*) EditorGetObject(hWndNclrViewer);
						}
						if (hWndNcgrViewer) {
							ncgr = (NCGR *) EditorGetObject(hWndNcgrViewer);
						}

						COLOR32 *bits = (COLOR32 *) calloc(256 * 512, sizeof(COLOR32));
						CellRenderCell(bits, cell, ncer->mappingMode, ncgr, nclr, NULL, 256, 128, 0, -1, 1.0f, 0.0f, 0.0f, 1.0f);
						ImgSwapRedBlue(bits, 512, 256);
						ImgWrite(bits, 512, 256, location);

						free(bits);
						free(location);
						break;
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			nitroPaintStruct->hWndNcerViewer = NULL;
			if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
			undoDestroy(&data->undo);
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

LRESULT CALLBACK NcerCreateCellWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

			SetWindowLongPtr(hWnd, 0 * sizeof(LONG_PTR), (LONG_PTR) px);
			SetWindowLongPtr(hWnd, 1 * sizeof(LONG_PTR), width);
			SetWindowLongPtr(hWnd, 2 * sizeof(LONG_PTR), height);

			//setup controls
			int groupWidth = 207, groupHeight = 131, group2Height = 77, groupX = 554, groupY = 10;

			LPCWSTR boundModes[] = { L"Opaque", L"Full Image" };
			CreateStatic(hWnd, L"Optimization:", 10, 10, 70, 22);
			data->hWndAggressiveness = CreateTrackbar(hWnd, 90, 10, 150, 22, 0, 100, 100);
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
			GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, &hWndNcgrEditor, 1);
			NCGR *ncgr = (NCGR *) EditorGetObject(hWndNcgrEditor);

			int lastIndex = -1;
			unsigned char zeroChar[64] = { 0 };
			for (int i = 0; i < ncgr->nTiles; i++) {
				if (memcmp(ncgr->tiles[i], zeroChar, sizeof(zeroChar)) != 0) {
					lastIndex = i;
				}
			}
			SetEditNumber(data->hWndCharacter, lastIndex + 1);
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
				NITROPAINTSTRUCT *npStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);

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

				//clear out current cell
				ncerViewerData->oam = 0;

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
				InvalidateAllEditors(hWndMain, FILE_TYPE_CELL);

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

VOID RegisterNcerViewerClass(VOID) {
	int features = EDITOR_FEATURE_UNDO;
	EditorRegister(L"NcerViewerClass", NcerViewerWndProc, L"Cell Editor", sizeof(NCERVIEWERDATA), features);
	RegisterGenericClass(L"NcerCreateCellClass", NcerCreateCellWndProc, 12 * sizeof(void *));
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
