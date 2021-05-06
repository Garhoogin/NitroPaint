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
	int mouseDown;
	int dragStartX;
	int dragStartY;
	int oamStartX;
	int oamStartY;
	int showCellBounds;

	HWND hWndCellDropdown;
	HWND hWndCharacterOffset;
	HWND hWndPaletteDropdown;
	HWND hWndSizeLabel;
	HWND hWndCharacterOffsetButton;
	HWND hWndOamDropdown;
	HWND hWndImportBitmap;
	HWND hWndImportReplacePalette;

	HWND hWndXInput;
	HWND hWndYInput;
	HWND hWndRotateScale;
	HWND hWndHFlip;
	HWND hWndVFlip;
	HWND hWndDisable;
	HWND hWndMatrix;
	HWND hWnd8bpp;
	HWND hWndMosaic;
	HWND hWndDoubleSize;
	HWND hWndType;
	HWND hWndPriority;
	HWND hWndOamAdd;
	HWND hWndOamRemove;
	HWND hWndCellAdd;
	HWND hWndCellRemove;
	HWND hWndSizeDropdown;
	HWND hWndCellBoundsCheckbox;
} NCERVIEWERDATA;

VOID RegisterNcerViewerClass(VOID);

HWND CreateNcerViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);