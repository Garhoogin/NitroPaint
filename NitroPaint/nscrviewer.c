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
#include "gdip.h"
#include "palette.h"
#include "tiler.h"

extern HICON g_appIcon;

DWORD *renderNscrBits(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int *width, int *height, int tileMarks, int highlightColor, int selStartX, int selStartY, int selEndX, int selEndY, BOOL transparent) {
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

			if (highlightColor != -1) {
				int color = highlightColor % (1 << ncgr->nBits);
				int highlightPalette = highlightColor / (1 << ncgr->nBits);
				WORD nscrData = nscr->data[x + y * (nscr->nWidth / 8)];
				int flip = (nscrData >> 10) & 3;
				if (((nscrData >> 12) & 0xF) == highlightPalette) {
					int charBase = tileBase;
					int tileIndex = nscrData & 0x3FF;
					if (tileIndex - charBase >= 0) {
						BYTE *tile = ncgr->tiles[tileIndex - charBase];
						for (int i = 0; i < 64; i++) {
							int bIndex = i;
							if (flip & TILE_FLIPX) bIndex ^= 7;
							if (flip & TILE_FLIPY) bIndex ^= 7 << 3;
							if (tile[bIndex] == color) {
								DWORD col = block[i];
								int lightness = (col & 0xFF) + ((col >> 8) & 0xFF) + ((col >> 16) & 0xFF);
								if(lightness < 383) block[i] = 0xFFFFFFFF;
								else block[i] = 0xFF000000;
							}
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

HBITMAP renderNscr(NSCR *renderNscr, NCGR *renderNcgr, NCLR *renderNclr, int tileBase, BOOL drawGrid, int *width, int *height, int highlightNclr, int highlightTile, int highlightColor, int scale, int selStartX, int selStartY, int selEndX, int selEndY, BOOL transparent) {
	if (renderNcgr != NULL) {
		if (highlightNclr != -1) highlightNclr += tileBase;
		DWORD *bits = renderNscrBits(renderNscr, renderNcgr, renderNclr, tileBase, width, height, highlightNclr, highlightColor, selStartX, selStartY, selEndX, selEndY, transparent);

		int hovX = -1, hovY = -1;
		if (highlightTile != -1) {
			hovX = highlightTile % (renderNscr->nWidth / 8);
			hovY = highlightTile / (renderNscr->nWidth / 8);
		}
		HBITMAP hBitmap = CreateTileBitmap2(bits, *width, *height, hovX, hovY, width, height, scale, drawGrid, 8, FALSE, TRUE);
		free(bits);
		return hBitmap;
	}
	return NULL;
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
			data->hWndCharacterLabel = CreateWindow(L"STATIC", L"Character:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndPaletteLabel = CreateWindow(L"STATIC", L"Palette:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndCharacterNumber = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndPaletteNumber = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndApply = CreateWindow(L"BUTTON", L"Apply", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndAdd = CreateWindow(L"BUTTON", L"Add", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndSubtract = CreateWindow(L"BUTTON", L"Subtract", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndTileBaseLabel = CreateWindow(L"STATIC", L"Tile Base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndTileBase = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndSize = CreateWindow(L"STATIC", L"Size: 0x0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
			data->hWndSelectionSize = CreateWindow(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
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
			data->frameData.contentWidth = getDimension(data->nscr.nWidth / 8, data->showBorders, data->scale);
			data->frameData.contentHeight = getDimension(data->nscr.nHeight / 8, data->showBorders, data->scale);

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
							COLOR32 *bits = renderNscrBits(nscr, ncgr, nclr, data->tileBase, &width, &height, -1, -1, -1, -1, -1, -1, TRUE);
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
						break;
					}
					case ID_FILE_SAVEAS:
					case ID_FILE_SAVE:
					{
						if (data->szOpenFile[0] == L'\0' || LOWORD(wParam) == ID_FILE_SAVEAS) {
							LPCWSTR filter = L"NSCR Files (*.nscr)\0*.nscr\0All Files\0*.*\0";
							switch (data->nscr.header.format) {
								case NSCR_TYPE_BIN:
								case NSCR_TYPE_HUDSON:
								case NSCR_TYPE_HUDSON2:
									filter = L"Screen Files (*.bin, *nsc.bin, *isc.bin, *.nbfs)\0*.bin;*.nbfs\0All Files\0*.*\0";
									break;
								case NSCR_TYPE_COMBO:
									filter = L"Combination Files (*.dat, *.bin)\0*.dat;*.bin\0";
									break;
								case NSCR_TYPE_NC:
									filter = L"NSC Files (*.nsc)\0*.nsc\0All Files\0*.*\0";
									break;
								case NSCR_TYPE_AC:
									filter = L"ASC Files (*.asc)\0*.asc\0All Files\0*.*\0";
									break;
							}
							LPWSTR path = saveFileDialog(getMainWindow(hWnd), L"Save As...", filter, L"nscr");
							if (path != NULL) {
								EditorSetFile(hWnd, path);
								free(path);
							}
						}
						ScrWriteFile(&data->nscr, data->szOpenFile);
						break;
					}
					case ID_NSCRMENU_IMPORTBITMAPHERE:
					{
						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						HWND h = CreateWindow(L"NscrBitmapImportClass", L"Import Bitmap", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, hWndMain, NULL, NULL, NULL);
						SendMessage(h, NV_INITIALIZE, 0, (LPARAM) hWnd);
						WORD d = data->nscr.data[data->contextHoverX + data->contextHoverY * (data->nscr.nWidth >> 3)];
						SendMessage(h, NV_INITIMPORTDIALOG, d, data->contextHoverX | (data->contextHoverY << 16));
						ShowWindow(h, SW_SHOW);
						break;
					}
					case ID_NSCRMENU_COPY:
					{
						OpenClipboard(hWnd);
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
						HWND hWndMain = getMainWindow(hWnd);
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
								-1, -1, -1, -1, -1, -1, TRUE);
							COLOR32 *sub = ImgCrop(bm, wholeWidth, wholeHeight, tileX * 8, tileY * 8, tilesX * 8, tilesY * 8);
							ImgSwapRedBlue(sub, tilesX * 8, tilesY * 8);
							copyBitmap(sub, width, height);
							free(bm);
							free(sub);
						}

						GlobalUnlock(hString);
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
						}

						GlobalUnlock(hString);
						break;
					}
					case ID_NSCRMENU_DESELECT:
					{
						data->selStartX = -1;
						data->selStartY = -1;
						data->selEndX = -1;
						data->selEndY = -1;
						InvalidateRect(hWnd, NULL, FALSE);
						SendMessage(data->hWndSelectionSize, WM_SETTEXT, 0, (LPARAM) L"");
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
			} else if (lParam) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->hWndApply || hWndControl == data->hWndAdd || hWndControl == data->hWndSubtract) {
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
							WORD oldValue = data->nscr.data[i];
							int newPalette = (oldValue >> 12) & 0xF;
							int newCharacter = oldValue & 0x3FF;
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
							data->nscr.data[i] = (oldValue & 0xC00) | newCharacter | (newPalette << 12);
						}
					}
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
			ObjFree((OBJECT_HEADER *) &data->nscr);
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
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

double calculatePaletteCharError(RxReduction *reduction, COLOR32 *block, RxYiqColor *pals, unsigned char *character, int flip, double maxError) {
	double error = 0;
	for (int i = 0; i < 64; i++) { //0b111 111
		int srcIndex = i;
		if (flip & TILE_FLIPX) srcIndex ^= 7;
		if (flip & TILE_FLIPY) srcIndex ^= 7 << 3;

		//convert source image pixel
		RxYiqColor yiq;
		COLOR32 col = block[srcIndex];
		RxConvertRgbToYiq(col, &yiq);

		//char pixel
		int index = character[i];
		RxYiqColor *matchedYiq = pals + index;
		int matchedA = index > 0 ? 255 : 0;
		if (matchedA == 0 && yiq.a < 128) {
			continue; //to prevent superfluous non-alpha difference
		}

		//diff
		double dy = reduction->yWeight * (reduction->lumaTable[yiq.y] - reduction->lumaTable[matchedYiq->y]);
		double di = reduction->iWeight * (yiq.i - matchedYiq->i);
		double dq = reduction->qWeight * (yiq.q - matchedYiq->q);
		double da = 40 * (yiq.a - matchedA);
		

		error += dy * dy;
		if (da != 0.0) error += da * da;
		if (error >= maxError) return maxError;
		error += di * di + dq * dq;
		if (error >= maxError) return maxError;
	}
	return error;
}

double calculateBestPaletteCharError(RxReduction *reduction, COLOR32 *block, RxYiqColor *pals, unsigned char *character, int *flip, double maxError) {
	double e00 = calculatePaletteCharError(reduction, block, pals, character, TILE_FLIPNONE, maxError);
	if (e00 == 0) {
		*flip = TILE_FLIPNONE;
		return e00;
	}
	double e01 = calculatePaletteCharError(reduction, block, pals, character, TILE_FLIPX, maxError);
	if (e01 == 0) {
		*flip = TILE_FLIPX;
		return e01;
	}
	double e10 = calculatePaletteCharError(reduction, block, pals, character, TILE_FLIPY, maxError);
	if (e10 == 0) {
		*flip = TILE_FLIPY;
		return e10;
	}
	double e11 = calculatePaletteCharError(reduction, block, pals, character, TILE_FLIPXY, maxError);
	if (e11 == 0) {
		*flip = TILE_FLIPXY;
		return e11;
	}

	if (e00 <= e01 && e00 <= e10 && e00 <= e11) {
		*flip = TILE_FLIPNONE;
		return e00;
	}
	if (e01 <= e00 && e01 <= e10 && e01 <= e11) {
		*flip = TILE_FLIPX;
		return e01;
	}
	if (e10 <= e00 && e10 <= e01 && e10 <= e11) {
		*flip = TILE_FLIPY;
		return e10;
	}
	*flip = TILE_FLIPXY;
	return e11;
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

void nscrImportBitmap(NCLR *nclr, NCGR *ncgr, NSCR *nscr, COLOR32 *px, int width, int height,
					  int writeScreen, int writeCharacterIndices,
					  int tileBase, int nPalettes, int paletteNumber, int paletteOffset, 
					  int paletteSize, BOOL newPalettes, int writeCharBase, int nMaxChars,
					  BOOL newCharacters, BOOL dither, float diffuse, int maxTilesX, int maxTilesY,
					  int nscrTileX, int nscrTileY, int balance, int colorBalance, int enhanceColors,
					  int *progress, int *progressMax) {
	int tilesX = width / 8;
	int tilesY = height / 8;
	int paletteStartFrom0 = 0;
	int maxPaletteSize = ncgr->nBits == 4 ? 16 : 256;

	//sanity checks
	if (tilesX > maxTilesX) tilesX = maxTilesX;
	if (tilesY > maxTilesY) tilesY = maxTilesY;
	if (paletteOffset >= maxPaletteSize) paletteOffset = maxPaletteSize - 1;
	if (paletteSize > maxPaletteSize) paletteSize = maxPaletteSize;
	if (paletteOffset + paletteSize > maxPaletteSize) paletteSize = maxPaletteSize - paletteOffset;
	if (writeCharBase >= ncgr->nTiles) writeCharBase = ncgr->nTiles - 1;
	if (writeCharBase + nMaxChars > ncgr->nTiles) nMaxChars = ncgr->nTiles - writeCharBase;

	//if no write screen, still set some proper bounds.
	if (!writeScreen) {
		paletteNumber = 0;
		if (ncgr->nBits == 4) {
			nPalettes = nclr->nColors / 16;
		} else {
			nPalettes = 1;
		}
	}

	*progressMax = tilesX * tilesY * 2;

	BGTILE *blocks = (BGTILE *) calloc(tilesX * tilesY, sizeof(BGTILE));
	COLOR32 *pals = (COLOR32 *) calloc(16 * maxPaletteSize, 4);

	//split image into 8x8 chunks
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			COLOR32 *block = blocks[x + y * tilesX].px;
			memcpy(block, px + srcOffset, 32);
			memcpy(block + 8, px + srcOffset + width, 32);
			memcpy(block + 16, px + srcOffset + width * 2, 32);
			memcpy(block + 24, px + srcOffset + width * 3, 32);
			memcpy(block + 32, px + srcOffset + width * 4, 32);
			memcpy(block + 40, px + srcOffset + width * 5, 32);
			memcpy(block + 48, px + srcOffset + width * 6, 32);
			memcpy(block + 56, px + srcOffset + width * 7, 32);

			for (int i = 0; i < 8 * 8; i++) {
				int a = (block[i] >> 24) & 0xFF;
				if (a < 128) block[i] = 0; //make transparent pixels transparent black
				else block[i] |= 0xFF000000; //opaque
			}
		}
	}

	int charBase = tileBase;
	int nscrTilesX = nscr->nWidth / 8;
	int nscrTilesY = nscr->nHeight / 8;
	uint16_t *nscrData = nscr->data;

	//create dummy reduction to setup parameters for color matching
	RxReduction *reduction = (RxReduction *) calloc(1, sizeof(RxReduction));
	RxInit(reduction, balance, colorBalance, 15, enhanceColors, paletteSize - !paletteOffset);

	//generate an nPalettes color palette
	if (newPalettes) {
		if (writeScreen) {
			//if we're writing the screen, we can write the palette as normal.
			RxCreateMultiplePalettesEx(px, tilesX, tilesY, pals, 0, nPalettes, maxPaletteSize, paletteSize, 
				paletteOffset, balance, colorBalance, enhanceColors, progress);
		} else {
			//else, we need to be a bit more methodical. Lucky for us, though, the palettes are already partitioned.
			//due to this, we can't respect user-set palette base and count. We're at the whim of the screen's
			//existing data. Iterate all 16 palettes. If tiles in our region use them, construct a histogram and
			//write its palette data.
			//first read in original palette, we'll write over it.
			for (int i = 0; i < nclr->nColors; i++) {
				pals[i] = ColorConvertFromDS(nclr->colors[i]);
			}
			for (int palNo = 0; palNo < nPalettes; palNo++) {
				int nTilesHistogram = 0;

				for (int y = 0; y < tilesY; y++) {
					for (int x = 0; x < tilesX; x++) {
						uint16_t d = nscrData[x + nscrTileX + (y + nscrTileY) * nscrTilesX];
						int thisPalNo = (d & 0xF000) >> 12;
						if (thisPalNo != palNo) continue;

						nTilesHistogram++;
						RxHistAdd(reduction, blocks[x + y * tilesX].px, 8, 8);
					}
				}

				//if we counted tiles, create palette
				if (nTilesHistogram > 0) {
					RxHistFinalize(reduction);
					RxComputePalette(reduction);
					
					COLOR32 *outPal = pals + palNo * maxPaletteSize + paletteOffset + !paletteOffset;
					for (int i = 0; i < paletteSize - !paletteOffset; i++) {
						uint8_t r = reduction->paletteRgb[i][0];
						uint8_t g = reduction->paletteRgb[i][1];
						uint8_t b = reduction->paletteRgb[i][2];
						outPal[i] = r | (g << 8) | (b << 16);
					}
					qsort(outPal, paletteSize - !paletteOffset, sizeof(COLOR32), RxColorLightnessComparator);
					if (paletteOffset == 0) pals[palNo * maxPaletteSize] = 0xFF00FF;
					RxHistClear(reduction);
				}
			}
		}
	} else {
		COLOR *destPalette = nclr->colors + paletteNumber * maxPaletteSize;
		int nColors = nPalettes * paletteSize;
		for (int i = 0; i < nColors; i++) {
			COLOR c = destPalette[i];
			pals[i] = ColorConvertFromDS(c);
		}
	}

	//write to NCLR
	if (newPalettes) {
		COLOR *destPalette = nclr->colors + paletteNumber * maxPaletteSize;
		for (int i = 0; i < nPalettes; i++) {
			COLOR *dest = destPalette + i * maxPaletteSize;
			for (int j = paletteOffset; j < paletteOffset + paletteSize; j++) {
				COLOR32 col = (pals + i * maxPaletteSize)[j];
				dest[j] = ColorConvertToDS(col);
			}
		}
	}

	//pre-convert palette to YIQ
	RxYiqColor *palsYiq = (RxYiqColor *) calloc(nPalettes * paletteSize, sizeof(RxYiqColor));
	for (int i = 0; i < nPalettes * paletteSize; i++) {
		RxConvertRgbToYiq(pals[i], palsYiq + i);
	}

	if (!writeScreen) {
		//no write screen, only character can be written (palette was already dealt with)
		if (newCharacters) {
			//just write each tile
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					BGTILE *tile = blocks + x + y * tilesX;

					uint16_t d = nscrData[x + nscrTileX + (y + nscrTileY) * nscrTilesX];
					int charIndex = (d & 0x3FF) - charBase;
					int palIndex = (d & 0xF000) >> 12;
					int flip = (d & 0x0C00) >> 10;
					if (charIndex < 0) continue;
					
					BYTE *chr = ncgr->tiles[charIndex];
					COLOR32 *thisPalette = pals + palIndex * maxPaletteSize + paletteOffset + !paletteOffset;
					RxReduceImageEx(tile->px, NULL, 8, 8, thisPalette, paletteSize - !paletteOffset, 
						FALSE, TRUE, FALSE, dither ? diffuse : 0.0f, balance, colorBalance, enhanceColors);
					for (int i = 0; i < 64; i++) {
						COLOR32 c = tile->px[i];
						int srcX = i % 8;
						int srcY = i / 8;
						int dstX = srcX ^ (flip & TILE_FLIPX ? 7 : 0);
						int dstY = srcY ^ (flip & TILE_FLIPY ? 7 : 0);

						int cidx = 0;
						if ((c >> 24) >= 0x80) cidx = RxPaletteFindClosestColorSimple(c, thisPalette, paletteSize - !paletteOffset) + paletteOffset + !paletteOffset;
						chr[dstX + dstY * 8] = cidx;
					}
				}
			}
		}
	} else if (writeCharacterIndices) {
		//write screen, write character indices
		//if we can write characters, do a normal character compression.
		if (newCharacters) {
			//do normal character compression.
			setupBgTilesEx(blocks, tilesX * tilesY, ncgr->nBits, pals, paletteSize, nPalettes, 0, paletteOffset, 
				dither, diffuse, balance, colorBalance, enhanceColors);
			int nOutChars = performCharacterCompression(blocks, tilesX * tilesY, ncgr->nBits, nMaxChars, pals, paletteSize, 
				nPalettes, paletteNumber, paletteOffset, balance, colorBalance, progress);

			//keep track of master tiles and how they map to real character indices
			int *masterMap = (int *) calloc(tilesX * tilesY, sizeof(int));

			//write chars
			int nCharsWritten = 0;
			int indexMask = (1 << ncgr->nBits) - 1;
			for (int i = 0; i < tilesX * tilesY; i++) {
				BGTILE *tile = blocks + i;
				if (tile->masterTile != i) continue;

				//master tile
				BYTE *destTile = ncgr->tiles[nCharsWritten + writeCharBase];
				for (int j = 0; j < 64; j++)
					destTile[j] = tile->indices[j] & indexMask;
				masterMap[i] = nCharsWritten + writeCharBase;
				nCharsWritten++;
			}
			
			//next, write screen.
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					BGTILE *tile = blocks + x + y * tilesX;
					int palette = tile->palette + paletteNumber;
					int charIndex = masterMap[tile->masterTile] + charBase;

					if (x + nscrTileX < nscrTilesX && y + nscrTileY < nscrTilesY) {
						uint16_t d = (tile->flipMode << 10) | (palette << 12) | charIndex;
						nscrData[x + nscrTileX + (y + nscrTileY) * (nscr->nWidth >> 3)] = d;
					}
				}
			}
			free(masterMap);
		} else {
			//else, we have to get by just using the screen itself.
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					BGTILE *tile = blocks + x + y * tilesX;
					COLOR32 *block = tile->px;

					//search best tile.
					int chosenCharacter = 0, chosenPalette = 0, chosenFlip = TILE_FLIPNONE;
					double minError = 1e32;
					for (int j = 0; j < ncgr->nTiles; j++) {
						for (int i = 0; i < nPalettes; i++) {
							int charId = j, mode;
							double err = calculateBestPaletteCharError(reduction, block, palsYiq + i * maxPaletteSize, ncgr->tiles[charId], &mode, minError);
							if (err < minError) {
								chosenCharacter = charId;
								chosenPalette = i;
								minError = err;
								chosenFlip = mode;
							}
						}
					}

					int nscrX = x + nscrTileX;
					int nscrY = y + nscrTileY;

					if (nscrX < nscrTilesX && nscrY < nscrTilesY) {
						uint16_t d = 0;
						d = d & 0xFFF;
						d |= (chosenPalette + paletteNumber) << 12;
						d &= 0xFC00;
						d |= (chosenCharacter + charBase);
						d |= chosenFlip << 10;
						nscrData[nscrX + nscrY * nscrTilesX] = d;
					}
				}
			}
		}

	} else {
		//write screen, no write character indices.
		//next, start palette matching. See which palette best fits a tile, set it in the NSCR, then write the bits to the NCGR.
		if (newCharacters) {
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					COLOR32 *block = blocks[x + y * tilesX].px;

					double leastError = 1e32;
					int leastIndex = 0;
					for (int i = 0; i < nPalettes; i++) {
						COLOR32 *thisPal = pals + i * maxPaletteSize + paletteOffset + !paletteOffset;
						double err = RxComputePaletteError(reduction, block, 64, thisPal, paletteSize - !paletteOffset, 128, leastError);
						if (err < leastError) {
							leastError = err;
							leastIndex = i;
						}
					}

					int nscrX = x + nscrTileX;
					int nscrY = y + nscrTileY;

					if (nscrX < nscrTilesX && nscrY < nscrTilesY) {
						uint16_t d = nscrData[nscrX + nscrY * nscrTilesX];
						d = d & 0x3FF;
						d |= (leastIndex + paletteNumber) << 12;
						nscrData[nscrX + nscrY * (nscr->nWidth >> 3)] = d;

						int charOrigin = d & 0x3FF;
						if (charOrigin - charBase < 0) continue;
						unsigned char *ncgrTile = ncgr->tiles[charOrigin - charBase];

						COLOR32 *thisPal = pals + leastIndex * maxPaletteSize + paletteOffset + !paletteOffset;
						RxReduceImageEx(block, NULL, 8, 8, thisPal, paletteSize - !paletteOffset, FALSE, TRUE, FALSE, dither ? diffuse : 0.0f,
							balance, colorBalance, enhanceColors);
						for (int i = 0; i < 64; i++) {
							if ((block[i] & 0xFF000000) < 0x80) ncgrTile[i] = 0;
							else {
								int index = paletteOffset + !paletteOffset + RxPaletteFindClosestColorSimple(block[i], thisPal, paletteSize - !paletteOffset);
								ncgrTile[i] = index;
							}
						}
					}
				}
			}
		} else {
			//no new character
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					COLOR32 *block = blocks[x + y * tilesX].px;
					int nscrX = x + nscrTileX;
					int nscrY = y + nscrTileY;

					//check bounds
					if (nscrX < nscrTilesX && nscrY < nscrTilesY) {

						//find what combination of palette and flip minimizes the error.
						uint16_t oldData = nscrData[nscrX + nscrY * nscrTilesX];
						int charId = (oldData & 0x3FF) - charBase, chosenPalette = 0, chosenFlip = TILE_FLIPNONE;
						double minError = 1e32;
						for (int i = 0; i < nPalettes; i++) {
							int mode;
							double err = calculateBestPaletteCharError(reduction, block, palsYiq + i * maxPaletteSize, ncgr->tiles[charId], &mode, minError);
							if (err < minError) {
								chosenPalette = i;
								minError = err;
								chosenFlip = mode;
							}
						}

						uint16_t d = 0;
						d = d & 0xFFF;
						d |= (chosenPalette + paletteNumber) << 12;
						d &= 0xFC00;
						d |= (charId + charBase);
						d |= chosenFlip << 10;
						nscrData[nscrX + nscrY * nscrTilesX] = d;
					}
				}
			}
		}
	}

	free(palsYiq);
	RxDestroy(reduction);
	free(reduction);

	free(blocks);
	free(pals);
}

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
	free(importData->px);
	free(data);
}

DWORD WINAPI threadedNscrImportBitmapInternal(LPVOID lpParameter) {
	PROGRESSDATA *progressData = (PROGRESSDATA *) lpParameter;
	NSCRIMPORTDATA *importData = (NSCRIMPORTDATA *) progressData->data;
	nscrImportBitmap(importData->nclr, importData->ncgr, importData->nscr, importData->px, importData->width, importData->height,
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

			CreateWindow(L"STATIC", L"Bitmap:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndBitmapName = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 10, width - 10 - 50 - 70, 22, hWnd, NULL, NULL, NULL);
			data->hWndBrowseButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, width - 10 - 50, 10, 50, 22, hWnd, NULL, NULL, NULL);

			data->hWndWriteScreenCheckbox = CreateWindow(L"BUTTON", L"Overwrite Screen", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, topY, 150, 22, hWnd, NULL, NULL, NULL);
			data->hWndWriteCharIndicesCheckbox = CreateWindow(L"BUTTON", L"Overwrite Character Indices", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, topY + 27, 150, 22, hWnd, NULL, NULL, NULL);

			data->hWndNewPaletteCheckbox = CreateWindow(L"BUTTON", L"Overwrite Palette", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, middleY, 150, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palettes:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPalettesInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, leftX + 85, middleY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27 * 2, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, leftX + 85, middleY + 27 * 2, 100, 200, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Size:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27 * 3, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteSize = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"16", WS_VISIBLE | WS_CHILD | ES_NUMBER, leftX + 85, middleY + 27 * 3, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Offset:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27 * 4, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteOffset = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER, leftX + 85, middleY + 27 * 4, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndNewCharactersCheckbox = CreateWindow(L"BUTTON", L"Overwrite Character", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, rightX, middleY, 150, 22, hWnd, NULL, NULL, NULL);
			data->hWndDitherCheckbox = CreateWindow(L"BUTTON", L"Dither", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, rightX, middleY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Diffuse:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, middleY + 27 * 2, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndDiffuseAmount = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, rightX + 85, middleY + 27 * 2, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, middleY + 27 * 3, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndCharacterBase = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER, rightX + 85, middleY + 27 * 3, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Count:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, middleY + 27 * 4, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndCharacterCount = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1024", WS_VISIBLE | WS_CHILD | ES_NUMBER, rightX + 85, middleY + 27 * 4, 100, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndEnhanceColors = CreateWindow(L"BUTTON", L"Enhance Colors", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, bottomY + 27 * 2, 200, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Lightness", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Green", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Red", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndColorBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY + 27, 200, 22, hWnd, NULL, NULL, NULL);


			data->hWndImportButton = CreateWindow(L"BUTTON", L"Import", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, width / 2 - 100, height - 32, 200, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"BUTTON", L"Screen", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, topY - 18, rightX + boxWidth - leftX, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Palette", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, middleY - 18, boxWidth, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Graphics", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, rightX - 10, middleY - 18, boxWidth, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Color", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, bottomY - 18, rightX + boxWidth - leftX, boxHeight3, hWnd, NULL, NULL, NULL);

			for (int i = 0; i < 16; i++) {
				WCHAR textBuffer[4];
				wsprintf(textBuffer, L"%d", i);
				SendMessage(data->hWndPaletteInput, CB_ADDSTRING, wcslen(textBuffer), (LPARAM) textBuffer);
			}
			SendMessage(data->hWndWriteScreenCheckbox, BM_SETCHECK, 1, 0);
			SendMessage(data->hWndDitherCheckbox, BM_SETCHECK, 1, 0);
			SendMessage(data->hWndNewPaletteCheckbox, BM_SETCHECK, 1, 0);
			SendMessage(data->hWndNewCharactersCheckbox, BM_SETCHECK, 1, 0);
			SendMessage(data->hWndBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);
			SendMessage(data->hWndColorBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndColorBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);

			setStyle(data->hWndCharacterBase, TRUE, WS_DISABLED);
			setStyle(data->hWndCharacterCount, TRUE, WS_DISABLED);

			SetWindowSize(hWnd, width, height);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
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
					DWORD *px = ImgRead(textBuffer, &width, &height);

					SendMessage(data->hWndDiffuseAmount, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					float diffuse = ((float) _wtoi(textBuffer)) * 0.01f;

					SendMessage(data->hWndCharacterBase, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					int characterBase = _wtoi(textBuffer);
					SendMessage(data->hWndCharacterCount, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					int characterCount = _wtoi(textBuffer);

					SendMessage(data->hWndPalettesInput, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					int nPalettes = _wtoi(textBuffer);
					SendMessage(data->hWndPaletteSize, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					int paletteSize = _wtoi(textBuffer);
					SendMessage(data->hWndPaletteOffset, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) textBuffer);
					int paletteOffset = _wtoi(textBuffer);
					if (nPalettes > 16) nPalettes = 16;

					int paletteNumber = SendMessage(data->hWndPaletteInput, CB_GETCURSEL, 0, 0);
					int dither = SendMessage(data->hWndDitherCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int newPalettes = SendMessage(data->hWndNewPaletteCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int newCharacters = SendMessage(data->hWndNewCharactersCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int balance = SendMessage(data->hWndBalance, TBM_GETPOS, 0, 0);
					int colorBalance = SendMessage(data->hWndColorBalance, TBM_GETPOS, 0, 0);
					int enhanceColors = SendMessage(data->hWndEnhanceColors, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int writeCharacterIndices = SendMessage(data->hWndWriteCharIndicesCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int writeScreen = SendMessage(data->hWndWriteScreenCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;

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
			HBITMAP hBitmap = renderNscr(nscr, ncgr, nclr, data->tileBase, data->showBorders, &bitmapWidth, &bitmapHeight, hoveredNcgrTile, hoveredNscrTile, highlightColor, data->scale, data->selStartX, data->selStartY, data->selEndX, data->selEndY, data->transparent);

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
		case WM_LBUTTONUP:
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
	EditorRegister(L"NscrViewerClass", NscrViewerWndProc, L"Screen Editor", sizeof(NSCRVIEWERDATA));
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
