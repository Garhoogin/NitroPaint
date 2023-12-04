#pragma once
#include <Windows.h>
#include "color.h"
#include "texture.h"
#include "childwindow.h"
#include "editor.h"

typedef struct {
	EDITOR_BASIC_MEMBERS;
	TextureObject texture;

	WCHAR szInitialFile[MAX_PATH]; //source image file
	BOOL hasPalette;
	BOOL isNitro;

	COLOR32 *px;
	int width;
	int height;

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
	HWND hWndExportNTF;

	HWND hWndFormat;
	HWND hWndPaletteName;
	HWND hWndDither;
	HWND hWndDiffuseAmount;
	HWND hWndDitherAlpha;
	HWND hWndColorEntries;
	HWND hWndDoConvertButton;
	HWND hWndOptimizationSlider;
	HWND hWndOptimizationLabel;
	HWND hWndFixedPalette;
	HWND hWndPaletteInput;
	HWND hWndPaletteBrowse;
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;
	HWND hWndPaletteSize;
	HWND hWndLimitPalette;

	HWND hWndProgress;

	HWND hWndPaletteEditor;
	DWORD tmpCust[16];

	//tile editor
	int selectedColor;
	int selectedAlpha;
	int tileMouseDown;

	HWND hWndInterpolate;
	HWND hWndTransparent;
	HWND hWndPaletteBase;
} TEXTUREEDITORDATA;

VOID RegisterTextureEditorClass(VOID);

HWND CreateTextureEditor(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateTextureEditorImmediate(int x, int y, int width, int height, HWND hWndParent, TEXTURE *texture);

int BatchTextureDialog(HWND hWndParent);

void BatchTexShowVramStatistics(HWND hWnd, LPCWSTR convertedDir);
