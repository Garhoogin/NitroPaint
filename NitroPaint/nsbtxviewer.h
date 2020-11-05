#pragma once
#include "nsbtx.h"
#include "childwindow.h"
#include <Windows.h>

typedef struct {
	FRAMEDATA frameData;
	NSBTX nsbtx;
	WCHAR szOpenFile[MAX_PATH];
	int showBorders;
	int scale;

	HWND hWndTextureSelect;
	HWND hWndPaletteSelect;
	HWND hWndReplaceButton;
} NSBTXVIEWERDATA;

VOID RegisterNsbtxViewerClass(VOID);

HWND CreateNsbtxViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);