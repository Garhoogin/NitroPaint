#pragma once
#include <Windows.h>
#include "nclr.h"

typedef struct {
	int contentWidth;
	int contentHeight;
	int paddingLeft;
	int paddingTop;
	int paddingRight;
	int paddingBottom;
	int allowClear;
	int sizeLevel;
	WCHAR szOpenFile[MAX_PATH];
	NCLR nclr;
	int hoverX;
	int hoverY;
	int contextHoverX;
	int contextHoverY;
	int hoverIndex;

	POINT dragStart; //in client coordinates, scroll transformed
	POINT dragPoint; //in client coordinates, scroll transformed
	int mouseDown;
	int dragging;
	int draggingIndex;
	int rowDragging;

	COLORREF tmpCust[16];
} NCLRVIEWERDATA;

VOID RegisterNclrViewerClass(VOID);

HWND CreateNclrViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);