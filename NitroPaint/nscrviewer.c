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

static void ScrViewerRender(HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight);


static COLOR32 *ScrViewerRenderBits(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int *width, int *height, BOOL transparent) {
	int bWidth = nscr->nWidth;
	int bHeight = nscr->nHeight;
	*width = bWidth;
	*height = bHeight;

	LPDWORD bits = (LPDWORD) calloc(bWidth * bHeight, 4);

	int tilesX = nscr->nWidth >> 3;
	int tilesY = nscr->nHeight >> 3;

	COLOR32 block[64];

	for (int y = 0; y < tilesY; y++) {
		int offsetY = y << 3;
		for (int x = 0; x < tilesX; x++) {
			int offsetX = x << 3;

			int tileNo = -1;
			nscrGetTileEx(nscr, ncgr, nclr, tileBase, x, y, block, &tileNo, transparent);
			COLOR32 dwDest = x * 8 + y * 8 * bWidth;

			for (int i = 0; i < 8; i++) {
				memcpy(bits + dwDest + i * bWidth, block + (i << 3), 32);
			}
		}
	}

	return bits;
}

unsigned char *ScrViewerRenderIndexed(NSCR *nscr, NCGR *ncgr, int tileBase, int *width, int *height, BOOL transparent) {
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

static void ScrViewerRender(HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight) {
	//get data pointer
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWnd);
	NSCR *nscr = &data->nscr;
	NCGR *ncgr = NULL;
	NCLR *nclr = NULL;

	int chrHover = -1;

	//get NCLR and NCGR pointers
	HWND hWndMain = getMainWindow(hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	if (nitroPaintStruct->hWndNclrViewer != NULL) {
		nclr = (NCLR *) EditorGetObject(nitroPaintStruct->hWndNclrViewer);
	}
	if (nitroPaintStruct->hWndNcgrViewer != NULL) {
		NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(nitroPaintStruct->hWndNcgrViewer);
		chrHover = ncgrViewerData->ted.hoverIndex;

		ncgr = (NCGR *) EditorGetObject(nitroPaintStruct->hWndNcgrViewer);
	}

	//get verify color params
	int hlStart = data->hlStart;
	int hlEnd = data->hlEnd;
	int hlMode = data->hlMode;
	if ((data->verifyFrames & 1) == 0) hlStart = hlEnd = -1;

	//get character hover params
	if (chrHover != -1) {
		chrHover += data->tileBase;
	}

	int tilesX = nscr->nWidth / 8;
	int tilesY = nscr->nHeight / 8;


	for (int y = 0; y < renderHeight; y++) {
		for (int x = 0; x < renderWidth; x++) {
			int srcX = (x + scrollX) / data->scale;
			int srcY = (y + scrollY) / data->scale;
			int srcTileX = srcX / 8;
			int srcTileY = srcY / 8;

			//get BG tile properties
			uint16_t scrdat = nscr->data[srcTileX + srcTileY * tilesX];
			int chrno = (scrdat >>  0) & 0x03FF;
			int flip  = (scrdat >> 10) & 0x0003;
			int palno = (scrdat >> 12) & 0x000F;

			//get graphics tile
			unsigned char *chr = NULL;
			chrno -= data->tileBase;
			if (ncgr != NULL && chrno >= 0 && chrno < ncgr->nTiles) {
				chr = ncgr->tiles[chrno];
			}

			//get source pixel in tile
			int inX = srcX % 8;
			int inY = srcY % 8;
			if (flip & TILE_FLIPX) inX ^= 7;
			if (flip & TILE_FLIPY) inY ^= 7;

			//sample color
			COLOR32 c = 0;
			int cidx = -1;
			if (chr != NULL) {
				cidx = chr[inX + inY * 8] + (palno << ncgr->nBits);
				if (nclr != NULL && cidx < nclr->nColors) {
					//sample color from palette
					c = ColorConvertFromDS(nclr->colors[cidx]);
				} else {
					//black
					c = 0;
				}

				//color 0 within palette?
				if ((cidx & ((1 << ncgr->nBits) - 1)) == 0) {
					//alpha = 0
				} else {
					//alpha = 255
					c |= 0xFF000000;
				}
			}

			//handle alpha
			if (data->transparent && (c >> 24) == 0) {
				//render transparent
				COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };
				c = checker[((x ^ y) >> 2) & 1];
			}

			//handle verify color
			if (hlStart != -1 && hlEnd != -1) {
				if (PalViewerIndexInRange(cidx, hlStart, hlEnd, hlMode == PALVIEWER_SELMODE_2D)) {
					int lightness = (c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF);
					if (lightness < 383) c = 0xFFFFFFFF;
					else c = 0xFF000000;
				}
			}

			//handle hover indication in character editor
			if (chrHover != -1 && chrHover == chrno) {
				unsigned int r = (c >> 0) & 0xFF;
				unsigned int g = (c >> 8) & 0xFF;
				unsigned int b = (c >> 16) & 0xFF;

				r = (r + 0x00 + 1) / 2;
				g = (g + 0xFF + 1) / 2;
				b = (b + 0xFF + 1) / 2;
				c = r | (g << 8) | (b << 16) | 0xFF000000;
			}

			//put pixel
			fb->px[x + y * fb->width] = REVERSE(c);
		}
	}
}

static void ScrViewerCopy(NSCRVIEWERDATA *data) {
	OpenClipboard(data->hWnd);
	EmptyClipboard();

	//clipboard format: "0000", "N", w, h, d
	int tileX = data->ted.contextHoverX;
	int tileY = data->ted.contextHoverY;
	int tilesX = 1, tilesY = 1;
	if (TedHasSelection(&data->ted)) {
		TedGetSelectionBounds(&data->ted, &tileX, &tileY, &tilesX, &tilesY);
	}
	HANDLE hString = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 10 + 4 * tilesX * tilesY);
	LPSTR clip = (LPSTR) GlobalLock(hString);
	*(uint32_t *) clip = 0x30303030;
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
		COLOR32 *bm = ScrViewerRenderBits(nscr, ncgr, nclr, data->tileBase, &wholeWidth, &wholeHeight, TRUE);
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

static void ScrViewerGraphicsChanged(NSCRVIEWERDATA *data) {
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
	PreviewLoadBgScreen(&data->nscr);
}

static void ScrViewerErase(NSCRVIEWERDATA *data) {
	int selX, selY, selW, selH;
	TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

	for (int y = 0; y < selH; y++) {
		for (int x = 0; x < selW; x++) {
			data->nscr.data[(x + selX) + (y + selY) * (data->nscr.nWidth / 8)] = data->nscr.clearValue;
		}
	}
}

void NscrViewerSetTileBase(HWND hWnd, int tileBase) {
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	data->tileBase = tileBase;
	SetEditNumber(data->hWndTileBase, tileBase);
}

static void ScrViewerMainOnMouseMove(NSCRVIEWERDATA *data, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TedMainOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
}

static void ScrViewerUpdateSelLabel(NSCRVIEWERDATA *data) {
	if (TedHasSelection(&data->ted)) {
		int selX, selY, selW, selH;
		TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

		WCHAR sizeBuffer[32];
		int len = wsprintfW(sizeBuffer, L"Selection: %dx%d", selW * 8, selH * 8);
		SendMessage(data->hWndSelectionSize, WM_SETTEXT, len, (LPARAM) sizeBuffer);
	} else {
		SendMessage(data->hWndSelectionSize, WM_SETTEXT, 0, (LPARAM) L"");
	}
}

static void ScrViewerTileHoverCallback(HWND hWnd, int tileX, int tileY) {
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWnd);

	ScrViewerUpdateSelLabel(data);
}

static int ScrViewerIsSelectionModeCallback(HWND hWnd) {
	(void) hWnd;
	return 1;
}

static HCURSOR ScrViewerGetCursorProc(HWND hWnd, int hit) {
	(void) hWnd;
	(void) hit;
	return LoadCursor(NULL, IDC_ARROW);
}

static void ScrViewerUpdateCursorCallback(HWND hWnd, int pxX, int pxY) {
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWnd);

	int hit = data->ted.mouseDownHit;
	int hitType = hit & HIT_TYPE_MASK;

	//if in content, start or continue a selection.
	if (hitType == HIT_CONTENT) {
		//if the mouse is down, start or continue a selection.
		if (!TedHasSelection(&data->ted)) {
			data->ted.selStartX = data->ted.hoverX;
			data->ted.selStartY = data->ted.hoverY;

			//set cursor to crosshair
			SetCursor(LoadCursor(NULL, IDC_CROSS));
		}

		data->ted.selEndX = data->ted.hoverX;
		data->ted.selEndY = data->ted.hoverY;
	}
}

static LRESULT WINAPI ScrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWnd);
	float dpiScale = GetDpiScale();

	switch (msg) {
		case WM_CREATE:
		{
			data->showBorders = 0;
			data->scale = 2;
			data->transparent = g_configuration.renderTransparent;


			HWND hWndViewer = CreateWindow(L"NscrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 300, 300, hWnd, NULL, NULL, NULL);
			TedInit(&data->ted, hWnd, hWndViewer);
			
			data->ted.getCursorProc = ScrViewerGetCursorProc;
			data->ted.tileHoverCallback = ScrViewerTileHoverCallback;
			data->ted.renderCallback = ScrViewerRender;
			data->ted.suppressHighlightCallback = NULL;
			data->ted.isSelectionModeCallback = ScrViewerIsSelectionModeCallback;
			data->ted.updateCursorCallback = ScrViewerUpdateCursorCallback;

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
				CheckMenuItem(GetMenu(getMainWindow(hWnd)), ID_VIEW_GRIDLINES, MF_CHECKED);
			}
			break;
		}
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			ScrViewerMainOnMouseMove(data, hWnd, msg, wParam, lParam);
			break;
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			MoveWindow(data->ted.hWndViewer, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, 
				rcClient.right - 200 - MARGIN_TOTAL_SIZE, rcClient.bottom - 22 - MARGIN_TOTAL_SIZE, FALSE);

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

			if (wParam == SIZE_RESTORED) InvalidateRect(hWnd, NULL, TRUE); //full update
			return DefMDIChildProc(hWnd, WM_SIZE, wParam, lParam);
		}
		case WM_PAINT:
		{
			TedMarginPaint(hWnd, (EDITOR_DATA *) data, &data->ted);
			InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
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
			data->ted.tilesX = data->nscr.nWidth / 8;
			data->ted.tilesY = data->nscr.nHeight / 8;
			PreviewLoadBgScreen(&data->nscr);

			data->frameData.contentWidth = data->nscr.nWidth * data->scale;
			data->frameData.contentHeight = data->nscr.nHeight * data->scale;

			RECT rc = { 0 };
			rc.right = data->frameData.contentWidth;
			rc.bottom = data->frameData.contentHeight;
			AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
			int width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL) + 200 + MARGIN_TOTAL_SIZE; //+4 to account for WS_EX_CLIENTEDGE
			int height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL) + 22 + MARGIN_TOTAL_SIZE;
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
			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
			if (hWndNcgrViewer != NULL) {
				NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrViewer);
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
			int selX, selY, selW, selH;
			TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

			int cc = wParam;
			switch (cc) {
				case VK_DELETE:
					//delete
					ScrViewerErase(data);
					InvalidateRect(hWnd, NULL, FALSE);
					TedDeselect(&data->ted);
					ScrViewerGraphicsChanged(data);
					break;
				case VK_ESCAPE:
					SendMessage(hWnd, WM_COMMAND, ID_NSCRMENU_DESELECT, 0);
					break;
				case VK_UP:
				case VK_DOWN:
				case VK_LEFT:
				case VK_RIGHT:
				{
					if (TedHasSelection(&data->ted)) {
						int shift = GetKeyState(VK_SHIFT) < 0;

						int dx = 0, dy = 0;
						switch (wParam) {
							case VK_UP: dy = -1; break;
							case VK_DOWN: dy = 1; break;
							case VK_LEFT: dx = -1; break;
							case VK_RIGHT: dx = 1; break;
						}

						if (!shift) {
							//offset whole selection
							int newX = selX + dx, newY = selY + dy;
							if (newX >= 0 && newY >= 0 && (newX + selW) < (int) data->nscr.nWidth / 8 && (newY + selH) < (int) data->nscr.nHeight / 8) {
								TedOffsetSelection(&data->ted, dx, dy);
							}
						} else {
							//offset selection end
							int newX = data->ted.selEndX + dx, newY = data->ted.selEndY + dy;
							if (newX >= 0 && newX < (int) data->nscr.nWidth / 8 && newY >= 0 && newY < (int) data->nscr.nHeight / 8) {
								data->ted.selEndX += dx;
								data->ted.selEndY += dy;
							}
						}

						InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
						TedUpdateMargins(&data->ted);
						TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
					}
					break;
				}
				case 'H':
					PostMessage(hWnd, WM_COMMAND, ID_NSCRMENU_FLIPHORIZONTALLY, 0);
					break;
				case 'V':
					PostMessage(hWnd, WM_COMMAND, ID_NSCRMENU_FLIPVERTICALLY, 0);
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
						TedSelectAll(&data->ted);
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

						HWND hWndMain = getMainWindow(hWnd);
						NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
						HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
						HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;

						NSCR *nscr = &data->nscr;
						NCGR *ncgr = NULL;
						NCLR *nclr = NULL;
						if (hWndNclrViewer != NULL) nclr = (NCLR *) EditorGetObject(hWndNclrViewer);
						if (hWndNcgrViewer != NULL) ncgr = (NCGR *) EditorGetObject(hWndNcgrViewer);

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
							unsigned char *bits = ScrViewerRenderIndexed(nscr, ncgr, data->tileBase, &width, &height, TRUE);
							ImgWriteIndexed(bits, width, height, palette, 256, location);
							free(bits);
						} else {
							//write direct
							COLOR32 *bits = ScrViewerRenderBits(nscr, ncgr, nclr, data->tileBase, &width, &height, TRUE);
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
						if (TedHasSelection(&data->ted)) {
							int selStartX, selStartY, selWidth, selHeight;
							TedGetSelectionBounds(&data->ted, &selStartX, &selStartY, &selWidth, &selHeight);
							
							//for each row
							for (int y = selStartY; y < selStartY + selHeight; y++) {
								//for width/2
								for (int x = 0; x < (selWidth + 1) / 2; x++) {
									//swap x with selWidth-1-x
									int t1 = x + selStartX, t2 = selWidth - 1 - x + selStartX;
									uint16_t d1 = data->nscr.data[t1 + y * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPX << 10);
									uint16_t d2 = data->nscr.data[t2 + y * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPX << 10);
									data->nscr.data[t1 + y * (data->nscr.nWidth >> 3)] = d2;
									if(x != selWidth) data->nscr.data[t2 + y * (data->nscr.nWidth >> 3)] = d1;
								}
							}
						}
						ScrViewerGraphicsChanged(data);
						break;
					}
					case ID_NSCRMENU_FLIPVERTICALLY:
					{
						if (TedHasSelection(&data->ted)) {
							int selStartX, selStartY, selWidth, selHeight;
							TedGetSelectionBounds(&data->ted, &selStartX, &selStartY, &selWidth, &selHeight);

							//for every column
							for (int x = selStartX; x < selStartX + selWidth; x++) {
								//for every row/2
								for (int y = 0; y < (selHeight + 1) / 2; y++) {
									int t1 = y + selStartY, t2 = selHeight - 1 - y + selStartY;
									uint16_t d1 = data->nscr.data[x + t1 * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPY << 10);
									uint16_t d2 = data->nscr.data[x + t2 * (data->nscr.nWidth >> 3)] ^ (TILE_FLIPY << 10);
									data->nscr.data[x + t1 * (data->nscr.nWidth >> 3)] = d2;
									if(y != selHeight) data->nscr.data[x + t2 * (data->nscr.nWidth >> 3)] = d1;
								}
							}
						}
						ScrViewerGraphicsChanged(data);
						break;
					}
					case ID_NSCRMENU_MAKEIDENTITY:
					{
						//each element use palette 0, increasing char index
						int selStartX, selStartY, selWidth, selHeight;
						TedGetSelectionBounds(&data->ted, &selStartX, &selStartY, &selWidth, &selHeight);

						int index = 0;
						//for every row
						for (int y = 0; y < selHeight; y++) {
							//for every column
							for (int x = selStartX; x < selStartX + selWidth; x++) {
								data->nscr.data[x + (y + selStartY) * (data->nscr.nWidth >> 3)] = index & 0x3FF;
								index++;
							}
						}
						ScrViewerGraphicsChanged(data);
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
						WORD d = data->nscr.data[data->ted.contextHoverX + data->ted.contextHoverY * (data->nscr.nWidth >> 3)];
						SendMessage(h, NV_INITIMPORTDIALOG, d, data->ted.contextHoverX | (data->ted.contextHoverY << 16));
						DoModal(h);
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

						int tileX, tileY;
						if (TedHasSelection(&data->ted)) {
							int selW, selH;
							TedGetSelectionBounds(&data->ted, &tileX, &tileY, &selW, &selH);
						} else if (data->ted.mouseOver) {
							tileX = data->ted.hoverX;
							tileY = data->ted.hoverY;
						} else {
							tileX = data->ted.contextHoverX;
							tileY = data->ted.contextHoverY;
						}

						if (*(uint32_t *) clip == 0x30303030 && clip[4] == 'N') {
							int tilesX = (clip[5] & 0xF) | ((clip[6] & 0xF) << 4);
							int tilesY = (clip[7] & 0xF) | ((clip[8] & 0xF) << 4);
							clip += 9; //advance info part

							int maxX = data->nscr.nWidth / 8, maxY = data->nscr.nHeight / 8;

							for (int y = tileY; y < tileY + tilesY; y++) {
								for (int x = tileX; x < tileX + tilesX; x++) {
									uint16_t d = (clip[0] & 0xF) | ((clip[1] & 0xF) << 4) | ((clip[2] & 0xF) << 8) | ((clip[3] & 0xF) << 12);
									if (x < (int) (data->nscr.nWidth / 8) && y < (int) (data->nscr.nHeight / 8)) {
										if (x >= 0 && x <= maxX && y >= 0 && y <= maxY) data->nscr.data[x + y * (data->nscr.nWidth / 8)] = d;
									}

									clip += 4;
								}
							}
							ScrViewerGraphicsChanged(data);
						}

						GlobalUnlock(hString);
						break;
					}
					case ID_NSCRMENU_DESELECT:
						TedDeselect(&data->ted);
						InvalidateRect(hWnd, NULL, FALSE);
						ScrViewerUpdateSelLabel(data);
						break;
					case ID_NSCRMENU_CUT:
						ScrViewerCopy(data);
						ScrViewerErase(data);
						ScrViewerGraphicsChanged(data);
						break;
					case ID_VIEW_GRIDLINES:
					case ID_ZOOM_100:
					case ID_ZOOM_200:
					case ID_ZOOM_400:
					case ID_ZOOM_800:
					case ID_ZOOM_1600:
						SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
						RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
						TedUpdateMargins(&data->ted);
						break;
				}
			} else if (lParam) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->hWndApply || hWndControl == data->hWndAdd || hWndControl == data->hWndSubtract) {
					//get data to overwrite
					int writePalette = GetCheckboxChecked(data->hWndPaletteLabel);
					int writeCharacter = GetCheckboxChecked(data->hWndCharacterLabel);

					int character = GetEditNumber(data->hWndCharacterNumber);
					int palette = SendMessage(data->hWndPaletteNumber, CB_GETCURSEL, 0, 0);
					HWND hWndMain = getMainWindow(hWnd);
					SendMessage(hWnd, NV_SETDATA, (WPARAM) character, (LPARAM) palette);
					int tilesX = data->nscr.nWidth / 8, tilesY = data->nscr.nHeight / 8;
					int nTiles = tilesX * tilesY;

					int selX, selY, selW, selH;
					TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

					for (int i = 0; i < nTiles; i++) {
						int x = i % tilesX, y = i / tilesX;
						if (x >= selX && y >= selY && x < (selX + selW) && y < (selY + selH)) {
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
					ScrViewerGraphicsChanged(data);
				} else if (hWndControl == data->hWndTileBase) {
					WORD command = HIWORD(wParam);
					if (command == EN_CHANGE) {
						int base = GetEditNumber(hWndControl);
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
			TedOnLButtonDown((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_LBUTTONUP:
			TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_DESTROY:
			TedDestroy(&data->ted);
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


typedef struct ScrViewerImportData_ {
	NCLR *nclr;
	NCGR *ncgr;
	NSCR *nscr;
	COLOR32 *px;
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
} ScrViewerImportData;

static void ScrViewerBitmapImportCallback(void *data) {
	ScrViewerImportData *importData = (ScrViewerImportData *) data;

	InvalidateRect(importData->hWndNclrViewer, NULL, FALSE);
	InvalidateRect(importData->hWndNcgrViewer, NULL, FALSE);
	InvalidateRect(importData->hWndNscrViewer, NULL, FALSE);
	SendMessage(importData->hWndNclrViewer, NV_UPDATEPREVIEW, 0, 0);
	SendMessage(importData->hWndNcgrViewer, NV_UPDATEPREVIEW, 0, 0);
	SendMessage(importData->hWndNscrViewer, NV_UPDATEPREVIEW, 0, 0);
	free(importData->px);
	free(data);
}

static DWORD WINAPI ScrViewerBitmapImportInternal(LPVOID lpParameter) {
	PROGRESSDATA *progressData = (PROGRESSDATA *) lpParameter;
	ScrViewerImportData *importData = (ScrViewerImportData *) progressData->data;
	BgReplaceSection(importData->nclr, importData->ncgr, importData->nscr, importData->px, importData->width, importData->height,
					 importData->writeScreen, importData->writeCharacterIndices,
					 importData->tileBase, importData->nPalettes, importData->paletteNumber,
					 importData->paletteOffset, importData->paletteSize,
					 importData->newPalettes, importData->charBase, importData->nMaxChars,
					 importData->newCharacters, importData->dither, importData->diffuse,
					 importData->maxTilesX, importData->maxTilesY, importData->nscrTileX, importData->nscrTileY,
					 importData->balance, importData->colorBalance, importData->enhanceColors,
					 &progressData->progress1, &progressData->progress1Max, &progressData->progress2, &progressData->progress2Max);
	progressData->waitOn = 1;
	return 0;
}

static void ScrViewerThreadedImportBitmap(PROGRESSDATA *param) {
	CreateThread(NULL, 0, ScrViewerBitmapImportInternal, param, 0, NULL);
}

static void ScrViewerImportDlgUpdate(HWND hWnd) {
	NSCRBITMAPIMPORTDATA *data = (NSCRBITMAPIMPORTDATA *) GetWindowLongPtr(hWnd, 0);
	int dither = GetCheckboxChecked(data->hWndDitherCheckbox);
	int writeChars = GetCheckboxChecked(data->hWndNewCharactersCheckbox);
	int writeScreen = GetCheckboxChecked(data->hWndWriteScreenCheckbox);
	int writeCharIndices = GetCheckboxChecked(data->hWndWriteCharIndicesCheckbox);

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

static LRESULT WINAPI ScrViewerImportDlgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
			data->hWndWriteCharIndicesCheckbox = CreateCheckbox(hWnd, L"Overwrite Character Indices", leftX, topY + 27, 150, 22, TRUE);

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

			CreateGroupbox(hWnd, L"Screen", leftX - 10, topY - 18, rightX + boxWidth - leftX, boxHeight);
			CreateGroupbox(hWnd, L"Palette", leftX - 10, middleY - 18, boxWidth, boxHeight2);
			CreateGroupbox(hWnd, L"Graphics", rightX - 10, middleY - 18, boxWidth, boxHeight2);
			CreateGroupbox(hWnd, L"Color", leftX - 10, bottomY - 18, rightX + boxWidth - leftX, boxHeight3);

			for (int i = 0; i < 16; i++) {
				WCHAR textBuffer[4];
				wsprintf(textBuffer, L"%d", i);
				SendMessage(data->hWndPaletteInput, CB_ADDSTRING, wcslen(textBuffer), (LPARAM) textBuffer);
			}
			
			setStyle(data->hWndDiffuseAmount, TRUE, WS_DISABLED);

			SetWindowSize(hWnd, width, height);
			SetGUIFont(hWnd);
			break;
		}
		case NV_INITIALIZE:
		{
			HWND hWndEditor = (HWND) lParam;
			HWND hWndMain = getMainWindow(hWndEditor);
			HWND hWndNcgrEditor = NULL, hWndNclrEditor = NULL;
			GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, &hWndNcgrEditor, 1);
			GetAllEditors(hWndMain, FILE_TYPE_PALETTE, &hWndNclrEditor, 1);
			NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndNcgrEditor);
			NCGR *ncgr = &ncgrViewerData->ncgr;

			NCLRVIEWERDATA *nclrViewerData = NULL;
			NCLR *nclr = NULL;
			if (hWndNclrEditor != NULL) {
				nclrViewerData = (NCLRVIEWERDATA *) EditorGetData(hWndNclrEditor);
				nclr = &nclrViewerData->nclr;
			}

			//set appropriate fields using data from NCGR
			int nMaxColors = 1 << ncgr->nBits;
			if (nclr != NULL) {
				if (nMaxColors > nclr->nColors) {
					nMaxColors = nclr->nColors;
				}
			}

			SetEditNumber(data->hWndPaletteSize, nMaxColors);
			SetEditNumber(data->hWndCharacterCount, ncgr->nTiles);

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

					HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					NCLR *nclr = (NCLR *) EditorGetObject(nitroPaintStruct->hWndNclrViewer);
					NCGR *ncgr = (NCGR *) EditorGetObject(nitroPaintStruct->hWndNcgrViewer);
					NSCR *nscr = (NSCR *) EditorGetObject(data->hWndEditor);
					HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;

					NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(data->hWndEditor);
					int maxTilesX = (nscr->nWidth / 8) - data->nscrTileX;
					int maxTilesY = (nscr->nHeight / 8) - data->nscrTileY;

					HWND hWndProgress = CreateWindow(L"ProgressWindowClass", L"In Progress...", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 100, hWndMain, NULL, NULL, NULL);
					ScrViewerImportData *nscrImportData = (ScrViewerImportData *) calloc(1, sizeof(ScrViewerImportData));
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
					nscrImportData->hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
					nscrImportData->hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
					nscrImportData->hWndNscrViewer = data->hWndEditor;
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					progressData->data = nscrImportData;
					progressData->callback = ScrViewerBitmapImportCallback;
					SendMessage(hWndProgress, NV_SETDATA, 0, (LPARAM) progressData);
					ShowWindow(hWndProgress, SW_SHOW);

					ScrViewerThreadedImportBitmap(progressData);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
					DoModalEx(hWndProgress, FALSE);
				} else if (hWndControl == data->hWndWriteCharIndicesCheckbox) {
					ScrViewerImportDlgUpdate(hWnd);
				} else if (hWndControl == data->hWndWriteScreenCheckbox) {
					ScrViewerImportDlgUpdate(hWnd);
				} else if (hWndControl == data->hWndDitherCheckbox) {
					ScrViewerImportDlgUpdate(hWnd);
				} else if (hWndControl == data->hWndNewCharactersCheckbox) {
					ScrViewerImportDlgUpdate(hWnd);
				}
			}
			break;
		}
		case WM_DESTROY:
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

static LRESULT WINAPI ScrViewerPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndNscrViewer = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NSCRVIEWERDATA *data = (NSCRVIEWERDATA *) EditorGetData(hWndNscrViewer);
	int contentWidth = 0, contentHeight = 0;
	if (data) {
		contentWidth = data->nscr.nWidth * data->scale;
		contentHeight = data->nscr.nHeight * data->scale;
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
		case WM_PAINT:
			TedOnViewerPaint((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_ERASEBKGND:
			return 1;
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
		case WM_SETCURSOR:
			return TedSetCursor((EDITOR_DATA *) data, &data->ted, wParam, lParam);
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			TedUpdateMargins(&data->ted);
			TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
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
		case WM_MOUSELEAVE:
			TedViewerOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
			TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_KEYDOWN:
		case WM_KEYUP:
			PostMessage(data->hWnd, msg, wParam, lParam);
			break;
		case WM_LBUTTONDOWN:
		{
			TedViewerOnLButtonDown((EDITOR_DATA *) data, &data->ted);
			InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
			TedUpdateMargins(&data->ted);
			ScrViewerUpdateSelLabel(data);

			int selX, selY, selW, selH;
			TedGetSelectionBounds(&data->ted, &selX, &selY, &selW, &selH);

			int tile = selX + selY * (data->nscr.nWidth / 8);
			uint16_t d = data->nscr.data[tile];
			int character = d & 0x3FF;
			int palette = d >> 12;

			SendMessage(data->hWndPaletteNumber, CB_SETCURSEL, palette, 0);
			SetEditNumber(data->hWndCharacterNumber, character);
			break;
		}
		case WM_LBUTTONUP:
			TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_RBUTTONUP:
		{
			SetFocus(hWnd);

			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			//transform it by scroll position
			int scrollX, scrollY;
			TedGetScroll(&data->ted, &scrollX, &scrollY);
			mousePos.x += scrollX;
			mousePos.y += scrollY;
			
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < (int) (data->nscr.nWidth * data->scale) && mousePos.y < (int) (data->nscr.nHeight * data->scale)) {
				TedOnRButtonDown(&data->ted);
				//if it is within the colors area, open a color chooser
				HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 2);

				//disable non-applicable options
				const int needsSel[] = {
					ID_NSCRMENU_DESELECT, ID_NSCRMENU_CUT, ID_NSCRMENU_COPY,
					ID_NSCRMENU_FLIPHORIZONTALLY, ID_NSCRMENU_FLIPVERTICALLY,
					ID_NSCRMENU_MAKEIDENTITY
				};
				int hasSel = TedHasSelection(&data->ted);
				for (int i = 0; i < sizeof(needsSel) / sizeof(int); i++) {
					EnableMenuItem(hPopup, needsSel[i], (!hasSel) ? MF_DISABLED : MF_ENABLED);
				}

				POINT mouse;
				GetCursorPos(&mouse);
				TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWndNscrViewer, NULL);
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void ScrViewerRegisterImportClass(void) {
	RegisterGenericClass(L"NscrBitmapImportClass", ScrViewerImportDlgWndProc, sizeof(LPVOID));
}

static void ScrViewerRegisterPreviewClass(void) {
	RegisterGenericClass(L"NscrPreviewClass", ScrViewerPreviewWndProc, sizeof(LPVOID));
}

void RegisterNscrViewerClass(void) {
	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EDITOR_CLASS *cls = EditorRegister(L"NscrViewerClass", ScrViewerWndProc, L"Screen Editor", sizeof(NSCRVIEWERDATA), features);
	EditorAddFilter(cls, NSCR_TYPE_NSCR, L"nscr", L"NSCR Files (*.nscr)\0*.nscr\0");
	EditorAddFilter(cls, NSCR_TYPE_NC, L"nsc", L"NSC Files (*.nsc)\0*.nsc\0");
	EditorAddFilter(cls, NSCR_TYPE_IC, L"isc", L"ISC Files (*.isc)\0*.isc\0");
	EditorAddFilter(cls, NSCR_TYPE_AC, L"asc", L"ASC Files (*.asc)\0*.asc\0");
	EditorAddFilter(cls, NSCR_TYPE_HUDSON, L"bin", L"Screen Files (*.bin)\0*.bin\0");
	EditorAddFilter(cls, NSCR_TYPE_HUDSON2, L"bin", L"Screen Files (*.bin)\0*.bin\0");
	EditorAddFilter(cls, NSCR_TYPE_BIN, L"bin", L"Screen Files (*.bin, *nsc.bin, *isc.bin, *.nbfs)\0*.bin;*.nbfs\0");
	EditorAddFilter(cls, NSCR_TYPE_COMBO, L"bin", L"Combination Files (*.dat, *.bin)\0*.dat;*.bin\0");

	ScrViewerRegisterImportClass();
	ScrViewerRegisterPreviewClass();
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
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL) + 200 + MARGIN_TOTAL_SIZE; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL) + 22 + MARGIN_TOTAL_SIZE;
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
		width = rc.right - rc.left + 4 + GetSystemMetrics(SM_CXVSCROLL) + 200 + MARGIN_TOTAL_SIZE; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4 + GetSystemMetrics(SM_CYHSCROLL) + 22 + MARGIN_TOTAL_SIZE;
	}

	HWND hWnd = EditorCreate(L"NscrViewerClass", x, y, width, height, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE_IMMEDIATE, (WPARAM) nscr, 0);
	if (nscr->header.format == NSCR_TYPE_HUDSON || nscr->header.format == NSCR_TYPE_HUDSON2) {
		SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
	}
	return hWnd;
}
