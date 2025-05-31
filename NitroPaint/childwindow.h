#pragma once
#include <Windows.h>

#include "ui.h"

typedef struct {
	int contentWidth;
	int contentHeight;
} FRAMEDATA;

BOOL __stdcall SetFontProc(HWND hWnd, LPARAM lParam);

VOID UpdateScrollbarVisibility(HWND hWnd);

VOID ScaleInterface(HWND hWnd, float scale);

VOID DestroyChild(HWND hWnd);

HWND getMainWindow(HWND hWnd);

LRESULT WINAPI DefChildProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

VOID SetWindowSize(HWND hWnd, int width, int height);