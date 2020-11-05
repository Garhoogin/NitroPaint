#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "resource.h"
#include "tiler.h"
#include "gdip.h"
#include "palette.h"
#include "tileeditor.h"

extern HICON g_appIcon;

int getDimension(int tiles, int border, int scale) {
	int width = tiles * 8 * scale;
	if (border) width += 1 + tiles;
	return width;
}

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

DWORD *NcgrToBitmap(NCGR *ncgr, int usePalette, NCLR *nclr) {
	int width = ncgr->tilesX * 8;
	DWORD *bits = (DWORD *) malloc(ncgr->tilesX * ncgr->tilesY * 64 * 4);

	int tileNumber = 0;
	for (int y = 0; y < ncgr->tilesY; y++) {
		for (int x = 0; x < ncgr->tilesX; x++) {
			BYTE *tile = ncgr->tiles + (64 * tileNumber);
			DWORD block[64];
			ncgrGetTile(ncgr, nclr, x, y, block, usePalette, TRUE);

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


			tile++;
		}
	}
	return bits;
}

VOID PaintNcgrViewer(HWND hWnd, NCGRVIEWERDATA *data, HDC hDC) {
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

	DWORD *px = NcgrToBitmap(&data->ncgr, data->selectedPalette, nclr);
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

			data->hWndPaletteDropdown = CreateWindowEx(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 0, 0, 200, 100, hWnd, NULL, NULL, NULL);
			data->hWndWidthDropdown = CreateWindowEx(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 0, 0, 200, 100, hWnd, NULL, NULL, NULL);
			data->hWndWidthLabel = CreateWindow(L"STATIC", L" Width:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 100, 21, hWnd, NULL, NULL, NULL);

			WCHAR bf[] = L"Palette 00";
			for (int i = 0; i < 16; i++) {
				wsprintfW(bf, L"Palette %02d", i);
				SendMessage(data->hWndPaletteDropdown, CB_ADDSTRING, 0, (LPARAM) bf);
			}
			SendMessage(data->hWndPaletteDropdown, CB_SETCURSEL, 0, 0);

			break;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->ncgr, (NCGR *) lParam, sizeof(NCGR));
			WCHAR titleBuffer[MAX_PATH + 15];
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
			int width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
			int height = rc.bottom - rc.top + 4 + 42; //+42 to account for combobox
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

					data->frameData.contentWidth = getDimension(data->ncgr.tilesX, data->showBorders, data->scale);
					data->frameData.contentHeight = getDimension(data->ncgr.tilesY, data->showBorders, data->scale);

					RECT rcClient;
					GetClientRect(hWnd, &rcClient);

					SCROLLINFO info;
					info.cbSize = sizeof(info);
					info.nMin = 0;
					info.nMax = data->frameData.contentWidth;
					info.nPage = rcClient.right - rcClient.left + 1;
					info.fMask = SIF_RANGE | SIF_PAGE;
					SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

					info.nMax = data->frameData.contentHeight;
					info.nPage = rcClient.bottom - rcClient.top + 1;
					SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
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

						data->frameData.contentWidth = getDimension(data->ncgr.tilesX, data->showBorders, data->scale);
						data->frameData.contentHeight = getDimension(data->ncgr.tilesY, data->showBorders, data->scale);

						SCROLLINFO info;
						info.cbSize = sizeof(info);
						info.nMin = 0;
						info.nMax = data->frameData.contentWidth;
						info.fMask = SIF_RANGE;
						SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

						info.nMax = data->frameData.contentHeight;
						SetScrollInfo(hWnd, SB_VERT, &info, TRUE);

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

						data->frameData.contentWidth = getDimension(data->ncgr.tilesX, data->showBorders, data->scale);
						data->frameData.contentHeight = getDimension(data->ncgr.tilesY, data->showBorders, data->scale);

						RECT rcClient;
						GetClientRect(hWnd, &rcClient);

						SCROLLINFO info;
						info.cbSize = sizeof(info);
						info.nMin = 0;
						info.nMax = data->frameData.contentWidth;
						info.nPage = data->frameData.contentWidth + 4 + 1;
						info.fMask = SIF_RANGE | SIF_PAGE;
						SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

						info.nMax = data->frameData.contentHeight;
						info.nPage = data->frameData.contentHeight + 4 + 42 + 1;
						SetScrollInfo(hWnd, SB_VERT, &info, TRUE);

						RECT rc = { 0 };
						rc.right = data->frameData.contentWidth + 4;
						rc.bottom = data->frameData.contentHeight + 4 + 42;
						AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_HSCROLL | WS_VSCROLL, FALSE);
						SetWindowPos(hWnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE);

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

						InvalidateRect(hWnd, NULL, FALSE);
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
						WORD *nitroPalette = nclr->colors + (paletteNumber << ncgr->nBits);
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
								DWORD col = getColor(nitroPalette[i]);
								int r = (col >> 16) & 0xFF;
								int g = (col >> 8) & 0xFF;
								int b = col & 0xFF;
								palette[i] = r | (g << 8) | (b << 16);
							}
						} else {
							//create a palette, then encode them to the nclr
							createPalette_(pixels, width, height, palette, paletteSize);
							for (int i = 0; i < paletteSize; i++) {
								DWORD d = palette[i];
								int r = d & 0xFF;
								int g = (d >> 8) & 0xFF;
								int b = (d >> 16) & 0xFF;
								r = (r + 4) * 31 / 255;
								g = (g + 4) * 31 / 255;
								b = (b + 4) * 31 / 255;
								nitroPalette[i] = r | (g << 5) | (b << 10);
								palette[i] = (r * 255 / 31) | ((g * 255 / 31) << 8) | ((b * 255 / 31) << 16);
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
						LPWSTR location = saveFileDialog(hWnd, L"Save Bitmap", L"BMP Files (*.bmp)\0*.bmp\0All Files\0*.*\0", L"bmp");
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

				}
			}
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
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < data->frameData.contentWidth && mousePos.y < data->frameData.contentHeight) {
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
					HWND hWndMain = getMainWindow(hWnd);
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
					TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL);
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
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < data->frameData.contentWidth && mousePos.y < data->frameData.contentHeight) {
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
				HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLong(hWndMain, 0);
				if (nitroPaintStruct->hWndNcgrViewer) InvalidateRect(nitroPaintStruct->hWndNcgrViewer, NULL, FALSE);
			}

			InvalidateRect(hWnd, NULL, FALSE);
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
			HBITMAP hBitmap = CreateCompatibleBitmap(hWindowDC, max(data->frameData.contentWidth, horiz.nPos + rcClient.right), max(data->frameData.contentHeight, vert.nPos + rcClient.bottom));
			SelectObject(hDC, hBitmap);
			IntersectClipRect(hDC, horiz.nPos, vert.nPos, horiz.nPos + rcClient.right, vert.nPos + rcClient.bottom);
			DefMDIChildProc(hWnd, WM_ERASEBKGND, (WPARAM) hDC, 0);
			HPEN defaultPen = SelectObject(hDC, GetStockObject(NULL_PEN));
			HBRUSH defaultBrush = SelectObject(hDC, GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1));
			Rectangle(hDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			SelectObject(hDC, defaultPen);
			SelectObject(hDC, defaultBrush);

			PaintNcgrViewer(hWnd, data, hDC);

			BitBlt(hWindowDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hDC, horiz.nPos, vert.nPos, SRCCOPY);
			EndPaint(hWnd, &ps);
			DeleteObject(hDC);
			DeleteObject(hBitmap);
			if (data->hWndTileEditorWindow) InvalidateRect(data->hWndTileEditorWindow, NULL, FALSE);
			
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLong(hWndMain, 0);
			if (nitroPaintStruct->hWndNscrViewer) InvalidateRect(nitroPaintStruct->hWndNscrViewer, NULL, FALSE);

			InvalidateRect(data->hWndWidthLabel, NULL, FALSE);
			break;
		}
		case WM_ERASEBKGND:
			return 1;
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
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			int height = rcClient.bottom - rcClient.top;
			MoveWindow(data->hWndPaletteDropdown, 0, height - 21, 150, 21, FALSE);
			MoveWindow(data->hWndWidthDropdown, 50, height - 42, 100, 21, FALSE);
			MoveWindow(data->hWndWidthLabel, 0, height - 42, 50, 21, FALSE);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNcgrViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NcgrViewerClass";
	wcex.lpfnWndProc = NcgrViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
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
		width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + 42; //+42 to account for the combobox
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"NCGR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
		if (ncgr.isHudson) {
			SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
		}
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"NCGR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
	return h;
}