#include <Windows.h>
#include <CommCtrl.h>
#include <math.h>

#include "editor.h"
#include "nscrviewer.h"
#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "childwindow.h"
#include "resource.h"
#include "nitropaint.h"
#include "nscr.h"
#include "bggen.h"
#include "gdip.h"
#include "palette.h"
#include "tiler.h"

#include "preview.h"

extern HICON g_appIcon;

DWORD *renderNscrBits(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int *width, int *height, int tileMarks, int hlStart, int hlEnd, int hlMode, int selStartX, int selStartY, int selEndX, int selEndY, BOOL transparent) {
	int bWidth = nscr->nWidth;
	int bHeight = nscr->nHeight;
	*width = bWidth;
	*height = bHeight;

	LPDWORD bits = (LPDWORD) calloc(bWidth * bHeight, 4);

	int tilesX = nscr->nWidth >> 3;
	int tilesY = nscr->nHeight >> 3;

	int selX = min(selStartX, selEndX);
	int selY = min(selStartY, selEndY);
	int selRight = max(selStartX, selEndX);
	int selBottom = max(selStartY, selEndY);

	DWORD block[64];

	for (int y = 0; y < tilesY; y++) {
		int offsetY = y << 3;
		for (int x = 0; x < tilesX; x++) {
			int offsetX = x << 3;

			int tileNo = -1;
			nscrGetTileEx(nscr, ncgr, nclr, tileBase, x, y, block, &tileNo, transparent);
			DWORD dwDest = x * 8 + y * 8 * bWidth;

			if (tileMarks != -1 && tileMarks == tileNo) {
				for (int i = 0; i < 64; i++) {
					DWORD d = block[i];
					int b = (d >> 0) & 0xFF;
					int g = (d >> 8) & 0xFF;
					int r = (d >> 16) & 0xFF;
					int a = (d >> 24) & 0xFF;

					if (a) { //effect on opaque pixels
						r = (r + 0) >> 1;
						g = (g + 255) >> 1;
						b = (b + 255) >> 1;
					} else { //on transparent pixels
						r = 0;
						g = b = 255;
						a = 128;
					}
					block[i] = (a << 24) | b | (g << 8) | (r << 16);
				}
			}

			//highlight selection
			if (x >= selX && y >= selY && x <= selRight && y <= selBottom) {
				for (int i = 0; i < 64; i++) {
					DWORD d = block[i];
					int b = d & 0xFF;
					int g = (d >> 8) & 0xFF;
					int r = (d >> 16) & 0xFF;
					int a = (d >> 24) & 0xFF;

					if (a) { //effect on opaque pixels
						r = (r * 3 + 255) >> 2;
						g = (g * 3 + 255) >> 2;
						b = (b * 3 + 0) >> 2;
					} else { //effect on transparent pixels
						r = g = 255;
						b = 0;
						a = 64;
					}
					block[i] = (a << 24) | b | (g << 8) | (r << 16);
				}
				//highlight edges for better visibility
				if (x == selX || x == selRight) {
					if (x == selX) for (int i = 0; i < 8; i++) block[i * 8] = 0xFFFFFF00;
					if (x == selRight) for (int i = 0; i < 8; i++) block[i * 8 + 7] = 0xFFFFFF00;
				}
				if (y == selY || y == selBottom) {
					if (y == selY) for (int i = 0; i < 8; i++) block[i] = 0xFFFFFF00;
					if (y == selBottom) for (int i = 0; i < 8; i++) block[i + 7 * 8] = 0xFFFFFF00;
				}
			}

			if (hlStart != -1 && hlEnd != -1) {
				uint16_t nscrData = nscr->data[x + y * (nscr->nWidth / 8)];
				int flip = (nscrData >> 10) & 3;
				int palno = (nscrData >> 12) & 0xF;

				int charBase = tileBase;
				int tileIndex = nscrData & 0x3FF;
				if (tileIndex - charBase >= 0) {
					BYTE *tile = ncgr->tiles[tileIndex - charBase];
					for (int i = 0; i < 64; i++) {
						int bIndex = i;
						if (flip & TILE_FLIPX) bIndex ^= 7;
						if (flip & TILE_FLIPY) bIndex ^= 7 << 3;

						int cindex = tile[bIndex] + (palno << ncgr->nBits);
						if (PalViewerIndexInRange(cindex, hlStart, hlEnd, hlMode == PALVIEWER_SELMODE_2D)) {
							DWORD col = block[i];
							int lightness = (col & 0xFF) + ((col >> 8) & 0xFF) + ((col >> 16) & 0xFF);
							if (lightness < 383) block[i] = 0xFFFFFFFF;
							else block[i] = 0xFF000000;
						}
					}
				}
			}

			for (int i = 0; i < 8; i++) {
				memcpy(bits + dwDest + i * bWidth, block + (i << 3), 32);
			}
		}
	}

	return bits;
}

unsigned char *renderNscrIndexed(NSCR *nscr, NCGR *ncgr, int tileBase, int *width, int *height, BOOL transparent) {
	int bWidth = nscr->nWidth;
	int bHeight = nscr->nHeight;
	*width = bWidth;
	*height = bHeight;

	unsigned char *bits = (unsigned char *) calloc(bWidth * bHeight, 1);

	int tilesX = nscr->nWidth >> 3;
	int tilesY = nscr->nHeight >> 3;

	unsigned char block[64];
	for (int y = 0; y < tilesY; y++) {
		int offsetY = y * 8;
		for (int x = 0; x < tilesX; x++) {
			int offsetX = x * 8;

			//fetch screen info
			uint16_t scr = nscr->data[x + y * tilesX];
			int chr = (scr & 0x3FF) - tileBase;
			int flip = scr >> 10;
			int pal = scr >> 12;

			if (chr >= 0 && chr < ncgr->nTiles) {
				unsigned char *chrData = ncgr->tiles[chr];
				for (int i = 0; i < 64; i++) {
					int tileX = i % 8;
					int tileY = i / 8;
					if (flip & TILE_FLIPX) tileX ^= 7;
					if (flip & TILE_FLIPY) tileY ^= 7;
					block[i] = chrData[tileX + tileY * 8] | (pal << 4);
				}
			} else {
				memset(block, 0, sizeof(block));
			}
			
			int destOffset = x * 8 + y * 8 * tilesX * 8;
			for (int i = 0; i < 8; i++) {
				memcpy(bits + destOffset + i * bWidth, block + i * 8, 8);
			}
		}
	}

	return bits;
}

HBITMAP renderNscr(NSCR *renderNscr, NCGR *renderNcgr, NCLR *renderNclr, int tileBase, BOOL drawGrid, int *width, int *height, int highlightNclr, int highlightTile, int hlStart, int hlEnd, int hlMode, int scale, int selStartX, int selStartY, int selEndX, int selEndY, BOOL transparent) {
	if (renderNcgr == NULL) return NULL;

	if (highlightNclr != -1) highlightNclr += tileBase;
	DWORD *bits = renderNscrBits(renderNscr, renderNcgr, renderNclr, tileBase, width, height, highlightNclr, hlStart, hlEnd, hlMode, selStartX, selStartY, selEndX, selEndY, transparent);

	int hovX = -1, hovY = -1;
	if (highlightTile != -1) {
		hovX = highlightTile % (renderNscr->nWidth / 8);
		hovY = highlightTile / (renderNscr->nWidth / 8);
	}
	HBITMAP hBitmap = CreateTileBitmap2(bits, *width, *height, hovX, hovY, width, height, scale, drawGrid, 8, FALSE, TRUE);
	free(bits);
	return hBitmap;
}

static void ScrViewerPaint(HWND hWnd, HDC hWindowDC, int xOffs, int yOffs) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWndEditor);

	NSCR *nscr = &data->nscr;
	NCGR *ncgr = NULL;
	NCLR *nclr = NULL;

	HWND hWndMain = getMainWindow(hWndEditor);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
	if (hWndNclrViewer != NULL) {
		nclr = (NCLR *) EditorGetObject(hWndNclrViewer);
	}

	int hoveredNcgrTile = -1, hoveredNscrTile = -1;
	if (data->hoverX != -1 && data->hoverY != -1) {
		hoveredNscrTile = data->hoverX + data->hoverY * (nscr->nWidth / 8);
	}

	HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
	if (hWndNcgrViewer != NULL) {
		NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer);
		ncgr = &ncgrViewerData->ncgr;
		hoveredNcgrTile = ncgrViewerData->hoverIndex;
	}

	int hlStart = data->hlStart;
	int hlEnd = data->hlEnd;
	int hlMode = data->hlMode;
	if ((data->verifyFrames & 1) == 0) {
		//animate selection
		hlStart = hlEnd = -1;
	}

	int bitmapWidth = getDimension(nscr->nWidth / 8, data->showBorders, data->scale);
	int bitmapHeight = getDimension(nscr->nHeight / 8, data->showBorders, data->scale);

	if (ncgr != NULL) {
		int tileBase = data->tileBase;
		int selStartX = data->selStartX, selEndX = data->selEndX;
		int selStartY = data->selStartY, selEndY = data->selEndY;

		int outWidth, outHeight;
		if (hoveredNcgrTile != -1) hoveredNcgrTile += tileBase;
		DWORD *bits = renderNscrBits(nscr, ncgr, nclr, tileBase, &outWidth, &outHeight, hoveredNcgrTile, hlStart, hlEnd, hlMode, 
			selStartX, selStartY, selEndX, selEndY, data->transparent);

		int hovX = -1, hovY = -1;
		if (hoveredNscrTile != -1) {
			hovX = hoveredNscrTile % (nscr->nWidth / 8);
			hovY = hoveredNscrTile / (nscr->nWidth / 8);
		}

		FbSetSize(&data->fb, bitmapWidth, bitmapHeight);
		RenderTileBitmap(data->fb.px, bitmapWidth, bitmapHeight, xOffs, yOffs, bitmapWidth - xOffs, bitmapHeight - yOffs,
			bits, outWidth, outHeight, hovX, hovY, data->scale, data->showBorders, 8, FALSE, TRUE);
		FbDraw(&data->fb, hWindowDC, 0, 0, bitmapWidth - xOffs, bitmapHeight - yOffs, xOffs, yOffs);

		free(bits);
	}

}

static void ScrViewerCopy(NSCRVIEWERDATA *data) {
	OpenClipboard(data->hWnd);
	EmptyClipboard();

	//clipboard format: "0000", "N", w, h, d
	int tileX = data->contextHoverX;
	int tileY = data->contextHoverY;
	int tilesX = 1, tilesY = 1;
	if (data->selStartX != -1 && data->selStartY != -1 && data->selEndX != -1 && data->selEndY != -1) {
		tileX = min(data->selStartX, data->selEndX);
		tileY = min(data->selStartY, data->selEndY);
		tilesX = max(data->selStartX, data->selEndX) + 1 - tileX;
		tilesY = max(data->selStartY, data->selEndY) + 1 - tileY;
	}
	HANDLE hString = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 10 + 4 * tilesX * tilesY);
	LPSTR clip = (LPSTR) GlobalLock(hString);
	*(DWORD *) clip = 0x30303030;
	clip[4] = 'N';
	clip[5] = (tilesX & 0xF) + 0x30;
	clip[6] = ((tilesX >> 4) & 0xF) + 0x30;
	clip[7] = (tilesY & 0xF) + 0x30;
	clip[8] = ((tilesY >> 4) & 0xF) + 0x30;

	int i = 0;
	for (int y = tileY; y < tileY + tilesY; y++) {
		for (int x = tileX; x < tileX + tilesX; x++) {
			WORD d = data->nscr.data[x + y * (data->nscr.nWidth / 8)];
			clip[9 + i * 4] = (d & 0xF) + 0x30;
			clip[9 + i * 4 + 1] = ((d >> 4) & 0xF) + 0x30;
			clip[9 + i * 4 + 2] = ((d >> 8) & 0xF) + 0x30;
			clip[9 + i * 4 + 3] = ((d >> 12) & 0xF) + 0x30;
			i++;
		}
	}

	HWND hWndNclrEditor = NULL, hWndNcgrEditor = NULL;
	HWND hWndMain = getMainWindow(data->hWnd);
	GetAllEditors(hWndMain, FILE_TYPE_PALETTE, &hWndNclrEditor, 1);
	GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, &hWndNcgrEditor, 1);

	COLOR32 *bm = NULL;
	if (hWndNclrEditor != NULL && hWndNcgrEditor != NULL) {
		//to bitmap
		NSCR *nscr = &data->nscr;
		NCGR *ncgr = &((NCGRVIEWERDATA *) EditorGetData(hWndNcgrEditor))->ncgr;
		NCLR *nclr = &((NCLRVIEWERDATA *) EditorGetData(hWndNclrEditor))->nclr;

		//cut selection region out of the image
		int wholeWidth, wholeHeight, width = tilesX * 8, height = tilesY * 8;
		COLOR32 *bm = renderNscrBits(nscr, ncgr, nclr, data->tileBase, &wholeWidth, &wholeHeight,
			-1, -1, -1, 0, -1, -1, -1, -1, TRUE);
		COLOR32 *sub = ImgCrop(bm, wholeWidth, wholeHeight, tileX * 8, tileY * 8, tilesX * 8, tilesY * 8);
		ImgSwapRedBlue(sub, tilesX * 8, tilesY * 8);
		copyBitmap(sub, width, height);
		free(bm);
		free(sub);
	}

	GlobalUnlock(hString);
	SetClipboardData(CF_TEXT, hString);
	CloseClipboard();
}

static void ScrViewerErase(NSCRVIEWERDATA *data) {
	for (int y = min(data->selStartY, data->selEndY); y <= max(data->selStartY, data->selEndY); y++) {
		for (int x = min(data->selStartX, data->selEndX); x <= max(data->selStartX, data->selEndX); x++) {
			data->nscr.data[x + y * (data->nscr.nWidth / 8)] = data->nscr.clearValue;
		}
	}
}

void NscrViewerSetTileBase(HWND hWnd, int tileBase) {
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	data->tileBase = tileBase;
	WCHAR buffer[16];
	int len = wsprintfW(buffer, L"%d", tileBase);
	SendMessage(data->hWndTileBase, WM_SETTEXT, len, (LPARAM) buffer);
}

LRESULT WINAPI NscrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
		{
			data->showBorders = 0;
			data->scale = 1;
			data->selStartX = -1;
			data->selStartY = -1;
			data->selEndX = -1;
			data->selEndY = -1;
			data->hoverX = -1;
			data->hoverY = -1;
			data->transparent = g_configuration.renderTransparent;

			data->hWndPreview = CreateWindow(L"NscrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 300, 300, hWnd, NULL, NULL, NULL);
			//Character: \n Palette: 
			data->hWndCharacterLabel = CreateCheckbox(hWnd, L"Character:", 0, 0, 0, 0, TRUE);
			data->hWndPaletteLabel = CreateCheckbox(hWnd, L"Palette:", 0, 0, 0, 0, TRUE);
			data->hWndCharacterNumber = CreateEdit(hWnd, L"0", 0, 0, 0, 0, TRUE);
			data->hWndPaletteNumber = CreateCombobox(hWnd, NULL, 0, 0, 0, 0, 0, 0);
			data->hWndApply = CreateButton(hWnd, L"Apply", 0, 0, 0, 0, TRUE);
			data->hWndAdd = CreateButton(hWnd, L"Add", 0, 0, 0, 0, FALSE);
			data->hWndSubtract = CreateButton(hWnd, L"Subtract", 0, 0, 0, 0, FALSE);
			data->hWndTileBaseLabel = CreateStatic(hWnd, L"Tile Base:", 0, 0, 0, 0);
			data->hWndTileBase = CreateEdit(hWnd, L"0", 0, 0, 0, 0, TRUE);
			data->hWndSize = CreateStatic(hWnd, L"Size: 0x0", 0, 0, 0, 0);
			data->hWndSelectionSize = CreateStatic(hWnd, L"", 0, 0, 0, 0);
			WCHAR bf[16];
			for (int i = 0; i < 16; i++) {
				wsprintf(bf, L"Palette %02d", i);
				SendMessage(data->hWndPaletteNumber, CB_ADDSTRING, (WPARAM) wcslen(bf), (LPARAM) bf);
			}
			SendMessage(data->hWndPaletteNumber, CB_SETCURSEL, 0, 0);

			//read config data
			if (g_configuration.nscrViewerConfiguration.gridlines) {
				data->showBorders = 1;
				CheckMenuItem(GetMenu((HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT)), ID_VIEW_GRIDLINES, MF_CHECKED);
			}
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			MoveWindow(data->hWndPreview, 0, 0, rcClient.right - 200, rcClient.bottom - 22, TRUE);

			MoveWindow(data->hWndCharacterLabel, rcClient.right - 190, 10, 70, 22, TRUE);
			MoveWindow(data->hWndPaletteLabel, rcClient.right - 190, 37, 70, 22, TRUE);
			MoveWindow(data->hWndCharacterNumber, rcClient.right - 110, 10, 100, 22, TRUE);
			MoveWindow(data->hWndPaletteNumber, rcClient.right - 110, 37, 100, 100, TRUE);
			MoveWindow(data->hWndApply, rcClient.right - 110, 64, 100, 22, TRUE);
			MoveWindow(data->hWndAdd, rcClient.right - 110, 91, 100, 22, TRUE);
			MoveWindow(data->hWndSubtract, rcClient.right - 110, 118, 100, 22, TRUE);
			MoveWindow(data->hWndTileBaseLabel, 0, rcClient.bottom - 22, 50, 22, TRUE);
			MoveWindow(data->hWndTileBase, 50, rcClient.bottom - 22, 100, 22, TRUE);
			MoveWindow(data->hWndSize, 160, rcClient.bottom - 22, 100, 22, TRUE);
			MoveWindow(data->hWndSelectionSize, 260, rcClient.bottom - 22, 100, 22, TRUE);

			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
		case WM_PAINT:
		{
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		}
		case NV_INITIALIZE:
		case NV_INITIALIZE_IMMEDIATE:
		{
			if (msg == NV_INITIALIZE) {
				LPWSTR path = (LPWSTR) wParam;
				memcpy(&data->nscr, (NSCR *) lParam, sizeof(NSCR));
				EditorSetFile(hWnd, path);
			} else {
				NSCR *nscr = (NSCR *) wParam;
				memcpy(&data->nscr, nscr, sizeof(NSCR));
			}
			PreviewLoadBgScreen(&data->nscr);

			data->frameData.contentWidth = getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale);
			data->frameData.contentHeight = getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale);
			FbCreate(&data->fb, hWnd, data->frameData.contentWidth, data->frameData.contentHeight);

			RECT rc = { 0 };
			rc.right = data->frameData.contentWidth;
			rc.bottom = data->frameData.contentHeight;
			AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
			int width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL) + 200; //+4 to account for WS_EX_CLIENTEDGE
			int height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL) + 22;
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

			//guess a tile base based on an open NCGR (if any)
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
			if (hWndNcgrViewer) {
				NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
				int nTiles = ncgrViewerData->ncgr.nTiles;
				if (data->nscr.nHighestIndex >= nTiles) {
					NscrViewerSetTileBase(hWnd, data->nscr.nHighestIndex + 1 - nTiles);
				}
			}

			//set size label
			WCHAR buffer[32];
			int len = wsprintfW(buffer, L"Size: %dx%d", data->nscr.nWidth, data->nscr.nHeight);
			SendMessage(data->hWndSize, WM_SETTEXT, len, (LPARAM) buffer);
			return 1;
		}
		case NV_UPDATEPREVIEW:
			PreviewLoadBgScreen(&data->nscr);
			break;
		case WM_KEYDOWN:
		{
			int cc = wParam;
			switch (cc) {
				case VK_DELETE:
					//delete
					ScrViewerErase(data);
					InvalidateRect(hWnd, NULL, FALSE);
					SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
					break;
				case VK_ESCAPE:
					SendMessage(hWnd, WM_COMMAND, ID_NSCRMENU_DESELECT, 0);
					break;
			}
			break;
		}
		case WM_COMMAND:
		{
			if (lParam == 0 && HIWORD(wParam) == 1) {
				//accelerator
				WORD accel = LOWORD(wParam);
				switch (accel) {
					case ID_ACCELERATOR_CUT:
						PostMessage(hWnd, WM_COMMAND, ID_NSCRMENU_CUT, 0);
						break;
					case ID_ACCELERATOR_COPY:
						PostMessage(hWnd, WM_COMMAND, ID_NSCRMENU_COPY, 0);
						break;
					case ID_ACCELERATOR_PASTE:
						PostMessage(hWnd, WM_COMMAND, ID_NSCRMENU_PASTE, 0);
						break;
					case ID_ACCELERATOR_DESELECT:
						PostMessage(hWnd, WM_COMMAND, ID_NSCRMENU_DESELECT, 0);
						break;
					case ID_ACCELERATOR_SELECT_ALL:
						data->selStartX = data->selStartY = 0;
						data->selEndX = data->nscr.nWidth / 8 - 1;
						data->selEndY = data->nscr.nHeight / 8 - 1;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
				}
			}
			if (HIWORD(wParam) == 0 && lParam == 0) {
				switch (LOWORD(wParam)) {
					case ID_FILE_EXPORT:
					{
						LPWSTR location = saveFileDialog(hWnd, L"Save Bitmap", L"PNG Files (*.png)\0*.png\0All Files\0*.*\0", L"png");
						if (!location) break;
						int width, height;

						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
						HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
						HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
						HWND hWndNscrViewer = hWnd;

						NCGR *ncgr = NULL;
						NCLR *nclr = NULL;
						NSCR *nscr = NULL;

						if (hWndNclrViewer) {
							NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
							nclr = &nclrViewerData->nclr;
						}
						if (hWndNcgrViewer) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
							ncgr = &ncgrViewerData->ncgr;
						}
						nscr = &data->nscr;

						//check: should we output indexed? If palette size > 256, we can't
						if (nclr->nColors <= 256) {
							//write 8bpp indexed
							COLOR32 palette[256] = { 0 };
							int transparentOutput = 1;
							int paletteSize = 1 << ncgr->nBits;
							if (nclr != NULL) {
								for (int i = 0; i < min(nclr->nColors, 256); i++) {
									int makeTransparent = transparentOutput && ((i % paletteSize) == 0);

									palette[i] = ColorConvertFromDS(nclr->colors[i]);
									if (!makeTransparent) palette[i] |= 0xFF000000;
								}
							}
							unsigned char *bits = renderNscrIndexed(nscr, ncgr, data->tileBase, &width, &height, TRUE);
							ImgWriteIndexed(bits, width, height, palette, 256, location);
							free(bits);
						} else {
							//write direct
							COLOR32 *bits = renderNscrBits(nscr, ncgr, nclr, data->tileBase, &width, &height, -1, -1, -1, 0, -1, -1, -1, -1, TRUE);
							for (int i = 0; i < width * height; i++) {
								COLOR32 c = bits[i];
								bits[i] = REVERSE(c);
							}
							ImgWrite(bits, width, height, location);
							free(bits);
						}
						free(location);
						break;
					}
					case ID_NSCRMENU_FLIPHORIZONTALLY:
					{
						int selStartX = min(data->selStartX, data->selEndX), selEndX = max(data->selStartX, data->selEndX);
						int selStartY = min(data->selStartY, data->selEndY), selEndY = max(data->selStartY, data->selEndY);
						int selWidth = selEndX + 1 - selStartX;
						int selHeight = selEndY + 1 - selStartY;
						if (selWidth == 1 && selHeight == 1) {
							int tileNo = data->contextHoverX + data->contextHoverY * (data->nscr.nWidth >> 3);
							WORD oldVal = data->nscr.data[tileNo];
							oldVal ^= (TILE_FLIPX << 10);
							data->nscr.data[tileNo] = oldVal;
						} else if (selStartX != -1 && selStartY != -1) {
							//for each row
							for (int y = selStartY; y < selStartY + selHeight; y++) {
								//for width/2
								for (int x = 0; x < (selWidth + 1) / 2; x++) {
									//swap x with selWidth-1-x
									int t1 = x + selStartX, t2 = selWidth - 1 - x + selStartX;
									WORD d1 = data->nscr.data[t1 + y * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPX << 10);
									WORD d2 = data->nscr.data[t2 + y * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPX << 10);
									data->nscr.data[t1 + y * (data->nscr.nWidth >> 3)] = d2;
									if(x != selWidth) data->nscr.data[t2 + y * (data->nscr.nWidth >> 3)] = d1;
								}
							}
						}
						SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_NSCRMENU_FLIPVERTICALLY:
					{
						int selStartX = min(data->selStartX, data->selEndX), selEndX = max(data->selStartX, data->selEndX);
						int selStartY = min(data->selStartY, data->selEndY), selEndY = max(data->selStartY, data->selEndY);
						int selWidth = selEndX + 1 - selStartX;
						int selHeight = selEndY + 1 - selStartY;
						if (selWidth == 1 && selHeight == 1) {
							int tileNo = data->contextHoverX + data->contextHoverY * (data->nscr.nWidth >> 3);
							WORD oldVal = data->nscr.data[tileNo];
							oldVal ^= (TILE_FLIPY << 10);
							data->nscr.data[tileNo] = oldVal;
						} else if (selStartX != -1 && selStartY != -1) {
							//for every column
							for (int x = selStartX; x < selStartX + selWidth; x++) {
								//for every row/2
								for (int y = 0; y < (selHeight + 1) / 2; y++) {
									int t1 = y + selStartY, t2 = selHeight - 1 - y + selStartY;
									WORD d1 = data->nscr.data[x + t1 * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPY << 10);
									WORD d2 = data->nscr.data[x + t2 * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPY << 10);
									data->nscr.data[x + t1 * (data->nscr.nWidth >> 3)] = d2;
									if(y != selHeight) data->nscr.data[x + t2 * (data->nscr.nWidth >> 3)] = d1;
								}
							}
						}
						InvalidateRect(hWnd, NULL, FALSE);
						SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
						break;
					}
					case ID_NSCRMENU_MAKEIDENTITY:
					{
						//each element use palette 0, increasing char index
						int selStartX = min(data->selStartX, data->selEndX), selEndX = max(data->selStartX, data->selEndX);
						int selStartY = min(data->selStartY, data->selEndY), selEndY = max(data->selStartY, data->selEndY);
						int selWidth = selEndX + 1 - selStartX;
						int selHeight = selEndY + 1 - selStartY;

						int index = 0;
						//for every row
						for (int y = 0; y < selHeight; y++) {
							//for every column
							for (int x = selStartX; x < selStartX + selWidth; x++) {
								data->nscr.data[x + (y + selStartY) * (data->nscr.nWidth >> 3)] = index & 0x3FF;
								index++;
							}
						}
						InvalidateRect(hWnd, NULL, FALSE);
						SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
						break;
					}
					case ID_FILE_SAVEAS:
						EditorSaveAs(hWnd);
						break;
					case ID_FILE_SAVE:
						EditorSave(hWnd);
						break;
					case ID_NSCRMENU_IMPORTBITMAPHERE:
					{
						HWND hWndMain = getMainWindow(hWnd);
						HWND h = CreateWindow(L"NscrBitmapImportClass", L"Import Bitmap", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, hWndMain, NULL, NULL, NULL);
						SendMessage(h, NV_INITIALIZE, 0, (LPARAM) hWnd);
						WORD d = data->nscr.data[data->contextHoverX + data->contextHoverY * (data->nscr.nWidth >> 3)];
						SendMessage(h, NV_INITIMPORTDIALOG, d, data->contextHoverX | (data->contextHoverY << 16));
						ShowWindow(h, SW_SHOW);
						break;
					}
					case ID_NSCRMENU_COPY:
						ScrViewerCopy(data);
						break;
					case ID_NSCRMENU_PASTE:
					{
						OpenClipboard(hWnd);
						HANDLE hString = GetClipboardData(CF_TEXT);
						CloseClipboard();
						LPSTR clip = GlobalLock(hString);

						int tileX = data->contextHoverX;
						int tileY = data->contextHoverY;

						if(*(DWORD *) clip == 0x30303030 && clip[4] == 'N'){
							int tilesX = (clip[5] & 0xF) | ((clip[6] & 0xF) << 4);
							int tilesY = (clip[7] & 0xF) | ((clip[8] & 0xF) << 4);
							clip += 9; //advance info part

							int minX = 0, minY = 0, maxX = data->nscr.nWidth / 8, maxY = data->nscr.nHeight / 8;
							if (data->selStartX != -1 && data->selStartY != -1) {
								int sminX = min(data->selStartX, data->selEndX);
								int smaxX = max(data->selStartX, data->selEndX);
								int sminY = min(data->selStartY, data->selEndY);
								int smaxY = max(data->selStartY, data->selEndY);
								if (tileX >= sminX && tileX <= smaxX && tileY >= sminY && tileY <= smaxY) {
									minX = sminX;
									minY = sminY;
									maxX = smaxX;
									maxY = smaxY;
								}
							}

							for (int y = tileY; y < tileY + tilesY; y++) {
								for (int x = tileX; x < tileX + tilesX; x++) {
									uint16_t d = (clip[0] & 0xF) | ((clip[1] & 0xF) << 4) | ((clip[2] & 0xF) << 8) | ((clip[3] & 0xF) << 12);
									if (x < (int) (data->nscr.nWidth / 8) && y < (int) (data->nscr.nHeight / 8)) {
										if (x >= minX && x <= maxX && y >= minY && y <= maxY) data->nscr.data[x + y * (data->nscr.nWidth / 8)] = d;
									}

									clip += 4;
								}
							}
							InvalidateRect(hWnd, NULL, FALSE);
							SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
						}

						GlobalUnlock(hString);
						break;
					}
					case ID_NSCRMENU_DESELECT:
						data->selStartX = -1;
						data->selStartY = -1;
						data->selEndX = -1;
						data->selEndY = -1;
						InvalidateRect(hWnd, NULL, FALSE);
						SendMessage(data->hWndSelectionSize, WM_SETTEXT, 0, (LPARAM) L"");
						break;
					case ID_NSCRMENU_CUT:
						ScrViewerCopy(data);
						ScrViewerErase(data);
						SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					case ID_VIEW_GRIDLINES:
					case ID_ZOOM_100:
					case ID_ZOOM_200:
					case ID_ZOOM_400:
					case ID_ZOOM_800:
						SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
						RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
						break;
				}
			} else if (lParam) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->hWndApply || hWndControl == data->hWndAdd || hWndControl == data->hWndSubtract) {
					//get data to overwrite
					int writePalette = GetCheckboxChecked(data->hWndPaletteLabel);
					int writeCharacter = GetCheckboxChecked(data->hWndCharacterLabel);

					WCHAR bf[16];
					SendMessage(data->hWndCharacterNumber, WM_GETTEXT, 15, (LPARAM) bf);
					int character = _wtoi(bf);
					int palette = SendMessage(data->hWndPaletteNumber, CB_GETCURSEL, 0, 0);
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					SendMessage(hWnd, NV_SETDATA, (WPARAM) character, (LPARAM) palette);
					int tilesX = data->nscr.nWidth / 8, tilesY = data->nscr.nHeight / 8;
					int nTiles = tilesX * tilesY;

					int xMin = min(data->selStartX, data->selEndX), xMax = max(data->selStartX, data->selEndX);
					int yMin = min(data->selStartY, data->selEndY), yMax = max(data->selStartY, data->selEndY);
					for (int i = 0; i < nTiles; i++) {
						int x = i % tilesX, y = i / tilesX;
						if (x >= xMin && y >= yMin && x <= xMax && y <= yMax) {
							uint16_t value = data->nscr.data[i];
							int newPalette = (value >> 12) & 0xF;
							int newCharacter = value & 0x3FF;

							if (hWndControl == data->hWndAdd) {
								newPalette = (newPalette + palette) & 0xF;
								newCharacter = (newCharacter + character) & 0x3FF;
							} else if (hWndControl == data->hWndSubtract) {
								newPalette = (newPalette - palette) & 0xF;
								newCharacter = (newCharacter - character) & 0x3FF;
							} else {
								newCharacter = character;
								newPalette = palette;
							}

							//write data we're told to
							if (writePalette) {
								value = (value & 0x0FFF) | (newPalette << 12);
							}
							if (writeCharacter) {
								value = (value & 0xFC00) | newCharacter;
							}

							data->nscr.data[i] = value;
						}
					}
					SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (hWndControl == data->hWndTileBase) {
					WORD command = HIWORD(wParam);
					if (command == EN_CHANGE) {
						WCHAR bf[16];
						SendMessage(hWndControl, WM_GETTEXT, 15, (LPARAM) bf);
						int base = _wtol(bf);
						if (base != data->tileBase) {
							data->tileBase = base;
							InvalidateRect(hWnd, NULL, FALSE);
						}
					}
				}
			}
			break;
		}
		case WM_TIMER:
		{
			if (wParam == 1) {
				data->verifyFrames--;
				if (!data->verifyFrames) {
					KillTimer(hWnd, wParam);
				}
				InvalidateRect(hWnd, NULL, FALSE);
				return 0;
			}
			break;
		}
		case WM_LBUTTONDOWN:
		{
			data->selStartX = -1;
			data->selStartY = -1;
			data->selEndX = -1;
			data->selEndY = -1;
			InvalidateRect(hWnd, NULL, FALSE);
			SendMessage(data->hWndSelectionSize, WM_SETTEXT, 0, (LPARAM) L"");
			break;
		}
		case WM_DESTROY:
			FbDestroy(&data->fb);
			break;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

typedef struct {
	HWND hWndEditor;
	HWND hWndBitmapName;
	HWND hWndBrowseButton;
	HWND hWndPaletteInput;
	HWND hWndPalettesInput;
	HWND hWndImportButton;
	HWND hWndDitherCheckbox;
	HWND hWndDiffuseAmount;
	HWND hWndNewPaletteCheckbox;
	HWND hWndNewCharactersCheckbox;
	HWND hWndWriteScreenCheckbox;
	HWND hWndWriteCharIndicesCheckbox;
	HWND hWndPaletteSize;
	HWND hWndPaletteOffset;
	HWND hWndCharacterBase;
	HWND hWndCharacterCount;
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;


	int nscrTileX;
	int nscrTileY;
	int characterOrigin;
} NSCRBITMAPIMPORTDATA;


typedef struct {
	NCLR *nclr;
	NCGR *ncgr;
	NSCR *nscr;
	DWORD *px;
	int width;
	int height;
	int tileBase;
	int nPalettes;
	int paletteNumber;
	int paletteSize;
	int paletteOffset;
	int newPalettes;
	int newCharacters;
	int charBase;
	int nMaxChars;
	int dither;
	float diffuse;
	int maxTilesX;
	int maxTilesY;
	int nscrTileX;
	int nscrTileY;
	int balance;
	int colorBalance;
	int enhanceColors;
	int writeScreen;
	int writeCharacterIndices;
	HWND hWndNclrViewer;
	HWND hWndNcgrViewer;
	HWND hWndNscrViewer;
} NSCRIMPORTDATA;

void nscrImportCallback(void *data) {
	NSCRIMPORTDATA *importData = (NSCRIMPORTDATA *) data;

	InvalidateRect(importData->hWndNclrViewer, NULL, FALSE);
	InvalidateRect(importData->hWndNcgrViewer, NULL, FALSE);
	InvalidateRect(importData->hWndNscrViewer, NULL, FALSE);
	SendMessage(importData->hWndNclrViewer, NV_UPDATEPREVIEW, 0, 0);
	SendMessage(importData->hWndNcgrViewer, NV_UPDATEPREVIEW, 0, 0);
	SendMessage(importData->hWndNscrViewer, NV_UPDATEPREVIEW, 0, 0);
	free(importData->px);
	free(data);
}

DWORD WINAPI threadedNscrImportBitmapInternal(LPVOID lpParameter) {
	PROGRESSDATA *progressData = (PROGRESSDATA *) lpParameter;
	NSCRIMPORTDATA *importData = (NSCRIMPORTDATA *) progressData->data;
	BgReplaceSection(importData->nclr, importData->ncgr, importData->nscr, importData->px, importData->width, importData->height,
					 importData->writeScreen, importData->writeCharacterIndices,
					 importData->tileBase, importData->nPalettes, importData->paletteNumber,
					 importData->paletteOffset, importData->paletteSize,
					 importData->newPalettes, importData->charBase, importData->nMaxChars,
					 importData->newCharacters, importData->dither, importData->diffuse,
					 importData->maxTilesX, importData->maxTilesY, importData->nscrTileX, importData->nscrTileY,
					 importData->balance, importData->colorBalance, importData->enhanceColors,
					 &progressData->progress1, &progressData->progress1Max);
	progressData->waitOn = 1;
	return 0;
}

void threadedNscrImportBitmap(PROGRESSDATA *param) {
	CreateThread(NULL, 0, threadedNscrImportBitmapInternal, param, 0, NULL);
}

void nscrBitmapImportUpdate(HWND hWnd) {
	NSCRBITMAPIMPORTDATA *data = (NSCRBITMAPIMPORTDATA *) GetWindowLongPtr(hWnd, 0);
	int dither = SendMessage(data->hWndDitherCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int writeChars = SendMessage(data->hWndNewCharactersCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int writeScreen = SendMessage(data->hWndWriteScreenCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int writeCharIndices = SendMessage(data->hWndWriteCharIndicesCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;

	setStyle(data->hWndDitherCheckbox, !writeChars, WS_DISABLED);

	//diffuse input only enabled when dithering enabled
	setStyle(data->hWndDiffuseAmount, !dither || !writeChars, WS_DISABLED);

	//write character indices only enabled when writing screen
	setStyle(data->hWndWriteCharIndicesCheckbox, !writeScreen, WS_DISABLED);

	//character base and count only enabled when overwriting character indices and writing screen
	setStyle(data->hWndCharacterBase, !writeChars || !writeCharIndices || !writeScreen, WS_DISABLED);
	setStyle(data->hWndCharacterCount, !writeChars || !writeCharIndices || !writeScreen, WS_DISABLED);

	//if not overwriting screen, palette base and count are invalid
	setStyle(data->hWndPaletteInput, !writeScreen, WS_DISABLED);
	setStyle(data->hWndPalettesInput, !writeScreen, WS_DISABLED);

	InvalidateRect(hWnd, NULL, FALSE);
}

LRESULT WINAPI NscrBitmapImportWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSCRBITMAPIMPORTDATA *data = (NSCRBITMAPIMPORTDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = calloc(1, sizeof(NSCRBITMAPIMPORTDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			int boxWidth = 100 + 100 + 10 + 10 + 10; //box width
			int boxHeight = 2 * 27 - 5 + 10 + 10 + 10; //first row height
			int boxHeight2 = 27 * 5 - 5 + 10 + 10 + 10; //second row height
			int boxHeight3 = 3 * 27 - 5 + 10 + 10 + 10; //third row height
			int width = 30 + 2 * boxWidth; //window width
			int height = 42 + boxHeight + 10 + boxHeight2 + 10 + boxHeight3 + 10 + 22 + 10; //window height

			int leftX = 10 + 10; //left box X
			int rightX = 10 + boxWidth + 10 + 10; //right box X
			int topY = 42 + 10 + 8; //top box Y
			int middleY = 42 + boxHeight + 10 + 10 + 8; //middle box Y
			int bottomY = 42 + boxHeight + 10 + boxHeight2 + 10 + 10 + 8; //bottom box Y

			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) | WS_DISABLED);
			/*

			Bitmap:   [__________] [...]
			Palette:  [_____]
			Palettes: [_____]
			          [Import]
			
			*/

			CreateStatic(hWnd, L"Bitmap:", 10, 10, 50, 22);
			data->hWndBitmapName = CreateEdit(hWnd, L"", 70, 10, width - 10 - 50 - 70, 22, FALSE);
			data->hWndBrowseButton = CreateButton(hWnd, L"...", width - 10 - 50, 10, 50, 22, FALSE);

			data->hWndWriteScreenCheckbox = CreateCheckbox(hWnd, L"Overwrite Screen", leftX, topY, 150, 22, TRUE);
			data->hWndWriteCharIndicesCheckbox = CreateCheckbox(hWnd, L"Overwrite Character Indices", leftX, topY + 27, 150, 22, FALSE);

			data->hWndNewPaletteCheckbox = CreateCheckbox(hWnd, L"Overwrite Palette", leftX, middleY, 150, 22, TRUE);
			CreateStatic(hWnd, L"Palettes:", leftX, middleY + 27, 75, 22);
			data->hWndPalettesInput = CreateEdit(hWnd, L"1", leftX + 85, middleY + 27, 100, 22, TRUE);
			CreateStatic(hWnd, L"Base:", leftX, middleY + 27 * 2, 75, 22);
			data->hWndPaletteInput = CreateCombobox(hWnd, NULL, 0, leftX + 85, middleY + 27 * 2, 100, 200, 0);
			CreateStatic(hWnd, L"Size:", leftX, middleY + 27 * 3, 75, 22);
			data->hWndPaletteSize = CreateEdit(hWnd, L"16", leftX + 85, middleY + 27 * 3, 100, 22, TRUE);
			CreateStatic(hWnd, L"Offset:", leftX, middleY + 27 * 4, 75, 22);
			data->hWndPaletteOffset = CreateEdit(hWnd, L"0", leftX + 85, middleY + 27 * 4, 100, 22, TRUE);

			data->hWndNewCharactersCheckbox = CreateCheckbox(hWnd, L"Overwrite Character", rightX, middleY, 150, 22, TRUE);
			data->hWndDitherCheckbox = CreateCheckbox(hWnd, L"Dither", rightX, middleY + 27, 100, 22, FALSE);
			CreateStatic(hWnd, L"Diffuse:", rightX, middleY + 27 * 2, 75, 22);
			data->hWndDiffuseAmount = CreateEdit(hWnd, L"100", rightX + 85, middleY + 27 * 2, 100, 22, TRUE);
			CreateStatic(hWnd, L"Base:", rightX, middleY + 27 * 3, 75, 22);
			data->hWndCharacterBase = CreateEdit(hWnd, L"0", rightX + 85, middleY + 27 * 3, 100, 22, TRUE);
			CreateStatic(hWnd, L"Count:", rightX, middleY + 27 * 4, 75, 22);
			data->hWndCharacterCount = CreateEdit(hWnd, L"1024", rightX + 85, middleY + 27 * 4, 100, 22, TRUE);

			CreateStatic(hWnd, L"Balance:", leftX, bottomY, 100, 22);
			CreateStatic(hWnd, L"Color Balance:", leftX, bottomY + 27, 100, 22);
			CreateCheckbox(hWnd, L"Enhance Colors", leftX, bottomY + 27 * 2, 200, 22, FALSE);
			CreateStaticAligned(hWnd, L"Lightness", leftX + 110, bottomY, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Color", leftX + 110 + 50 + 200, bottomY, 50, 22, SCA_LEFT);
			CreateStaticAligned(hWnd, L"Green", leftX + 110, bottomY + 27, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Red", leftX + 110 + 50 + 200, bottomY + 27, 50, 22, SCA_LEFT);
			data->hWndBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			data->hWndColorBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY + 27, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);

			data->hWndImportButton = CreateButton(hWnd, L"Import", width / 2 - 100, height - 32, 200, 22, TRUE);

			CreateWindow(L"BUTTON", L"Screen", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, topY - 18, rightX + boxWidth - leftX, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Palette", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, middleY - 18, boxWidth, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Graphics", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, rightX - 10, middleY - 18, boxWidth, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Color", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, bottomY - 18, rightX + boxWidth - leftX, boxHeight3, hWnd, NULL, NULL, NULL);

			for (int i = 0; i < 16; i++) {
				WCHAR textBuffer[4];
				wsprintf(textBuffer, L"%d", i);
				SendMessage(data->hWndPaletteInput, CB_ADDSTRING, wcslen(textBuffer), (LPARAM) textBuffer);
			}

			setStyle(data->hWndCharacterBase, TRUE, WS_DISABLED);
			setStyle(data->hWndCharacterCount, TRUE, WS_DISABLED);
			setStyle(data->hWndDiffuseAmount, TRUE, WS_DISABLED);

			SetWindowSize(hWnd, width, height);
			SetGUIFont(hWnd);
			break;
		}
		case NV_INITIALIZE:
		{
			HWND hWndEditor = (HWND) lParam;
			HWND hWndMain = getMainWindow(hWndEditor);
			HWND hWndNcgrEditor = NULL;
			GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, &hWndNcgrEditor, 1);
			NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrEditor);
			NCGR *ncgr = &ncgrViewerData->ncgr;

			//set appropriate fields using data from NCGR
			WCHAR bf[16];
			int len = wsprintfW(bf, L"%d", ncgr->nBits == 4 ? 16 : 256);
			SendMessage(data->hWndPaletteSize, WM_SETTEXT, len, (LPARAM) bf);
			len = wsprintfW(bf, L"%d", ncgr->nTiles);
			SendMessage(data->hWndCharacterCount, WM_SETTEXT, len, (LPARAM) bf);

			data->hWndEditor = hWndEditor;
			break;
		}
		case NV_INITIMPORTDIALOG:
		{
			WORD d = wParam;
			int palette = (d >> 12) & 0xF;
			int charOrigin = d & 0x3FF;
			int nscrTileX = LOWORD(lParam);
			int nscrTileY = HIWORD(lParam);

			data->nscrTileX = nscrTileX;
			data->nscrTileY = nscrTileY;
			data->characterOrigin = charOrigin;

			SendMessage(data->hWndPaletteInput, CB_SETCURSEL, palette, 0);
			break;
		}
		case WM_COMMAND:
		{
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->hWndBrowseButton) {
					LPWSTR location = openFileDialog(hWnd, L"Select Bitmap", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
					if (!location) break;

					SendMessage(data->hWndBitmapName, WM_SETTEXT, wcslen(location), (LPARAM) location);

					free(location);
				} else if (hWndControl == data->hWndImportButton) {
					WCHAR textBuffer[MAX_PATH + 1];
					SendMessage(data->hWndBitmapName, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					int width, height;
					COLOR32 *px = ImgRead(textBuffer, &width, &height);

					float diffuse = ((float) GetEditNumber(data->hWndDiffuseAmount)) * 0.01f;

					int characterBase = GetEditNumber(data->hWndCharacterBase);
					int characterCount = GetEditNumber(data->hWndCharacterCount);

					int nPalettes = GetEditNumber(data->hWndPalettesInput);
					int paletteSize = GetEditNumber(data->hWndPaletteSize);
					int paletteOffset = GetEditNumber(data->hWndPaletteOffset);
					if (nPalettes > 16) nPalettes = 16;

					int paletteNumber = SendMessage(data->hWndPaletteInput, CB_GETCURSEL, 0, 0);
					int dither = GetCheckboxChecked(data->hWndDitherCheckbox);
					int newPalettes = GetCheckboxChecked(data->hWndNewPaletteCheckbox);
					int newCharacters = GetCheckboxChecked(data->hWndNewCharactersCheckbox);
					int balance = GetTrackbarPosition(data->hWndBalance);
					int colorBalance = GetTrackbarPosition(data->hWndColorBalance);
					int enhanceColors = GetCheckboxChecked(data->hWndEnhanceColors);
					int writeCharacterIndices = GetCheckboxChecked(data->hWndWriteCharIndicesCheckbox);
					int writeScreen = GetCheckboxChecked(data->hWndWriteScreenCheckbox);

					if (!writeScreen) writeCharacterIndices = 0;

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
					NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer);
					NCGR *ncgr = &ncgrViewerData->ncgr;
					HWND hWndNscrViewer = data->hWndEditor;
					NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(hWndNscrViewer);
					NSCR *nscr = &nscrViewerData->nscr;
					HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
					NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) EditorGetData(hWndNclrViewer);
					NCLR *nclr = &nclrViewerData->nclr;
					int maxTilesX = (nscr->nWidth / 8) - data->nscrTileX;
					int maxTilesY = (nscr->nHeight / 8) - data->nscrTileY;

					HWND hWndProgress = CreateWindow(L"ProgressWindowClass", L"In Progress...", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 100, hWndMain, NULL, NULL, NULL);
					ShowWindow(hWndProgress, SW_SHOW);
					NSCRIMPORTDATA *nscrImportData = (NSCRIMPORTDATA *) calloc(1, sizeof(NSCRIMPORTDATA));
					nscrImportData->nclr = nclr;
					nscrImportData->ncgr = ncgr;
					nscrImportData->nscr = nscr;
					nscrImportData->px = px;
					nscrImportData->width = width;
					nscrImportData->height = height;
					nscrImportData->tileBase = nscrViewerData->tileBase;
					nscrImportData->nPalettes = nPalettes;
					nscrImportData->paletteNumber = paletteNumber;
					nscrImportData->paletteOffset = paletteOffset;
					nscrImportData->paletteSize = paletteSize;
					nscrImportData->newPalettes = newPalettes;
					nscrImportData->newCharacters = newCharacters;
					nscrImportData->charBase = characterBase;
					nscrImportData->nMaxChars = characterCount;
					nscrImportData->dither = dither;
					nscrImportData->diffuse = diffuse;
					nscrImportData->balance = balance;
					nscrImportData->colorBalance = colorBalance;
					nscrImportData->maxTilesX = maxTilesX;
					nscrImportData->maxTilesY = maxTilesY;
					nscrImportData->writeCharacterIndices = writeCharacterIndices;
					nscrImportData->writeScreen = writeScreen;
					nscrImportData->nscrTileX = data->nscrTileX;
					nscrImportData->nscrTileY = data->nscrTileY;
					nscrImportData->hWndNclrViewer = hWndNclrViewer;
					nscrImportData->hWndNcgrViewer = hWndNcgrViewer;
					nscrImportData->hWndNscrViewer = hWndNscrViewer;
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					progressData->data = nscrImportData;
					progressData->callback = nscrImportCallback;
					SendMessage(hWndProgress, NV_SETDATA, 0, (LPARAM) progressData);

					threadedNscrImportBitmap(progressData);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
					SetActiveWindow(hWndProgress);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
				} else if (hWndControl == data->hWndWriteCharIndicesCheckbox) {
					nscrBitmapImportUpdate(hWnd);
				} else if (hWndControl == data->hWndWriteScreenCheckbox) {
					nscrBitmapImportUpdate(hWnd);
				} else if (hWndControl == data->hWndDitherCheckbox) {
					nscrBitmapImportUpdate(hWnd);
				} else if (hWndControl == data->hWndNewCharactersCheckbox) {
					nscrBitmapImportUpdate(hWnd);
				}
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndParent);
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

LRESULT WINAPI NscrPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndNscrViewer = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWndNscrViewer);
	int contentWidth = 0, contentHeight = 0;
	if (data) {
		contentWidth = getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale);
		contentHeight = getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale);
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
		{
			ShowScrollBar(hWnd, SB_BOTH, FALSE);
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hWindowDC = BeginPaint(hWnd, &ps);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.nPos = 0;
			vert.nPos = 0;
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);

			ScrViewerPaint(hWnd, hWindowDC, horiz.nPos, vert.nPos);

			EndPaint(hWnd, &ps);
			break;
		}
		case NV_RECALCULATE:
		{
			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			SendMessage(hWnd, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
			break;
		}
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			return DefChildProc(hWnd, msg, wParam, lParam);
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
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		{
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
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);
			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;

			if (msg != WM_MOUSELEAVE) {
				int x = mousePos.x / (8 * data->scale);
				int y = mousePos.y / (8 * data->scale);

				if (data->showBorders) {
					x = (mousePos.x - 1) / (8 * data->scale + 1);
					y = (mousePos.y - 1) / (8 * data->scale + 1);
				}

				if (x < 0) x = 0;
				if (y < 0) y = 0;
				if (x >= (int) (data->nscr.nWidth / 8)) x = data->nscr.nWidth / 8 - 1;
				if (y >= (int) (data->nscr.nHeight / 8)) y = data->nscr.nHeight / 8 - 1;

				if (x != data->hoverX || y != data->hoverY) {
					if (data->mouseDown && data->selStartX != -1 && data->selStartY != -1) {
						data->selEndX = x;
						data->selEndY = y;

						WCHAR sizeBuffer[32];
						int len = wsprintfW(sizeBuffer, L"Selection: %dx%d", 8 * (max(data->selEndX, data->selStartX) - min(data->selEndX, data->selStartX) + 1),
								  8 * (max(data->selEndY, data->selStartY) - min(data->selEndY, data->selStartY) + 1));
						SendMessage(data->hWndSelectionSize, WM_SETTEXT, len, (LPARAM) sizeBuffer);
					}
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}

			if (mousePos.x >= 0 && mousePos.y >= 0
				&& mousePos.x < getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale)
				&& mousePos.y < getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale) && msg != WM_MOUSELEAVE) {
				int x = mousePos.x / (8 * data->scale);
				int y = mousePos.y / (8 * data->scale);
				if (data->showBorders) {
					x = (mousePos.x - 1) / (8 * data->scale + 1);
					y = (mousePos.y - 1) / (8 * data->scale + 1);
				}
				if (x != data->hoverX || y != data->hoverY) {
					data->hoverX = x;
					data->hoverY = y;
					InvalidateRect(hWnd, NULL, FALSE);
				}
			} else {
				int x = -1, y = -1;
				if (x != data->hoverX || y != data->hoverY) {
					data->hoverX = -1;
					data->hoverY = -1;
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}

			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_KEYDOWN:
		case WM_KEYUP:
			PostMessage(data->hWnd, msg, wParam, lParam);
			break;
		case WM_LBUTTONUP:
		case WM_LBUTTONDOWN:
		case WM_RBUTTONUP:
		{
			int hoverY = data->hoverY;
			int hoverX = data->hoverX;
			SetFocus(hWnd);

			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);
			//transform it by scroll position
			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);

			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;
			if (msg == WM_LBUTTONUP) {
				data->mouseDown = 0;
				ReleaseCapture();
			}
			if (mousePos.x >= 0 && mousePos.y >= 0
				&& mousePos.x < getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale)
				&& mousePos.y < getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale)) {
				if (msg == WM_RBUTTONUP) {
					//if it is within the colors area, open a color chooser
					HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 2);

					//disable non-applicable options
					const int needsSel[] = {
						ID_NSCRMENU_DESELECT, ID_NSCRMENU_CUT, ID_NSCRMENU_COPY,
						ID_NSCRMENU_FLIPHORIZONTALLY, ID_NSCRMENU_FLIPVERTICALLY,
						ID_NSCRMENU_MAKEIDENTITY
					};
					for (int i = 0; i < sizeof(needsSel) / sizeof(int); i++) {
						EnableMenuItem(hPopup, needsSel[i], (data->selStartX == -1) ? MF_DISABLED : MF_ENABLED);
					}

					POINT mouse;
					GetCursorPos(&mouse);
					TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWndNscrViewer, NULL);
					data->contextHoverY = hoverY;
					data->contextHoverX = hoverX;
				} else if(msg == WM_LBUTTONUP) {
					if (data->hoverX == data->selStartX && data->hoverY == data->selStartY) {
						data->editingX = data->hoverX;
						data->editingY = data->hoverY;
						data->hoverX = -1;
						data->hoverY = -1;
					}
				} else if (msg == WM_LBUTTONDOWN) {
					data->selStartX = data->hoverX;
					data->selStartY = data->hoverY;
					data->selEndX = data->selStartX;
					data->selEndY = data->selStartY;
					data->mouseDown = 1;
					InvalidateRect(hWnd, NULL, FALSE);
					SetCapture(hWnd);

					int tile = data->selStartX + data->selStartY * (data->nscr.nWidth / 8);
					WORD d = data->nscr.data[tile];
					int character = d & 0x3FF;
					int palette = d >> 12;

					WCHAR bf[16];
					SendMessage(data->hWndPaletteNumber, CB_SETCURSEL, palette, 0);
					int len = wsprintfW(bf, L"%d", character);
					SendMessage(data->hWndCharacterNumber, WM_SETTEXT, len, (LPARAM) bf);

					len = wsprintfW(bf, L"Selection: %dx%d", 8, 8);
					SendMessage(data->hWndSelectionSize, WM_SETTEXT, len, (LPARAM) bf);
				}
			} else {
				if (msg == WM_LBUTTONDOWN) {
					data->selStartX = -1;
					data->selStartY = -1;
					data->selEndX = -1;
					data->selEndY = -1;
					InvalidateRect(hWnd, NULL, FALSE);
					SendMessage(data->hWndSelectionSize, WM_SETTEXT, 0, (LPARAM) L"");
				}
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNscrBitmapImportClass(VOID) {
	RegisterGenericClass(L"NscrBitmapImportClass", NscrBitmapImportWndProc, sizeof(LPVOID));
}

VOID RegisterNscrPreviewClass(VOID) {
	RegisterGenericClass(L"NscrPreviewClass", NscrPreviewWndProc, sizeof(LPVOID));
}

VOID RegisterNscrViewerClass(VOID) {
	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EDITOR_CLASS *cls = EditorRegister(L"NscrViewerClass", NscrViewerWndProc, L"Screen Editor", sizeof(NSCRVIEWERDATA), features);
	EditorAddFilter(cls, NSCR_TYPE_NSCR, L"nscr", L"NSCR Files (*.nscr)\0*.nscr\0");
	EditorAddFilter(cls, NSCR_TYPE_NC, L"nsc", L"NSC Files (*.nsc)\0*.nsc\0");
	EditorAddFilter(cls, NSCR_TYPE_IC, L"isc", L"ISC Files (*.isc)\0*.isc\0");
	EditorAddFilter(cls, NSCR_TYPE_AC, L"asc", L"ASC Files (*.asc)\0*.asc\0");
	EditorAddFilter(cls, NSCR_TYPE_HUDSON, L"bin", L"Screen Files (*.bin)\0*.bin\0");
	EditorAddFilter(cls, NSCR_TYPE_HUDSON2, L"bin", L"Screen Files (*.bin)\0*.bin\0");
	EditorAddFilter(cls, NSCR_TYPE_BIN, L"bin", L"Screen Files (*.bin, *nsc.bin, *isc.bin, *.nbfs)\0*.bin;*.nbfs\0");
	EditorAddFilter(cls, NSCR_TYPE_COMBO, L"bin", L"Combination Files (*.dat, *.bin)\0*.dat;*.bin\0");

	RegisterNscrBitmapImportClass();
	RegisterNscrPreviewClass();
}

HWND CreateNscrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NSCR nscr;
	int n = ScrReadFile(&nscr, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}
	width = nscr.nWidth;
	height = nscr.nHeight;

	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL) + 200; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL) + 22;
	}

	HWND hWnd = EditorCreate(L"NscrViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nscr);
	if (nscr.header.format == NSCR_TYPE_HUDSON || nscr.header.format == NSCR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}

HWND CreateNscrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NSCR *nscr) {
	width = nscr->nWidth;
	height = nscr->nHeight;

	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL) + 200; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL) + 22;
	}

	HWND hWnd = EditorCreate(L"NscrViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE_IMMEDIATE, (WPARAM) nscr, 0);
	if (nscr->header.format == NSCR_TYPE_HUDSON || nscr->header.format == NSCR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}
