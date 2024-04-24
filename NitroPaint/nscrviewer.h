#pragma once
#include <Windows.h>
#include "editor.h"
#include "childwindow.h"
#include "nscr.h"
#include "framebuffer.h"

typedef struct {
	EDITOR_BASIC_MEMBERS;
	NSCR nscr;
	int transparent;

	FrameBuffer fb;
	HWND hWndTileEditor;
	HWND hWndPreview;
	HWND hWndCharacterLabel;
	HWND hWndCharacterNumber;
	HWND hWndPaletteLabel;
	HWND hWndPaletteNumber;
	HWND hWndApply;
	HWND hWndAdd;
	HWND hWndSubtract;
	HWND hWndTileBaseLabel;
	HWND hWndTileBase;
	HWND hWndSize;
	HWND hWndSelectionSize;

	int hoverX;
	int hoverY;
	int contextHoverX;
	int contextHoverY;
	int editingX;
	int editingY;
	int hlStart;
	int hlEnd;
	int hlMode;
	int verifyFrames;
	int tileBase;

	int mouseDown;
	int selStartX; //-1 when no selection.
	int selStartY;
	int selEndX;
	int selEndY;
} NSCRVIEWERDATA;

void NscrViewerSetTileBase(HWND hWnd, int tileBase);

VOID RegisterNscrViewerClass(VOID);

HWND CreateNscrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNscrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NSCR *nscr);
