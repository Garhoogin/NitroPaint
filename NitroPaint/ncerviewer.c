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
	decodeAttributesEx(&info, cell, data->oam);

	int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
	memset(data->frameBuffer, 0, sizeof(data->frameBuffer));
	NCER_VRAM_TRANSFER_ENTRY *transferEntry = NULL;
	if (data->ncer.vramTransfer != NULL)
		transferEntry = data->ncer.vramTransfer + data->cell;
	DWORD *bits = ncerRenderWholeCell3(data->frameBuffer, data->ncer.cells + data->cell, ncgr, nclr, transferEntry, 
		translateX, translateY, 1, data->oam, 1.0f, 0.0f, 0.0f, 1.0f);

	//draw lines if needed
	if (data->showCellBounds) {
		int minX = cell->minX + translateX, maxX = cell->maxX + translateX - 1;
		int minY = cell->minY + translateY, maxY = cell->maxY + translateY - 1;
		minX &= 0x1FF, maxX &= 0x1FF, minY &= 0xFF, maxY &= 0xFF;

		for (int i = 0; i < 256; i++) {
			if(bits[i * 512 + minX] >> 24 != 0xFE) bits[i * 512 + minX] = 0xFF0000FF;
			if(bits[i * 512 + maxX] >> 24 != 0xFE) bits[i * 512 + maxX] = 0xFF0000FF;
		}
		for (int i = 0; i < 512; i++) {
			if(bits[minY * 512 + i] >> 24 != 0xFE) bits[minY * 512 + i] = 0xFF0000FF;
			if(bits[maxY * 512 + i] >> 24 != 0xFE) bits[maxY * 512 + i] = 0xFF0000FF;
		}
	}

	HBITMAP hbm = CreateBitmap(512, 256, 1, 32, bits);
	HDC hCompatibleDC = CreateCompatibleDC(hWindowDC);
	SelectObject(hCompatibleDC, hbm);
	BitBlt(hWindowDC, 0, 0, 512, 256, hCompatibleDC, 0, 0, SRCCOPY);
	DeleteObject(hbm);

	int width, height;
	bits = ncerCellToBitmap(&info, ncgr, nclr, &width, &height, 1);
	hbm = CreateBitmap(width, height, 1, 32, bits);
	SelectObject(hCompatibleDC, hbm);
	BitBlt(hWindowDC, 512 - 69, 256 + 5, width, height, hCompatibleDC, 0, 0, SRCCOPY);
	DeleteObject(hbm);
	free(bits);

	DeleteObject(hCompatibleDC);

	EndPaint(hWnd, &ps);
}

void UpdateEnabledControls(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	NCER_CELL *cell = data->ncer.cells + data->cell;
	NCER_CELL_INFO info;
	decodeAttributesEx(&info, cell, data->oam);

	//if rotate/scale, disable HV Flip and Disable, enable matrix.
	if (info.rotateScale) {
		SetWindowLong(data->hWndHFlip, GWL_STYLE, GetWindowLong(data->hWndHFlip, GWL_STYLE) | WS_DISABLED);
		SetWindowLong(data->hWndVFlip, GWL_STYLE, GetWindowLong(data->hWndVFlip, GWL_STYLE) | WS_DISABLED);
		SetWindowLong(data->hWndDisable, GWL_STYLE, GetWindowLong(data->hWndDisable, GWL_STYLE) | WS_DISABLED);
		SetWindowLong(data->hWndMatrix, GWL_STYLE, GetWindowLong(data->hWndMatrix, GWL_STYLE) & ~WS_DISABLED);
		SetWindowLong(data->hWndDoubleSize, GWL_STYLE, GetWindowLong(data->hWndDoubleSize, GWL_STYLE) & ~WS_DISABLED);
	} else {
		SetWindowLong(data->hWndHFlip, GWL_STYLE, GetWindowLong(data->hWndHFlip, GWL_STYLE) & ~WS_DISABLED);
		SetWindowLong(data->hWndVFlip, GWL_STYLE, GetWindowLong(data->hWndVFlip, GWL_STYLE) & ~WS_DISABLED);
		SetWindowLong(data->hWndDisable, GWL_STYLE, GetWindowLong(data->hWndDisable, GWL_STYLE) & ~WS_DISABLED);
		SetWindowLong(data->hWndMatrix, GWL_STYLE, GetWindowLong(data->hWndMatrix, GWL_STYLE) | WS_DISABLED);
		SetWindowLong(data->hWndDoubleSize, GWL_STYLE, GetWindowLong(data->hWndDoubleSize, GWL_STYLE) | WS_DISABLED);
	}
	RedrawWindow(hWnd, NULL, NULL, RDW_ALLCHILDREN | RDW_INVALIDATE);
}

void UpdateControls(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	NCER_CELL *cell = data->ncer.cells + data->cell;
	NCER_CELL_INFO info;
	decodeAttributesEx(&info, cell, data->oam);

	WCHAR buffer[16];
	int len;

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

		len = wsprintfW(buffer, L"%d", info.matrix);
		SendMessage(data->hWndMatrix, WM_SETTEXT, len, (LPARAM) buffer);
		len = wsprintfW(buffer, L"%d", info.characterName);
		SendMessage(data->hWndCharacterOffset, WM_SETTEXT, len, (LPARAM) buffer);
	}
	len = wsprintfW(buffer, L"%d", info.x >= 256 ? (info.x - 512) : info.x);
	SendMessage(data->hWndXInput, WM_SETTEXT, len, (LPARAM) buffer);
	len = wsprintfW(buffer, L"%d", info.y);
	SendMessage(data->hWndYInput, WM_SETTEXT, len, (LPARAM) buffer);

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
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
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
		decodeAttributesEx(&info, cell, i);

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
		cell->attr = (WORD *) malloc(cell->nAttr * 2);
		memcpy(cell->attr, attr, cell->nAttr * 2);
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
		cell->attr = malloc(cell->nAttr * 2);
		memcpy(cell->attr, oldAttr, cell->nAttr * 2);
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
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	undo(&data->undo);

	ncerEditorUndoRedo(data);

	UpdateControls(hWnd);
}

void ncerEditorRedo(HWND hWnd) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	redo(&data->undo);

	ncerEditorUndoRedo(data);

	UpdateControls(hWnd);
}

LRESULT WINAPI NcerViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
		{
			//64x64 cell

			//+-----+ [character index]
			//|     | [palette number]
			//|     | [Size: XxX]
			//+-----+
			//[Cell x]
			data->hWndCellDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 256, 132, 100, hWnd, NULL, NULL, NULL);
			data->hWndOamDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 512, 0, 68, 100, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Character: ", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 512, 21, 50, 21, hWnd, NULL, NULL, NULL);
			data->hWndCharacterOffset = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 512 + 50, 21, 30, 21, hWnd, NULL, NULL, NULL);
			data->hWndCharacterOffsetButton = CreateWindow(L"BUTTON", L"Set", WS_VISIBLE | WS_CHILD, 512 + 50 + 30, 21, 20, 21, hWnd, NULL, NULL, NULL);
			data->hWndPaletteDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 512, 42, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndSizeLabel = CreateWindow(L"STATIC", L"Size:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 512, 63, 25, 21, hWnd, NULL, NULL, NULL);
			data->hWndImportBitmap = CreateWindow(L"BUTTON", L"Import Bitmap", WS_VISIBLE | WS_CHILD, 0, 282, 164, 22, hWnd, NULL, NULL, NULL);
			data->hWndImportReplacePalette = CreateWindow(L"BUTTON", L"Import and Replace Palette", WS_VISIBLE | WS_CHILD, 0, 304, 164, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"OBJ:", WS_VISIBLE | WS_CHILD, 418, 261, 25, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"X: ", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 512, 85, 25, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Y:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 512, 107, 25, 22, hWnd, NULL, NULL, NULL);
			data->hWndXInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 537, 85, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndYInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 537, 107, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndRotateScale = CreateWindow(L"BUTTON", L"Rotate/Scale", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 512, 129, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndHFlip = CreateWindow(L"BUTTON", L"H Flip", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 512, 151, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndVFlip = CreateWindow(L"BUTTON", L"V Flip", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 562, 151, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndDisable = CreateWindow(L"BUTTON", L"Disable", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 512, 173, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Matrix:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 512, 195, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndMatrix = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 562, 195, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWnd8bpp = CreateWindow(L"BUTTON", L"8bpp", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 512, 217 + 22, 25 + 20, 22, hWnd, NULL, NULL, NULL);
			data->hWndMosaic = CreateWindow(L"BUTTON", L"Mosaic", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 537 + 20, 217 + 22, 75 - 20, 22, hWnd, NULL, NULL, NULL);

			data->hWndOamRemove = CreateWindow(L"BUTTON", L"-", WS_VISIBLE | WS_CHILD, 580, 0, 16, 21, hWnd, NULL, NULL, NULL);
			data->hWndOamAdd = CreateWindow(L"BUTTON", L"+", WS_VISIBLE | WS_CHILD, 596, 0, 16, 21, hWnd, NULL, NULL, NULL);
			data->hWndCellRemove = CreateWindow(L"BUTTON", L"-", WS_VISIBLE | WS_CHILD, 132, 256, 16, 21, hWnd, NULL, NULL, NULL);
			data->hWndCellAdd = CreateWindow(L"BUTTON", L"+", WS_VISIBLE | WS_CHILD, 148, 256, 16, 21, hWnd, NULL, NULL, NULL);

			data->hWndDoubleSize = CreateWindow(L"BUTTON", L"Double Size", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 512, 239 - 22, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Priority:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 512, 261, 50, 21, hWnd, NULL, NULL, NULL);
			data->hWndPriority = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 512 + 50, 261, 50, 100, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Type:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 512, 282, 30, 21, hWnd, NULL, NULL, NULL);
			data->hWndType = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 512 + 30, 282, 70, 100, hWnd, NULL, NULL, NULL);

			data->hWndSizeDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 537, 63, 75, 100, hWnd, NULL, NULL, NULL);
			data->hWndCellBoundsCheckbox = CreateWindow(L"BUTTON", L"Show cell bounds", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 169, 256, 100, 22, hWnd, NULL, NULL, NULL);
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
			data->frameData.contentWidth = 612;
			data->frameData.contentHeight = 326;

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
			decodeAttributes(&cinfo, data->ncer.cells);
			
			WCHAR size[13];
			int sizes[] = { 0, 0, 1, 0, 1, 2, 1, 2, 2, 3, 3, 3 };
			int shapes[] = { 0, 2, 2, 1, 0, 2, 1, 1, 0, 2, 1, 0 };

			for (int i = 0; i < 12; i++) {
				int shape = shapes[i];
				int objSize = sizes[i];
				int objWidth, objHeight;
				getObjSize(shape, objSize, &objWidth, &objHeight);
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

			for (int i = 0; i < 4; i++) {
				wsprintf(size, L"%d", i);
				SendMessage(data->hWndPriority, CB_ADDSTRING, 0, (LPARAM) size);
			}
			SendMessage(data->hWndPriority, CB_SETCURSEL, 0, 0);

			SendMessage(data->hWndType, CB_ADDSTRING, 0, (LPARAM) L"Normal");
			SendMessage(data->hWndType, CB_ADDSTRING, 0, (LPARAM) L"Translucent");
			SendMessage(data->hWndType, CB_ADDSTRING, 0, (LPARAM) L"Window");
			SendMessage(data->hWndType, CB_SETCURSEL, 0, 0);

			UpdateOamDropdown(hWnd);
			UpdateControls(hWnd);

			undoInitialize(&data->undo, sizeof(NCER));
			data->undo.freeFunction = (void (*) (void *)) ncerFree;
			NCER copy;
			ncerCreateCopy(&copy, &data->ncer);
			undoAdd(&data->undo, &copy);

			break;
		}
		case WM_LBUTTONDOWN:
		{
			POINT pos;
			GetCursorPos(&pos);
			ScreenToClient(hWnd, &pos);
			SetFocus(hWnd);
			if (pos.x >= 0 && pos.y >= 0 && pos.x < 512 && pos.y < 256) {
				NCER_CELL *cell = data->ncer.cells + data->cell;
				int x = (pos.x - 256 + (cell->maxX + cell->minX) / 2) & 0x1FF;
				int y = (pos.y - 128 + (cell->maxY + cell->minY) / 2) & 0xFF;
				int oam = getOamFromPoint(cell, x, y);
				if (oam != -1) {
					NCER_CELL_INFO info;
					decodeAttributesEx(&info, cell, oam);
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
				memcpy(currentSlot->cells[data->cell].attr, data->ncer.cells[data->cell].attr, currentSlot->cells[data->cell].nAttr * 2);

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
						memcpy(currentSlot->cells[data->cell].attr, data->ncer.cells[data->cell].attr, currentSlot->cells[data->cell].nAttr * 2);
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

				HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);

				int changed = 0;

				if (notification == CBN_SELCHANGE && hWndControl == data->hWndCellDropdown) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					data->cell = sel;
					data->oam = 0;
					NCER_CELL_INFO cinfo;
					NCER_CELL *cell = data->ncer.cells + sel;
					decodeAttributesEx(&cinfo, cell, data->oam);
					
					UpdateOamDropdown(hWnd);

					UpdateControls(hWnd);
					InvalidateRect(hWnd, NULL, TRUE);

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
					WCHAR input[16];
					SendMessage(data->hWndCharacterOffset, WM_GETTEXT, (WPARAM) 15, (LPARAM) input);
					int chr = _wtoi(input);
					NCER_CELL *cell = data->ncer.cells + data->cell;
					WORD attr2 = cell->attr[2 + 3 * data->oam];
					WORD attr0 = cell->attr[0 + 3 * data->oam];
					if ((attr0 >> 13) & 1) chr &= ~1;
					attr2 = attr2 & 0xFC00;
					attr2 |= chr & 0x3FF;
					cell->attr[2 + 3 * data->oam] = attr2;
					InvalidateRect(hWnd, NULL, TRUE);
					changed = 1;
				} else if (notification == BN_CLICKED && (hWndControl == data->hWndImportBitmap || hWndControl == data->hWndImportReplacePalette)) {
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
					HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
					NCLR *nclr = NULL;
					NCGR *ncgr = NULL;
					if (hWndNcgrViewer) {
						NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
						ncgr = &ncgrViewerData->ncgr;
					}
					if (hWndNclrViewer) {
						NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
						nclr = &nclrViewerData->nclr;
					}

					if (nclr == NULL || ncgr == NULL) {
						MessageBox(hWnd, L"Open palette and character graphics are required to import.", L"No NCLR", MB_ICONERROR);
						break;
					}

					LPWSTR path = openFileDialog(hWnd, L"Select Image", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0", L"");
					if (path == NULL) break;

					NCER_CELL *cell = data->ncer.cells + data->cell;

					BOOL createPalette = hWndControl == data->hWndImportReplacePalette;
					BOOL dither = MessageBox(hWnd, L"Use dithering?", L"Dither", MB_ICONQUESTION | MB_YESNO) == IDYES;

					DWORD *palette = (DWORD *) calloc(nclr->nColors, 4);
					COLOR *nitroPalette = nclr->colors;
					int paletteSize = 1 << ncgr->nBits;
					for (int i = 0; i < nclr->nColors; i++) {
						palette[i] = ColorConvertFromDS(nitroPalette[i]);
					}

					int width, height;
					DWORD *pixels = gdipReadImage(path, &width, &height);

					//if we do not use an existing palette, generate one.
					if(createPalette){
						//create a palette, then encode them to the nclr
						createPalette_(pixels, width, height, palette, paletteSize);
						for (int i = 0; i < paletteSize; i++) {
							DWORD d = palette[i];
							COLOR ds = ColorConvertToDS(d);
							palette[i] = ColorConvertFromDS(ds);
						}
						//write out to each palette used by this cell
						for (int i = 0; i < cell->nAttribs; i++) {
							WORD *attr = cell->attr + (i * 3);
							int paletteIndex = attr[2] >> 12;
							COLOR *thisPalette = nitroPalette + (paletteIndex << ncgr->nBits);
							for (int j = 0; j < paletteSize; j++) {
								thisPalette[j] = ColorConvertToDS(palette[j]);
							}
						}
					}

					//for each OAM entry, match each pixel to a pixel of the image.
					int translateX = -256 + (cell->maxX + cell->minX) / 2, translateY = -128 + (cell->maxY + cell->minY) / 2;
					for (int i = 0; i < cell->nAttribs; i++) {
						NCER_CELL_INFO info;
						decodeAttributesEx(&info, cell, i);
						if (info.disable) continue;
						
						BYTE **characterBase = ncgr->tiles + NCGR_BOUNDARY(ncgr, info.characterName);
						int nCharsX = info.width / 8, nCharsY = info.height / 8;
						for (int cellY = 0; cellY < info.height; cellY++) {
							for (int cellX = 0; cellX < info.width; cellX++) {
								BYTE *character;
								if (NCGR_1D(ncgr->mappingMode)) {
									character = characterBase[cellX / 8 + nCharsX * (cellY / 8)];
								} else {
									character = characterBase[cellX / 8 + (cellY / 8) * ncgr->tilesX];
								}

								int totalWidth = info.width << info.doubleSize;
								int totalHeight = info.height << info.doubleSize;
								int padX = (totalWidth - info.width) / 2;
								int padY = (totalHeight - info.height) / 2;

								int x = (cellX + info.x + translateX + padX) & 0x1FF;
								int y = (cellY + info.y + translateY + padY) & 0xFF;
								
								//adjust x and y if the cell is flipped
								if (info.flipX) {
									x = (totalWidth - 1 - (x - info.x) + info.x) & 0x1FF;
								}
								if (info.flipY) {
									y = (totalHeight - 1 - (y - info.y) + info.y) & 0xFF;
								}

								if (x < width && y < height) {
									//pixel is in the image. 
									DWORD col = pixels[x + y * width];
									int _x = cellX % 8, _y = cellY % 8;
									if (col >> 24 > 0x80) {
										int closest = closestPalette(col, palette + (info.palette << ncgr->nBits) + 1, paletteSize - 1) + 1;
										character[_x + _y * 8] = closest;

										//diffuse
										if (dither) {
											DWORD chosen = palette[16 * info.palette + closest];
											int dr = (chosen & 0xFF) - (col & 0xFF);
											int dg = ((chosen >> 8) & 0xFF) - ((col >> 8) & 0xFF);
											int db = ((chosen >> 16) & 0xFF) - ((col >> 16) & 0xFF);
											doDiffuse(x + y * width, width, height, pixels, -dr, -dg, -db, 0, 1.0f);
										}
									} else {
										character[_x + _y * 8] = 0;
									}
								}
							}
						}
					}

					
					free(path);
					free(pixels);
					free(palette);
					InvalidateRect(hWndNclrViewer, NULL, FALSE);
					InvalidateRect(hWndNcgrViewer, NULL, FALSE);
					InvalidateRect(hWnd, NULL, FALSE);
					changed = 1;
				} else if (notification == BN_CLICKED && (
					hWndControl == data->hWnd8bpp || hWndControl == data->hWndDisable || hWndControl == data->hWndHFlip
					|| hWndControl == data->hWndVFlip || hWndControl == data->hWndMosaic || hWndControl == data->hWndRotateScale
					|| hWndControl == data->hWndDoubleSize)) {
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;

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
					WCHAR buffer[16];
					int len = SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) buffer);
					int x = _wtol(buffer);
					WORD *dest = data->ncer.cells[data->cell].attr + (data->oam * 3 + 1);
					*dest = (*dest & 0xFE00) | (x & 0x1FF);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == EN_CHANGE && hWndControl == data->hWndYInput) {
					WCHAR buffer[16];
					int len = SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) buffer);
					int y = _wtol(buffer);
					WORD *dest = data->ncer.cells[data->cell].attr + (data->oam * 3);
					*dest = (*dest & 0xFF00) | (y & 0xFF);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == EN_CHANGE && hWndControl == data->hWndMatrix) {
					WCHAR buffer[16];
					int len = SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) buffer);
					int mtx = _wtol(buffer);
					WORD *dest = data->ncer.cells[data->cell].attr + (data->oam * 3 + 1);
					WORD attr0 = dest[-1];
					if (attr0 & 0x100) {
						*dest = (*dest & 0xC1FF) | ((mtx & 0x1F) << 9);
						InvalidateRect(hWnd, NULL, FALSE);
					}
				} else if (notification == BN_CLICKED && hWndControl == data->hWndOamRemove) {
					NCER_CELL *cell = data->ncer.cells + data->cell;

					//don't remove an entry if it's the only entry!
					if (cell->nAttribs > 1) {
						memmove(cell->attr + (data->oam * 3), cell->attr + ((data->oam + 1) * 3), (cell->nAttribs - data->oam - 1) * 6);
						cell->nAttr -= 3;
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
					cell->nAttr += 3;
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
					cell->nAttr = 3;
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
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
					data->showCellBounds = state;
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
				}

				//log a change
				if (changed) {
					NCER copy;
					ncerCreateCopy(&copy, &data->ncer);
					undoAdd(&data->undo, &copy);
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
						ncerWriteFile(&data->ncer, data->szOpenFile);
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
						int width, height;

						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
						HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
						HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;

						NCGR *ncgr = NULL;
						NCLR *nclr = NULL;
						NCER *ncer = &data->ncer;
						NCER_CELL *cell = ncer->cells + data->cell;
						NCER_CELL_INFO info;
						decodeAttributesEx(&info, cell, data->oam);

						if (hWndNclrViewer) {
							NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
							nclr = &nclrViewerData->nclr;
						}
						if (hWndNcgrViewer) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
							ncgr = &ncgrViewerData->ncgr;
						}

						int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
						COLOR32 *bits = ncerRenderWholeCell(cell, ncgr, nclr, translateX, translateY, &width, &height, 0, -1);
						for (int i = 0; i < width * height; i++) {
							COLOR32 c = bits[i];
							bits[i] = REVERSE(c);
						}

						writeImage(bits, width, height, location);
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
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			nitroPaintStruct->hWndNcerViewer = NULL;
			if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
			undoDestroy(&data->undo);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNcerViewerClass(VOID) {
	EditorRegister(L"NcerViewerClass", NcerViewerWndProc, L"Cell Editor", sizeof(NCERVIEWERDATA));
}

HWND CreateNcerViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NCER ncer;
	if (ncerReadFile(&ncer, path)) {
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
