#include <Windows.h>
#include <CommCtrl.h>

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

DWORD * renderNcgrBits(NCGR * renderNcgr, NCLR * renderNclr, BOOL drawGrid, BOOL drawChecker, int * width, int * height, int markX, int markY, int previewPalette, BOOL transparent) {
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
			ncgrGetTile(renderNcgr, renderNclr, x, y, block, previewPalette, drawChecker, transparent);
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

DWORD *NcgrToBitmap(NCGR *ncgr, int usePalette, NCLR *nclr, int highlightColor, BOOL transparent) {
	int width = ncgr->tilesX * 8;
	DWORD *bits = (DWORD *) malloc(ncgr->tilesX * ncgr->tilesY * 64 * 4);

	int tileNumber = 0;
	for (int y = 0; y < ncgr->tilesY; y++) {
		for (int x = 0; x < ncgr->tilesX; x++) {
			BYTE *tile = ncgr->tiles[tileNumber];
			DWORD block[64];
			ncgrGetTile(ncgr, nclr, x, y, block, usePalette, TRUE, transparent);
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
	HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	if (nitroPaintStruct->hWndNclrViewer) {
		HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
		NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLong(hWndNclrViewer, 0);
		nclr = &nclrViewerData->nclr;
	}

	int highlightColor = data->verifyColor % (1 << data->ncgr.nBits);
	if ((data->verifyFrames & 1) == 0) highlightColor = -1;
	DWORD *px = NcgrToBitmap(&data->ncgr, data->selectedPalette, nclr, highlightColor, data->transparent);
	int outWidth, outHeight;
	HBITMAP hTiles = CreateTileBitmap(px, width, height, data->hoverX, data->hoverY, &outWidth, &outHeight, data->scale, data->showBorders);

	HDC hCompat = CreateCompatibleDC(hDC);
	SelectObject(hCompat, hTiles);
	BitBlt(hDC, 0, 0, outWidth, outHeight, hCompat, 0, 0, SRCCOPY);
	DeleteObject(hCompat);
	DeleteObject(hTiles);

	free(px);
}

void ncgrExportImage(NCGR *ncgr, NCLR *nclr, int paletteIndex, LPCWSTR path) {
	//convert to bitmap layout
	int tilesX = ncgr->tilesX, tilesY = ncgr->tilesY;
	int width = tilesX * 8, height = tilesY * 8;
	unsigned char *bits = (unsigned char *) calloc(width * height, 1);
	for (int tileY = 0; tileY < tilesY; tileY++) {
		for (int tileX = 0; tileX < tilesX; tileX++) {
			BYTE *tile = ncgr->tiles[tileX + tileY * tilesX];
			for (int y = 0; y < 8; y++) {
				memcpy(bits + tileX * 8 + (tileY * 8 + y) * width, tile + y * 8, 8);
			}
		}
	}

	//convert palette
	int depth = ncgr->nBits;
	int paletteSize = 1 << depth;
	int paletteStart = paletteIndex << depth;
	if (paletteStart + paletteSize > nclr->nColors) {
		paletteSize = nclr->nColors - paletteStart;
	}
	if (paletteSize < 0) paletteSize = 0;
	COLOR32 *pal = (COLOR32 *) calloc(paletteSize, sizeof(COLOR32));
	for (int i = 0; i < paletteSize; i++) {
		COLOR32 c = ColorConvertFromDS(nclr->colors[paletteStart + i]);
		if (i) c |= 0xFF000000;
		pal[i] = c;
	}

	imageWriteIndexed(bits, width, height, pal, paletteSize, path);

	free(bits);
	free(pal);
}

typedef struct CHARIMPORTDATA_ {
	WCHAR path[MAX_PATH];
	NCLR *nclr;
	NCGR *ncgr;
	int contextHoverX;
	int contextHoverY;
	int selectedPalette;
	HWND hWndOverwritePalette;
	HWND hWndPaletteBase;
	HWND hWndPaletteSize;
	HWND hWndDither;
	HWND hWndDiffuse;
	HWND hWnd1D;
	HWND hWndCompression;
	HWND hWndMaxChars;
	HWND hWndImport;
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;
} CHARIMPORTDATA;

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
			data->transparent = g_configuration.renderTransparent;
			data->hWnd = hWnd;

			data->hWndViewer = CreateWindow(L"NcgrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 256, 256, hWnd, NULL, NULL, NULL);
			data->hWndCharacterLabel = CreateWindow(L"STATIC", L" Character 0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteDropdown = CreateWindowEx(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 0, 0, 200, 100, hWnd, NULL, NULL, NULL);
			data->hWndWidthDropdown = CreateWindowEx(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 0, 0, 200, 100, hWnd, NULL, NULL, NULL);
			data->hWndWidthLabel = CreateWindow(L"STATIC", L" Width:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 0, 0, 100, 21, hWnd, NULL, NULL, NULL);
			data->hWndExpand = CreateWindow(L"BUTTON", L"Extend", WS_CHILD | WS_VISIBLE, 0, 0, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWnd8bpp = CreateWindow(L"BUTTON", L"8bpp", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 100, 22, hWnd, NULL, NULL, NULL);

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
		case NV_SETTITLE:
		{
			LPWSTR path = (LPWSTR) lParam;
			WCHAR titleBuffer[MAX_PATH + 20];
			if (!g_configuration.fullPaths) path = GetFileName(path);
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - Character Editor", 40);
			SetWindowText(hWnd, titleBuffer);
			break;
		}
		case NV_INITIALIZE:
		case NV_INITIALIZE_IMMEDIATE:
		{
			if (msg == NV_INITIALIZE) {
				LPWSTR path = (LPWSTR) wParam;
				memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
				memcpy(&data->ncgr, (NCGR *) lParam, sizeof(NCGR));
				SendMessage(hWnd, NV_SETTITLE, 0, (LPARAM) path);
			} else {
				NCGR *ncgr = (NCGR *) lParam;
				memcpy(&data->ncgr, ncgr, sizeof(NCGR));
			}
			data->frameData.contentWidth = getDimension(data->ncgr.tilesX, data->showBorders, data->scale);
			data->frameData.contentHeight = getDimension(data->ncgr.tilesY, data->showBorders, data->scale);

			int width = data->frameData.contentWidth + GetSystemMetrics(SM_CXVSCROLL) + 4;
			int height = data->frameData.contentHeight + 4 + 42 + 22 + GetSystemMetrics(SM_CYHSCROLL); //+42 to account for combobox;
			if (width < 255 + 4) width = 255 + 4; //min width for controls
			SetWindowSize(hWnd, width, height);


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
			if (data->ncgr.nBits == 8) SendMessage(data->hWnd8bpp, BM_SETCHECK, 1, 0);

			//guess a tile base for open NSCR (if any)
			HWND hWndMain = getMainWindow(hWnd);
			int nNscrEditors = GetAllEditors(hWndMain, FILE_TYPE_SCREEN, NULL, 0);
			if (nNscrEditors > 0) {
				//for each editor
				HWND *nscrEditors = (HWND *) calloc(nNscrEditors, sizeof(HWND));
				GetAllEditors(hWndMain, FILE_TYPE_SCREEN, nscrEditors, nNscrEditors);
				for (int i = 0; i < nNscrEditors; i++) {
					HWND hWndNscrViewer = nscrEditors[i];
					NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0);
					NSCR *nscr = &nscrViewerData->nscr;
					if (nscr->nHighestIndex >= data->ncgr.nTiles) {
						NscrViewerSetTileBase(hWndNscrViewer, nscr->nHighestIndex + 1 - data->ncgr.nTiles);
					} else {
						NscrViewerSetTileBase(hWndNscrViewer, 0);
					}
				}
				free(nscrEditors);
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
					ncgrChangeWidth(&data->ncgr, width);

					RECT rcClient;
					GetClientRect(hWnd, &rcClient);

					SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndExpand) {
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					HWND h = CreateWindow(L"ExpandNcgrClass", L"Expand NCGR", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
										  CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
					ShowWindow(h, SW_SHOW);
					SetActiveWindow(h);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
					SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
				} else if (notification == BN_CLICKED && hWndControl == data->hWnd8bpp) {
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
					if (state) {
						//convert 4bpp graphic to 8bpp

						BYTE **tiles2 = (BYTE **) calloc(data->ncgr.nTiles / 2, sizeof(BYTE **));
						for (int i = 0; i < data->ncgr.nTiles / 2; i++) {
							BYTE *tile1 = data->ncgr.tiles[i * 2];
							BYTE *tile2 = data->ncgr.tiles[i * 2 + 1];
							BYTE *dest = (BYTE *) calloc(64, 1);
							tiles2[i] = dest;

							for (int j = 0; j < 32; j++) {
								dest[j] = tile1[j * 2] | (tile1[j * 2 + 1] << 4);
							}
							for (int j = 0; j < 32; j++) {
								dest[j + 32] = tile2[j * 2] | (tile2[j * 2 + 1] << 4);
							}
						}
						for (int i = 0; i < data->ncgr.nTiles; i++) {
							free(data->ncgr.tiles[i]);
						}
						free(data->ncgr.tiles);
						data->ncgr.tiles = tiles2;
						
						if ((data->ncgr.tilesX & 1) == 0) {
							data->ncgr.tilesX /= 2;
							data->ncgr.nTiles /= 2;
						} else {
							data->ncgr.nTiles /= 2;
							data->ncgr.tilesX = calculateWidth(data->ncgr.nTiles);
							data->ncgr.tilesY = data->ncgr.nTiles / data->ncgr.tilesX;
						}
						data->ncgr.nBits = 8;
					} else {
						//covert 8bpp graphic to 4bpp

						BYTE **tiles2 = (BYTE **) calloc(data->ncgr.nTiles * 2, sizeof(BYTE **));
						for (int i = 0; i < data->ncgr.nTiles; i++) {
							BYTE *tile1 = calloc(64, 1);
							BYTE *tile2 = calloc(64, 1);
							BYTE *src = data->ncgr.tiles[i];
							tiles2[i * 2] = tile1;
							tiles2[i * 2 + 1] = tile2;

							for (int j = 0; j < 32; j++) {
								tile1[j * 2] = src[j] & 0xF;
								tile1[j * 2 + 1] = src[j] >> 4;
							}
							for (int j = 0; j < 32; j++) {
								tile2[j * 2] = src[j + 32] & 0xF;
								tile2[j * 2 + 1] = src[j + 32] >> 4;
							}
						}
						for (int i = 0; i < data->ncgr.nTiles; i++) {
							free(data->ncgr.tiles[i]);
						}
						free(data->ncgr.tiles);
						data->ncgr.tiles = tiles2;
						data->ncgr.tilesX *= 2;
						data->ncgr.nTiles *= 2;
						data->ncgr.nBits = 4;
					}

					SendMessage(data->hWndWidthDropdown, CB_RESETCONTENT, 0, 0);
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

					InvalidateRect(hWnd, NULL, FALSE);
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					if (nitroPaintStruct->hWndNclrViewer) {
						InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
					}
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

						BOOL createPalette = (LOWORD(wParam) == ID_NCGRMENU_IMPORTBITMAPHEREANDREPLACEPALETTE);
						HWND hWndMain = getMainWindow(hWnd);
						HWND h = CreateWindow(L"CharImportDialog", L"Import Bitmap", 
							(WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX) | WS_VISIBLE,
											  CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
						CHARIMPORTDATA *cidata = (CHARIMPORTDATA *) GetWindowLongPtr(h, 0);
						memcpy(cidata->path, path, 2 * (wcslen(path) + 1));
						if(createPalette) SendMessage(cidata->hWndOverwritePalette, BM_SETCHECK, BST_CHECKED, 0);

						//populate inputs with sensible defaults
						WCHAR bf[16];
						if (data->ncgr.nBits == 4) SendMessage(cidata->hWndPaletteSize, WM_SETTEXT, 2, (LPARAM) L"16");
						wsprintfW(bf, L"%d", data->ncgr.nTiles - (data->contextHoverX + data->contextHoverY * data->ncgr.tilesX));
						SendMessage(cidata->hWndMaxChars, WM_SETTEXT, wcslen(bf), (LPARAM) bf);

						NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
						HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
						NCLR *nclr = NULL;
						NCGR *ncgr = &data->ncgr;
						if (hWndNclrViewer) {
							NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(hWndNclrViewer, 0);
							nclr = &nclrViewerData->nclr;
						}
						cidata->nclr = nclr;
						cidata->ncgr = ncgr;
						cidata->selectedPalette = data->selectedPalette;
						cidata->contextHoverX = data->contextHoverX;
						cidata->contextHoverY = data->contextHoverY;
						free(path);
						break;
					}
					case ID_FILE_SAVEAS:
					case ID_FILE_SAVE:
					{
						if (data->szOpenFile[0] == L'\0' || LOWORD(wParam) == ID_FILE_SAVEAS) {
							LPCWSTR filter = L"NCGR Files (*.ncgr)\0*.ncgr\0All Files\0*.*\0";
							switch (data->ncgr.header.format) {
								case NCGR_TYPE_BIN:
								case NCGR_TYPE_HUDSON:
								case NCGR_TYPE_HUDSON2:
									filter = L"Character Files (*.bin, *ncg.bin, *icg.bin, *.nbfc)\0*.bin;*.nbfc\0All Files\0*.*\0";
									break;
								case NCGR_TYPE_COMBO:
									filter = L"Combination Files (*.dat, *.bnr, *.bin)\0*.dat;*.bnr;*.bin\0";
									break;
								case NCGR_TYPE_NC:
									filter = L"NCG Files (*.ncg)\0*.ncg\0All Files\0*.*\0";
									break;
							}
							LPWSTR path = saveFileDialog(getMainWindow(hWnd), L"Save As...", filter, L"ncgr");
							if (path != NULL) {
								memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
								SendMessage(hWnd, NV_SETTITLE, 0, (LPARAM) path);
								free(path);
							} else break;
						}
						ncgrWriteFile(&data->ncgr, data->szOpenFile);
						break;
					}
					case ID_FILE_EXPORT:
					{
						LPWSTR location = saveFileDialog(hWnd, L"Save Bitmap", L"PNG Files (*.png)\0*.png\0All Files\0*.*\0", L"png");
						if (!location) break;

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

						ncgrExportImage(ncgr, nclr, data->selectedPalette, location);
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
			fileFree((OBJECT_HEADER *) &data->ncgr);
			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			nitroPaintStruct->hWndNcgrViewer = NULL;
			if (nitroPaintStruct->hWndNclrViewer) InvalidateRect(nitroPaintStruct->hWndNclrViewer, NULL, FALSE);
			if (data->hWndTileEditorWindow) DestroyWindow(data->hWndTileEditorWindow);
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
			MoveWindow(data->hWndExpand, 155, height - 42, 100, 22, TRUE);
			MoveWindow(data->hWnd8bpp, 155, height - 21, 100, 21, TRUE);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
		case NV_GETTYPE:
			return FILE_TYPE_CHAR;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
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

			HWND hWndMain = getMainWindow(hWndNcgrViewer);
			InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);

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
					if (nclr != NULL) {
						if (data->hWndTileEditorWindow) DestroyWindow(data->hWndTileEditorWindow);
						data->hWndTileEditorWindow = CreateTileEditor(CW_USEDEFAULT, CW_USEDEFAULT, 480, 256, nitroPaintStruct->hWndMdi, data->hoverX, data->hoverY);
					}
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

LRESULT CALLBACK NcgrExpandProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCGRVIEWERDATA *data = (NCGRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case NV_INITIALIZE:
		{
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) lParam);
			data = (NCGRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
			WCHAR buffer[16];
			wsprintfW(buffer, L"%d", data->ncgr.nTiles / data->ncgr.tilesX);

			CreateWindow(L"STATIC", L"Rows:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndExpandRowsInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", buffer, WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 90, 10, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndExpandButton = CreateWindow(L"BUTTON", L"Set", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 90, 37, 75, 22, hWnd, NULL, NULL, NULL);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
			SetWindowSize(hWnd, 175, 69);
			SetFocus(data->hWndExpandRowsInput);
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(data->hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
		case WM_COMMAND:
		{
			int notification = LOWORD(wParam);
			HWND hWndControl = (HWND) lParam;
			if (notification == BN_CLICKED && hWndControl == data->hWndExpandButton) {
				WCHAR buffer[16];
				SendMessage(data->hWndExpandRowsInput, WM_GETTEXT, 16, (LPARAM) buffer);
				int tilesX = data->ncgr.tilesX;
				int nRows = _wtol(buffer);
				int nOldRows = data->ncgr.nTiles / tilesX;
				if (nRows > nOldRows) {
					BYTE **chars = data->ncgr.tiles;
					data->ncgr.tiles = realloc(chars, nRows * tilesX * sizeof(BYTE *));
					for (int i = 0; i < nRows * tilesX - nOldRows * tilesX; i++) {
						data->ncgr.tiles[i + nOldRows * tilesX] = (BYTE *) calloc(64, 1);
					}
				} else if (nRows < nOldRows) {
					for (int i = 0; i < nOldRows * tilesX - nRows * tilesX; i++) {
						free(data->ncgr.tiles[i + nRows * tilesX]);
					}
					data->ncgr.tiles = realloc(data->ncgr.tiles, nRows * tilesX * sizeof(BYTE *));
				}
				data->ncgr.nTiles = nRows * tilesX;
				data->ncgr.tilesY = nRows;
				SendMessage(data->hWndViewer, NV_RECALCULATE, 0, 0);
				InvalidateRect(data->hWnd, NULL, FALSE);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

typedef struct {
	BOOL createPalette;
	BOOL dither;
	BOOL import1D;
	BOOL charCompression;
	float diffuse;
	int paletteBase;
	int paletteSize;
	int nMaxChars;
	int originX;
	int originY;
	int paletteNumber;
	int balance;
	int colorBalance;
	int enhanceColors;
	NCLR *nclr;
	NCGR *ncgr;
	HWND hWndMain;
	WCHAR imgPath[MAX_PATH];
} CHARIMPORT;

int charImportCallback(void *data) {
	CHARIMPORT *cim = (CHARIMPORT *) data;
	HWND hWndMain = cim->hWndMain;
	InvalidateAllEditors(hWndMain, FILE_TYPE_PALETTE);
	InvalidateAllEditors(hWndMain, FILE_TYPE_CHAR);
	InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);
	InvalidateAllEditors(hWndMain, FILE_TYPE_CELL);
	free(data);

	SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
	SetForegroundWindow(hWndMain);
	return 0;
}

void charImport(NCLR *nclr, NCGR *ncgr, LPCWSTR imgPath, BOOL createPalette, int paletteNumber, int paletteSize, int paletteBase, 
	BOOL dither, float diffuse, BOOL import1D, BOOL charCompression, int nMaxChars, int originX, int originY, 
	int balance, int colorBalance, int enhanceColors, int *progress) {
	int maxPaletteSize = 1 << ncgr->nBits;

	//if we start at base 0, increment by 1. We'll put a placeholder color in slot 0.
	if (paletteBase == 0) {
		paletteBase = 1;
		paletteSize--;
		nclr->colors[0] = ColorConvertToDS(0xFF00FF);
	}

	int firstColorIndex = (paletteNumber << ncgr->nBits) + paletteBase;
	if(paletteSize > maxPaletteSize) paletteSize = maxPaletteSize;
	if (firstColorIndex + paletteSize >= nclr->nColors) {
		paletteSize = nclr->nColors - firstColorIndex;
	}

	COLOR *nitroPalette = nclr->colors + firstColorIndex;
	COLOR32 *palette = (COLOR32 *) calloc(paletteSize, 4);

	int width, height;
	COLOR32 *pixels = gdipReadImage(imgPath, &width, &height);

	//if we use an existing palette, decode the palette values.
	//if we do not use an existing palette, generate one.
	if (!createPalette) {
		//decode the palette
		for (int i = 0; i < paletteSize; i++) {
			COLOR32 col = ColorConvertFromDS(nitroPalette[i]);
			palette[i] = col;
		}
	} else {
		//create a palette, then encode them to the nclr
		createPaletteSlowEx(pixels, width, height, palette, paletteSize, balance, colorBalance, enhanceColors, 0);
		for (int i = 0; i < paletteSize; i++) {
			COLOR32 d = palette[i];
			COLOR ds = ColorConvertToDS(d);
			nitroPalette[i] = ds;
			palette[i] = ColorConvertFromDS(ds);
		}
	}

	//index image with given parameters.
	if (!dither) diffuse = 0.0f;
	ditherImagePaletteEx(pixels, NULL, width, height, palette, paletteSize, 0, 1, 0, diffuse, balance, colorBalance, enhanceColors);

	//now, write out indices. 
	int originOffset = originX + originY * ncgr->tilesX;
	//determine how many tiles the bitmap needs
	int tilesX = width >> 3;
	int tilesY = height >> 3;

	//perform the write. 1D or 2D?
	if (!import1D) {
		//clip the bitmap so it doesn't go over the edges.
		if (tilesX + originX > ncgr->tilesX) tilesX = ncgr->tilesX - originX;
		if (tilesY + originY > ncgr->tilesY) tilesY = ncgr->tilesY - originY;

		//write out each tile
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				int offset = (y + originY) * ncgr->tilesX + x + originX;
				BYTE *tile = ncgr->tiles[offset];

				//write out this tile using the palette. Diffuse any error accordingly.
				for (int i = 0; i < 64; i++) {
					int offsetX = i & 0x7;
					int offsetY = i >> 3;
					int poffset = x * 8 + offsetX + (y * 8 + offsetY) * width;
					COLOR32 pixel = pixels[poffset];

					int closest = closestPalette(pixel, palette, paletteSize) + paletteBase;
					if ((pixel >> 24) < 127) closest = 0;
					tile[i] = closest;
				}
			}
		}
	} else {
		//1D import, start at index and continue linearly.
		COLOR32 *tiles = (COLOR32 *) calloc(tilesX * tilesY, 64 * sizeof(COLOR32));
		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				int imgX = x * 8, imgY = y * 8;
				int tileIndex = x + y * tilesX;
				int srcIndex = imgX + imgY * width;
				COLOR32 *src = pixels + srcIndex;
				for (int i = 0; i < 8; i++) {
					memcpy(tiles + 64 * tileIndex + 8 * i, src + i * width, 32);
				}
			}
		}

		//character compression
		int nChars = tilesX * tilesY;
		if (charCompression) {
			//create dummy whole palette that the character compression functions expect
			COLOR32 dummyFull[256] = { 0 };
			memcpy(dummyFull + paletteBase, palette, paletteSize * 4);

			BGTILE *bgTiles = (BGTILE *) calloc(nChars, sizeof(BGTILE));

			//split image into 8x8 tiles.
			for (int y = 0; y < tilesY; y++) {
				for (int x = 0; x < tilesX; x++) {
					int srcOffset = x * 8 + y * 8 * (width);
					COLOR32 *block = bgTiles[x + y * tilesX].px;

					int index = x + y * tilesX;
					memcpy(block, tiles + index * 64, 64 * 4);
				}
			}
			int nTiles = nChars;
			setupBgTilesEx(bgTiles, nChars, ncgr->nBits, dummyFull, paletteSize, 1, 0, paletteBase, 0, 0.0f, balance, colorBalance, enhanceColors);
			nChars = performCharacterCompression(bgTiles, nChars, ncgr->nBits, nMaxChars, dummyFull, paletteSize, 1, 0, paletteBase, 
				balance, colorBalance, progress);

			//read back result
			int outIndex = 0;
			for (int i = 0; i < nTiles; i++) {
				if (bgTiles[i].masterTile != i) continue;
				BGTILE *t = bgTiles + i;

				COLOR32 *dest = tiles + outIndex * 64;
				for (int j = 0; j < 64; j++) {
					int index = t->indices[j];
					if (index) dest[j] = dummyFull[index] | 0xFF000000;
					else dest[j] = 0;
				}
				outIndex++;
			}
			free(bgTiles);
		}

		//break into tiles and write
		int destBaseIndex = originX + originY * ncgr->tilesX;
		int nWriteChars = min(nChars, ncgr->nTiles - destBaseIndex);
		for (int i = 0; i < nWriteChars; i++) {
			BYTE *tile = ncgr->tiles[i + destBaseIndex];
			COLOR32 *srcTile = tiles + i * 64;

			for (int j = 0; j < 64; j++) {
				COLOR32 pixel = srcTile[j];

				int closest = closestPalette(pixel, palette, paletteSize) + paletteBase;
				if ((pixel >> 24) < 127) closest = 0;
				tile[j] = closest;
			}
		}
		free(tiles);
	}

	free(pixels);
	free(palette);
}

DWORD WINAPI charImportInternal(LPVOID lpParameter) {
	PROGRESSDATA *progress = (PROGRESSDATA *) lpParameter;
	CHARIMPORT *cim = (CHARIMPORT *) progress->data;
	progress->progress1Max = 100;
	progress->progress1 = 100;
	progress->progress2Max = 1000;
	charImport(cim->nclr, cim->ncgr, cim->imgPath, cim->createPalette, cim->paletteNumber, cim->paletteSize, cim->paletteBase, 
			   cim->dither, cim->diffuse, cim->import1D, cim->charCompression, cim->nMaxChars, cim->originX, cim->originY, 
			   cim->balance, cim->colorBalance, cim->enhanceColors, &progress->progress2);
	progress->waitOn = 1;
	return 0;
}

void threadedCharImport(PROGRESSDATA *progress) {
	CreateThread(NULL, 0, charImportInternal, progress, 0, NULL);
}

LRESULT CALLBACK CharImportProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CHARIMPORTDATA *data = (CHARIMPORTDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (CHARIMPORTDATA *) calloc(1, sizeof(CHARIMPORTDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			int boxWidth = 100 + 100 + 10 + 10 + 10; //box width
			int boxHeight = 3 * 27 - 5 + 10 + 10 + 10; //first row height
			int boxHeight2 = 3 * 27 - 5 + 10 + 10 + 10; //second row height
			int boxHeight3 = 3 * 27 - 5 + 10 + 10 + 10; //third row height
			int width = 30 + 2 * boxWidth; //window width
			int height = 10 + boxHeight + 10 + boxHeight2 + 10 + boxHeight3 + 10 + 22 + 10; //window height

			int leftX = 10 + 10; //left box X
			int rightX = 10 + boxWidth + 10 + 10; //right box X
			int topY = 10 + 10 + 8; //top box Y
			int middleY = 10 + boxHeight + 10 + 10 + 8; //middle box Y
			int bottomY = 10 + boxHeight + 10 + boxHeight2 + 10 + 10 + 8; //bottom box Y

			data->hWndOverwritePalette = CreateWindow(L"BUTTON", L"Write Palette", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, topY, 150, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette Base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY + 27, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteBase = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 85, topY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette Size:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY + 27 * 2, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteSize = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"256", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 85, topY + 27 * 2, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndDither = CreateWindow(L"BUTTON", L"Dither", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, rightX, topY, 150, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Diffuse:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, topY + 27, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndDiffuse = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, rightX + 85, topY + 27, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWnd1D = CreateWindow(L"BUTTON", L"1D Import", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, middleY, 150, 22, hWnd, NULL, NULL, NULL);
			data->hWndCompression = CreateWindow(L"BUTTON", L"Compress Character", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, middleY + 27, 150, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Max chars:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27 * 2, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndMaxChars = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1024", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 85, middleY + 27 * 2, 100, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndEnhanceColors = CreateWindow(L"BUTTON", L"Enhance Colors", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, bottomY + 27 * 2, 200, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Lightness", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Green", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Red", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndColorBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY + 27, 200, 22, hWnd, NULL, NULL, NULL);

			data->hWndImport = CreateWindow(L"BUTTON", L"Import", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, width / 2 - 100, height - 32, 200, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"BUTTON", L"Palette", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10, 10, boxWidth, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Graphics", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10 + boxWidth + 10, 10, boxWidth, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Dimension", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10, 10 + boxHeight + 10, boxWidth * 2 + 10, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Color", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10, 10 + boxHeight + 10 + boxHeight2 + 10, 10 + 2 * boxWidth, boxHeight3, hWnd, NULL, NULL, NULL);

			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) (HFONT) GetStockObject(DEFAULT_GUI_FONT));
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
			SetWindowLong(data->hWndDiffuse, GWL_STYLE, GetWindowLong(data->hWndDiffuse, GWL_STYLE) | WS_DISABLED);
			SetWindowLong(data->hWndCompression, GWL_STYLE, GetWindowLong(data->hWndCompression, GWL_STYLE) | WS_DISABLED);
			SetWindowLong(data->hWndMaxChars, GWL_STYLE, GetWindowLong(data->hWndMaxChars, GWL_STYLE) | WS_DISABLED);
			SendMessage(data->hWndBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);
			SendMessage(data->hWndColorBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndColorBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);
			SetWindowSize(hWnd, width, height);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				if (hWndControl == data->hWndDither) {
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
					if (state) SetWindowLong(data->hWndDiffuse, GWL_STYLE, GetWindowLong(data->hWndDiffuse, GWL_STYLE) & ~WS_DISABLED);
					else SetWindowLong(data->hWndDiffuse, GWL_STYLE, GetWindowLong(data->hWndDiffuse, GWL_STYLE) | WS_DISABLED);
					InvalidateRect(hWnd, NULL, TRUE);
				} else if (hWndControl == data->hWnd1D) {
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int ccState = SendMessage(data->hWndCompression, BM_GETCHECK, 0, 0) == BST_CHECKED;
					if (state) {
						SetWindowLong(data->hWndCompression, GWL_STYLE, GetWindowLong(data->hWndCompression, GWL_STYLE) & ~WS_DISABLED);
						if (ccState) SetWindowLong(data->hWndMaxChars, GWL_STYLE, GetWindowLong(data->hWndMaxChars, GWL_STYLE) & ~WS_DISABLED);
					} else {
						SetWindowLong(data->hWndCompression, GWL_STYLE, GetWindowLong(data->hWndCompression, GWL_STYLE) | WS_DISABLED);
						SetWindowLong(data->hWndMaxChars, GWL_STYLE, GetWindowLong(data->hWndMaxChars, GWL_STYLE) | WS_DISABLED);
					}
					InvalidateRect(hWnd, NULL, TRUE);
				} else if(hWndControl == data->hWndCompression){
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
					if (state) SetWindowLong(data->hWndMaxChars, GWL_STYLE, GetWindowLong(data->hWndMaxChars, GWL_STYLE) & ~WS_DISABLED);
					else SetWindowLong(data->hWndMaxChars, GWL_STYLE, GetWindowLong(data->hWndMaxChars, GWL_STYLE) | WS_DISABLED);
					InvalidateRect(hWnd, NULL, TRUE);
				} else if (hWndControl == data->hWndImport) {
					BOOL createPalette = SendMessage(data->hWndOverwritePalette, BM_GETCHECK, 0, 0) == BST_CHECKED;
					BOOL dither = SendMessage(data->hWndDither, BM_GETCHECK, 0, 0) == BST_CHECKED;
					BOOL import1D = SendMessage(data->hWnd1D, BM_GETCHECK, 0, 0) == BST_CHECKED;
					BOOL charCompression = SendMessage(data->hWndCompression, BM_GETCHECK, 0, 0) == BST_CHECKED;
					WCHAR inBuffer[16];
					SendMessage(data->hWndDiffuse, WM_GETTEXT, 16, (LPARAM) inBuffer);
					float diffuse = ((float) _wtol(inBuffer)) / 100.0f;
					if (!dither) diffuse = 0.0f;
					SendMessage(data->hWndPaletteBase, WM_GETTEXT, 16, (LPARAM) inBuffer);
					int paletteBase = _wtol(inBuffer);
					SendMessage(data->hWndPaletteSize, WM_GETTEXT, 16, (LPARAM) inBuffer);
					int paletteSize = _wtol(inBuffer);
					SendMessage(data->hWndMaxChars, WM_GETTEXT, 16, (LPARAM) inBuffer);
					int nMaxChars = _wtol(inBuffer);
					int balance = SendMessage(data->hWndBalance, TBM_GETPOS, 0, 0);
					int colorBalance = SendMessage(data->hWndColorBalance, TBM_GETPOS, 0, 0);
					BOOL enhanceColors = SendMessage(data->hWndEnhanceColors, BM_GETCHECK, 0, 0) == BST_CHECKED;

					NCLR *nclr = data->nclr;
					NCGR *ncgr = data->ncgr;

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					CHARIMPORT *cimport = (CHARIMPORT *) calloc(1, sizeof(CHARIMPORT));
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					cimport->createPalette = createPalette;
					cimport->dither = dither;
					cimport->import1D = import1D;
					cimport->charCompression = charCompression;
					cimport->diffuse = diffuse;
					cimport->paletteBase = paletteBase;
					cimport->paletteSize = paletteSize;
					cimport->nMaxChars = nMaxChars;
					cimport->nclr = nclr;
					cimport->ncgr = ncgr;
					cimport->originX = data->contextHoverX;
					cimport->originY = data->contextHoverY;
					cimport->paletteNumber = data->selectedPalette;
					cimport->balance = balance;
					cimport->colorBalance = colorBalance;
					cimport->enhanceColors = enhanceColors;
					cimport->hWndMain = hWndMain;
					memcpy(cimport->imgPath, data->path, 2 * (wcslen(data->path) + 1));
					progressData->data = cimport;
					progressData->callback = charImportCallback;

					HWND hWndProgress = CreateWindow(L"ProgressWindowClass", L"In Progress...", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 100, hWndMain, NULL, NULL, NULL);
					ShowWindow(hWndProgress, SW_SHOW);
					SendMessage(hWndProgress, NV_SETDATA, 0, (LPARAM) progressData);
					threadedCharImport(progressData);

					DestroyWindow(hWnd);
				}
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetForegroundWindow(hWndMain);
			break;
		}
		case WM_DESTROY:
		{
			free(data);
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

VOID RegisterNcgrExpandClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"ExpandNcgrClass";
	wcex.lpfnWndProc = NcgrExpandProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterCharImportClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"CharImportDialog";
	wcex.lpfnWndProc = CharImportProc;
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
	RegisterNcgrExpandClass();
	RegisterCharImportClass();
}

HWND CreateNcgrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
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
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"Character Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
		if (ncgr.header.format == NCGR_TYPE_HUDSON || ncgr.header.format == NCGR_TYPE_HUDSON2) {
			SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
		}
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"Character Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &ncgr);
	return h;
}

HWND CreateNcgrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCGR *ncgr) {
	width = ncgr->tilesX * 8;
	height = ncgr->tilesY * 8;

	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		if (width < 150) width = 150;
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL); //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + 42 + 22 + GetSystemMetrics(SM_CYHSCROLL); //+42 to account for the combobox
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"Character Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) ncgr);
		if (ncgr->header.format == NCGR_TYPE_HUDSON || ncgr->header.format == NCGR_TYPE_HUDSON2) {
			SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
		}
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NcgrViewerClass", L"Character Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) ncgr);
	return h;
}
