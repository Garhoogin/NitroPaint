#include <Windows.h>

#include "color.h"
#include "palops.h"
#include "childwindow.h"
#include "colorchooser.h"

typedef struct PAL_OP_DATA_ {
	HWND hWndHue;
	HWND hWndSaturation;
	HWND hWndValue;
	HWND hWndRotate;
	HWND hWndSrcIndex;
	HWND hWndSrcLength;
	HWND hWndIgnoreFirst;
	HWND hWndDestOffset;
	HWND hWndDestCount;
	HWND hWndDestStride;
	HWND hWndComplete;
	int inited;
} PAL_OP_DATA;

#define NV_SETDATA (WM_USER+1)

void PalopPopulateUI(PAL_OP_DATA *data, PAL_OP *op) {
	WCHAR buf[16];
	SendMessage(data->hWndIgnoreFirst, BM_SETCHECK, op->ignoreFirst, 0);
	int len = wsprintfW(buf, L"%d", op->hueRotate);
	SendMessage(data->hWndHue, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->saturationAdd);
	SendMessage(data->hWndSaturation, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->valueAdd);
	SendMessage(data->hWndValue, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->paletteRotation);
	SendMessage(data->hWndRotate, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->srcIndex);
	SendMessage(data->hWndSrcIndex, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->srcLength);
	SendMessage(data->hWndSrcLength, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->dstOffset);
	SendMessage(data->hWndDestOffset, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->dstCount);
	SendMessage(data->hWndDestCount, WM_SETTEXT, len, (LPARAM) buf);
	len = wsprintfW(buf, L"%d", op->dstStride);
	SendMessage(data->hWndDestStride, WM_SETTEXT, len, (LPARAM) buf);
}

void PalopReadUI(PAL_OP_DATA *data, PAL_OP *op) {
	WCHAR buf[16];
	op->ignoreFirst = SendMessage(data->hWndIgnoreFirst, BM_GETCHECK, 0, 0);
	SendMessage(data->hWndHue, WM_GETTEXT, 16, (LPARAM) buf);
	op->hueRotate = _wtol(buf);
	SendMessage(data->hWndSaturation, WM_GETTEXT, 16, (LPARAM) buf);
	op->saturationAdd = _wtol(buf);
	SendMessage(data->hWndValue, WM_GETTEXT, 16, (LPARAM) buf);
	op->valueAdd = _wtol(buf);
	SendMessage(data->hWndRotate, WM_GETTEXT, 16, (LPARAM) buf);
	op->paletteRotation = _wtol(buf);
	SendMessage(data->hWndSrcIndex, WM_GETTEXT, 16, (LPARAM) buf);
	op->srcIndex = _wtol(buf);
	SendMessage(data->hWndSrcLength, WM_GETTEXT, 16, (LPARAM) buf);
	op->srcLength = _wtol(buf);
	SendMessage(data->hWndDestOffset, WM_GETTEXT, 16, (LPARAM) buf);
	op->dstOffset = _wtol(buf);
	SendMessage(data->hWndDestCount, WM_GETTEXT, 16, (LPARAM) buf);
	op->dstCount = _wtol(buf);
	SendMessage(data->hWndDestStride, WM_GETTEXT, 16, (LPARAM) buf);
	op->dstStride = _wtol(buf);
}

void PalopRunOperation(COLOR *palIn, COLOR *palOut, int palSize, PAL_OP *op) {
	for (int i = 0; i < palSize; i++) {
		palOut[i] = palIn[i];
	}

	//run one entry for each iteration at a time
	COLOR *inBase = palIn + op->srcIndex;
	COLOR *outBase = palOut + op->srcIndex + op->dstOffset * op->dstStride;
	int outBaseIndex = op->srcIndex + op->dstOffset * op->dstStride;
	int blockLength = op->srcLength;
	for (int i = 0; i < blockLength; i++) {
		COLOR srcColor = inBase[i];
		COLOR32 as32 = ColorConvertFromDS(srcColor);
		COLOR32 out32;
		int ch, cs, cv;
		ConvertRGBToHSV(as32, &ch, &cs, &cv);

		for (int j = 0; j < op->dstCount; j++) {
			COLOR *destBlock = outBase + j * op->dstStride;
			ch += op->hueRotate;
			cs += op->saturationAdd;
			cv += op->valueAdd;

			ch %= 360;
			if (ch < 0) ch += 360;
			if (cs > 100) cs = 100;
			else if (cs < 0) cs = 0;
			if (cv > 100) cv = 100;
			else if (cv < 0) cv = 0;

			ConvertHSVToRGB(ch, cs, cv, &out32);
			if (i == 0 && op->ignoreFirst) {
				out32 = as32;
			}
			int outIndex = i;

			//process rotation
			int rotateBy = 0;
			if (i != 0 || !op->ignoreFirst) {
				if (op->ignoreFirst) {
					rotateBy = (j + 1) * op->paletteRotation;
					outIndex = ((outIndex - 1) + rotateBy) % (blockLength - 1);
					if (outIndex < 0) outIndex += blockLength;
					outIndex++;
				} else {
					rotateBy = (j + 1) * op->paletteRotation;
					outIndex = (outIndex + rotateBy) % blockLength;
					if (outIndex < 0) outIndex += blockLength;
				}
			}


			if (destBlock + outIndex < palOut + palSize) {
				destBlock[outIndex] = ColorConvertToDS(out32);
			}
		}
	}
}

LRESULT CALLBACK PalopWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	PAL_OP *palOp = (PAL_OP *) GetWindowLongPtr(hWnd, 0);
	PAL_OP_DATA *data = (PAL_OP_DATA *) GetWindowLongPtr(hWnd, sizeof(PAL_OP *));
	switch (msg) {
		case WM_CREATE:
			break;
		case NV_SETDATA:
		{
			int boxWidth = 200, box1Height = 133, box2Height = 106, box3Height = 79 + 27;
			int box1Y = 10;
			int box2Y = 20 + box1Height;
			int box3Y = 30 + box1Height + box2Height;
			int bottomY = 40 + box1Height + box2Height + box3Height;
			int leftX = 20;
			CreateWindow(L"BUTTON", L"Palette Operation", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 10, box1Y, boxWidth, box1Height, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Source", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 10, box2Y, boxWidth, box2Height, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Destination", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 10, box3Y, boxWidth, box3Height, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Hue Rotation:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box1Y + 18, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Saturation:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box1Y + 18 + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Value:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box1Y + 18 + 54, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette Rotation:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box1Y + 18 + 81, 100, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Index:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box2Y + 18 + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Length:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box2Y + 18 + 54, 100, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Offset:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box3Y + 18, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Count:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box3Y + 18 + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Stride:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, box3Y + 18 + 54, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndHue = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box1Y + 18, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndSaturation = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box1Y + 18 + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndValue = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box1Y + 18 + 54, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndRotate = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box1Y + 18 + 81, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndIgnoreFirst = CreateWindow(L"BUTTON", L"Ignore First", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP, leftX, box2Y + 18, 150, 22, hWnd, NULL, NULL, NULL);
			data->hWndSrcIndex = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box2Y + 18 + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndSrcLength = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box2Y + 18 + 54, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndDestOffset = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box3Y + 18, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndDestCount = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box3Y + 18 + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndDestStride = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_TABSTOP, leftX + 110, box3Y + 18 + 54, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndComplete = CreateWindow(L"BUTTON", L"Complete", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_TABSTOP, 10 + boxWidth - 150, bottomY, 150, 22, hWnd, NULL, NULL, NULL);

			PalopPopulateUI(data, palOp);
			SetWindowSize(hWnd, 20 + boxWidth, 72 + box1Height + box2Height + box3Height);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) (HFONT) GetStockObject(DEFAULT_GUI_FONT));
			SetFocus(data->hWndHue);
			data->inited = 1;
			if (palOp->updateCallback != NULL) {
				palOp->updateCallback(palOp);
			}
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl == NULL || data == NULL) break;
			if (!data->inited) break;

			int notif = HIWORD(wParam);
			if (hWndControl == data->hWndComplete) {
				palOp->result = 1;
				SendMessage(hWnd, WM_CLOSE, 0, 0);
				break;
			}

			//get clas
			WCHAR class[16];
			GetClassName(hWndControl, class, sizeof(class) / sizeof(*class));
			if ((_wcsicmp(class, L"EDIT") == 0 && notif == EN_CHANGE) || 
				(_wcsicmp(class, L"BUTTON") == 0 && notif == BN_CLICKED)) {
				PalopReadUI(data, palOp);
				if (palOp->updateCallback != NULL) {
					palOp->updateCallback(palOp);
				}
			}
			break;
		}
		case WM_CLOSE:
			if (palOp != NULL) {
				SetForegroundWindow(palOp->hWndParent);
				SetWindowLong(palOp->hWndParent, GWL_STYLE, GetWindowLong(palOp->hWndParent, GWL_STYLE) & ~WS_DISABLED);
			}
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int SelectPaletteOperation(PAL_OP *opStruct) {
	//test class registration
	static int clsRegistered = 0;
	if (!clsRegistered) {
		WNDCLASSEX wcex = { 0 };
		wcex.cbSize = sizeof(wcex);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH) (COLOR_WINDOW);
		wcex.lpszClassName = L"PaletteOperationClass";
		wcex.lpfnWndProc = PalopWndProc;
		wcex.cbWndExtra = sizeof(PAL_OP *) + sizeof(PAL_OP_DATA *);
		RegisterClassEx(&wcex);

		clsRegistered = 1;
	}

	PAL_OP_DATA *data = (PAL_OP_DATA *) calloc(1, sizeof(PAL_OP_DATA));
	HWND hWndParent = opStruct->hWndParent;
	opStruct->result = 0;
	HWND hWnd = CreateWindowEx(WS_EX_DLGMODALFRAME, L"PaletteOperationClass", L"Palette Operation", WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndParent, NULL, NULL, NULL);
	SetWindowLongPtr(hWnd, 0, (LONG_PTR) opStruct);
	SetWindowLongPtr(hWnd, sizeof(PAL_OP *), (LONG_PTR) data);
	SendMessage(hWnd, NV_SETDATA, 0, 0);
	ShowWindow(hWnd, SW_SHOW);
	SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) | WS_DISABLED);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (!IsDialogMessage(hWnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	free(data);
	SetForegroundWindow(opStruct->hWndParent);
	return opStruct->result;
}
