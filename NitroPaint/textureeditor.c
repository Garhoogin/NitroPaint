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


#define TEXVIEWER_SB_FORMAT    0
#define TEXVIEWER_SB_COLORS    1
#define TEXVIEWER_SB_PLTCOLS   2
#define TEXVIEWER_SB_TEX_VRAM  3
#define TEXVIEWER_SB_PLT_VRAM  4
#define TEXVIEWER_SB_CURPOS    5
#define TEXVIEWER_SB_COLOR     6


extern HICON g_appIcon;

HWND CreateTexturePaletteEditor(TEXTUREEDITORDATA *data);
static void TexViewerEnsurePaletteEditor(TEXTUREEDITORDATA *data);

static int TexViewerGetFormatForPreset(void) {
	switch (g_configuration.preset) {
		default:
		case NP_PRESET_NITROSYSTEM: return TEXTURE_TYPE_NNSTGA;
		case NP_PRESET_IMAGESTUDIO: return TEXTURE_TYPE_ISTUDIO;
		case NP_PRESET_GRIT: return TEXTURE_TYPE_GRF;
	}
}

// convert a narrow resource name to a wide character string
wchar_t *TexNarrowResourceNameToWideChar(const char *name) {
	//NULL resource name
	if (name == NULL) return NULL;

	int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
	wchar_t *buf = (wchar_t *) calloc(len + 1, sizeof(wchar_t));
	if (buf == NULL) return NULL;

	MultiByteToWideChar(CP_UTF8, 0, name, -1, buf, len + 1);
	return buf;
}

// convert a wide character string to a narrow resource string
char *TexNarrowResourceNameFromWideChar(const wchar_t *name) {
	if (name == NULL) return NULL;
	
	int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, NULL, 0, NULL, NULL);
	char *buf = calloc(len + 1, 1);
	if (buf == NULL) return NULL;

	WideCharToMultiByte(CP_UTF8, 0, name, -1, buf, len + 1, NULL, NULL);
	return buf;
}

static int TexViewerGetContentWidth(TEXTUREEDITORDATA *data) {
	return data->width * data->scale;
}

static int TexViewerGetContentHeight(TEXTUREEDITORDATA *data) {
	return data->height * data->scale;
}

int TexViewerIsConverted(TEXTUREEDITORDATA *data) {
	if (data->texture == NULL) return 0;

	int fmt = FORMAT(data->texture->texture.texels.texImageParam);
	return fmt != 0;
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
		TxRender(px, &texture->texels, &texture->palette);
		
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

static void TexViewerUpdateImageColorCountLabel(TEXTUREEDITORDATA *data) {
	wchar_t bf[32];

	//count colors
	int nColors = ImgCountColors(data->px, data->width * data->height);
	wsprintfW(bf, L"%d colors", nColors);
	UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_COLORS, bf);
}

static void TexViewerUpdatePaletteLabel(TEXTUREEDITORDATA *data) {
	wchar_t bf[32];
	if (TexViewerIsConverted(data) && data->texture->texture.palette.nColors) {
		wsprintfW(bf, L"Palette: %d colors", data->texture->texture.palette.nColors);
	} else {
		wsprintfW(bf, L"No palette");
	}
	UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_PLTCOLS, bf);
}

static void TexViewerFormatLabelKB(TEXTUREEDITORDATA *data, int iLabel, const wchar_t *label, unsigned int nByte) {
	unsigned int kb00 = (nByte * 100 + 512) / 1024;

	wchar_t bf[32];
	wsprintfW(bf, L"%s: %d.%d%dKB", label,
		(kb00 / 100),
		(kb00 /  10) % 10,
		(kb00 /   1) % 10
	);

	UiStatusbarSetText(data->hWndStatus, iLabel, bf);
}

static void TexViewerUpdateVramLabel(TEXTUREEDITORDATA *data) {
	//this code is ugly due to being unable to just use %.2f
	TexViewerFormatLabelKB(data, TEXVIEWER_SB_TEX_VRAM, L"Texel", TxGetTextureVramSize(&data->texture->texture.texels));

	TexViewerFormatLabelKB(data, TEXVIEWER_SB_PLT_VRAM, L"Palette", TxGetTexPlttVramSize(&data->texture->texture.palette));
}

static void TexViewerUpdateStatusBar(HWND hWnd) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) EditorGetData(hWnd);
	if (data->texture == NULL) return;

	TexViewerUpdatePaletteLabel(data);

	WCHAR bf[32];
	wsprintfW(bf, L"Format: %S", TxNameFromTexFormat(FORMAT(data->texture->texture.texels.texImageParam)));
	UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_FORMAT, bf);

	TexViewerUpdateImageColorCountLabel(data);
	TexViewerUpdateVramLabel(data);
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

static void TexViewerOnMouseMove(HWND hWnd, int pxX, int pxY) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) EditorGetData(hWnd);

	int scrollX, scrollY;
	TedGetScroll(&data->ted, &scrollX, &scrollY);

	//palette highlight parameters
	int newPlttHighlightStart = 0, newPlttHighlightLength = 0;

	if (data->ted.mouseX >= 0 && data->ted.mouseY >= 0 && pxX < data->width && pxY < data->height) {
		//position
		wchar_t buf[32];
		wsprintfW(buf, L"(%d, %d)", pxX, pxY);
		UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_CURPOS, buf);

		uint32_t texImageParam = data->texture->texture.texels.texImageParam;
		int fmt = FORMAT(texImageParam);

		//color
		if (fmt == 0) {
			UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_COLOR, L"");
		} else {
			void *texel = data->texture->texture.texels.texel;
			uint16_t *pidx = data->texture->texture.texels.cmp;
			unsigned int texW = TEXW(texImageParam);

			static const unsigned char bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
			unsigned int depth = bits[fmt];
			unsigned int iPx = pxX + pxY * texW;

			switch (fmt) {
				case CT_DIRECT:
				{
					COLOR c = ((COLOR *) texel)[iPx];
					wsprintfW(buf, L"Color: %04X", c);
					UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_COLOR, buf);
					break;
				}
				case CT_4COLOR:
				case CT_16COLOR:
				case CT_256COLOR:
				{
					unsigned int pxPerByte = 8 / depth;
					unsigned char pval = (((unsigned char *) texel)[iPx / pxPerByte] >> ((iPx % pxPerByte) * depth)) & ((1 << depth) - 1);

					wsprintfW(buf, L"Color: %02X", pval);
					UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_COLOR, buf);

					newPlttHighlightLength = 1;
					newPlttHighlightStart = pval;
					break;
				}
				case CT_A3I5:
				case CT_A5I3:
				{
					unsigned int iBits = (fmt == CT_A3I5) ? 5 : 3;
					unsigned int iMask = (1 << iBits) - 1;
					unsigned char pval = ((unsigned char *) texel)[iPx];

					wsprintfW(buf, L"Color: %02X A=%d", pval & iMask, pval >> iBits);
					UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_COLOR, buf);

					newPlttHighlightLength = 1;
					newPlttHighlightStart = pval;
					break;
				}
				case CT_4x4:
				{
					unsigned int iBlock = (pxX / 4) + (pxY / 4) * (texW / 4);
					uint32_t block = ((uint32_t *) texel)[iBlock];
					uint16_t idx = pidx[iBlock];

					unsigned int pval = (block >> ((pxX & 3) + 4 * (pxY * 3))) & 0x3;

					wsprintfW(buf, L"Color: %d (%04X)", pval, idx);
					UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_COLOR, buf);

					newPlttHighlightLength = (idx & COMP_INTERPOLATE) ? 2 : 4;
					newPlttHighlightStart = COMP_INDEX(idx);
					break;
				}
			}
		}
	} else {
		UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_CURPOS, L"");
		UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_COLOR, L"");
	}

	if (newPlttHighlightStart != data->highlightStart || newPlttHighlightLength != data->highlightLength) {
		//update palette view
		data->highlightStart = newPlttHighlightStart;
		data->highlightLength = newPlttHighlightLength;
		if (data->hWndPaletteEditor != NULL) {
			RedrawWindow(data->hWndPaletteEditor, NULL, NULL, RDW_INVALIDATE | RDW_INTERNALPAINT);
		}
	}
}

static void TexViewerRender(HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight) {
	//texture image rendered 
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) EditorGetData(hWnd);
	int width = data->width;

	//render texture to framebuffer
	for (int y = 0; y < renderHeight; y++) {
		int srcY = (y + scrollY) / data->scale;

		for (int x = 0; x < renderWidth; x++) {
			int srcX = (x + scrollX) / data->scale;

			COLOR32 c = data->px[srcX + srcY * width];

			//alpha blend
			c = TedAlphaBlendColor(c, x, y);
			fb->px[x + y * fb->width] = REVERSE(c);
		}
	}

}

static void TexViewerGraphicsUpdated(TEXTUREEDITORDATA *data) {
	TxRender(data->px, &data->texture->texture.texels, &data->texture->texture.palette);
	InvalidateRect(data->hWnd, NULL, FALSE);
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

	data->highlightStart = 0;
	data->highlightLength = 0;
	
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
	data->ted.mouseMoveCallback = TexViewerOnMouseMove;

	static const int parts[] = { 100, 100, 120, 100, 100, 100, -1 };
	data->hWndStatus = UiStatusbarCreate(hWnd, 7, parts);
	UiStatusbarSetText(data->hWndStatus, TEXVIEWER_SB_FORMAT, L"Format: none");
	TexViewerFormatLabelKB(data, TEXVIEWER_SB_TEX_VRAM, L"Texel", 0);
	TexViewerFormatLabelKB(data, TEXVIEWER_SB_PLT_VRAM, L"Palette", 0);

	TexViewerUpdatePaletteLabel(data);

	data->hWndConvert = CreateButton(hWnd, L"Convert To...", 0, 0, 0, 0, FALSE);
	data->hWndExportNTF = CreateButton(hWnd, L"Export NTF", 0, 0, 0, 0, FALSE);
}

static void TexViewerOnPaint(TEXTUREEDITORDATA *data) {
	InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
	TedMarginPaint(data->hWnd, (EDITOR_DATA *) data, &data->ted);
}

static void TexViewerUpdateLayout(TEXTUREEDITORDATA *data) {
	RECT rcClient, rcStatus;
	GetClientRect(data->hWnd, &rcClient);

	SendMessage(data->hWndStatus, WM_SIZE, 0, 0);
	GetClientRect(data->hWndStatus, &rcStatus);
	rcClient.bottom -= rcStatus.bottom;

	float dpiScale = GetDpiScale();

	int panelWidth = UI_SCALE_COORD(120, dpiScale);
	int ctlWidth = UI_SCALE_COORD(100, dpiScale);
	int ctlHeight = UI_SCALE_COORD(22, dpiScale);
	int panelPadding = UI_SCALE_COORD(10, dpiScale);

	int viewerWidth = rcClient.right - MARGIN_TOTAL_SIZE;
	int paletteWidth = 0;
	if (TexViewerIsConverted(data) && FORMAT(data->texture->texture.texels.texImageParam) != CT_DIRECT) {
		//subtract dimension for palette
		paletteWidth = 16 * 16 + GetSystemMetrics(SM_CXVSCROLL);
		viewerWidth -= paletteWidth;
	}

	MoveWindow(data->ted.hWndViewer, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE + ctlHeight,
		viewerWidth, rcClient.bottom - MARGIN_TOTAL_SIZE - ctlHeight, FALSE);
	MoveWindow(data->hWndConvert, ctlWidth * 0, 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndExportNTF, ctlWidth * 1, 0, ctlWidth, ctlHeight, TRUE);

	if (data->hWndPaletteEditor != NULL) {
		MoveWindow(data->hWndPaletteEditor, rcClient.right - paletteWidth, ctlHeight, paletteWidth, rcClient.bottom - ctlHeight, TRUE);
	}
}

static LRESULT TexViewerOnSize(TEXTUREEDITORDATA *data, WPARAM wParam, LPARAM lParam) {
	TexViewerUpdateLayout(data);

	if (wParam == SIZE_RESTORED) InvalidateRect(data->hWnd, NULL, TRUE); //full update
	return DefMDIChildProc(data->hWnd, WM_SIZE, wParam, lParam);
}

static int TexViewerCheckConverted(TEXTUREEDITORDATA *data) {
	if (TexViewerIsConverted(data)) return 1;
	
	//show user error
	MessageBox(data->hWnd, L"Texture must be converted.", L"Texture Editor", MB_ICONERROR);
	return 0;
}

static void TexViewerOnCtlCommand(TEXTUREEDITORDATA *data, HWND hWndControl, int notification) {
	HWND hWnd = data->hWnd;
	if (hWndControl == data->hWndConvert) {
		data->hWndConvertDialog = CreateWindowEx(WS_EX_CONTEXTHELP, L"ConvertDialogClass", L"Convert Texture",
			WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_THICKFRAME,
			CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->editorMgr->hWnd, NULL, NULL, NULL);
		SetWindowLongPtr(data->hWndConvertDialog, 0, (LONG_PTR) data);
		SendMessage(data->hWndConvertDialog, NV_INITIALIZE, 0, 0);
		DoModal(data->hWndConvertDialog);
	} else if (hWndControl == data->hWndExportNTF) {
		//if not in any format, it cannot be exported.
		if (!TexViewerCheckConverted(data)) return;

		HWND hWndMain = data->editorMgr->hWnd;
		LPWSTR ntftPath = saveFileDialog(hWndMain, L"Save NTFT", L"NTFT Files (*.ntft)\0*.ntft\0All Files\0*.*\0\0", L"ntft");
		if (ntftPath == NULL) return;


		int fmt = FORMAT(data->texture->texture.texels.texImageParam);

		LPWSTR ntfiPath = NULL;
		if (fmt == CT_4x4) {
			ntfiPath = saveFileDialog(hWndMain, L"Save NTFI", L"NTFI Files (*.ntfi)\0*.ntfi\0All Files\0*.*\0\0", L"ntfi");
			if (ntfiPath == NULL) {
				free(ntftPath);
				return;
			}
		}

		DWORD dwWritten;
		int texImageParam = data->texture->texture.texels.texImageParam;
		int texelSize = TxGetTexelSize(TEXW(texImageParam), data->texture->texture.texels.height, texImageParam);
		HANDLE hFile = CreateFile(ntftPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		WriteFile(hFile, data->texture->texture.texels.texel, texelSize, &dwWritten, NULL);
		CloseHandle(hFile);
		free(ntftPath);

		if (ntfiPath != NULL) {
			hFile = CreateFile(ntfiPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			WriteFile(hFile, data->texture->texture.texels.cmp, texelSize / 2, &dwWritten, NULL);
			CloseHandle(hFile);
			free(ntfiPath);
		}

		//palette export
		if (data->texture->texture.palette.pal != NULL) {
			COLOR *colors = data->texture->texture.palette.pal;
			int nColors = data->texture->texture.palette.nColors;

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
	int fmt = FORMAT(data->texture->texture.texels.texImageParam);

	//color-0 transparency
	int c0xp = 0;
	if (fmt >= CT_4COLOR && fmt <= CT_256COLOR && COL0TRANS(data->texture->texture.texels.texImageParam)) c0xp = 1;

	//if the texture is not converted or is direct/4x4, we will copy a direct color bitmap.
	if (fmt == 0 || fmt == CT_DIRECT || fmt == CT_4x4) {
		copyBitmap(data->px, data->width, data->height);
		return;
	}

	//we will write an indexed color bitmap. 
	COLOR32 pltbuf[256] = { 0 };
	for (int i = 0; i < data->texture->texture.palette.nColors && i < 0x100; i++) {
		COLOR32 c = ColorConvertFromDS(data->texture->texture.palette.pal[i]);

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

	unsigned int width = TEXW(data->texture->texture.texels.texImageParam);
	unsigned int height = TEXH(data->texture->texture.texels.texImageParam);
	unsigned char *bmp = (unsigned char *) calloc(width * height, sizeof(char));

	unsigned int nPltt = 256;
	if (nBpp == 8) {
		//copy direct
		memcpy(bmp, data->texture->texture.texels.texel, width * height);
	} else {
		//copy with bit conversion
		unsigned int pxPerByte = 8 / nBpp;
		unsigned int pxMask = (1 << nBpp) - 1;
		for (unsigned int i = 0; i < width * height; i++) {
			bmp[i] = (data->texture->texture.texels.texel[i / pxPerByte] >> ((i % pxPerByte) * nBpp)) & pxMask;
		}

		if (nPltt >= (1u << nBpp)) nPltt = 1u << nBpp;
	}

	ClipCopyBitmapEx(bmp, width, height, 1, pltbuf, nPltt);

	free(bmp);
}

static void TexViewerOnMenuCommand(TEXTUREEDITORDATA *data, int idMenu) {
	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
			InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
			break;
		case ID_FILE_SAVE:
			if (!TexViewerCheckConverted(data)) break;

			EditorSave(data->hWnd);
			break;
		case ID_FILE_SAVEAS:
			if (!TexViewerCheckConverted(data)) break;

			EditorSaveAs(data->hWnd);
			break;
		case ID_FILE_EXPORT:
		{
			//PNG export
			LPWSTR path = saveFileDialog(data->editorMgr->hWnd, L"Export Texture", L"PNG files (*.png)\0*.png\0All Files\0*.*\0", L".png");
			if (path == NULL) break;

			//if texture is in DS format, export from texture data
			if (TexViewerIsConverted(data)) {
				TexViewerExportTextureImage(path, &data->texture->texture);
			} else {
				ImgWrite(data->px, data->width, data->height, path);
			}
			free(path);
			break;
		}
		case ID_TEXTUREMENU_COPY:
		{
			OpenClipboard(data->hWnd);
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

static void TexViewerSetImage(TEXTUREEDITORDATA *data, COLOR32 *px, unsigned int width, unsigned int height) {
	//set image pixels and size (and update the scrolling and tiled viewer)
	data->px = px;
	data->width = width;
	data->height = height;
	data->frameData.contentWidth = width;
	data->frameData.contentHeight = height;
	data->ted.tilesX = width / 4;
	data->ted.tilesY = height / 4;

	//update the color count label
	TexViewerUpdateImageColorCountLabel(data);
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
		case NV_ZOOMUPDATED:
			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			TedUpdateMargins(&data->ted);
			break;
		case NV_INITIALIZE:
		{
			COLOR32 *px = (COLOR32 *) lParam;
			data->texture = (TextureObject *) calloc(1, sizeof(TextureObject));
			TxInit(data->texture, TexViewerGetFormatForPreset());

			//check: is it a Nitro TGA?
			if (!TxReadFile(data->texture, data->szInitialFile)) {
				int format = FORMAT(data->texture->texture.texels.texImageParam);
				EditorSetFile(hWnd, data->szInitialFile);
				TxRender(px, &data->texture->texture.texels, &data->texture->texture.palette);
				TexViewerUpdateStatusBar(hWnd);
			}
			TexViewerEnsurePaletteEditor(data);

			TexViewerSetImage(data, px, LOWORD(wParam), HIWORD(wParam));

			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		}
		case NV_INITIALIZE_IMMEDIATE:
		{
			//set texture data directly
			TextureObject *texture = (TextureObject *) lParam;
			data->texture = texture;

			//decode texture data for preview
			unsigned int width = TEXW(texture->texture.texels.texImageParam);
			unsigned int height = texture->texture.texels.height;
			COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
			TxRender(px, &texture->texture.texels, &texture->texture.palette);
			TexViewerSetImage(data, px, width, height);

			//update UI
			TexViewerEnsurePaletteEditor(data);

			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			TexViewerUpdateStatusBar(hWnd);
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

int ilog2(int x);

static void DrawColorEntryAlpha(HDC hDC, HPEN hOutline, COLOR color, int alpha, int x, int y) {
	HPEN hOldPen = (HPEN) SelectObject(hDC, hOutline);
	HPEN hNullPen = GetStockObject(NULL_PEN);
	HBRUSH hNullBrush = GetStockObject(NULL_BRUSH);
	HBRUSH hOldBrush = (HBRUSH) SelectObject(hDC, hNullBrush);

	if (alpha == 255) {
		HBRUSH hBg = CreateSolidBrush((COLORREF) ColorConvertFromDS(color));
		SelectObject(hDC, hBg);
		Rectangle(hDC, x, y, x + 16, y + 16);
		DeleteObject(hBg);
	} else {
		COLOR32 c = ColorConvertFromDS(color) | (alpha << 24);
		COLOR32 w = TedAlphaBlendColor(c, 0, 0);
		COLOR32 g = TedAlphaBlendColor(c, 0, 4);

		HBRUSH hbrWhite = CreateSolidBrush((COLORREF) w);
		HBRUSH hbrGray = CreateSolidBrush((COLORREF) g);

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

static unsigned int getTextureOffsetByTileCoordinates(TEXELS *texel, unsigned int x, unsigned int y) {
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

static void PaintTextureTileEditor(HDC hDC, TEXTURE *texture, unsigned int tileX, unsigned int tileY, int colorIndex, int alphaIndex) {
	uint32_t param = texture->texels.texImageParam;
	int format = FORMAT(param);
	unsigned int width = TEXW(param);

	uint16_t indexBuffer = 0;
	if (format == CT_4x4) indexBuffer = texture->texels.cmp[tileX + (width / 4) * tileY];

	//render section 4x4
	COLOR32 rendered[4 * 4];
	TxRenderRect(rendered, tileX * 4, tileY * 4, 4, 4, &texture->texels, &texture->palette);
	ImgSwapRedBlue(rendered, 4, 4);

	const unsigned int scale = 32;
	COLOR32 *preview = (COLOR32 *) calloc(4 * 4 * scale * scale, sizeof(COLOR32));
	for (unsigned int y = 0; y < 4 * scale; y++) {
		for (unsigned int x = 0; x < 4 * scale; x++) {
			unsigned int sampleX = x / scale;
			unsigned int sampleY = y / scale;
			COLOR32 c = rendered[sampleX + sampleY * 4];

			preview[x + y * 4 * scale] = TedAlphaBlendColor(c, x, y);
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
		pal = stackPaletteBuffer;
		nColors = 4;
		transparentIndex = (indexBuffer & COMP_OPAQUE) ? -1 : 3;
		unsigned int paletteIndex = COMP_INDEX(indexBuffer);
		COLOR *palSrc = texture->palette.pal + paletteIndex;

		pal[0] = palSrc[0];
		pal[1] = palSrc[1];
		switch (indexBuffer & COMP_MODE_MASK) {
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
			if (i != selectedColor) SelectObject(hDC, hBlack);
			else SelectObject(hDC, hWhite);

			int x = 4 * scale + 10 + (i % 16) * 16;
			int y = (i / 16) * 16;
			DrawColorEntryAlpha(hDC, (i != selectedColor) ? hBlack : hWhite, pal[i], i == transparentIndex ? 0 : 255, x, y);
		}
	}

	//draw alpha levels if a3i5 or a5i3
	int selectedAlpha = alphaIndex;
	if (format == CT_A3I5 || format == CT_A5I3) {
		COLOR selected = pal[selectedColor];
		unsigned int aMax = (format == CT_A3I5) ? 7 : 31;
		for (unsigned int i = 0; i <= aMax; i++) {
			int x = 4 * scale + 10 + (i % 16) * 16;
			int y = 42 + (i / 16) * 16;
			unsigned int alpha = (i * 510 + aMax) / (2 * aMax);
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
				PaintTextureTileEditor(hDC, &data->texture->texture, tileX, tileY, data->selectedColor, data->selectedAlpha);

			EndPaint(hWnd, &ps);
			break;
		}
		case NV_INITIALIZE:
		{
			SetWindowSize(hWnd, 398, 260);

			TEXELS *texels = &data->texture->texture.texels;
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
				TEXELS *texels = &data->texture->texture.texels;
				int width = TEXW(texels->texImageParam);
				int tilesX = width / 4;
				uint16_t *pIdx = &texels->cmp[tileX + tileY * tilesX];

				int notification = HIWORD(wParam);
				if (notification == BN_CLICKED && hWndControl == data->hWndTransparent) {
					int state = GetCheckboxChecked(hWndControl);
					*pIdx = ((*pIdx) & 0x7FFF) | ((!state) << 15);
					TexViewerGraphicsUpdated(data);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndInterpolate) {
					int state = GetCheckboxChecked(hWndControl);
					*pIdx = ((*pIdx) & 0xBFFF) | (state << 14);
					TexViewerGraphicsUpdated(data);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == EN_CHANGE && hWndControl == data->hWndPaletteBase) {
					*pIdx = ((*pIdx) & 0xC000) | (GetEditNumber(hWndControl) & 0x3FFF);
					TexViewerGraphicsUpdated(data);
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

			TEXELS *texels = &data->texture->texture.texels;
			PALETTE *palette = &data->texture->texture.palette;
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
							HWND hWndMain = data->editorMgr->hWnd;
							COLOR *color = (COLOR *) (pTile + y * width * 2 + x * 2);
							
							if (NpChooseColor15(hWndMain, hWndMain, color)) {
								*color |= 0x8000;
							}
						}
					}
				}
				TexViewerGraphicsUpdated(data);
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
			if (!TexViewerIsConverted(data)) break;

			int x = data->ted.hoverX;
			int y = data->ted.hoverY;
			int texImageParam = data->texture->texture.texels.texImageParam;
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

static void createPaletteName(WCHAR *buffer, const WCHAR *file) {
	//find the last \ or /
	file = GetFileName(file);

	//copy up to 12 characters of the file name
	int i;
	WCHAR *lastDot = wcsrchr(file, L'.');
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
	int nColors;

	if (area <= 128 * 128) {
		//for textures smaller than 256x256, use 8*sqrt(area)
		nColors = (int) (8 * sqrt((float) area));
	} else {
		//larger sizes, increase by 256 every width/height increment
		nColors = (int) (256 * (log2((float) area) - 10));
	}
	nColors = (nColors + 15) & ~15;
	return nColors;
}

static void updateConvertDialog(TEXTUREEDITORDATA *data) {
	int fmt = UiCbGetCurSel(data->hWndFormat) + 1;
	BOOL isPlttN = fmt == CT_4COLOR || fmt == CT_16COLOR || fmt == CT_256COLOR;
	BOOL isPltt = fmt != CT_DIRECT;
	BOOL is4x4 = fmt == CT_4x4;

	BOOL fixedPalette = GetCheckboxChecked(data->hWndFixedPalette) && isPltt;
	BOOL dither = GetCheckboxChecked(data->hWndDither);
	BOOL limitPalette = GetCheckboxChecked(data->hWndLimitPalette) && is4x4 && !fixedPalette;

	EnableWindow(data->hWndDitherAlpha, dither);
	EnableWindow(data->hWndDiffuseAmount, dither);
	EnableWindow(data->hWndColorEntries, limitPalette);
	EnableWindow(data->hWndOptimizationSlider, is4x4 && !fixedPalette);
	EnableWindow(data->hWndPaletteName, isPltt);
	EnableWindow(data->hWndFixedPalette, isPltt);
	EnableWindow(data->hWndPaletteInput, fixedPalette);
	EnableWindow(data->hWndPaletteBrowse, fixedPalette);
	EnableWindow(data->hWndPaletteSize, isPltt && !is4x4 && !fixedPalette);
	EnableWindow(data->hWndLimitPalette, is4x4 && !fixedPalette);
	EnableWindow(data->balance.hWndEnhanceColors, isPltt);

	//paletteN formats: enable color 0 mode
	EnableWindow(data->hWndColor0Transparent, isPlttN);

	//when alpha key is enabled, enable the select button
	EnableWindow(data->hWndSelectAlphaKey, GetCheckboxChecked(data->hWndCheckboxAlphaKey));

	SetFocus(data->hWndConvertDialog);
}

static void TexViewerShowTooltip(HWND hWndParent, HWND hWndCtl, const wchar_t *pstr) {
	HWND hTool = CreateWindow(TOOLTIPS_CLASS, NULL, WS_VISIBLE | WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);

	TOOLINFO toolInfo = { 0 };
	toolInfo.cbSize = sizeof(toolInfo);
	toolInfo.hwnd = hWndParent;
	toolInfo.lpszText = pstr;
	toolInfo.uId = (UINT_PTR) hWndCtl;
	toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	SendMessage(hTool, TTM_ADDTOOL, 0, (LPARAM) &toolInfo);
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
			for (int i = 1; i <= CT_DIRECT; i++) {
				WCHAR bf[16];
				mbstowcs(bf, TxNameFromTexFormat(i), sizeof(bf) / sizeof(bf[0]));
				UiCbAddString(data->hWndFormat, bf);
			}

			int format = TexViewerJudgeFormat(data->px, data->width, data->height);
			UiCbSetCurSel(data->hWndFormat, format - 1);

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
			if (TexViewerIsConverted(data)) {
				//fill existing palette name
				pname = TexNarrowResourceNameToWideChar(data->texture->texture.palette.name);
			} else {
				//generate a palette name
				pname = (WCHAR *) calloc(16, sizeof(WCHAR));
				createPaletteName(pname, data->szInitialFile);
			}

			if (pname != NULL) {
				UiEditSetText(data->hWndPaletteName, pname);
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
					int format = UiCbGetCurSel(hWndControl) + 1;
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
					HWND hWndMain = data->editorMgr->hWnd;
					CHOOSECOLOR cc = { 0 };
					cc.lStructSize = sizeof(cc);
					cc.hInstance = (HWND) (HINSTANCE) GetWindowLongPtr(hWnd, GWL_HINSTANCE); //weird struct definition
					cc.hwndOwner = hWnd;
					cc.rgbResult = data->alphaKey;
					cc.lpCustColors = data->tmpCust;
					cc.Flags = 0x103;
					if (ChooseColorW(&cc)) {
						data->alphaKey = cc.rgbResult;
					}
				} else if (hWndControl == data->hWndPaletteBrowse && controlCode == BN_CLICKED) {
					LPWSTR path = openFileDialog(hWnd, L"Select palette", L"Palette Files\0*.nclr;*ncl.bin;*.ntfp\0All Files\0*.*\0\0", L"");
					if (path != NULL) {
						UiEditSetText(data->hWndPaletteInput, path);
						free(path);
					}
				} else if ((hWndControl == data->hWndDoConvertButton && controlCode == BN_CLICKED) || idc == IDOK) {
					TxConversionParameters params = { 0 };

					int fmt = UiCbGetCurSel(data->hWndFormat) + 1;

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
					unsigned int texelSize = TxGetTexelSize(data->width, data->height, fmt << 20);

					if (fmt == CT_4x4 && nPx > (512 * 1024)) {
						//ordinary texture VRAM allocation prohibits this
						int cfm = MessageBox(hWnd, L"Converting tex4x4 texture larger than 1024x512. Proceed?", L"Texture Size Warning", MB_ICONWARNING | MB_YESNO);
						if (cfm == IDNO) break;
					}
					if (texelSize > (512 * 1024) || (fmt == CT_4x4 && texelSize > (256 * 1024))) {
						//texture cannot fit in VRAM (512KB for normal texture, 256KB for 4x4)
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
					params.dest = &data->texture->texture;
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
						if (data->texture->texture.palette.nColors > 0) {
							data->texture->texture.palette.pal[0] = ColorConvertToDS(data->alphaKey);
						}
					}

					//if the format has a palette, show the palette viewer.
					TexViewerEnsurePaletteEditor(data);

					InvalidateRect(data->ted.hWndViewer, NULL, FALSE);

					TexViewerUpdateStatusBar(data->hWnd);
					data->selectedAlpha = (fmt == CT_A3I5) ? 7 : ((fmt == CT_A5I3) ? 31 : 0);
					data->selectedColor = 0;
				} else if (idc == IDCANCEL) {
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}
			}
			break;
		}
		case WM_HELP:
		{
			HELPINFO *hi = (HELPINFO *) lParam;
			if (hi->cbSize < sizeof(HELPINFO) || hi->iContextType != HELPINFO_WINDOW) break;

			//to be implemented
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
				if (params != NULL) {
					int ctl = MessageBox(hWnd, L"Abort texture conversion?", L"Texture Conversion", MB_ICONQUESTION | MB_YESNO);
					if (ctl == IDYES) {
						//send terminate request
						params->terminate = 1;
					}
				}
				//don't end the modal until the thread naturally exits
				return 0;
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
			data->frameData.contentHeight = ((data->data->texture->texture.palette.nColors + 15) / 16) * 16;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentHeight;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			break;
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
			HPEN hGreenPen = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));

			int hlStart = data->data->highlightStart;
			int hlEnd = hlStart + data->data->highlightLength;

			COLOR *palette = data->data->texture->texture.palette.pal;
			int nColors = data->data->texture->texture.palette.nColors;
			for (int i = 0; i < nColors; i++) {
				int x = i & 0xF, y = i >> 4;

				if (y * 16 + 16 - vert.nPos >= 0 && y * 16 - vert.nPos < rcClient.bottom) {
					HBRUSH hbr = CreateSolidBrush(ColorConvertFromDS(palette[i]));
					SelectObject(hOffDC, hbr);

					int index = x + y * 16;

					if (index == data->hoverIndex) SelectObject(hOffDC, hWhitePen);
					else if (index >= hlStart && index < hlEnd) SelectObject(hOffDC, hGreenPen);
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
			DeleteObject(hGreenPen);
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

			int nRows = (data->data->texture->texture.palette.nColors + 15) / 16;

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
				if (index < data->data->texture->texture.palette.nColors) {
					//if left click, open color editor dialogue.
					if (msg == WM_LBUTTONDOWN) {
						HWND hWndMain = data->data->editorMgr->hWnd;
						
						if (NpChooseColor15(hWndMain, hWndMain, &data->data->texture->texture.palette.pal[index])) {
							InvalidateRect(hWnd, NULL, FALSE);

							TexViewerGraphicsUpdated(data->data);
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
						int maxOffset = data->data->texture->texture.palette.nColors;

						OpenClipboard(hWnd);
						PastePalette(data->data->texture->texture.palette.pal + offset, maxOffset - offset);
						CloseClipboard();

						TexViewerGraphicsUpdated(data->data);
						
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_PALETTEMENU_COPY:
					{
						int offset = data->contextHoverIndex & (~15);
						int length = 16;
						int maxOffset = data->data->texture->texture.palette.nColors;
						if (offset + length >= maxOffset) {
							length = maxOffset - offset;
							if (length < 0) length = 0;
						}

						OpenClipboard(hWnd);
						EmptyClipboard();
						CopyPalette(data->data->texture->texture.palette.pal + offset, length);
						CloseClipboard();
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
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			return DefWindowProc(hWnd, msg, wParam, lParam);
		}
	}
	if (data->data != NULL) {
		return DefChildProc(hWnd, msg, wParam, lParam);
	} else {
		return DefWindowProc(hWnd, msg, wParam, lParam);
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

static wchar_t *BatchTexPathCat(const wchar_t *str1, const wchar_t *str2) {
	unsigned int len1 = wcslen(str1), len2 = wcslen(str2);
	
	//add slash?
	unsigned int addSlash = 1;
	if (len1 > 0 && (str1[len1 - 1] == L'/' || str1[len1 - 1] == L'\\')) addSlash = 0;

	unsigned int len3 = len1 + addSlash + len2 + 1;
	wchar_t *str3 = (wchar_t *) calloc(len3, sizeof(wchar_t));
	memcpy(str3, str1, len1 * sizeof(wchar_t));
	if (addSlash) str3[len1] = L'\\';
	memcpy(str3 + len1 + addSlash, str2, (len2 + 1) * sizeof(wchar_t));

	return str3;
}

static wchar_t *BatchTexPathChangeExtension(const wchar_t *str, const wchar_t *ext) {
	unsigned int len1 = wcslen(str);
	const wchar_t *dotpos = wcsrchr(str, L'.');
	if (dotpos == NULL) dotpos = str + len1;

	unsigned int lenNoExt = dotpos - str;
	unsigned int lenExt = 1 + wcslen(ext);

	wchar_t *str2 = (wchar_t *) calloc(lenNoExt + lenExt + 1, sizeof(wchar_t));
	memcpy(str2, str, lenNoExt * sizeof(wchar_t));
	str2[lenNoExt] = L'.';
	memcpy(str2 + lenNoExt + 1, ext, lenExt * sizeof(wchar_t));
	return str2;
}

static int BatchTexPathEndsWith(const wchar_t *str, const wchar_t *substr) {
	unsigned int len1 = wcslen(str), len2 = wcslen(substr);
	if (len2 > len1) return 0;

	return _wcsicmp(str + len1 - len2, substr) == 0;
}

static void BatchTexPutPropInt(LPCWSTR path, LPCWSTR prop, int n) {
	WCHAR buf[32];
	wsprintfW(buf, L"%d", n);
	WritePrivateProfileString(L"Texture", prop, buf, path);
}

static void BatchTexPutPropStrA(LPCWSTR path, LPCWSTR prop, const char *s) {
	WCHAR buf[MAX_PATH + 1];
	wsprintfW(buf, L"%S", s);
	WritePrivateProfileString(L"Texture", prop, buf, path);
}

static int BatchTexGetPropInt(LPCWSTR path, LPCWSTR prop, int defval) {
	int missing = 0;
	int n1 = GetPrivateProfileInt(L"Texture", prop, -1, path);
	if (n1 == -1) {
		int n2 = GetPrivateProfileInt(L"Texture", prop, 0, path);
		if (n2 == 0) missing = 1;
	}

	if (missing) {
		n1 = defval;
		BatchTexPutPropInt(path, prop, n1); // put default value
	}
	return n1;
}

static void BatchTexGetPropStrA(LPCWSTR path, LPCWSTR prop, char *s, unsigned int nMax) {
	WCHAR buf[MAX_PATH + 1];
	GetPrivateProfileString(L"Texture", prop, L"", buf, sizeof(buf) / sizeof(buf[0]), path);
	wcstombs(s, buf, nMax);
}

void BatchTexReadOptions(LPCWSTR path, TxConversionParameters *params, char *pnam) {
	char narrow[MAX_PATH] = { 0 };

	//format
	BatchTexGetPropStrA(path, L"Format", narrow, sizeof(narrow));
	
	int foundFormat = 0;
	for (int i = CT_A3I5; i <= CT_DIRECT; i++) {
		const char *fname = TxNameFromTexFormat(i);
		if (strcmp(fname, narrow) == 0) {
			params->fmt = i;
			foundFormat = 1;
			break;
		}
	}
	if (!foundFormat) {
		BatchTexPutPropStrA(path, L"Format", TxNameFromTexFormat(params->fmt));
	}

	//dithering
	int diffuseI = (int) (params->diffuseAmount * 100.0f + 0.5f);
	params->dither = BatchTexGetPropInt(path, L"Dither", params->dither);
	params->ditherAlpha = BatchTexGetPropInt(path, L"DitherAlpha", params->ditherAlpha);
	diffuseI = BatchTexGetPropInt(path, L"Diffuse", diffuseI);
	params->diffuseAmount = ((float) diffuseI) / 100.0f;
	
	//palette
	params->colorEntries = BatchTexGetPropInt(path, L"PaletteSize", params->colorEntries);
	BatchTexGetPropStrA(path, L"PaletteName", narrow, sizeof(narrow));
	if (narrow[0] == '\0') {
		//missing palette name
		BatchTexPutPropStrA(path, L"PaletteName", pnam);
	} else {
		strcpy(pnam, narrow);
	}
	params->c0xp = BatchTexGetPropInt(path, L"C0xp", params->c0xp);

	//balance
	params->balance = BatchTexGetPropInt(path, L"Balance", params->balance);
	params->colorBalance = BatchTexGetPropInt(path, L"ColorBalance", params->colorBalance);
	params->enhanceColors = BatchTexGetPropInt(path, L"EnhanceColors", params->enhanceColors);
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
	if (px == NULL) return TRUE; // just skip the file by reporting a success

	//invalid texture size?
	if (!TxDimensionIsValid(width) || !TxDimensionIsValid(height)) {
		free(px);
		return FALSE; // report actual error
	}

	BATCHTEXENTRY texEntry = { 0 };
	texEntry.params.px = px;
	texEntry.params.width = width;
	texEntry.params.height = height;

	//construct output path: change path for output texture and config file extensions
	const wchar_t *filename = GetFileName(path);
	wchar_t *outPath1 = BatchTexPathCat(g_batchTexOut, filename);
	wchar_t *outPath = BatchTexPathChangeExtension(outPath1, L"TGA");
	wchar_t *configPath = BatchTexPathChangeExtension(filename, L"INI");
	free(outPath1);

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

	//dither params
	texEntry.params.dither = 0;
	texEntry.params.ditherAlpha = 0;
	texEntry.params.diffuseAmount = 0.0f;

	//palette settings
	texEntry.params.c0xp = TexViewerJudgeColor0Mode(px, width, height);
	int useFixedPalette = 0;
	COLOR *fixedPalette = NULL;

	//4x4 settings
	texEntry.params.fmt = TexViewerJudgeFormat(px, width, height);
	texEntry.params.colorEntries = TexViewerJudgeColorCount(width, height);
	int threshold4x4 = 0;

	//check the directory. Last directory name after base should be the name
	//of a format.
	BatchTexCheckFormatDir(path, &texEntry.params.fmt);

	//max color entries for the selected format
	switch (texEntry.params.fmt) {
		case CT_4COLOR  : texEntry.params.colorEntries =   4; break;
		case CT_16COLOR : texEntry.params.colorEntries =  16; break;
		case CT_256COLOR: texEntry.params.colorEntries = 256; break;
		case CT_A3I5    : texEntry.params.colorEntries =  32; break;
		case CT_A5I3    : texEntry.params.colorEntries =   8; break;
	}

	//balance settings
	texEntry.params.balance = BALANCE_DEFAULT;
	texEntry.params.colorBalance = BALANCE_DEFAULT;
	texEntry.params.enhanceColors = 1;

	//read overrides from file. Missing fields have default values written back.
	BatchTexReadOptions(configPath, &texEntry.params, pnam);
	free(configPath);

	texEntry.params.dest = (TEXTURE *) calloc(1, sizeof(TEXTURE));
	texEntry.params.threshold = threshold4x4;
	texEntry.params.useFixedPalette = useFixedPalette;
	texEntry.params.fixedPalette = useFixedPalette ? fixedPalette : NULL;
	texEntry.params.pnam = _strdup(pnam);
	texEntry.path = _wcsdup(path);
	texEntry.outPath = outPath;
	StListAdd((StList *) param, &texEntry);

	return TRUE;
}

BOOL CALLBACK BatchTexConvertDirectoryCallback(LPCWSTR path, void *param) {
	return TRUE;
}

BOOL CALLBACK BatchTexConvertDirectoryExclusion(LPCWSTR path, void *param) {
	//if name ends in converted or converted\, reject (return FALSE)
	if (BatchTexPathEndsWith(path, L"\\converted\\")) return FALSE;
	if (BatchTexPathEndsWith(path, L"\\converted")) return FALSE;
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
				WCHAR *path = UiDlgBrowseForFolder(hWnd, L"Select output folder...");
				if (path == NULL) break;

				UiEditSetText(data->hWndDirectory, path);
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
				if (allComplete) {
					EnableWindow((HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT), TRUE);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}
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
	EditorAddFilter(cls, TEXTURE_TYPE_GRF, L"grf", L"GRF Files (*.grf)\0*.grf\0");

	RegisterTexturePreviewClass();
	RegisterConvertDialogClass();
	RegisterCompressionProgressClass();
	RegisterTexturePaletteEditorClass();
	RegisterTextureTileEditorClass();
	RegisterBatchTextureDialogClass();
	RegisterGenericClass(L"BatchProgressClass", BatchTexProgressProc, sizeof(void *));
}

static HWND CreateTexturePaletteEditor(TEXTUREEDITORDATA *data) {
	HWND h = CreateWindowEx(0, L"TexturePaletteEditorClass", L"Palette Editor",
		WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL | WS_CHILD, 0, 0, 0, 0, data->hWnd, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
	return h;
}

static void TexViewerEnsurePaletteEditor(TEXTUREEDITORDATA *data) {
	int fmt = FORMAT(data->texture->texture.texels.texImageParam);
	if (fmt != 0 && fmt != CT_DIRECT) {
		//create palette editor
		if (data->hWndPaletteEditor == NULL) data->hWndPaletteEditor = CreateTexturePaletteEditor(data);
		InvalidateRect(data->hWndPaletteEditor, NULL, FALSE);
	} else {
		//destroy palette editor
		if (data->hWndPaletteEditor != NULL) DestroyWindow(data->hWndPaletteEditor);
		data->hWndPaletteEditor = NULL;
	}
	TexViewerUpdateLayout(data);

	//the texture viewer window scrollbars might not update correctly.
	RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
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
		TextureObject *texture = (TextureObject *) calloc(1, sizeof(TextureObject));
		TxReadFile(texture, path);
		SendMessage(h, NV_SETPATH, wcslen(path), (LPARAM) path);
		SendMessage(h, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) texture);
		EditorSetFile(h, path);
	}
	return h;
}

HWND CreateTextureEditorImmediate(int x, int y, int width, int height, HWND hWndParent, TEXTURE *texture) {
	TextureObject *texObj = (TextureObject *) calloc(1, sizeof(TextureObject));
	TxInit(texObj, TEXTURE_TYPE_NNSTGA);
	memcpy(&texObj->texture, texture, sizeof(TEXTURE));

	HWND h = EditorCreate(L"TextureEditorClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) texObj);
	return h;
}
