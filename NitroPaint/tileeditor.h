#pragma once
#include <Windows.h>

typedef struct {
	int tileX;
	int tileY;
	int selectedColor;
	int mouseDown;
} TILEEDITORDATA;

HWND CreateTileEditor(int x, int y, int width, int height, HWND hWndParent, int tileX, int tileY);

VOID RegisterTileEditorClass(VOID);