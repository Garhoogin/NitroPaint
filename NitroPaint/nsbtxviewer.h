#pragma once
#include "nsbtx.h"
#include "childwindow.h"
#include <Windows.h>

typedef struct {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];
	TexArc nsbtx;
	int showBorders;
	int scale;

	HWND hWndTextureSelect;
	HWND hWndPaletteSelect;
	HWND hWndExportAll;
	HWND hWndResourceButton;
	HWND hWndReplaceButton;
	HWND hWndAddButton;
} NSBTXVIEWERDATA;

VOID RegisterNsbtxViewerClass(VOID);

void CreateVramUseWindow(HWND hWndParent, TexArc *nsbtx);

HWND CreateNsbtxViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNsbtxViewerImmediate(int x, int y, int width, int height, HWND hWndParent, TexArc *nsbtx);
