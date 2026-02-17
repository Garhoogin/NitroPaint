#pragma once
#include <Windows.h>
#include "editor.h"
#include "nclr.h"
#include "palops.h"

//edit modes
#define PALVIEWER_MODE_EDIT       0
#define PALVIEWER_MODE_SELECTION  1

//selection modes
#define PALVIEWER_SELMODE_1D      0
#define PALVIEWER_SELMODE_2D      1

int PalViewerIndexInRange(int index, int start, int end, int is2d);

typedef struct {
	EDITOR_BASIC_MEMBERS;
	NCLR *nclr;
	int editMode;
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
	int preserveDragging; //Shift+Drag, can combine with row drag for screen.
	int rowSelection;     //Ctrl+Drag when making a selection, selects whole rows.

	COLOR *tempPalette; //used and discarded
	PAL_OP palOp;       //stores current & last palette operation
	int palOpDialog;    //0 if no palette operation in progress

	int makingSelection;//is making selection
	int movingSelection;//is moving selection
	int selStart;       //color index of selection start (inclusive)
	int selEnd;         //color index of selection end (inclusive)
	int selMode;        //selection mode

	COLORREF tmpCust[16];

	HWND hWndFileInput;
	HWND hWndBrowse;
	HWND hWndReserve;
	HWND hWndColors;
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;
	HWND hWndGenerate;

	HWND hWndEditCompressionCheckbox;
	HWND hWndEditCompressionList;
	HWND hWndEditCompressionOK;
} NCLRVIEWERDATA;

VOID CopyPalette(COLOR *palette, int nColors);

VOID PastePalette(COLOR *dest, int nMax);

VOID RegisterNclrViewerClass(VOID);

HWND CreateNclrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCLR *nclr);
