#pragma once

#include "texture.h"
#include "palette.h"
#include "texconv.h"

#include <Windows.h>

typedef struct TexconvDialogParam_ {
	TxConversionParameters param;  // the texture conversion parameters

	//alpha key info
	int useAlphaKey;
	COLOR32 alphaKey;
	COLORREF *pCustomColors;

	//palette info
	wchar_t *fixedPalettePath;  // file path of the fixed palette
	int noWritePalette;         // prevents the dialog from allowing the palette to be written

	//4x4 info
	int limitPaletteSize;       // limit 4x4 palette size
	int maxColors;              // 4x4 max colors
} TexconvDialogParam;

int TexconvDialog(HWND hWnd, TexconvDialogParam *param);
