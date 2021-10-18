#pragma once
#include <Windows.h>
#include "color.h"
#include "texture.h"
#include "childwindow.h"

typedef struct {
	FRAMEDATA frameData;
	WCHAR szInitialFile[MAX_PATH]; //opened file
	WCHAR szCurrentFile[MAX_PATH]; //save destination
	BOOL hasPalette;
	BOOL isNitro;
	TEXTURE textureData;
	int format;
	int width;
	int height;
	DWORD *px;
	int scale;
	int showBorders;

	int hoverX;
	int hoverY;
	int hoverIndex;
	int contextHoverX;
	int contextHoverY;

	HWND hWnd;
	HWND hWndPreview;
	HWND hWndFormatLabel;
	HWND hWndPaletteLabel;
	HWND hWndEditPalette;
	HWND hWndConvert;
	HWND hWndTileEditor;
	HWND hWndConvertDialog;
	HWND hWndUniqueColors;
	HWND hWndTexelVram;
	HWND hWndPaletteVram;

	HWND hWndFormat;
	HWND hWndPaletteName;
	HWND hWndDither;
	HWND hWndDitherAlpha;
	HWND hWndColorEntries;
	HWND hWndDoConvertButton;
	HWND hWndOptimizationSlider;
	HWND hWndOptimizationLabel;
	HWND hWndFixedPalette;
	HWND hWndPaletteInput;
	HWND hWndPaletteBrowse;

	HWND hWndProgress;

	HWND hWndPaletteEditor;
	DWORD tmpCust[16];
} TEXTUREEDITORDATA;

VOID RegisterTextureEditorClass(VOID);

HWND CreateTextureEditor(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);