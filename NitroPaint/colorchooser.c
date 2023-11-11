#include <Windows.h>

#include "childwindow.h"
#include "nitropaint.h"
#include "colorchooser.h"
#include "color.h"

static BOOL g_ccRegistered = FALSE;

VOID ConvertRGBToHSV(COLORREF col, int *h, int *s, int *v) {
	int r = col & 0xFF;
	int g = (col >> 8) & 0xFF;
	int b = (col >> 16) & 0xFF;

	int maxComponent = max(max(r, g), b);
	int minComponent = min(min(r, g), b);
	int rangeComponents = maxComponent - minComponent;
	int lightness = (maxComponent + minComponent + 1) >> 1;

	*h = 0;
	if (rangeComponents) {
		if (maxComponent == r) {
			*h = 60 * (g - b) / rangeComponents;
		} else if (maxComponent == g) {
			*h = 120 + 60 * (b - r) / rangeComponents;
		} else if (maxComponent == b) {
			*h = 240 + 60 * (r - g) / rangeComponents;
		}
		if (*h < 0) *h += 360;
	}

	*v = 100 * maxComponent / 255;
	*s = 0;
	if (maxComponent) {		
		*s = 100 * rangeComponents / maxComponent;		
	}
}

VOID ConvertHSVToRGB(int h, int s, int v, COLORREF *rgb) {
	h %= 360;
	int chroma = 255 * s * v / 10000;
	int x = chroma * (60 - abs((h % 120) - 60)) / 60;

	v = v * 255 / 100;

	int r1 = 0, g1 = 0, b1 = 0;
	if (0 <= h && h <= 60) {
		r1 = chroma;
		g1 = x;
	} else if (60 < h && h <= 120) {
		r1 = x;
		g1 = chroma;
	} else if (120 < h && h <= 180) {
		g1 = chroma;
		b1 = x;
	} else if (180 < h && h <= 240) {
		g1 = x;
		b1 = chroma;
	} else if (240 < h && h <= 300) {
		r1 = x;
		b1 = chroma;
	} else if (300 < h && h <= 360) {
		r1 = chroma;
		b1 = x;
	}
	int m = v - chroma;

	r1 += m;
	g1 += m;
	b1 += m;

	*rgb = RGB(r1, g1, b1);
}

static HBITMAP RenderGradientBar(COLORREF left, COLORREF right, int width, int height) {
	DWORD *px = (DWORD *) calloc(width * height, 4);

	int r1 = left & 0xFF, r2 = right & 0xFF;
	int g1 = (left >> 8) & 0xFF, g2 = (right >> 8) & 0xFF;
	int b1 = (left >> 16) & 0xFF, b2 = (right >> 16) & 0xFF;

	for (int x = 0; x < width; x++) {
		int r = ((width - 1 - x) * r1 + x * r2) / (width - 1);
		int g = ((width - 1 - x) * g1 + x * g2) / (width - 1);
		int b = ((width - 1 - x) * b1 + x * b2) / (width - 1);
		DWORD col = b | (g << 8) | (r << 16);
		for (int y = 0; y < height; y++) {
			px[x + y * width] = col;
		}
	}

	HBITMAP hBitmap = CreateBitmap(width, height, 1, 32, px);
	free(px);
	return hBitmap;
}

static HBITMAP RenderHSVGradientBar(int hLeft, int sLeft, int vLeft, int hRight, int sRight, int vRight, int width, int height) {
	DWORD *px = (DWORD *) calloc(width * height, 4);

	for (int x = 0; x < width; x++) {
		int h = ((width - 1 - x) * hLeft + x * hRight) / (width - 1);
		int s = ((width - 1 - x) * sLeft + x * sRight) / (width - 1);
		int v = ((width - 1 - x) * vLeft + x * vRight) / (width - 1);

		COLORREF rgb;
		ConvertHSVToRGB(h, s, v, &rgb);

		DWORD col = (rgb & 0xFF00) | (rgb >> 16) | ((rgb & 0xFF) << 16);
		for (int y = 0; y < height; y++) {
			px[x + y * width] = col;
		}
	}

	HBITMAP hBitmap = CreateBitmap(width, height, 1, 32, px);
	free(px);
	return hBitmap;
}

static VOID DrawTick(HDC hDC, int x, int y) {
	MoveToEx(hDC, x - 1, y + 3 + 1, NULL);
	LineTo(hDC, x, y + 4 + 1);
	LineTo(hDC, x + 2, y + 2 + 1);
	MoveToEx(hDC, x - 1, y + 4 + 1, NULL);
	LineTo(hDC, x, y + 5 + 1);
	LineTo(hDC, x + 2, y + 3 + 1);
	MoveToEx(hDC, x - 1, 17 + y - 1, NULL);
	LineTo(hDC, x, 16 + y - 1);
	LineTo(hDC, x + 2, 18 + y - 1);
	MoveToEx(hDC, x - 1, 18 + y - 1, NULL);
	LineTo(hDC, x, 17 + y - 1);
	LineTo(hDC, x + 2, 19 + y - 1);
}

typedef struct {
	CHOOSECOLORW *chooseColor;
	COLORREF inputColor;
	BOOL mouseDown;
	int draggingSlider;
	HWND inputs[7];
	HWND hWndOk;
	HWND hWndCancel;
	int initialH;
	int initialS;
	int initialV;
	BOOL noUpdateTextBoxes;
	int exitStatus;
} CHOOSECOLORDATA;

static VOID UpdateValues(HWND hWnd, COLORREF rgb) {
	CHOOSECOLORDATA *data = (CHOOSECOLORDATA *) GetWindowLongPtr(hWnd, 0);
	data->noUpdateTextBoxes = TRUE;

	WCHAR text[5];

	HWND hWndFocus = GetFocus();

	COLOR ds = ColorConvertToDS(rgb);
	if (hWndFocus != data->inputs[0]) SetEditNumber(data->inputs[0], (ds >> 0) & 0x1F);
	if (hWndFocus != data->inputs[1]) SetEditNumber(data->inputs[1], (ds >> 5) & 0x1F);
	if (hWndFocus != data->inputs[2]) SetEditNumber(data->inputs[2], (ds >> 10) & 0x1F);

	int h, s, v;
	ConvertRGBToHSV(rgb, &h, &s, &v);
	if (hWndFocus != data->inputs[3]) SetEditNumber(data->inputs[3], h);
	if (hWndFocus != data->inputs[4]) SetEditNumber(data->inputs[4], s);
	if (hWndFocus != data->inputs[5]) SetEditNumber(data->inputs[5], v);

	wsprintf(text, L"%04X", ds);
	if (hWndFocus != data->inputs[6]) SendMessage(data->inputs[6], WM_SETTEXT, 0, (LPARAM) text);

	InvalidateRect(hWnd, NULL, FALSE);
	data->noUpdateTextBoxes = FALSE;
}

static LRESULT WINAPI ColorChooserWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CHOOSECOLORDATA *data = (CHOOSECOLORDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (CHOOSECOLORDATA *) calloc(1, sizeof(CHOOSECOLORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			CreateStatic(hWnd, L"R:", 10, 10, 15, 22);
			CreateStatic(hWnd, L"G:", 10, 37, 15, 22);
			CreateStatic(hWnd, L"B:", 10, 64, 15, 22);

			CreateStatic(hWnd, L"H:", 10, 96, 15, 22);
			CreateStatic(hWnd, L"S:", 10, 123, 15, 22);
			CreateStatic(hWnd, L"V:", 10, 150, 15, 22);

			CreateStatic(hWnd, L"Hex:", 271, 91, 30, 22);

			HWND hR = CreateEdit(hWnd, L"0", 235, 10, 20, 22, TRUE);
			HWND hG = CreateEdit(hWnd, L"0", 235, 37, 20, 22, TRUE);
			HWND hB = CreateEdit(hWnd, L"0", 235, 64, 20, 22, TRUE);
			HWND hH = CreateEdit(hWnd, L"0", 235, 96, 30, 22, TRUE);
			HWND hS = CreateEdit(hWnd, L"0", 235, 123, 30, 22, TRUE);
			HWND hV = CreateEdit(hWnd, L"0", 235, 150, 30, 22, TRUE);
			HWND hHex = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0000", WS_VISIBLE | WS_CHILD | ES_UPPERCASE | ES_AUTOHSCROLL | WS_TABSTOP, 301, 91, 40, 22, hWnd, NULL, NULL, NULL);
			HWND hWndOk = CreateButton(hWnd, L"OK", 275, 177, 66, 22, TRUE);
			HWND hWndCancel = CreateButton(hWnd, L"Cancel", 199, 177, 66, 22, FALSE);
			data->inputs[0] = hR;
			data->inputs[1] = hG;
			data->inputs[2] = hB;
			data->inputs[3] = hH;
			data->inputs[4] = hS;
			data->inputs[5] = hV;
			data->inputs[6] = hHex;
			data->hWndOk = hWndOk;
			data->hWndCancel = hWndCancel;
			data->exitStatus = FALSE;

			SetGUIFont(hWnd);
			break;
		}
		case WM_LBUTTONDOWN:
		{
			SetFocus(hWnd);
			SetCapture(hWnd);

			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);
			int x = pt.x;
			int y = pt.y;

			if (x >= 30 && x < 230 && y >= 10 && y <= 96) {
				if (y < 10 + 22) {
					data->draggingSlider = 0;
					data->mouseDown = TRUE;
				} else if (y >= 37 && y < 37 + 22) {
					data->draggingSlider = 1;
					data->mouseDown = TRUE;
				} else if (y >= 64 && y < 86) {
					data->draggingSlider = 2;
					data->mouseDown = TRUE;
				}
			}
			if (x >= 30 && x < 230 && y >= 96 && y < 172) {
				int h, s, v;
				CHOOSECOLORW *chooseColor = (CHOOSECOLORW *) data->chooseColor;
				COLORREF rgb = chooseColor->rgbResult;
				ConvertRGBToHSV(rgb, &h, &s, &v);

				data->initialH = h;
				data->initialS = s;
				data->initialV = v;

				if (y < 96 + 22) {
					data->draggingSlider = 3;
					data->mouseDown = TRUE;
				} else if (y >= 123 && y < 123 + 22) {
					data->draggingSlider = 4;
					data->mouseDown = TRUE;
				} else if (y >= 150 && y < 150 + 22) {
					data->draggingSlider = 5;
					data->mouseDown = TRUE;
				}
			}
			//pass-through
		}
		case WM_MOUSEMOVE:
		{
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);
			int x = pt.x;
			int y = pt.y;
			if (data->mouseDown) {
				int channel = data->draggingSlider;
				x -= 30;
				int v = x;
				if (v >= 200) v = 199;
				if (v < 0) v = 0;

				CHOOSECOLORW *chooseColor = data->chooseColor;
				COLORREF rgb = chooseColor->rgbResult;

				if (channel == 0 || channel == 1 || channel == 2) {
					v = (x * 255 + 99) / 199;
					if (v < 0) v = 0;
					if (v > 255) v = 255;
					v = ColorRoundToDS15(v);

					if (channel == 0) rgb &= 0xFFFF00;
					if (channel == 1) rgb &= 0xFF00FF;
					if (channel == 2) rgb &= 0x00FFFF;
					rgb |= (v << (channel * 8));
				}
				if (channel == 3 || channel == 4 || channel == 5) {
					int hsv[3];
					hsv[0] = data->initialH;
					hsv[1] = data->initialS;
					hsv[2] = data->initialV;
					if (channel == 3) {
						v = v * 359 / 199;
					} else {
						v = v * 100 / 199;
					}
					hsv[channel - 3] = v;
					ConvertHSVToRGB(hsv[0], hsv[1], hsv[2], &rgb);

					rgb = ColorConvertFromDS(ColorConvertToDS(rgb));
				}
				if (chooseColor->rgbResult != rgb) {
					chooseColor->rgbResult = rgb;

					UpdateValues(hWnd, rgb);
				}
			}
			break;
		}
		case WM_LBUTTONUP:
		{
			data->mouseDown = FALSE;
			ReleaseCapture();
			break;
		}
		case WM_COMMAND:
		{
			if (((HWND) lParam == data->hWndOk && HIWORD(wParam) == BN_CLICKED) || (lParam == 0 && LOWORD(wParam) == IDOK)) {
				data->exitStatus = TRUE;
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			if (((HWND) lParam == data->hWndCancel && HIWORD(wParam) == BN_CLICKED) || (lParam == 0 && LOWORD(wParam) == IDCANCEL)) {
				data->chooseColor->rgbResult = data->inputColor;
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			if (lParam && HIWORD(wParam) == EN_SETFOCUS) {
				CHOOSECOLORW *chooseColor = data->chooseColor;
				if (chooseColor != NULL) {
					COLORREF rgb = chooseColor->rgbResult;

					HWND hWndEdit = (HWND) lParam;
					int index = -1;
					for (int i = 0; i < 7; i++) {
						if (data->inputs[i] == hWndEdit) {
							index = i;
							break;
						}
					}
					if (index > -1) {
						if (index == 3 || index == 4 || index == 5) {
							int h, s, v;
							ConvertRGBToHSV(rgb, &h, &s, &v);
							data->initialH = h;
							data->initialS = s;
							data->initialV = v;
						}
					}
				}
			}
			if (lParam && HIWORD(wParam) == EN_UPDATE && !data->noUpdateTextBoxes) {


				CHOOSECOLORW *chooseColor = data->chooseColor;
				if (chooseColor != NULL) {
					COLORREF rgb = chooseColor->rgbResult;

					HWND hWndEdit = (HWND) lParam;
					int index = -1;
					for (int i = 0; i < 7; i++) {
						if (data->inputs[i] == hWndEdit) {
							index = i;
							break;
						}
					}

					if (index > -1) {
						WCHAR buffer[5] = { 0 };
						SendMessage(hWndEdit, WM_GETTEXT, 5, (LPARAM) buffer);
						int val = _wtol(buffer);

						if (index == 0 || index == 1 || index == 2) {
							val = (val * 510 + 31) / 62;
							if (index == 0) rgb &= 0xFFFF00;
							if (index == 1) rgb &= 0xFF00FF;
							if (index == 2) rgb &= 0x00FFFF;
							rgb |= (val << (index * 8));
							chooseColor->rgbResult = rgb;
							UpdateValues(hWnd, rgb);
						} else if (index == 3 || index == 4 || index == 5) {
							int hsv[3];
							ConvertRGBToHSV(rgb, hsv, hsv + 1, hsv + 2);

							hsv[0] = data->initialH;
							hsv[1] = data->initialS;
							hsv[2] = data->initialV;

							hsv[index - 3] = val;
							ConvertHSVToRGB(hsv[0], hsv[1], hsv[2], &rgb);
							chooseColor->rgbResult = rgb;
							UpdateValues(hWnd, rgb);
						} else if (index == 6) {
							WCHAR *current = buffer;
							val = 0;
							while (*current) {
								WCHAR c = *current;
								val <<= 4;
								if (c >= 'A') val |= (c - 'A' + 10);
								else val |= (c - '0');
								current++;
							}

							rgb = ColorConvertFromDS(val);
							chooseColor->rgbResult = rgb;
							UpdateValues(hWnd, rgb);
						}
					}
				}
			}
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			CHOOSECOLORW *chooseColor = data->chooseColor;
			COLORREF currentColor = chooseColor->rgbResult;

			HDC hOff = CreateCompatibleDC(hDC);
			HBITMAP hRectangle = RenderGradientBar(currentColor & 0xFFFF00, currentColor | 0xFF, 200, 14);
			SelectObject(hOff, hRectangle);
			BitBlt(hDC, 30, 10 + 4, 200, 22 - 8, hOff, 0, 0, SRCCOPY);
			DeleteObject(hRectangle);

			hRectangle = RenderGradientBar(currentColor & 0xFF00FF, currentColor | 0xFF00, 200, 14);
			SelectObject(hOff, hRectangle);
			BitBlt(hDC, 30, 37 + 4, 200, 22 - 8, hOff, 0, 0, SRCCOPY);
			DeleteObject(hRectangle);

			hRectangle = RenderGradientBar(currentColor & 0xFFFF, currentColor | 0xFF0000, 200, 14);
			SelectObject(hOff, hRectangle);
			BitBlt(hDC, 30, 64 + 4, 200, 22 - 8, hOff, 0, 0, SRCCOPY);
			DeleteObject(hRectangle);

			int h, s, v;
			ConvertRGBToHSV(currentColor, &h, &s, &v);

			hRectangle = RenderHSVGradientBar(0, s, v, 359, s, v, 200, 14);
			SelectObject(hOff, hRectangle);
			BitBlt(hDC, 30, 96 + 4, 200, 22 - 8, hOff, 0, 0, SRCCOPY);
			DeleteObject(hRectangle);

			hRectangle = RenderHSVGradientBar(h, 0, v, h, 100, v, 200, 14);
			SelectObject(hOff, hRectangle);
			BitBlt(hDC, 30, 123 + 4, 200, 22 - 8, hOff, 0, 0, SRCCOPY);
			DeleteObject(hRectangle);

			hRectangle = RenderHSVGradientBar(h, s, 0, h, s, 100, 200, 14);
			SelectObject(hOff, hRectangle);
			BitBlt(hDC, 30, 150 + 4, 200, 22 - 8, hOff, 0, 0, SRCCOPY);
			DeleteObject(hRectangle);


			HBRUSH colorBrush = CreateSolidBrush(currentColor & 0xFFFFFF);
			SelectObject(hDC, colorBrush);
			Rectangle(hDC, 265, 10, 341, 86);
			DeleteObject(colorBrush);

			HPEN oldPen = SelectObject(hDC, GetStockObject(NULL_PEN));
			HBRUSH back = GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1);
			SelectObject(hDC, back);
			Rectangle(hDC, 230, 10, 232, 172);
			Rectangle(hDC, 29, 10, 31, 172);
			SelectObject(hDC, oldPen);

			//draw ticks
			int r = currentColor & 0xFF;
			int g = (currentColor >> 8) & 0xFF;
			int b = (currentColor >> 16) & 0xFF;
			int l = max(max(r, g), b) + min(min(r, g), b);

			if (l < 255) {
				SelectObject(hDC, GetStockObject(WHITE_PEN));
			}

			DrawTick(hDC, 30 + 199 * r / 255, 10);
			DrawTick(hDC, 30 + 199 * g / 255, 37);
			DrawTick(hDC, 30 + 199 * b / 255, 64);
			DrawTick(hDC, 30 + 199 * h / 359, 96);
			DrawTick(hDC, 30 + 199 * s / 100, 123);
			DrawTick(hDC, 30 + 199 * v / 100, 150);

			DeleteObject(hOff);
			EndPaint(hWnd, &ps);
			break;
		}
		case WM_CLOSE:
		{
			if (!data->exitStatus) {
				data->chooseColor->rgbResult = data->inputColor;
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

BOOL WINAPI CustomChooseColor(CHOOSECOLORW *chooseColor) {
	if (chooseColor->lStructSize != sizeof(CHOOSECOLORW)) return FALSE;
	if (!g_ccRegistered) {
		WNDCLASSEX wcex = { 0 };
		wcex.cbSize = sizeof(wcex);
		wcex.lpszClassName = L"ColorChooserClass";
		wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = ColorChooserWndProc;
		wcex.cbWndExtra = sizeof(LPVOID) * 14;
		RegisterClassEx(&wcex);
		g_ccRegistered = TRUE;
	}

	DWORD dwStyle = WS_CAPTION | WS_SYSMENU;

	RECT rc;
	rc.top = 0;
	rc.left = 0;
	rc.right = 351;
	rc.bottom = 209;
	AdjustWindowRect(&rc, dwStyle, FALSE);

	HWND hWndChooser = CreateWindowEx(WS_EX_DLGMODALFRAME, L"ColorChooserClass", L"Choose Color", dwStyle, 
									CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, chooseColor->hwndOwner, NULL, NULL, NULL);
	CHOOSECOLORDATA *data = (CHOOSECOLORDATA *) GetWindowLongPtr(hWndChooser, 0);
	data->chooseColor = chooseColor;
	ShowWindow(hWndChooser, SW_SHOW);
	UpdateValues(hWndChooser, chooseColor->rgbResult);
	InvalidateRect(hWndChooser, NULL, FALSE);
	SetFocus(data->inputs[0]);

	HWND hWndOwner = chooseColor->hwndOwner;
	DoModal(hWndChooser);

	int status = data->exitStatus;
	free(data);

	return status;
}
