// -----------------------------------------------------------------------------------------------
// Copyright (c) 2020, Garhoogin
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// -----------------------------------------------------------------------------------------------


#include "PaletteSwap.h"
#include "editor/textureeditor.h"

extern int ilog2(int x);



typedef struct PaletteSwapEntry_ {
	WCHAR path[MAX_PATH];
	COLOR32 *px;
	unsigned int width;
	unsigned int height;
} PaletteSwapEntry;

typedef struct PaletteSwapData_ {
	PaletteSwapEntry entries[RX_PALETTE_MAX_COUNT];
	unsigned int nEntries;

	HWND hWndCancel;
	HWND hWndOK;
	HWND hWndTextureFormat;
	HWND hWndC0xp;
	HWND hWndType;
	HWND hWndDither;
	HWND hWndDiffuse;
	HWND hWndMaxColors;
	HWND hWndFileLabels[RX_PALETTE_MAX_COUNT];
	HWND hWndPaletteNames[RX_PALETTE_MAX_COUNT];
	HWND hWndUpButtons[RX_PALETTE_MAX_COUNT];
	HWND hWndDownButtons[RX_PALETTE_MAX_COUNT];

	NpBalanceControl balance;
} PaletteSwapData;



typedef struct PaletteSwapParams_ {
	COLOR32 *imgCat;
	int *indices;
	unsigned int width;
	unsigned int height;
	unsigned int nLayers;
	COLOR32 *pltt;
	unsigned int plttSize;
	int texfmt;
	float diffuse;
	int c0xp;
	RxFlag flag;
	const RxBalanceSetting *balance;
	unsigned int *pnUsed;
} PaletteSwapParams;

static DWORD CALLBACK PaletteSwapImpl(LPVOID lpParam) {
	PaletteSwapParams *params = (PaletteSwapParams *) lpParam;

	//create the palette data
	RxReduction *reduction = RxNew(params->balance);
	RxSetPaletteLayers(reduction, params->nLayers);
	RxApplyFlags(reduction, params->flag);
	RxHistAdd(reduction, params->imgCat, params->width, params->height);
	RxHistFinalize(reduction);
	RxComputePalette(reduction, params->plttSize - (params->c0xp ? 1 : 0));
	RxSortPalette(reduction, RX_FLAG_SORT_ONLY_USED | RX_FLAG_SORT_END_DIFFER);

	//read palettes
	unsigned int nUsedColors = reduction->nUsedColors;
	for (unsigned int i = 0; i < params->nLayers; i++) {
		RxGetPalette(reduction, params->pltt + params->plttSize * i, i);
	}
	*params->pnUsed = nUsedColors;

	if (params->texfmt != GX_TEXFMT_A3I5 && params->texfmt != GX_TEXFMT_A5I3) {
		//loading the palette as-is
		RxPaletteLoad(reduction, params->pltt, params->plttSize);
	} else {
		//constructing the pseudo-palette for a3i5/a5i3
		RxApplyFlags(reduction, RX_FLAG_ALPHA_MODE_PALETTE);

		//TODO: this should belong in texconv
		COLOR32 *tmp = (COLOR32 *) calloc(256 * params->nLayers, sizeof(COLOR32));

		unsigned int nColsAlpha = params->texfmt == GX_TEXFMT_A3I5 ? 32 :  8;
		unsigned int nAlpha     = params->texfmt == GX_TEXFMT_A3I5 ?  8 : 32;
		for (unsigned int i = 0; i < params->nLayers; i++) {
			COLOR32 *plttLayer = tmp + i * 256;

			for (unsigned int j = 0; j < nAlpha; j++) {
				COLOR32 *alphaPltt = plttLayer + j * nColsAlpha;
				memcpy(alphaPltt, params->pltt + params->plttSize * i, nColsAlpha * sizeof(COLOR32));

				unsigned int a = j;
				if (params->texfmt == GX_TEXFMT_A3I5) a = GX_A3I5_A3_TO_A5(a);
				a = (a * 510 + 31) / 62;

				for (unsigned int k = 0; k < nColsAlpha; k++) {
					alphaPltt[k] = (alphaPltt[k] & 0x00FFFFFF) | (a << 24);
				}
			}
		}

		RxPaletteLoad(reduction, tmp, 256);
		free(tmp);
	}

	//index the images
	if (params->texfmt != GX_TEXFMT_A3I5 && params->texfmt != GX_TEXFMT_A5I3) {
		RxReduceImage(reduction, params->imgCat, params->indices, params->width, params->height, params->flag, params->diffuse);
	} else {
		RxFlag flag = RX_FLAG_NO_WRITEBACK | RX_FLAG_ALPHA_MODE_PALETTE | RX_FLAG_NO_ALPHA_DITHER;
		RxReduceImage(reduction, params->imgCat, params->indices, params->width, params->height, flag, params->diffuse);
	}

	RxFree(reduction);
	return 0;
}

static void PaletteSwapThreaded(HWND hWnd, COLOR32 *imgCat, int *indices, unsigned int width, unsigned int height, unsigned int nLayers, COLOR32 *pltt, unsigned int plttSize, float diffuse, int texfmt, int c0xp, RxFlag flag, const RxBalanceSetting *balance, unsigned int *pnUsedColors) {

	//modal window
	HWND hWndModal = CreateWindow(L"CompressionProgress", L"Compressing",
		WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 150, hWnd, NULL, NULL, NULL);

	PaletteSwapParams params;
	params.imgCat = imgCat;
	params.indices = indices;
	params.width = width;
	params.height = height;
	params.nLayers = nLayers;
	params.pltt = pltt;
	params.plttSize = plttSize;
	params.texfmt = texfmt;
	params.diffuse = diffuse;
	params.c0xp = c0xp;
	params.flag = flag;
	params.balance = balance;
	params.pnUsed = pnUsedColors;
	HANDLE hThread = CreateThread(NULL, 0, PaletteSwapImpl, &params, 0, NULL);

	DoModalWait(hWndModal, hThread);

	CloseHandle(hThread);
}

static LRESULT CALLBACK PaletteSwapProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	PaletteSwapData *data = (PaletteSwapData *) GetWindowLongPtr(hWnd, 0);

	//data output types for dropdown
	enum {
		TYPE_MULTIPLE_TEXTURES,
		TYPE_SINGLE_TEXTURE,
		TYPE_TEXARC
	};

	//texture formats for dropdown
	enum {
		FMT_PLTT4,
		FMT_PLTT16,
		FMT_PLTT256,
		FMT_A3I5,
		FMT_A5I3
	};

	switch (msg) {
		case WM_CREATE:
		{
			data = (PaletteSwapData *) calloc(1, sizeof(PaletteSwapData));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
			break;
		}
		case NV_SETDATA:
		{
			if (data->nEntries >= RX_PALETTE_MAX_COUNT) break;

			PaletteSwapEntry *ent = (PaletteSwapEntry *) lParam;
			memcpy(&data->entries[data->nEntries], ent, sizeof(PaletteSwapEntry));
			data->nEntries++;
			break;
		}
		case NV_INITIALIZE:
		{
			//create UI
			int fileWidth = 300;
			for (unsigned int i = 0; i < data->nEntries; i++) {
				int y = 10 + i * 27;
				WCHAR plttName[17] = { 0 };
				TexViewerChoosePaletteName(plttName, data->entries[i].path);

				CreateStatic(hWnd, L"File:", 10, y, 30, 22);
				data->hWndFileLabels[i] = CreateStatic(hWnd, data->entries[i].path, 40, y, fileWidth, 22);
				CreateStatic(hWnd, L"Palette Name:", 40 + fileWidth + 5, y, 75, 22);
				data->hWndPaletteNames[i] = CreateEdit(hWnd, plttName, 40 + fileWidth + 5 + 75, y, 100, 22, FALSE);

				data->hWndUpButtons[i] = CreateButton(hWnd, L"\x25B4", 40 + fileWidth + 5 + 75 + 100 + 5, y - 2, 30, 13, FALSE);
				data->hWndDownButtons[i] = CreateButton(hWnd, L"\x25BE", 40 + fileWidth + 5 + 75 + 100 + 5, y - 2 + 13, 30, 13, FALSE);
			}

			//disable the first up arrow and the last down arrow.
			EnableWindow(data->hWndUpButtons[0], FALSE);
			EnableWindow(data->hWndDownButtons[data->nEntries - 1], FALSE);

			//should color 0 be transparent?
			int c0xp = 0;
			for (unsigned int i = 0; i < data->nEntries; i++) {
				COLOR32 *px = data->entries[i].px;
				for (unsigned int j = 0; j < data->entries[i].width * data->entries[i].height; j++) {
					unsigned int a = px[j] >> 24;
					if (a < 0x80) c0xp = 1;
				}
			}

			int panelY = 10 + data->nEntries * 27 - 5 + 10;

			LPCWSTR types[] = { L"Separate Textures", L"Single Texture, Large Palette", L"Texture Archive" };
			LPCWSTR formats[] = { L"palette4", L"palette16", L"palette256", L"a3i5", L"a5i3" };

			CreateStatic(hWnd, L"Data Output:", 10, panelY, 100, 22);
			data->hWndType = CreateCombobox(hWnd, types, 3, 110, panelY, 175, 22, 0);

			CreateStatic(hWnd, L"Texture Format:", 10, panelY + 27, 100, 22);
			data->hWndTextureFormat = CreateCombobox(hWnd, formats, sizeof(formats) / sizeof(formats[0]), 110, panelY + 27, 100, 22, 2);
			data->hWndC0xp = CreateCheckbox(hWnd, L"Color 0 is Transparent", 220, panelY + 27, 150, 22, c0xp);

			data->hWndDither = CreateCheckbox(hWnd, L"Dither:", 380, panelY + 27, 60, 22, FALSE);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", 440, panelY + 27, 50, 22, TRUE);
			EnableWindow(data->hWndDiffuse, FALSE);

			CreateStatic(hWnd, L"Colors per Palette:", 10, panelY + 27 + 27, 100, 22);
			data->hWndMaxColors = CreateEdit(hWnd, L"256", 110, panelY + 27 + 27, 75, 22, TRUE);

			int balanceHeight = 3 * 27 - 5 + 10 + 10 + 10;
			int okY = panelY + 27 + 27 + 32 + balanceHeight + 10;
			int okX = 40 + fileWidth + 5 + 75 + 5 + 30;

			NpCreateBalanceInput(&data->balance, hWnd, 10, panelY + 27 + 27 + 32, okX + 100 - 10);

			data->hWndOK = CreateButton(hWnd, L"OK", okX, okY, 100, 22, TRUE);
			data->hWndCancel = CreateButton(hWnd, L"Cancel", okX - 105, okY, 100, 22, FALSE);

			SetWindowSize(hWnd, okX + 100 + 10, okY + 22 + 10);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int idCtl = LOWORD(wParam);
			int cmd = HIWORD(wParam);
			
			if ((hWndControl == data->hWndOK || idCtl == IDOK) && cmd == BN_CLICKED) {
				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);

				int c0xp = GetCheckboxChecked(data->hWndC0xp);
				int dataType = UiCbGetCurSel(data->hWndType);
				unsigned int nLayers = data->nEntries;

				//get texture format
				int fmtSel = UiCbGetCurSel(data->hWndTextureFormat);
				int texfmt = CT_256COLOR;
				unsigned int maxCols = 256, bpp = 8, translucent = 0;
				switch (fmtSel) {
					case FMT_PLTT4   : texfmt = CT_4COLOR;   maxCols =   4; bpp = 2; translucent = 0; break;
					case FMT_PLTT16  : texfmt = CT_16COLOR;  maxCols =  16; bpp = 4; translucent = 0; break;
					case FMT_PLTT256 : texfmt = CT_256COLOR; maxCols = 256; bpp = 8; translucent = 0; break;
					case FMT_A3I5    : texfmt = CT_A3I5;     maxCols =  32; bpp = 8; translucent = 1; break;
					case FMT_A5I3    : texfmt = CT_A5I3;     maxCols =   8; bpp = 8; translucent = 1; break;
				}

				if (translucent) c0xp = 0; // not applicable

				//dither settings
				int dither = GetCheckboxChecked(data->hWndDither);
				float diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;
				if (!dither) diffuse = 0.0f;

				//get max colors
				unsigned int plttSize = GetEditNumber(data->hWndMaxColors);
				if (plttSize > maxCols) plttSize = maxCols;

				RxBalanceSetting balance;
				NpGetBalanceSetting(&data->balance, &balance);

				RxFlag flag = RX_FLAG_NO_WRITEBACK;
				flag |= RX_FLAG_NO_ALPHA_DITHER; // TODO: user input?

				if (texfmt == CT_A3I5 || texfmt == CT_A5I3) {
					flag |= RX_FLAG_ALPHA_MODE_PIXEL;
				} else if (c0xp) {
					flag |= RX_FLAG_ALPHA_MODE_RESERVE;
				} else {
					flag |= RX_FLAG_ALPHA_MODE_NONE;
				}

				unsigned int width = data->entries[0].width, height = data->entries[0].height;
				unsigned int padWidth = 1, padHeight = 1;
				while (padHeight < height) padHeight <<= 1;
				while (padWidth  < width)  padWidth  <<= 1;

				unsigned int nPx = width * height;

				//the alpha levels cannot differ across images, so we will take the mean alpha across
				//images. When one image has an alpha of zero, we must take it to be zero across all
				//layers.
				for (unsigned int i = 0; i < nPx; i++) {
					unsigned int aTotal = 0;
					int transparent = 0;

					for (unsigned int j = 0; j < nLayers; j++) {
						unsigned int a = data->entries[j].px[i] >> 24;
						aTotal += a;

						if (a == 0) transparent = 1;
					}

					if (transparent) aTotal = 0;
					unsigned int a = (aTotal * 2 + nLayers) / (nLayers * 2);

					//write across images
					for (unsigned int j = 0; j < nLayers; j++) {
						data->entries[j].px[i] = (data->entries[j].px[i] & 0x00FFFFFF) | (a << 24);
					}
				}

				//concatenate image buffers
				COLOR32 *imgCat = (COLOR32 *) calloc(nPx * nLayers, sizeof(COLOR32));
				for (unsigned int i = 0; i < nLayers; i++) {
					memcpy(imgCat + i * nPx, data->entries[i].px, nPx * sizeof(COLOR32));
				}

				//create the palette data.
				COLOR32 *pltt = (COLOR32 *) calloc(256 * nLayers, sizeof(COLOR32));
				int *indices = (int *) calloc(width * height, sizeof(int));

				unsigned int nUsedColors;
				PaletteSwapThreaded(hWnd, imgCat, indices, width, height, nLayers, pltt, plttSize, diffuse, texfmt, c0xp, flag, &balance, &nUsedColors);

				free(imgCat);

				//create the texel data
				unsigned int texelSize = (padWidth * padHeight * bpp) / 8;
				unsigned char *texel = (unsigned char *) calloc(texelSize, 1);

				unsigned int pxPerByte = 8 / bpp;
				for (unsigned int i = 0; i < nPx; i++) {
					unsigned char icol = (unsigned char) indices[i];

					unsigned int iPx = i / pxPerByte;
					unsigned int shift = (i % pxPerByte) * bpp;
					texel[iPx] |= icol << shift;
				}
				free(indices);

				//compute the TEXIMAGE_PARAM
				uint32_t texImageParam = 0;
				if (c0xp) texImageParam |= (1 << 29);
				texImageParam |= (1 << 17) | (1 << 16);
				texImageParam |= (ilog2(padWidth >> 3) << 20) | (ilog2(padHeight >> 3) << 23);
				texImageParam |= texfmt << 26;

				switch (dataType) {
					case TYPE_MULTIPLE_TEXTURES:
					{
						//create multiple textures
						for (unsigned int i = 0; i < nLayers; i++) {

							TextureObject *tex = (TextureObject *) ObjAlloc(FILE_TYPE_TEXTURE, NpGetTextureFormatForPreset());

							wchar_t *plttName = UiEditGetText(data->hWndPaletteNames[i]);

							unsigned char *texelCopy = malloc(texelSize);
							memcpy(texelCopy, texel, texelSize);

							tex->texture.texels.name = _strdup("pswap");
							tex->texture.texels.texImageParam = texImageParam;
							tex->texture.texels.height = height;
							tex->texture.texels.texel = texelCopy;

							tex->texture.palette.name = TexNarrowResourceNameFromWideChar(plttName);
							tex->texture.palette.nColors = plttSize;
							tex->texture.palette.pal = (COLOR *) calloc(plttSize, sizeof(COLOR));
							for (unsigned int j = 0; j < plttSize; j++) {
								tex->texture.palette.pal[j] = ColorConvertToDS(pltt[i * plttSize + j]);
							}
							free(plttName);

							NpOpenObject(hWndMain, &tex->header);
						}

						free(texel);

						break;
					}
					case TYPE_SINGLE_TEXTURE:
					{
						//create one texture with one large palette
						TextureObject *tex = (TextureObject *) ObjAlloc(FILE_TYPE_TEXTURE, NpGetTextureFormatForPreset());

						wchar_t *plttName = UiEditGetText(data->hWndPaletteNames[0]);

						tex->texture.texels.name = _strdup("pswap");
						tex->texture.texels.texImageParam = texImageParam;
						tex->texture.texels.height = height;
						tex->texture.texels.texel = texel;
						
						tex->texture.palette.name = TexNarrowResourceNameFromWideChar(plttName);
						tex->texture.palette.nColors = plttSize * data->nEntries;
						tex->texture.palette.pal = (COLOR *) calloc(plttSize * data->nEntries, sizeof(COLOR));
						for (unsigned int i = 0; i < plttSize * nLayers; i++) {
							tex->texture.palette.pal[i] = ColorConvertToDS(pltt[i]);
						}

						free(plttName);

						NpOpenObject(hWndMain, &tex->header);

						break;
					}
					case TYPE_TEXARC:
					{
						//create one texture archive
						TexArc *texarc = (TexArc *) ObjAlloc(FILE_TYPE_NSBTX, NSBTX_TYPE_NNS);

						texarc->nTextures = 1;
						texarc->nPalettes = data->nEntries;

						//put texture
						texarc->textures = (TEXELS *) calloc(1, sizeof(TEXELS));
						texarc->textures[0].name = _strdup("pswap");
						texarc->textures[0].texImageParam = texImageParam;
						texarc->textures[0].height = height;
						texarc->textures[0].texel = texel;

						//put palettes
						texarc->palettes = (PALETTE *) calloc(data->nEntries, sizeof(PALETTE));
						for (unsigned int i = 0; i < nLayers; i++) {
							wchar_t *plttName = UiEditGetText(data->hWndPaletteNames[i]);

							texarc->palettes[i].name = TexNarrowResourceNameFromWideChar(plttName);
							texarc->palettes[i].nColors = plttSize;
							texarc->palettes[i].pal = (COLOR *) calloc(plttSize, sizeof(COLOR));
							free(plttName);

							//put palette colors
							for (unsigned int j = 0; j < plttSize; j++) {
								texarc->palettes[i].pal[j] = ColorConvertToDS(pltt[i * plttSize + j]);
							}
						}

						NpOpenObject(hWndMain, &texarc->header);
						break;
					}
				}

				//last: to report the created palette, calculate the number of shared palette colors.
				unsigned int nShared = 0;
				for (unsigned int i = 0; i < nUsedColors; i++) {
					int shared = 1;

					for (unsigned int j = 1; j < nLayers; j++) {
						if (pltt[j * plttSize + i] != pltt[(j - 1) * plttSize + i]) shared = 0;
					}

					if (shared) nShared++;
				}

				free(pltt);

				SendMessage(hWnd, WM_CLOSE, 0, 0);

				//user info message (after dialog closure)
				wchar_t infobuf[64];
				wsprintfW(infobuf, L"Created %d palettes. %d colors used, %d colors shared.", nLayers,
					nUsedColors, nShared);
				MessageBox(hWndMain, infobuf, L"Create Palette Swap", MB_ICONINFORMATION);

			} else if ((hWndControl == data->hWndCancel || idCtl == IDCANCEL) && cmd == BN_CLICKED) {
				//cancel dialog
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if (hWndControl == data->hWndType && cmd == CBN_SELCHANGE) {
				int sel = UiCbGetCurSel(hWndControl);

				int enablePaletteNames = 1;
				if (sel == TYPE_SINGLE_TEXTURE) enablePaletteNames = 0; // in "Large palette" mode, only one palette name exists

				for (unsigned int i = 1; i < data->nEntries; i++) {
					EnableWindow(data->hWndPaletteNames[i], enablePaletteNames);
				}

			} else if (hWndControl == data->hWndDither && cmd == BN_CLICKED) {
				int en = GetCheckboxChecked(hWndControl);
				EnableWindow(data->hWndDiffuse, en);
			} else if (hWndControl == data->hWndTextureFormat && cmd == CBN_SELCHANGE) {
				//texture format changed
				int selIdx = UiCbGetCurSel(hWndControl);

				static const unsigned char texfmts[] = {
					GX_TEXFMT_PLTT4,
					GX_TEXFMT_PLTT16,
					GX_TEXFMT_PLTT256,
					GX_TEXFMT_A3I5,
					GX_TEXFMT_A5I3
				};
				int texfmt = texfmts[selIdx];
				
				int nMaxCols = 256;
				switch (texfmt) {
					case GX_TEXFMT_PLTT4   : nMaxCols =   4; break;
					case GX_TEXFMT_PLTT16  : nMaxCols =  16; break;
					case GX_TEXFMT_PLTT256 : nMaxCols = 256; break;
					case GX_TEXFMT_A3I5    : nMaxCols =  32; break;
					case GX_TEXFMT_A5I3    : nMaxCols =   8; break;
				}
				SetEditNumber(data->hWndMaxColors, nMaxCols);

				if (texfmt == GX_TEXFMT_A5I3 || texfmt == GX_TEXFMT_A3I5) {
					EnableWindow(data->hWndC0xp, FALSE);
				} else {
					EnableWindow(data->hWndC0xp, TRUE);
				}
			} else {

				//check the up/down buttons
				for (unsigned int i = 0; i < data->nEntries - 1; i++) {
					if ((hWndControl == data->hWndDownButtons[i] || hWndControl == data->hWndUpButtons[i + 1]) && cmd == BN_CLICKED) {
						//command to move item i down and item i+1 up

						//swap data in struct
						PaletteSwapEntry temp;
						memcpy(&temp, &data->entries[i], sizeof(temp));
						memcpy(&data->entries[i], &data->entries[i + 1], sizeof(temp));
						memcpy(&data->entries[i + 1], &temp, sizeof(temp));

						//swap the UI items
						SendMessage(data->hWndFileLabels[i], WM_SETTEXT, -1, (LPARAM) data->entries[i].path);
						SendMessage(data->hWndFileLabels[i + 1], WM_SETTEXT, -1, (LPARAM) data->entries[i + 1].path);

						//swap palette names
						wchar_t *pltt1 = UiEditGetText(data->hWndPaletteNames[i]);
						wchar_t *pltt2 = UiEditGetText(data->hWndPaletteNames[i + 1]);
						UiEditSetText(data->hWndPaletteNames[i + 1], pltt1);
						UiEditSetText(data->hWndPaletteNames[i], pltt2);
						free(pltt1);
						free(pltt2);

						break;
					}
				}

			}

			break;
		}
		case WM_DESTROY:
		{
			free(data);
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) NULL);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}



void PaletteSwapDialog(HWND hWnd) {
	LPWSTR paths = openFilesDialog(hWnd, L"Select Images", FILTER_IMAGE, L"");
	if (paths == NULL) return;

	wchar_t pathbuf[MAX_PATH + 1];
	int nPaths = getPathCount(paths);

	//check image dimensions are all equal
	int width = 0, height = 0, invalid = 0;
	for (int i = 0; i < nPaths; i++) {
		getPathFromPaths(paths, i, pathbuf);

		int width2, height2;
		COLOR32 *px = ImgRead(pathbuf, &width2, &height2);
		free(px);

		if (i == 0) {
			width = width2;
			height = height2;
		} else {
			if (width != width2 || height != height2) {
				invalid = 1;
				break;
			}
		}
	}

	//if the dimensions were invalid, report an error
	if (invalid) {
		MessageBox(hWnd, L"The images must all have equal dimension.", L"Create Palette Swap", MB_ICONERROR);
		free(paths);
		return;
	}

	//image dimensions must be valid texture image dimensions
	if (!TxDimensionIsValid(width) || (height > 1024)) {
		MessageBox(hWnd, L"Textures must have dimensions as powers of two greater than or equal to 8, and not exceeding 1024.",
			L"Create Palette Swap", MB_ICONERROR);
		free(paths);
		return;
	}

	//checks succeed, create palette swap dialog

	//dialog structure:
	//  [ File: C:\...\image1.png   Palette Name: image1_pl    ^ v ]
	//

	HWND h = CreateWindow(L"PaletteSwapClass", L"Create Palette Swap",
		WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWnd, NULL, NULL, NULL);

	for (int i = 0; i < nPaths; i++) {
		PaletteSwapEntry ent = { 0 };
		getPathFromPaths(paths, i, ent.path);

		ent.px = ImgRead(ent.path, &ent.width, &ent.height);
		SendMessage(h, NV_SETDATA, 0, (LPARAM) &ent);
	}
	SendMessage(h, NV_INITIALIZE, 0, 0);
	DoModal(h);

	free(paths);
}


void RegisterPaletteSwapClass(void) {
	RegisterGenericClass(L"PaletteSwapClass", PaletteSwapProc, sizeof(LPVOID));
}
