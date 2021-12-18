#include <Windows.h>
#include <CommCtrl.h>
#include <stdio.h>

#include "nsbtxviewer.h"
#include "nitropaint.h"
#include "resource.h"

extern HICON g_appIcon;

#define NV_INITIALIZE (WM_USER+1)

extern int max16Len(char *str);

HBITMAP renderTexture(TEXELS *texture, PALETTE *palette, int zoom) {
	int width = TEXW(texture->texImageParam);
	int height = TEXH(texture->texImageParam);
	DWORD *px = (DWORD *) calloc(width * zoom * height * zoom, 4);
	convertTexture(px, texture, palette, 0);

	//perform alpha blending
	int scaleWidth = width * zoom, scaleHeight = height * zoom;
	for (int yDest = scaleHeight - 1; yDest >= 0; yDest--) {
		int y = yDest / zoom;
		for (int xDest = scaleWidth - 1; xDest >= 0; xDest--) {
			int x = xDest / zoom;

			DWORD pixel = px[x + y * width];
			int a = pixel >> 24;
			if (a != 255) {
				int s = ((xDest >> 3) ^ (yDest >> 3)) & 1;
				int shades[] = {255, 192};
				int shade = shades[s];
				int r = pixel & 0xFF;
				int g = (pixel >> 8) & 0xFF;
				int b = (pixel >> 16) & 0xFF;

				r = (r * a + shade * (255 - a)) / 255;
				g = (g * a + shade * (255 - a)) / 255;
				b = (b * a + shade * (255 - a)) / 255;

				px[xDest + yDest * scaleWidth] = r | (g << 8) | (b << 16) | 0xFF000000;
			} else {
				px[xDest + yDest * scaleWidth] = pixel;
			}
		}
	}
	
	HBITMAP hBitmap = CreateBitmap(width * zoom, height * zoom, 1, 32, px);
	free(px);
	return hBitmap;
}

LRESULT WINAPI NsbtxViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSBTXVIEWERDATA *data = (NSBTXVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (NSBTXVIEWERDATA *) calloc(1, sizeof(NSBTXVIEWERDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			data->hWndTextureSelect = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, L"", WS_VISIBLE | WS_CHILD | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | LBS_NOTIFY, 0, 0, 150, 100, hWnd, NULL, NULL, NULL);
			data->hWndPaletteSelect = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, L"", WS_VISIBLE | WS_CHILD | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | LBS_NOTIFY, 0, 100, 150, 100, hWnd, NULL, NULL, NULL);
			data->hWndReplaceButton = CreateWindow(L"BUTTON", L"Replace", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 150, 300 - 22, 100, 22, hWnd, NULL, NULL, NULL);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
			data->scale = 1;
			return 1;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->nsbtx, (NSBTX *) lParam, sizeof(NSBTX));
			WCHAR titleBuffer[MAX_PATH + 15];
			if (!g_configuration.fullPaths) path = GetFileName(path);
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - NSBTX Viewer", 32);
			SetWindowText(hWnd, titleBuffer);

			WCHAR buffer[17];
			for (int i = 0; i < data->nsbtx.nTextures; i++) {
				char *name = data->nsbtx.textures[i].name;
				int len = max16Len(name);
				for (int j = 0; j < len; j++) {
					buffer[j] = name[j];
				}
				buffer[len] = 0;
				SendMessage(data->hWndTextureSelect, LB_ADDSTRING, 0, (LPARAM) buffer);
			}
			for (int i = 0; i < data->nsbtx.nPalettes; i++) {
				char *name = data->nsbtx.palettes[i].name;
				int len = max16Len(name);
				for (int j = 0; j < len; j++) {
					buffer[j] = name[j];
				}
				buffer[len] = 0;
				SendMessage(data->hWndPaletteSelect, LB_ADDSTRING, 0, (LPARAM) buffer);
			}
			SendMessage(data->hWndTextureSelect, LB_SETCURSEL, 0, 0);
			SendMessage(data->hWndPaletteSelect, LB_SETCURSEL, 0, 0);
			return 1;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			int t = SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0);
			int p = SendMessage(data->hWndPaletteSelect, LB_GETCURSEL, 0, 0);
			TEXELS *texture = data->nsbtx.textures + t;
			PALETTE *palette = data->nsbtx.palettes + p;
			HBITMAP hBitmap = renderTexture(texture, palette, data->scale);

			HDC hCompat = CreateCompatibleDC(hDC);
			SelectObject(hCompat, hBitmap);
			BitBlt(hDC, 150, 22, TEXW(texture->texImageParam) * data->scale, TEXH(texture->texImageParam) * data->scale, hCompat, 0, 0, SRCCOPY);

			char bf[64];
			SelectObject(hDC, GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(hDC, TRANSPARENT);
			if (FORMAT(texture->texImageParam) == CT_DIRECT) {
				sprintf(bf, "%s texture, %dx%d", stringFromFormat(FORMAT(texture->texImageParam)), TEXW(texture->texImageParam), TEXH(texture->texImageParam));
			} else {
				sprintf(bf, "%s texture, %dx%d; palette: %d colors", stringFromFormat(FORMAT(texture->texImageParam)), TEXW(texture->texImageParam), TEXH(texture->texImageParam), palette->nColors);
			}
			RECT rcText;
			rcText.left = 155;
			rcText.top = 0;
			rcText.right = 350;
			rcText.bottom = 22;
			DrawTextA(hDC, bf, -1, &rcText, DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX | DT_VCENTER);

			EndPaint(hWnd, &ps);
			DeleteObject(hCompat);
			DeleteObject(hBitmap);
			return 0;
		}
		case WM_COMMAND:
		{
			if (lParam == 0 && HIWORD(wParam) == 0) {

				switch (LOWORD(wParam)) {
					case ID_FILE_EXPORT:
					{
						LPWSTR location = saveFileDialog(hWnd, L"Save Texture", L"TGA Files (*.tga)\0*.tga\0All Files\0*.*\0", L"tga");
						if (!location) break;

						writeNitroTGA(location, data->nsbtx.textures + SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0),
									  data->nsbtx.palettes + SendMessage(data->hWndPaletteSelect, LB_GETCURSEL, 0, 0));

						free(location);
						break;
					}
					case ID_FILE_SAVE:
						nsbtxWriteFile(&data->nsbtx, data->szOpenFile);
						break;
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

						InvalidateRect(hWnd, NULL, TRUE);
						break;
					}
				}
			}
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				int m = HIWORD(wParam);
				if (m == LBN_SELCHANGE) {
					int currentTexture = SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0);
					if (hWndControl == data->hWndTextureSelect || hWndControl == data->hWndPaletteSelect) {
						InvalidateRect(hWnd, NULL, TRUE);
					}
				} else if (m == BN_CLICKED) {
					if (hWndControl == data->hWndReplaceButton) {
						LPWSTR path = openFileDialog(hWnd, L"Open Nitro TGA", L"TGA Files (*.tga)\0*.tga\0All Files\0*.*\0", L"tga");
						if (!path) break;

						TEXELS texels;
						PALETTE palette;
						int s = nitroTgaRead(path, &texels, &palette);
						if (s) {
							MessageBox(hWnd, L"Invalid Nitro TGA.", L"Invalid Nitro TGA", MB_ICONERROR);
						} else {
							int selectedPalette = SendMessage(data->hWndPaletteSelect, LB_GETCURSEL, 0, 0);
							int selectedTexture = SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0);
							TEXELS *destTex = data->nsbtx.textures + selectedTexture;
							PALETTE *destPal = data->nsbtx.palettes + selectedPalette;
							int oldTexImageParam = destTex->texImageParam;
							memcpy(texels.name, destTex->name, 16);
							memcpy(palette.name, destPal->name, 16);
							if (destTex->cmp) free(destTex->cmp);
							memcpy(destTex, &texels, sizeof(TEXELS));
							if (FORMAT(texels.texImageParam) != CT_DIRECT) {
								free(destPal->pal);
								memcpy(destPal, &palette, sizeof(PALETTE));
							}
							//keep flipping, repeat, and transfomation
							int mask = 0xC00F0000;
							destTex->texImageParam = (oldTexImageParam & mask) | (destTex->texImageParam & ~mask);
							InvalidateRect(hWnd, NULL, TRUE);
						}

						free(path);
					}
				}
			}
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			int height = rcClient.bottom;
			MoveWindow(data->hWndTextureSelect, 0, 0, 150, height / 2, TRUE);
			MoveWindow(data->hWndPaletteSelect, 0, height / 2, 150, height / 2, TRUE);
			MoveWindow(data->hWndReplaceButton, 150, height - 22, 100, 22, TRUE);
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
			NSBTX *nsbtx = &data->nsbtx;
			for (int i = 0; i < nsbtx->nTextures; i++) {
				free(nsbtx->textures[i].texel);
				if (nsbtx->textures[i].cmp) free(nsbtx->textures[i].cmp);
			}
			for (int i = 0; i < nsbtx->nPalettes; i++) {
				free(nsbtx->palettes[i].pal);
			}
			if (nsbtx->mdl0 != NULL) {
				free(nsbtx->mdl0);
			}
			free(nsbtx->textureDictionary.entry.data);
			free(nsbtx->paletteDictionary.entry.data);
			free(nsbtx->palettes);
			free(nsbtx->textures);
			free(data);
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNsbtxViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	//wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NsbtxViewerClass";
	wcex.lpfnWndProc = NsbtxViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

HWND CreateNsbtxViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NSBTX nsbtx;
	int n = nsbtxReadFile(&nsbtx, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NsbtxViewerClass", L"NSBTX Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nsbtx);
	return h;
}