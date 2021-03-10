#include <Windows.h>
#include <CommCtrl.h>

#include "nsbtxviewer.h"
#include "nitropaint.h"
#include "resource.h"

extern HICON g_appIcon;

#define NV_INITIALIZE (WM_USER+1)

extern int max16Len(char *str);

HBITMAP renderTexture(TEXELS *texture, PALETTE *palette) {
	int width = TEXS(texture->texImageParam);
	int height = TEXT(texture->texImageParam);
	DWORD *px = (DWORD *) calloc(width * height, 4);
	convertTexture(px, texture, palette, 0);

	//perform alpha blending
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			DWORD pixel = px[x + y * width];
			int a = pixel >> 24;
			if (a != 255) {
				int s = ((x >> 3) ^ (y >> 3)) & 1;
				int shades[] = {255, 192};
				int shade = shades[s];
				int r = pixel & 0xFF;
				int g = (pixel >> 8) & 0xFF;
				int b = (pixel >> 16) & 0xFF;

				r = (r * a + shade * (255 - a)) / 255;
				g = (g * a + shade * (255 - a)) / 255;
				b = (b * a + shade * (255 - a)) / 255;

				px[x + y * width] = r | (g << 8) | (b << 16) | 0xFF000000;
			}
		}
	}
	
	HBITMAP hBitmap = CreateBitmap(width, height, 1, 32, px);
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
			EnumChildWindows(hWnd, SetFontProc, GetStockObject(DEFAULT_GUI_FONT));
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
				SendMessage(data->hWndTextureSelect, LB_ADDSTRING, 0, buffer);
			}
			for (int i = 0; i < data->nsbtx.nPalettes; i++) {
				char *name = data->nsbtx.palettes[i].name;
				int len = max16Len(name);
				for (int j = 0; j < len; j++) {
					buffer[j] = name[j];
				}
				buffer[len] = 0;
				SendMessage(data->hWndPaletteSelect, LB_ADDSTRING, 0, buffer);
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
			HBITMAP hBitmap = renderTexture(texture, palette);

			HDC hCompat = CreateCompatibleDC(hDC);
			SelectObject(hCompat, hBitmap);
			BitBlt(hDC, 150, 22, TEXS(texture->texImageParam), TEXT(texture->texImageParam), hCompat, 0, 0, SRCCOPY);

			char bf[64];
			SelectObject(hDC, GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(hDC, TRANSPARENT);
			if (FORMAT(texture->texImageParam) == CT_DIRECT) {
				sprintf(bf, "%s texture, %dx%d", stringFromFormat(FORMAT(texture->texImageParam)), TEXS(texture->texImageParam), TEXT(texture->texImageParam));
			} else {
				sprintf(bf, "%s texture, %dx%d; palette: %d colors", stringFromFormat(FORMAT(texture->texImageParam)), TEXS(texture->texImageParam), TEXT(texture->texImageParam), palette->nColors);
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
				if (LOWORD(wParam) == ID_FILE_EXPORT) {
					LPWSTR location = saveFileDialog(hWnd, L"Save Texture", L"TGA Files (*.tga)\0*.tga\0All Files\0*.*\0", L"tga");
					if (!location) break;

					writeNitroTGA(location, data->nsbtx.textures + SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0),
								  data->nsbtx.palettes + SendMessage(data->hWndPaletteSelect, LB_GETCURSEL, 0, 0));

					free(location);
				} else if (LOWORD(wParam) == ID_FILE_SAVE) {
					nsbtxSaveFile(data->szOpenFile, &data->nsbtx);
				}
			}
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				int m = HIWORD(wParam);
				if (m == LBN_SELCHANGE) {
					InvalidateRect(hWnd, NULL, TRUE);
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
		case WM_DESTROY:
		{
			for (int i = 0; i < data->nsbtx.nTextures; i++) {
				free(data->nsbtx.textures[i].texel);
				if (data->nsbtx.textures[i].cmp) free(data->nsbtx.textures[i].cmp);
			}
			for (int i = 0; i < data->nsbtx.nPalettes; i++) {
				free(data->nsbtx.palettes[i].pal);
			}
			free(data->nsbtx.textureDictionary.entry.data);
			free(data->nsbtx.paletteDictionary.entry.data);
			free(data->nsbtx.palettes);
			free(data->nsbtx.textures);
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

HWND CreateNsbtxViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path) {
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