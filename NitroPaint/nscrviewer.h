#pragma once
#include <Windows.h>
#include "editor.h"
#include "childwindow.h"
#include "nscr.h"
#include "framebuffer.h"
#include "tilededitor.h"

typedef struct {
	EDITOR_BASIC_MEMBERS;
	NSCR nscr;
	int transparent;

	TedData ted;

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

	int hlStart;
	int hlEnd;
	int hlMode;
	int verifyFrames;
	int tileBase;
} NSCRVIEWERDATA;

void NscrViewerSetTileBase(HWND hWnd, int tileBase);

VOID RegisterNscrViewerClass(VOID);

HWND CreateNscrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNscrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NSCR *nscr);
