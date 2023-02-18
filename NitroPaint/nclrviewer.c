#include <Windows.h>
#include <CommCtrl.h>

#include "nclrviewer.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "ncgrviewer.h"
#include "nscrviewer.h"
#include "ncerviewer.h"
#include "colorchooser.h"
#include "resource.h"
#include "palette.h"
#include "gdip.h"

extern HICON g_appIcon;

//IS.Colors4
typedef struct NC_CLIPBOARD_PALETTE_HEADER_ {
	DWORD magic; //0xC208B8
	BOOL is1D;
	int originRow;
	int originCol;
	int unused[2];
	int nCols;
	int nRows;
} NC_CLIPBOARD_PALETTE_HEADER;

typedef struct NC_CLIPBOARD_PALETTE_FOOTER_ {
	uint8_t field0[8]; //no idea how these work
} NC_CLIPBOARD_PALETTE_FOOTER;

//OPX_PALETTE
typedef struct OP_CLIPBOARD_PALETTE_HEADER_ {
	short three; //3
	short nColors;
} OP_CLIPBOARD_PALETTE_HEADER;

static int g_ncClipboardFormat = 0;
static int g_opClipboardFormat = 0;

VOID NclrViewerEnsureClipboardFormats(VOID) {
	if (g_ncClipboardFormat == 0) {
		g_ncClipboardFormat = RegisterClipboardFormat(L"IS.Colors4");
		g_opClipboardFormat = RegisterClipboardFormat(L"OPX_PALETTE");
	}
}

VOID CopyPalette(COLOR *palette, int nColors) {
	NclrViewerEnsureClipboardFormats();

	//NC and OPX formats
	int ncSize = sizeof(NC_CLIPBOARD_PALETTE_HEADER) + nColors * 8 + sizeof(NC_CLIPBOARD_PALETTE_FOOTER);
	int opSize = sizeof(OP_CLIPBOARD_PALETTE_HEADER) + nColors * 4;
	HGLOBAL hNc = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, ncSize);
	HGLOBAL hOp = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, opSize);
	NC_CLIPBOARD_PALETTE_HEADER *ncData = (NC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hNc);
	OP_CLIPBOARD_PALETTE_HEADER *opData = (OP_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hOp);
	COLOR32 *ncPalette = (COLOR32 *) (ncData + 1);
	COLOR32 *opPalette = (COLOR32 *) (opData + 1);
	ncData->magic = 0xC208B8;
	ncData->is1D = 1;
	ncData->nCols = nColors;
	ncData->nRows = 1;
	opData->three = 3;
	opData->nColors = nColors;
	for (int i = 0; i < nColors; i++) {
		ncPalette[i] = ColorConvertFromDS(palette[i]);
		ncPalette[i + nColors] = ColorConvertFromDS(palette[i]);
		opPalette[i] = ColorConvertFromDS(palette[i]);
	}
	GlobalUnlock(hNc);
	GlobalUnlock(hOp);
	SetClipboardData(g_ncClipboardFormat, hNc);
	SetClipboardData(g_opClipboardFormat, hOp);
}

VOID PastePalette(COLOR *dest, int nMax) {
	NclrViewerEnsureClipboardFormats();

	HGLOBAL hNc = GetClipboardData(g_ncClipboardFormat);
	HGLOBAL hOp = GetClipboardData(g_opClipboardFormat);
	COLOR32 *src = NULL;
	int nCols = 0;

	if (hNc != NULL) {
		NC_CLIPBOARD_PALETTE_HEADER *ncData = (NC_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hNc);
		nCols = ncData->nCols * ncData->nRows;
		src = (COLOR32 *) (ncData + 1);
	} else if (hOp != NULL) {
		OP_CLIPBOARD_PALETTE_HEADER *opData = (OP_CLIPBOARD_PALETTE_HEADER *) GlobalLock(hOp);
		nCols = opData->nColors;
		src = (COLOR32 *) (opData + 1);
	} else {
		return;
	}

	if (nCols > nMax) nCols = nMax;
	for (int i = 0; i < nCols; i++) {
		dest[i] = ColorConvertToDS(src[i]);
	}
	if (hNc != NULL) GlobalUnlock(hNc);
	if (hOp != NULL) GlobalUnlock(hOp);
}

VOID PaintNclrViewer(HWND hWnd, NCLRVIEWERDATA *data, HDC hDC) {

	WORD *cols = data->nclr.colors;
	int nRows = data->nclr.nColors / 16;

	HPEN defaultOutline = GetStockObject(BLACK_PEN);
	HPEN highlightRowOutline = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
	HPEN previewOutline = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
	HPEN highlightCellOutline = GetStockObject(WHITE_PEN);
	HPEN ncerOutline = CreatePen(PS_SOLID, 1, RGB(0, 192, 32));
	HPEN opSrcOutline = CreatePen(PS_SOLID, 1, RGB(255, 255, 0));
	HPEN opDstOutline = CreatePen(PS_DOT, 1, RGB(0, 192, 128));

	int previewPalette = -1;
	HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	int nRowsPerPalette = (1 << data->nclr.nBits) / 16;
	if (nitroPaintStruct->hWndNcgrViewer) {
		NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLong(nitroPaintStruct->hWndNcgrViewer, 0);
		previewPalette = ncgrViewerData->selectedPalette;
		nRowsPerPalette = (1 << ncgrViewerData->ncgr.nBits) / 16;
	}
	int ncerPalette = -1;
	if (nitroPaintStruct->hWndNcerViewer) {
		NCERVIEWERDATA *ncerViewerData = (NCERVIEWERDATA *) GetWindowLong(nitroPaintStruct->hWndNcerViewer, 0);
		NCER_CELL *cell = ncerViewerData->ncer.cells + ncerViewerData->cell;
		NCER_CELL_INFO info;
		decodeAttributesEx(&info, cell, ncerViewerData->oam);
		ncerPalette = info.palette;
	}
	
	int nclrRowsPerPalette = nRowsPerPalette; //(1 << data->nclr.nBits) / 16; //16 in 4, 256 in 8
	int highlightRowStart = previewPalette * nclrRowsPerPalette;
	int highlightRowEnd = highlightRowStart + nRowsPerPalette;
	
	int palOpSrcIndex = -1, palOpSrcLength = 0, palOpDstIndex = -1, palOpStrideLength = 0, palOpBlocks = 0;
	if (data->palOpDialog) {
		palOpSrcIndex = data->palOp.srcIndex;
		palOpSrcLength = data->palOp.srcLength;
		palOpDstIndex = data->palOp.dstOffset * data->palOp.dstStride + data->palOp.srcIndex;
		palOpStrideLength = data->palOp.dstStride;
		palOpBlocks = data->palOp.dstCount;
	}

	int nColors = 0;

	int srcIndex, dstIndex;
	if (!data->rowDragging) {
		srcIndex = (data->dragStart.x / 16) + 16 * (data->dragStart.y >> 4);
		dstIndex = (data->dragPoint.x / 16) + 16 * (data->dragPoint.y >> 4);

		if (data->preserveDragging) {
			int paletteMask = nRowsPerPalette == 1 ? 0xF0 : 0x00;
			if ((srcIndex & paletteMask) != (dstIndex & paletteMask)) {
				dstIndex = (srcIndex & paletteMask) | (dstIndex & ~paletteMask);
			}
		}
	} else {
		srcIndex = 16 * (data->dragStart.y >> 4);
		dstIndex = 16 * (data->dragPoint.y >> 4);
	}

	SetBkColor(hDC, RGB(0, 0, 0));
	for (int y = 0; y < nRows; y++) {
		for (int x = 0; x < 16; x++) {
			int index = nColors;
			if (data->dragging && data->mouseDown) {
				if (!data->rowDragging) {
					if (dstIndex < data->nclr.nColors && dstIndex >= 0) {
						if (index == srcIndex) index = dstIndex;
						else if (index == dstIndex) index = srcIndex;
					}
				} else {
					if (dstIndex + 15 < data->nclr.nColors && dstIndex >= 0) {
						if (index >= srcIndex && index < srcIndex + 16) index = dstIndex + (index & 0xF);
						else if (index >= dstIndex && index < dstIndex + 16) index = srcIndex + (index & 0xF);
					}
				}
			}
			COLOR col = cols[index];
			COLOR32 rgb = ColorConvertFromDS(col);

			HBRUSH hbr = CreateSolidBrush(rgb);
			SelectObject(hDC, hbr);

			//is in palette operation destination area?
			int isInPalOpDest = 0;
			if (data->palOpDialog && palOpStrideLength) {
				int dstRel = x + y * 16 - palOpDstIndex;
				if (dstRel >= 0 && dstRel < (palOpBlocks - 1) * palOpStrideLength + palOpSrcLength) {
					dstRel %= palOpStrideLength;
					if (dstRel < palOpSrcLength) {
						isInPalOpDest = 1;
					}
				}
			}

			if (y * 16 + x >= palOpSrcIndex && y * 16 + x < palOpSrcIndex + palOpSrcLength) {
				SelectObject(hDC, opSrcOutline);
			} else if (isInPalOpDest) {
				SelectObject(hDC, opDstOutline);
			} else if (nColors == data->hoverIndex) {
				SelectObject(hDC, highlightCellOutline);
			} else if (previewPalette != -1 && (y >= highlightRowStart && y < highlightRowEnd)) {
				SelectObject(hDC, previewOutline);
			} else if(ncerPalette != -1 && (y >= (ncerPalette * nclrRowsPerPalette) && y < (ncerPalette * nclrRowsPerPalette + nRowsPerPalette))){
				SelectObject(hDC, ncerOutline);
			} else if(y == data->hoverY) {
				SelectObject(hDC, highlightRowOutline);
			} else {
				SelectObject(hDC, defaultOutline);
			}

			Rectangle(hDC, x * 16, y * 16, x * 16 + 16, y * 16 + 16);

			DeleteObject(hbr);
			nColors++;
			if (nColors > data->nclr.nColors) break;
		}
	}

	DeleteObject(highlightRowOutline);
	DeleteObject(previewOutline);
	DeleteObject(ncerOutline);
	DeleteObject(opSrcOutline);
	DeleteObject(opDstOutline);
}

void NclrViewerPalOpUpdateCallback(PAL_OP *palOp) {
	HWND hWnd = (HWND) palOp->param;
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	PalopRunOperation(data->tempPalette, data->nclr.colors, data->nclr.nColors, palOp);

	InvalidateRect(hWnd, NULL, FALSE);
}

int lightness(COLOR col) {
	int r = GetR(col);
	int g = GetG(col);
	int b = GetB(col);
	return 1063 * r + 3576 * g + 361 * b;
}

int colorSortLightness(LPCVOID p1, LPCVOID p2) {
	COLOR c1 = *(COLOR *) p1;
	COLOR c2 = *(COLOR *) p2;
	return lightness(c1) - lightness(c2);
}

int colorSortHue(LPCVOID p1, LPCVOID p2) {
	COLOR c1 = *(COLOR *) p1;
	COLOR c2 = *(COLOR *) p2;

	COLORREF col1 = ColorConvertFromDS(c1);
	COLORREF col2 = ColorConvertFromDS(c2);

	int h1, s1, v1, h2, s2, v2;
	ConvertRGBToHSV(col1, &h1, &s1, &v1);
	ConvertRGBToHSV(col2, &h2, &s2, &v2);
	return h1 - h2;
}

BOOL ValidateColorsNscrProc(HWND hWnd, void *param) {
	NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	nscrViewerData->verifyColor = (int) param;
	nscrViewerData->verifyFrames = 10;
	SetTimer(hWnd, 1, 100, NULL);
	return TRUE;
}

BOOL SwapNscrPalettesProc(HWND hWnd, void *param) {
	int *srcDest = (int *) param;
	int srcPalette = srcDest[0];
	int dstPalette = srcDest[1];

	NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	NSCR *nscr = &nscrViewerData->nscr;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		uint16_t d = nscr->data[i];
		int pal = (d >> 12) & 0xF;
		if (pal == srcPalette) pal = dstPalette;
		else if (pal == dstPalette) pal = srcPalette;
		d = (d & 0xFFF) | (pal << 12);
		nscr->data[i] = d;
	}
	return TRUE;
}

void paletteSwapColors(COLOR *palette, int i1, int i2) {
	COLOR c1 = palette[i1];
	palette[i1] = palette[i2];
	palette[i2] = c1;
}

int paletteNeuroSortPermute(COLOR *palette, int nColors, unsigned long long bestDiff) {
	int totalDiff = 0;
	for (int i = 1; i < nColors; i++) {
		COLOR32 last = ColorConvertFromDS(palette[i - 1]);
		int nextIndex = i;

		int minDiff = 0x7FFFFFFF;
		for (int j = i; j < nColors; j++) {
			COLOR32 test = ColorConvertFromDS(palette[j]);
			
			int dr, dg, db, dy, du, dv;
			dr = (last & 0xFF) - (test & 0xFF);
			dg = ((last >> 8) & 0xFF) - ((test >> 8) & 0xFF);
			db = ((last >> 16) & 0xFF) - ((test >> 16) & 0xFF);
			convertRGBToYUV(dr, dg, db, &dy, &du, &dv);
			int diff = 4 * dy * dy + du * du + dv * dv;
			if (diff < minDiff) {
				nextIndex = j;
				minDiff = diff;
			}
		}

		paletteSwapColors(palette, i, nextIndex);
		totalDiff += minDiff;
		if (totalDiff >= bestDiff) return totalDiff;
	}
	return totalDiff;
}

typedef struct PALETTE_ARRANGE_DATA_ {
	HWND hWndViewer;
	COLOR *palette;
	int nColors;
} PALETTE_ARRANGE_DATA;

DWORD CALLBACK paletteNeuroSort(LPVOID param) {
	PALETTE_ARRANGE_DATA *data = (PALETTE_ARRANGE_DATA *) param;
	HWND hWnd = data->hWndViewer;
	COLOR *palette = data->palette;
	int nColors = data->nColors;

	int best = 0x7FFFFFFF;
	COLOR *tempBuf = (COLOR *) calloc(nColors, sizeof(COLOR));

	//iterate permutations
	for (int i = 0; i < nColors; i++) {
		memcpy(tempBuf, palette, nColors * sizeof(COLOR));
		paletteSwapColors(tempBuf, 0, i);

		int permutationError = paletteNeuroSortPermute(tempBuf, nColors, best);
		if (permutationError < best) {
			memcpy(palette, tempBuf, nColors * sizeof(COLOR));
			best = permutationError;
			PostMessage(hWnd, NV_XTINVALIDATE, 0, 0);
		}
	}

	free(tempBuf);
	free(data);
	return 0;
}

void paletteNeuroSortThreaded(HWND hWnd, COLOR *palette, int nColors) {
	PALETTE_ARRANGE_DATA *data = (PALETTE_ARRANGE_DATA *) calloc(1, sizeof(PALETTE_ARRANGE_DATA));
	DWORD tid;
	data->hWndViewer = hWnd;
	data->palette = palette;
	data->nColors = nColors;
	CreateThread(NULL, 0, paletteNeuroSort, (LPVOID) data, 0, &tid);
}

LRESULT WINAPI NclrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (NCLRVIEWERDATA *) calloc(1, sizeof(NCLRVIEWERDATA));
		SetWindowLongPtr(hWnd, 0, (LONG) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			data->contentWidth = 256;
			data->contentHeight = 256;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;

			PAL_OP *palOp = &data->palOp;
			HWND hWndMain = getMainWindow(hWnd);
			palOp->hWndParent = hWndMain;
			palOp->param = (void *) hWnd;
			palOp->dstOffset = 1;
			palOp->ignoreFirst = 1;
			palOp->dstCount = 1;
			palOp->dstStride = 16;
			palOp->srcLength = 16;
			palOp->updateCallback = NclrViewerPalOpUpdateCallback;

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->contentWidth;
			info.nPos = 0;
			info.nPage = rcClient.right - rcClient.left;
			info.nTrackPos = 0;
			info.fMask = SIF_POS | SIF_RANGE | SIF_POS | SIF_TRACKPOS | SIF_PAGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = data->contentHeight;
			info.nPage = rcClient.bottom - rcClient.top;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			break;
		}
		case NV_SETTITLE:
		{
			LPWSTR path = (LPWSTR) lParam;
			WCHAR titleBuffer[MAX_PATH + 19];
			if (!g_configuration.fullPaths) path = GetFileName(path);
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - Palette Editor", 38);
			SetWindowText(hWnd, titleBuffer);
			break;
		}
		case NV_INITIALIZE_IMMEDIATE:
		case NV_INITIALIZE:
		{
			if (msg == NV_INITIALIZE) {
				LPWSTR path = (LPWSTR) wParam;
				memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
				int n = nclrReadFile(&data->nclr, path);
				if (n) return 0;
				
				SendMessage(hWnd, NV_SETTITLE, 0, (LPARAM) path);
			} else {
				NCLR *nclr = (NCLR *) wParam;
				memcpy(&data->nclr, nclr, sizeof(NCLR));
			}

			HWND hWndMain = getMainWindow(hWnd);
			InvalidateAllEditors(hWndMain, FILE_TYPE_CHAR);
			InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);

			if (data->nclr.header.format == NCLR_TYPE_HUDSON) {
				SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)));
			}
			return 1;
		}
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
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

			if (data->dragStart.x / 16 != mousePos.x / 16 || data->dragStart.y / 16 != mousePos.y / 16) {
				data->dragging = 1;
			}
			data->dragPoint.x = mousePos.x;
			data->dragPoint.y = mousePos.y;

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

			int nRows = data->nclr.nColors / 16;

			int hoverX = -1, hoverY = -1, hoverIndex = -1;
			if (mousePos.x >= 0 && mousePos.x < 256 && mousePos.y >= 0) {
				hoverX = mousePos.x / 16;
				hoverY = mousePos.y / 16;
				hoverIndex = hoverX + hoverY * 16;
				if (hoverY >= nRows) {
					hoverX = -1, hoverY = -1, hoverIndex = -1;
				}
			}
			if (msg == WM_MOUSELEAVE) {
				hoverX = -1, hoverY = -1, hoverIndex = -1;
			}
			data->hoverX = hoverX;
			data->hoverY = hoverY;
			data->hoverIndex = hoverIndex;

			HWND hWndMain = getMainWindow(hWnd);
			InvalidateRect(hWnd, NULL, FALSE);
			InvalidateAllEditors(hWndMain, FILE_TYPE_CHAR);
			InvalidateAllEditors(hWndMain, FILE_TYPE_CELL);
			InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);
			break;
		}
		case WM_LBUTTONDOWN:
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			int ctrlPressed = GetKeyState(VK_CONTROL) >> 15;
			int shiftPressed = GetKeyState(VK_SHIFT) >> 15;

			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < 256) {
				int x = mousePos.x / 16;
				int y = mousePos.y / 16;
				int index = y * 16 + x;
				if (index < data->nclr.nColors) {
					data->mouseDown = 1;
					data->dragging = 0;
					data->rowDragging = !!ctrlPressed;
					data->preserveDragging = !!shiftPressed;

					SCROLLINFO horiz, vert;
					horiz.cbSize = sizeof(horiz);
					vert.cbSize = sizeof(vert);
					horiz.fMask = SIF_ALL;
					vert.fMask = SIF_ALL;
					GetScrollInfo(hWnd, SB_HORZ, &horiz);
					GetScrollInfo(hWnd, SB_VERT, &vert);

					mousePos.x += horiz.nPos;
					mousePos.y += vert.nPos;

					data->dragStart.x = mousePos.x;
					data->dragStart.y = mousePos.y;

					SetCapture(hWnd);
				}
			}

			break;
		}
		case WM_LBUTTONUP:
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);
			ReleaseCapture();
			if (!data->mouseDown) {
				break;
			}
			if (!data->dragging) {
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
				//if it is within the colors area, open a color chooser
				if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < 256) {
					int x = mousePos.x / 16;
					int y = mousePos.y / 16;
					int index = y * 16 + x;
					if (index < data->nclr.nColors && index >= 0) {
						HWND hWndMain = getMainWindow(hWnd);
						CHOOSECOLOR cc = { 0 };
						cc.lStructSize = sizeof(cc);
						cc.hInstance = (HWND) (HINSTANCE) GetWindowLong(hWnd, GWL_HINSTANCE); //weird struct definition?
						cc.hwndOwner = hWndMain;
						cc.rgbResult = ColorConvertFromDS(data->nclr.colors[index]);
						cc.lpCustColors = data->tmpCust;
						cc.Flags = 0x103;
						BOOL (__stdcall *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
						if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;
						if (ChooseColorFunction(&cc)) {
							DWORD result = cc.rgbResult;
							data->nclr.colors[index] = ColorConvertToDS(result);
							InvalidateRect(hWnd, NULL, FALSE);
							
							InvalidateAllEditors(hWndMain, FILE_TYPE_CHAR);
							InvalidateAllEditors(hWndMain, FILE_TYPE_CELL);
							InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);
						}
					}
				}
			} else {
				//handle drag finish
				//compute source index
				int srcIndex = (data->dragStart.x / 16) + 16 * (data->dragStart.y >> 4);
				int dstIndex = (data->dragPoint.x / 16) + 16 * (data->dragPoint.y >> 4);
				if (data->rowDragging) {
					srcIndex = 16 * (data->dragStart.y >> 4);
					dstIndex = 16 * (data->dragPoint.y >> 4);
				}

				HWND hWndMain = getMainWindow(hWnd);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
				HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
				if (!data->rowDragging) {
					//test for preserve dragging to adjust destination 
					if (data->preserveDragging && hWndNcgrViewer != NULL) {
						NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
						NCGR *ncgr = &ncgrViewerData->ncgr;

						int paletteMask = 0xFF << ncgr->nBits;
						if ((srcIndex & paletteMask) != (dstIndex & paletteMask)) {
							dstIndex = (srcIndex & paletteMask) | (dstIndex & ~paletteMask);
						}
					}

					if (dstIndex < data->nclr.nColors && dstIndex >= 0) {
						WORD *pal = data->nclr.colors;
						WORD src = pal[srcIndex];
						pal[srcIndex] = pal[dstIndex];
						pal[dstIndex] = src;

						//if preserve mode, update associated graphics data.
						if (data->preserveDragging) {

							if (hWndNcgrViewer != NULL) {
								NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
								NCGR *ncgr = &ncgrViewerData->ncgr;

								//if no screen, just swap the indices if the palette numbers line up.
								int nclrPalette = srcIndex >> ncgr->nBits;
								int mask = ncgr->nBits == 8 ? 0xFF : 0xF;
								int nNscrEditors;
								if ((nNscrEditors = GetAllEditors(hWndMain, FILE_TYPE_SCREEN, NULL, 0)) == 0) {
									int ncgrPalette = ncgrViewerData->selectedPalette;
									if (ncgrPalette == nclrPalette) {

										for (int i = 0; i < ncgr->nTiles; i++) {
											BYTE *tile = ncgr->tiles[i];
											for (int j = 0; j < 64; j++) {
												if (tile[j] == (srcIndex & mask)) tile[j] = dstIndex & mask;
												else if (tile[j] == (dstIndex & mask)) tile[j] = srcIndex & mask;
											}
										}
									}
								} else {
									//this is messy. To avoid "fxing" a tile twice, keep track of which ones have been "fixed".
									BYTE *fixBuffer = (BYTE *) calloc(ncgr->nTiles, 1);

									//check each open screen file
									HWND *nscrViewers = (HWND *) calloc(nNscrEditors, sizeof(HWND));
									GetAllEditors(hWndMain, FILE_TYPE_SCREEN, nscrViewers, nNscrEditors);
									for (int nscrId = 0; nscrId < nNscrEditors; nscrId++) {
										//with screen, so do the above but only for tiles referenced by it.
										HWND hWndNscrViewer = nscrViewers[nscrId];
										NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0);
										NSCR *nscr = &nscrViewerData->nscr;

										for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
											WORD d = nscr->data[i];
											int chr = d & 0x3FF;
											int pal = (d >> 12) & 0xF;

											if (pal == nclrPalette) {
												int cIndex = chr - nscrViewerData->tileBase;
												if (cIndex >= 0 && cIndex < ncgr->nTiles && !fixBuffer[cIndex]) {
													BYTE *tile = ncgr->tiles[cIndex];
													for (int j = 0; j < 64; j++) {
														if (tile[j] == (srcIndex & mask)) tile[j] = dstIndex & mask;
														else if (tile[j] == (dstIndex & mask)) tile[j] = srcIndex & mask;
													}
													fixBuffer[cIndex] = 1;
												}
											}
										}
									}
									free(fixBuffer);
									free(nscrViewers);
								}
							}
						}
					}
				} else {
					if (dstIndex + 15 < data->nclr.nColors && dstIndex >= 0) {
						COLOR tmp[16];
						COLOR *pal = data->nclr.colors;
						memcpy(tmp, pal + srcIndex, 32);
						memcpy(pal + srcIndex, pal + dstIndex, 32);
						memcpy(pal + dstIndex, tmp, 32);

						//if screen is present and we're in preserve mode, then switch relevant tile palettes as well.
						if (data->preserveDragging) {
							//doing this only really makes sense for 4-bit graphics, but who are we to disagree with the user
							int srcPalette = srcIndex >> 4;
							int dstPalette = dstIndex >> 4;
							int srcDest[] = { srcPalette, dstPalette };
							EnumAllEditors(hWndMain, FILE_TYPE_SCREEN, SwapNscrPalettesProc, srcDest);
						}
					}
				}
				InvalidateRect(hWnd, NULL, FALSE);
			}
			data->mouseDown = 0;
			data->dragging = 0;
			break;
		}
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
			//if it is within the colors area, open a color chooser
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < 256) {
				int x = mousePos.x / 16;
				int y = mousePos.y / 16;
				int index = y * 16 + x;
				if (index < data->nclr.nColors) {
					HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 0);
					POINT mouse;
					GetCursorPos(&mouse);
					TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL);
					data->contextHoverY = hoverY;
					data->contextHoverX = hoverX;
				}
			}
			break;
		}
		case NV_XTINVALIDATE:
			InvalidateRect(hWnd, NULL, FALSE);
			break;
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
			HBITMAP hBitmap = CreateCompatibleBitmap(hWindowDC, max(data->contentWidth, horiz.nPos + rcClient.right), max(data->contentHeight, vert.nPos + rcClient.bottom));
			SelectObject(hDC, hBitmap);
			IntersectClipRect(hDC, horiz.nPos, vert.nPos, horiz.nPos + rcClient.right, vert.nPos + rcClient.bottom);
			DefMDIChildProc(hWnd, WM_ERASEBKGND, (WPARAM) hDC, 0);
			HPEN defaultPen = SelectObject(hDC, GetStockObject(NULL_PEN));
			HBRUSH defaultBrush = SelectObject(hDC, GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1));
			Rectangle(hDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			SelectObject(hDC, defaultPen);
			SelectObject(hDC, defaultBrush);

			PaintNclrViewer(hWnd, data, hDC);

			BitBlt(hWindowDC, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hDC, horiz.nPos, vert.nPos, SRCCOPY);
			EndPaint(hWnd, &ps);
			DeleteObject(hDC);
			DeleteObject(hBitmap);
			break;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_CTLCOLORSTATIC:
			SetBkMode((HDC) wParam, TRANSPARENT);
			if(g_useDarkTheme) SetTextColor((HDC) wParam, RGB(255, 255, 255));
		case WM_CTLCOLORBTN:
		{
			return GetClassLong(hWnd, GCL_HBRBACKGROUND);
		}
		case NV_PICKFILE:
		{
			LPCWSTR filter = L"NCLR Files (*.nclr)\0*.nclr\0All Files\0*.*\0";
			switch (data->nclr.header.format) {
				case NCLR_TYPE_BIN:
				case NCLR_TYPE_HUDSON:
					filter = L"Palette Files (*.bin, *ncl.bin, *icl.bin, *.nbfp)\0*.bin;*.nbfp\0All Files\0*.*\0";
					break;
				case NCLR_TYPE_COMBO:
					filter = L"Combination Files (*.dat, *.bnr, *.bin)\0*.dat;*.bnr;*.bin\0";
					break;
				case NCLR_TYPE_NC:
					filter = L"NCL Files (*.ncl)\0*.ncl\0All Files\0*.*\0";
					break;
			}
			LPWSTR path = saveFileDialog(getMainWindow(hWnd), L"Save As...", filter, L"nclr");
			if (path != NULL) {
				memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
				SendMessage(hWnd, NV_SETTITLE, 0, (LPARAM) path);
				free(path);
			}
			break;
		}
		case WM_COMMAND:
		{
			if (lParam == 0 && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
					case ID_MENU_PASTE:
					{
						int offset = data->contextHoverY * 16;

						OpenClipboard(hWnd);
						PastePalette(data->nclr.colors + offset, data->nclr.nColors - offset);
						CloseClipboard();

						HWND hWndMain = getMainWindow(hWnd);
						InvalidateAllEditors(hWndMain, FILE_TYPE_CHAR);
						InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_MENU_COPY:
					{
						int offset = data->contextHoverY * 16;
						int length = 16;
						int maxOffset = data->nclr.nColors;
						if (offset + length >= maxOffset) {
							length = maxOffset - offset;
							if (length < 0) length = 0;
						}

						OpenClipboard(hWnd);
						EmptyClipboard();
						CopyPalette(data->nclr.colors + offset, length);
						CloseClipboard();
						break;
					}
					case ID_MENU_INVERTCOLOR:
					{
						int index = data->contextHoverY * 16;
						COLOR *pal = data->nclr.colors;
						for (int i = 0; i < 16; i++) {
							pal[index + i] ^= 0x7FFF;
						}
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_MENU_MAKEGRAYSCALE:
					{
						int index = data->contextHoverY * 16;
						COLOR *pal = data->nclr.colors;
						for (int i = 0; i < 16; i++) {
							COLOR col = pal[index + i];
							int r = GetR(col);
							int g = GetG(col);
							int b = GetB(col);

							//0.2126r + 0.7152g + 0.0722b
							int l = lightness(col);

							pal[index + i] = ColorCreate(l, l, l);
						}
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_ARRANGEPALETTE_BYLIGHTNESS:
					case ID_ARRANGEPALETTE_BYHUE:
					case ID_ARRANGEPALETTE_NEURO:
					{
						int index = data->contextHoverX + data->contextHoverY * 16;
						int palette = index >> data->nclr.nBits;

						int nColsPerPalette = 1 << data->nclr.nBits;
						COLOR *pal = data->nclr.colors + palette * nColsPerPalette;
						int nColors = nColsPerPalette;
						if ((palette + 1) * nColsPerPalette > data->nclr.nColors) 
							nColors = data->nclr.nColors - palette * nColsPerPalette;

						int type = LOWORD(wParam);
						if (type == ID_ARRANGEPALETTE_BYLIGHTNESS || type == ID_ARRANGEPALETTE_BYHUE) {
							qsort(pal + 1, nColors - 1, sizeof(COLOR),
								type == ID_ARRANGEPALETTE_BYLIGHTNESS ? colorSortLightness : colorSortHue);
						} else {
							paletteNeuroSortThreaded(hWnd, pal + 1, nColors - 1);
						}
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_FILE_SAVE:
					case ID_FILE_SAVEAS:
					{
						if (data->szOpenFile[0] == L'\0' || LOWORD(wParam) == ID_FILE_SAVEAS) {
							SendMessage(hWnd, NV_PICKFILE, 0, 0);
						}
						if (data->szOpenFile[0] != L'\0') nclrWriteFile(&data->nclr, data->szOpenFile);;
						break;
					}
					case ID_FILE_EXPORT:
					{
						LPWSTR path = saveFileDialog(getMainWindow(hWnd), L"Export Palette", L"PNG Files (*.png)\0*.png\0All Files\0*.*\0", L"png");
						if (path == NULL) break;

						//construct bitmap
						int width = 16;
						int height = (data->nclr.nColors + 15) / 16;
						COLOR32 *bits = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
						for (int i = 0; i < data->nclr.nColors; i++) {
							COLOR32 as32 = ColorConvertFromDS(data->nclr.colors[i]) | 0xFF000000;
							bits[i] = as32;
						}
						writeImage(bits, width, height, path);
						free(path);

						break;
					}
					case ID_MENU_IMPORT:
					{
						LPWSTR path = openFileDialog(getMainWindow(hWnd), L"Import Palette", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
						if (path == NULL) break;

						int width, height;
						COLOR32 *colors = gdipReadImage(path, &width, &height);
						int nColors = width * height;
						int startIndex = data->contextHoverX + data->contextHoverY * 16;
						int nCopyColors = min(nColors, data->nclr.nColors - startIndex);
						for (int i = 0; i < nCopyColors; i++) {
							data->nclr.colors[i + startIndex] = ColorConvertToDS(colors[i]);
						}
						free(colors);

						free(path);
						break;
					}
					case ID_MENU_VERIFYCOLOR:
					{
						int index = data->contextHoverX + data->contextHoverY * 16;
						int palette = index >> data->nclr.nBits;
						
						HWND hWndMain = getMainWindow(hWnd);
						NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
						HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
						if (hWndNcgrViewer) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(hWndNcgrViewer, 0);
							ncgrViewerData->verifyColor = index;
							ncgrViewerData->verifyFrames = 10;
							SetTimer(hWndNcgrViewer, 1, 100, NULL);
						}
						EnumAllEditors(hWndMain, FILE_TYPE_SCREEN, ValidateColorsNscrProc, (void *) index);
						break;
					}
					case ID_MENU_CREATE:
					{
						int index = data->contextHoverX + data->contextHoverY;
						int palette = index >> data->nclr.nBits;

						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						HWND hWndPaletteDialog = CreateWindow(L"PaletteGeneratorClass", L"Generate Palette", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX), CW_USEDEFAULT, CW_USEDEFAULT, 200, 200, hWndMain, NULL, NULL, NULL);
						SendMessage(hWndPaletteDialog, NV_INITIALIZE, 0, (LPARAM) data);
						ShowWindow(hWndPaletteDialog, SW_SHOW);
						SetActiveWindow(hWndPaletteDialog);
						SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
						break;
					}
					case ID_MENU_ANIMATEPALETTE:
					{
						data->tempPalette = (COLOR *) calloc(data->nclr.nColors, sizeof(COLOR));
						memcpy(data->tempPalette, data->nclr.colors, data->nclr.nColors * sizeof(COLOR));

						PAL_OP *palOp = &data->palOp;
						palOp->srcIndex = data->contextHoverY * 16;
						data->palOpDialog = 1;
						int n = SelectPaletteOperation(palOp);
						data->palOpDialog = 0;

						memcpy(data->nclr.colors, data->tempPalette, data->nclr.nColors * sizeof(COLOR));
						free(data->tempPalette);
						data->tempPalette = NULL;

						//apply modifier
						if (n) {
							COLOR *cpy = (COLOR *) calloc(data->nclr.nColors, sizeof(COLOR));
							PalopRunOperation(data->nclr.colors, cpy, data->nclr.nColors, palOp);
							memcpy(data->nclr.colors, cpy, data->nclr.nColors * sizeof(COLOR));
							free(cpy);
						}
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			fileFree((OBJECT_HEADER *) &data->nclr);
			if (data->nclr.idxTable != NULL) free(data->nclr.idxTable);
			free(data);

			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			nitroPaintStruct->hWndNclrViewer = NULL;
			InvalidateAllEditors(hWndMain, FILE_TYPE_CHAR);
			InvalidateAllEditors(hWndMain, FILE_TYPE_CELL);
			InvalidateAllEditors(hWndMain, FILE_TYPE_SCREEN);
			break;
		}
		case NV_GETTYPE:
			return FILE_TYPE_PALETTE;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK PaletteGeneratorDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NCLRVIEWERDATA *data = (NCLRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case WM_CREATE:
		{
			SetWindowSize(hWnd, 355, 214);	
			break;
		}
		case NV_INITIALIZE:
		{
			data = (NCLRVIEWERDATA *) lParam;
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			CreateWindow(L"STATIC", L"Bitmap:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndFileInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 120, 10, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndBrowse = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 320, 10, 25, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Colors:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndColors = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"16", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 120, 37, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Reserve first:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 64, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndReserve = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 64, 22, 22, hWnd, NULL, NULL, NULL);
			SendMessage(data->hWndReserve, BM_SETCHECK, 1, 0);

			//palette options
			CreateWindow(L"STATIC", L"Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 96, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Lightness", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, 120, 96, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 170 + 150, 96, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, 170, 96, 150, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 123, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Green", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, 120, 123, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Red", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 170 + 150, 123, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndColorBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, 170, 123, 150, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Enhance colors:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 150, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndEnhanceColors = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 150, 22, 22, hWnd, NULL, NULL, NULL);

			data->hWndGenerate = CreateWindow(L"BUTTON", L"Generate", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 120, 182, 100, 22, hWnd, NULL, NULL, NULL);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));

			SendMessage(data->hWndBalance, TBM_SETRANGE, TRUE, 0 | (40 << 16));
			SendMessage(data->hWndColorBalance, TBM_SETRANGE, TRUE, 0 | (40 << 16));
			SendMessage(data->hWndBalance, TBM_SETPOS, TRUE, 20);
			SendMessage(data->hWndColorBalance, TBM_SETPOS, TRUE, 20);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			WORD notif = HIWORD(wParam);
			if (notif == BN_CLICKED && hWndControl == data->hWndBrowse) {
				LPWSTR path = openFilesDialog(hWnd, L"Select Bitmap", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
				if (path != NULL) {
					SendMessage(data->hWndFileInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
					free(path);
				}
			} else if (notif == BN_CLICKED && hWndControl == data->hWndGenerate) {
				int width, height;
				WCHAR bf[MAX_PATH + 1];
				LPWSTR paths = (LPWSTR) calloc((MAX_PATH + 1) * 32 + 1, sizeof(WCHAR));
				SendMessage(data->hWndFileInput, WM_GETTEXT, (MAX_PATH + 1) * 32 + 1, (LPARAM) paths);
				int nPaths = getPathCount(paths);
				COLOR32 *bits = gdipReadImage(bf, &width, &height);

				SendMessage(data->hWndColors, WM_GETTEXT, MAX_PATH, (LPARAM) bf);
				int nColors = _wtol(bf);
				int balance = SendMessage(data->hWndBalance, TBM_GETPOS, 0, 0);
				int colorBalance = SendMessage(data->hWndColorBalance, TBM_GETPOS, 0, 0);
				if (nColors > 256) nColors = 256;

				BOOL enhanceColors = SendMessage(data->hWndEnhanceColors, BM_GETCHECK, 0, 0) == BST_CHECKED;
				BOOL reserveFirst = SendMessage(data->hWndReserve, BM_GETCHECK, 0, 0) == BST_CHECKED;

				//create palette copy
				int nTotalColors = data->nclr.nColors;
				int index = data->contextHoverX + data->contextHoverY * 16;
				COLOR32 *paletteCopy = (COLOR32 *) calloc(nColors, sizeof(COLOR32));

				//compute histogram
				REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
				initReduction(reduction, balance, colorBalance, 15, enhanceColors, nColors - reserveFirst);
				for (int i = 0; i < nPaths; i++) {
					getPathFromPaths(paths, i, bf);
					COLOR32 *bits = gdipReadImage(bf, &width, &height);
					computeHistogram(reduction, bits, width, height);
					free(bits);
				}
				flattenHistogram(reduction);

				//create and write palette
				optimizePalette(reduction);
				for (int i = 0; i < nColors - reserveFirst; i++) {
					uint8_t *c8 = &reduction->paletteRgb[i][0];
					COLOR32 c = c8[0] | (c8[1] << 8) | (c8[2] << 16);
					(paletteCopy + reserveFirst)[i] = c;
				}
				qsort(paletteCopy + reserveFirst, nColors - reserveFirst, sizeof(COLOR32), lightnessCompare);
				destroyReduction(reduction);
				free(reduction);

				//write back
				for (int i = 0; i < nColors; i++) {
					if (i + index >= nTotalColors) break;
					data->nclr.colors[i + index] = ColorConvertToDS(paletteCopy[i]);
				}
				free(paths);
				free(paletteCopy);
				free(bits);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
				InvalidateRect((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), NULL, FALSE);
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

VOID RegisterPaletteGenerationClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"PaletteGeneratorClass";
	wcex.lpfnWndProc = PaletteGeneratorDialogProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterNclrViewerClass(VOID) {
	RegisterPaletteGenerationClass();
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NclrViewerClass";
	wcex.lpfnWndProc = NclrViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

HWND CreateNclrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4;
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NclrViewerClass", L"Palette Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		if (!SendMessage(h, NV_INITIALIZE, (WPARAM) path, 0)) {
			DestroyWindow(h);
			MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
			return NULL;
		}
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NclrViewerClass", L"Palette Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, 0);
	return h;
}

HWND CreateNclrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCLR *nclr) {
	if (width != CW_USEDEFAULT && height != CW_USEDEFAULT) {
		RECT rc = { 0 };
		rc.right = width;
		rc.bottom = height;
		AdjustWindowRect(&rc, WS_CAPTION | WS_THICKFRAME | WS_SYSMENU, FALSE);
		width = rc.right - rc.left + 4; //+4 to account for WS_EX_CLIENTEDGE
		height = rc.bottom - rc.top + 4;
		HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NclrViewerClass", L"Palette Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
		SendMessage(h, NV_INITIALIZE_IMMEDIATE, (WPARAM) nclr, 0);
		return h;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NclrViewerClass", L"Palette Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE_IMMEDIATE, (WPARAM) nclr, 0);
	return h;
}
