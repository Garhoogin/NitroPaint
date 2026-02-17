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

	COLOR32 *px;
	int width;
	int height;

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

	//palette editor
	int highlightStart;
	int highlightLength;

	HWND hWndInterpolate;
	HWND hWndTransparent;
	HWND hWndPaletteBase;
} TEXTUREEDITORDATA;

wchar_t *TexNarrowResourceNameToWideChar(const char *name);
char *TexNarrowResourceNameFromWideChar(const wchar_t *name);

void RegisterTextureEditorClass(void);

int TexViewerIsConverted(TEXTUREEDITORDATA *data);

HWND CreateTextureEditorFromUnconverted(int x, int y, int width, int height, HWND hWndParent, const unsigned char *buffer, unsigned int size);

HWND CreateTextureEditorImmediate(int x, int y, int width, int height, HWND hWndParent, TextureObject *texture);

int BatchTextureDialog(HWND hWndParent);

void BatchTexShowVramStatistics(HWND hWnd, LPCWSTR convertedDir);
