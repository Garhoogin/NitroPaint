#pragma once
#include <Windows.h>

typedef struct {
	int tileIndex;
	int selectedColor;
	int mouseDown;
} TILEEDITORDATA;

HWND CreateTileEditor(int x, int y, int width, int height, HWND hWndParent, int tileNo);

VOID RegisterTileEditorClass(VOID);