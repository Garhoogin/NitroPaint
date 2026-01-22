#pragma once
#include <Windows.h>
#include "color.h"
#include "texture.h"
#include "childwindow.h"
#include "editor.h"
#include "tilededitor.h"
#include "framebuffer.h"

typedef struct {
	EDITOR_BASIC_MEMBERS;
	TextureObject *texture;

	TedData ted;

	WCHAR szInitialFile[MAX_PATH]; //source image file
	BOOL hasPalette;
	BOOL isNitro;

	COLOR32 *px;
	int width;
	int height;

	HWND hWndEditPalette;
	HWND hWndConvert;
	HWND hWndTileEditor;
	HWND hWndConvertDialog;
	HWND hWndExportNTF;
	HWND hWndStatus;

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
	NpBalanceControl balance;
	HWND hWndPaletteSize;
	HWND hWndLimitPalette;
	HWND hWndColor0Transparent;
	HWND hWndCheckboxAlphaKey;
	HWND hWndSelectAlphaKey;
	COLOR32 alphaKey;

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

WCHAR *TexNarrowResourceNameToWideChar(const char *name);
char *TexNarrowResourceNameFromWideChar(const WCHAR *name);

void RegisterTextureEditorClass(void);

HWND CreateTextureEditor(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateTextureEditorImmediate(int x, int y, int width, int height, HWND hWndParent, TEXTURE *texture);

int BatchTextureDialog(HWND hWndParent);

void BatchTexShowVramStatistics(HWND hWnd, LPCWSTR convertedDir);
