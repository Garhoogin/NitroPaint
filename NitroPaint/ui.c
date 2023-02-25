#include <Windows.h>
#include <CommCtrl.h>

#include "ui.h"

HWND CreateButton(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL def) {
	return CreateWindow(L"BUTTON", text, WS_VISIBLE | WS_CHILD | (def ? BS_DEFPUSHBUTTON : 0), x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateCheckbox(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL checked) {
	HWND h = CreateWindow(L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, x, y, width, height, hWnd, NULL, NULL, NULL);
	SendMessage(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
	return h;
}

HWND CreateGroupbox(HWND hWnd, LPCWSTR title, int x, int y, int width, int height) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX;
	return CreateWindow(L"BUTTON", title, dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateEdit(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL number) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | (number ? ES_NUMBER : 0);
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", text, dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	return h;
}

HWND CreateStatic(HWND hWnd, LPCWSTR text, int x, int y, int width, int height) {
	return CreateWindow(L"STATIC", text, WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateStaticAligned(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, int alignment) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE;
	if (alignment == SCA_RIGHT) dwStyle |= SS_RIGHT;
	if (alignment == SCA_CENTER) dwStyle |= SS_CENTER;
	return CreateWindow(L"STATIC", text, dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateCombobox(HWND hWnd, LPCWSTR *items, int nItems, int x, int y, int width, int height, int def) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST;
	HWND h = CreateWindow(L"COMBOBOX", L"", dwStyle, x, y, width, 100, hWnd, NULL, NULL, NULL);
	if (items != NULL && nItems > 0) {
		for (int i = 0; i < nItems; i++) {
			LPCWSTR item = items[i];
			SendMessage(h, CB_ADDSTRING, wcslen(item), (LPARAM) item);
		}
	}
	SendMessage(h, CB_SETCURSEL, def, 0);
	return h;
}

HWND CreateTrackbar(HWND hWnd, int x, int y, int width, int height, int vMin, int vMax, int vDef) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD;
	HWND h = CreateWindow(TRACKBAR_CLASS, L"", dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	SendMessage(h, TBM_SETRANGE, TRUE, vMin | (vMax << 16));
	SendMessage(h, TBM_SETPOS, TRUE, vDef);
	return h;
}

int GetCheckboxChecked(HWND hWnd) {
	return SendMessage(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int GetEditNumber(HWND hWnd) {
	WCHAR buffer[32];
	SendMessage(hWnd, WM_GETTEXT, sizeof(buffer) / sizeof(*buffer), (LPARAM) buffer);
	return _wtol(buffer);
}

int GetTrackbarPosition(HWND hWnd) {
	return SendMessage(hWnd, TBM_GETPOS, 0, 0);
}
