#pragma once
#include "childwindow.h"
#include "ncer.h"

typedef struct NCERVIEWERDATA_ {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];
	NCER ncer;
	int showBorders;
	int scale;
	int hoverX;
	int hoverY;
	int cell;
	int oam;

	HWND hWndCellDropdown;
	HWND hWndCharacterOffset;
	HWND hWndPaletteDropdown;
	HWND hWndSizeLabel;
	HWND hWndCharacterOffsetButton;
	HWND hWndOamDropdown;
	HWND hWndImportBitmap;
	HWND hWndImportReplacePalette;
} NCERVIEWERDATA;

VOID RegisterNcerViewerClass(VOID);

HWND CreateNcerViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);