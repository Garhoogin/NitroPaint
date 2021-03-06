#include "ncerviewer.h"
#include "nitropaint.h"
#include "ncgr.h"
#include "nclr.h"
#include "tiler.h"
#include "resource.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"

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
	int width, height;

	DWORD *bits = ncerCellToBitmap(&info, ncgr, nclr, &width, &height, 1);

	HBITMAP hbm = CreateBitmap(width, height, 1, 32, bits);
	HDC hCompatibleDC = CreateCompatibleDC(hWindowDC);
	SelectObject(hCompatibleDC, hbm);
	BitBlt(hWindowDC, 0, 0, width, height, hCompatibleDC, 0, 0, SRCCOPY);

	DeleteObject(hCompatibleDC);
	DeleteObject(hbm);
	free(bits);
	EndPaint(hWnd, &ps);
}

#define NV_INITIALIZE (WM_USER+1)

LRESULT WINAPI NcerViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCERVIEWERDATA *data = (NCERVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (NCERVIEWERDATA *) calloc(1, sizeof(NCERVIEWERDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			//64x64 cell

			//+-----+ [character index]
			//|     | [palette number]
			//|     | [Size: XxX]
			//+-----+
			//[Cell x]
			data->hWndCellDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 64, 94, 100, hWnd, NULL, NULL, NULL);
			data->hWndOamDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 94, 64, 70, 100, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Character: ", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 64, 0, 50, 21, hWnd, NULL, NULL, NULL);
			data->hWndCharacterOffset = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 64 + 50, 0, 30, 21, hWnd, NULL, NULL, NULL);
			data->hWndCharacterOffsetButton = CreateWindow(L"BUTTON", L"Set", WS_VISIBLE | WS_CHILD, 64 + 50 + 30, 0, 20, 21, hWnd, NULL, NULL, NULL);
			data->hWndPaletteDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 64, 21, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndSizeLabel = CreateWindow(L"STATIC", L"Size: 64x64", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 64, 42, 100, 21, hWnd, NULL, NULL, NULL);

			break;
		}
		case WM_NCHITTEST:	//make the border non-sizeable
		{
			LRESULT ht = DefMDIChildProc(hWnd, msg, wParam, lParam);
			if (ht == HTTOP || ht == HTBOTTOM || ht == HTLEFT || ht == HTRIGHT || ht == HTTOPLEFT || ht == HTTOPRIGHT || ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT)
				return HTBORDER;
			return ht;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->ncer, (NCER *) lParam, sizeof(NCER));
			WCHAR titleBuffer[MAX_PATH + 15];
			if (!g_configuration.fullPaths) path = GetFileName(path);
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - NCER Viewer", 30);
			SetWindowText(hWnd, titleBuffer);
			data->frameData.contentWidth = 64 + 100;
			data->frameData.contentHeight = 64 + 21;

			RECT rc = { 0 };
			rc.right = data->frameData.contentWidth;
			rc.bottom = data->frameData.contentHeight;
			if (rc.right < 150) rc.right = 150;
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
			wsprintf(size, L"Size: %dx%d", cinfo.width, cinfo.height);
			SendMessage(data->hWndSizeLabel, WM_SETTEXT, (WPARAM) wcslen(size), (LPARAM) size);
			for (int i = 0; i < data->ncer.nCells; i++) {
				wsprintf(size, L"Cell %02d", i);
				SendMessage(data->hWndCellDropdown, CB_ADDSTRING, 0, (LPARAM) size);
			}
			SendMessage(data->hWndCellDropdown, CB_SETCURSEL, 0, 0);

			for (int i = 0; i < data->ncer.cells->nAttribs; i++) {
				wsprintf(size, L"OAM %d", i);
				SendMessage(data->hWndOamDropdown, CB_ADDSTRING, 0, (LPARAM) size);
			}
			SendMessage(data->hWndOamDropdown, CB_SETCURSEL, 0, 0);

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

				if (notification == CBN_SELCHANGE && hWndControl == data->hWndCellDropdown) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					data->cell = sel;
					data->oam = 0;
					WCHAR size[13];
					NCER_CELL_INFO cinfo;
					NCER_CELL *cell = data->ncer.cells + sel;
					decodeAttributesEx(&cinfo, cell, data->oam);
					SendMessage(data->hWndOamDropdown, CB_RESETCONTENT, 0, 0);

					for (int i = 0; i < cell->nAttribs; i++) {
						wsprintf(size, L"OAM %d", i);
						SendMessage(data->hWndOamDropdown, CB_ADDSTRING, 0, (LPARAM) size);
					}
					SendMessage(data->hWndOamDropdown, CB_SETCURSEL, 0, 0);

					wsprintf(size, L"Size: %dx%d", cinfo.width, cinfo.height);
					SendMessage(data->hWndSizeLabel, WM_SETTEXT, (WPARAM) wcslen(size), (LPARAM) size);
					SendMessage(data->hWndPaletteDropdown, CB_SETCURSEL, cinfo.palette, 0);
					wsprintf(size, L"%d", cinfo.characterName);
					SendMessage(data->hWndCharacterOffset, WM_SETTEXT, (WPARAM) wcslen(size), (LPARAM) size);
					InvalidateRect(hWnd, NULL, TRUE);

					if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndPaletteDropdown) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					NCER_CELL *cell = data->ncer.cells + data->cell;
					WORD attr2 = cell->attr[2 + 3 * data->oam];
					cell->attr[2 + 3 * data->oam] = (attr2 & 0x0FFF) | (sel << 12);
					InvalidateRect(hWnd, NULL, TRUE);

					if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndOamDropdown){
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					data->oam = sel;


					WCHAR size[13];
					NCER_CELL_INFO cinfo;
					NCER_CELL *cell = data->ncer.cells + data->cell;
					decodeAttributesEx(&cinfo, cell, data->oam);

					wsprintf(size, L"Size: %dx%d", cinfo.width, cinfo.height);
					SendMessage(data->hWndSizeLabel, WM_SETTEXT, (WPARAM) wcslen(size), (LPARAM) size);
					SendMessage(data->hWndPaletteDropdown, CB_SETCURSEL, cinfo.palette, 0);
					wsprintf(size, L"%d", cinfo.characterName);
					SendMessage(data->hWndCharacterOffset, WM_SETTEXT, (WPARAM) wcslen(size), (LPARAM) size);
					InvalidateRect(hWnd, NULL, TRUE);

					if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndCharacterOffsetButton) {
					WCHAR input[16];
					SendMessage(data->hWndCharacterOffset, WM_GETTEXT, (WPARAM) 15, (LPARAM) input);
					int chr = _wtoi(input);
					NCER_CELL *cell = data->ncer.cells + data->cell;
					WORD attr2 = cell->attr[2 + 3 * data->oam];
					attr2 = attr2 & 0xFC00;
					attr2 |= chr & 0x3FF;
					cell->attr[2 + 3 * data->oam] = attr2;
					InvalidateRect(hWnd, NULL, TRUE);
				}
			}
			if (lParam == 0 && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
					case ID_FILE_SAVE:
					{
						ncerWrite(&data->ncer, data->szOpenFile);
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

						DWORD *bits =  ncerCellToBitmap(&info, ncgr, nclr, &width, &height, 0);

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
			ncerFree(&data->ncer);
			free(data);
			if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNcerViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NcerViewerClass";
	wcex.lpfnWndProc = NcerViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

HWND CreateNcerViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path) {

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
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcerViewerClass", L"NCER Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncer);
		if (ncer.header.format == NCER_TYPE_HUDSON) {
			SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
		}
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcerViewerClass", L"NCER Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	//SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
	return h;
}