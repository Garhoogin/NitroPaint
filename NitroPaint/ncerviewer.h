#pragma once
#include "editor.h"
#include "childwindow.h"
#include "ncer.h"
#include "undo.h"

typedef struct NCERVIEWERDATA_ {
	EDITOR_BASIC_MEMBERS;
	NCER ncer;
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
	int showGuidelines;
	int showObjOutline;
	UNDO undo;

	COLOR32 frameBuffer[256 * 512];

	HWND hWndCellDropdown;
	HWND hWndCharacterOffset;
	HWND hWndPaletteDropdown;
	HWND hWndSizeLabel;
	HWND hWndCharacterOffsetButton;
	HWND hWndOamDropdown;
	HWND hWndCreateCell;
	HWND hWndDuplicateCell;
	HWND hWndMappingMode;
	HWND hWndGuidelines;
	HWND hWndOutlineObj;

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

HWND CreateNcerViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNcerViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCER *ncer);
