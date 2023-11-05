#pragma once
#include <Windows.h>
#include "editor.h"
#include "nsbtx.h"
#include "childwindow.h"

typedef struct {
	EDITOR_BASIC_MEMBERS;
	TexArc nsbtx;

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
