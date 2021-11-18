#include "textureeditor.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "palette.h"
#include "colorchooser.h"
#include "resource.h"
#include "tiler.h"
#include "gdip.h"
#include "texconv.h"
#include "nclr.h"

#include <commctrl.h>
#include <math.h>

extern HICON g_appIcon;

#define NV_INITIALIZE (WM_USER+1)
#define NV_RECALCULATE (WM_USER+2)
#define NV_SETPATH (WM_USER+3)

HWND CreateTexturePaletteEditor(int x, int y, int width, int height, HWND hWndParent, TEXTUREEDITORDATA *data);

int getTexelVramSize(int texImageParam) {
	int w = TEXW(texImageParam);
	int h = TEXH(texImageParam);
	int fmt = FORMAT(texImageParam);

	int bpps[] = { 0, 8, 2, 4, 8, 3, 8, 16 };
	return bpps[fmt] * w * h / 8;
}

void UpdatePaletteLabel(HWND hWnd) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);

	WCHAR bf[32];
	int len;
	if (data->textureData.palette.nColors) {
		len = wsprintfW(bf, L"Palette: %d colors", data->textureData.palette.nColors);
		data->hasPalette = TRUE;
	} else {
		len = wsprintfW(bf, L"No palette");
		data->hasPalette = FALSE;
	}
	SendMessage(data->hWndPaletteLabel, WM_SETTEXT, len, (LPARAM) bf);

	len = wsprintfW(bf, L"Format: %S", stringFromFormat(FORMAT(data->textureData.texels.texImageParam)));
	SendMessage(data->hWndFormatLabel, WM_SETTEXT, len, (LPARAM) bf);

	int nColors = countColors(data->px, data->width * data->height);
	len = wsprintfW(bf, L"Colors: %d", nColors);
	SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) bf);

	int texelVram = getTexelVramSize(data->textureData.texels.texImageParam);
	int paletteVram = data->textureData.palette.nColors * 2;
	
	//this code is ugly due to being unable to just use %.2f
	len = wsprintfW(bf, L"Texel: %d.%d%dKB", texelVram / 1024, (texelVram * 10 / 1024) % 10,
		((texelVram * 100 + 512) / 1024) % 10);
	SendMessage(data->hWndTexelVram, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"Palette: %d.%d%dKB", paletteVram / 1024, (paletteVram * 10 / 1024) % 10,
		((paletteVram * 100 + 512) / 1024) % 10);
	SendMessage(data->hWndPaletteVram, WM_SETTEXT, len, (LPARAM) bf);
}

LRESULT CALLBACK TextureEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (TEXTUREEDITORDATA *) calloc(1, sizeof(TEXTUREEDITORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			/*
			+----------------------+  Format: none
			|                      |  [Convert to...]
			|                      |  
			| Texture Preview      |  Palette: <x> colors
			|                      |  [Edit Palette]
			|                      |
			+----------------------+
			*/
			data->scale = 1;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;
			data->hWnd = hWnd;
			data->hWndPreview = CreateWindow(L"TexturePreviewClass", L"Texture Preview", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 300, 300, hWnd, NULL, NULL, NULL);
			data->hWndFormatLabel = CreateWindow(L"STATIC", L"Format: none", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 10, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndConvert = CreateWindow(L"BUTTON", L"Convert To...", WS_VISIBLE | WS_CHILD, 310, 37, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteLabel = CreateWindow(L"STATIC", L"No palette", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 69, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndEditPalette = CreateWindow(L"BUTTON", L"Edit Palette", WS_VISIBLE | WS_CHILD, 310, 96, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndUniqueColors = CreateWindow(L"STATIC", L"Colors: 0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 128, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndTexelVram = CreateWindow(L"STATIC", L"Texel: 0KB", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 155, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteVram = CreateWindow(L"STATIC", L"Palette: 0KB", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 182, 110, 22, hWnd, NULL, NULL, NULL);
			break;
		}
		case WM_PAINT:
		{
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			MoveWindow(data->hWndPreview, 0, 0, rcClient.right - 120, rcClient.bottom, TRUE);
			MoveWindow(data->hWndFormatLabel, rcClient.right - 110, 10, 100, 22, TRUE);
			MoveWindow(data->hWndConvert, rcClient.right - 110, 37, 100, 22, TRUE);
			MoveWindow(data->hWndPaletteLabel, rcClient.right - 110, 69, 100, 22, TRUE);
			MoveWindow(data->hWndEditPalette, rcClient.right - 110, 96, 100, 22, TRUE);
			MoveWindow(data->hWndUniqueColors, rcClient.right - 110, 128, 100, 22, TRUE);
			MoveWindow(data->hWndTexelVram, rcClient.right - 110, 155, 100, 22, TRUE);
			MoveWindow(data->hWndPaletteVram, rcClient.right - 110, 182, 110, 22, TRUE);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
		case NV_INITIALIZE:
		{
			data->width = wParam & 0xFFFF;
			data->height = (wParam >> 16) & 0xFFFF;
			data->px = (DWORD *) lParam;
			data->format = 0;
			data->hasPalette = FALSE;
			data->frameData.contentWidth = data->width;
			data->frameData.contentHeight = data->height;

			//check: is it a Nitro TGA?
			if (!nitroTgaRead(data->szInitialFile, &data->textureData.texels, &data->textureData.palette)) {
				memcpy(data->szCurrentFile, data->szInitialFile, 2 + 2 * wcslen(data->szInitialFile));
				data->format = FORMAT(data->textureData.texels.texImageParam);
				data->hasPalette = (data->format != CT_DIRECT && data->format != 0);
				data->isNitro = 1;
				convertTexture(data->px, &data->textureData.texels, &data->textureData.palette, 0);
				for (int i = 0; i < data->width * data->height; i++) {
					DWORD col = data->px[i];
					data->px[i] = REVERSE(col);
				}
				UpdatePaletteLabel(hWnd);
			}

			WCHAR buffer[16];
			int nColors = countColors(data->px, data->width * data->height);
			int len = wsprintfW(buffer, L"Colors: %d", nColors);
			SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) buffer);

			SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		}
		case NV_SETPATH:
		{
			memcpy(data->szInitialFile, (LPWSTR) lParam, 2 * wParam + 2);
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
		case WM_COMMAND:
		{
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->hWndEditPalette) {
					int format = FORMAT(data->textureData.texels.texImageParam);
					if (format == CT_DIRECT || format == 0) {
						MessageBox(hWnd, L"No palette for this texture.", L"No palette", MB_ICONERROR);
					} else {
						HWND hWndMdi = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
						if (data->hWndPaletteEditor == NULL) {
							data->hWndPaletteEditor = CreateTexturePaletteEditor(CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, hWndMdi, data);
						} else {
							SendMessage(hWndMdi, WM_MDIACTIVATE, (WPARAM) data->hWndPaletteEditor, 0);
						}
					}
				} else if (hWndControl == data->hWndConvert) {
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					data->hWndConvertDialog = CreateWindow(L"ConvertDialogClass", L"Convert Texture",
														   WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_THICKFRAME, 
														   CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
					SetWindowLongPtr(data->hWndConvertDialog, 0, (LONG_PTR) data);
					ShowWindow(data->hWndConvertDialog, SW_SHOW);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
					SendMessage(data->hWndConvertDialog, NV_INITIALIZE, 0, 0);
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
						SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
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

						SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
						InvalidateRect(hWnd, NULL, FALSE);
						RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
						break;
					}
					case ID_FILE_SAVE:
					{
						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						//if not in any format, it cannot be saved.
						if (!data->isNitro) {
							MessageBox(hWnd, L"Texture must be converted.", L"Not converted", MB_ICONERROR);
							break;
						}
						if (*data->szCurrentFile == L'\0') {
							//browse for file
							LPWSTR path = saveFileDialog(hWndMain, L"Save Texture", L"Nitro TGA Files (*.tga)\0*.tga\0All Files\0*.*\0\0", L"tga");
							if (!path) break;
							memcpy(data->szCurrentFile, path, 2 * wcslen(path) + 2);
							free(path);
						}
						writeNitroTGA(data->szCurrentFile, &data->textureData.texels, &data->textureData.palette);
						break;
					}
					case ID_FILE_EXPORT:
					{
						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						//if not in any format, it cannot be exported.
						if (!data->isNitro) {
							MessageBox(hWnd, L"Texture must be converted.", L"Not converted", MB_ICONERROR);
							break;
						}

						LPWSTR ntftPath = saveFileDialog(hWndMain, L"Save NTFT", L"NTFT Files (*.ntft)\0All Files\0*.*\0\0", L"ntft");
						if (ntftPath == NULL) break;

						LPWSTR ntfiPath = NULL;
						if (FORMAT(data->textureData.texels.texImageParam) == CT_4x4) {
							ntfiPath = saveFileDialog(hWndMain, L"Save NTFI", L"NTFI Files (*.ntfi)\0All Files\0*.*\0\0", L"ntfi");
							if (ntfiPath == NULL) {
								free(ntftPath);
								break;
							}
						}

						DWORD dwWritten;
						int texImageParam = data->textureData.texels.texImageParam;
						int texelSize = getTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
						HANDLE hFile = CreateFile(ntftPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(hFile, data->textureData.texels.texel, texelSize, &dwWritten, NULL);
						CloseHandle(hFile);
						free(ntftPath);

						if (ntfiPath != NULL) {
							hFile = CreateFile(ntfiPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
							WriteFile(hFile, data->textureData.texels.cmp, texelSize / 2, &dwWritten, NULL);
							CloseHandle(hFile);
							free(ntfiPath);
						}
						break;
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			if (data->hWndPaletteEditor) DestroyWindow(data->hWndPaletteEditor);
			if (data->hWndTileEditor) DestroyWindow(data->hWndTileEditor);
			SetWindowLongPtr(data->hWndPreview, 0, 0);
			free(data->px);
			if (data->textureData.palette.pal) free(data->textureData.palette.pal);
			if (data->textureData.texels.texel) free(data->textureData.texels.texel);
			if (data->textureData.texels.cmp) free(data->textureData.texels.cmp);
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK TexturePreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), 0);
	int contentWidth = 0, contentHeight = 0;
	if (data) {
		contentWidth = getDimension2(data->width / 4, data->showBorders, data->scale, 4);
		contentHeight = getDimension2(data->height / 4, data->showBorders, data->scale, 4);
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
			HDC hDC = BeginPaint(hWnd, &ps);

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);

			HDC hOffDC = CreateCompatibleDC(hDC);
			HBITMAP hOffBitmap = CreateCompatibleBitmap(hDC, rcClient.right, rcClient.bottom);
			SelectObject(hOffDC, hOffBitmap);
			SelectObject(hOffDC, GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1));
			SelectObject(hOffDC, GetStockObject(NULL_PEN));
			Rectangle(hOffDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			
			int width, height;
			HBITMAP hBitmap = CreateTileBitmap2(data->px, data->width, data->height, data->hoverX, data->hoverY, &width, &height, data->scale, data->showBorders, 4, TRUE, TRUE);
			HDC hCompat = CreateCompatibleDC(hDC);
			SelectObject(hCompat, hBitmap);
			BitBlt(hOffDC, -horiz.nPos, -vert.nPos, width, height, hCompat, 0, 0, SRCCOPY);
			DeleteObject(hCompat);
			DeleteObject(hBitmap);

			BitBlt(hDC, 0, 0, rcClient.right, rcClient.bottom, hOffDC, 0, 0, SRCCOPY);
			DeleteObject(hOffDC);
			DeleteObject(hOffBitmap);

			EndPaint(hWnd, &ps);
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
					int cellWidth = 4 * data->scale + 1;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				} else {
					int cellWidth = 4 * data->scale;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				}
				hoverX = x, hoverY = y;
				hoverIndex = hoverX + hoverY * (data->width / 4);
			}
			if (msg == WM_MOUSELEAVE) {
				hoverX = -1, hoverY = -1, hoverIndex = -1;
			}
			data->hoverX = hoverX;
			data->hoverY = hoverY;
			data->hoverIndex = hoverIndex;
			if (data->hoverIndex != oldHovered) {
				HWND hWndViewer = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
				HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWndViewer, GWL_HWNDPARENT), GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLong(hWndMain, 0);
				InvalidateRect(hWndViewer, NULL, FALSE);
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
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int isTranslucent(DWORD *px, int nWidth, int nHeight) {
	for (int i = 0; i < nWidth * nHeight; i++) {
		int a = px[i] >> 24;
		if (a && a != 255) return 1;
	}
	return 0;
}

int guessFormat(DWORD *px, int nWidth, int nHeight) {
	//Guess a good format for the data. Default to 4x4.
	int fmt = CT_4x4;
	
	//if the texture is 1024x1024, do not choose 4x4.
	if (nWidth * nHeight == 1024 * 1024) fmt = CT_256COLOR;

	//is there translucency?
	if (isTranslucent(px, nWidth, nHeight)) {
		//then choose a3i5 or a5i3. Do this by using color count.
		int colorCount = countColors(px, nWidth * nHeight);
		if (colorCount < 16) {
			//colors < 16, choose a5i3.
			fmt = CT_A5I3;
		} else {
			//otherwise, choose a3i5.
			fmt = CT_A3I5;
		}
	} else {
		//weigh the other format options for optimal size.
		int nColors = countColors(px, nWidth * nHeight);
		
		//if <= 4 colors, choose 4-color.
		if (nColors <= 4) {
			fmt = CT_4COLOR;
		} else {
			//weigh 16-color, 256-color, and 4x4. 
			//take the number of pixels per color. 
			int pixelsPerColor = nWidth * nHeight / nColors;
			if (pixelsPerColor >= 2 && !(nWidth * nHeight >= 1024 * 512)) {
				fmt = CT_4x4;
			} else {
				//otherwise, 4x4 probably isn't a good option.
				if (nColors < 32) {
					fmt = CT_16COLOR;
				} else {
					fmt = CT_256COLOR;
				}
			}
		}
	}

	return fmt;
}
void createPaletteName(WCHAR *buffer, WCHAR *file) {
	//find the last \ or /
	int index = -1;
	unsigned int i;
	for (i = 0; i < wcslen(file); i++) {
		if (file[i] == L'\\' || file[i] == L'/') index = i;
	}
	file += index + 1;
	//copy up to 12 characters of the file name
	for (i = 0; i < 12; i++) {
		WCHAR c = file[i];
		if (c == L'\0' || c == L'.') break;
		buffer[i] = c;
	}
	//suffix _pl
	memcpy(buffer + i, L"_pl", 6);
}

float mylog2(float d) { //UGLY!
	float ans;
	_asm {
		fld1
		fld dword ptr [d]
		fyl2x
		fstp dword ptr [ans]
	}
	return ans;
}
#define log2 mylog2

int chooseColorCount(int bWidth, int bHeight) {
	int colors = (int) (250.0f * (0.5f * log2((float) bWidth * bHeight) - 5.0f) + 0.5f);
	if (sqrt(bWidth * bHeight) < 83.0f) {
		colors = (int) (4.345466990625f * sqrt(bWidth * bHeight) - 16.5098578365f);
	}
	if (colors & 7) {
		colors += 8 - (colors & 7);
	}
	return colors;
}

void setStyle(HWND hWnd, BOOL set, DWORD style) {
	if (set) {
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | style);
	} else {
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) & ~style);
	}
}

void updateConvertDialog(TEXTUREEDITORDATA *data) {
	HWND hWndFormat = data->hWndFormat;
	int sel = SendMessage(hWndFormat, CB_GETCURSEL, 0, 0);
	int fixedPalette = SendMessage(data->hWndFixedPalette, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int fmt = sel + 1;
	//some things are only applicable to certain formats!
	BOOL disables[] = {TRUE, FALSE, TRUE, FALSE, FALSE, FALSE};
	switch (fmt) {
		case CT_A3I5:
		case CT_A5I3:
		{
			disables[0] = FALSE;
			disables[1] = FALSE;
			disables[2] = TRUE;
			disables[3] = FALSE;
			break;
		}
		case CT_4x4:
		{
			disables[0] = TRUE;
			disables[1] = TRUE;
			disables[2] = FALSE;
			disables[3] = FALSE;
			break;
		}
		case CT_DIRECT:
		{
			disables[0] = TRUE;
			disables[1] = FALSE;
			disables[2] = TRUE;
			disables[3] = TRUE;
			disables[4] = TRUE;
			disables[5] = TRUE;
			break;
		}
	}
	if (!disables[4]) {
		if (!fixedPalette) disables[5] = TRUE;
	}
	if (fixedPalette && !disables[5]) {
		disables[2] = TRUE;
	}
	setStyle(data->hWndDitherAlpha, disables[0], WS_DISABLED);
	setStyle(data->hWndDither, disables[1], WS_DISABLED);
	setStyle(data->hWndColorEntries, disables[2], WS_DISABLED);
	setStyle(data->hWndOptimizationSlider, disables[2], WS_DISABLED);
	setStyle(data->hWndPaletteName, disables[3], WS_DISABLED);
	setStyle(data->hWndFixedPalette, disables[4], WS_DISABLED);
	setStyle(data->hWndPaletteInput, disables[5], WS_DISABLED);
	setStyle(data->hWndPaletteBrowse, disables[5], WS_DISABLED);
	setStyle(data->hWndDiffuseAmount, !disables[2], WS_DISABLED);
	SetFocus(data->hWndConvertDialog);
	InvalidateRect(data->hWndConvertDialog, NULL, FALSE);
}

void conversionCallback(void *p) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) p;
	InvalidateRect(data->hWndPreview, NULL, FALSE);
	data->isNitro = TRUE;

	HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(data->hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	setStyle(hWndMain, FALSE, WS_DISABLED);
	SendMessage(data->hWndProgress, WM_CLOSE, 0, 0);
	SetForegroundWindow(hWndMain);
	data->hWndProgress = NULL;

	UpdatePaletteLabel(data->hWnd);
}

LRESULT CALLBACK ConvertDialogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case WM_CREATE:
		{
			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette name:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Fixed palette:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 64, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette file:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 91, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Dither:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 118, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Diffusion:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 172, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"%", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 120 + 50 + 5, 172, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Dither alpha:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 145, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"4x4 color entries:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 199, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"4x4 optimization:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 226, 100, 22, hWnd, NULL, NULL, NULL);

			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
			SetWindowSize(hWnd, 230, 285 + 27);
			break;
		}
		case NV_INITIALIZE:
		{
			data->hWndDoConvertButton = CreateWindow(L"BUTTON", L"Convert", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 120, 280, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndFormat = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 120, 10, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndPaletteName = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 120, 37, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndFixedPalette = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 64, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 120, 91, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteBrowse = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 120 + 75, 91, 25, 22, hWnd, NULL, NULL, NULL);
			data->hWndDither = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 118, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndDiffuseAmount = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 120, 172, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndDitherAlpha = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 120, 145, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndColorEntries = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"256", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 120, 199, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndOptimizationSlider = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD | TBS_NOTIFYBEFOREMOVE, 10, 253, 210, 22, hWnd, NULL, NULL, NULL);
			data->hWndOptimizationLabel = CreateWindow(L"STATIC", L"0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, 120, 226, 100, 22, hWnd, NULL, NULL, NULL);

			//populate the dropdown list
			WCHAR bf[16];
			int len;
			for (int i = 1; i <= CT_DIRECT; i++) {
				char *str = stringFromFormat(i);
				len = 0;
				while (*str) {
					bf[len] = *str;
					str++;
					len++;
				}
				bf[len] = L'\0';
				SendMessage(data->hWndFormat, CB_ADDSTRING, len, (LPARAM) bf);
			}

			int format = guessFormat(data->px, data->width, data->height);
			SendMessage(data->hWndFormat, CB_SETCURSEL, format - 1, 0);

			//pick default 4x4 color count
			int maxColors = chooseColorCount(data->width, data->height);
			len = wsprintfW(bf, L"%d", maxColors);
			SendMessage(data->hWndColorEntries, WM_SETTEXT, len, (LPARAM) bf);
			SendMessage(data->hWndOptimizationSlider, TBM_SETPOS, 0, 0);

			//fill palette name
			WCHAR pname[16] = { 0 };
			createPaletteName(pname, data->szInitialFile);
			SendMessage(data->hWndPaletteName, WM_SETTEXT, wcslen(pname), (LPARAM) pname);

			updateConvertDialog(data);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl) {
				int controlCode = HIWORD(wParam);
				if (hWndControl == data->hWndFormat && controlCode == LBN_SELCHANGE) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndFixedPalette && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndPaletteBrowse && controlCode == BN_CLICKED) {
					LPWSTR path = openFileDialog(hWnd, L"Select palette", L"Palette Files\0*.nclr;*ncl.bin;*.ntfp\0All Files\0*.*\0\0", L"");
					if (path != NULL) {
						SendMessage(data->hWndPaletteInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
						free(path);
					}
				} else if (hWndControl == data->hWndDoConvertButton && controlCode == BN_CLICKED) {
					int fmt = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0) + 1;
					BOOL fixedPalette = SendMessage(data->hWndFixedPalette, BM_GETCHECK, 0, 0) == BST_CHECKED;

					WCHAR path[MAX_PATH];
					SendMessage(data->hWndPaletteInput, WM_GETTEXT, MAX_PATH, (LPARAM) path);

					NCLR paletteFile = { 0 };
					if (fixedPalette) {
						int status = 1;
						if (path[0]) {
							status = nclrReadFile(&paletteFile, path);
						}
						if (status) {
							MessageBox(hWnd, L"Invalid palette file.", L"Invalid file", MB_ICONERROR);
							break;
						}
					}

					WCHAR bf[32];
					SendMessage(data->hWndColorEntries, WM_GETTEXT, 31, (LPARAM) bf);
					int colorEntries = _wtol(bf);
					SendMessage(data->hWndDiffuseAmount, WM_GETTEXT, 31, (LPARAM) bf);
					float diffuse = _wtol(bf) / 100.0f;
					int optimization = SendMessage(data->hWndOptimizationSlider, TBM_GETPOS, 0, 0);
					SendMessage(data->hWndPaletteName, WM_GETTEXT, 16, (LPARAM) bf);

					BOOL dither = SendMessage(data->hWndDither, BM_GETCHECK, 0, 0) == BST_CHECKED;
					BOOL ditherAlpha = SendMessage(data->hWndDitherAlpha, BM_GETCHECK, 0, 0) == BST_CHECKED;

					char mbpnam[16];
					for (int i = 0; i < 16; i++) {
						mbpnam[i] = (char) bf[i];
					}

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					data->hWndProgress = CreateWindow(L"CompressionProgress", L"Compressing", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX), 
													  CW_USEDEFAULT, CW_USEDEFAULT, 500, 150, hWndMain, NULL, NULL, NULL);
					ShowWindow(data->hWndProgress, SW_SHOW);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
					SetActiveWindow(data->hWndProgress);
					threadedConvert(data->px, data->width, data->height, fmt, dither, diffuse, ditherAlpha, fixedPalette ? paletteFile.nColors : colorEntries, 
									fixedPalette, paletteFile.colors, optimization, mbpnam, &data->textureData, conversionCallback, (void *) data);

					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
				}
			}
			break;
		}
		case WM_HSCROLL:
		{
			HWND hWndControl = (HWND) lParam;
			 if (hWndControl == data->hWndOptimizationSlider) {
				 WCHAR bf[8];
				 int len = wsprintfW(bf, L"%d", SendMessage(hWndControl, TBM_GETPOS, 0, 0));
				 SendMessage(data->hWndOptimizationLabel, WM_SETTEXT, len, (LPARAM) bf);
			 }
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndEditor = data->hWnd;
			HWND hWndMain = getMainWindow(hWndEditor);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			break;
		}
		case WM_DESTROY:
		{
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK CompressionProgressProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
		{
			CreateWindow(L"STATIC", L"Progress:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			HWND hWndProgress = CreateWindow(PROGRESS_CLASSW, L"", WS_VISIBLE | WS_CHILD, 10, 42, 400, 22, hWnd, NULL, NULL, NULL);
			SendMessage(hWndProgress, PBM_DELTAPOS, 1, 0);
			SetWindowLong(hWnd, 0, (LONG) hWndProgress);
			SetWindowSize(hWnd, 420, 74);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));

			SetTimer(hWnd, 1, 16, NULL);
			break;
		}
		case WM_TIMER:
		{
			if (_globFinal) {
				HWND hWndProgress = (HWND) GetWindowLong(hWnd, 0);
				SendMessage(hWndProgress, PBM_SETRANGE, 0, _globFinal << 16);
				SendMessage(hWndProgress, PBM_SETPOS, _globColors, 0);
			}
			break;
		}
		case WM_CLOSE:
		{
			if (_globFinished) {
				KillTimer(hWnd, 1);
				break;
			} else {
				return 0;
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

typedef struct {
	FRAMEDATA frameData;
	TEXTUREEDITORDATA *data;
	int hoverX;
	int hoverY;
	int hoverIndex;
	int contextHoverIndex;
}TEXTUREPALETTEEDITORDATA;

LRESULT CALLBACK TexturePaletteEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREPALETTEEDITORDATA *data = (TEXTUREPALETTEEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (TEXTUREPALETTEEDITORDATA *) calloc(1, sizeof(TEXTUREPALETTEEDITORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case NV_INITIALIZE:
		{
			data->data = (TEXTUREEDITORDATA *) lParam;
			data->frameData.contentWidth = 256;
			data->frameData.contentHeight = ((data->data->textureData.palette.nColors + 15) / 16) * 16;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentHeight;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			SetWindowSize(hWnd, 256 + GetSystemMetrics(SM_CXVSCROLL) + 4, 256 + 4);
			break;
		}
		case WM_NCHITTEST:
		{
			LRESULT hit = DefChildProc(hWnd, msg, wParam, lParam);
			if (hit == HTLEFT || hit == HTRIGHT) return HTBORDER;
			if (hit == HTTOPLEFT) return HTTOP;
			if (hit == HTBOTTOMLEFT) return HTBOTTOM;
			if (hit == HTTOPRIGHT) return HTTOP;
			if (hit == HTBOTTOMRIGHT) return HTBOTTOM;
			return hit;
		}
		case WM_PAINT:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO vert;
			vert.cbSize = sizeof(vert);
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &vert);

			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			HDC hOffDC = CreateCompatibleDC(hDC);
			HBITMAP hBitmap = CreateCompatibleBitmap(hDC, rcClient.right, rcClient.bottom);
			SelectObject(hOffDC, hBitmap);
			HBRUSH hBackground = GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1);
			SelectObject(hOffDC, hBackground);
			HPEN hBlackPen = SelectObject(hOffDC, GetStockObject(NULL_PEN));
			Rectangle(hOffDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			SelectObject(hOffDC, hBlackPen);

			HPEN hRowPen = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
			HPEN hWhitePen = GetStockObject(WHITE_PEN);

			COLOR *palette = data->data->textureData.palette.pal;
			int nColors = data->data->textureData.palette.nColors;
			for (int i = 0; i < nColors; i++) {
				int x = i & 0xF, y = i >> 4;

				if (y * 16 + 16 - vert.nPos >= 0 && y * 16 - vert.nPos < rcClient.bottom) {
					HBRUSH hbr = CreateSolidBrush(ColorConvertFromDS(palette[i]));
					SelectObject(hOffDC, hbr);
					if (x + y * 16 == data->hoverIndex) SelectObject(hOffDC, hWhitePen);
					else if (y == data->hoverY) SelectObject(hOffDC, hRowPen);
					else SelectObject(hOffDC, hBlackPen);
					Rectangle(hOffDC, x * 16, y * 16 - vert.nPos, x * 16 + 16, y * 16 + 16 - vert.nPos);
					DeleteObject(hbr);
				}
			}

			BitBlt(hDC, 0, 0, rcClient.right, rcClient.bottom, hOffDC, 0, 0, SRCCOPY);
			EndPaint(hWnd, &ps);

			DeleteObject(hOffDC);
			DeleteObject(hBitmap);
			DeleteObject(hBackground);
			DeleteObject(hRowPen);
			break;
		}
		case WM_ERASEBKGND:
		{
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

			int nRows = (data->data->textureData.palette.nColors + 15) / 16;

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

			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		{
			POINT pos;
			GetCursorPos(&pos);
			ScreenToClient(hWnd, &pos);

			SCROLLINFO vert;
			vert.cbSize = sizeof(vert);
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &vert);
			pos.y += vert.nPos;

			int x = pos.x / 16, y = pos.y / 16;
			if (x < 16) {
				int index = y * 16 + x;
				if (index < data->data->textureData.palette.nColors) {
					//if left click, open color editor dialogue.
					if (msg == WM_LBUTTONDOWN) {
						COLOR c = data->data->textureData.palette.pal[index];
						DWORD ex = ColorConvertFromDS(c);

						HWND hWndMain = getMainWindow(hWnd);
						CHOOSECOLOR cc = { 0 };
						cc.lStructSize = sizeof(cc);
						cc.hInstance = (HWND) (HINSTANCE) GetWindowLong(hWnd, GWL_HINSTANCE); //weird struct definition
						cc.hwndOwner = hWndMain;
						cc.rgbResult = ex;
						cc.lpCustColors = data->data->tmpCust;
						cc.Flags = 0x103;
						BOOL(__stdcall *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
						if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;
						if (ChooseColorFunction(&cc)) {
							DWORD result = cc.rgbResult;
							data->data->textureData.palette.pal[index] = ColorConvertToDS(result);
							InvalidateRect(hWnd, NULL, FALSE);

							convertTexture(data->data->px, &data->data->textureData.texels, &data->data->textureData.palette, 0);
							int param = data->data->textureData.texels.texImageParam;
							int width = TEXW(param);
							int height = 8 << ((param >> 23) & 7);
							//convertTexture outputs red and blue in the opposite order, so flip them here.
							for (int i = 0; i < width * height; i++) {
								DWORD p = data->data->px[i];
								data->data->px[i] = REVERSE(p);
							}
							InvalidateRect(data->data->hWnd, NULL, FALSE);
						}
					} else if (msg == WM_RBUTTONDOWN) {
						//otherwise open context menu
						data->contextHoverIndex = data->hoverIndex;
						HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 3);
						POINT mouse;
						GetCursorPos(&mouse);
						TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL);
					}
				}
			}
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl == NULL && HIWORD(wParam) == 0) {
				WORD notification = LOWORD(wParam);
				switch (notification) {
					case ID_PALETTEMENU_PASTE:
					{
						int offset = data->contextHoverIndex & (~15);

						OpenClipboard(hWnd);
						HANDLE hString = GetClipboardData(CF_TEXT);
						CloseClipboard();
						LPSTR palString = GlobalLock(hString);
						WORD length = (palString[0] & 0xF) | ((palString[1] & 0xF) << 4) | ((palString[2] & 0xF) << 8) | ((palString[3] & 0xF) << 12);

						int maxOffset = data->data->textureData.palette.nColors;

						int strOffset = 4;
						for (int i = 0; i < length; i++) {
							int location = offset + i;
							if (location >= maxOffset) break;
							int row = location >> 4;
							int col = 1 + (location & 0xF);
							DWORD color = 0;
							for (int j = 0; j < 8; j++) {
								color = (color << 4) | (palString[strOffset] & 0xF);
								strOffset++;
							}
							data->data->textureData.palette.pal[location] = ColorConvertToDS(color);
						}
						GlobalUnlock(hString);

						TEXTURE *texture = &data->data->textureData;
						convertTexture(data->data->px, &texture->texels, &texture->palette, 0);
						for (int i = 0; i < data->data->width * data->data->height; i++) {
							DWORD col = data->data->px[i];
							data->data->px[i] = REVERSE(col);
						}
						
						InvalidateRect(hWnd, NULL, FALSE);
						InvalidateRect(data->data->hWnd, NULL, FALSE);
						break;
					}
					case ID_PALETTEMENU_COPY:
					{
						int offset = data->contextHoverIndex & (~15);
						int length = 16;
						int maxOffset = data->data->textureData.palette.nColors;
						if (offset + length >= maxOffset) {
							length = maxOffset - offset;
							if (length < 0) length = 0;
						}
						HANDLE hString = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 4 + 8 * length + 1);
						LPSTR palString = (LPSTR) GlobalLock(hString);
						palString[0] = 0x20 + (length & 0xF);
						palString[1] = 0x20 + ((length >> 4) & 0xF);
						palString[2] = 0x20 + ((length >> 8) & 0xF);
						palString[3] = 0x20 + ((length >> 12) & 0xF);

						int strOffset = 4;
						for (int i = 0; i < length; i++) {
							int offs = i + offset;
							int row = offs >> 4;
							int col = (offs & 0xF) + 1;
							DWORD d = 0x00FFFFFF & (ColorConvertFromDS(data->data->textureData.palette.pal[offs]));

							for (int j = 0; j < 8; j++) {
								palString[strOffset] = 0x30 + ((d >> 28) & 0xF);
								d <<= 4;
								strOffset++;
							}
						}

						GlobalUnlock(hString);
						OpenClipboard(hWnd);
						EmptyClipboard();
						SetClipboardData(CF_TEXT, hString);
						CloseClipboard();
						break;
					}
					case ID_FILE_EXPORT:
					{
						//export as NTFP
						COLOR *colors = data->data->textureData.palette.pal;
						int nColors = data->data->textureData.palette.nColors;

						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(data->data->hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						LPWSTR path = saveFileDialog(hWndMain, L"Save NTFP", L"NTFP files (*.ntfp)\0*.ntfp\0All Files\0*.*\0\0", L"ntfp");
						if (path == NULL) break;

						DWORD dwWritten;
						HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(hFile, colors, nColors * 2, &dwWritten, NULL);
						CloseHandle(hFile);

						free(path);
						break;
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			data->data->hWndPaletteEditor = NULL;
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
	}
	if (data->data != NULL) {
		return DefChildProc(hWnd, msg, wParam, lParam);
	} else {
		return DefMDIChildProc(hWnd, msg, wParam, lParam);
	}

}

VOID RegisterTexturePreviewClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"TexturePreviewClass";
	wcex.lpfnWndProc = TexturePreviewWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterConvertDialogClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"ConvertDialogClass";
	wcex.lpfnWndProc = ConvertDialogWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterCompressionProgressClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"CompressionProgress";
	wcex.lpfnWndProc = CompressionProgressProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterTexturePaletteEditorClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"TexturePaletteEditorClass";
	wcex.lpfnWndProc = TexturePaletteEditorWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterTextureEditorClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"TextureEditorClass";
	wcex.lpfnWndProc = TextureEditorWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
	RegisterTexturePreviewClass();
	RegisterConvertDialogClass();
	RegisterCompressionProgressClass();
	RegisterTexturePaletteEditorClass();
}

HWND CreateTexturePaletteEditor(int x, int y, int width, int height, HWND hWndParent, TEXTUREEDITORDATA *data) {
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TexturePaletteEditorClass", L"Palette Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN | WS_VSCROLL, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
	return h;
}

HWND CreateTextureEditor(int x, int y, int width, int height, HWND hWndParent, LPWSTR path) {
	int bWidth, bHeight;
	DWORD *bits = gdipReadImage(path, &bWidth, &bHeight);
	if (bits == NULL) {
		MessageBox(hWndParent, L"An invalid image file was specified.", L"Invalid Image", MB_ICONERROR);
		return NULL;
	}
	if (!textureDimensionIsValid(bWidth) || !textureDimensionIsValid(bHeight)) {
		free(bits);
		MessageBox(hWndParent, L"Textures must have dimensions as powers of two greater than or equal to 8, and not exceeding 1024.", L"Invalid dimensions", MB_ICONERROR);
		return NULL;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TextureEditorClass", L"Texture Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_SETPATH, wcslen(path), (LPARAM) path);
	SendMessage(h, NV_INITIALIZE, bWidth | (bHeight << 16), (LPARAM) bits);
	return h;
}