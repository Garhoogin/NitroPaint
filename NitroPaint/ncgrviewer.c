#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "nscrviewer.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "resource.h"
#include "tiler.h"
#include "gdip.h"
#include "palette.h"
#include "tileeditor.h"

extern HICON g_appIcon;

DWORD * renderNcgrBits(NCGR * renderNcgr, NCLR * renderNclr, BOOL drawGrid, BOOL drawChecker, int * width, int * height, int markX, int markY, int previewPalette) {
	int xTiles = renderNcgr->tilesX;
	int yTiles = renderNcgr->tilesY;
	int bitmapWidth = xTiles << 3;
	int bitmapHeight = yTiles << 3;
	if (drawGrid) {
		bitmapWidth = xTiles * 9 + 1;
		bitmapHeight = yTiles * 9 + 1;
	}

	DWORD * bits = (DWORD *) calloc(bitmapWidth * bitmapHeight, 4);

	//draw tiles
	DWORD block[64];

	for (int y = 0; y < yTiles; y++) {
		for (int x = 0; x < xTiles; x++) {
			ncgrGetTile(renderNcgr, renderNclr, x, y, block, previewPalette, drawChecker);
			if (x == markX && y == markY) {
				for (int i = 0; i < 64; i++) {
					DWORD d = block[i];
					d ^= 0xFFFFFF;
					block[i] = d;
				}
			}
			DWORD offset = 8 * x + 8 * y * bitmapWidth;
			if (drawGrid) {
				offset = 9 * x + 9 * y * bitmapWidth + 1 + bitmapWidth;
			}
			for (int i = 0; i < 8; i++) {
				CopyMemory(bits + offset + i * bitmapWidth, block + 8 * i, 32);
			}
		}
	}
	/*for (int i = 0; i < bitmapWidth * bitmapHeight; i++) {
		DWORD d = bits[i];
		d = ((d & 0xFF) << 16) | (d & 0xFF00FF00) | ((d >> 16) & 0xFF);
		bits[i] = d;
	}*/
	*width = bitmapWidth;
	*height = bitmapHeight;
	return bits;
}

DWORD *NcgrToBitmap(NCGR *ncgr, int usePalette, NCLR *nclr, int highlightColor) {
	int width = ncgr->tilesX * 8;
	DWORD *bits = (DWORD *) malloc(ncgr->tilesX * ncgr->tilesY * 64 * 4);

	int tileNumber = 0;
	for (int y = 0; y < ncgr->tilesY; y++) {
		for (int x = 0; x < ncgr->tilesX; x++) {
			BYTE *tile = ncgr->tiles[tileNumber];
			DWORD block[64];
			ncgrGetTile(ncgr, nclr, x, y, block, usePalette, TRUE);
			if (highlightColor != -1) {
				for (int i = 0; i < 64; i++) {
					if (tile[i] != highlightColor) continue;
					DWORD col = block[i];
					int lightness = (col & 0xFF) + ((col >> 8) & 0xFF) + ((col >> 16) & 0xFF);
					if (lightness < 383) block[i] = 0xFFFFFFFF;
					else block[i] = 0xFF000000;
				}
			}

			int imgX = x * 8, imgY = y * 8;
			int offset = imgX + imgY * width;
			memcpy(bits + offset, block, 32);
			memcpy(bits + offset + width, block + 8, 32);
			memcpy(bits + offset + width * 2, block + 16, 32);
			memcpy(bits + offset + width * 3, block + 24, 32);
			memcpy(bits + offset + width * 4, block + 32, 32);
			memcpy(bits + offset + width * 5, block + 40, 32);
			memcpy(bits + offset + width * 6, block + 48, 32);
			memcpy(bits + offset + width * 7, block + 56, 32);


			tileNumber++;
		}
	}
	return bits;
}

VOID PaintNcgrViewer(HWND hWnd, NCGRVIEWERDATA *data, HDC hDC, int xMin, int yMin, int xMax, int yMax) {
	int width = data->ncgr.tilesX * 8;
	int height = data->ncgr.tilesY * 8;

	NCLR *nclr = NULL;
	HWND hWndMain = GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	if (nitroPaintStruct->hWndNclrViewer) {
		HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
		NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLong(hWndNclrViewer, 0);
		nclr = &nclrViewerData->nclr;
	}

	int highlightColor = data->verifyColor % (1 << data->ncgr.nBits);
	if ((data->verifyFrames & 1) == 0) highlightColor = -1;
	DWORD *px = NcgrToBitmap(&data->ncgr, data->selectedPalette, nclr, highlightColor);
	int outWidth, outHeight;
	HBITMAP hTiles = CreateTileBitmap(px, width, height, data->hoverX, data->hoverY, &outWidth, &outHeight, data->scale, data->showBorders);

	HDC hCompat = CreateCompatibleDC(hDC);
	SelectObject(hCompat, hTiles);
	BitBlt(hDC, 0, 0, outWidth, outHeight, hCompat, 0, 0, SRCCOPY);
	DeleteObject(hCompat);
	DeleteObject(hTiles);

	free(px);
}

HWND getMainWindow(HWND hWnd) {
	HWND hWndMdi = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
	HWND hWndMain = (HWND) GetWindowLong(hWndMdi, GWL_HWNDPARENT);
	return hWndMain;
}

#define NV_INITIALIZE (WM_USER+1)
#define NV_RECALCULATE (WM_USER+2)

LRESULT WINAPI NcgrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (NCGRVIEWERDATA *) calloc(1, sizeof(NCGRVIEWERDATA));
		SetWindowLongPtr(hWnd, 0, (LONG) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			data->frameData.contentWidth = 256;
			data->frameData.contentHeight = 256;
			data->frameData.paddingBottom = 21 * 2;
			data->showBorders = 1;
			data->scale = 1;
			data->selectedPalette = 0;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;

			data->hWndViewer = CreateWindow(L"NcgrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 256, 256, hWnd, NULL, NULL, NULL);
			data->hWndCharacterLabel = CreateWindow(L"STATIC", L" Character 0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteDropdown = CreateWindowEx(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 0, 0, 200, 100, hWnd, NULL, NULL, NULL);
			data->hWndWidthDropdown = CreateWindowEx(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 0, 0, 200, 100, hWnd, NULL, NULL, NULL);
			data->hWndWidthLabel = CreateWindow(L"STATIC", L" Width:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 100, 21, hWnd, NULL, NULL, NULL);

			WCHAR bf[] = L"Palette 00";
			for (int i = 0; i < 16; i++) {
				wsprintfW(bf, L"Palette %02d", i);
				SendMessage(data->hWndPaletteDropdown, CB_ADDSTRING, 0, (LPARAM) bf);
			}
			SendMessage(data->hWndPaletteDropdown, CB_SETCURSEL, 0, 0);

			//read config data
			if (!g_configuration.ncgrViewerConfiguration.gridlines) {
				data->showBorders = 0;
				CheckMenuItem(GetMenu((HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT)), ID_VIEW_GRIDLINES, MF_UNCHECKED);
			}
			break;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->ncgr, (NCGR *) lParam, sizeof(NCGR));
			WCHAR titleBuffer[MAX_PATH + 15];
			if (!g_configuration.fullPaths) path = GetFileName(path);
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - NCGR Viewer", 30);
			SetWindowText(hWnd, titleBuffer);
			data->frameData.contentWidth = getDimension(data->ncgr.tilesX, data->showBorders, data->scale);
			data->frameData.contentHeight = getDimension(data->ncgr.tilesY, data->showBorders, data->scale);

			RECT rc = { 0 };
			rc.right = data->frameData.contentWidth;
			rc.bottom = data->frameData.contentHeight;
			if (rc.right < 150) rc.right = 150;
			AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
			int width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL); //+4 to account for WS_EX_CLIENTEDGE
			int height = rc.bottom - rc.top + 4 + 42 + 22 + GetSystemMetrics(SM_CYHSCROLL); //+42 to account for combobox
			width += 1, height += 1;
			SetWindowPos(hWnd, HWND_TOP, 0, 0, width, height, SWP_NOMOVE);


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

			//guess a tile base based on an open NCGR (if any)
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNscrViewer = nitroPaintStruct->hWndNscrViewer;
			if (hWndNscrViewer) {
				NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0);
				NSCR *nscr = &nscrViewerData->nscr;
				if (nscr->nHighestIndex >= data->ncgr.nTiles) {
					NscrViewerSetTileBase(hWndNscrViewer, nscr->nHighestIndex + 1 - data->ncgr.nTiles);
				} else {
					NscrViewerSetTileBase(hWndNscrViewer, 0);
				}
			}
			break;
		}
		case WM_COMMAND:
		{
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				WORD notification = HIWORD(wParam);
				if (notification == CBN_SELCHANGE && hWndControl == data->hWndPaletteDropdown) {
					int sel = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					data->selectedPalette = sel;
					InvalidateRect(hWnd, NULL, FALSE);

					HWND hWndMain = getMainWindow(hWnd);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
				} else if (notification == CBN_SELCHANGE && hWndControl == data->hWndWidthDropdown) {
					WCHAR text[16];
					int selected = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					SendMessage(hWndControl, CB_GETLBTEXT, (WPARAM) selected, (LPARAM) text);
					int width = _wtol(text);
					data->ncgr.tilesX = width;
					data->ncgr.tilesY = data->ncgr.nTiles / width;

					RECT rcClient;
					GetClientRect(hWnd, &rcClient);

					SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}
			if (lParam == 0 && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
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
						SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
						InvalidateRect(hWnd, NULL, FALSE);
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
						for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
							int id = ids[i];
							CheckMenuItem(GetMenu(getMainWindow(hWnd)), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
						}

						SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
						InvalidateRect(hWnd, NULL, FALSE);
						RedrawWindow(data->hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
						break;
					}
					case ID_NCGRMENU_IMPORTBITMAPHERE:
					case ID_NCGRMENU_IMPORTBITMAPHEREANDREPLACEPALETTE:
					{
						LPWSTR path = openFileDialog(hWnd, L"Select Bitmap", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
						if (!path) break;

						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
						HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
						NCLR *nclr = NULL;
						NCGR *ncgr = &data->ncgr;
						if (hWndNclrViewer) {
							NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
							nclr = &nclrViewerData->nclr;
						}

						BOOL createPalette = (LOWORD(wParam) == ID_NCGRMENU_IMPORTBITMAPHEREANDREPLACEPALETTE);
						BOOL dither = MessageBox(hWnd, L"Use dithering?", L"Dither", MB_ICONQUESTION | MB_YESNO) == IDYES;
						int originX = data->contextHoverX;
						int originY = data->contextHoverY;
						int paletteNumber = data->selectedPalette;
						COLOR *nitroPalette = nclr->colors + (paletteNumber << ncgr->nBits);
						int paletteSize = 1 << data->ncgr.nBits;
						if ((data->selectedPalette << ncgr->nBits) + paletteSize >= nclr->nColors) {
							paletteSize = nclr->nColors - (data->selectedPalette << ncgr->nBits);
						}

						DWORD *palette = (DWORD *) calloc(paletteSize, 4);

						int width, height;
						DWORD *pixels = gdipReadImage(path, &width, &height);

						//if we use an existing palette, decode the palette values.
						//if we do not use an existing palette, generate one.
						if (!createPalette) {
							//decode the palette
							for (int i = 0; i < paletteSize; i++) {
								DWORD col = ColorConvertFromDS(nitroPalette[i]);
								palette[i] = col;
							}
						} else {
							//create a palette, then encode them to the nclr
							createPalette_(pixels, width, height, palette, paletteSize);
							for (int i = 0; i < paletteSize; i++) {
								DWORD d = palette[i];
								COLOR ds = ColorConvertToDS(d);
								nitroPalette[i] = ds;
								palette[i] = ColorConvertFromDS(ds);
							}
						}


						//now, write out indices. 
						int originOffset = originX + originY * data->ncgr.tilesX;
						//determine how many tiles the bitmap needs
						int tilesX = width >> 3;
						int tilesY = height >> 3;
						//clip the bitmap so it doesn't go over the edges.
						if (tilesX + originX > data->ncgr.tilesX) tilesX = data->ncgr.tilesX - originX;
						if (tilesY + originY > data->ncgr.tilesY) tilesY = data->ncgr.tilesY - originY;

						float diffuse = 1.0f;
						if (!dither) diffuse = 0.0f;
						//write out each tile
						for (int y = 0; y < tilesY; y++) {
							for (int x = 0; x < tilesX; x++) {
								int offset = (y + originY) * data->ncgr.tilesX + x + originX;
								BYTE * tile = data->ncgr.tiles[offset];

								//write out this tile using the palette. Diffuse any error accordingly.
								for (int i = 0; i < 64; i++) {
									int offsetX = i & 0x7;
									int offsetY = i >> 3;
									int poffset = x * 8 + offsetX + (y * 8 + offsetY) * width;
									DWORD pixel = pixels[poffset];
									RGB error;
									int closest = closestpalette(*(RGB *) &pixel, palette + 1, paletteSize - 1, &error) + 1;
									if ((pixel >> 24) < 127) closest = 0;
									int errorRed = (pixel & 0xFF) - (palette[closest] & 0xFF);
									int errorGreen = ((pixel >> 8) & 0xFF) - ((palette[closest] >> 8) & 0xFF);
									int errorBlue = ((pixel >> 16) & 0xFF) - ((palette[closest] >> 16) & 0xFF);
									tile[i] = closest;
									if (dither && (pixel >> 24) >= 127) doDiffuse(poffset, width, height, pixels, errorRed, errorGreen, errorBlue, 0, diffuse);
								}
							}
						}

						free(path);
						free(pixels);
						free(palette);

						InvalidateRect(hWnd, NULL, FALSE);
						if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
						if (nitroPaintStruct->hWndNscrViewer) InvalidateRect(nitroPaintStruct->hWndNscrViewer, NULL, FALSE);
						if (nitroPaintStruct->hWndNcerViewer) InvalidateRect(nitroPaintStruct->hWndNcerViewer, NULL, FALSE);
						break;
					}
					case ID_FILE_SAVE:
					{
						ncgrWrite(&data->ncgr, data->szOpenFile);
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
						HWND hWndNcgrViewer = hWnd;

						NCGR *ncgr = NULL;
						NCLR *nclr = NULL;

						if (hWndNclrViewer) {
							NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
							nclr = &nclrViewerData->nclr;
						}
						ncgr = &data->ncgr;

						DWORD * bits = renderNcgrBits(ncgr, nclr, FALSE, FALSE, &width, &height, -1, -1, data->selectedPalette);

						writeImage(bits, width, height, location);
						free(bits);
						free(location);
						break;
					}
					case ID_NCGRMENU_COPY:
					{
						HANDLE hString = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 134);
						LPSTR clip = (LPSTR) GlobalLock(hString);
						*(DWORD *) clip = 0x30303030;
						clip[4] = 'C';
						BYTE *tile = data->ncgr.tiles[data->contextHoverX + data->contextHoverY * data->ncgr.tilesX];
						for (int i = 0; i < 64; i++) {
							int n = tile[i];
							clip[5 + i * 2] = (n & 0xF) + '0';
							clip[6 + i * 2] = ((n >> 4) & 0xF) + '0';
						}
						GlobalUnlock(hString);
						OpenClipboard(hWnd);
						EmptyClipboard();
						SetClipboardData(CF_TEXT, hString);
						CloseClipboard();
						break;
					}
					case ID_NCGRMENU_PASTE:
					{
						OpenClipboard(hWnd);
						HANDLE hString = GetClipboardData(CF_TEXT);
						CloseClipboard();
						LPSTR clip = GlobalLock(hString);

						if (strlen(clip) == 133 && *(DWORD *) clip == 0x30303030 && clip[4] == 'C') {
							BYTE *tile = data->ncgr.tiles[data->contextHoverX + data->contextHoverY * data->ncgr.tilesX];
							for (int i = 0; i < 64; i++) {
								tile[i] = (clip[5 + i * 2] & 0xF) | ((clip[6 + i * 2] & 0xF) << 4);
							}
							InvalidateRect(hWnd, NULL, FALSE);
						}

						GlobalUnlock(hString);
						break;
					}

				}
			}
			break;
		}
		case WM_PAINT:
		{
			WCHAR buffer[32];
			wsprintf(buffer, L" Character %d", data->hoverIndex);
			SendMessage(data->hWndCharacterLabel, WM_SETTEXT, wcslen(buffer), (LPARAM) buffer);
			InvalidateRect(data->hWndViewer, NULL, FALSE);
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
				InvalidateRect(data->hWndViewer, NULL, FALSE);
				return 0;
			}
			break;
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
		case WM_DESTROY:
		{
			for (int i = 0; i < data->ncgr.nTiles; i++) {
				free(data->ncgr.tiles[i]);
			}
			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			nitroPaintStruct->hWndNcgrViewer = NULL;
			if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
			if (data->hWndTileEditorWindow) DestroyWindow(data->hWndTileEditorWindow);
			free(data->ncgr.tiles);
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			int height = rcClient.bottom - rcClient.top;
			MoveWindow(data->hWndViewer, 0, 0, rcClient.right, height - 42 - 22, FALSE);
			MoveWindow(data->hWndCharacterLabel, 0, height - 42 - 22, 100, 22, TRUE);
			MoveWindow(data->hWndPaletteDropdown, 0, height - 21, 150, 21, TRUE);
			MoveWindow(data->hWndWidthDropdown, 50, height - 42, 100, 21, TRUE);
			MoveWindow(data->hWndWidthLabel, 0, height - 42, 50, 21, FALSE);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

VOID UpdateScrollbarVisibility(HWND hWnd) {
	SCROLLINFO scroll;
	scroll.fMask = SIF_ALL;
	ShowScrollBar(hWnd, SB_BOTH, TRUE);
	GetScrollInfo(hWnd, SB_HORZ, &scroll);
	if (scroll.nMax < scroll.nPage) {
		EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH);
	} else {
		EnableScrollBar(hWnd, SB_HORZ, ESB_ENABLE_BOTH);
	}
	GetScrollInfo(hWnd, SB_VERT, &scroll);
	if (scroll.nMax < scroll.nPage) {
		EnableScrollBar(hWnd, SB_VERT, ESB_DISABLE_BOTH);
	} else {
		EnableScrollBar(hWnd, SB_VERT, ESB_ENABLE_BOTH);
	}
}

LRESULT WINAPI NcgrPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndNcgrViewer = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
	int contentWidth = 0, contentHeight = 0;
	if (data) {
		contentWidth = getDimension(data->ncgr.tilesX, data->showBorders, data->scale);
		contentHeight = getDimension(data->ncgr.tilesY, data->showBorders, data->scale);
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
			HBITMAP hBitmap = CreateCompatibleBitmap(hWindowDC, max(contentWidth, horiz.nPos + rcClient.right), max(contentHeight, vert.nPos + rcClient.bottom));
			SelectObject(hDC, hBitmap);
			IntersectClipRect(hDC, horiz.nPos, vert.nPos, horiz.nPos + rcClient.right, vert.nPos + rcClient.bottom);
			DefMDIChildProc(hWnd, WM_ERASEBKGND, (WPARAM) hDC, 0);
			HPEN defaultPen = SelectObject(hDC, GetStockObject(NULL_PEN));
			HBRUSH defaultBrush = SelectObject(hDC, GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1));
			Rectangle(hDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			SelectObject(hDC, defaultPen);
			SelectObject(hDC, defaultBrush);

			PaintNcgrViewer(hWndNcgrViewer, data, hDC, horiz.nPos, vert.nPos, horiz.nPos + rcClient.right, vert.nPos + rcClient.bottom);

			BitBlt(hWindowDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hDC, horiz.nPos, vert.nPos, SRCCOPY);
			EndPaint(hWnd, &ps);
			DeleteObject(hDC);
			DeleteObject(hBitmap);
			if (data->hWndTileEditorWindow) InvalidateRect(data->hWndTileEditorWindow, NULL, FALSE);

			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWndNcgrViewer, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLong(hWndMain, 0);
			if (nitroPaintStruct->hWndNscrViewer) InvalidateRect(nitroPaintStruct->hWndNscrViewer, NULL, FALSE);

			InvalidateRect(data->hWndWidthLabel, NULL, FALSE);
			break;
		}
		case WM_RBUTTONUP:
		case WM_LBUTTONDOWN:
		{
			//get coordinates.
			int hoverX = data->hoverX;
			int hoverY = data->hoverY;
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
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < contentWidth && mousePos.y < contentHeight) {
				//find the tile coordinates.
				int x = 0, y = 0;
				if (data->showBorders) {
					mousePos.x -= 1;
					mousePos.y -= 1;
					if (mousePos.x < 0) mousePos.x = 0;
					if (mousePos.y < 0) mousePos.y = 0;
					int cellWidth = 8 * data->scale + 1;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				} else {
					int cellWidth = 8 * data->scale;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				}
				if (msg == WM_LBUTTONDOWN) {
					HWND hWndMain = getMainWindow(hWndNcgrViewer);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					NCLR *nclr = NULL;
					if (nitroPaintStruct->hWndNclrViewer) {
						NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(nitroPaintStruct->hWndNclrViewer, 0);
						nclr = &nclrViewerData->nclr;
					}
					if (data->hWndTileEditorWindow) DestroyWindow(data->hWndTileEditorWindow);
					data->hWndTileEditorWindow = CreateTileEditor(CW_USEDEFAULT, CW_USEDEFAULT, 480, 256, nitroPaintStruct->hWndMdi, data->hoverX, data->hoverY);
				} else {
					HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 1);
					POINT mouse;
					GetCursorPos(&mouse);
					TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWndNcgrViewer, NULL);
					data->contextHoverY = hoverY;
					data->contextHoverX = hoverX;
				}
			}
			break;
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
			int oldHovered = data->hoverIndex;
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

			int hoverX = -1, hoverY = -1, hoverIndex = -1;
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < contentWidth && mousePos.y < contentHeight) {
				//find the tile coordinates.
				int x = 0, y = 0;
				if (data->showBorders) {
					mousePos.x -= 1;
					mousePos.y -= 1;
					if (mousePos.x < 0) mousePos.x = 0;
					if (mousePos.y < 0) mousePos.y = 0;
					int cellWidth = 8 * data->scale + 1;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				} else {
					int cellWidth = 8 * data->scale;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				}
				hoverX = x, hoverY = y;
				hoverIndex = hoverX + hoverY * data->ncgr.tilesX;
			}
			if (msg == WM_MOUSELEAVE) {
				hoverX = -1, hoverY = -1, hoverIndex = -1;
			}
			data->hoverX = hoverX;
			data->hoverY = hoverY;
			data->hoverIndex = hoverIndex;
			if (data->hoverIndex != oldHovered) {
				HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWndNcgrViewer, GWL_HWNDPARENT), GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLong(hWndMain, 0);
				if (nitroPaintStruct->hWndNcgrViewer) InvalidateRect(nitroPaintStruct->hWndNcgrViewer, NULL, FALSE);
			}

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
		case WM_ERASEBKGND:
			return 1;
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
		case WM_DESTROY:
		{
			free(frameData);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		}
		
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNcgrPreviewClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme ? CreateSolidBrush(RGB(32, 32, 32)) : (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NcgrPreviewClass";
	wcex.lpfnWndProc = NcgrPreviewWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterNcgrViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NcgrViewerClass";
	wcex.lpfnWndProc = NcgrViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
	RegisterNcgrPreviewClass();
	RegisterTileEditorClass();
}

HWND CreateNcgrViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path) {
	/*HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"NCGR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	return h;*/

	NCGR ncgr;
	int n = ncgrReadFile(&ncgr, path);
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
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"NCGR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
		if (ncgr.header.format == NCGR_TYPE_HUDSON || ncgr.header.format == NCGR_TYPE_HUDSON2) {
			SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
		}
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"NCGR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
	return h;
}