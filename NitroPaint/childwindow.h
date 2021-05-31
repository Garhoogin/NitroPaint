#pragma once
#include <Windows.h>

typedef struct {
	int contentWidth;
	int contentHeight;
	int paddingLeft;
	int paddingTop;
	int paddingRight;
	int paddingBottom;
	int allowClear;
	int sizeLevel;
} FRAMEDATA;

BOOL __stdcall SetFontProc(HWND hWnd, LPARAM lParam);

VOID UpdateScrollbarVisibility(HWND hWnd);

HWND getMainWindow(HWND hWnd);

LRESULT WINAPI DefChildProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

VOID SetWindowSize(HWND hWnd, int width, int height);