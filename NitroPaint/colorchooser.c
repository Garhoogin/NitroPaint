#include <Windows.h>

#include "childwindow.h"
#include "colorchooser.h"

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

HBITMAP RenderGradientBar(COLORREF left, COLORREF right, int width, int height) {
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

HBITMAP RenderHSVGradientBar(int hLeft, int sLeft, int vLeft, int hRight, int sRight, int vRight, int width, int height) {
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

VOID DrawTick(HDC hDC, int x, int y) {
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

WORD ConvertRGBToDS(COLORREF rgb) {
	int r = rgb & 0xFF;
	int g = (rgb >> 8) & 0xFF;
	int b = (rgb >> 16) & 0xFF;
	r = (r + 4) * 31 / 255;
	g = (g + 4) * 31 / 255;
	b = (b + 4) * 31 / 255;
	return r | (g << 5) | (b << 10);
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

VOID UpdateValues(HWND hWnd, COLORREF rgb) {
	CHOOSECOLORDATA *data = (CHOOSECOLORDATA *) GetWindowLongPtr(hWnd, 0);
	data->noUpdateTextBoxes = TRUE;

	WCHAR text[5];

	HWND hWndFocus = GetFocus();

	wsprintfW(text, L"%d", ((rgb & 0xFF) + 4) * 31 / 255);
	if(hWndFocus != data->inputs[0]) SendMessage(data->inputs[0], WM_SETTEXT, 0, (LPARAM) text);
	wsprintfW(text, L"%d", (((rgb >> 8) & 0xFF) + 4) * 31 / 255);
	if(hWndFocus != data->inputs[1]) SendMessage(data->inputs[1], WM_SETTEXT, 0, (LPARAM) text);
	wsprintfW(text, L"%d", (((rgb >> 16) & 0xFF) + 4) * 31 / 255);
	if(hWndFocus != data->inputs[2]) SendMessage(data->inputs[2], WM_SETTEXT, 0, (LPARAM) text);

	int h, s, v;
	ConvertRGBToHSV(rgb, &h, &s, &v);

	wsprintf(text, L"%d", h);
	if(hWndFocus != data->inputs[3]) SendMessage(data->inputs[3], WM_SETTEXT, 0, (LPARAM) text);
	wsprintf(text, L"%d", s);
	if(hWndFocus != data->inputs[4]) SendMessage(data->inputs[4], WM_SETTEXT, 0, (LPARAM) text);
	wsprintf(text, L"%d", v);
	if(hWndFocus != data->inputs[5]) SendMessage(data->inputs[5], WM_SETTEXT, 0, (LPARAM) text);

	WORD ds = ConvertRGBToDS(rgb);
	wsprintf(text, L"%04X", ds);
	if(hWndFocus != data->inputs[6]) SendMessage(data->inputs[6], WM_SETTEXT, 0, (LPARAM) text);

	InvalidateRect(hWnd, NULL, FALSE);
	data->noUpdateTextBoxes = FALSE;
}

LRESULT WINAPI ColorChooserWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CHOOSECOLORDATA *data = (CHOOSECOLORDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (CHOOSECOLORDATA *) calloc(1, sizeof(CHOOSECOLORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			CreateWindow(L"STATIC", L"R:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 15, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"G:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 15, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"B:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 64, 15, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"H:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 96, 15, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"S:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 123, 15, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"V:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 150, 15, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Hex:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 271, 91, 30, 22, hWnd, NULL, NULL, NULL);

			HWND hR = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"31", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_TABSTOP, 235, 10, 20, 22, hWnd, NULL, NULL, NULL);
			HWND hG = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"31", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_TABSTOP, 235, 37, 20, 22, hWnd, NULL, NULL, NULL);
			HWND hB = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"31", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_TABSTOP, 235, 64, 20, 22, hWnd, NULL, NULL, NULL);
			HWND hH = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_TABSTOP, 235, 96, 30, 22, hWnd, NULL, NULL, NULL);
			HWND hS = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_TABSTOP, 235, 123, 30, 22, hWnd, NULL, NULL, NULL);
			HWND hV = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_TABSTOP, 235, 150, 30, 22, hWnd, NULL, NULL, NULL);
			HWND hHex = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0000", WS_VISIBLE | WS_CHILD | ES_UPPERCASE | WS_TABSTOP, 301, 91, 40, 22, hWnd, NULL, NULL, NULL);
			HWND hWndOk = CreateWindow(L"BUTTON", L"OK", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_TABSTOP, 275, 177, 66, 22, hWnd, NULL, NULL, NULL);
			HWND hWndCancel = CreateWindow(L"BUTTON", L"Cancel", WS_VISIBLE | WS_CHILD | WS_TABSTOP, 199, 177, 66, 22, hWnd, NULL, NULL, NULL);
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

			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
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
					v = x * 255 / 199;
					if (v < 0) v = 0;
					if (v > 255) v = 255;
					v = ((v + 4) * 31 / 255) * 255 / 31;

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

					int r = rgb & 0xFF;
					int g = (rgb >> 8) & 0xFF;
					int b = (rgb >> 16) & 0xFF;
					r = ((r + 4) * 31 / 255) * 255 / 31;
					g = ((g + 4) * 31 / 255) * 255 / 31;
					b = ((b + 4) * 31 / 255) * 255 / 31;
					rgb = RGB(r, g, b);
				}
				if (chooseColor->rgbResult != rgb) {
					chooseColor->rgbResult = rgb;

					WCHAR text[5];

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
			if ((HWND) lParam == data->hWndOk && HIWORD(wParam) == BN_CLICKED) {
				data->exitStatus = TRUE;
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			if ((HWND) lParam == data->hWndCancel && HIWORD(wParam) == BN_CLICKED) {
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
							val = val * 255 / 31;
							if (index == 0) rgb &= 0xFFFF00;
							if (index == 1) rgb &= 0xFF00FF;
							if (index == 2) rgb &= 0x00FFFF;
							rgb |= (val << (index * 8));
							chooseColor->rgbResult = rgb;
							UpdateValues(hWnd, rgb);
						} else if (index == 3 || index == 4 || index == 5) {
							if (index == 3) val = val * 359 / 199;
							else val = val * 100 / 199;

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

							int r = val & 0x1F;
							int g = (val >> 5) & 0x1F;
							int b = (val >> 10) & 0x1F;
							r = r * 255 / 31;
							g = g * 255 / 31;
							b = b * 255 / 31;
							rgb = RGB(r, g, b);
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
			HWND hWndOwner = data->chooseColor->hwndOwner;
			if (hWndOwner) {
				SetWindowLongPtr(hWndOwner, GWL_STYLE, GetWindowLongPtr(hWndOwner, GWL_STYLE) & ~WS_DISABLED);
				SetActiveWindow(hWndOwner);
			}
			break;
		}
		case WM_DESTROY:
		{
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

	HWND hWndOwner = chooseColor->hwndOwner;
	if (hWndOwner) {
		SetWindowLongPtr(hWndOwner, GWL_STYLE, GetWindowLongPtr(hWndOwner, GWL_STYLE) | WS_DISABLED);
	}

	MSG msg;
	BOOL exitLoop = FALSE;
	while (IsWindow(hWndChooser)) {
		if(!GetMessage(&msg, NULL, 0, 0)) break;
		if (!IsDialogMessage(hWndChooser, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	if (msg.message == WM_QUIT) {
		PostQuitMessage(0);
	}
	int status = data->exitStatus;
	free(data);

	return status;
}