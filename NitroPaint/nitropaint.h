#pragma once

#define g_useDarkTheme 0

typedef struct {
	HWND hWndMdi;
	HWND hWndNclrViewer;
	HWND hWndNcgrViewer;
	HWND hWndNscrViewer;
	HWND hWndNcerViewer;
	HWND hWndNsbtxViewer;
} NITROPAINTSTRUCT;

LPWSTR openFileDialog(HWND hWnd, LPWSTR title, LPWSTR filter, LPWSTR extension);

LPWSTR saveFileDialog(HWND hWnd, LPWSTR title, LPWSTR filter, LPWSTR extension);