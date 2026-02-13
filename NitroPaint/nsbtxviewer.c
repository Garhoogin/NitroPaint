#include <Windows.h>
#include <CommCtrl.h>
#include <stdio.h>
#include <Shlobj.h>

#include "nsbtxviewer.h"
#include "textureeditor.h"
#include "nitropaint.h"
#include "resource.h"
#include "ui.h"
#include "editor.h"
#include "gdip.h"
#include "nclr.h"

extern HICON g_appIcon;

extern size_t my_strnlen(const char *_Str, size_t _MaxCount);
extern size_t my_wcsnlen(const wchar_t *_Str, size_t _MaxCount);
#define strnlen my_strnlen
#define wcsnlen my_wcsnlen


static HBITMAP renderTexture(TEXELS *texture, PALETTE *palette, int zoom) {
	if (texture == NULL) return NULL;
	int width = TEXW(texture->texImageParam);
	int height = texture->height;
	COLOR32 *px = (COLOR32 *) calloc(width * zoom * height * zoom, 4);
	TxRender(px, texture, palette);
	ImgSwapRedBlue(px, width, height);

	//perform alpha blending
	int scaleWidth = width * zoom, scaleHeight = height * zoom;
	for (int yDest = scaleHeight - 1; yDest >= 0; yDest--) {
		int y = yDest / zoom;
		for (int xDest = scaleWidth - 1; xDest >= 0; xDest--) {
			int x = xDest / zoom;

			COLOR32 pixel = px[x + y * width];
			int a = pixel >> 24;
			if (a != 255) {
				int s = ((xDest >> 3) ^ (yDest >> 3)) & 1;
				int shades[] = {255, 192};
				int shade = shades[s];
				int r = pixel & 0xFF;
				int g = (pixel >> 8) & 0xFF;
				int b = (pixel >> 16) & 0xFF;

				r = (r * a + shade * (255 - a)) / 255;
				g = (g * a + shade * (255 - a)) / 255;
				b = (b * a + shade * (255 - a)) / 255;

				px[xDest + yDest * scaleWidth] = r | (g << 8) | (b << 16) | 0xFF000000;
			} else {
				px[xDest + yDest * scaleWidth] = pixel;
			}
		}
	}
	
	HBITMAP hBitmap = CreateBitmap(width * zoom, height * zoom, 1, 32, px);
	free(px);
	return hBitmap;
}

static int levenshtein(const char *str1, int l1, const char *str2, int l2, int dst, int maxDst) {
	if (dst >= maxDst) return maxDst;
	while (1) {
		if (l1 == 0) return l2;
		if (l2 == 0) return l1;

		char c1 = *str1, c2 = *str2;
		if (c1 >= 'a' && c1 <= 'z') c1 = c1 + 'A' - 'a';
		if (c2 >= 'a' && c2 <= 'z') c2 = c2 + 'A' - 'a';

		if (c1 != c2) {
			int m1 = levenshtein(str1 + 1, l1 - 1, str2, l2, dst + 1, maxDst);
			int m2 = levenshtein(str1, l1, str2 + 1, l2 - 1, dst + 1, min(maxDst, m1));
			int m3 = levenshtein(str1 + 1, l1 - 1, str2 + 1, l2 - 1, dst + 1, min(maxDst, m2));
			return min(min(m1, m2), m3) + 1;
		}

		str1++;
		str2++;
		l1--;
		l2--;
	}
}

static int calculateHighestPaletteIndex(TEXELS *texture) {
	int format = FORMAT(texture->texImageParam);
	if (format == CT_DIRECT) return -1;

	int nHighest = 0;
	int texelSize = TxGetTexelSize(TEXW(texture->texImageParam), texture->height, texture->texImageParam);
	switch (format) {
		case CT_4COLOR:
			for (int i = 0; i < texelSize; i++) {
				BYTE b = texture->texel[i];
				for (int j = 0; j < 4; j++) {
					int n = (b >> (j * 2)) & 3;
					if (n > nHighest) nHighest = n;
				}
			}
			break;
		case CT_16COLOR:
			for (int i = 0; i < texelSize; i++) {
				BYTE b = texture->texel[i];
				for (int j = 0; j < 2; j++) {
					int n = (b >> (j * 4)) & 0xF;
					if (n > nHighest) nHighest = n;
				}
			}
			break;
		case CT_256COLOR:
		case CT_A3I5:
		case CT_A5I3:
		{
			int idxMask = 0xFF;
			if (format == CT_A3I5) idxMask = 0x1F;
			else if (format == CT_A5I3) idxMask = 0x7;
			for (int i = 0; i < texelSize; i++) {
				BYTE b = texture->texel[i] & idxMask;
				if (b > nHighest) nHighest = b;
			}
			break;
		}
		case CT_4x4:
		{
			//find highest pidx
			int idxSize = texelSize / 4;
			for (int i = 0; i < idxSize; i++) {
				unsigned short pidx = texture->cmp[i];
				int mode = (pidx >> 14) & 0x3;
				int nColors = (mode == 0 || mode == 2) ? 4 : 2;
				int palOffset = (pidx & 0x3FFF) << 1;
				int idx = palOffset + nColors - 1;
				if (idx > nHighest) nHighest = idx;
			}
			break;
		}
	}
	return nHighest;
}

static int guessTexPlttByName(const char *textureName, char **paletteNames, int nPalettes, TEXELS *texture, PALETTE *palettes) {
	int format = FORMAT(texture->texImageParam);
	if (format == CT_DIRECT) return -1;

	//first gueses: Same name (case insensitive)?
	for (int i = 0; i < nPalettes; i++) {
		if (_stricmp(textureName, paletteNames[i]) == 0) return i;
	}

	int texNameLen = strnlen(textureName, 16);

	//second guess: add "_pl" to texture name (and truncate to 16)
	char nameBuffer[20];
	memcpy(nameBuffer, textureName, texNameLen);
	nameBuffer[texNameLen] = '\0';
	memcpy(nameBuffer + strlen(nameBuffer), "_pl", 4);
	nameBuffer[16] = '\0';

	for (int i = 0; i < nPalettes; i++) {
		if (_stricmp(nameBuffer, paletteNames[i]) == 0) return i;
	}

	//third guess: same as last but truncate to 15 (sometimes names are only 15 characters?)
	nameBuffer[15] = '\0';
	for (int i = 0; i < nPalettes; i++) {
		if (_stricmp(nameBuffer, paletteNames[i]) == 0) return i;
	}

	//fourth guess: add "_pl" after truncating texture name length to 13
	memcpy(nameBuffer, textureName, texNameLen);
	nameBuffer[texNameLen] = '\0';
	nameBuffer[13] = '\0';
	memcpy(nameBuffer + strlen(nameBuffer), "_pl", 4);
	for (int i = 0; i < nPalettes; i++) {
		if (_stricmp(nameBuffer, paletteNames[i]) == 0) return i;
	}

	//fifth guess: same thing but truncate texture name length to 12
	memcpy(nameBuffer, textureName, texNameLen);
	nameBuffer[texNameLen] = '\0';
	nameBuffer[12] = '\0';
	memcpy(nameBuffer + strlen(nameBuffer), "_pl", 4);
	for (int i = 0; i < nPalettes; i++) {
		if (_stricmp(nameBuffer, paletteNames[i]) == 0) return i;
	}
	
	//do a search for the "best-conforming" palette. Palette must use at least the number of colors the texture references.
	//For paletted textures (not 4x4 and not direct), do not allow for more colors than is standard for the format. 
	unsigned char *valids = (unsigned char *) calloc(nPalettes, 1);
	for (int i = 0; i < nPalettes; i++) {
		int nColors = palettes[i].nColors;

		//test for too many colors
		if (nColors > 8 && format == CT_4COLOR) continue;
		else if (nColors > 16 && format == CT_16COLOR) continue;
		else if (nColors > 256 && format == CT_256COLOR) continue;
		else if (nColors > 256 && (format == CT_A3I5 || format == CT_A5I3)) continue; //OPTPiX quirk

		//test for not enough colors
		int nHighest = calculateHighestPaletteIndex(texture);
		if (nHighest >= nColors) continue;

		//if it's a 4x4 texture, allow some wiggle room.
		if (format == CT_4x4 && nHighest + 7 < nColors) continue;
		
		valids[i] = 1;
	}

	//if only one palette is valid, then return it.
	int nMatched = 0, lastMatch = -1;
	for (int i = 0; i < nPalettes; i++) {
		if (valids[i]) {
			nMatched++;
			lastMatch = i;
		}
	}
	if (nMatched == 1) {
		free(valids);
		return lastMatch;
	}

	//well, nothing's worked so far. Let's try using Levenshtein distance to be our best guess.
	int bestDistance = 0x7FFFFFFF, bestIndex = -1;
	for (int i = 0; i < nPalettes; i++) {
		if (!valids[i]) continue;

		int dst = levenshtein(textureName, strlen(textureName), paletteNames[i], strlen(paletteNames[i]), 0, bestDistance);
		if (dst < bestDistance) {
			bestDistance = dst;
			bestIndex = i;
		}
	}

	free(valids);
	if (bestIndex != -1) {
		return bestIndex;
	}

	//still here? just guess
	return 0;
}

LRESULT CALLBACK ListboxDeleteSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT subclass, DWORD_PTR ref) {
	if (msg == WM_KEYDOWN && wParam == VK_DELETE) {
		HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
		int sel = GetListBoxSelection(hWnd);
		RemoveListBoxItem(hWnd, sel);
		SendMessage(hWndParent, NV_CHILDNOTIF, sel, (LPARAM) hWnd);
	}
	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void CreateVramUseWindow(HWND hWndParent, TexArc *nsbtx);

static void ConstructResourceNameFromFilePath(LPCWSTR path, char **out) {
	LPCWSTR name = GetFileName(path);
	const WCHAR *lastDot = wcsrchr(name, L'.');

	unsigned int len = wcsnlen(name, 16);
	if (lastDot != NULL && len > (unsigned int) (lastDot - name)) len = lastDot - name;
	*out = (char *) calloc(len + 1, 1);

	for (unsigned int i = 0; i < len; i++) {
		(*out)[i] = (char) name[i];
	}
}

static int TexarcViewerPromptTexImage(NSBTXVIEWERDATA *data, TEXELS *texels, PALETTE *palette) {
	LPWSTR filter = FILTER_TEXTURE FILTER_IMAGE FILTER_PALETTE FILTER_ALLFILES;
	LPWSTR path = openFileDialog(data->hWnd, L"Select Texture Image", filter, L"tga");
	if (path == NULL) return 0;

	//read texture
	int s = TxReadFileDirect(texels, palette, path);
	if (!s) {
		//success: return
		free(path);
		return 1;
	}

	//invalid NNS TGA. try reading as an image file and convert on the spot.
	int succeeded = 0;
	COLOR32 *px;
	int bWidth = 0, bHeight = 0;
	px = ImgRead(path, &bWidth, &bHeight);
	if (px == NULL || bWidth == 0 || bHeight == 0) {
		//invalid NNS TGA and image. Try reading a palette.
		//MessageBox(data->hWnd, L"Invalid NNS TGA.", L"Invalid NNS TGA", MB_ICONERROR);
		//succeeded = 0;
		memset(texels, 0, sizeof(TEXELS));
		texels->texImageParam = 0; //no texture

		NCLR nclr;
		int s = PalReadFile(&nclr, path);
		if (s) {
			//invalid file.
			MessageBox(data->hWnd, L"Invalid texture, image, or palette file.", L"Invalid File", MB_ICONERROR);
			succeeded = 0;
		} else {
			//copy out
			palette->nColors = nclr.nColors;
			palette->pal = (COLOR *) calloc(palette->nColors, sizeof(COLOR));
			memcpy(palette->pal, nclr.colors, palette->nColors * sizeof(COLOR));

			//name
			ConstructResourceNameFromFilePath(path, &palette->name);
			
			succeeded = 1;
			ObjFree(&nclr.header);
		}
	} else if (!TxDimensionIsValid(bWidth) || (bWidth > 1024 || bHeight > 1024)) {
		//invalid dimension.
		MessageBox(data->hWnd, L"Textures must have dimensions as powers of two greater than or equal to 8, and not exceeding 1024.", L"Invalid dimensions", MB_ICONERROR);
		succeeded = 0;
		free(px);
	} else {
		//must be converted.
		NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
		HWND hWndMdi = nitroPaintStruct->hWndMdi;

		free(px); //for validation

		//create temporary texture editor
		SendMessage(hWndMdi, WM_SETREDRAW, FALSE, 0);
		int verySmall = -(CW_USEDEFAULT >> 1); //(smallest power of 2 that won't be picked up as CW_USEDEFAULT)
		HWND hWndTextureEditor = CreateTextureEditor(verySmall, verySmall, 0, 0, hWndMdi, path);
		ShowWindow(hWndTextureEditor, SW_HIDE);
		TEXTUREEDITORDATA *teData = (TEXTUREEDITORDATA *) EditorGetData(hWndTextureEditor);
		
		//open conversion dialog
		HWND hWndConvertDialog = CreateWindow(L"ConvertDialogClass", L"Convert Texture",
			WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_THICKFRAME,
			CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, nitroPaintStruct->edMgr.hWnd, NULL, NULL, NULL);
		teData->hWndConvertDialog = hWndConvertDialog; //prevent from redrawing the whole screen
		SendMessage(hWndMdi, WM_SETREDRAW, TRUE, 0);
		SetWindowLongPtr(hWndConvertDialog, 0, (LONG_PTR) teData);
		SendMessage(hWndConvertDialog, NV_INITIALIZE, 0, 0);
		DoModal(hWndConvertDialog);

		//we can check the isNitro field of the texture editor to determine if the conversion succeeded.
		if (TexViewerIsConverted(teData)) {
			succeeded = 1;

			//copy data
			TEXELS *srcTexel = &teData->texture->texture.texels;
			PALETTE *srcPal = &teData->texture->texture.palette;

			int texImageParam = srcTexel->texImageParam;
			int texelSize = TxGetTexelSize(TEXW(texImageParam), srcTexel->height, texImageParam);
			texels->texImageParam = texImageParam;
			texels->height = srcTexel->height;
			texels->texel = (unsigned char *) calloc(texelSize, 1);
			memcpy(texels->texel, srcTexel->texel, texelSize);

			//4x4 palette index
			if (FORMAT(texImageParam) == CT_4x4) {
				texels->cmp = (uint16_t *) calloc(texelSize / 2, 1);
				memcpy(texels->cmp, srcTexel->cmp, texelSize / 2);
			}

			//palette
			if (FORMAT(texImageParam) != CT_DIRECT) {
				unsigned int namelen = strlen(srcPal->name);
				palette->nColors = srcPal->nColors;
				palette->pal = (COLOR *) calloc(palette->nColors, sizeof(COLOR));
				palette->name = (char *) calloc(namelen + 1, 1);
				memcpy(palette->pal, srcPal->pal, palette->nColors * sizeof(COLOR));
				memcpy(palette->name, srcPal->name, namelen);
			}

			//texture name
			ConstructResourceNameFromFilePath(path, &texels->name);
		} else {
			succeeded = 0;
		}
		DestroyChild(hWndTextureEditor);
	}

	free(path);
	return succeeded;
}

static int NsbtxViewerCheckSave(NSBTXVIEWERDATA *data) {
	//NSBTX files: check lengths under 16 characters

	if (data->nsbtx->header.format == NSBTX_TYPE_NNS) {
		int warnNames = 0;
		WCHAR *warnbuf = _wcsdup(L"Resource names truncated on save:");
		WCHAR warntemp[96];

		for (int i = 0; i < data->nsbtx->nTextures; i++) {
			if (strlen(data->nsbtx->textures[i].name) <= 16) continue;

			wsprintfW(warntemp, L"\n  %.64S -> %.16S", data->nsbtx->textures[i].name, data->nsbtx->textures[i].name);

			warnbuf = (WCHAR *) realloc(warnbuf, (wcslen(warnbuf) + wcslen(warntemp) + 1) * sizeof(WCHAR));
			memcpy(warnbuf + wcslen(warnbuf), warntemp, (wcslen(warntemp) + 1) * sizeof(WCHAR));
			warnNames++;
		}
		for (int i = 0; i < data->nsbtx->nPalettes; i++) {
			if (strlen(data->nsbtx->palettes[i].name) <= 16) continue;

			wsprintfW(warntemp, L"\n  %.64S -> %.16S", data->nsbtx->palettes[i].name, data->nsbtx->palettes[i].name);

			warnbuf = (WCHAR *) realloc(warnbuf, (wcslen(warnbuf) + wcslen(warntemp) + 1) * sizeof(WCHAR));
			memcpy(warnbuf + wcslen(warnbuf), warntemp, (wcslen(warntemp) + 1) * sizeof(WCHAR));
			warnNames++;
		}

		if (warnNames) {
			MessageBox(data->hWnd, warnbuf, L"Name Truncation", MB_ICONWARNING);
		}
		free(warnbuf);
	}

	return 1;
}

LRESULT WINAPI NsbtxViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSBTXVIEWERDATA *data = (NSBTXVIEWERDATA *) EditorGetData(hWnd);
	switch (msg) {
		case WM_CREATE:
		{
			data->hWndTextureSelect = CreateListBox(hWnd, 0, 0, 150, 100);
			data->hWndPaletteSelect = CreateListBox(hWnd, 0, 100, 150, 100);
			data->hWndExportAll = CreateButton(hWnd, L"Export All", 0, 300 - 22, 75, 22, FALSE);
			data->hWndResourceButton = CreateButton(hWnd, L"VRAM Use", 75, 300 - 22, 75, 22, FALSE);
			data->hWndReplaceButton = CreateButton(hWnd, L"Replace", 150, 300 - 22, 100, 22, TRUE);
			data->hWndAddButton = CreateButton(hWnd, L"Add", 250, 300 - 22, 100, 22, FALSE);
			SetGUIFont(hWnd);
			SetWindowSubclass(data->hWndTextureSelect, ListboxDeleteSubclassProc, 1, 0);
			SetWindowSubclass(data->hWndPaletteSelect, ListboxDeleteSubclassProc, 1, 0);
			data->scale = 1;
			return 1;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			if (path != NULL) EditorSetFile(hWnd, path);
			data->nsbtx = (TexArc *) lParam;

			for (int i = 0; i < data->nsbtx->nTextures; i++) {
				WCHAR *buffer = TexNarrowResourceNameToWideChar(data->nsbtx->textures[i].name);
				AddListBoxItem(data->hWndTextureSelect, buffer);
				free(buffer);
			}

			for (int i = 0; i < data->nsbtx->nPalettes; i++) {
				WCHAR *buffer = TexNarrowResourceNameToWideChar(data->nsbtx->palettes[i].name);
				AddListBoxItem(data->hWndPaletteSelect, buffer);
				free(buffer);
			}
			SetListBoxSelection(data->hWndTextureSelect, 0);
			SetListBoxSelection(data->hWndPaletteSelect, 0);
			return 1;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			int t = GetListBoxSelection(data->hWndTextureSelect);
			int p = GetListBoxSelection(data->hWndPaletteSelect);
			TEXELS *texture = data->nsbtx->textures + t;
			PALETTE *palette = data->nsbtx->palettes + p;
			if (t >= data->nsbtx->nTextures || t < 0) texture = NULL; //catch out-of-bounds cases
			if (p >= data->nsbtx->nPalettes || p < 0) palette = NULL;

			//if no texture, we can't really render anything
			if (texture == NULL) {
				EndPaint(hWnd, &ps);
				return 0;
			}
			if (FORMAT(texture->texImageParam) != CT_DIRECT && palette == NULL) {
				EndPaint(hWnd, &ps);
				return 0;
			}

			float dpiScale = GetDpiScale();
			int panelWidth = UI_SCALE_COORD(150, dpiScale);
			int ctlHeight = UI_SCALE_COORD(22, dpiScale);

			HBITMAP hBitmap = renderTexture(texture, palette, data->scale);
			HDC hCompat = CreateCompatibleDC(hDC);
			SelectObject(hCompat, hBitmap);
			BitBlt(hDC, panelWidth, ctlHeight, TEXW(texture->texImageParam) * data->scale, texture->height * data->scale, hCompat, 0, 0, SRCCOPY);

			char bf[64];
			SelectObject(hDC, GetGUIFont());
			SetBkMode(hDC, TRANSPARENT);
			if (FORMAT(texture->texImageParam) == CT_DIRECT) {
				sprintf(bf, "%s texture, %dx%d", TxNameFromTexFormat(FORMAT(texture->texImageParam)), TEXW(texture->texImageParam), texture->height);
			} else {
				sprintf(bf, "%s texture, %dx%d; palette: %d colors", TxNameFromTexFormat(FORMAT(texture->texImageParam)), TEXW(texture->texImageParam), texture->height, palette->nColors);
			}
			RECT rcText;
			rcText.left = UI_SCALE_COORD(155, dpiScale);
			rcText.top = 0;
			rcText.right = rcClient.right;
			rcText.bottom = ctlHeight;
			DrawTextA(hDC, bf, -1, &rcText, DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX | DT_VCENTER);

			EndPaint(hWnd, &ps);
			DeleteObject(hCompat);
			DeleteObject(hBitmap);
			return 0;
		}
		case NV_CHILDNOTIF:
		{
			//if lParam is the texture or palette select, we're being notified of a deletion
			HWND hWndControl = (HWND) lParam;
			int sel = wParam;
			if (hWndControl == data->hWndTextureSelect) {
				//delete the sel-th texture
				TEXELS *textures = data->nsbtx->textures;
				void *texel = textures[sel].texel, *index = textures[sel].cmp;
				memmove(textures + sel, textures + sel + 1, (data->nsbtx->nTextures - sel - 1) * sizeof(TEXELS));
				data->nsbtx->nTextures--;
				data->nsbtx->textures = (TEXELS *) realloc(data->nsbtx->textures, data->nsbtx->nTextures * sizeof(TEXELS));
				//don't forget to free!
				free(texel);
				if (index) free(index);

				//update index if it would be out of bounds
				if (sel >= data->nsbtx->nTextures) sel = data->nsbtx->nTextures - 1;
			} else if (hWndControl == data->hWndPaletteSelect) {
				//delete the sel-th palette
				PALETTE *palettes = data->nsbtx->palettes;
				void *cols = palettes[sel].pal;
				memmove(palettes + sel, palettes + sel + 1, (data->nsbtx->nPalettes - sel - 1) * sizeof(PALETTE));
				data->nsbtx->nPalettes--;
				data->nsbtx->palettes = (PALETTE *) realloc(data->nsbtx->palettes, data->nsbtx->nPalettes * sizeof(PALETTE));
				free(cols);
				if (sel >= data->nsbtx->nPalettes) sel = data->nsbtx->nPalettes - 1;
			}
			SetListBoxSelection(hWndControl, sel);
			InvalidateRect(hWnd, NULL, TRUE);
			break;
		}
		case NV_ZOOMUPDATED:
			InvalidateRect(hWnd, NULL, TRUE);
			break;
		case WM_COMMAND:
		{
			if (lParam == 0 && HIWORD(wParam) == 0) {

				switch (LOWORD(wParam)) {
					case ID_FILE_EXPORT:
					{
						LPWSTR location = saveFileDialog(hWnd, L"Save Texture", L"TGA Files (*.tga)\0*.tga\0All Files\0*.*\0", L"tga");
						if (!location) break;

						TxWriteFileDirect(data->nsbtx->textures + GetListBoxSelection(data->hWndTextureSelect),
									  data->nsbtx->palettes + GetListBoxSelection(data->hWndPaletteSelect), TEXTURE_TYPE_NNSTGA, location);

						free(location);
						break;
					}
					case ID_FILE_SAVEAS:
					case ID_FILE_SAVE:
					{
						if (data->szOpenFile[0] == L'\0' || LOWORD(wParam) == ID_FILE_SAVEAS) {
							LPCWSTR filter = L"TexArc Files (*.nsbtx)\0*.nsbtx\0All Files\0*.*\0";
							LPWSTR path = saveFileDialog(data->editorMgr->hWnd, L"Save As...", filter, L"nsbtx");
							if (path != NULL) {
								EditorSetFile(hWnd, path);
								free(path);
							} else break;
						}
						if (NsbtxViewerCheckSave(data)) {
							TexarcWriteFile(data->nsbtx, data->szOpenFile);
						}
						break;
					}
				}
			}
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				int m = HIWORD(wParam);
				if (m == LBN_SELCHANGE) {
					int currentTexture = GetListBoxSelection(data->hWndTextureSelect);
					if (hWndControl == data->hWndTextureSelect || hWndControl == data->hWndPaletteSelect) {
						InvalidateRect(hWnd, NULL, TRUE);
					}
				} else if (m == BN_CLICKED) {
					if (hWndControl == data->hWndReplaceButton) {
						TEXELS texels;
						PALETTE palette;
						int s = TexarcViewerPromptTexImage(data, &texels, &palette);
						if (s) {
							int selectedPalette = GetListBoxSelection(data->hWndPaletteSelect);
							int selectedTexture = GetListBoxSelection(data->hWndTextureSelect);

							TEXELS *destTex = data->nsbtx->textures + selectedTexture;
							PALETTE *destPal = data->nsbtx->palettes + selectedPalette;

							if (FORMAT(texels.texImageParam) != 0) {
								//only replace texture if there's one to replace it with
								int oldTexImageParam = destTex->texImageParam;

								//transfer ownership (except name)
								if (destTex->texel != NULL) free(destTex->texel);
								if (destTex->cmp != NULL) free(destTex->cmp);
								if (texels.name != NULL) free(texels.name);
								texels.name = destTex->name;
								memcpy(destTex, &texels, sizeof(texels));

								//keep flipping, repeat, and transfomation
								int mask = 0xC00F0000;
								destTex->texImageParam = (oldTexImageParam & mask) | (destTex->texImageParam & ~mask);
							}

							if (FORMAT(texels.texImageParam) != CT_DIRECT) {
								free(destPal->pal);
								if (palette.name != NULL) free(palette.name);

								//transfer ownership of data (except name)
								destPal->pal = palette.pal;
								destPal->nColors = palette.nColors;
							}

							InvalidateRect(hWnd, NULL, TRUE);
						}
					} else if (hWndControl == data->hWndExportAll) {
						//we will overwrite this with the *real* path
						WCHAR *dirpath = UiDlgBrowseForFolder(data->editorMgr->hWnd, L"Select output folder...");
						if (dirpath == NULL) break;

						int pathLen = wcslen(dirpath);
						WCHAR path[MAX_PATH];
						memcpy(path, dirpath, (pathLen + 1) * sizeof(WCHAR));
						free(dirpath);

						if (path[pathLen - 1] != L'\\' && path[pathLen - 1] != '/') {
							path[pathLen] = '\\';
							pathLen++;
						}

						//iterate over textures. First, create an array of palette names.
						char **palNames = (char **) calloc(data->nsbtx->nPalettes, sizeof(char *));
						for (int i = 0; i < data->nsbtx->nPalettes; i++) {
							palNames[i] = data->nsbtx->palettes[i].name;
						}

						//next, associate each texture with a palette. Write out Nitro TGA files.
						for (int i = 0; i < data->nsbtx->nTextures; i++) {
							char *name = data->nsbtx->textures[i].name;
							int pltt = guessTexPlttByName(name, palNames, data->nsbtx->nPalettes, &data->nsbtx->textures[i], data->nsbtx->palettes);

							//copy texture name to the end of `path`
							for (unsigned int j = 0; j < strlen(name) + 1; j++) {
								path[j + pathLen] = (WCHAR) name[j];
							}
							//suffix ".tga"
							memcpy(path + pathLen + strlen(name), L".tga", 10);

							TxWriteFileDirect(&data->nsbtx->textures[i], &data->nsbtx->palettes[pltt], TEXTURE_TYPE_NNSTGA, path);
						}

						//free palette name array
						free(palNames);
					} else if (hWndControl == data->hWndResourceButton) {
						CreateVramUseWindow(data->editorMgr->hWnd, data->nsbtx);
					} else if (hWndControl == data->hWndAddButton) {
						//read texture
						TEXELS texels;
						PALETTE palette;
						int s = TexarcViewerPromptTexImage(data, &texels, &palette);
						if (s) {
							//add
							int texImageParam = texels.texImageParam;
							int fmt = FORMAT(texImageParam);
							int hasPalette = fmt != CT_DIRECT;
							int hasTexel = fmt != 0;

							//add texel
							if (hasTexel) {
								//update TexArc
								int texIndex = TexarcAddTexture(data->nsbtx, &texels);
								if (texIndex != -1) {
									//update UI
									WCHAR *strbuf = TexNarrowResourceNameToWideChar(texels.name);
									AddListBoxItem(data->hWndTextureSelect, strbuf);
									SetListBoxSelection(data->hWndTextureSelect, data->nsbtx->nTextures - 1);
									free(strbuf);
								} else {
									int existingIndex = TexarcGetTextureIndexByName(data->nsbtx, texels.name);
									SetListBoxSelection(data->hWndTextureSelect, existingIndex);
									MessageBox(hWnd, L"Texture name conflict.", L"Texture name conflict", MB_ICONERROR);
								}
							}

							//add palette (if the name is the same as an existing palette, use the larger
							//of the two palettes)
							if (hasPalette) {
								//if the palette name does not exist, create a default name.
								if (palette.name == NULL) {
									//create empty name.
									palette.name = (char *) calloc(1, 1);
									palette.name[0] = '\0';
								}

								int nOriginalPalettes = data->nsbtx->nPalettes;
								int palIndex = TexarcAddPalette(data->nsbtx, &palette);
								if (palIndex == -1) {
									MessageBox(hWnd, L"Palette name conflict.", L"Palette name conflict", MB_ICONERROR);
									free(palette.pal);
									break;
								} else {
									int nPalettesAfter = data->nsbtx->nPalettes;
									if (nPalettesAfter > nOriginalPalettes) {
										//add to UI
										WCHAR *strbuf = TexNarrowResourceNameToWideChar(palette.name);
										AddListBoxItem(data->hWndPaletteSelect, strbuf);
										free(strbuf);
									}

									//select
									SetListBoxSelection(data->hWndPaletteSelect, palIndex);
								}
							}

							//invalidate
							InvalidateRect(hWnd, NULL, TRUE);
						}
					}
				} else if (m == LBN_DBLCLK) {
					if (hWndControl == data->hWndTextureSelect || hWndControl == data->hWndPaletteSelect) {
						WCHAR textBuffer[256] = { 0 };
						int sel = GetListBoxSelection(hWndControl);
						SendMessage(hWndControl, LB_GETTEXT, sel, (LPARAM) textBuffer);
						
						//make prompt
						HWND hWndMain = data->editorMgr->hWnd;
						int n = PromptUserText(hWndMain, L"Name Entry", L"Enter a name:", textBuffer, 256);
						if (n) {
							//replace selected text
							ReplaceListBoxItem(hWndControl, sel, textBuffer);
							SetFocus(hWndControl);

							//update TexArc
							char **destName = NULL;
							if (hWndControl == data->hWndTextureSelect) {
								destName = &data->nsbtx->textures[sel].name;
							} else {
								destName = &data->nsbtx->palettes[sel].name;
							}
							if (*destName != NULL) free(*destName);
							
							*destName = TexNarrowResourceNameFromWideChar(textBuffer);
						}
					}
				}
			}
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			float dpiScale = GetDpiScale();
			int ctlHeight = UI_SCALE_COORD(22, dpiScale);
			int ctlWidth = UI_SCALE_COORD(100, dpiScale);
			int paneWidth = UI_SCALE_COORD(150, dpiScale);

			int height = rcClient.bottom;
			MoveWindow(data->hWndTextureSelect, 0, 0, paneWidth, (height - ctlHeight) / 2, TRUE);
			MoveWindow(data->hWndPaletteSelect, 0, (height - ctlHeight) / 2, paneWidth, (height - ctlHeight) / 2, TRUE);
			MoveWindow(data->hWndExportAll, 0, height - ctlHeight, paneWidth / 2, ctlHeight, TRUE);
			MoveWindow(data->hWndResourceButton, paneWidth / 2, height - ctlHeight, paneWidth / 2, ctlHeight, TRUE);
			MoveWindow(data->hWndReplaceButton, paneWidth, height - ctlHeight, ctlWidth, ctlHeight, TRUE);
			MoveWindow(data->hWndAddButton, paneWidth + ctlWidth, height - ctlHeight, ctlWidth, ctlHeight, TRUE);
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

void CreateVramUseWindow(HWND hWndParent, TexArc *nsbtx) {
	HWND hWndVramViewer = CreateWindow(L"VramUseClass", L"VRAM Usage", WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndParent, NULL, NULL, NULL);
	SendMessage(hWndVramViewer, NV_INITIALIZE, 0, (LPARAM) nsbtx);
	DoModal(hWndVramViewer);
}

LRESULT CALLBACK VramUseWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndTexLabel = (HWND) GetWindowLong(hWnd, 0 * sizeof(void *));
	HWND hWndPalLabel = (HWND) GetWindowLong(hWnd, 1 * sizeof(void *));
	HWND hWndTexList = (HWND) GetWindowLong(hWnd, 2 * sizeof(void *));
	HWND hWndPalList = (HWND) GetWindowLong(hWnd, 3 * sizeof(void *));
	HWND hWndInfoButton = (HWND) GetWindowLong(hWnd, 4 * sizeof(void *));

	switch (msg) {
		case WM_CREATE:
		{
			int width = 350 + GetSystemMetrics(SM_CXVSCROLL);
			HWND hWndTextureLabel = CreateStatic(hWnd, L"Texture usage: %d.%03dKB (%d.%03dKB Texel, %d.%03dKB Index)", 5, 0, width - 5, 22);
			HWND hWndButton = CreateButton(hWnd, L"...", width - 25, 0, 25, 22, FALSE);

			HWND hWndTextures = CreateListView(hWnd, 0, 22, width, 150);
			AddListViewColumn(hWndTextures, L"Texture", 0, 125, SCA_LEFT);
			AddListViewColumn(hWndTextures, L"Format", 1, 75, SCA_LEFT);
			AddListViewColumn(hWndTextures, L"Texel (KB)", 2, 75, SCA_RIGHT);
			AddListViewColumn(hWndTextures, L"Index (KB)", 3, 75, SCA_RIGHT);

			HWND hWndPaletteLabel = CreateStatic(hWnd, L"Palette usage: 0KB", 5, 22 + 150, width - 5, 22);

			HWND hWndPalettes = CreateListView(hWnd, 0, 22 + 22 + 150, width, 150);
			AddListViewColumn(hWndPalettes, L"Palette", 0, 125, SCA_LEFT);
			AddListViewColumn(hWndPalettes, L"Colors", 1, 75, SCA_RIGHT);
			AddListViewColumn(hWndPalettes, L"Size (KB)", 2, 75, SCA_RIGHT);
			SetWindowSize(hWnd, width, 300 + 22 + 22);
			SetGUIFont(hWnd);

			SetWindowLongPtr(hWnd, 0 * sizeof(void *), (LONG_PTR) hWndTextureLabel);
			SetWindowLongPtr(hWnd, 1 * sizeof(void *), (LONG_PTR) hWndPaletteLabel);
			SetWindowLongPtr(hWnd, 2 * sizeof(void *), (LONG_PTR) hWndTextures);
			SetWindowLongPtr(hWnd, 3 * sizeof(void *), (LONG_PTR) hWndPalettes);
			SetWindowLongPtr(hWnd, 4 * sizeof(void *), (LONG_PTR) hWndButton);
			break;
		}
		case NV_INITIALIZE:
		{
			TexArc *nsbtx = (TexArc *) lParam;

			//for all textures...
			int nTextures = 0, nPalettes = 0;
			int totalTexelSize = 0, totalIndexSize = 0, totalPaletteSize = 0, totalTextureSize = 0;
			int compressedTexelSize = 0, normalTexelSize = 0;
			WCHAR textBuffer[256];
			for (int i = 0; i < nsbtx->nTextures; i++) {
				TEXELS *tex = nsbtx->textures + i;
				int texImageParam = tex->texImageParam;
				int format = FORMAT(texImageParam);
				int texelSize = TxGetTexelSize(TEXW(texImageParam), tex->height, texImageParam);
				int indexSize = TxGetIndexVramSize(tex);

				//copy name. Beware, not null terminated
				unsigned int len = strnlen(tex->name, 255);
				textBuffer[len] = L'\0';
				for (unsigned int j = 0; j < len; j++) {
					textBuffer[j] = (WCHAR) tex->name[j];
				}
				AddListViewItem(hWndTexList, textBuffer, i, 0);

				//format
				const char *fmt = TxNameFromTexFormat(FORMAT(texImageParam));
				for (unsigned int j = 0; j < strlen(fmt); j++) {
					textBuffer[j] = (WCHAR) fmt[j];
					textBuffer[j + 1] = L'\0';
				}
				AddListViewItem(hWndTexList, textBuffer, i, 1);

				//sizes
				wsprintfW(textBuffer, L"%d.%03d", texelSize / 1024, (texelSize % 1024) * 1000 / 1024);
				AddListViewItem(hWndTexList, textBuffer, i, 2);
				if (indexSize) {
					wsprintfW(textBuffer, L"%d.%03d", indexSize / 1024, (indexSize % 1024) * 1000 / 1024);
					AddListViewItem(hWndTexList, textBuffer, i, 3);
				}

				nTextures++;
				totalTexelSize += texelSize;
				totalIndexSize += indexSize;
				totalTextureSize += texelSize + indexSize;
				if (format == CT_4x4) compressedTexelSize += texelSize;
				else normalTexelSize += texelSize;
			}

			//all palettes...
			for (int i = 0; i < nsbtx->nPalettes; i++) {
				PALETTE *pal = nsbtx->palettes + i;
				int nCols = pal->nColors;
				int paletteSize = nCols * 2;

				//copy name.
				unsigned int len = strnlen(pal->name, 255);
				textBuffer[len] = L'\0';
				for (unsigned int j = 0; j < len; j++) {
					textBuffer[j] = (WCHAR) pal->name[j];
				}
				AddListViewItem(hWndPalList, textBuffer, i, 0);

				//colors and size
				wsprintfW(textBuffer, L"%d", pal->nColors);
				AddListViewItem(hWndPalList, textBuffer, i, 1);
				wsprintfW(textBuffer, L"%d.%03d", paletteSize / 1024, (paletteSize % 1024) * 1000 / 1024);
				AddListViewItem(hWndPalList, textBuffer, i, 2);

				totalPaletteSize += paletteSize;
			}

			//label text
			int len = wsprintfW(textBuffer, L"Texture usage: %d.%03dKB (%d.%03dKB Texel, %d.%03dKB Index)",
				totalTextureSize / 1024, (totalTextureSize % 1024) * 1000 / 1024,
				totalTexelSize / 1024, (totalTexelSize % 1024) * 1000 / 1024,
				totalIndexSize / 1024, (totalIndexSize % 1024) * 1000 / 1024);
			SendMessage(hWndTexLabel, WM_SETTEXT, len, (LPARAM) textBuffer);
			len = wsprintfW(textBuffer, L"Palette usage: %d.%03dKB",
				totalPaletteSize / 1024, (totalPaletteSize % 1024) * 1000 / 1024);
			SendMessage(hWndPalLabel, WM_SETTEXT, len, (LPARAM) textBuffer);

			//set info fields
			SetWindowLong(hWnd, 5 * sizeof(void *), (LONG) normalTexelSize);
			SetWindowLong(hWnd, 6 * sizeof(void *), (LONG) compressedTexelSize);
			SetWindowLong(hWnd, 7 * sizeof(void *), (LONG) totalIndexSize);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				if (hWndControl == hWndInfoButton && HIWORD(wParam) == BN_CLICKED) {
					WCHAR buffer[128];
					int normalTexelSize = GetWindowLong(hWnd, 5 * sizeof(void *));
					int compressedTexelSize = GetWindowLong(hWnd, 6 * sizeof(void *));
					int totalIndexSize = GetWindowLong(hWnd, 7 * sizeof(void *));
					wsprintfW(buffer, L"Texture Summary:\nNormal Texel:\t\t%d.%03dKB\nCompressed Texel:\t\t%d.%03dKB\nIndex Data:\t\t%d.%03dKB",
						normalTexelSize / 1024, (normalTexelSize % 1024) * 1000 / 1024,
						compressedTexelSize / 1024, (compressedTexelSize % 1024) * 1000 / 1024,
						totalIndexSize / 1024, (totalIndexSize % 1024) * 1000 / 1024);
					MessageBox(hWnd, buffer, L"Texture VRAM Usage", MB_ICONINFORMATION);
				}
			}
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			int width = rcClient.right, height = rcClient.bottom;

			int listViewHeight = (height - (22 * 2)) / 2;
			MoveWindow(hWndInfoButton, width - 25, 0, 25, 22, TRUE);
			MoveWindow(hWndTexList, 0, 22, width, listViewHeight, TRUE);
			MoveWindow(hWndPalLabel, 5, 22 + listViewHeight, width - 5, 22, TRUE);
			MoveWindow(hWndPalList, 0, 44 + listViewHeight, width, height - (44 + listViewHeight), TRUE);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNsbtxViewerClass(VOID) {
	TexarcRegisterFormats();

	int features = EDITOR_FEATURE_ZOOM;
	EditorRegister(L"NsbtxViewerClass", NsbtxViewerWndProc, L"NSBTX Editor", sizeof(NSBTXVIEWERDATA), features);
	RegisterGenericClass(L"VramUseClass", VramUseWndProc, 8 * sizeof(void *));
}

static HWND CreateNsbtxViewerInternal(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path, TexArc *nsbtx) {
	HWND h = EditorCreate(L"NsbtxViewerClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) nsbtx);
	return h;
}

HWND CreateNsbtxViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	TexArc *nsbtx = (TexArc *) calloc(1, sizeof(TexArc));
	if (TexarcReadFile(nsbtx, path)) {
		free(nsbtx);
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	return CreateNsbtxViewerInternal(x, y, width, height, hWndParent, path, nsbtx);
}

HWND CreateNsbtxViewerImmediate(int x, int y, int width, int height, HWND hWndParent, TexArc *nsbtx) {
	return CreateNsbtxViewerInternal(x, y, width, height, hWndParent, NULL, nsbtx);
}
