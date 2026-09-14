#include "texconvdlg.h"
#include "nitropaint.h"
#include "ui.h"
#include "object/filecommon.h"
#include "object/NitroPalette.h"
#include "editor/textureeditor.h"

#include <CommCtrl.h>

typedef struct TexconvDlgData_ {
	HWND hWndMain;
	COLOR32 alphaKey;

	COLORREF customColors[16];

	HWND hWndFormat;
	HWND hWndDither;
	HWND hWndDitherAlpha;
	HWND hWndDiffuseAmount;
	HWND hWndCheckboxAlphaKey;
	HWND hWndSelectAlphaKey;

	HWND hWndPaletteName;
	HWND hWndFixedPalette;
	HWND hWndPaletteInput;
	HWND hWndPaletteBrowse;
	HWND hWndPaletteSize;
	HWND hWndColor0Transparent;

	HWND hWndLimitPalette;
	HWND hWndColorEntries;
	HWND hWndOptimizationSlider;
	HWND hWndOptimizationLabel;

	NpBalanceControl balance;

	HWND hWndConvertButton;

	TexconvDialogParam *params;  // out data
	int result;
} TexconvDlgData;

static void TexconvDlgUpdate(TexconvDlgData *data) {
	int fmt = UiCbGetCurSel(data->hWndFormat) + 1;
	BOOL texParamFixed = data->params->noWritePalette;
	BOOL isPlttN = fmt == GX_TEXFMT_PLTT4 || fmt == GX_TEXFMT_PLTT16 || fmt == GX_TEXFMT_PLTT256;
	BOOL isPltt  = fmt != GX_TEXFMT_DIRECT;
	BOOL is4x4   = fmt == GX_TEXFMT_TEX4x4;

	BOOL fixedPalette  = GetCheckboxChecked(data->hWndFixedPalette) && isPltt;
	BOOL ditherEnabled = GetCheckboxChecked(data->hWndDither);
	BOOL limitPalette  = GetCheckboxChecked(data->hWndLimitPalette) && is4x4 && !fixedPalette;
	BOOL useAlphaKey   = GetCheckboxChecked(data->hWndCheckboxAlphaKey);
	
	//general settings
	EnableWindow(data->hWndSelectAlphaKey, useAlphaKey);
	EnableWindow(data->hWndDitherAlpha,    ditherEnabled);
	EnableWindow(data->hWndDiffuseAmount,  ditherEnabled);

	//the various settings that are suppressed when texture parameters shall not be changed
	EnableWindow(data->hWndFormat,                !texParamFixed);
	EnableWindow(data->hWndColor0Transparent,     !texParamFixed && isPlttN);
	EnableWindow(data->hWndColorEntries,          !texParamFixed && limitPalette);
	EnableWindow(data->hWndPaletteInput,          !texParamFixed && fixedPalette);
	EnableWindow(data->hWndPaletteBrowse,         !texParamFixed && fixedPalette);
	EnableWindow(data->hWndPaletteName,           !texParamFixed && isPltt);
	EnableWindow(data->hWndFixedPalette,          !texParamFixed && isPltt);
	EnableWindow(data->balance.hWndEnhanceColors, !texParamFixed && isPltt);
	EnableWindow(data->hWndOptimizationSlider,    !texParamFixed && is4x4            && !fixedPalette);
	EnableWindow(data->hWndPaletteSize,           !texParamFixed && isPltt && !is4x4 && !fixedPalette);
	EnableWindow(data->hWndLimitPalette,          !texParamFixed && is4x4            && !fixedPalette);
}

static void TexViewerShowTooltip(HWND hWndParent, HWND hWndCtl, const wchar_t *pstr) {
	HWND hTool = CreateWindow(TOOLTIPS_CLASS, NULL, WS_VISIBLE | WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);

	TOOLINFO toolInfo = { 0 };
	toolInfo.cbSize = sizeof(toolInfo);
	toolInfo.hwnd = hWndParent;
	toolInfo.lpszText = (LPWSTR) pstr;
	toolInfo.uId = (UINT_PTR) hWndCtl;
	toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	SendMessage(hTool, TTM_ADDTOOL, 0, (LPARAM) &toolInfo);
}

static void TexconvDlgCallbackUpdate(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	TexconvDlgUpdate((TexconvDlgData *) param);
}

static LRESULT CALLBACK ConvertDialogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TexconvDlgData *data = (TexconvDlgData *) UiDlgGetData(hWnd);
	switch (msg) {
		case WM_CREATE:
		{
			int boxWidth = 100 + 100 + 10 + 10 + 10;     // box width
			int boxHeight = 5 * 27 - 5 + 10 + 10 + 10;   // first row height
			int boxHeight2 = 3 * 27 - 5 + 10 + 10 + 10;  // second row height
			int boxHeight3 = 3 * 27 - 5 + 10 + 10 + 10;  // third row height
			int width = 30 + 2 * boxWidth;               // window width
			int height = 10 + boxHeight + 10 + boxHeight2 + 10 + boxHeight3 + 10 + 22 + 10;  // window height

			int leftX = 10 + 10;                                    // left box X
			int rightX = 10 + boxWidth + 10 + 10;                   // right box X
			int topY = 10 + 10 + 8;                                 // top box Y
			int middleY = boxHeight + 10 + topY;                    // middle box Y
			int bottomY = boxHeight + 10 + boxHeight2 + 10 + topY;  // bottom box Y

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

			data->hWndConvertButton = CreateButton(hWnd, L"Convert", width / 2 - 100, height - 32, 200, 22, TRUE);

			//populate the dropdown list
			for (int i = 1; i <= GX_TEXFMT_DIRECT; i++) {
				WCHAR bf[16];
				mbstowcs(bf, TxNameFromTexFormat(i), sizeof(bf) / sizeof(bf[0]));
				UiCbAddString(data->hWndFormat, bf);
			}

			int format = data->params->param.fmt;
			UiCbSetCurSel(data->hWndFormat, format - 1);

			//based on the texture format and presence of transparent pixels, select default color 0 mode. This option
			//only applies to paletteN texture formats, but we'll decide as though we were using one of those formats,
			//in case the user changes the texture format, the default settings will still be applicable.
			if (data->params->param.c0xp) {
				SendMessage(data->hWndColor0Transparent, BM_SETCHECK, BST_CHECKED, 0);
			}
			
			data->alphaKey = data->params->alphaKey;

			//pick default 4x4 color count
			int maxColors = data->params->maxColors;
			SetEditNumber(data->hWndColorEntries, maxColors);

			//fill palette name
			WCHAR *pname = data->params->paletteName;
			if (pname != NULL) UiEditSetText(data->hWndPaletteName, pname);

			if (data->params->noWritePalette) {
				SendMessage(data->hWndFixedPalette, BM_SETCHECK, BST_CHECKED, 0);
				if (data->params->param.fmt != GX_TEXFMT_DIRECT) {
					UiEditSetText(data->hWndPaletteInput, data->params->fixedPalettePath);
				}
			}

			TexconvDlgUpdate(data);

			//register controls
			UiDlgRegisterCtlOK(hWnd, data->hWndConvertButton);
			UiDlgRegisterCtlCommand(hWnd, data->hWndFixedPalette,     BN_CLICKED, TexconvDlgCallbackUpdate);
			UiDlgRegisterCtlCommand(hWnd, data->hWndDither,           BN_CLICKED, TexconvDlgCallbackUpdate);
			UiDlgRegisterCtlCommand(hWnd, data->hWndDitherAlpha,      BN_CLICKED, TexconvDlgCallbackUpdate);
			UiDlgRegisterCtlCommand(hWnd, data->hWndLimitPalette,     BN_CLICKED, TexconvDlgCallbackUpdate);
			UiDlgRegisterCtlCommand(hWnd, data->hWndCheckboxAlphaKey, BN_CLICKED, TexconvDlgCallbackUpdate);
			
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int idc = LOWORD(wParam);
			if (hWndControl || idc) {
				int controlCode = HIWORD(wParam);
				if (hWndControl == data->hWndFormat && controlCode == LBN_SELCHANGE) {
					TexconvDlgUpdate(data);

					//color count - update for paletted textures
					int format = UiCbGetCurSel(hWndControl) + 1;
					if (format != GX_TEXFMT_DIRECT && format != GX_TEXFMT_TEX4x4) {
						int colorCounts[] = { 0, 32, 4, 16, 256, 0, 8, 0 };
						SetEditNumber(data->hWndPaletteSize, colorCounts[format]);
					}
				} else if (hWndControl == data->hWndSelectAlphaKey && controlCode == BN_CLICKED) {
					//custom colors buffer
					COLORREF *pCust = data->params->pCustomColors;
					if (pCust == NULL) pCust = data->customColors;

					//choose a color for the alpha key
					CHOOSECOLOR cc = { 0 };
					cc.lStructSize = sizeof(cc);
					cc.hInstance = (HWND) (HINSTANCE) GetWindowLongPtr(hWnd, GWL_HINSTANCE); //weird struct definition
					cc.hwndOwner = hWnd;
					cc.rgbResult = data->alphaKey;
					cc.lpCustColors = pCust;
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
				} else if (idc == IDOK && controlCode == BN_CLICKED) {
					TxConversionParameters *params = &data->params->param;

					int fmt = UiCbGetCurSel(data->hWndFormat) + 1;

					WCHAR path[MAX_PATH];
					SendMessage(data->hWndPaletteInput, WM_GETTEXT, MAX_PATH, (LPARAM) path);

					COLOR *fixedPalette = NULL;
					unsigned int fixedPaletteSize = 0;

					BOOL useFixedPalette = GetCheckboxChecked(data->hWndFixedPalette);
					if (useFixedPalette && path[0]) {
						//read fixed palette
						NCLR *paletteFile = (NCLR *) ObjAutoReadFile(path, FILE_TYPE_PALETTE);
						if (paletteFile == NULL) {
							MessageBox(hWnd, L"Invalid palette file.", L"Invalid file", MB_ICONERROR);
							break;
						}

						//copy colors out
						fixedPaletteSize = paletteFile->nColors;
						fixedPalette = (COLOR *) calloc(fixedPaletteSize, sizeof(COLOR));
						memcpy(fixedPalette, paletteFile->colors, fixedPaletteSize * sizeof(COLOR));

						//release object
						ObjFree(&paletteFile->header);
					}

					WCHAR bf[64];
					SendMessage(data->hWndPaletteName, WM_GETTEXT, 63, (LPARAM) bf);

					int colorEntries = GetEditNumber(data->hWndColorEntries); // for 4x4
					int paletteSize = GetEditNumber(data->hWndPaletteSize);   // for non-4x4

					//if we set to not limit palette, set the max size to the max allowed
					BOOL limitPalette = GetCheckboxChecked(data->hWndLimitPalette);
					if (!limitPalette || colorEntries > 32768) {
						colorEntries = 32768;
					}

					//check texture format 
					unsigned int width = data->params->width, height = data->params->height;
					unsigned int nPx = width * height;
					unsigned int texelSize = TxCalcTexelSize(fmt << 20, width, height);
					
					if (fmt == GX_TEXFMT_TEX4x4 && nPx > (512 * 1024)) {
						//ordinary texture VRAM allocation prohibits this
						int cfm = MessageBox(hWnd, L"Converting tex4x4 texture larger than 1024x512. Proceed?", L"Texture Size Warning", 
							MB_ICONWARNING | MB_YESNO);
						if (cfm == IDNO) {
							free(fixedPalette);
							break;
						}
					}
					if (texelSize > (512 * 1024) || (fmt == GX_TEXFMT_TEX4x4 && texelSize > (256 * 1024))) {
						//texture cannot fit in VRAM (512KB for normal texture, 256KB for 4x4)
						int cfm = MessageBox(hWnd, L"Texture data size exceeds VRAM capacity. Proceed?", L"Texture Size Warning", 
							MB_ICONWARNING | MB_YESNO);
						if (cfm == IDNO) {
							free(fixedPalette);
							break;
						}
					}

					//copy pixel buffer
					COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
					memcpy(px, data->params->px, width * height * sizeof(COLOR32));

					//alpha key preprocessing of input image
					BOOL useAlphaKey = GetCheckboxChecked(data->hWndCheckboxAlphaKey);
					if (useAlphaKey) {
						for (unsigned int i = 0; i < width * height; i++) {
							COLOR32 c = px[i];
							if ((c & 0x00FFFFFF) == (data->alphaKey & 0x00FFFFFF)) {
								px[i] = 0;
							}
						}
					}

					params->diffuseAmount = GetEditNumber(data->hWndDiffuseAmount) / 100.0f;
					params->threshold = GetTrackbarPosition(data->hWndOptimizationSlider);

					params->dither = GetCheckboxChecked(data->hWndDither);
					params->ditherAlpha = GetCheckboxChecked(data->hWndDitherAlpha);
					params->c0xp = GetCheckboxChecked(data->hWndColor0Transparent);

					params->px = px;
					params->width = width;
					params->height = height;
					params->fmt = fmt;
					params->colorEntries = useFixedPalette ? fixedPaletteSize : (fmt == GX_TEXFMT_TEX4x4 ? colorEntries : paletteSize);
					params->fixedPalette = useFixedPalette ? fixedPalette : NULL;
					params->pnam = TexNarrowResourceNameFromWideChar(bf);
					NpGetBalanceSetting(&data->balance, &params->balance);

					data->result = 1; // complete

					UiDlgEnd(hWnd);
				} else if (idc == IDCANCEL) {
					data->result = 0; // not complete
					UiDlgEnd(hWnd);
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
	return DefModalProc(hWnd, msg, wParam, lParam);
}

int TexconvDialog(HWND hWnd, TexconvDialogParam *param) {
	TexconvDlgData data = { 0 };
	data.hWndMain = hWnd;
	data.params = param;

	UiDlgCreateModal(hWnd, ConvertDialogWndProc, L"Convert Texture", 490, 444, &data);

	return data.result;
}
