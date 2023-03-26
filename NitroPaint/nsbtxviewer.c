#include <Windows.h>
#include <CommCtrl.h>
#include <stdio.h>
#include <Shlobj.h>

#include "nsbtxviewer.h"
#include "nitropaint.h"
#include "resource.h"
#include "ui.h"

extern HICON g_appIcon;

extern int max16Len(char *str);

HBITMAP renderTexture(TEXELS *texture, PALETTE *palette, int zoom) {
	int width = TEXW(texture->texImageParam);
	int height = TEXH(texture->texImageParam);
	DWORD *px = (DWORD *) calloc(width * zoom * height * zoom, 4);
	textureRender(px, texture, palette, 0);

	//perform alpha blending
	int scaleWidth = width * zoom, scaleHeight = height * zoom;
	for (int yDest = scaleHeight - 1; yDest >= 0; yDest--) {
		int y = yDest / zoom;
		for (int xDest = scaleWidth - 1; xDest >= 0; xDest--) {
			int x = xDest / zoom;

			DWORD pixel = px[x + y * width];
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

int levenshtein(const char *str1, int l1, const char *str2, int l2, int dst, int maxDst) {
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

int calculateHighestPaletteIndex(TEXELS *texture) {
	int format = FORMAT(texture->texImageParam);
	if (format == CT_DIRECT) return -1;

	int nHighest = 0;
	int texelSize = getTexelSize(TEXW(texture->texImageParam), TEXH(texture->texImageParam), texture->texImageParam);
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

int guessTexPlttByName(char *textureName, char **paletteNames, int nPalettes, TEXELS *texture, PALETTE *palettes) {
	int format = FORMAT(texture->texImageParam);
	if (format == CT_DIRECT) return -1;

	//first gueses: Same name (case insensitive)?
	for (int i = 0; i < nPalettes; i++) {
		if (_stricmp(textureName, paletteNames[i]) == 0) return i;
	}

	//second guess: add "_pl" to texture name (and truncate to 16)
	char nameBuffer[20];
	memcpy(nameBuffer, textureName, strlen(textureName) + 1);
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
	memcpy(nameBuffer, textureName, strlen(textureName) + 1);
	nameBuffer[13] = '\0';
	memcpy(nameBuffer + strlen(nameBuffer), "_pl", 4);
	for (int i = 0; i < nPalettes; i++) {
		if (_stricmp(nameBuffer, paletteNames[i]) == 0) return i;
	}

	//fifth guess: same thing but truncate texture name length to 12
	memcpy(nameBuffer, textureName, strlen(textureName) + 1);
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

void CreateVramUseWindow(HWND hWndParent, NSBTX *nsbtx);

LRESULT WINAPI NsbtxViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NSBTXVIEWERDATA *data = (NSBTXVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (NSBTXVIEWERDATA *) calloc(1, sizeof(NSBTXVIEWERDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			data->hWndTextureSelect = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, L"", WS_VISIBLE | WS_CHILD | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | LBS_NOTIFY, 0, 0, 150, 100, hWnd, NULL, NULL, NULL);
			data->hWndPaletteSelect = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, L"", WS_VISIBLE | WS_CHILD | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | LBS_NOTIFY, 0, 100, 150, 100, hWnd, NULL, NULL, NULL);
			data->hWndExportAll = CreateWindow(L"BUTTON", L"Export all", WS_VISIBLE | WS_CHILD, 0, 300 - 22, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndResourceButton = CreateButton(hWnd, L"VRAM Use", 75, 300 - 22, 75, 22, FALSE);
			data->hWndReplaceButton = CreateWindow(L"BUTTON", L"Replace", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 150, 300 - 22, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndAddButton = CreateButton(hWnd, L"Add", 250, 300 - 22, 100, 22, FALSE);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
			data->scale = 1;
			return 1;
		}
		case NV_SETTITLE:
		{
			LPWSTR path = (LPWSTR) lParam;
			WCHAR titleBuffer[MAX_PATH + 15];
			if (!g_configuration.fullPaths) path = GetFileName(path);
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - NSBTX Viewer", 32);
			SetWindowText(hWnd, titleBuffer);
			break;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->nsbtx, (NSBTX *) lParam, sizeof(NSBTX));
			SendMessage(hWnd, NV_SETTITLE, 0, (LPARAM) path);

			WCHAR buffer[17];
			for (int i = 0; i < data->nsbtx.nTextures; i++) {
				char *name = data->nsbtx.textures[i].name;
				int len = max16Len(name);
				for (int j = 0; j < len; j++) {
					buffer[j] = name[j];
				}
				buffer[len] = 0;
				SendMessage(data->hWndTextureSelect, LB_ADDSTRING, 0, (LPARAM) buffer);
			}
			for (int i = 0; i < data->nsbtx.nPalettes; i++) {
				char *name = data->nsbtx.palettes[i].name;
				int len = max16Len(name);
				for (int j = 0; j < len; j++) {
					buffer[j] = name[j];
				}
				buffer[len] = 0;
				SendMessage(data->hWndPaletteSelect, LB_ADDSTRING, 0, (LPARAM) buffer);
			}
			SendMessage(data->hWndTextureSelect, LB_SETCURSEL, 0, 0);
			SendMessage(data->hWndPaletteSelect, LB_SETCURSEL, 0, 0);
			return 1;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			int t = SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0);
			int p = SendMessage(data->hWndPaletteSelect, LB_GETCURSEL, 0, 0);
			TEXELS *texture = data->nsbtx.textures + t;
			PALETTE *palette = data->nsbtx.palettes + p;
			HBITMAP hBitmap = renderTexture(texture, palette, data->scale);

			HDC hCompat = CreateCompatibleDC(hDC);
			SelectObject(hCompat, hBitmap);
			BitBlt(hDC, 150, 22, TEXW(texture->texImageParam) * data->scale, TEXH(texture->texImageParam) * data->scale, hCompat, 0, 0, SRCCOPY);

			char bf[64];
			SelectObject(hDC, GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(hDC, TRANSPARENT);
			if (FORMAT(texture->texImageParam) == CT_DIRECT) {
				sprintf(bf, "%s texture, %dx%d", stringFromFormat(FORMAT(texture->texImageParam)), TEXW(texture->texImageParam), TEXH(texture->texImageParam));
			} else {
				sprintf(bf, "%s texture, %dx%d; palette: %d colors", stringFromFormat(FORMAT(texture->texImageParam)), TEXW(texture->texImageParam), TEXH(texture->texImageParam), palette->nColors);
			}
			RECT rcText;
			rcText.left = 155;
			rcText.top = 0;
			rcText.right = 350;
			rcText.bottom = 22;
			DrawTextA(hDC, bf, -1, &rcText, DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX | DT_VCENTER);

			EndPaint(hWnd, &ps);
			DeleteObject(hCompat);
			DeleteObject(hBitmap);
			return 0;
		}
		case WM_COMMAND:
		{
			if (lParam == 0 && HIWORD(wParam) == 0) {

				switch (LOWORD(wParam)) {
					case ID_FILE_EXPORT:
					{
						LPWSTR location = saveFileDialog(hWnd, L"Save Texture", L"TGA Files (*.tga)\0*.tga\0All Files\0*.*\0", L"tga");
						if (!location) break;

						writeNitroTGA(location, data->nsbtx.textures + SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0),
									  data->nsbtx.palettes + SendMessage(data->hWndPaletteSelect, LB_GETCURSEL, 0, 0));

						free(location);
						break;
					}
					case ID_FILE_SAVEAS:
					case ID_FILE_SAVE:
					{
						if (data->szOpenFile[0] == L'\0' || LOWORD(wParam) == ID_FILE_SAVEAS) {
							LPCWSTR filter = L"NSBTX Files (*.nsbtx)\0*.nsbtx\0All Files\0*.*\0";
							LPWSTR path = saveFileDialog(getMainWindow(hWnd), L"Save As...", filter, L"nsbtx");
							if (path != NULL) {
								memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
								SendMessage(hWnd, NV_SETTITLE, 0, (LPARAM) path);
								free(path);
							} else break;
						}
						nsbtxWriteFile(&data->nsbtx, data->szOpenFile);
						break;
					}
					case ID_ZOOM_100:
					case ID_ZOOM_200:
					case ID_ZOOM_400:
					case ID_ZOOM_800:
					{
						if (LOWORD(wParam) == ID_ZOOM_100) data->scale = 1;
						if (LOWORD(wParam) == ID_ZOOM_200) data->scale = 2;
						if (LOWORD(wParam) == ID_ZOOM_400) data->scale = 4;
						if (LOWORD(wParam) == ID_ZOOM_800) data->scale = 8;

						int checkBox = ID_ZOOM_100;
						if (data->scale == 2) {
							checkBox = ID_ZOOM_200;
						} else if (data->scale == 4) {
							checkBox = ID_ZOOM_400;
						} else if (data->scale == 8) {
							checkBox = ID_ZOOM_800;
						}
						int ids[] = {ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800};
						for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
							int id = ids[i];
							CheckMenuItem(GetMenu(getMainWindow(hWnd)), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
						}

						InvalidateRect(hWnd, NULL, TRUE);
						break;
					}
				}
			}
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				int m = HIWORD(wParam);
				if (m == LBN_SELCHANGE) {
					int currentTexture = SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0);
					if (hWndControl == data->hWndTextureSelect || hWndControl == data->hWndPaletteSelect) {
						InvalidateRect(hWnd, NULL, TRUE);
					}
				} else if (m == BN_CLICKED) {
					if (hWndControl == data->hWndReplaceButton) {
						LPWSTR path = openFileDialog(hWnd, L"Open Nitro TGA", L"TGA Files (*.tga)\0*.tga\0All Files\0*.*\0", L"tga");
						if (!path) break;

						TEXELS texels;
						PALETTE palette;
						int s = nitroTgaRead(path, &texels, &palette);
						if (s) {
							MessageBox(hWnd, L"Invalid Nitro TGA.", L"Invalid Nitro TGA", MB_ICONERROR);
						} else {
							int selectedPalette = SendMessage(data->hWndPaletteSelect, LB_GETCURSEL, 0, 0);
							int selectedTexture = SendMessage(data->hWndTextureSelect, LB_GETCURSEL, 0, 0);
							TEXELS *destTex = data->nsbtx.textures + selectedTexture;
							PALETTE *destPal = data->nsbtx.palettes + selectedPalette;
							int oldTexImageParam = destTex->texImageParam;
							memcpy(texels.name, destTex->name, 16);
							memcpy(palette.name, destPal->name, 16);
							if (destTex->cmp) free(destTex->cmp);
							memcpy(destTex, &texels, sizeof(TEXELS));
							if (FORMAT(texels.texImageParam) != CT_DIRECT) {
								free(destPal->pal);
								memcpy(destPal, &palette, sizeof(PALETTE));
							}
							//keep flipping, repeat, and transfomation
							int mask = 0xC00F0000;
							destTex->texImageParam = (oldTexImageParam & mask) | (destTex->texImageParam & ~mask);
							InvalidateRect(hWnd, NULL, TRUE);
						}

						free(path);
					} else if (hWndControl == data->hWndExportAll) {
						WCHAR path[MAX_PATH]; //we will overwrite this with the *real* path

						BROWSEINFO bf;
						bf.hwndOwner = getMainWindow(hWnd);
						bf.pidlRoot = NULL;
						bf.pszDisplayName = path;
						bf.lpszTitle = L"Select output folder...";
						bf.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_VALIDATE; //I don't much like the new dialog style
						bf.lpfn = NULL;
						bf.lParam = 0;
						bf.iImage = 0;
						PIDLIST_ABSOLUTE idl = SHBrowseForFolder(&bf);

						if (idl == NULL) {
							CoUninitialize();
							break;
						}
						SHGetPathFromIDList(idl, path);
						CoTaskMemFree(idl);

						int pathLen = wcslen(path);
						if (path[pathLen - 1] != L'\\' && path[pathLen - 1] != '/') {
							path[pathLen] = '\\';
							pathLen++;
						}

						//iterate over textures. First, create an array of palette names.
						char **palNames = (char **) calloc(data->nsbtx.nPalettes, sizeof(char *));
						for (int i = 0; i < data->nsbtx.nPalettes; i++) {
							char *nameBuffer = (char *) calloc(17, 1); //null terminator for convenience
							memcpy(nameBuffer, data->nsbtx.palettes[i].name, 16);
							palNames[i] = nameBuffer;
						}

						//next, associate each texture with a palette. Write out Nitro TGA files.
						for (int i = 0; i < data->nsbtx.nTextures; i++) {
							char name[17];
							memcpy(name, data->nsbtx.textures[i].name, 16);
							name[16] = '\0';
							int pltt = guessTexPlttByName(name, palNames, data->nsbtx.nPalettes, &data->nsbtx.textures[i], data->nsbtx.palettes);

							//copy texture name to the end of `path`
							for (unsigned int j = 0; j < strlen(name) + 1; j++) {
								path[j + pathLen] = (WCHAR) name[j];
							}
							//suffix ".tga"
							memcpy(path + pathLen + strlen(name), L".tga", 10);

							writeNitroTGA(path, &data->nsbtx.textures[i], &data->nsbtx.palettes[pltt]);
						}

						//free palette name array
						for (int i = 0; i < data->nsbtx.nPalettes; i++) {
							free(palNames[i]);
						}
						free(palNames);
					} else if (hWndControl == data->hWndResourceButton) {
						HWND hWndMain = getMainWindow(hWnd);
						CreateVramUseWindow(hWndMain, &data->nsbtx);
					} else if (hWndControl == data->hWndAddButton) {
						LPWSTR path = openFileDialog(hWnd, L"Open Nitro TGA", L"TGA Files (*.tga)\0*.tga\0All Files\0*.*\0", L"tga");
						if (!path) break;

						//read texture
						TEXELS texels;
						PALETTE palette;
						int s = nitroTgaRead(path, &texels, &palette);
						if (s) {
							MessageBox(hWnd, L"Invalid Nitro TGA.", L"Invalid Nitro TGA", MB_ICONERROR);
						} else {
							//add
							WCHAR strbuf[17] = { 0 };
							int texImageParam = texels.texImageParam;
							int fmt = FORMAT(texImageParam);
							int hasPalette = fmt != CT_DIRECT;

							//add texel
							{
								//update NSBTX
								data->nsbtx.textures = realloc(data->nsbtx.textures, (data->nsbtx.nTextures + 1) * sizeof(TEXELS));
								memcpy(data->nsbtx.textures + data->nsbtx.nTextures, &texels, sizeof(TEXELS));
								data->nsbtx.nTextures++;

								//update UI
								for (int i = 0; i < 16; i++) {
									strbuf[i] = (WCHAR) texels.name[i];
								}
								SendMessage(data->hWndTextureSelect, LB_ADDSTRING, 0, (LPARAM) strbuf);
								SendMessage(data->hWndTextureSelect, LB_SETCURSEL, data->nsbtx.nTextures - 1, 0);
							}

							//add palette (if the name is the same as an existing palette, use the larger
							//of the two palettes)
							if (hasPalette) {
								//check if a palette of that name already exists.
								PALETTE *existing = NULL;
								int existingIndex = -1;
								for (int i = 0; i < data->nsbtx.nPalettes; i++) {
									PALETTE *p = data->nsbtx.palettes + i;
									if (memcmp(p->name, palette.name, 16) == 0) {
										existing = data->nsbtx.palettes + i;
										existingIndex = i;
										break;
									}
								}

								//existing?
								PALETTE *dest = NULL;
								int destIndex = -1;
								if (existing != NULL) {
									//if existing, ensure one is a subset of the other.
									int nColsExisting = existing->nColors;
									int nColsNew = palette.nColors;
									int nColsCompare = min(nColsNew, nColsExisting);

									//not a subset?
									if (memcmp(palette.pal, existing->pal, nColsCompare * sizeof(COLOR)) != 0) {
										MessageBox(hWnd, L"Palette name conflict.", L"Palette name conflict", MB_ICONERROR);
										free(palette.pal);
										break;
									}
									dest = existing;
									destIndex = existingIndex;

									//if new palette is larger, realloc
									if (nColsNew > nColsExisting) {
										dest->nColors = nColsNew;
										free(dest->pal);
										dest->pal = palette.pal;
									} else {
										free(palette.pal);
									}
								} else {
									//increase palette count. Update NSBTX
									data->nsbtx.palettes = realloc(data->nsbtx.palettes, (data->nsbtx.nPalettes + 1) * sizeof(PALETTE));
									dest = data->nsbtx.palettes + data->nsbtx.nPalettes;
									memcpy(dest, &palette, sizeof(PALETTE));
									data->nsbtx.nPalettes++;

									//add to UI
									for (int i = 0; i < 16; i++) {
										strbuf[i] = (WCHAR) palette.name[i];
									}
									SendMessage(data->hWndPaletteSelect, LB_ADDSTRING, 0, (LPARAM) strbuf);
								}

								//update UI
								SendMessage(data->hWndPaletteSelect, LB_SETCURSEL, data->nsbtx.nPalettes - 1, 0);
							}

							//invalidate
							InvalidateRect(hWnd, NULL, TRUE);
						}

						free(path);
					}
				}
			}
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			int height = rcClient.bottom;
			MoveWindow(data->hWndTextureSelect, 0, 0, 150, height / 2 - 11, TRUE);
			MoveWindow(data->hWndPaletteSelect, 0, height / 2 - 11, 150, height / 2 - 11, TRUE);
			MoveWindow(data->hWndExportAll, 0, height - 22, 75, 22, TRUE);
			MoveWindow(data->hWndResourceButton, 75, height - 22, 75, 22, TRUE);
			MoveWindow(data->hWndReplaceButton, 150, height - 22, 100, 22, TRUE);
			MoveWindow(data->hWndAddButton, 250, height - 22, 100, 22, TRUE);
			break;
		}
		case WM_MDIACTIVATE:
		{
			HWND hWndMain = getMainWindow(hWnd);
			if ((HWND) lParam == hWnd) {
				if (data->showBorders)
					CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_CHECKED);
				else
					CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
				int checkBox = ID_ZOOM_100;
				if (data->scale == 2) {
					checkBox = ID_ZOOM_200;
				} else if (data->scale == 4) {
					checkBox = ID_ZOOM_400;
				} else if (data->scale == 8) {
					checkBox = ID_ZOOM_800;
				}
				int ids[] = {ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800};
				for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
					int id = ids[i];
					CheckMenuItem(GetMenu(hWndMain), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			NSBTX *nsbtx = &data->nsbtx;
			fileFree((OBJECT_HEADER *) nsbtx);
			break;
		}
		case NV_GETTYPE:
			return FILE_TYPE_NSBTX;
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

void CreateVramUseWindow(HWND hWndParent, NSBTX *nsbtx) {
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
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) (HFONT) GetStockObject(DEFAULT_GUI_FONT));

			SetWindowLong(hWnd, 0 * sizeof(void *), (LONG) hWndTextureLabel);
			SetWindowLong(hWnd, 1 * sizeof(void *), (LONG) hWndPaletteLabel);
			SetWindowLong(hWnd, 2 * sizeof(void *), (LONG) hWndTextures);
			SetWindowLong(hWnd, 3 * sizeof(void *), (LONG) hWndPalettes);
			SetWindowLong(hWnd, 4 * sizeof(void *), (LONG) hWndButton);
			break;
		}
		case NV_INITIALIZE:
		{
			NSBTX *nsbtx = (NSBTX *) lParam;

			//for all textures...
			int nTextures = 0, nPalettes = 0;
			int totalTexelSize = 0, totalIndexSize = 0, totalPaletteSize = 0, totalTextureSize = 0;
			int compressedTexelSize = 0, normalTexelSize = 0;
			WCHAR textBuffer[256];
			for (int i = 0; i < nsbtx->nTextures; i++) {
				TEXELS *tex = nsbtx->textures + i;
				int texImageParam = tex->texImageParam;
				int format = FORMAT(texImageParam);
				int texelSize = getTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
				int indexSize = getIndexVramSize(tex);

				//copy name. Beware, not null terminated
				for (unsigned int j = 0; j < 16; j++) {
					textBuffer[j] = (WCHAR) tex->name[j];
					textBuffer[j + 1] = L'\0';
				}
				AddListViewItem(hWndTexList, textBuffer, i, 0);

				//format
				const char *fmt = stringFromFormat(FORMAT(texImageParam));
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

				//copy name. Beware, not null terminated
				for (unsigned int j = 0; j < 16; j++) {
					textBuffer[j] = (WCHAR) pal->name[j];
					textBuffer[j + 1] = L'\0';
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
			MoveWindow(hWndPalLabel, 5, 22 + listViewHeight, width - 5, 22, FALSE);
			MoveWindow(hWndPalList, 0, 44 + listViewHeight, width, height - (44 + listViewHeight), TRUE);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

VOID RegisterVramUseClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme ? CreateSolidBrush(RGB(32, 32, 32)) : (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"VramUseClass";
	wcex.lpfnWndProc = VramUseWndProc;
	wcex.cbWndExtra = 8 * sizeof(void *); //2 labels, 2 ListViews, info button
	                                      //texel size, 4x4 texel size, index size
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterNsbtxViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	//wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NsbtxViewerClass";
	wcex.lpfnWndProc = NsbtxViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
	RegisterVramUseClass();
}

HWND CreateNsbtxViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NSBTX nsbtx;
	int n = nsbtxReadFile(&nsbtx, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NsbtxViewerClass", L"NSBTX Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nsbtx);
	return h;
}