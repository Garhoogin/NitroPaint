#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "tileeditor.h"
#include "nitropaint.h"
#include "nscrviewer.h"
#include "tiler.h"
#include "ncgr.h"

extern HANDLE g_appIcon;

#define NV_INITIALIZE (WM_USER+1)

#define REVERSE(x) ((x)&0xFF00FF00)|(((x)&0xFF)<<16)|(((x)>>16)&0xFF)

HWND CreateTileEditor(int x, int y, int width, int height, HWND hWndParent, int tileX, int tileY) {
	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4;
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TileEditorClass", L"Tile Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN & (~WS_THICKFRAME), x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, 0, (LPARAM) tileX | (tileY << 16));
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TileEditorClass", L"Tile Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, 0, 0);
	return h;
}

LRESULT WINAPI TileEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TILEEDITORDATA *data = (TILEEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (TILEEDITORDATA *) calloc(1, sizeof(TILEEDITORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_NCHITTEST:	//make the border non-sizeable
		{
			LRESULT ht = DefMDIChildProc(hWnd, msg, wParam, lParam);
			if (ht == HTTOP || ht == HTBOTTOM || ht == HTLEFT || ht == HTRIGHT || ht == HTTOPLEFT || ht == HTTOPRIGHT || ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT)
				return HTBORDER;
			return ht;
		}
		case NV_INITIALIZE:
		{
			int tileX = LOWORD(lParam);
			int tileY = HIWORD(lParam);
			data->tileX = tileX;
			data->tileY = tileY;
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hWindowDC = BeginPaint(hWnd, &ps);

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			HDC hDC = CreateCompatibleDC(hWindowDC);
			HBITMAP hWindowBitmap = CreateCompatibleBitmap(hWindowDC, rcClient.right, rcClient.bottom);
			SelectObject(hDC, hWindowBitmap);

			SelectObject(hDC, GetStockObject(NULL_PEN));
			SelectObject(hDC, GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1));
			Rectangle(hDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);

			NCGR *ncgr = NULL;
			NCLR *nclr = NULL;
			int usedPalette = 0;
			
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
			if (hWndNclrViewer) {
				NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
				nclr = &nclrViewerData->nclr;
			}
			HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
			if (hWndNcgrViewer) {
				NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
				ncgr = &ncgrViewerData->ncgr;
				usedPalette = ncgrViewerData->selectedPalette;
			}
			
			DWORD tileBits[64];
			ncgrGetTile(ncgr, nclr, data->tileX, data->tileY, tileBits, usedPalette, FALSE);
			int width, height;
			HBITMAP hBitmap = CreateTileBitmap(tileBits, 8, 8, -1, -1, &width, &height, 32, FALSE);
			HDC hCompat = CreateCompatibleDC(hDC);
			SelectObject(hCompat, hBitmap);
			BitBlt(hDC, 0, 0, width, height, hCompat, 0, 0, SRCCOPY);

			RECT rcText;
			rcText.top = 0;
			rcText.left = 257;
			rcText.right = 256 + 50;
			rcText.bottom = 21;
			SelectObject(hDC, GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(hDC, TRANSPARENT);
			DrawTextW(hDC, L"Selected color:", -1, &rcText, DT_SINGLELINE | DT_NOCLIP | DT_VCENTER | DT_NOPREFIX);

			int paletteSize = 1 << ncgr->nBits;
			//does this go beyond the NCLR's limits?
			if (nclr) {
				int colorIndex = usedPalette << ncgr->nBits;
				if (colorIndex + paletteSize >= nclr->nColors) {
					paletteSize = nclr->nColors - colorIndex;
				}
			} else paletteSize = 0;

			SelectObject(hDC, GetStockObject(BLACK_PEN));
			WORD col = nclr->colors[data->selectedColor + usedPalette * paletteSize];
			DWORD dw = getColor(col);
			HBRUSH hbr = CreateSolidBrush(REVERSE(dw));
			SelectObject(hDC, hbr);
			if (data->selectedColor) {
				Rectangle(hDC, 350, 0, 371, 21);
			} else {
				HBRUSH lt = GetStockObject(WHITE_BRUSH);
				HBRUSH dk = CreateSolidBrush(RGB(192, 192, 192));
				SelectObject(hDC, GetStockObject(NULL_PEN));
				SelectObject(hDC, dk);
				Rectangle(hDC, 350, 0, 362, 12);
				Rectangle(hDC, 361, 11, 372, 22);
				SelectObject(hDC, lt);
				Rectangle(hDC, 361, 0, 372, 12);
				Rectangle(hDC, 350, 11, 362, 22);
				DeleteObject(dk);
				SelectObject(hDC, GetStockObject(BLACK_PEN));
				SelectObject(hDC, GetStockObject(NULL_BRUSH));
				Rectangle(hDC, 350, 0, 371, 21);
			}
			DeleteObject(hbr);

			for (int y = 0; y < paletteSize / 16; y++) {
				for (int x = 0; x < 16; x++) {
					int pX = x * 14 + 256;
					int pY = y * 14 + 31;
					int idx = x + y * 16;

					if (idx == data->selectedColor) {
						SelectObject(hDC, GetStockObject(WHITE_PEN));
					} else {
						SelectObject(hDC, GetStockObject(BLACK_PEN));
					}
					if (idx) {
						col = nclr->colors[idx + (usedPalette << ncgr->nBits)];
						dw = getColor(col);
						hbr = CreateSolidBrush(REVERSE(dw));
						SelectObject(hDC, hbr);
						Rectangle(hDC, pX, pY, pX + 14, pY + 14);
						DeleteObject(hbr);
					} else {
						HBRUSH lt = GetStockObject(WHITE_BRUSH);
						HBRUSH dk = CreateSolidBrush(RGB(192, 192, 192));
						SelectObject(hDC, GetStockObject(NULL_PEN));
						SelectObject(hDC, dk);

						Rectangle(hDC, pX, pY, pX + 8, pY + 8);
						Rectangle(hDC, pX + 7, pY + 7, pX + 15, pY + 15);
						SelectObject(hDC, lt);
						Rectangle(hDC, pX + 7, pY, pX + 15, pY + 8);
						Rectangle(hDC, pX, pY + 7, pX + 8, pY + 15);

						DeleteObject(dk);

						if (idx == data->selectedColor) SelectObject(hDC, GetStockObject(WHITE_PEN));
						else SelectObject(hDC, GetStockObject(BLACK_PEN));
						SelectObject(hDC, GetStockObject(NULL_BRUSH));
						Rectangle(hDC, pX, pY, pX + 14, pY + 14);
					}
				}
			}

			BitBlt(hWindowDC, 0, 0, rcClient.right, rcClient.bottom, hDC, 0, 0, SRCCOPY);

			EndPaint(hWnd, &ps);
			DeleteObject(hCompat);
			DeleteObject(hBitmap);
			DeleteObject(hDC);
			DeleteObject(hWindowBitmap);
			return 0;
		}
		case WM_ERASEBKGND:
		{

			return 1;
		}
		case WM_LBUTTONDOWN:
		{
			//what region is the mouse in?
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			NCGR *ncgr = NULL;
			NCLR *nclr = NULL;

			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
			if (hWndNclrViewer) {
				NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
				nclr = &nclrViewerData->nclr;
			}
			HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
			NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
			ncgr = &ncgrViewerData->ncgr;
			HWND hWndNcerViewer = nitroPaintStruct->hWndNcerViewer;

			if (mousePos.x < 0 || mousePos.y < 0 || mousePos.y >= 256) break;
			if (mousePos.x < 256) { //graphics region
				mousePos.x /= 32;
				mousePos.y /= 32;

				int tileIndex = data->tileX + data->tileY * ncgr->tilesX;
				int ptIndex = mousePos.x + mousePos.y * 8;
				BYTE *tile = ncgr->tiles[tileIndex];
				tile[ptIndex] = data->selectedColor;
				InvalidateRect(hWnd, NULL, FALSE);
				InvalidateRect(hWndNcgrViewer, NULL, FALSE);
				if(hWndNcerViewer) InvalidateRect(hWndNcerViewer, NULL, FALSE);
			} else if (mousePos.y > 31) {
				mousePos.x -= 256;
				mousePos.y -= 31;
				mousePos.x /= 14;
				mousePos.y /= 14;
				int index = mousePos.x + 16 * mousePos.y;
				if (index < (1 << ncgr->nBits) && (index + (ncgrViewerData->selectedPalette << ncgr->nBits) < nclr->nColors)) {
					data->selectedColor = index;
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}

			break;
		}
		case WM_DESTROY:
		{
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
			NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
			if(ncgrViewerData) ncgrViewerData->hWndTileEditorWindow = NULL;
			if (data) free(data);
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

VOID RegisterTileEditorClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.lpszClassName = L"TileEditorClass";
	wcex.lpfnWndProc = TileEditorWndProc;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}