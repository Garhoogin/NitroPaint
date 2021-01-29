#include <math.h>
#include "nscrviewer.h"
#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "childwindow.h"
#include "resource.h"
#include "nitropaint.h"
#include "nscr.h"
#include "gdip.h"
#include "palette.h"
#include "tiler.h"

extern HICON g_appIcon;

#define NV_INITIALIZE (WM_USER+1)
#define NV_SETDATA (WM_USER+2)
#define NV_INITIMPORTDIALOG (WM_USER+3)
#define NV_RECALCULATE (WM_USER+4)

DWORD *renderNscrBits(NSCR *renderNscr, NCGR *renderNcgr, NCLR *renderNclr, BOOL drawGrid, BOOL checker, int *width, int *height, int tileMarks, int highlightTile, int highlightColor) {
	int bWidth = renderNscr->nWidth;
	int bHeight = renderNscr->nHeight;
	if (drawGrid) {
		bWidth += renderNscr->nWidth / 8 + 1;
		bHeight += renderNscr->nHeight / 8 + 1;
	}
	*width = bWidth;
	*height = bHeight;

	LPDWORD bits = (LPDWORD) calloc(bWidth * bHeight, 4);

	int tilesX = renderNscr->nWidth >> 3;
	int tilesY = renderNscr->nHeight >> 3;

	DWORD block[64];

	for (int y = 0; y < tilesY; y++) {
		int offsetY = y << 3;
		if (drawGrid) offsetY = y * 9 + 1;
		for (int x = 0; x < tilesX; x++) {
			int offsetX = x << 3;
			if (drawGrid) offsetX = x * 9 + 1;

			int tileNo = -1;
			nscrGetTileEx(renderNscr, renderNcgr, renderNclr, x, y, checker, block, &tileNo);
			DWORD dwDest = x * 8 + y * 8 * bWidth;

			if (tileMarks != -1 && tileMarks == tileNo) {
				for (int i = 0; i < 64; i++) {
					DWORD d = block[i];
					int b = d & 0xFF;
					int g = (d >> 8) & 0xFF;
					int r = (d >> 16) & 0xFF;
					r = r >> 1;
					g = (g + 255) >> 1;
					b = (b + 255) >> 1;
					block[i] = (d & 0xFF000000) | b | (g << 8) | (r << 16);
				}
			}

			if (highlightTile != -1 && (x + y * tilesX) == highlightTile) {
				for (int i = 0; i < 64; i++) {
					DWORD d = block[i];
					int b = d & 0xFF;
					int g = (d >> 8) & 0xFF;
					int r = (d >> 16) & 0xFF;
					r = (r + 255) >> 1;
					g = (g + 255) >> 1;
					b = (b + 255) >> 1;
					block[i] = (d & 0xFF000000) | b | (g << 8) | (r << 16);
				}
			}

			if (highlightColor != -1) {
				int color = highlightColor % (1 << renderNcgr->nBits);
				int highlightPalette = highlightColor / (1 << renderNcgr->nBits);
				WORD nscrData = renderNscr->data[x + y * (renderNscr->nWidth / 8)];
				if (((nscrData >> 12) & 0xF) == highlightPalette) {
					int charBase = 0;
					if (renderNscr->nHighestIndex >= renderNcgr->nTiles) charBase = renderNscr->nHighestIndex + 1 - renderNcgr->nTiles;
					int tileIndex = nscrData & 0x3FF;
					if (tileIndex - charBase >= 0) {
						BYTE *tile = renderNcgr->tiles[tileIndex - charBase];
						for (int i = 0; i < 64; i++) {
							if (tile[i] == color) {
								block[i] = 0xFFFFFFFF;
							}
						}
					}
				}
			}

			for (int i = 0; i < 8; i++) {
				CopyMemory(bits + dwDest + i * bWidth, block + (i << 3), 32);
			}
		}
	}

	/*for (int i = 0; i < bWidth * bHeight; i++) {
		DWORD d = bits[i];
		int r = d & 0xFF;
		int g = (d >> 8) & 0xFF;
		int b = (d >> 16) & 0xFF;
		int a = (d >> 24) & 0xFF;
		bits[i] = b | (g << 8) | (r << 16) | (a << 24);
	}*/
	return bits;
}

HBITMAP renderNscr(NSCR *renderNscr, NCGR *renderNcgr, NCLR *renderNclr, BOOL drawGrid, int *width, int *height, int highlightNclr, int highlightTile, int highlightColor, int scale) {
	if (renderNcgr != NULL) {
		if (renderNscr->nHighestIndex >= renderNcgr->nTiles && highlightNclr != -1) highlightNclr -= (renderNcgr->nTiles - renderNscr->nHighestIndex - 1);
		DWORD *bits = renderNscrBits(renderNscr, renderNcgr, renderNclr, FALSE, TRUE, width, height, highlightNclr, highlightTile, highlightColor);

		//HBITMAP hBitmap = CreateBitmap(*width, *height, 1, 32, bits);
		HBITMAP hBitmap = CreateTileBitmap(bits, *width, *height, -1, -1, width, height, scale, drawGrid);
		free(bits);
		return hBitmap;
	}
	return NULL;
}

LRESULT WINAPI NscrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (NSCRVIEWERDATA *) calloc(1, sizeof(NSCRVIEWERDATA));
		SetWindowLongPtr(hWnd, 0, (LONG) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			data->showBorders = 0;
			data->scale = 1;

			data->hWndPreview = CreateWindow(L"NscrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 300, 300, hWnd, NULL, NULL, NULL);

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
			MoveWindow(data->hWndPreview, 0, 0, rcClient.right, rcClient.bottom, TRUE);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
		case WM_PAINT:
		{
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->nscr, (NSCR *) lParam, sizeof(NSCR));
			WCHAR titleBuffer[MAX_PATH + 15];
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - NSCR Viewer", 30);
			SetWindowText(hWnd, titleBuffer);
			data->frameData.contentWidth = getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale);
			data->frameData.contentHeight = getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale);

			RECT rc = { 0 };
			rc.right = data->frameData.contentWidth;
			rc.bottom = data->frameData.contentHeight;
			AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
			int width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL); //+4 to account for WS_EX_CLIENTEDGE
			int height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL);
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
			return 1;
		}
		case WM_MDIACTIVATE:
		{
			HWND hWndMain = getMainWindow(hWnd);
			if ((HWND) lParam == hWnd) {
				if (data->showBorders)
					CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_CHECKED);
				else
					CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
				int checkBox = ID_ZOOM_100;
				if (data->scale == 2) {
					checkBox = ID_ZOOM_200;
				} else if (data->scale == 4) {
					checkBox = ID_ZOOM_400;
				} else if (data->scale == 8) {
					checkBox = ID_ZOOM_800;
				}
				int ids[] = {ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800};
				for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
					int id = ids[i];
					CheckMenuItem(GetMenu(hWndMain), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
				}
			}
			break;
		}
		case WM_COMMAND:
		{
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

						DWORD * bits = renderNscrBits(nscr, ncgr, nclr, FALSE, FALSE, &width, &height, -1, -1, -1);
						
						writeImage(bits, width, height, location);
						free(bits);
						free(location);
						break;
					}
					case ID_NSCRMENU_FLIPHORIZONTALLY:
					{
						int tileNo = data->contextHoverX + data->contextHoverY * (data->nscr.nWidth >> 3);
						WORD oldVal = data->nscr.data[tileNo];
						oldVal ^= (TILE_FLIPX << 10);
						data->nscr.data[tileNo] = oldVal;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_NSCRMENU_FLIPVERTICALLY:
					{
						int tileNo = data->contextHoverX + data->contextHoverY * (data->nscr.nWidth >> 3);
						WORD oldVal = data->nscr.data[tileNo];
						oldVal ^= (TILE_FLIPY << 10);
						data->nscr.data[tileNo] = oldVal;
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_FILE_SAVE:
					{
						nscrWrite(&data->nscr, data->szOpenFile);
						break;
					}
					case ID_NSCRMENU_IMPORTBITMAPHERE:
					{
						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						HWND h = CreateWindow(L"NscrBitmapImportClass", L"Import Bitmap", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, hWndMain, NULL, NULL, NULL);
						WORD d = data->nscr.data[data->contextHoverX + data->contextHoverY * (data->nscr.nWidth >> 3)];
						SendMessage(h, NV_INITIMPORTDIALOG, d, data->contextHoverX | (data->contextHoverY << 16));
						ShowWindow(h, SW_SHOW);
						break;
					}
					case ID_NSCRMENU_COPY:
					{
						int tileX = data->contextHoverX;
						int tileY = data->contextHoverY;
						WORD d = data->nscr.data[tileX + tileY * (data->nscr.nWidth / 8)];
						HANDLE hString = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 10);
						LPSTR clip = (LPSTR) GlobalLock(hString);
						*(DWORD *) clip = 0x30303030;
						clip[4] = 'N';
						clip[5] = (d & 0xF) + '0';
						clip[6] = ((d >> 4) & 0xF) + '0';
						clip[7] = ((d >> 8) & 0xF) + '0';
						clip[8] = ((d >> 12) & 0xF) + '0';
						clip[9] = '\0';
						GlobalUnlock(hString);
						OpenClipboard(hWnd);
						EmptyClipboard();
						SetClipboardData(CF_TEXT, hString);
						CloseClipboard();
						break;
					}
					case ID_NSCRMENU_PASTE:
					{
						OpenClipboard(hWnd);
						HANDLE hString = GetClipboardData(CF_TEXT);
						CloseClipboard();
						LPSTR clip = GlobalLock(hString);
						if (strlen(clip) == 9 && *(DWORD *) clip == 0x30303030 && clip[4] == 'N') {
							WORD d = (clip[5] & 0xF) | ((clip[6] & 0xF) << 4) | ((clip[7] & 0xF) << 8) | ((clip[8] & 0xF) << 12);
							data->nscr.data[data->contextHoverX + data->contextHoverY * (data->nscr.nWidth / 8)] = d;
							InvalidateRect(hWnd, NULL, FALSE);
						}

						GlobalUnlock(hString);
						break;
					}
					case ID_VIEW_GRIDLINES:
					{
						HWND hWndMain = getMainWindow(hWnd);
						int state = GetMenuState(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_BYCOMMAND);
						state = !state;
						if (state) {
							data->showBorders = 1;
							CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_CHECKED);
						} else {
							data->showBorders = 0;
							CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
						}
						data->frameData.contentWidth = getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale);
						data->frameData.contentHeight = getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale);

						SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
						InvalidateRect(hWnd, NULL, TRUE);
						RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
						break;
					}
					case ID_ZOOM_100:
					case ID_ZOOM_200:
					case ID_ZOOM_400:
					case ID_ZOOM_800:
					{
						if (LOWORD(wParam) == ID_ZOOM_100) data->scale = 1;
						if (LOWORD(wParam) == ID_ZOOM_200) data->scale = 2;
						if (LOWORD(wParam) == ID_ZOOM_400) data->scale = 4;
						if (LOWORD(wParam) == ID_ZOOM_800) data->scale = 8;

						int checkBox = ID_ZOOM_100;
						if (data->scale == 2) {
							checkBox = ID_ZOOM_200;
						} else if (data->scale == 4) {
							checkBox = ID_ZOOM_400;
						} else if (data->scale == 8) {
							checkBox = ID_ZOOM_800;
						}
						int ids[] = {ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800};
						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
							int id = ids[i];
							CheckMenuItem(GetMenu(hWndMain), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
						}
						data->frameData.contentWidth = getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale);
						data->frameData.contentHeight = getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale);

						SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
						InvalidateRect(hWnd, NULL, TRUE);
						RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
						break;
					}
				}
			}
			break;
		}
		case WM_TIMER:
		{
			if (wParam == 1) {
				int color = data->verifyColor;
				data->verifyFrames--;
				if (!data->verifyFrames) {
					KillTimer(hWnd, wParam);
				}
				InvalidateRect(hWnd, NULL, FALSE);
				return 0;
			}
			break;
		}
		case WM_DESTROY:
		{
			if (data->hWndTileEditor) DestroyWindow(data->hWndTileEditor);
			free(data->nscr.data);
			free(data);
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			nitroPaintStruct->hWndNscrViewer = NULL;
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		}
		case NV_SETDATA:
		{
			int tile = data->editingX + data->editingY * (data->nscr.nWidth >> 3);
			int character = wParam & 0x3FF;
			int palette = lParam & 0xF; //0x0C00
			data->nscr.data[tile] = (data->nscr.data[tile] & 0xC00) | character | (palette << 12);
			InvalidateRect(hWnd, NULL, FALSE);

			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}


typedef struct {
	HWND hWndCharacterInput;
	HWND hWndPaletteInput;
	HWND hWndOk;
} NSCRTILEEDITORDATA;

LRESULT WINAPI NscrTileEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSCRTILEEDITORDATA *data = (NSCRTILEEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (NSCRTILEEDITORDATA *) calloc(1, sizeof(NSCRTILEEDITORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			RECT rc = { 0 };
			rc.right = 200;
			rc.bottom = 96;
			AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
			int width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
			int height = rc.bottom - rc.top + 4;
			SetWindowPos(hWnd, hWnd, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);

			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNscrViewer = nitroPaintStruct->hWndNscrViewer;
			NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0);
			int editing = nscrViewerData->editingX + nscrViewerData->editingY * (nscrViewerData->nscr.nWidth >> 3);
			WORD cell = nscrViewerData->nscr.data[editing];

			/*
				Character: [______]
				Palette:   [_____v]
			*/
			CreateWindow(L"STATIC", L"Character:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 70, 22, hWnd, NULL, NULL, NULL);
			data->hWndCharacterInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 90, 10, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 70, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 90, 37, 100, 300, hWnd, NULL, NULL, NULL);
			WCHAR bf[16];
			for (int i = 0; i < 16; i++) {
				wsprintf(bf, L"Palette %02d", i);
				SendMessage(data->hWndPaletteInput, CB_ADDSTRING, (WPARAM) wcslen(bf), (LPARAM) bf);
			}
			SendMessage(data->hWndPaletteInput, CB_SETCURSEL, (cell >> 12) & 0xF, 0);
			wsprintf(bf, L"%d", cell & 0x3FF);
			SendMessage(data->hWndCharacterInput, WM_SETTEXT, (WPARAM) wcslen(bf), (LPARAM) bf);
			data->hWndOk = CreateWindow(L"BUTTON", L"OK", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 90, 64, 100, 22, hWnd, NULL, NULL, NULL);
			EnumChildWindows(hWnd, SetFontProc, GetStockObject(DEFAULT_GUI_FONT));
			break;
		}
		case WM_NCHITTEST:
		{
			int ht = DefMDIChildProc(hWnd, msg, wParam, lParam);
			if (ht == HTTOP || ht == HTBOTTOM || ht == HTLEFT || ht == HTRIGHT || ht == HTTOPLEFT || ht == HTTOPRIGHT || ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT) return HTCAPTION;
			return ht;
		}
		case WM_DESTROY:
		{
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNscrViewer = nitroPaintStruct->hWndNscrViewer;
			NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0);
			nscrViewerData->hWndTileEditor = NULL;
			free(data);
			break;
		}
		case WM_COMMAND:
		{
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->hWndOk) {
					WCHAR bf[16];
					SendMessage(data->hWndCharacterInput, WM_GETTEXT, 15, (LPARAM) bf);
					int character = _wtoi(bf);
					int palette = SendMessage(data->hWndPaletteInput, CB_GETCURSEL, 0, 0);
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					HWND hWndNscrViewer = nitroPaintStruct->hWndNscrViewer;
					SendMessage(hWndNscrViewer, NV_SETDATA, (WPARAM) character, (LPARAM) palette);
					DestroyWindow(hWnd);
					SetFocus(hWndNscrViewer);
				}
			}
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

int calculatePaletteCharError(DWORD *block, DWORD *pals, BYTE *character, int charNumber) {
	int error = 0;
	for (int i = 0; i < 64; i++) {
		DWORD col = block[i];
		int r = col & 0xFF;
		int g = (col >> 8) & 0xFF;
		int b = (col >> 16) & 0xFF;
		int a = (col >> 24) & 0xFF;

		int index = character[i];
		DWORD matched = pals[index];
		int mr = matched & 0xFF;
		int mg = (matched >> 8) & 0xFF;
		int mb = (matched >> 16) & 0xFF;
		int ma = 255;
		if (!index) {
			ma = 0;
			mr = r;
			mg = g;
			mb = b;
		}

		int dr = r - mr;
		int dg = g - mg;
		int db = b - mb;
		int da = a - ma;
		error += (int) sqrt(dr * dr + dg * dg + db * db + da * da);
	}
	return error;
}

typedef struct {
	HWND hWndBitmapName;
	HWND hWndBrowseButton;
	HWND hWndPaletteInput;
	HWND hWndPalettesInput;
	HWND hWndImportButton;
	HWND hWndDitherCheckbox;
	HWND hWndNewPaletteCheckbox;
	HWND hWndNewCharactersCheckbox;

	int nscrTileX;
	int nscrTileY;
	int characterOrigin;
} NSCRBITMAPIMPORTDATA;

void nscrImportBitmap(NCLR *nclr, NCGR *ncgr, NSCR *nscr, DWORD *px, int width, int height, int nPalettes, int paletteNumber, BOOL newPalettes,
					  BOOL newCharacters, BOOL diffuse, int maxTilesX, int maxTilesY, int nscrTileX, int nscrTileY) {
	int tilesX = width / 8;
	int tilesY = height / 8;
	int paletteSize = ncgr->nBits == 4 ? 16 : 256;
	if (tilesX > maxTilesX) tilesX = maxTilesX;
	if (tilesY > maxTilesY) tilesY = maxTilesY;

	DWORD *blocks = (DWORD *) calloc(tilesX * tilesY, 64 * 4);
	DWORD *pals = calloc(nPalettes * paletteSize, 4);

	//split image into 8x8 chunks, and find the average color in each.
	DWORD *avgs = calloc(tilesX * tilesY, 4);
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			DWORD *block = blocks + 64 * (x + y * tilesX);
			CopyMemory(block, px + srcOffset, 32);
			CopyMemory(block + 8, px + srcOffset + width, 32);
			CopyMemory(block + 16, px + srcOffset + width * 2, 32);
			CopyMemory(block + 24, px + srcOffset + width * 3, 32);
			CopyMemory(block + 32, px + srcOffset + width * 4, 32);
			CopyMemory(block + 40, px + srcOffset + width * 5, 32);
			CopyMemory(block + 48, px + srcOffset + width * 6, 32);
			CopyMemory(block + 56, px + srcOffset + width * 7, 32);
			DWORD avg = averageColor(block, 64);
			avgs[x + y * tilesX] = avg;
		}
	}


	//generate an nPalettes color palette
	if (newPalettes) {
		
		createMultiplePalettes(blocks, avgs, width, tilesX, tilesY, pals, nPalettes, paletteSize);
	} else {
		WORD *destPalette = nclr->colors + paletteNumber * paletteSize;
		int nColors = nPalettes * paletteSize;
		for (int i = 0; i < nColors; i++) {
			WORD c = destPalette[i];
			int r = c & 0x1F;
			int g = (c >> 5) & 0x1F;
			int b = (b >> 10) & 0x1F;
			r = r * 255 / 31;
			g = g * 255 / 31;
			b = b * 255 / 31;
			pals[i] = r | (g << 8) | (b << 16);
		}
	}

	int charBase = 0;
	if (nscr->nHighestIndex >= ncgr->nTiles) {
		charBase = nscr->nHighestIndex + 1 - ncgr->nTiles;
	}

	//write to NCLR
	if (newPalettes) {
		WORD *destPalette = nclr->colors + paletteNumber * paletteSize;
		for (int i = 0; i < nPalettes; i++) {
			WORD *dest = destPalette + i * paletteSize;
			for (int j = 0; j < paletteSize; j++) {
				DWORD col = (pals + i * paletteSize)[j];
				int r = col & 0xFF;
				int g = (col >> 8) & 0xFF;
				int b = (col >> 16) & 0xFF;
				r = r * 31 / 255;
				g = g * 31 / 255;
				b = b * 31 / 255;
				dest[j] = r | (g << 5) | (b << 10);
			}
		}
	}

	//next, start palette matching. See which palette best fits a tile, set it in the NSCR, then write the bits to the NCGR.
	WORD *nscrData = nscr->data;
	if (newCharacters) {
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				DWORD *block = blocks + 64 * (x + y * tilesX);

				int leastError = 0x7FFFFFFF;
				int leastIndex = 0;
				for (int i = 0; i < nPalettes; i++) {
					int err = getPaletteError((RGB*) block, 64, pals + i * paletteSize, paletteSize);
					if (err < leastError) {
						leastError = err;
						leastIndex = i;
					}
				}

				int nscrX = x + nscrTileX;
				int nscrY = y + nscrTileY;

				WORD d = nscrData[nscrX + nscrY * (nscr->nWidth >> 3)];
				d = d & 0xFFF;
				d |= (leastIndex + paletteNumber) << 12;
				nscrData[nscrX + nscrY * (nscr->nWidth >> 3)] = d;

				int charOrigin = d & 0x3FF;
				int ncgrX = charOrigin % ncgr->tilesX;
				int ncgrY = charOrigin / ncgr->tilesX;
				if (charOrigin - charBase < 0) continue;
				BYTE *ncgrTile = ncgr->tiles[charOrigin - charBase];
				for (int i = 0; i < 64; i++) {
					if ((block[i] & 0xFF000000) == 0) ncgrTile[i] = 0;
					else {
						int index = 1 + closestpalette(*(RGB *) &block[i], pals + leastIndex * paletteSize + 1, paletteSize - 1, NULL);
						if (diffuse) {
							RGB original = *(RGB *) &block[i];
							RGB closest = ((RGB *) (pals + leastIndex * paletteSize))[index];
							int er = closest.r - original.r;
							int eg = closest.g - original.g;
							int eb = closest.b - original.b;
							doDiffuse(i, 8, 8, block, -er, -eg, -eb, 0, 1.0f);
						}
						ncgrTile[i] = index;
					}
				}
			}
		}
	} else {
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				DWORD *block = blocks + 64 * (x + y * tilesX);

				//find what combination of palette and character minimizes the error.
				int chosenCharacter = 0, chosenPalette = 0;
				int minError = 0x7FFFFFFF;
				for (int i = 0; i < nPalettes; i++) {
					for (int j = 0; j < ncgr->nTiles; j++) {
						int charId = j;
						int err = calculatePaletteCharError(block, pals + i * paletteSize, ncgr->tiles[charId], charId);
						if (err < minError) {
							chosenCharacter = charId;
							chosenPalette = i;
							minError = err;
						}
					}
				}

				int nscrX = x + nscrTileX;
				int nscrY = y + nscrTileY;

				WORD d = nscrData[nscrX + nscrY * (nscr->nWidth >> 3)];
				d = d & 0xFFF;
				d |= (chosenPalette + paletteNumber) << 12;
				d &= 0xFC00;
				d |= (chosenCharacter + charBase);
				nscrData[nscrX + nscrY * (nscr->nWidth >> 3)] = d;
			}
		}
	}

	free(blocks);
	free(pals);
	free(avgs);
}

typedef struct {
	NCLR *nclr;
	NCGR *ncgr;
	NSCR *nscr;
	DWORD *px;
	int width;
	int height;
	int nPalettes;
	int paletteNumber;
	int newPalettes;
	int newCharacters;
	int diffuse;
	int maxTilesX;
	int maxTilesY;
	int nscrTileX;
	int nscrTileY;
	HWND hWndNclrViewer;
	HWND hWndNcgrViewer;
	HWND hWndNscrViewer;
} NSCRIMPORTDATA;

void nscrImportCallback(void *data) {
	NSCRIMPORTDATA *importData = (NSCRIMPORTDATA *) data;

	InvalidateRect(importData->hWndNclrViewer, NULL, FALSE);
	InvalidateRect(importData->hWndNcgrViewer, NULL, FALSE);
	InvalidateRect(importData->hWndNscrViewer, NULL, FALSE);
	free(importData->px);
	free(data);
}

DWORD WINAPI threadedNscrImportBitmapInternal(LPVOID lpParameter) {
	PROGRESSDATA *progressData = (PROGRESSDATA *) lpParameter;
	NSCRIMPORTDATA *importData = (NSCRIMPORTDATA *) progressData->data;
	nscrImportBitmap(importData->nclr, importData->ncgr, importData->nscr, importData->px,
					 importData->width, importData->height, importData->nPalettes, importData->paletteNumber,
					 importData->newPalettes, importData->newCharacters, importData->diffuse,
					 importData->maxTilesX, importData->maxTilesY, importData->nscrTileX, importData->nscrTileY);
	progressData->waitOn = 1;
	return 0;
}

void threadedNscrImportBitmap(PROGRESSDATA *param) {
	CreateThread(NULL, 0, threadedNscrImportBitmapInternal, param, 0, NULL);
	//nscrImportBitmap(nclr, ncgr, nscr, px, width, height, nPalettes, paletteNumber, newPalettes, \
					 newCharacters, diffuse, maxTilesX, maxTilesY, nscrTileX, nscrTileY);
}

LRESULT WINAPI NscrBitmapImportWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSCRBITMAPIMPORTDATA *data = GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = calloc(1, sizeof(NSCRBITMAPIMPORTDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) | WS_DISABLED);
			/*

			Bitmap:   [__________] [...]
			Palette:  [_____]
			Palettes: [_____]
			          [Import]
			
			*/

			CreateWindow(L"STATIC", L"Bitmap:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palettes:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 64, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Dither:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 91, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Create new palette:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 118, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Overwrite characters:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 145, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndBitmapName = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 120, 10, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndBrowseButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 320, 10, 25, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 120, 37, 100, 200, hWnd, NULL, NULL, NULL);
			data->hWndPalettesInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 120, 64, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndDitherCheckbox = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 91, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndImportButton = CreateWindow(L"BUTTON", L"Import", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 120, 172, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndNewPaletteCheckbox = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 118, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndNewCharactersCheckbox = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 145, 22, 22, hWnd, NULL, NULL, NULL);

			for (int i = 0; i < 16; i++) {
				WCHAR textBuffer[4];
				wsprintf(textBuffer, L"%d", i);
				SendMessage(data->hWndPaletteInput, CB_ADDSTRING, wcslen(textBuffer), (LPARAM) textBuffer);
			}
			SendMessage(data->hWndDitherCheckbox, BM_SETCHECK, 1, 0);
			SendMessage(data->hWndNewPaletteCheckbox, BM_SETCHECK, 1, 0);
			SendMessage(data->hWndNewCharactersCheckbox, BM_SETCHECK, 1, 0);

			SetWindowSize(hWnd, 355, 204);
			EnumChildWindows(hWnd, SetFontProc, GetStockObject(DEFAULT_GUI_FONT));
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
					DWORD *px = gdipReadImage(textBuffer, &width, &height);

					SendMessage(data->hWndPalettesInput, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					int nPalettes = _wtoi(textBuffer);
					if (nPalettes > 16) nPalettes = 16;
					int paletteNumber = SendMessage(data->hWndPaletteInput, CB_GETCURSEL, 0, 0);
					int diffuse = SendMessage(data->hWndDitherCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int newPalettes = SendMessage(data->hWndNewPaletteCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int newCharacters = SendMessage(data->hWndNewCharactersCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
					NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
					NCGR *ncgr = &ncgrViewerData->ncgr;
					HWND hWndNscrViewer = nitroPaintStruct->hWndNscrViewer;
					NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0);
					NSCR *nscr = &nscrViewerData->nscr;
					HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
					NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
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
					nscrImportData->nPalettes = nPalettes;
					nscrImportData->paletteNumber = paletteNumber;
					nscrImportData->newPalettes = newPalettes;
					nscrImportData->newCharacters = newCharacters;
					nscrImportData->diffuse = diffuse;
					nscrImportData->maxTilesX = maxTilesX;
					nscrImportData->maxTilesY = maxTilesY;
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
				}
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) & ~WS_DISABLED);
			SetFocus(hWndParent);
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
	HWND hWndNscrViewer = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0);
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

			NSCR *nscr = NULL;
			NCGR *ncgr = NULL;
			NCLR *nclr = NULL;

			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWndNscrViewer, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
			if (hWndNclrViewer) {
				NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
				nclr = &nclrViewerData->nclr;
			}
			HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
			int hoveredNcgrTile = -1, hoveredNscrTile = -1;
			if (data->hoverX != -1 && data->hoverY != -1) {
				hoveredNscrTile = data->hoverX + data->hoverY * (data->nscr.nWidth / 8);
			}
			if (hWndNcgrViewer) {
				NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
				ncgr = &ncgrViewerData->ncgr;
				hoveredNcgrTile = ncgrViewerData->hoverIndex;
			}

			nscr = &data->nscr;
			int bitmapWidth, bitmapHeight;

			int highlightColor = data->verifyColor;
			if ((data->verifyFrames & 1) == 0) highlightColor = -1;
			HBITMAP hBitmap = renderNscr(nscr, ncgr, nclr, data->showBorders, &bitmapWidth, &bitmapHeight, hoveredNcgrTile, hoveredNscrTile, highlightColor, data->scale);

			HDC hDC = CreateCompatibleDC(hWindowDC);
			SelectObject(hDC, hBitmap);
			BitBlt(hWindowDC, -horiz.nPos, -vert.nPos, bitmapWidth, bitmapHeight, hDC, 0, 0, SRCCOPY);
			DeleteObject(hDC);
			DeleteObject(hBitmap);

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
		case WM_LBUTTONDOWN:
		case WM_RBUTTONUP:
		{
			int hoverY = data->hoverY;
			int hoverX = data->hoverX;
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
			if (mousePos.x >= 0 && mousePos.y >= 0
				&& mousePos.x < getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale)
				&& mousePos.y < getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale)) {
				if (msg == WM_RBUTTONUP) {
					//if it is within the colors area, open a color chooser
					HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 2);
					POINT mouse;
					GetCursorPos(&mouse);
					TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWndNscrViewer, NULL);
					data->contextHoverY = hoverY;
					data->contextHoverX = hoverX;
				} else {
					if (data->hWndTileEditor != NULL) DestroyWindow(data->hWndTileEditor);
					data->editingX = data->hoverX;
					data->editingY = data->hoverY;
					data->hWndTileEditor = CreateWindowEx(WS_EX_MDICHILD | WS_EX_CLIENTEDGE, L"NscrTileEditorClass", L"Tile Editor", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN,
														  CW_USEDEFAULT, CW_USEDEFAULT, 200, 150, (HWND) GetWindowLong(hWndNscrViewer, GWL_HWNDPARENT), NULL, NULL, NULL);
					data->hoverX = -1;
					data->hoverY = -1;
				}
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNscrTileEditorClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NscrTileEditorClass";
	wcex.lpfnWndProc = NscrTileEditorWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterNscrBitmapImportClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NscrBitmapImportClass";
	wcex.lpfnWndProc = NscrBitmapImportWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterNscrPreviewClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NscrPreviewClass";
	wcex.lpfnWndProc = NscrPreviewWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterNscrViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NscrViewerClass";
	wcex.lpfnWndProc = NscrViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
	RegisterNscrTileEditorClass();
	RegisterNscrBitmapImportClass();
	RegisterNscrPreviewClass();
}

HWND CreateNscrViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path) {
	NSCR nscr;
	int n = nscrReadFile(&nscr, path);
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
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL); //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL);
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NscrViewerClass", L"NSCR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nscr);

		if (nscr.type == NSCR_TYPE_HUDSON || nscr.type == NSCR_TYPE_HUDSON2) {
			SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
		}
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NscrViewerClass", L"NSCR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nscr);
	return h;
}