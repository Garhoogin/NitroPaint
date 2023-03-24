#pragma once
#include "nsbtx.h"
#include "childwindow.h"
#include <Windows.h>

typedef struct {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];
	NSBTX nsbtx;
	int showBorders;
	int scale;

	HWND hWndTextureSelect;
	HWND hWndPaletteSelect;
	HWND hWndExportAll;
	HWND hWndResourceButton;
	HWND hWndReplaceButton;
} NSBTXVIEWERDATA;

VOID RegisterNsbtxViewerClass(VOID);

HWND CreateNsbtxViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);