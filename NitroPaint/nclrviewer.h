#pragma once
#include <Windows.h>
#include "editor.h"
#include "nclr.h"
#include "palops.h"

typedef struct {
	EDITOR_BASIC_MEMBERS;
	NCLR nclr;
	int hoverX;
	int hoverY;
	int contextHoverX;
	int contextHoverY;
	int hoverIndex;
	int showFrequency;
	int showUnused;

	POINT dragStart; //in client coordinates, scroll transformed
	POINT dragPoint; //in client coordinates, scroll transformed
	int mouseDown;
	int dragging;
	int draggingIndex;
	int rowDragging;
	int preserveDragging; //Shift+Drag, can combine with row drag for screen.
	COLOR *tempPalette; //used and discarded
	PAL_OP palOp;       //stores current & last palette operation
	int palOpDialog;    //0 if no palette operation in progress

	COLORREF tmpCust[16];

	HWND hWndFileInput;
	HWND hWndBrowse;
	HWND hWndReserve;
	HWND hWndColors;
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;
	HWND hWndGenerate;
} NCLRVIEWERDATA;

VOID CopyPalette(COLOR *palette, int nColors);

VOID PastePalette(COLOR *dest, int nMax);

VOID RegisterNclrViewerClass(VOID);

HWND CreateNclrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNclrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCLR *nclr);
