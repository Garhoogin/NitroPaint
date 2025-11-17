#include "textureeditor.h"
#include "nsbtxviewer.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "nclrviewer.h"
#include "palette.h"
#include "colorchooser.h"
#include "resource.h"
#include "gdip.h"
#include "texconv.h"
#include "nclr.h"

#include <Shlwapi.h>
#include <ShlObj.h>
#include <commctrl.h>
#include <math.h>

extern HICON g_appIcon;

HWND CreateTexturePaletteEditor(int x, int y, int width, int height, HWND hWndParent, TEXTUREEDITORDATA *data);

// convert a narrow resource name to a wide character string
WCHAR *TexNarrowResourceNameToWideChar(const char *name) {
	//NULL resource name
	if (name == NULL) return NULL;

	unsigned int len = strlen(name);
	WCHAR *buf = (WCHAR *) calloc(len + 1, sizeof(WCHAR));
	if (buf == NULL) return NULL;

	for (unsigned int i = 0; i < len; i++) {
		buf[i] = (WCHAR) name[i];
	}
	return buf;
}

// convert a wide character string to a narrow resource string
char *TexNarrowResourceNameFromWideChar(const WCHAR *name) {
	if (name == NULL) return NULL;

	unsigned int len = wcslen(name);
	char *buf = (char *) calloc(len + 1, sizeof(char));
	if (buf == NULL) return NULL;

	for (unsigned int i = 0; i < len; i++) {
		buf[i] = (char) name[i];
	}
	return buf;
}

static int TexViewerGetContentWidth(TEXTUREEDITORDATA *data) {
	return data->width * data->scale;
}

static int TexViewerGetContentHeight(TEXTUREEDITORDATA *data) {
	return data->height * data->scale;
}

static void TexViewerExportTextureImage(LPCWSTR path, TEXTURE *texture) {
	//export as PNG
	//if texture is palette4, palette16 or palette256, output indexed image with appropriate color 0
	//if texture is direct or 4x4, output non-indexed
	//if texture is a3i5 or a5i3, use a 256-color palette that repeats the palette at varying alphas
	int texImageParam = texture->texels.texImageParam;
	int format = FORMAT(texImageParam);
	int width = TEXW(texImageParam);
	int height = texture->texels.height;

	//buffer to hold converted palette
	int paletteSize = 0;
	COLOR32 palette[256] = { 0 };
	if (format != CT_DIRECT) {
		//convert to 24-bit
		if (format != CT_4x4) {
			paletteSize = texture->palette.nColors;
			if (paletteSize > 256) paletteSize = 256;
			for (int i = 0; i < paletteSize; i++) {
				palette[i] = ColorConvertFromDS(texture->palette.pal[i]);
			}
		}

		//for a3i5 and a5i3, build up varying levels of alpha
		if (format == CT_A3I5 || format == CT_A5I3) {
			int nAlphaLevels = format == CT_A3I5 ? 8 : 32;
			int nColorsPerAlpha = 256 / nAlphaLevels;

			for (int i = 1; i < nAlphaLevels; i++) {
				int alpha = (i * 510 + nAlphaLevels - 1) / (2 * nAlphaLevels - 2); //rounding to nearest
				for (int j = 0; j < paletteSize; j++) {
					COLOR32 c = palette[j];
					c |= alpha << 24;
					palette[j + i * nColorsPerAlpha] = c;
				}
			}
			paletteSize = 256;
		} else if (format == CT_4COLOR || format == CT_16COLOR || format == CT_256COLOR) {
			//make palette opaque except first color if c0xp
			int c0xp = COL0TRANS(texImageParam);
			for (int i = 0; i < paletteSize; i++) {
				if (i || !c0xp) palette[i] |= 0xFF000000;
			}
		} else {
			//else we can't export an indexed image unfortunately (4x4 and direct)
			paletteSize = 0;
		}
	}

	if (format == CT_256COLOR || format == CT_A3I5 || format == CT_A5I3) {
		//prepare image output. For palette256, a3i5 and a5i3, we can export the data as it already is.
		ImgWriteIndexed((unsigned char *) texture->texels.texel, width, height, palette, paletteSize, path);
	} else if (format == CT_4x4 || format == CT_DIRECT) {
		//else if 4x4 or direct, just export full-color image. Red/blue must be swapped here
		COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
		TxRender(px, width, height, &texture->texels, &texture->palette, 0);
		
		ImgWrite(px, width, height, path);
		free(px);
	} else {
		//palette16 or palette4, will need to convert the bit depth
		unsigned char *bits = (unsigned char *) calloc(width * height, 1);
		int depth = format == CT_4COLOR ? 2 : 4;
		int mask = depth == 2 ? 0x3 : 0xF;

		for (int y = 0; y < height; y++) {
			unsigned char *rowSrc = texture->texels.texel + y * width * depth / 8;
			unsigned char *rowDst = bits + y * width;
			for (int x = 0; x < width; x++) {
				rowDst[x] = (rowSrc[x * depth / 8] >> ((x * depth) % 8)) & mask;
			}
		}
		ImgWriteIndexed(bits, width, height, palette, paletteSize, path);
		free(bits);
	}
}

static void TexViewerUpdatePaletteLabel(HWND hWnd) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);

	WCHAR bf[32];
	int len;
	if (data->texture.texture.palette.nColors) {
		len = wsprintfW(bf, L"Palette: %d colors", data->texture.texture.palette.nColors);
		data->hasPalette = TRUE;
	} else {
		len = wsprintfW(bf, L"No palette");
		data->hasPalette = FALSE;
	}
	SendMessage(data->hWndPaletteLabel, WM_SETTEXT, len, (LPARAM) bf);

	len = wsprintfW(bf, L"Format: %S", TxNameFromTexFormat(FORMAT(data->texture.texture.texels.texImageParam)));
	SendMessage(data->hWndFormatLabel, WM_SETTEXT, len, (LPARAM) bf);

	int nColors = ImgCountColors(data->px, data->width * data->height);
	len = wsprintfW(bf, L"Colors: %d", nColors);
	SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) bf);

	int texelVram = TxGetTextureVramSize(&data->texture.texture.texels);
	int paletteVram = TxGetTexPlttVramSize(&data->texture.texture.palette);
	
	//this code is ugly due to being unable to just use %.2f
	len = wsprintfW(bf, L"Texel: %d.%d%dKB", texelVram / 1024, (texelVram * 10 / 1024) % 10,
		((texelVram * 100 + 512) / 1024) % 10);
	SendMessage(data->hWndTexelVram, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"Palette: %d.%d%dKB", paletteVram / 1024, (paletteVram * 10 / 1024) % 10,
		((paletteVram * 100 + 512) / 1024) % 10);
	SendMessage(data->hWndPaletteVram, WM_SETTEXT, len, (LPARAM) bf);
}

static DWORD CALLBACK textureStartConvertThreadEntry(LPVOID lpParam) {
	TxConversionParameters *params = (TxConversionParameters *) lpParam;
	return TxConvert(params);
}

static HANDLE textureConvertThreaded(TxConversionParameters *params){
	return CreateThread(NULL, 0, textureStartConvertThreadEntry, (LPVOID) params, 0, NULL);
}

static TxConversionResult TexViewerModalConvert(TxConversionParameters *params, HWND hWndMain) {
	//modal window
	HWND hWndProgress = CreateWindow(L"CompressionProgress", L"Compressing",
		WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 150, hWndMain, NULL, NULL, NULL);

	//set progress window to conversion operation
	SendMessage(hWndProgress, NV_INITIALIZE, 0, (LPARAM) params);

	//start conversion thread and modal wait
	HANDLE hThread = textureConvertThreaded(params);
	DoModalWait(hWndProgress, hThread);
	CloseHandle(hThread);

	return params->result;
}

static HCURSOR TexViewerGetCursorProc(HWND hWnd, int hit) {
	return LoadCursor(NULL, IDC_ARROW);
}

static void TexViewerTileHoverCallback(HWND hWnd, int tileX, int tileY) {

}

static void TexViewerRender(HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight) {
	//texture image rendered 
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) EditorGetData(hWnd);
	COLOR32 *px = data->px;
	int width = data->width, height = data->height;

	//render texture to framebuffer
	for (int y = 0; y < renderHeight; y++) {
		for (int x = 0; x < renderWidth; x++) {
			COLOR32 c = px[(x + scrollX) / data->scale + ((y + scrollY) / data->scale) * width];

			//alpha blend
			unsigned int a = (c >> 24);
			COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };
			if (a == 0) {
				c = checker[((x ^ y) >> 2) & 1];
			} else if (a < 255) {
				COLOR32 bg = checker[((x ^ y) >> 2) & 1];

				unsigned int r = (((c >>  0) & 0xFF) * a + ((bg >>  0) & 0xFF) * (255 - a) + 127) / 255;
				unsigned int g = (((c >>  8) & 0xFF) * a + ((bg >>  8) & 0xFF) * (255 - a) + 127) / 255;
				unsigned int b = (((c >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (255 - a) + 127) / 255;
				c = r | (g << 8) | (b << 16);
			}

			fb->px[x + y * fb->width] = REVERSE(c);
		}
	}

}

static int TexViewerIsSelectionModeCallback(HWND hWnd) {
	return 0;
}

static void TexViewerUpdateCursorCallback(HWND hWnd, int pxX, int pxY) {

}

static HMENU TexViewerGetPopupMenu(HWND hWnd) {
	return GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 4);
}

static void TexViewerOnCreate(HWND hWnd) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) EditorGetData(hWnd);
	data->scale = 1;
	
	HWND hWndViewer = CreateWindow(L"TexturePreviewClass", L"Texture Preview", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 
		0, 0, 300, 300, hWnd, NULL, NULL, NULL);
	TedInit(&data->ted, hWnd, hWndViewer, 4, 4);

	data->ted.allowSelection = 0;
	data->ted.getCursorProc = TexViewerGetCursorProc;
	data->ted.tileHoverCallback = TexViewerTileHoverCallback;
	data->ted.renderCallback = TexViewerRender;
	data->ted.suppressHighlightCallback = NULL;
	data->ted.isSelectionModeCallback = TexViewerIsSelectionModeCallback;
	data->ted.updateCursorCallback = TexViewerUpdateCursorCallback;
	data->ted.getPopupMenuCallback = TexViewerGetPopupMenu;

	data->hWndFormatLabel = CreateStatic(hWnd, L"Format: none", 310, 10, 100, 22);
	data->hWndConvert = CreateButton(hWnd, L"Convert To...", 310, 37, 100, 22, FALSE);
	data->hWndPaletteLabel = CreateStatic(hWnd, L"No palette", 310, 69, 100, 22);
	data->hWndEditPalette = CreateButton(hWnd, L"Edit Palette", 310, 123, 100, 22, FALSE);
	data->hWndExportNTF = CreateButton(hWnd, L"Export NTF", 310, 150, 100, 22, FALSE);
	data->hWndUniqueColors = CreateStatic(hWnd, L"Colors: 0", 310, 155, 100, 22);

	data->hWndTexelVram = CreateStatic(hWnd, L"Texel: 0KB", 310, 182, 100, 22);
	data->hWndPaletteVram = CreateStatic(hWnd, L"Palette: 0KB", 310, 209, 110, 22);
}

static void TexViewerOnPaint(TEXTUREEDITORDATA *data) {
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
	TedMarginPaint(data->hWnd, (EDITOR_DATA *) data, &data->ted);
}

static LRESULT TexViewerOnSize(TEXTUREEDITORDATA *data, WPARAM wParam, LPARAM lParam) {
	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	MoveWindow(data->ted.hWndViewer, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE,
		rcClient.right - 120 - MARGIN_TOTAL_SIZE, rcClient.bottom - MARGIN_TOTAL_SIZE, FALSE);
	MoveWindow(data->hWndFormatLabel, rcClient.right - 110, 10, 100, 22, TRUE);
	MoveWindow(data->hWndConvert, rcClient.right - 110, 37, 100, 22, TRUE);
	MoveWindow(data->hWndPaletteLabel, rcClient.right - 110, 69, 100, 22, TRUE);
	MoveWindow(data->hWndEditPalette, rcClient.right - 110, 96, 100, 22, TRUE);
	MoveWindow(data->hWndExportNTF, rcClient.right - 110, 123, 100, 22, TRUE);
	MoveWindow(data->hWndUniqueColors, rcClient.right - 110, 155, 100, 22, TRUE);
	MoveWindow(data->hWndTexelVram, rcClient.right - 110, 182, 100, 22, TRUE);
	MoveWindow(data->hWndPaletteVram, rcClient.right - 110, 209, 110, 22, TRUE);

	if (wParam == SIZE_RESTORED) InvalidateRect(data->hWnd, NULL, TRUE); //full update
	return DefMDIChildProc(data->hWnd, WM_SIZE, wParam, lParam);
}

static void TexViewerOnCtlCommand(TEXTUREEDITORDATA *data, HWND hWndControl, int notification) {
	HWND hWnd = data->hWnd;
	if (hWndControl == data->hWndEditPalette) {
		int format = FORMAT(data->texture.texture.texels.texImageParam);
		if (format == CT_DIRECT || format == 0) {
			MessageBox(hWnd, L"No palette for this texture.", L"No palette", MB_ICONERROR);
		} else {
			HWND hWndMdi = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			if (data->hWndPaletteEditor == NULL) {
				data->hWndPaletteEditor = CreateTexturePaletteEditor(CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, hWndMdi, data);
			} else {
				SendMessage(hWndMdi, WM_MDIACTIVATE, (WPARAM) data->hWndPaletteEditor, 0);
			}
		}
	} else if (hWndControl == data->hWndConvert) {
		HWND hWndMain = getMainWindow(hWnd);
		data->hWndConvertDialog = CreateWindow(L"ConvertDialogClass", L"Convert Texture",
			WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_THICKFRAME,
			CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
		SetWindowLongPtr(data->hWndConvertDialog, 0, (LONG_PTR) data);
		SendMessage(data->hWndConvertDialog, NV_INITIALIZE, 0, 0);
		DoModal(data->hWndConvertDialog);
	} else if (hWndControl == data->hWndExportNTF) {
		HWND hWndMain = getMainWindow(hWnd);
		//if not in any format, it cannot be exported.
		if (!data->isNitro) {
			MessageBox(hWnd, L"Texture must be converted.", L"Not converted", MB_ICONERROR);
			return;
		}

		LPWSTR ntftPath = saveFileDialog(hWndMain, L"Save NTFT", L"NTFT Files (*.ntft)\0*.ntft\0All Files\0*.*\0\0", L"ntft");
		if (ntftPath == NULL) return;

		LPWSTR ntfiPath = NULL;
		if (FORMAT(data->texture.texture.texels.texImageParam) == CT_4x4) {
			ntfiPath = saveFileDialog(hWndMain, L"Save NTFI", L"NTFI Files (*.ntfi)\0*.ntfi\0All Files\0*.*\0\0", L"ntfi");
			if (ntfiPath == NULL) {
				free(ntftPath);
				return;
			}
		}

		DWORD dwWritten;
		int texImageParam = data->texture.texture.texels.texImageParam;
		int texelSize = TxGetTexelSize(TEXW(texImageParam), data->texture.texture.texels.height, texImageParam);
		HANDLE hFile = CreateFile(ntftPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, data->texture.texture.texels.texel, texelSize, &dwWritten, NULL);
		CloseHandle(hFile);
		free(ntftPath);

		if (ntfiPath != NULL) {
			hFile = CreateFile(ntfiPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(hFile, data->texture.texture.texels.cmp, texelSize / 2, &dwWritten, NULL);
			CloseHandle(hFile);
			free(ntfiPath);
		}

		//palette export
		if (data->texture.texture.palette.pal != NULL) {
			COLOR *colors = data->texture.texture.palette.pal;
			int nColors = data->texture.texture.palette.nColors;

			HWND hWndMain = getMainWindow(hWnd);
			LPWSTR ntfpPath = saveFileDialog(hWndMain, L"Save NTFP", L"NTFP files (*.ntfp)\0*.ntfp\0All Files\0*.*\0\0", L"ntfp");
			if (ntfpPath == NULL) return;

			DWORD dwWritten;
			HANDLE hFile = CreateFile(ntfpPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(hFile, colors, nColors * 2, &dwWritten, NULL);
			CloseHandle(hFile);

			free(ntfpPath);
		}
	}
}

static void TexViewerCopyTexture(TEXTUREEDITORDATA *data) {
	//texture format
	int fmt = 0;
	if (data->isNitro) fmt = FORMAT(data->texture.texture.texels.texImageParam);

	//color-0 transparency
	int c0xp = 0;
	if (fmt >= CT_4COLOR && fmt <= CT_256COLOR && COL0TRANS(data->texture.texture.texels.texImageParam)) c0xp = 1;

	//if the texture is not converted or is direct/4x4, we will copy a direct color bitmap.
	if (!data->isNitro || fmt == CT_DIRECT || fmt == CT_4x4) {
		copyBitmap(data->px, data->width, data->height);
		return;
	}

	//we will write an indexed color bitmap. 
	COLOR32 pltbuf[256] = { 0 };
	for (int i = 0; i < data->texture.texture.palette.nColors && i < 0x100; i++) {
		COLOR32 c = ColorConvertFromDS(data->texture.texture.palette.pal[i]);

		//set alpha bits (except color 0 of a c0xp-marked palette)
		if (i > 0 || !c0xp) c |= 0xFF000000;
		pltbuf[i] = c;
	}

	//for a3i5 and a5i3 textures, we will duplicate colors of the palette at varying alpha levels.
	if (fmt == CT_A3I5 || fmt == CT_A5I3) {
		unsigned int alphaBit = (fmt == CT_A3I5) ? 3 : 5;
		unsigned int indexBit = 8 - alphaBit;

		unsigned int indexMax = (1 << indexBit) - 1;
		unsigned int alphaMax = (1 << alphaBit) - 1;
		for (unsigned int i = 0; i <= indexMax; i++) {
			COLOR32 c = pltbuf[i] & 0x00FFFFFF;

			//all alphas
			for (unsigned int j = 0; j <= alphaMax; j++) {
				unsigned int a = (j * 510 + alphaMax) / (2 * alphaMax);
				pltbuf[i + (j << indexBit)] = c | (a << 24);
			}
		}
	}

	//create bitmap data for texture image.
	unsigned int nBpp = 8;
	if (fmt == CT_4COLOR) nBpp = 2;
	if (fmt == CT_16COLOR) nBpp = 4;

	unsigned int width = TEXW(data->texture.texture.texels.texImageParam);
	unsigned int height = TEXH(data->texture.texture.texels.texImageParam);
	unsigned char *bmp = (unsigned char *) calloc(width * height, sizeof(char));

	unsigned int nPltt = 256;
	if (nBpp == 8) {
		//copy direct
		memcpy(bmp, data->texture.texture.texels.texel, width * height);
	} else {
		//copy with bit conversion
		unsigned int pxPerByte = 8 / nBpp;
		unsigned int pxMask = (1 << nBpp) - 1;
		for (unsigned int i = 0; i < width * height; i++) {
			bmp[i] = (data->texture.texture.texels.texel[i / pxPerByte] >> ((i % pxPerByte) * nBpp)) & pxMask;
		}

		if (nPltt >= (1u << nBpp)) nPltt = 1u << nBpp;
	}

	ClipCopyBitmapEx(bmp, width, height, 1, pltbuf, nPltt);

	free(bmp);
}

static void TexViewerOnMenuCommand(TEXTUREEDITORDATA *data, int idMenu) {
	HWND hWnd = data->hWnd;
	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
		case ID_ZOOM_100:
		case ID_ZOOM_200:
		case ID_ZOOM_400:
		case ID_ZOOM_800:
		case ID_ZOOM_1600:
			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			TedUpdateMargins(&data->ted);
			break;
		case ID_FILE_SAVE:
			if (!data->isNitro) {
				MessageBox(hWnd, L"Texture must be converted.", L"Not converted", MB_ICONERROR);
				break;
			}
			EditorSave(hWnd);
			break;
		case ID_FILE_SAVEAS:
			if (!data->isNitro) {
				MessageBox(hWnd, L"Texture must be converted.", L"Not converted", MB_ICONERROR);
				break;
			}
			EditorSaveAs(hWnd);
			break;
		case ID_FILE_EXPORT:
		{
			//PNG export
			HWND hWndMain = getMainWindow(hWnd);
			LPWSTR path = saveFileDialog(hWndMain, L"Export Texture", L"PNG files (*.png)\0*.png\0All Files\0*.*\0", L".png");
			if (path == NULL) break;

			//if texture is in DS format, export from texture data
			if (data->isNitro) {
				TexViewerExportTextureImage(path, &data->texture.texture);
			} else {
				ImgWrite(data->px, data->width, data->height, path);
			}
			free(path);
			break;
		}
		case ID_TEXTUREMENU_COPY:
		{
			OpenClipboard(hWnd);
			EmptyClipboard();
			TexViewerCopyTexture(data);
			CloseClipboard();
			break;
		}
	}
}

static void TexViewerOnCommand(TEXTUREEDITORDATA *data, WPARAM wParam, LPARAM lParam) {
	if (lParam) {
		TexViewerOnCtlCommand(data, (HWND) lParam, HIWORD(wParam));
	} else if (HIWORD(wParam) == 0) {
		TexViewerOnMenuCommand(data, LOWORD(wParam));
	}
}

static LRESULT CALLBACK TextureEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) EditorGetData(hWnd);
	switch (msg) {
		case WM_CREATE:
			TexViewerOnCreate(hWnd);
			break;
		case WM_PAINT:
			TexViewerOnPaint(data);
			break;
		case WM_ERASEBKGND:
			return TedMainOnEraseBkgnd((EDITOR_DATA *) data, &data->ted, wParam, lParam);
		case WM_SIZE:
			return TexViewerOnSize(data, wParam, lParam);
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			TedMainOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
			break;
		case NV_INITIALIZE:
		{
			TxInit(&data->texture, TEXTURE_TYPE_NNSTGA);
			data->width = wParam & 0xFFFF;
			data->height = (wParam >> 16) & 0xFFFF;
			data->px = (COLOR32 *) lParam;
			data->hasPalette = FALSE;
			data->frameData.contentWidth = data->width;
			data->frameData.contentHeight = data->height;
			data->ted.tilesX = data->width / 4;
			data->ted.tilesY = data->height / 4;

			//check: is it a Nitro TGA?
			if (!TxReadFile(&data->texture, data->szInitialFile)) {
				int format = FORMAT(data->texture.texture.texels.texImageParam);
				EditorSetFile(hWnd, data->szInitialFile);
				data->hasPalette = (format != CT_DIRECT && format != 0);
				data->isNitro = 1;
				TxRender(data->px, data->width, data->height, &data->texture.texture.texels, &data->texture.texture.palette, 0);
				TexViewerUpdatePaletteLabel(hWnd);
			}

			WCHAR buffer[16];
			int nColors = ImgCountColors(data->px, data->width * data->height);
			int len = wsprintfW(buffer, L"Colors: %d", nColors);
			SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) buffer);

			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		}
		case NV_INITIALIZE_IMMEDIATE:
		{
			//set texture data directly
			TextureObject *texture = (TextureObject *) lParam;
			memcpy(&data->texture, texture, sizeof(TextureObject));
			data->isNitro = 1;
			data->hasPalette = FORMAT(texture->texture.texels.texImageParam) != CT_DIRECT;
			data->width = TEXW(texture->texture.texels.texImageParam);
			data->height = texture->texture.texels.height;
			data->frameData.contentWidth = data->width;
			data->frameData.contentHeight = data->height;
			data->px = (COLOR32 *) calloc(data->width * data->height, sizeof(COLOR32));
			data->ted.tilesX = data->width / 4;
			data->ted.tilesY = data->height / 4;

			//decode texture data for preview
			int nPx = data->width * data->height;
			TxRender(data->px, data->width, data->height, &texture->texture.texels, &texture->texture.palette, 0);

			//update UI
			WCHAR buffer[16];
			int nColors = ImgCountColors(data->px, data->width * data->height);
			int len = wsprintfW(buffer, L"Colors: %d", nColors);
			SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) buffer);

			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			TexViewerUpdatePaletteLabel(hWnd);
			break;
		}
		case NV_SETPATH:
			memcpy(data->szInitialFile, (LPWSTR) lParam, 2 * wParam + 2);
			break;
		case WM_COMMAND:
			TexViewerOnCommand(data, wParam, lParam);
			break;
		case WM_LBUTTONDOWN:
			TedOnLButtonDown((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_LBUTTONUP:
			TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_DESTROY:
		{
			if (data->hWndPaletteEditor) DestroyChild(data->hWndPaletteEditor);
			if (data->hWndTileEditor) DestroyChild(data->hWndTileEditor);
			SetWindowLongPtr(data->ted.hWndViewer, 0, 0);
			free(data->px);
			TedDestroy(&data->ted);
			break;
		}
		case NV_GETTYPE:
			return FILE_TYPE_TEXTURE;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

static HWND CreateTextureTileEditor(HWND hWndParent, int tileX, int tileY) {
	HWND hWndMdi = (HWND) GetWindowLongPtr(hWndParent, GWL_HWNDPARENT);
	
	HWND hWnd = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TextureTileEditorClass", L"Tile Editor", 
							   WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT,
							   500, 300, hWndMdi, NULL, NULL, NULL);
	SetWindowLongPtr(hWnd, 0, (LONG_PTR) hWndParent);
	SetWindowLongPtr(hWnd, sizeof(LONG_PTR), (LONG_PTR) tileX);
	SetWindowLongPtr(hWnd, 2 * sizeof(LONG_PTR), (LONG_PTR) tileY);
	SendMessage(hWnd, NV_INITIALIZE, 0, 0);
	ShowWindow(hWnd, SW_SHOW);
	return hWnd;
}

static int getTextureOffsetByTileCoordinates(TEXELS *texel, int x, int y) {
	int fmt = FORMAT(texel->texImageParam);
	int width = TEXW(texel->texImageParam);

	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };

	if (fmt != CT_4x4) {
		int pxX = x * 4;
		int pxY = y * 4;
		return (pxX + pxY * width) * bits[fmt] / 8;
	}

	int tileNumber = x + (width / 4) * y;
	int tileOffset = tileNumber * 4;
	return tileOffset;
}

int ilog2(int x);

static void DrawColorEntryAlpha(HDC hDC, HPEN hOutline, COLOR color, float alpha, int x, int y) {
	HPEN hOldPen = (HPEN) SelectObject(hDC, hOutline);
	HPEN hNullPen = GetStockObject(NULL_PEN);
	HBRUSH hNullBrush = GetStockObject(NULL_BRUSH);
	HBRUSH hOldBrush = (HBRUSH) SelectObject(hDC, hNullBrush);

	if (alpha == 1.0f) {
		HBRUSH hBg = CreateSolidBrush((COLORREF) ColorConvertFromDS(color));
		SelectObject(hDC, hBg);
		Rectangle(hDC, x, y, x + 16, y + 16);
		DeleteObject(hBg);
	} else {
		COLOR32 c = ColorConvertFromDS(color);
		int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;

		int wr = (int) (r * alpha + 255 * (1.0f - alpha) + 0.5f);
		int wg = (int) (g * alpha + 255 * (1.0f - alpha) + 0.5f);
		int wb = (int) (b * alpha + 255 * (1.0f - alpha) + 0.5f);
		int gr = (int) (r * alpha + 192 * (1.0f - alpha) + 0.5f);
		int gg = (int) (g * alpha + 192 * (1.0f - alpha) + 0.5f);
		int gb = (int) (b * alpha + 192 * (1.0f - alpha) + 0.5f);
		HBRUSH hbrWhite = CreateSolidBrush(RGB(wr, wg, wb));
		HBRUSH hbrGray = CreateSolidBrush(RGB(gr, gg, gb));

		SelectObject(hDC, hbrWhite);
		Rectangle(hDC, x, y, x + 16, y + 16);
		SelectObject(hDC, hbrGray);
		SelectObject(hDC, hNullPen);
		Rectangle(hDC, x + 8, y + 1, x + 16, y + 9);
		Rectangle(hDC, x + 1, y + 8, x + 9, y + 16);

		DeleteObject(hbrWhite);
		DeleteObject(hbrGray);
	}

	SelectObject(hDC, hOldPen);
	SelectObject(hDC, hOldBrush);
}

static void PaintTextureTileEditor(HDC hDC, TEXTURE *texture, int tileX, int tileY, int colorIndex, int alphaIndex) {
	//first paint 4x4 tile (scaled 32x)
	unsigned char tileBuffer[128]; //big enough for an 8x8 texture of any format
	unsigned short indexBuffer[4] = { 0 }; //big enough for an 8x8 4x4 texture
	COLOR32 rendered[64];

	int param = texture->texels.texImageParam;
	int format = FORMAT(param), width = TEXW(param);
	int offset = getTextureOffsetByTileCoordinates(&texture->texels, tileX, tileY);
	unsigned char *texelSrc = texture->texels.texel + offset;
	if (format == CT_4x4) indexBuffer[0] = texture->texels.cmp[offset / 4];

	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
	int nBytesPerRow = 8 * bits[format] / 8;

	if (format != CT_4x4) {
		for (int y = 0; y < 4; y++) {
			memcpy(tileBuffer + y * nBytesPerRow, texelSrc + (y * width * bits[format] / 8), nBytesPerRow / 2);
		}
	} else {
		memcpy(tileBuffer, texelSrc, 4);
	}

	//assemble texture struct
	TEXTURE temp;
	temp.texels.cmp = indexBuffer;
	temp.texels.texel = tileBuffer;
	temp.texels.texImageParam = format << 26;
	temp.palette.nColors = texture->palette.nColors;
	temp.palette.pal = texture->palette.pal;
	TxRender(rendered, 8, 8, &temp.texels, &temp.palette, 0);
	ImgSwapRedBlue(rendered, 8, 8);

	//convert back to 4x4
	memmove(rendered + 0, rendered + 0, 16);
	memmove(rendered + 4, rendered + 8, 16);
	memmove(rendered + 8, rendered + 16, 16);
	memmove(rendered + 12, rendered + 24, 16);

	const scale = 32;
	COLOR32 *preview = (COLOR32 *) calloc(4 * 4 * scale * scale, sizeof(COLOR32));
	for (int y = 0; y < 4 * scale; y++) {
		for (int x = 0; x < 4 * scale; x++) {
			int sampleX = x / scale;
			int sampleY = y / scale;
			COLOR32 c = rendered[sampleX + sampleY * 4];

			int gray = ((x / 4) ^ (y / 4)) & 1;
			gray = gray ? 255 : 192;
			int alpha = (c >> 24) & 0xFF;
			if (alpha == 0) {
				preview[x + y * 4 * scale] = gray | (gray << 8) | (gray << 16);
			} else if (alpha == 255) {
				preview[x + y * 4 * scale] = c & 0xFFFFFF;
			} else {
				int r = c & 0xFF;
				int g = (c >> 8) & 0xFF;
				int b = (c >> 16) & 0xFF;
				r = (r * alpha + gray * (255 - alpha) + 127) / 255;
				g = (g * alpha + gray * (255 - alpha) + 127) / 255;
				b = (b * alpha + gray * (255 - alpha) + 127) / 255;
				preview[x + y * 4 * scale] = r | (g << 8) | (b << 16);
			}
		}
	}

	HBITMAP hBitmap = CreateBitmap(4 * scale, 4 * scale, 1, 32, preview);
	HDC hOffDC = CreateCompatibleDC(hDC);
	SelectObject(hOffDC, hBitmap);
	BitBlt(hDC, 0, 0, 4 * scale, 4 * scale, hOffDC, 0, 0, SRCCOPY);
	DeleteObject(hOffDC);
	DeleteObject(hBitmap);
	free(preview);

	//draw palette
	int nColors = texture->palette.nColors, transparentIndex = COL0TRANS(texture->texels.texImageParam) - 1;
	COLOR *pal = texture->palette.pal;
	COLOR stackPaletteBuffer[4];
	if (format == CT_4x4) {
		unsigned short mode = indexBuffer[0] & COMP_MODE_MASK;
		pal = stackPaletteBuffer;
		nColors = 4;
		transparentIndex = (mode == 0x0000 || mode == 0x4000) ? 3 : -1;
		int paletteIndex = (indexBuffer[0] & COMP_INDEX_MASK) << 1;
		COLOR *palSrc = texture->palette.pal + paletteIndex;

		pal[0] = palSrc[0];
		pal[1] = palSrc[1];
		switch (mode) {
			case COMP_TRANSPARENT | COMP_FULL:
				pal[2] = palSrc[2];
				pal[3] = 0;
				break;
			case COMP_TRANSPARENT | COMP_INTERPOLATE:
				pal[2] = ColorInterpolate(pal[0], pal[1], 0.5f);
				pal[3] = 0;
				break;
			case COMP_OPAQUE | COMP_FULL:
				pal[2] = palSrc[2];
				pal[3] = palSrc[3];
				break;
			case COMP_OPAQUE | COMP_INTERPOLATE:
				pal[2] = ColorInterpolate(pal[0], pal[1], 0.375f);
				pal[3] = ColorInterpolate(pal[0], pal[1], 0.625f);
				break;
		}
	}

	//draw palette entries if not direct
	int selectedColor = colorIndex;
	HPEN hBlack = (HPEN) GetStockObject(BLACK_PEN);
	HPEN hWhite = (HPEN) GetStockObject(WHITE_PEN);
	if (format != CT_DIRECT) {
		for (int i = 0; i < nColors; i++) {
			if(i != selectedColor) SelectObject(hDC, hBlack);
			else SelectObject(hDC, hWhite);

			int x = 4 * scale + 10 + (i % 16) * 16;
			int y = (i / 16) * 16;
			DrawColorEntryAlpha(hDC, (i != selectedColor) ? hBlack : hWhite, pal[i], i == transparentIndex ? 0.0f : 1.0f, x, y);
		}
	}

	//draw alpha levels if a3i5 or a5i3
	int selectedAlpha = alphaIndex;
	if (format == CT_A3I5 || format == CT_A5I3) {
		COLOR selected = pal[selectedColor];
		int nLevels = (format == CT_A3I5) ? 8 : 32;
		for (int i = 0; i < nLevels; i++) {
			int x = 4 * scale + 10 + (i % 16) * 16;
			int y = 42 + (i / 16) * 16;
			float alpha = ((float) i) / (nLevels - 1);
			DrawColorEntryAlpha(hDC, (i != selectedAlpha) ? hBlack : hWhite, selected, alpha, x, y);
		}
	}
}

static LRESULT CALLBACK TextureTileEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndTextureEditor = (HWND) GetWindowLongPtr(hWnd, 0);
	if (hWndTextureEditor == NULL) return DefMDIChildProc(hWnd, msg, wParam, lParam);

	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWndTextureEditor, 0);
	int tileX = GetWindowLongPtr(hWnd, sizeof(LONG_PTR));
	int tileY = GetWindowLongPtr(hWnd, 2 * sizeof(LONG_PTR));

	switch (msg) {
		case WM_CREATE:
			SetWindowSize(hWnd, 398, 260);
			break;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);
			
			if(hWndTextureEditor != NULL)
				PaintTextureTileEditor(hDC, &data->texture.texture, tileX, tileY, data->selectedColor, data->selectedAlpha);

			EndPaint(hWnd, &ps);
			break;
		}
		case NV_INITIALIZE:
		{
			SetWindowSize(hWnd, 398, 260);

			TEXELS *texels = &data->texture.texture.texels;
			int format = FORMAT(texels->texImageParam);
			if (format == CT_4x4) {
				data->hWndInterpolate = CreateCheckbox(hWnd, L"Interpolate", 5, 133, 100, 22, FALSE);
				data->hWndTransparent = CreateCheckbox(hWnd, L"Transparent", 5, 160, 100, 22, FALSE);
				CreateStatic(hWnd, L"Palette base:", 5, 187, 60, 22);
				data->hWndPaletteBase = CreateEdit(hWnd, L"0", 70, 187, 50, 22, TRUE);
				SetGUIFont(hWnd);

				//populate fields
				int tilesX = TEXW(texels->texImageParam) / 4;
				uint16_t idx = texels->cmp[tileX + tileY * tilesX];
				if (!(idx & COMP_OPAQUE))
					SendMessage(data->hWndTransparent, BM_SETCHECK, 1, 0);
				if (idx & COMP_INTERPOLATE)
					SendMessage(data->hWndInterpolate, BM_SETCHECK, 1, 0);
				SetEditNumber(data->hWndPaletteBase, idx & COMP_INDEX_MASK);
			}
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				TEXELS *texels = &data->texture.texture.texels;
				int width = TEXW(texels->texImageParam);
				int tilesX = width / 4;
				uint16_t *pIdx = &texels->cmp[tileX + tileY * tilesX];

				int notification = HIWORD(wParam);
				if (notification == BN_CLICKED && hWndControl == data->hWndTransparent) {
					int state = GetCheckboxChecked(hWndControl);
					*pIdx = ((*pIdx) & 0x7FFF) | ((!state) << 15);
					TxRender(data->px, data->width, data->height, texels, &data->texture.texture.palette, 0);
					InvalidateRect(data->hWnd, NULL, FALSE);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndInterpolate) {
					int state = GetCheckboxChecked(hWndControl);
					*pIdx = ((*pIdx) & 0xBFFF) | (state << 14);
					TxRender(data->px, data->width, data->height, texels, &data->texture.texture.palette, 0);
					InvalidateRect(data->hWnd, NULL, FALSE);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == EN_CHANGE && hWndControl == data->hWndPaletteBase) {
					*pIdx = ((*pIdx) & 0xC000) | (GetEditNumber(hWndControl) & 0x3FFF);
					TxRender(data->px, data->width, data->height, texels, &data->texture.texture.palette, 0);
					InvalidateRect(data->hWnd, NULL, FALSE);
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}
			break;
		}
		case WM_NCHITTEST:
		{
			int ht = DefMDIChildProc(hWnd, msg, wParam, lParam);
			if (ht == HTTOP || ht == HTBOTTOM || ht == HTLEFT || ht == HTRIGHT || ht == HTTOPLEFT || ht == HTTOPRIGHT
				|| ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT) {
				return HTBORDER;
			}
			return ht;
		}
		case WM_LBUTTONDOWN:
		{
			SetCapture(hWnd);
			data->tileMouseDown = 1;
		}
		case WM_MOUSEMOVE:
		{
			if (!data->tileMouseDown) break;

			TEXELS *texels = &data->texture.texture.texels;
			PALETTE *palette = &data->texture.texture.palette;
			int format = FORMAT(texels->texImageParam);
			int width = TEXW(texels->texImageParam);

			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);

			if (pt.x >= 0 && pt.y >= 0 && pt.x < 128 && pt.y < 128) { //draw pixel
				int masks[] = { 0, 0xFF, 0x03, 0x0F, 0xFF, 0x03, 0xFF, 0xFFFF };
				int depths[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
				int x = pt.x / 32;
				int y = pt.y / 32;

				//get pointer to containing u32
				int offset = getTextureOffsetByTileCoordinates(texels, tileX, tileY);
				unsigned char *pTile = texels->texel + offset;
				if (format == CT_4x4) {
					int index = x + y * 4;
					uint32_t *pTileCmp = (uint32_t *) pTile;
					*pTileCmp = ((*pTileCmp) & ~(3 << (index * 2))) | (data->selectedColor << (index * 2));
				} else {
					if (format == CT_A3I5 || format == CT_A5I3) {
						int alphaShift = (format == CT_A3I5) ? 5 : 3;
						int value = data->selectedColor | (data->selectedAlpha << alphaShift);
						unsigned char *pPx = pTile + y * width + x;
						*pPx = value;
					} else if (format == CT_4COLOR || format == CT_16COLOR || format == CT_256COLOR) {
						int depth = depths[format];
						unsigned char *pTexel = pTile + (y * width * depth / 8) + x * depth / 8;
						int pxAdvance = x % (8 / depth);
						int mask = (1 << depth) - 1;
						*pTexel = ((*pTexel) & ~(mask << (pxAdvance * depth))) | (data->selectedColor << (pxAdvance * depth));
					} else {
						if (msg == WM_LBUTTONDOWN) { //we don't really want click+drag for this one
							HWND hWndMain = getMainWindow(hWnd);
							COLOR *color = (COLOR *) (pTile + y * width * 2 + x * 2);
							CHOOSECOLOR cc = { 0 };
							cc.lStructSize = sizeof(cc);
							cc.hInstance = (HWND) (HINSTANCE) GetWindowLongPtr(hWnd, GWL_HINSTANCE); //weird struct definition?
							cc.hwndOwner = hWndMain;
							cc.rgbResult = ColorConvertFromDS(*color);
							cc.lpCustColors = data->tmpCust;
							cc.Flags = 0x103;
							BOOL(__stdcall *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
							if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;
							if (ChooseColorFunction(&cc)) {
								COLOR32 result = cc.rgbResult;
								*color = 0x8000 | ColorConvertToDS(result);
							}
						}
					}
				}
				TxRender(data->px, data->width, data->height, texels, &data->texture.texture.palette, 0);
				InvalidateRect(data->hWnd, NULL, FALSE);
				InvalidateRect(hWnd, NULL, FALSE);
			} else if (pt.x >= 138 && pt.y >= 0) { //select palette/alpha
				int nColors = palette->nColors;
				if (format == CT_4x4) nColors = 4;
				
				int x = (pt.x - 138) / 16;
				int y = pt.y / 16;
				int index = x + y * 16;
				if (index < nColors) {
					data->selectedColor = index;
					InvalidateRect(hWnd, NULL, FALSE);
				} else if((format == CT_A3I5 || format == CT_A5I3) && pt.y >= 42)  {
					int nLevels = (format == CT_A3I5) ? 8 : 32;
					y = (pt.y - 42) / 16;
					index = x + y * 16;
					if (index < nLevels) {
						data->selectedAlpha = index;
						InvalidateRect(hWnd, NULL, FALSE);
					}
				}
			}
			
			
			break;
		}
		case NV_GETTYPE:
			return FILE_TYPE_TEXTURE;
		case WM_LBUTTONUP:
		{
			data->tileMouseDown = 0;
			ReleaseCapture();
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK TexturePreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) EditorGetData((HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT));
	int contentWidth = 0, contentHeight = 0;
	if (data != NULL) {
		contentWidth = data->width * data->scale;
		contentHeight = data->height * data->scale;
	}

	//little hack for code reuse >:)
	FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
	if (frameData == NULL) {
		frameData = calloc(1, sizeof(FRAMEDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) frameData);
	}
	frameData->contentWidth = contentWidth;
	frameData->contentHeight = contentHeight;

	switch (msg) {
		case WM_CREATE:
			SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_HSCROLL | WS_VSCROLL);
			break;
		case WM_PAINT:
			TedOnViewerPaint((EDITOR_DATA *) data, &data->ted);
			return 1;
		case WM_LBUTTONDOWN:
		{
			TedViewerOnLButtonDown((EDITOR_DATA *) data, &data->ted);
			if (!data->isNitro) break;

			int x = data->ted.hoverX;
			int y = data->ted.hoverY;
			int texImageParam = data->texture.texture.texels.texImageParam;
			if (x >= 0 && y >= 0 && x < data->ted.tilesX && y < data->ted.tilesY) {
				HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				
				if (data->hWndTileEditor != NULL) DestroyChild(data->hWndTileEditor);
				data->hWndTileEditor = CreateTextureTileEditor(hWndEditor, x, y);
			}
			break;
		}
		case WM_LBUTTONUP:
			TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
			break;
		case WM_RBUTTONDOWN:
		{
			//mark hovered tile
			TedOnRButtonDown(&data->ted);

			//context menu
			TedTrackPopup((EDITOR_DATA *) data, &data->ted);
			break;
		}
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		case WM_MOUSELEAVE:
			TedViewerOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
			break;
		case WM_SETCURSOR:
			return TedSetCursor((EDITOR_DATA *) data, &data->ted, wParam, lParam);
		case NV_RECALCULATE:
		{
			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			SendMessage(hWnd, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
			break;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			TedUpdateMargins(&data->ted);
			TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
			return DefChildProc(hWnd, msg, wParam, lParam);
		case WM_SIZE:
		{
			UpdateScrollbarVisibility(hWnd);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static int TexViewerImageHasTranslucentPixels(COLOR32 *px, int nWidth, int nHeight) {
	for (int i = 0; i < nWidth * nHeight; i++) {
		int a = px[i] >> 24;
		if (a >= 5 && a <= 250) return 1;
	}
	return 0;
}

static int TexViewerJudgeColor0Mode(COLOR32 *px, int width, int height) {
	for (int i = 0; i < width * height; i++) {
		if ((px[i] >> 24) < 0x80) return 1; // transparent
	}
	return 0;
}

static int TexViewerJudgeFormat(COLOR32 *px, int nWidth, int nHeight) {
	//Guess a good format for the data. Default to 4x4.
	int fmt = CT_4x4;
	
	//if the texture is 1024x1024, do not choose 4x4.
	if (nWidth * nHeight == 1024 * 1024) fmt = CT_256COLOR;

	//is there translucency?
	if (TexViewerImageHasTranslucentPixels(px, nWidth, nHeight)) {
		//then choose a3i5 or a5i3. Do this by using color count.
		unsigned int colorCount = ImgCountColorsEx(px, nWidth, nHeight, 
			IMG_CCM_IGNORE_ALPHA | IMG_CCM_NO_IGNORE_TRANSPARENT_COLOR | IMG_CCM_NO_COUNT_TRANSPARENT);
		if (colorCount < 16) {
			//colors < 16, choose a5i3.
			fmt = CT_A5I3;
		} else {
			//otherwise, choose a3i5.
			fmt = CT_A3I5;
		}
	} else {
		//weigh the other format options for optimal size.
		unsigned int nColors = ImgCountColorsEx(px, nWidth, nHeight, 0);
		
		//if <= 4 colors, choose 4-color.
		if (nColors <= 4) {
			fmt = CT_4COLOR;
		} else {
			//weigh 16-color, 256-color, and 4x4. 
			if ((nWidth * nHeight) <= 1024 * 512) {
				//unfrt 1024x512/512x1024: use 4x4
				fmt = CT_4x4;
			} else if (nColors < 32) {
				//not more than 32 colors: use palette16
				fmt = CT_16COLOR;
			} else {
				//otherwise, use palette256
				fmt = CT_256COLOR;
			}
		}
	}

	return fmt;
}

static void createPaletteName(WCHAR *buffer, WCHAR *file) {
	//find the last \ or /
	int index = -1;
	unsigned int i;
	for (i = 0; i < wcslen(file); i++) {
		if (file[i] == L'\\' || file[i] == L'/') index = i;
	}
	file += index + 1;

	WCHAR *lastDot = wcsrchr(file, L'.');
	//copy up to 12 characters of the file name
	for (i = 0; i < 12; i++) {
		WCHAR c = file[i];
		if (c == L'\0' || file + i == lastDot) break;
		buffer[i] = c;
	}
	//suffix _pl
	memcpy(buffer + i, L"_pl", 6);
}

#ifdef _MSC_VER

float mylog2(float d) { //UGLY!
	float ans;
	_asm {
		fld1
		fld dword ptr [d]
		fyl2x
		fstp dword ptr [ans]
	}
	return ans;
}
#define log2 mylog2

#endif //_MSC_VER

static int TexViewerJudgeColorCount(int bWidth, int bHeight) {
	int area = bWidth * bHeight;

	//for textures smaller than 256x256, use 8*sqrt(area)
	if (area <= 128 * 128) {
		int nColors = (int) (8 * sqrt((float) area));
		nColors = (nColors + 15) & ~15;
		return nColors;
	}

	//larger sizes, increase by 256 every width/height increment
	return (int) (256 * (log2((float) area) - 10));
}

static void updateConvertDialog(TEXTUREEDITORDATA *data) {
	int fmt = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0) + 1;
	BOOL isTranslucent = fmt == CT_A3I5 || fmt == CT_A5I3;
	BOOL isPlttN = fmt == CT_4COLOR || fmt == CT_16COLOR || fmt == CT_256COLOR;
	BOOL isPltt = fmt != CT_DIRECT;
	BOOL is4x4 = fmt == CT_4x4;

	BOOL fixedPalette = GetCheckboxChecked(data->hWndFixedPalette) && isPltt;
	BOOL dither = GetCheckboxChecked(data->hWndDither);
	BOOL ditherAlpha = GetCheckboxChecked(data->hWndDitherAlpha) && isTranslucent;
	BOOL limitPalette = GetCheckboxChecked(data->hWndLimitPalette) && is4x4 && !fixedPalette;

	EnableWindow(data->hWndDitherAlpha, isTranslucent);
	EnableWindow(data->hWndColorEntries, limitPalette);
	EnableWindow(data->hWndOptimizationSlider, is4x4 && !fixedPalette);
	EnableWindow(data->hWndPaletteName, isPltt);
	EnableWindow(data->hWndFixedPalette, isPltt);
	EnableWindow(data->hWndPaletteInput, fixedPalette);
	EnableWindow(data->hWndPaletteBrowse, fixedPalette);
	EnableWindow(data->hWndPaletteSize, isPltt && !is4x4 && !fixedPalette);
	EnableWindow(data->hWndLimitPalette, is4x4 && !fixedPalette);
	EnableWindow(data->balance.hWndBalance, isPltt);
	EnableWindow(data->balance.hWndColorBalance, isPltt);
	EnableWindow(data->balance.hWndEnhanceColors, isPltt);

	EnableWindow(data->hWndDiffuseAmount, dither || ditherAlpha);

	//paletteN formats: enable color 0 mode
	EnableWindow(data->hWndColor0Transparent, isPlttN);

	//when alpha key is enabled, enable the select button
	EnableWindow(data->hWndSelectAlphaKey, GetCheckboxChecked(data->hWndCheckboxAlphaKey));

	SetFocus(data->hWndConvertDialog);
}

static LRESULT CALLBACK ConvertDialogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case WM_CREATE:
			SetWindowSize(hWnd, 490, 444);
			break;
		case NV_INITIALIZE:
		{
			int boxWidth = 100 + 100 + 10 + 10 + 10; //box width
			int boxHeight = 5 * 27 - 5 + 10 + 10 + 10; //first row height
			int boxHeight2 = 3 * 27 - 5 + 10 + 10 + 10; //second row height
			int boxHeight3 = 3 * 27 - 5 + 10 + 10 + 10; //third row height
			int width = 30 + 2 * boxWidth; //window width
			int height = 10 + boxHeight + 10 + boxHeight2 + 10 + boxHeight3 + 10 + 22 + 10; //window height

			int leftX = 10 + 10; //left box X
			int rightX = 10 + boxWidth + 10 + 10; //right box X
			int topY = 10 + 10 + 8; //top box Y
			int middleY = boxHeight + 10 + topY; //middle box Y
			int bottomY = boxHeight + 10 + boxHeight2 + 10 + topY; //bottom box Y

			CreateStatic(hWnd, L"Format:", leftX, topY, 75, 22);
			data->hWndFormat = CreateCombobox(hWnd, NULL, 0, leftX + 85, topY, 100, 22, 0);
			data->hWndDither = CreateCheckbox(hWnd, L"Dither", leftX, topY + 27, 100, 22, FALSE);
			data->hWndDitherAlpha = CreateCheckbox(hWnd, L"Dither Alpha", leftX, topY + 27 * 2, 100, 22, FALSE);
			CreateStatic(hWnd, L"Diffusion:", leftX, topY + 27 * 3, 75, 22);
			data->hWndDiffuseAmount = CreateEdit(hWnd, L"100", leftX + 85, topY + 27 * 3, 100, 22, TRUE);
			data->hWndCheckboxAlphaKey = CreateCheckbox(hWnd, L"Alpha Key:", leftX, topY + 27 * 4, 85, 22, FALSE);
			data->hWndSelectAlphaKey = CreateButton(hWnd, L"...", leftX + 85, topY + 27 * 4, 50, 22, FALSE);

			CreateStatic(hWnd, L"Palette Name:", rightX, topY, 75, 22);
			data->hWndPaletteName = CreateEdit(hWnd, L"", rightX + 85, topY, 100, 22, FALSE);
			data->hWndFixedPalette = CreateCheckbox(hWnd, L"Use Fixed Palette", rightX, topY + 27, 100, 22, FALSE);
			CreateStatic(hWnd, L"Palette File:", rightX, topY + 27 * 2, 75, 22);
			data->hWndPaletteInput = CreateEdit(hWnd, L"", rightX + 85, topY + 27 * 2, 75, 22, FALSE);
			data->hWndPaletteBrowse = CreateButton(hWnd, L"...", rightX + 85 + 75, topY + 27 * 2, 25, 22, FALSE);
			CreateStatic(hWnd, L"Colors:", rightX, topY + 27 * 3, 75, 22);
			data->hWndPaletteSize = CreateEdit(hWnd, L"256", rightX + 85, topY + 27 * 3, 100, 22, TRUE);
			data->hWndColor0Transparent = CreateCheckbox(hWnd, L"Color 0 is Transparent", rightX, topY + 27 * 4, 150, 22, FALSE);

			data->hWndLimitPalette = CreateCheckbox(hWnd, L"Limit Palette Size", leftX, middleY, 100, 22, TRUE);
			CreateStatic(hWnd, L"Maximum Colors:", leftX, middleY + 27, 100, 22);
			data->hWndColorEntries = CreateEdit(hWnd, L"256", leftX + 110, middleY + 27, 100, 22, TRUE);
			CreateStatic(hWnd, L"Optimization:", leftX, middleY + 27 * 2, 100, 22);
			data->hWndOptimizationSlider = CreateTrackbar(hWnd, leftX + 110, middleY + 27 * 2, 210, 22, 0, 100, 0);
			data->hWndOptimizationLabel = CreateStatic(hWnd, L"0", leftX + 330, middleY + 27 * 2, 50, 22);

			NpCreateBalanceInput(&data->balance, hWnd, leftX - 10, bottomY - 18, rightX + boxWidth - leftX);

			CreateGroupbox(hWnd, L"Texture", leftX - 10, topY - 18, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Palette", rightX - 10, topY - 18, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"4x4 Compression", leftX - 10, middleY - 18, rightX + boxWidth - leftX, boxHeight2);

			data->hWndDoConvertButton = CreateButton(hWnd, L"Convert", width / 2 - 100, height - 32, 200, 22, TRUE);

			//populate the dropdown list
			WCHAR bf[16];
			int len;
			for (int i = 1; i <= CT_DIRECT; i++) {
				const char *str = TxNameFromTexFormat(i);
				len = 0;
				while (*str) {
					bf[len] = *str;
					str++;
					len++;
				}
				bf[len] = L'\0';
				SendMessage(data->hWndFormat, CB_ADDSTRING, len, (LPARAM) bf);
			}

			int format = TexViewerJudgeFormat(data->px, data->width, data->height);
			SendMessage(data->hWndFormat, CB_SETCURSEL, format - 1, 0);

			//based on the texture format and presence of transparent pixels, select default color 0 mode. This option
			//only applies to paletteN texture formats, but we'll decide as though we were using one of those formats,
			//in case the user changes the texture format, the default settings will still be applicable.
			if (TexViewerJudgeColor0Mode(data->px, data->width, data->height)) {
				SendMessage(data->hWndColor0Transparent, BM_SETCHECK, BST_CHECKED, 0);
			}

			//set default alpha key
			{
				data->alphaKey = 0xFF00FF; // default color: magenta

				//we'll scan the image for appearances of common alpha keys.
				int nFF00FF = 0, n00FF00 = 0, nFFFF00 = 0;
				for (int i = 0; i < data->width * data->height; i++) {
					COLOR32 c = data->px[i] & 0xFFFFFF;
					
					switch (c) {
						case 0xFF00FF: // magenta
							nFF00FF++; break;
						case 0x00FF00: // green
							n00FF00++; break;
						case 0xFFFF00: // cyan
							nFFFF00++; break;
					}
				}

				if (nFF00FF) data->alphaKey = 0xFF00FF;
				else if (n00FF00) data->alphaKey = 0x00FF00;
				else if (nFFFF00) data->alphaKey = 0xFFFF00;
				else {
					//select top-left color.
					if (data->width >= 1 && data->height >= 0) {
						data->alphaKey = data->px[0] & 0xFFFFFF;
					} else {
						data->alphaKey = 0xFF00FF;
					}
				}
			}

			//pick default 4x4 color count
			int maxColors = TexViewerJudgeColorCount(data->width, data->height);
			SetEditNumber(data->hWndColorEntries, maxColors);

			//fill palette name
			WCHAR *pname = NULL;
			if (data->isNitro) {
				//fill existing palette name
				pname = TexNarrowResourceNameToWideChar(data->texture.texture.palette.name);
			} else {
				//generate a palette name
				pname = (WCHAR *) calloc(16, sizeof(WCHAR));
				createPaletteName(pname, data->szInitialFile);
			}

			if (pname != NULL) {
				SendMessage(data->hWndPaletteName, WM_SETTEXT, wcslen(pname), (LPARAM) pname);
				free(pname);
			}

			updateConvertDialog(data);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int idc = LOWORD(wParam);
			if (hWndControl || idc) {
				int controlCode = HIWORD(wParam);
				if (hWndControl == data->hWndFormat && controlCode == LBN_SELCHANGE) {
					updateConvertDialog(data);
					
					//color count - update for paletted textures
					int format = SendMessage(hWndControl, CB_GETCURSEL, 0, 0) + 1;
					if (format != CT_DIRECT && format != CT_4x4) {
						int colorCounts[] = { 0, 32, 4, 16, 256, 0, 8, 0 };
						SetEditNumber(data->hWndPaletteSize, colorCounts[format]);
					}
				} else if (hWndControl == data->hWndFixedPalette && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndDither && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndDitherAlpha && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndLimitPalette && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndCheckboxAlphaKey && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndCheckboxAlphaKey && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndSelectAlphaKey && controlCode == BN_CLICKED) {
					//choose a color for the alpha key
					HWND hWndMain = getMainWindow(hWnd);
					CHOOSECOLOR cc = { 0 };
					cc.lStructSize = sizeof(cc);
					cc.hInstance = (HWND) (HINSTANCE) GetWindowLongPtr(hWnd, GWL_HINSTANCE); //weird struct definition
					cc.hwndOwner = hWndMain;
					cc.rgbResult = data->alphaKey;
					cc.lpCustColors = data->tmpCust;
					cc.Flags = 0x103;
					if (ChooseColorW(&cc)) {
						data->alphaKey = cc.rgbResult;
					}
				} else if (hWndControl == data->hWndPaletteBrowse && controlCode == BN_CLICKED) {
					LPWSTR path = openFileDialog(hWnd, L"Select palette", L"Palette Files\0*.nclr;*ncl.bin;*.ntfp\0All Files\0*.*\0\0", L"");
					if (path != NULL) {
						SendMessage(data->hWndPaletteInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
						free(path);
					}
				} else if ((hWndControl == data->hWndDoConvertButton && controlCode == BN_CLICKED) || idc == IDOK) {
					TxConversionParameters params = { 0 };

					int fmt = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0) + 1;

					WCHAR path[MAX_PATH];
					SendMessage(data->hWndPaletteInput, WM_GETTEXT, MAX_PATH, (LPARAM) path);

					NCLR paletteFile = { 0 };
					BOOL fixedPalette = GetCheckboxChecked(data->hWndFixedPalette);
					if (fixedPalette) {
						int status = 1;
						if (path[0]) {
							status = PalReadFile(&paletteFile, path);
						}
						if (status) {
							MessageBox(hWnd, L"Invalid palette file.", L"Invalid file", MB_ICONERROR);
							break;
						}
					}

					WCHAR bf[64];
					int colorEntries = GetEditNumber(data->hWndColorEntries); //for 4x4
					int paletteSize = GetEditNumber(data->hWndPaletteSize); //for non-4x4
					params.diffuseAmount = GetEditNumber(data->hWndDiffuseAmount) / 100.0f;
					params.threshold = GetTrackbarPosition(data->hWndOptimizationSlider);
					SendMessage(data->hWndPaletteName, WM_GETTEXT, 63, (LPARAM) bf);

					RxBalanceSetting balance;
					params.dither = GetCheckboxChecked(data->hWndDither);
					params.ditherAlpha = GetCheckboxChecked(data->hWndDitherAlpha);
					params.c0xp = GetCheckboxChecked(data->hWndColor0Transparent);
					NpGetBalanceSetting(&data->balance, &balance);

					//if we set to not limit palette, set the max size to the max allowed
					BOOL limitPalette = GetCheckboxChecked(data->hWndLimitPalette);
					if (!limitPalette || colorEntries > 32768) {
						colorEntries = 32768;
					}

					//check texture format 
					unsigned int nPx = data->width * data->height;
					if (fmt == CT_4x4 && nPx > (512 * 1024)) {
						//ordinary texture VRAM allocation prohibits this
						int cfm = MessageBox(hWnd, L"Converting tex4x4 texture larger than 1024x512. Proceed?", L"Texture Size Warning", MB_ICONWARNING | MB_YESNO);
						if (cfm == IDNO) break;
					}
					if ((fmt == CT_256COLOR || fmt == CT_A5I3 || fmt == CT_A3I5) && nPx > (512 * 1024)) {
						//texture cannot fit in VRAM
						int cfm = MessageBox(hWnd, L"Texture data size exceeds VRAM capacity. Proceed?", L"Texture Size Warning", MB_ICONWARNING | MB_YESNO);
						if (cfm == IDNO) break;
					}

					//alpha key preprocessing of input image
					BOOL useAlphaKey = GetCheckboxChecked(data->hWndCheckboxAlphaKey);
					if (useAlphaKey) {
						for (int i = 0; i < data->width * data->height; i++) {
							COLOR32 c = data->px[i];
							if ((c & 0x00FFFFFF) == (data->alphaKey & 0x00FFFFFF)) {
								data->px[i] = 0;
							}
						}
					}

					params.px = data->px;
					params.width = data->width;
					params.height = data->height;
					params.fmt = fmt;
					params.useFixedPalette = fixedPalette;
					params.colorEntries = fixedPalette ? paletteFile.nColors : (fmt == CT_4x4 ? colorEntries : paletteSize);
					params.balance = balance.balance;
					params.colorBalance = balance.colorBalance;
					params.enhanceColors = balance.enhanceColors;
					params.dest = &data->texture.texture;
					params.fixedPalette = fixedPalette ? paletteFile.colors : NULL;
					params.pnam = TexNarrowResourceNameFromWideChar(bf);

					HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
					SendMessage(hWnd, WM_CLOSE, 0, 0);

					TxConversionResult result = TexViewerModalConvert(&params, hWndMain);
					free(params.pnam);

					if (result != TEXCONV_SUCCESS) {
						if (result != TEXCONV_ABORT) {
							//explicit error
							MessageBox(hWndMain, L"Texture conversion failed.", L"Error", MB_ICONERROR);
						}
						break;
					}

					//if the format is paletteN, we have not used fixed palette, color 0 was transparent and we used alpha keying, put 
					//the alpha key into color index 0.
					if (fmt >= CT_4COLOR && fmt <= CT_256COLOR && params.c0xp && useAlphaKey && !fixedPalette) {
						if (data->texture.texture.palette.nColors > 0) {
							data->texture.texture.palette.pal[0] = ColorConvertToDS(data->alphaKey);
						}
					}

					InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
					data->isNitro = TRUE;

					TexViewerUpdatePaletteLabel(data->hWnd);
					data->selectedAlpha = (fmt == CT_A3I5) ? 7 : ((fmt == CT_A5I3) ? 31 : 0);
					data->selectedColor = 0;
				} else if (idc == IDCANCEL) {
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}
			}
			break;
		}
		case WM_HSCROLL:
		{
			HWND hWndControl = (HWND) lParam;
			 if (hWndControl == data->hWndOptimizationSlider) {
				 WCHAR bf[8];
				 int len = wsprintfW(bf, L"%d", SendMessage(hWndControl, TBM_GETPOS, 0, 0));
				 SendMessage(data->hWndOptimizationLabel, WM_SETTEXT, len, (LPARAM) bf);
			 }
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK CompressionProgressProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TxConversionParameters *params = (TxConversionParameters *) GetWindowLongPtr(hWnd, 1 * sizeof(LONG_PTR));

	switch (msg) {
		case WM_CREATE:
		{
			CreateStatic(hWnd, L"Progress:", 10, 10, 100, 22);
			HWND hWndProgress = CreateWindow(PROGRESS_CLASSW, L"", WS_VISIBLE | WS_CHILD, 10, 42, 400, 22, hWnd, NULL, NULL, NULL);
			SendMessage(hWndProgress, PBM_DELTAPOS, 1, 0);
			SetWindowLong(hWnd, 0, (LONG) hWndProgress);
			SetWindowSize(hWnd, 420, 74);
			SetGUIFont(hWnd);
			break;
		}
		case NV_INITIALIZE:
		{
			SetWindowLongPtr(hWnd, 1 * sizeof(LONG_PTR), (LONG_PTR) (TxConversionParameters *) lParam);
			SetTimer(hWnd, 1, 16, NULL);
			break;
		}
		case WM_TIMER:
		{
			if (params != NULL && params->progressMax) {
				HWND hWndProgress = (HWND) GetWindowLongPtr(hWnd, 0);
				SendMessage(hWndProgress, PBM_SETRANGE, 0, params->progressMax << 16);
				SendMessage(hWndProgress, PBM_SETPOS, params->progress, 0);
			}
			break;
		}
		case WM_CLOSE:
		{
			if (params != NULL && params->complete) {
				KillTimer(hWnd, 1);
				break;
			} else {
				if (params != NULL) params->terminate = 1; // send terminate request
				return 0;                                  // don't end the modal until the thread naturally exits
			}
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

typedef struct {
	FRAMEDATA frameData;
	TEXTUREEDITORDATA *data;
	int hoverX;
	int hoverY;
	int hoverIndex;
	int contextHoverIndex;
} TEXTUREPALETTEEDITORDATA;

LRESULT CALLBACK TexturePaletteEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREPALETTEEDITORDATA *data = (TEXTUREPALETTEEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (TEXTUREPALETTEEDITORDATA *) calloc(1, sizeof(TEXTUREPALETTEEDITORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case NV_INITIALIZE:
		{
			data->data = (TEXTUREEDITORDATA *) lParam;
			data->frameData.contentWidth = 256;
			data->frameData.contentHeight = ((data->data->texture.texture.palette.nColors + 15) / 16) * 16;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentHeight;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			SetWindowSize(hWnd, 256 + GetSystemMetrics(SM_CXVSCROLL) + 4, 256 + 4);
			break;
		}
		case WM_NCHITTEST:
		{
			LRESULT hit = DefChildProc(hWnd, msg, wParam, lParam);
			if (hit == HTLEFT || hit == HTRIGHT) return HTBORDER;
			if (hit == HTTOPLEFT) return HTTOP;
			if (hit == HTBOTTOMLEFT) return HTBOTTOM;
			if (hit == HTTOPRIGHT) return HTTOP;
			if (hit == HTBOTTOMRIGHT) return HTBOTTOM;
			return hit;
		}
		case WM_PAINT:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO vert;
			vert.cbSize = sizeof(vert);
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &vert);

			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			HDC hOffDC = CreateCompatibleDC(hDC);
			HBITMAP hBitmap = CreateCompatibleBitmap(hDC, rcClient.right, rcClient.bottom);
			SelectObject(hOffDC, hBitmap);
			HBRUSH hBackground = GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1);
			SelectObject(hOffDC, hBackground);
			HPEN hBlackPen = SelectObject(hOffDC, GetStockObject(NULL_PEN));
			Rectangle(hOffDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			SelectObject(hOffDC, hBlackPen);

			HPEN hRowPen = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
			HPEN hWhitePen = GetStockObject(WHITE_PEN);

			COLOR *palette = data->data->texture.texture.palette.pal;
			int nColors = data->data->texture.texture.palette.nColors;
			for (int i = 0; i < nColors; i++) {
				int x = i & 0xF, y = i >> 4;

				if (y * 16 + 16 - vert.nPos >= 0 && y * 16 - vert.nPos < rcClient.bottom) {
					HBRUSH hbr = CreateSolidBrush(ColorConvertFromDS(palette[i]));
					SelectObject(hOffDC, hbr);
					if (x + y * 16 == data->hoverIndex) SelectObject(hOffDC, hWhitePen);
					else if (y == data->hoverY) SelectObject(hOffDC, hRowPen);
					else SelectObject(hOffDC, hBlackPen);
					Rectangle(hOffDC, x * 16, y * 16 - vert.nPos, x * 16 + 16, y * 16 + 16 - vert.nPos);
					DeleteObject(hbr);
				}
			}

			BitBlt(hDC, 0, 0, rcClient.right, rcClient.bottom, hOffDC, 0, 0, SRCCOPY);
			EndPaint(hWnd, &ps);

			DeleteObject(hOffDC);
			DeleteObject(hBitmap);
			DeleteObject(hBackground);
			DeleteObject(hRowPen);
			break;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);
			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;

			TRACKMOUSEEVENT evt;
			evt.cbSize = sizeof(evt);
			evt.hwndTrack = hWnd;
			evt.dwHoverTime = 0;
			evt.dwFlags = TME_LEAVE;
			TrackMouseEvent(&evt);

		}
		case WM_MOUSELEAVE:
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);
			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;

			int nRows = (data->data->texture.texture.palette.nColors + 15) / 16;

			int hoverX = -1, hoverY = -1, hoverIndex = -1;
			if (mousePos.x >= 0 && mousePos.x < 256 && mousePos.y >= 0) {
				hoverX = mousePos.x / 16;
				hoverY = mousePos.y / 16;
				hoverIndex = hoverX + hoverY * 16;
				if (hoverY >= nRows) {
					hoverX = -1, hoverY = -1, hoverIndex = -1;
				}
			}
			if (msg == WM_MOUSELEAVE) {
				hoverX = -1, hoverY = -1, hoverIndex = -1;
			}
			data->hoverX = hoverX;
			data->hoverY = hoverY;
			data->hoverIndex = hoverIndex;

			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		{
			POINT pos;
			GetCursorPos(&pos);
			ScreenToClient(hWnd, &pos);

			SCROLLINFO vert;
			vert.cbSize = sizeof(vert);
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &vert);
			pos.y += vert.nPos;

			int x = pos.x / 16, y = pos.y / 16;
			if (x < 16) {
				int index = y * 16 + x;
				if (index < data->data->texture.texture.palette.nColors) {
					//if left click, open color editor dialogue.
					if (msg == WM_LBUTTONDOWN) {
						COLOR c = data->data->texture.texture.palette.pal[index];
						DWORD ex = ColorConvertFromDS(c);

						HWND hWndMain = getMainWindow(hWnd);
						CHOOSECOLOR cc = { 0 };
						cc.lStructSize = sizeof(cc);
						cc.hInstance = (HWND) (HINSTANCE) GetWindowLongPtr(hWnd, GWL_HINSTANCE); //weird struct definition
						cc.hwndOwner = hWndMain;
						cc.rgbResult = ex;
						cc.lpCustColors = data->data->tmpCust;
						cc.Flags = 0x103;
						BOOL(__stdcall *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
						if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;
						if (ChooseColorFunction(&cc)) {
							DWORD result = cc.rgbResult;
							data->data->texture.texture.palette.pal[index] = ColorConvertToDS(result);
							InvalidateRect(hWnd, NULL, FALSE);

							TxRender(data->data->px, data->data->width, data->data->height, 
								&data->data->texture.texture.texels, &data->data->texture.texture.palette, 0);
							int param = data->data->texture.texture.texels.texImageParam;
							int width = TEXW(param);
							int height = 8 << ((param >> 23) & 7);
							
							InvalidateRect(data->data->hWnd, NULL, FALSE);
						}
					} else if (msg == WM_RBUTTONDOWN) {
						//otherwise open context menu
						data->contextHoverIndex = data->hoverIndex;
						HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 3);
						POINT mouse;
						GetCursorPos(&mouse);
						TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL);
					}
				}
			}
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl == NULL && HIWORD(wParam) == 0) {
				WORD notification = LOWORD(wParam);
				switch (notification) {
					case ID_PALETTEMENU_PASTE:
					{
						int offset = data->contextHoverIndex & (~15);
						int maxOffset = data->data->texture.texture.palette.nColors;

						OpenClipboard(hWnd);
						PastePalette(data->data->texture.texture.palette.pal + offset, maxOffset - offset);
						CloseClipboard();

						TEXTURE *texture = &data->data->texture.texture;
						TxRender(data->data->px, data->data->width, data->data->height, &texture->texels, &texture->palette, 0);
						
						InvalidateRect(hWnd, NULL, FALSE);
						InvalidateRect(data->data->hWnd, NULL, FALSE);
						break;
					}
					case ID_PALETTEMENU_COPY:
					{
						int offset = data->contextHoverIndex & (~15);
						int length = 16;
						int maxOffset = data->data->texture.texture.palette.nColors;
						if (offset + length >= maxOffset) {
							length = maxOffset - offset;
							if (length < 0) length = 0;
						}

						OpenClipboard(hWnd);
						EmptyClipboard();
						CopyPalette(data->data->texture.texture.palette.pal + offset, length);
						CloseClipboard();
						break;
					}
					case ID_FILE_EXPORT:
					{
						//export as NTFP
						COLOR *colors = data->data->texture.texture.palette.pal;
						int nColors = data->data->texture.texture.palette.nColors;

						HWND hWndMain = getMainWindow(data->data->hWnd);
						LPWSTR path = saveFileDialog(hWndMain, L"Save NTFP", L"NTFP files (*.ntfp)\0*.ntfp\0All Files\0*.*\0\0", L"ntfp");
						if (path == NULL) break;

						DWORD dwWritten;
						HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(hFile, colors, nColors * 2, &dwWritten, NULL);
						CloseHandle(hFile);

						free(path);
						break;
					}
					case ID_FILE_SAVE:
					case ID_FILE_SAVEAS:
						SendMessage(data->data->hWnd, msg, notification, 0);
						break;
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			data->data->hWndPaletteEditor = NULL;
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
	}
	if (data->data != NULL) {
		return DefChildProc(hWnd, msg, wParam, lParam);
	} else {
		return DefMDIChildProc(hWnd, msg, wParam, lParam);
	}

}


typedef struct BATCHTEXCONVDATA_ {
	HWND hWndDirectory;
	HWND hWndBrowse;
	HWND hWndConvert;
	HWND hWndClean;
} BATCHTEXCONVDATA;

typedef struct BATCHTEXENTRY_ {
	TxConversionParameters params;
	WCHAR *path;
	WCHAR *outPath;
	HWND hWndProgress;
	HANDLE hThread;
	HWND hWndStatus;
	int started;
	int lastComplete;
} BATCHTEXENTRY;

typedef struct BATCHTEXPROGRESSDATA_ {
	StList texList;          // BATCHTEXENTRY*
	unsigned int nThreads;   // number of conversion threads to spawn
} BATCHTEXPROGRESSDATA;

int EnumAllFiles(LPCWSTR path, BOOL(CALLBACK *fileCallback) (LPCWSTR, void *), BOOL(CALLBACK *dirCallback) (LPCWSTR, void *),
	BOOL(CALLBACK *preprocessDirCallback) (LPCWSTR, void *), void *param) {
	//copy string to add \*
	int pathlen = wcslen(path);
	WCHAR cpy[MAX_PATH + 2] = { 0 };
	memcpy(cpy, path, 2 * (pathlen + 1));

	//add \*
	if (pathlen == 0 || cpy[pathlen - 1] != L'\\') {
		cpy[pathlen++] = L'\\';
	}
	cpy[pathlen++] = L'*';

	WIN32_FIND_DATA ffd;
	HANDLE hFind = FindFirstFile(cpy, &ffd);

	//process all the things (if they exist)
	if (hFind == INVALID_HANDLE_VALUE) return 1;

	int status = 1;
	do {
		//if name is . or .., ignore
		if (_wcsicmp(ffd.cFileName, L".") == 0 || _wcsicmp(ffd.cFileName, L"..") == 0)
			continue;

		//get full name
		WCHAR fullPath[MAX_PATH] = { 0 };
		memcpy(fullPath, cpy, 2 * (pathlen - 1)); //cut off *
		memcpy(fullPath + pathlen - 1, ffd.cFileName, 2 * (wcslen(ffd.cFileName) + 1));

		//if a directory, procses it recursively. If a file, prcoess it normally.
		DWORD attr = ffd.dwFileAttributes;
		if (attr & FILE_ATTRIBUTE_DIRECTORY) {
			if (preprocessDirCallback == NULL || preprocessDirCallback(fullPath, param)) { //nonexistent or returns TRUE, process recurse
				status = EnumAllFiles(fullPath, fileCallback, dirCallback, preprocessDirCallback, param) && status; //prevent short-circuiting
			}
		} else {
			status = fileCallback(fullPath, param) && status;
		}
	} while (FindNextFile(hFind, &ffd));
	FindClose(hFind);

	//if we've succeeded, process the directory.
	if (status) {
		dirCallback(path, param);
	}
	return status;
}

BOOL CALLBACK DeleteFileCallback(LPCWSTR path, void *param) {
	return DeleteFile(path);
}

BOOL CALLBACK RemoveDirectoryCallback(LPCWSTR path, void *param) {
	return RemoveDirectory(path);
}

int BatchTexDelete(LPCWSTR path) {
	return EnumAllFiles(path, DeleteFileCallback, RemoveDirectoryCallback, NULL, NULL);
}

//some global state for the current batch operation
int g_batchTexConvertedTex = 0; //number of textures converted
LPCWSTR g_batchTexOut = NULL;
HWND g_hWndBatchTexWindow;

BOOL BatchTexReadOptions(LPCWSTR path, int *fmt, int *dither, int *ditherAlpha, float *diffuse, int *paletteSize, int *c0xp, char *pnam,
	int *balance, int *colorBalance, int *enhanceColors) {

	char narrow[MAX_PATH] = { 0 };
	WCHAR buffer[MAX_PATH] = { 0 };
	BOOL hasMissing = FALSE; //any missing entries?

	//format
	GetPrivateProfileString(L"Texture", L"Format", L"", buffer, MAX_PATH, path);
	for (unsigned int i = 0; i <= wcslen(buffer); i++) {
		narrow[i] = (char) buffer[i];
	}
	int foundFormat = 0;
	for (int i = CT_A3I5; i <= CT_DIRECT; i++) {
		const char *fname = TxNameFromTexFormat(i);
		if (strcmp(fname, narrow) == 0) {
			*fmt = i;
			foundFormat = 1;
			break;
		}
	}
	if (!foundFormat) hasMissing = TRUE;

	//dithering
	int rawInt = GetPrivateProfileInt(L"Texture", L"Dither", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *dither = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"DitherAlpha", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *ditherAlpha = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"Diffuse", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *diffuse = ((float) rawInt) / 100.0f;

	//palette
	rawInt = GetPrivateProfileInt(L"Texture", L"PaletteSize", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *paletteSize = rawInt;
	GetPrivateProfileString(L"Texture", L"PaletteName", L"", buffer, MAX_PATH, path);
	if (*buffer == L'\0') hasMissing = TRUE;
	else {
		memset(pnam, 0, 17);
		for (unsigned int i = 0; i < 16; i++) {
			pnam[i] = (char) buffer[i];
			if (buffer[i] == L'\0') break;
		}
	}
	rawInt = GetPrivateProfileInt(L"Texture", L"C0xp", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *c0xp = rawInt;

	//balance
	rawInt = GetPrivateProfileInt(L"Texture", L"Balance", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *balance = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"ColorBalance", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *colorBalance = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"EnhanceColors", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *enhanceColors = rawInt;

	return hasMissing;
}

void BatchTexWriteOptions(LPCWSTR path, int fmt, int dither, int ditherAlpha, float diffuse, int paletteSize, int c0xp, char *pnam, 
	int balance, int colorBalance, int enhanceColors) {

	//format
	WCHAR buffer[MAX_PATH] = { 0 };
	wsprintfW(buffer, L"%S", TxNameFromTexFormat(fmt));
	WritePrivateProfileString(L"Texture", L"Format", buffer, path);

	//dithering
	wsprintfW(buffer, L"%d", dither);
	WritePrivateProfileString(L"Texture", L"Dither", buffer, path);
	wsprintfW(buffer, L"%d", ditherAlpha);
	WritePrivateProfileString(L"Texture", L"DitherAlpha", buffer, path);
	wsprintfW(buffer, L"%d", (int) (diffuse * 100.0f + 0.5f));
	WritePrivateProfileString(L"Texture", L"Diffuse", buffer, path);

	//palette
	wsprintfW(buffer, L"%d", paletteSize);
	WritePrivateProfileString(L"Texture", L"PaletteSize", buffer, path);
	wsprintfW(buffer, L"%S", pnam);
	WritePrivateProfileString(L"Texture", L"PaletteName", buffer, path);
	wsprintfW(buffer, L"%d", c0xp);
	WritePrivateProfileString(L"Texture", L"C0xp", buffer, path);

	//balance
	wsprintfW(buffer, L"%d", balance);
	WritePrivateProfileString(L"Texture", L"Balance", buffer, path);
	wsprintfW(buffer, L"%d", colorBalance);
	WritePrivateProfileString(L"Texture", L"ColorBalance", buffer, path);
	wsprintfW(buffer, L"%d", enhanceColors);
	WritePrivateProfileString(L"Texture", L"EnhanceColors", buffer, path);
}

BOOL BatchTexShouldConvert(LPCWSTR path, LPCWSTR configPath, LPCWSTR outPath) {
	HANDLE hTextureFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE hConfigFile = CreateFile(configPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE hOutFile = CreateFile(outPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
	//if input doesn't exist, we can't possibly 
	BOOL shouldWrite = FALSE;
	FILETIME srcTime = { 0 }, configTime = { 0 }, destTime = { 0 };
	if (hTextureFile == INVALID_HANDLE_VALUE) {
		shouldWrite = FALSE;
		goto cleanup;
	}

	//if output doesn't exist or config doesn't exist, do output
	if (hOutFile == INVALID_HANDLE_VALUE || hConfigFile == INVALID_HANDLE_VALUE) {
		shouldWrite = TRUE;
		goto cleanup;
	}

	//if either of srcTime or configTime are greater than or equal to destTime, do write
	LARGE_INTEGER srcInt, configInt, destInt;
	GetFileTime(hTextureFile, NULL, NULL, &srcTime);
	GetFileTime(hConfigFile, NULL, NULL, &configTime);
	GetFileTime(hOutFile, NULL, NULL, &destTime);
	srcInt.LowPart = srcTime.dwLowDateTime;
	srcInt.HighPart = srcTime.dwHighDateTime;
	configInt.LowPart = configTime.dwLowDateTime;
	configInt.HighPart = configTime.dwHighDateTime;
	destInt.LowPart = destTime.dwLowDateTime;
	destInt.HighPart = destTime.dwHighDateTime;
	if (destInt.QuadPart <= srcInt.QuadPart || destInt.QuadPart <= configInt.QuadPart) {
		shouldWrite = TRUE;
		goto cleanup;
	}
	

cleanup:
	if (hTextureFile != INVALID_HANDLE_VALUE) CloseHandle(hTextureFile);
	if (hConfigFile != INVALID_HANDLE_VALUE) CloseHandle(hConfigFile);
	if (hOutFile != INVALID_HANDLE_VALUE) CloseHandle(hOutFile);
	return shouldWrite;
}

void BatchTexCheckFormatDir(LPCWSTR path, int *fmt) {
	LPCWSTR end = path + wcslen(path);
	int nSlashCounted = 0;
	while (end > path) {
		end--;
		if (*end == L'\\') {
			nSlashCounted++;
			if (nSlashCounted == 2) break; //second slash from end
		}
	}
	if (*end != L'\0') end++; //skip separator

	WCHAR dirNameBuf[MAX_PATH] = { 0 };
	for (unsigned int i = 0; i < wcslen(end); i++) {
		if (end[i] == L'\\') break;
		dirNameBuf[i] = end[i];
	}

	for (int i = CT_A3I5; i <= CT_DIRECT; i++) {
		WCHAR wideFmt[16] = { 0 };
		const char *name = TxNameFromTexFormat(i);
		for (unsigned int j = 0; j < strlen(name); j++) wideFmt[j] = name[j];
		
		//case insensitive check
		if (_wcsicmp(dirNameBuf, wideFmt) == 0) {
			*fmt = i;
			break;
		}
	}
}

BOOL CALLBACK BatchTexConvertFileCallback(LPCWSTR path, void *param) {
	//read image
	int width, height;
	COLOR32 *px = ImgRead(path, &width, &height);

	//invalid image?
	if (px == NULL) {
		return TRUE; //just skip the file by reporting a success
	}

	//invalid texture size?
	if (!TxDimensionIsValid(width) || !TxDimensionIsValid(height)) {
		if (px) free(px);
		return FALSE; //report actual error
	}

	//construct output path (ensure .TGA extension)
	WCHAR outPath[MAX_PATH] = { 0 };
	LPCWSTR filename = GetFileName(path);
	int outPathLen = wcslen(g_batchTexOut);
	memcpy(outPath, g_batchTexOut, 2 * (outPathLen + 1));
	outPath[outPathLen++] = L'\\';
	memcpy(outPath + outPathLen, filename, 2 * wcslen(filename) + 2);

	//ensure extension
	int extensionIndex = 0;
	for (unsigned int i = 0; i < wcslen(outPath); i++) {
		if (outPath[i] == L'.') extensionIndex = i;
	}
	memcpy(outPath + extensionIndex, L".TGA", 5 * sizeof(WCHAR));

	//construct congfiguration path; used to read/write for this texture
	WCHAR configPath[MAX_PATH] = { 0 };
	memcpy(configPath, path, 2 * (wcslen(path) + 1));
	extensionIndex = 0;
	for (unsigned int i = 0; i < wcslen(configPath); i++) {
		if (configPath[i] == L'.') extensionIndex = i;
	}
	memcpy(configPath + extensionIndex, L".INI", 5 * sizeof(WCHAR));

	//check: should we re-convert?
	BOOL doConvert = BatchTexShouldConvert(path, configPath, outPath);
	if (!doConvert) return TRUE; //skip

	int i;
	char pnam[17] = { 0 };
	for (i = 0; i < 12; i++) { //add _pl, max 15 chars
		if (filename[i] == L'\0') break;
		if (filename[i] == L'.') break;
		pnam[i] = (char) filename[i];
	}
	memcpy(pnam + i, "_pl", 4);

	//setup texture params
	int dither = 0, ditherAlpha = 0;
	float diffuse = 0.0f;

	//palette settings
	int c0xp = TexViewerJudgeColor0Mode(px, width, height);
	int useFixedPalette = 0;
	COLOR *fixedPalette = NULL;

	//4x4 settings
	int fmt = TexViewerJudgeFormat(px, width, height);
	int colorEntries = TexViewerJudgeColorCount(width, height);
	int threshold4x4 = 0;

	//check the directory. Last directory name after base should be the name
	//of a format.
	BatchTexCheckFormatDir(path, &fmt);

	//max color entries for the selected format
	switch (fmt) {
		case CT_4COLOR:
			colorEntries = 4;
			break;
		case CT_16COLOR:
			colorEntries = 16;
			break;
		case CT_256COLOR:
			colorEntries = 256;
			break;
		case CT_A3I5:
			colorEntries = 32;
			break;
		case CT_A5I3:
			colorEntries = 8;
			break;
	}

	//balance settings
	int balance = BALANCE_DEFAULT, colorBalance = BALANCE_DEFAULT;
	int enhanceColors = 0;

	//read overrides from file.
	BOOL hasMissing = BatchTexReadOptions(configPath, &fmt, &dither, &ditherAlpha, &diffuse, &colorEntries, &c0xp, pnam,
		&balance, &colorBalance, &enhanceColors);

	//write back options to file (if there were any missing entries)
	if (hasMissing) {
		BatchTexWriteOptions(configPath, fmt, dither, ditherAlpha, diffuse, colorEntries, c0xp, pnam, balance, colorBalance, enhanceColors);
	}

	TEXTURE *texture = (TEXTURE *) calloc(1, sizeof(TEXTURE));
	BATCHTEXENTRY texEntry = { 0 };
	texEntry.params.px = px;
	texEntry.params.width = width;
	texEntry.params.height = height;
	texEntry.params.fmt = fmt;
	texEntry.params.dither = dither;
	texEntry.params.diffuseAmount = diffuse;
	texEntry.params.c0xp = c0xp;
	texEntry.params.ditherAlpha = ditherAlpha;
	texEntry.params.colorEntries = colorEntries;
	texEntry.params.threshold = threshold4x4;
	texEntry.params.balance = balance;
	texEntry.params.colorBalance = colorBalance;
	texEntry.params.enhanceColors = enhanceColors;
	texEntry.params.dest = texture;
	texEntry.params.useFixedPalette = useFixedPalette;
	texEntry.params.fixedPalette = useFixedPalette ? fixedPalette : NULL;
	texEntry.params.pnam = _strdup(pnam);
	texEntry.path = _wcsdup(path);
	texEntry.outPath = _wcsdup(outPath);
	StListAdd((StList *) param, &texEntry);

	return TRUE;
}

BOOL CALLBACK BatchTexConvertDirectoryCallback(LPCWSTR path, void *param) {
	return TRUE;
}

BOOL CALLBACK BatchTexConvertDirectoryExclusion(LPCWSTR path, void *param) {
	//if name ends in converted or converted\, reject (return FALSE)
	int len = wcslen(path);

	//check both endings
	LPCWSTR end = path + len;
	if (len >= 11 && _wcsicmp(end - 11, L"\\converted\\") == 0) return FALSE;
	if (len >= 10 && _wcsicmp(end - 10, L"\\converted") == 0) return FALSE;
	return TRUE;
}

int BatchTexConvert(LPCWSTR path, LPCWSTR convertedDir) {
	//ensure output directory exists
	BOOL b = CreateDirectory(convertedDir, NULL);
	if (!b && GetLastError() != ERROR_ALREADY_EXISTS) return 0; //failure

	BATCHTEXPROGRESSDATA batchData = { 0 };
	StListCreateInline(&batchData.texList, BATCHTEXENTRY, NULL);

	SYSTEM_INFO info;
	GetSystemInfo(&info);
	batchData.nThreads = info.dwNumberOfProcessors;
	if (batchData.nThreads > 8) batchData.nThreads = 8; // use at most 8 threads

	//recursively process all the textures in this directory. Ugh ummm
	g_batchTexOut = convertedDir;
	int status = EnumAllFiles(path, BatchTexConvertFileCallback, BatchTexConvertDirectoryCallback, BatchTexConvertDirectoryExclusion, &batchData.texList);
	g_batchTexOut = NULL;

	WCHAR buf[48];
	wsprintfW(buf, L"%d texture%s outstanding. OK?", batchData.texList.length, batchData.texList.length == 1 ? L"" : L"s");

	int proceed = 1;
	if (batchData.texList.length == 0) {
		MessageBox(g_hWndBatchTexWindow, L"No textures outstanding.", L"No Textures", MB_ICONINFORMATION);
		proceed = 0;
	} else {
		int id = MessageBox(g_hWndBatchTexWindow, buf, L"Proceed?", MB_ICONQUESTION | MB_OKCANCEL);
		if (id != IDOK) proceed = 0;
	}

	//if the user selects to proceed, show the modal conversion window and create threads.
	if (proceed) {
		HWND hWndProgress = CreateWindow(L"BatchProgressClass", L"Batch Operation",
			WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_hWndBatchTexWindow, NULL, NULL, NULL);
		SendMessage(hWndProgress, NV_INITIALIZE, 0, (LPARAM) &batchData);
		DoModalEx(hWndProgress, FALSE);
	}

	//free texture resources
	for (unsigned int i = 0; i < batchData.texList.length; i++) {
		BATCHTEXENTRY *ent = StListGetPtr(&batchData.texList, i);

		//free texture allocation
		free(ent->params.dest->palette.pal);
		free(ent->params.dest->palette.name);
		free(ent->params.dest->texels.texel);
		free(ent->params.dest->texels.cmp);
		free(ent->params.dest->texels.name);
		free(ent->params.dest);

		free(ent->params.pnam);
		free(ent->path);
		free(ent->outPath);
	}
	
	StListFree(&batchData.texList);

	return status;
}

BOOL CALLBACK BatchTexAddTexture(LPCWSTR path, void *param) {
	TexArc *nsbtx = (TexArc *) param;

	//read file and determine if valid
	int size;
	void *pf = ObjReadWholeFile(path, &size);
	int valid = TxIsValidNnsTga(pf, size);
	free(pf);
	if (!valid) return TRUE;

	//read texture
	TextureObject textureObj = { 0 };
	TxReadFile(&textureObj, path);

	//split from object
	TEXTURE *texture = TxUncontain(&textureObj);

	//add to TexArc
	int fmt = FORMAT(texture->texels.texImageParam);
	TexarcAddTexture(nsbtx, &texture->texels);
	if (fmt != CT_DIRECT) {
		TexarcAddPalette(nsbtx, &texture->palette);
	}
	return TRUE;
}

BOOL CALLBACK BatchTexAddDir(LPCWSTR path, void *param) {
	//do nothing
	return TRUE;
}

void BatchTexShowVramStatistics(HWND hWnd, LPCWSTR convertedDir) {
	//enumerate files in this folder and construct a temporary texture archive of them
	TexArc nsbtx;
	TexarcInit(&nsbtx, NSBTX_TYPE_NNS);
	EnumAllFiles(convertedDir, BatchTexAddTexture, BatchTexAddDir, NULL, (void *) &nsbtx);

	//create dialog
	CreateVramUseWindow(hWnd, &nsbtx);

	//free
	ObjFree(&nsbtx.header);
}

LRESULT CALLBACK BatchTextureWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	BATCHTEXCONVDATA *data = (BATCHTEXCONVDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (BATCHTEXCONVDATA *) calloc(1, sizeof(BATCHTEXCONVDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}

	switch (msg) {
		case WM_CREATE:
		{
			g_hWndBatchTexWindow = hWnd;
			CreateGroupbox(hWnd, L"Batch Conversion", 10, 10, 350, 78);
			CreateStatic(hWnd, L"Texture Directory:", 20, 28, 100, 22);
			data->hWndDirectory = CreateEdit(hWnd, L"", 125, 28, 200, 22, FALSE);
			data->hWndBrowse = CreateButton(hWnd, L"...", 325, 28, 25, 22, FALSE);
			data->hWndConvert = CreateButton(hWnd, L"Convert", 20, 55, 100, 22, TRUE);
			data->hWndClean = CreateButton(hWnd, L"Clean", 125, 55, 100, 22, FALSE);

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 370, 97);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
			if (notif == BN_CLICKED && hWndControl == data->hWndBrowse) { //browse button
				//we will overwrite this with the *real* path
				WCHAR *path = UiDlgBrowseForFolder(getMainWindow(hWnd), L"Select output folder...");
				if (path == NULL) break;

				SendMessage(data->hWndDirectory, WM_SETTEXT, wcslen(path), (LPARAM) path);
				free(path);
			} else if (notif == BN_CLICKED && (hWndControl == data->hWndClean || hWndControl == data->hWndConvert)) { //clean and convert buttons
				//delete directory\converted, if it exists
				WCHAR path[MAX_PATH] = { 0 };
				WCHAR convertedDir[MAX_PATH] = { 0 };
				int len = SendMessage(data->hWndDirectory, WM_GETTEXT, MAX_PATH, (LPARAM) path);
				if (len == 0) {
					MessageBox(hWnd, L"Enter a path.", L"No path", MB_ICONERROR);
					break;
				}

				//append \converted
				memcpy(convertedDir, path, 2 * (len + 1));
				if (convertedDir[len - 1] != L'\\') {
					convertedDir[len++] = L'\\';
				}
				memcpy(convertedDir + len, L"converted", 22);
				len += 10;

				if (hWndControl == data->hWndClean) {
					int status = BatchTexDelete(convertedDir);
					if (status) {
						MessageBox(hWnd, L"The operation completed successfully.", L"Result", MB_ICONINFORMATION);
					} else {
						MessageBox(hWnd, L"An error occurred.", L"Error", MB_ICONERROR);
					}
				} else {
					int status = BatchTexConvert(path, convertedDir);
					if (status) {
						BatchTexShowVramStatistics(hWnd, convertedDir);
					} else {
						MessageBox(hWnd, L"An error occurred.", L"Error", MB_ICONERROR);
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			free(data);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK BatchTexProgressProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	BATCHTEXPROGRESSDATA *data = (BATCHTEXPROGRESSDATA *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case NV_INITIALIZE:
		{
			data = (BATCHTEXPROGRESSDATA *) lParam;
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			int labelWidth = 75;
			int progressWidth = 400;
			int statusWidth = 50;

			//labels and progressbars
			for (unsigned int i = 0; i < data->texList.length; i++) {
				BATCHTEXENTRY *ent = StListGetPtr(&data->texList, i);
				
				CreateStatic(hWnd, GetFileName(ent->path), 10, 10 + i * 27, labelWidth, 22);
				ent->hWndProgress = CreateWindow(PROGRESS_CLASSW, L"", WS_VISIBLE | WS_CHILD,
					10 + labelWidth + 5, 10 + i * 27, progressWidth, 22, hWnd, NULL, NULL, NULL);
				ent->hWndStatus = CreateStatic(hWnd, L"Working...", 10 + labelWidth + 5 + progressWidth + 5, 10 + i * 27, statusWidth, 22);

				//start conversion
				//ent->hThread = textureConvertThreaded(&ent->params);
				ent->started = 0;
				ent->hThread = NULL;
			}

			SetWindowSize(hWnd, 10 + labelWidth + 5 + progressWidth + 5 + statusWidth + 10, data->texList.length * 27 - 5 + 20);
			SetGUIFont(hWnd);
			SetTimer(hWnd, 1, 16, NULL);
			break;
		}
		case WM_CLOSE:
		{
			//send terminate request to all conversions
			if (data != NULL) {
				//if all batch operations are complete, we may exit.
				int allComplete = 1;
				for (unsigned int i = 0; i < data->texList.length; i++) {
					BATCHTEXENTRY *ent = StListGetPtr(&data->texList, i);
					if (!ent->params.complete) allComplete = 0;
				}

				if (allComplete) {
					//all complete: write texture to file

					for (unsigned int i = 0; i < data->texList.length; i++) {
						BATCHTEXENTRY *ent = StListGetPtr(&data->texList, i);

						//free thread
						CloseHandle(ent->hThread);

						if (ent->params.result != TEXCONV_SUCCESS) continue; // skip an incomplete texture

						//contain texture
						TextureObject textureObj;
						TxContain(&textureObj, TEXTURE_TYPE_NNSTGA, ent->params.dest);

						//write file out
						TxWriteFile(&textureObj, ent->outPath);

						//free texture memory
						ObjFree(&textureObj.header);
						memset(ent->params.dest, 0, sizeof(TEXTURE));
					}

					break;
				}

				for (unsigned int i = 0; i < data->texList.length; i++) {
					BATCHTEXENTRY *ent = StListGetPtr(&data->texList, i);
					ent->params.terminate = 1;
				}
			}
			return 0;
		}
		case WM_TIMER:
		{
			if (data != NULL) {
				//check if any conversions should be started
				unsigned int nInProcess = 0, nStarted = 0;
				for (unsigned int i = 0; i < data->texList.length; i++) {
					BATCHTEXENTRY *ent = StListGetPtr(&data->texList, i);
					if (ent->started) {
						//the start flag is updated by this thread, so there is no concurrency concern there.
						//the completed flag is updated on the conversion thread, however. In the worst case,
						//this means that we may overestimate the number of in-process threads if a conversion
						//completes after we check its completion status. In this case, we will observe the
						//completed status on the next timer tick, so this isn't really a problem.
						nStarted++;
						if (!ent->params.complete) nInProcess++;
					}
				}

				//if the number of in-process conversions is less than the number of allowed threads, spawn another.
				if (nInProcess < data->nThreads && nStarted < data->texList.length) {
					//for every thread not utilized, we spawn a conversion thread, up to the number of textures total.
					unsigned int nSpawn = data->nThreads - nInProcess, nSpawned = 0;

					for (unsigned int i = 0; i < data->texList.length && nSpawned < nSpawn; i++) {
						BATCHTEXENTRY *ent = StListGetPtr(&data->texList, i);
						if (!ent->started) {
							ent->started = 1;
							ent->hThread = textureConvertThreaded(&ent->params);
							nSpawned++;
						}
					}
				}

				int allComplete = 1;
				for (unsigned int i = 0; i < data->texList.length; i++) {
					BATCHTEXENTRY *ent = StListGetPtr(&data->texList, i);
					SendMessage(ent->hWndProgress, PBM_SETRANGE, 0, ent->params.progressMax << 16);
					SendMessage(ent->hWndProgress, PBM_SETPOS, ent->params.progress, 0);

					if (!ent->params.complete) allComplete = 0;

					//update status label
					if (!ent->lastComplete && ent->params.complete) {
						ent->lastComplete = 1;

						LPCWSTR status = L"Complete";
						switch (ent->params.result) {
							case TEXCONV_SUCCESS : status = L"Complete"; break;
							case TEXCONV_INVALID : status = L"Invalid";  break;
							case TEXCONV_NOMEM   : status = L"Error";    break;
							case TEXCONV_ABORT   : status = L"Aborted";  break;
						}
						SendMessage(ent->hWndStatus, WM_SETTEXT, wcslen(status), (LPARAM) status);
					}
				}

				//if all complete, we may end the dialog.
				if (allComplete) SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

int BatchTextureDialog(HWND hWndParent) {
	HWND hWnd = CreateWindow(L"BatchTextureClass", L"Batch Texture Conversion", WS_CAPTION | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, hWndParent, NULL, NULL, NULL);
	ShowWindow(hWnd, SW_SHOW);
	DoModal(hWnd);
	return 0;
}

static void RegisterBatchTextureDialogClass(void) {
	RegisterGenericClass(L"BatchTextureClass", BatchTextureWndProc, sizeof(LPVOID));
}

static void RegisterTexturePreviewClass(void) {
	RegisterGenericClass(L"TexturePreviewClass", TexturePreviewWndProc, sizeof(LPVOID));
}

static void RegisterConvertDialogClass(void) {
	RegisterGenericClass(L"ConvertDialogClass", ConvertDialogWndProc, sizeof(LPVOID));
}

static void RegisterCompressionProgressClass(void) {
	RegisterGenericClass(L"CompressionProgress", CompressionProgressProc, 2 * sizeof(LPVOID));
}

static void RegisterTexturePaletteEditorClass(void) {
	RegisterGenericClass(L"TexturePaletteEditorClass", TexturePaletteEditorWndProc, sizeof(LPVOID));
}

static void RegisterTextureTileEditorClass(void) {
	RegisterGenericClass(L"TextureTileEditorClass", TextureTileEditorWndProc, 3 * sizeof(LONG_PTR));
}

void RegisterTextureEditorClass(void) {
	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EDITOR_CLASS *cls = EditorRegister(L"TextureEditorClass", TextureEditorWndProc, L"Texture Editor", sizeof(TEXTUREEDITORDATA), features);
	EditorAddFilter(cls, TEXTURE_TYPE_NNSTGA, L"tga", L"NNS TGA Files (*.tga)\0*.tga\0");
	EditorAddFilter(cls, TEXTURE_TYPE_ISTUDIO, L"5tx", L"iMageStudio Textures (*.5tx)\0*.5tx\0");
	EditorAddFilter(cls, TEXTURE_TYPE_TDS, L"tds", L"Ghost Trick Textures (*.tds)\0*.tds\0");
	EditorAddFilter(cls, TEXTURE_TYPE_NTGA, L"nnstga", L"NTGA Files (*.nnstga)\0*.nnstga\0");

	RegisterTexturePreviewClass();
	RegisterConvertDialogClass();
	RegisterCompressionProgressClass();
	RegisterTexturePaletteEditorClass();
	RegisterTextureTileEditorClass();
	RegisterBatchTextureDialogClass();
	RegisterGenericClass(L"BatchProgressClass", BatchTexProgressProc, sizeof(void *));
}

HWND CreateTexturePaletteEditor(int x, int y, int width, int height, HWND hWndParent, TEXTUREEDITORDATA *data) {
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TexturePaletteEditorClass", L"Palette Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN | WS_VSCROLL, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
	return h;
}

HWND CreateTextureEditor(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	unsigned int fileSize;
	unsigned char *bytes = ObjReadWholeFile(path, &fileSize);
	int compression = CxGetCompressionType(bytes, fileSize);
	if (compression != COMPRESSION_NONE) {
		unsigned char *dec = CxDecompress(bytes, fileSize, &fileSize);
		free(bytes);
		bytes = dec;
	}
	int textureType = TxIdentify(bytes, fileSize);
	free(bytes);

	int bWidth, bHeight;
	COLOR32 *bits = ImgRead(path, &bWidth, &bHeight);
	if (bits == NULL && textureType == TEXTURE_TYPE_INVALID) {
		MessageBox(hWndParent, L"An invalid image file was specified.", L"Invalid Image", MB_ICONERROR);
		return NULL;
	}

	//if not already a valid texture file, validate dimensions
	if (textureType == TEXTURE_TYPE_INVALID && (!TxDimensionIsValid(bWidth) || (bHeight > 1024))) {
		free(bits);
		MessageBox(hWndParent, L"Textures must have dimensions as powers of two greater than or equal to 8, and not exceeding 1024.", L"Invalid dimensions", MB_ICONERROR);
		return NULL;
	}

	HWND h = EditorCreate(L"TextureEditorClass", x, y, width, height, hWndParent);

	//for unconverted images, open path and bitmap
	if (textureType == TEXTURE_TYPE_INVALID) {
		SendMessage(h, NV_SETPATH, wcslen(path), (LPARAM) path);
		SendMessage(h, NV_INITIALIZE, bWidth | (bHeight << 16), (LPARAM) bits);
	} else {
		TextureObject texture = { 0 };
		TxReadFile(&texture, path);
		SendMessage(h, NV_SETPATH, wcslen(path), (LPARAM) path);
		SendMessage(h, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) &texture);
		EditorSetFile(h, path);
	}
	return h;
}

HWND CreateTextureEditorImmediate(int x, int y, int width, int height, HWND hWndParent, TEXTURE *texture) {
	TextureObject texObj;
	TxInit(&texObj, TEXTURE_TYPE_NNSTGA);
	memcpy(&texObj.texture, texture, sizeof(TEXTURE));

	HWND h = EditorCreate(L"TextureEditorClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) &texObj);
	return h;
}
