#include <Windows.h>
#include <CommCtrl.h>
#include <math.h>

#include "resource.h"
#include "nftrviewer.h"
#include "gdip.h"
#include "colorchooser.h"

#define NFTR_VIEWER_CLASS_NAME    L"NftrViewerClass"
#define NFTR_VIEWER_MARGIN_CLASS  L"NftrViewerMarginClass"
#define NFTR_VIEWER_PREVIEW_CLASS L"NftrViewerPreviewClass"

#define PREVIEW_ICON_WIDTH  48
#define PREVIEW_ICON_HEIGHT 48

#define TIMER_DBLCLICK            1
#define TIMER_DBLCLICK_DURATION 500

static NFTR_GLYPH *NftrViewerGetGlyph(NFTRVIEWERDATA *data, int i);
static NFTR_GLYPH *NftrViewerGetCurrentGlyph(NFTRVIEWERDATA *data);
static int NftrViewerGetGlyphIndexByCP(NFTRVIEWERDATA *data, uint16_t cp);
static NFTR_GLYPH *NftrViewerGetGlyphByCP(NFTRVIEWERDATA *data, uint16_t cp);
static NFTR_GLYPH *NftrViewerGetDefaultGlyph(NFTRVIEWERDATA *data);
static void NftrViewerFullRefreshGlyphList(NFTRVIEWERDATA *data, int iFirst, int iLast);

// UI commands
static void NftrViewerCmdPreviewTextChanged(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdPrevSpaceXChanged(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdPrevSpaceYChanged(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdImportCodeMap(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdExportCodeMap(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdSetBitDepth(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdSetAscent(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdSetGlyphLeading(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdSetGlyphWidth(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdSetGlyphTrailing(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdSetCellWidth(HWND hWnd, HWND hWndControl, int notif, void *data);
static void NftrViewerCmdSetCellHeight(HWND hWnd, HWND hWndControl, int notif, void *data);



// ----- glyph rendering routines

static COLOR32 NftrViewerSampleGlyph(NFTRVIEWERDATA *data, NFTR_GLYPH *glyph, int x, int y, int renderTransparent) {
	unsigned int cidx = 0;
	if (glyph != NULL) {
		cidx = glyph->px[x + y * data->nftr->cellWidth];
	}

	COLOR32 col = 0;
	unsigned int cmax = (1 << data->nftr->bpp) - 1;
	if (renderTransparent) {
		//render transparent: use last palette color and alpha blend
		COLOR32 drawCol = ColorConvertFromDS(data->palette[cmax]);
		unsigned int drawR = (drawCol >>  0) & 0xFF;
		unsigned int drawG = (drawCol >>  8) & 0xFF;
		unsigned int drawB = (drawCol >> 16) & 0xFF;

		//round alpha to scale of 0-255
		unsigned int a = (cidx * 510 + cmax) / (cmax * 2);
		col = (drawR << 0) | (drawG << 8) | (drawB << 16) | (a << 24);
	} else {
		//no render transparent: use color palette
		col = ColorConvertFromDS(data->palette[cidx]) | 0xFF000000;
	}
	return col;
}

static COLOR32 *NftrViewerRenderSingleGlyphListPreview(NFTRVIEWERDATA *data, NFTR_GLYPH *glyph, COLOR32 col, COLOR32 *temp) {
	int cellWidth = data->nftr->cellWidth;
	int cellHeight = data->nftr->cellHeight;

	unsigned int alphaMax = (1 << data->nftr->bpp) - 1;
	for (int y = 0; y < cellHeight; y++) {
		for (int x = 0; x < cellWidth; x++) {
			COLOR32 drawColor = NftrViewerSampleGlyph(data, glyph, x, y, 0);
			
			temp[x + y * cellWidth] = 0xFF000000 | REVERSE(drawColor);
		}
	}

	//produce scaled+cropped image
	COLOR32 *scaled = ImgScaleEx(temp, cellWidth, cellHeight, PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, IMG_SCALE_FIT);
	return scaled;
}

static void NftrViewerRenderGlyphMasks(NFTRVIEWERDATA *data, NFTR_GLYPH *glyph, COLOR32 col, HBITMAP *pColorbm, HBITMAP *pMaskbm) {
	int cellWidth = data->nftr->cellWidth, cellHeight = data->nftr->cellHeight;

	//render each glyph to the imagelist
	COLOR32 *px = (COLOR32 *) calloc(cellWidth * cellHeight, sizeof(COLOR32));
	COLOR32 *imglist = NftrViewerRenderSingleGlyphListPreview(data, glyph, col, px);
	free(px);

	//render mask
	unsigned char *mask = ImgCreateAlphaMask(imglist, PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 0x80, NULL, NULL);

	//create bitmaps
	HBITMAP hBmColor = CreateBitmap(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 1, 32, imglist);
	HBITMAP hBmAlpha = CreateBitmap(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 1, 1, mask);
	free(imglist);
	free(mask);

	*pColorbm = hBmColor;
	*pMaskbm = hBmAlpha;
}


// ----- encoding/decoding routines

static wchar_t NftrViewerDecodeSjisCharacter(uint16_t cpSjis) {
	unsigned char byte1 = (cpSjis >> 0) & 0xFF;
	unsigned char byte2 = (cpSjis >> 8) & 0xFF;

	//ASCII plane
	if (byte2 == 0 && byte1 < 0xA1 && (byte1 != 0x5C && byte1 != 0x7E)) {
		return (wchar_t) byte1;
	}

	char buf[3] = { 0 };
	wchar_t bufU[2] = { 0 };

	if (byte2 == 0) {
		buf[0] = byte1;
	} else {
		buf[0] = byte2;
		buf[1] = byte1;
	}
	MultiByteToWideChar(932, MB_ERR_INVALID_CHARS | MB_PRECOMPOSED, buf, sizeof(buf), bufU, 2);

	return bufU[0];
}

static int NftrViewerEncodeSjisCharacter(wchar_t chr) {
	wchar_t bufU[2] = { 0 };
	char bufJ[3] = { 0 };
	bufU[0] = chr;

	int n = WideCharToMultiByte(932, 0, bufU, 2, bufJ, 3, NULL, NULL);
	if (n < 1) return -1; // error

	//for SJIS characters with 1 byte
	if (bufJ[1] == '\0') return (unsigned char) bufJ[0];

	//for SJIS characters with 2 byte
	uint16_t jis = 0;
	jis |= ((unsigned char) bufJ[0]) << 8;
	jis |= ((unsigned char) bufJ[1]) << 0;
	return jis;
}

static wchar_t NftrDecodeCharacter(NFTR *nftr, uint16_t cp) {
	if (nftr->charset == FONT_CHARSET_SJIS) return NftrViewerDecodeSjisCharacter(cp);
	return (wchar_t) cp;
}

static uint16_t NftrEncodeCharacter(NFTR *nftr, wchar_t chr) {
	if (nftr->charset == FONT_CHARSET_SJIS) return NftrViewerEncodeSjisCharacter(chr);
	return (uint16_t) chr;
}


// ----- glyph cache routines

#define GLYPH_CACHE_SIZE 64

typedef struct NftrViewerCacheEntry_ {
	uint16_t cp;
	int image;
} NftrViewerCacheEntry;

static void NftrViewerCacheInit(NFTRVIEWERDATA *data) {
	if (data->glyphCacheInit) return;

	StListCreateInline(&data->glyphCache, NftrViewerCacheEntry, NULL);
	StListCreateInline(&data->glyphCacheFree, int, NULL);

	//populate free images
	for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
		StListAdd(&data->glyphCacheFree, &i);
	}
	data->glyphCacheInit = 1;
}

static void NftrViewerCacheFree(NFTRVIEWERDATA *data) {
	if (data->glyphCacheInit) {
		StListFree(&data->glyphCache);
		StListFree(&data->glyphCacheFree);
		data->glyphCacheInit = 0;
	}
}

static void NftrViewerCacheInvalidateAll(NFTRVIEWERDATA *data) {
	StListClear(&data->glyphCache);
	StListClear(&data->glyphCacheFree);
	for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
		StListAdd(&data->glyphCacheFree, &i);
	}
}

static void NftrViewerCacheInvalidateGlyphByCP(NFTRVIEWERDATA *data, uint16_t cp) {
	//invalidate single code point
	for (unsigned int i = 0; i < data->glyphCache.length; i++) {
		NftrViewerCacheEntry *ent = StListGetPtr(&data->glyphCache, i);
		if (ent->cp == cp) {
			//remove from cache
			int remove = ent->image;
			StListRemove(&data->glyphCache, i);
			StListAdd(&data->glyphCacheFree, &remove);
			return;
		}
	}
}

static void NftrViewerCacheInvalidateGlyphByIndex(NFTRVIEWERDATA *data, int i) {
	NFTR_GLYPH *glyph = NftrViewerGetGlyph(data, i);
	if (glyph == NULL) return;

	NftrViewerCacheInvalidateGlyphByCP(data, glyph->cp);
}

static int NftrViewerCacheGetByCP(NFTRVIEWERDATA *data, uint16_t cp) {
	//check the cache
	for (unsigned int i = 0; i < data->glyphCache.length; i++) {
		NftrViewerCacheEntry *ent = StListGetPtr(&data->glyphCache, i);
		if (ent->cp == cp) {
			//found
			int iImage = ent->image;
			NftrViewerCacheEntry cpy;
			StListGet(&data->glyphCache, i, &cpy);
			StListRemove(&data->glyphCache, i);
			StListAdd(&data->glyphCache, &cpy);
			return iImage;
		}
	}

	NFTR_GLYPH *glyph = NftrViewerGetGlyphByCP(data, cp);
	if (glyph == NULL) return -1;

	//create entry
	int newImage = -1;
	if (data->glyphCache.length >= GLYPH_CACHE_SIZE) {
		//remove the oldest glyph cache entry
		NftrViewerCacheEntry *ent0 = StListGetPtr(&data->glyphCache, 0);
		newImage = ent0->image;
		StListRemove(&data->glyphCache, 0);
	} else {
		//get next free image index
		StListGet(&data->glyphCacheFree, 0, &newImage);
		StListRemove(&data->glyphCacheFree, 0);
	}

	//add entry
	NftrViewerCacheEntry newent;
	newent.cp = cp;
	newent.image = newImage;
	StListAdd(&data->glyphCache, &newent);

	//render
	HBITMAP hbmColor, hbmMask;
	HIMAGELIST himl = ListView_GetImageList(data->hWndGlyphList, LVSIL_NORMAL);
	NftrViewerRenderGlyphMasks(data, glyph, 0x00000, &hbmColor, &hbmMask);
	ImageList_Replace(himl, newImage, hbmColor, hbmMask);
	DeleteObject(hbmColor);
	DeleteObject(hbmMask);

	return newImage;
}


static void NftrViewerRenderGlyph(NFTR *nftr, COLOR32 *pxbuf, int width, int height, int x, int y, NFTR_GLYPH *glyph, const COLOR *pltt) {
	for (int cellY = 0; cellY < nftr->cellHeight; cellY++) {
		for (int cellX = 0; cellX < nftr->cellWidth; cellX++) {
			int destX = x + cellX;
			int destY = y + cellY;
			if (destX < 0 || destX >= width || destY < 0 || destY >= height) continue;

			int col = glyph->px[cellX + cellY * nftr->cellWidth];
			if (!col) continue;

			//put pixel
			COLOR32 c = ColorConvertFromDS(pltt[col]);
			pxbuf[destX + destY * width] = REVERSE(c) | 0xFF000000;
		}
	}
}

void NftrRenderString(NFTR *nftr, COLOR32 *pxbuf, int width, int height, const wchar_t *str, int spaceX, int spaceY, const COLOR *pltt) {
	if (str == NULL) return;

	//render glyph string
	int x = 0, y = 0;

	wchar_t c;
	int nCharsLine = 0;
	while ((c = *(str++)) != L'\0') {
		if (c == L'\r') continue; // ignore carriage returns
		if (c == L'\n') {
			//new line (TODO: text orientation)
			x = 0;
			y += nftr->lineHeight + spaceY;
			nCharsLine = 0;
			continue;
		}
		
		NFTR_GLYPH *glyph = NftrGetGlyphByCP(nftr, NftrEncodeCharacter(nftr, c));
		if (glyph == NULL) glyph = NftrGetInvalidGlyph(nftr);

		if (glyph != NULL) {
			//draw
			if (nCharsLine > 0) x += glyph->spaceLeft;
			NftrViewerRenderGlyph(nftr, pxbuf, width, height, x, y, glyph, pltt);

			//advance position (TODO: consider text orientation)
			x += glyph->width + glyph->spaceRight + spaceX;
			nCharsLine++;
		}
	}
}

static void NftrViewerRenderString(NFTRVIEWERDATA *data, COLOR32 *pxbuf, int width, int height, const wchar_t *str) {
	if (data->nftr == NULL || !data->nftr->hasCodeMap) return;

	NftrRenderString(data->nftr, pxbuf, width, height, str, data->spaceX, data->spaceY, data->palette);
}


// ----- data manipulation routines

static NFTR_GLYPH *NftrViewerGetGlyph(NFTRVIEWERDATA *data, int i) {
	if (i < 0 || i >= data->nftr->nGlyph) return NULL;
	return &data->nftr->glyphs[i];
}

static NFTR_GLYPH *NftrViewerGetCurrentGlyph(NFTRVIEWERDATA *data) {
	return NftrViewerGetGlyph(data, data->curGlyph);
}

static int NftrViewerGetGlyphIndexByCP(NFTRVIEWERDATA *data, uint16_t cp) {
	return NftrGetGlyphIndexByCP(data->nftr, cp);
}

static NFTR_GLYPH *NftrViewerGetGlyphByCP(NFTRVIEWERDATA *data, uint16_t cp) {
	return NftrGetGlyphByCP(data->nftr, cp);
}

static NFTR_GLYPH *NftrViewerGetDefaultGlyph(NFTRVIEWERDATA *data) {
	for (int i = 0; i < data->nftr->nGlyph; i++) {
		if (data->nftr->glyphs[i].isInvalid) return &data->nftr->glyphs[i];
	}
	return NULL;
}

static void NftrViewerPutPixel(NFTRVIEWERDATA *data, int x, int y) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	if (x >= 0 && y >= 0 && x < data->nftr->cellWidth && y < data->nftr->cellHeight) {
		glyph->px[x + y * data->nftr->cellWidth] = data->selectedColor;
	}
}

static void NftrViewerSetCurrentColor(NFTRVIEWERDATA *data, unsigned int col) {
	unsigned int maxCol = (1 << data->nftr->bpp) - 1;
	if (col > maxCol) col = maxCol;

	data->selectedColor = col;
}


// ----- editor backend routines

static void NftrViewerSetCurrentGlyphByIndex(NFTRVIEWERDATA *data, int i, BOOL updateList) {
	//bound check
	NFTR_GLYPH *glyph = NftrViewerGetGlyph(data, i);
	if (glyph == NULL) return;

	//update list
	if (updateList) {
		SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);
		ListView_SetItemState(data->hWndGlyphList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_SetItemState(data->hWndGlyphList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);
	}

	//update glyph properties
	data->curGlyph = i;
	SetEditNumber(data->hWndInputGlyphWidth, glyph->width);
	SetEditNumber(data->hWndInputGlyphLeading, glyph->spaceLeft);
	SetEditNumber(data->hWndInputGlyphTrailing, glyph->spaceRight);

	InvalidateRect(data->hWndMargin, NULL, FALSE);
}

static void NftrViewerSetCurrentGlyphByCodePoint(NFTRVIEWERDATA *data, int cc, BOOL updateList) {
	//if no code points, cannot perform
	if (!data->nftr->hasCodeMap) return;

	int gidx = NftrViewerGetGlyphIndexByCP(data, cc);
	if (gidx == -1) return;

	NftrViewerSetCurrentGlyphByIndex(data, gidx, updateList);
}



// ----- editor interface functions

static void NftrViewerCalcPosCellEditor(NFTRVIEWERDATA *data, RECT *pRc) {
	float scale = GetDpiScale();

	HWND hWnd = data->hWnd;
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	pRc->left = rcClient.right * 2 / 5;
	pRc->top = 0;
	pRc->right = rcClient.right - UI_SCALE_COORD(150, scale);
	pRc->bottom = rcClient.bottom * 2 / 3;
}

static void NftrViewerCalcPosPalette(NFTRVIEWERDATA *data, RECT *pRc) {
	RECT rcClient, rcCell;
	GetClientRect(data->hWnd, &rcClient);
	NftrViewerCalcPosCellEditor(data, &rcCell);

	float scale = GetDpiScale();
	pRc->left = rcCell.left;
	pRc->top = rcCell.bottom;
	pRc->bottom = pRc->top + UI_SCALE_COORD(22, scale);
	pRc->right = rcClient.right - UI_SCALE_COORD(150, scale);
}

static void NftrViewerCalcPosPreview(NFTRVIEWERDATA *data, RECT *pRcInput, RECT *pRcPreview) {
	RECT rcClient, rcCell;
	GetClientRect(data->hWnd, &rcClient);
	NftrViewerCalcPosCellEditor(data, &rcCell);

	float scale = GetDpiScale();
	int prevWidth = rcClient.right - rcCell.left - UI_SCALE_COORD(150, scale);

	if (pRcInput != NULL) {
		pRcInput->left = rcCell.left;
		pRcInput->top = rcCell.bottom + UI_SCALE_COORD(22, scale);
		pRcInput->bottom = rcClient.bottom;
		pRcInput->right = pRcInput->left + (prevWidth / 2);
	}

	if (pRcPreview != NULL) {
		pRcPreview->left = rcCell.left + prevWidth / 2;
		pRcPreview->top = rcCell.bottom + UI_SCALE_COORD(22, scale);
		pRcPreview->bottom = rcClient.bottom;
		pRcPreview->right = rcClient.right - UI_SCALE_COORD(150, scale);
	}
}

static void NftrViewerUpdatePreview(NFTRVIEWERDATA *data) {
	//get bounds and invalidate
	RECT rcTextPreview;
	NftrViewerCalcPosPreview(data, NULL, &rcTextPreview);
	InvalidateRect(data->hWnd, &rcTextPreview, FALSE);
}

static void NftrViewerFontUpdated(NFTRVIEWERDATA *data) {
	//update cell and text preview
	InvalidateRect(data->hWndMargin, NULL, FALSE);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
	NftrViewerUpdatePreview(data);
}



static int NftrViewerParseCharacter(const wchar_t *buf, int outSjis) {
	//get character code
	uint16_t inputCP = 0; // input code point
	int inputSjis = 0;    // input is in Shift-JIS

	unsigned int len = wcslen(buf);
	if (len == 0) return -1; // error (empty)

	if ((buf[0] == L'U' || buf[0] == L'u' || buf[0] == L'J' || buf[0] == L'j') && buf[1] == L'+') {
		//U+xxxx or J+xxxx
		for (unsigned int i = 2; i < len; i++) {
			wchar_t wc = buf[i];

			inputCP <<= 4;
			if (wc >= L'0' && wc <= L'9') inputCP |= wc - L'0' + 0x0;
			else if (wc >= L'A' && wc <= L'F') inputCP |= wc - L'A' + 0xA;
			else if (wc >= L'a' && wc <= L'f') inputCP |= wc - L'a' + 0xA;
			else return -1; // error (invalid hexadecimal code point)
		}

		//determine shift-JIS
		inputSjis = (buf[0] == L'J' || buf[0] == L'j');
	} else {
		//else, character input.
		if (len > 1) return -1; // error (multiple characters input)

		//single code point
		inputCP = buf[0];
		inputSjis = 0;
	}

	//if the code input is in the correct format, 
	if (inputSjis == !!outSjis) return inputCP;

	//convert
	if (outSjis) {
		//Unicode -> Shift-JIS
		return NftrViewerEncodeSjisCharacter((wchar_t) inputCP);
	} else {
		//Shift-JIS -> Unicode
		return (int) (uint16_t) NftrViewerDecodeSjisCharacter(inputCP);
	}
}

static int NftrViewerPromptCharacter(NFTRVIEWERDATA *data, LPCWSTR title, LPCWSTR prompt, uint16_t defCP) {
	HWND hWndMain = data->editorMgr->hWnd;
	WCHAR textbuf[16];
	wsprintf(textbuf, L"%c+%04X", data->nftr->charset == FONT_CHARSET_SJIS ? 'J' : 'U', defCP);

	while (1) {
		int s = PromptUserText(hWndMain, title, prompt, textbuf, sizeof(textbuf));
		if (!s) return -1;
		
		int inputCP = NftrViewerParseCharacter(textbuf, data->nftr->charset == FONT_CHARSET_SJIS);
		if (inputCP != -1) return inputCP;

		MessageBox(hWndMain, L"Invalid format. Enter a character or code point.", L"Error", MB_ICONERROR);
		// try again
	}
}


// ----- glyph list routines

static void NftrViewerGetGlyphListText(NFTRVIEWERDATA *data, int i, WCHAR *textbuf) {
	NFTR_GLYPH *glyph = NftrViewerGetGlyph(data, i);
	if (glyph == NULL) {
		textbuf[0] = L'\0';
		return;
	}

	if (data->nftr->hasCodeMap) {
		if (data->nftr->charset == FONT_CHARSET_SJIS) {
			//decode as Shift-JIS (J+0000)
			wsprintfW(textbuf, L"%c (J+%04X)", NftrViewerDecodeSjisCharacter(glyph->cp), glyph->cp);
		} else {
			//decode as Unicode (U+0000)
			wsprintfW(textbuf, L"%c (U+%04X)", glyph->cp, glyph->cp);
		}
		if (textbuf[0] == L'\0') textbuf[0] = L' ';
	} else {
		//no code point associated
		wsprintfW(textbuf, L"Glyph %d", i);
	}

	if (data->nftr->glyphs[i].isInvalid) {
		wsprintfW(textbuf + wcslen(textbuf), L"\n**Invalid**");
	}
}

static void NftrViewerUpdateGlyphListImage(NFTRVIEWERDATA *data, int i) {
	if (i < 0 || i >= data->nftr->nGlyph) return;

	//refresh list view
	NftrViewerCacheInvalidateGlyphByIndex(data, i);
	InvalidateRect(data->hWndGlyphList, NULL, FALSE);
}

static void NftrViewerReassignGlyph(NFTRVIEWERDATA *data, int i, uint16_t inputCP) {
	//first, set the code point on the glyph and sort the glyph list.
	data->nftr->glyphs[i].cp = inputCP;
	NftrEnsureSorted(data->nftr);

	//next, get the new index of glyph.
	int newI = NftrViewerGetGlyphIndexByCP(data, inputCP);
	NFTR_GLYPH *glyph = &data->nftr->glyphs[newI];

	//next, update the list view.
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);
	{
		//for the cleanest edit to the list view, iterate all glyphs and update them.
		for (int j = min(i, newI); j <= max(i, newI); j++) {
			//re-render
			NftrViewerUpdateGlyphListImage(data, j);
		}
	}

	//update font
	NftrViewerFontUpdated(data);

	InvalidateRect(data->hWndGlyphList, NULL, FALSE);
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);
}

static void NftrViewerUpdateAllGlyphTextAndImage(NFTRVIEWERDATA *data) {
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);

	//re-render
	NftrViewerCacheInvalidateAll(data);
	InvalidateRect(data->hWndGlyphList, NULL, TRUE);
	
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);
}

static void NftrViewerFullRefreshGlyphList(NFTRVIEWERDATA *data, int iFirst, int iLast) {
	//reassign glyph list items
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);

	//put new images
	NftrViewerCacheInvalidateAll(data);
	InvalidateRect(data->hWndGlyphList, NULL, FALSE);
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);
}

static void NftrViewerDeleteGlyph(NFTRVIEWERDATA *data, int i) {
	//delete glyph i from glyph list
	memmove(&data->nftr->glyphs[i], &data->nftr->glyphs[i + 1], (data->nftr->nGlyph - i - 1) * sizeof(NFTR_GLYPH));
	data->nftr->nGlyph--;
	data->nftr->glyphs = (NFTR_GLYPH *) realloc(data->nftr->glyphs, data->nftr->nGlyph * sizeof(NFTR_GLYPH));

	//reassign glyph list items
	NftrViewerFullRefreshGlyphList(data, i, data->nftr->nGlyph - 1);

	//delete last from list view
	ListView_SetItemCount(data->hWndGlyphList, data->nftr->nGlyph);

	//set selection
	int iNewSel = i;
	if (iNewSel >= data->nftr->nGlyph) iNewSel = data->nftr->nGlyph - 1;
	NftrViewerSetCurrentGlyphByIndex(data, i, TRUE);
}

static void NftrViewerCreateGlyph(NFTRVIEWERDATA *data, int cc) {
	//allocate new glyph
	data->nftr->nGlyph++;
	data->nftr->glyphs = (NFTR_GLYPH *) realloc(data->nftr->glyphs, data->nftr->nGlyph * sizeof(NFTR_GLYPH));

	NFTR_GLYPH *last = &data->nftr->glyphs[data->nftr->nGlyph - 1];
	memset(last, 0, sizeof(NFTR_GLYPH));
	last->cp = cc;
	last->px = (unsigned char *) calloc(data->nftr->cellWidth * data->nftr->cellHeight, 1);
	last->width = data->nftr->cellWidth;

	//sort
	NftrEnsureSorted(data->nftr);

	//refresh all but the last glyph
	NFTR_GLYPH *ins = NftrViewerGetGlyphByCP(data, cc);
	NftrViewerFullRefreshGlyphList(data, ins - data->nftr->glyphs, data->nftr->nGlyph - 2);

	//add and update last item
	ListView_SetItemCount(data->hWndGlyphList, data->nftr->nGlyph);
	NftrViewerFullRefreshGlyphList(data, data->nftr->nGlyph - 1, data->nftr->nGlyph - 1);
	NftrViewerFontUpdated(data);
}

static void NftrViewerSetBitDepth(NFTRVIEWERDATA *data, int depth, BOOL setDropdown) {
	int depthOld = data->nftr->bpp;

	//update bit depth
	if (depth != depthOld) {
		NftrSetBitDepth(data->nftr, depth);
	}

	//update color palette
	int nShade = 1 << data->nftr->bpp;
	for (int i = 0; i < nShade; i++) {
		int l = 31 - (i * 62 + nShade - 1) / (2 * (nShade - 1));
		COLOR c = l | (l << 5) | (l << 10);
		data->palette[i] = c;
	}

	//update dropdown
	if (setDropdown) {
		//set font parameters
		int bppsel = depth - 1;
		UiCbSetCurSel(data->hWndDepthList, bppsel);
	}

	//update palette view
	if (data->selectedColor >= nShade) {
		data->selectedColor = nShade - 1;
	}

	if (depth != depthOld) {
		RECT rcPalette;
		NftrViewerCalcPosPalette(data, &rcPalette);
		InvalidateRect(data->hWnd, &rcPalette, TRUE);

		//update glyph images
		NftrViewerFullRefreshGlyphList(data, 0, data->nftr->nGlyph - 1);
		NftrViewerFontUpdated(data);
	}
}

static void NftrViewerSetCellSize(NFTRVIEWERDATA *data, int width, int height) {
	//if size is the same, do nothing
	if (width == data->nftr->cellWidth && height == data->nftr->cellHeight) return;

	//update params
	NftrSetCellSize(data->nftr, width, height);

	//update UI
	TedUpdateSize((EDITOR_DATA *) data, &data->ted, width, height);
	NftrViewerUpdateAllGlyphTextAndImage(data);
	NftrViewerFontUpdated(data);

	//update inputs
	WCHAR textbuf[16];
	wsprintfW(textbuf, L"%d", data->nftr->cellWidth);
	SendMessage(data->hWndInputCellWidth, WM_SETTEXT, -1, (LPARAM) textbuf);
	wsprintfW(textbuf, L"%d", data->nftr->cellHeight);
	SendMessage(data->hWndInputCellHeight, WM_SETTEXT, -1, (LPARAM) textbuf);
}

static void NftrViewerSetGlyphListSize(NFTRVIEWERDATA *data, int count) {
	HWND hWndList = data->hWndGlyphList;
	SendMessage(hWndList, WM_SETREDRAW, 0, 0);

	//create glyph image list
	HIMAGELIST himl = ListView_GetImageList(data->hWndGlyphList, LVSIL_NORMAL);
	ImageList_SetImageCount(himl, GLYPH_CACHE_SIZE);
	ListView_SetItemCount(data->hWndGlyphList, count);

	SendMessage(hWndList, WM_SETREDRAW, 1, 0);
}


// ----- editor functionality

static HCURSOR NftrViewerGetCursorProc(HWND hWnd, int hit) {
	(void) hWnd;
	(void) hit;
	return LoadCursor(NULL, MAKEINTRESOURCE(32631)); // pencil cursor
}

static int NftrViewerIsSelectionModeCallback(HWND hWnd) {
	(void) hWnd;
	return 0;
}

static void NftrViewerUpdateCursorCallback(HWND hWnd, int x, int y) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NFTRVIEWERDATA *data = EditorGetData(hWndEditor);

	int hit = data->ted.mouseDownHit;
	int hitType = hit & HIT_TYPE_MASK;

	if (hitType == HIT_CONTENT) {
		NftrViewerPutPixel(data, x, y);
		InvalidateRect(hWnd, NULL, FALSE);
	}
}

static void NftrViewerPreviewHoverCallback(HWND hWnd, int tileX, int tileY) {

}

static int NftrViewerSuppressHighlightCallback(HWND hWnd) {
	return 1;
}

static void NftrViewerPreviewPaintCallback(HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NFTRVIEWERDATA *data = EditorGetData(hWndEditor);
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);

	static const COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };

	for (int y = 0; y < renderHeight; y++) {
		for (int x = 0; x < renderWidth; x++) {

			int srcX = (x + scrollX) / data->scale;
			int srcY = (y + scrollY) / data->scale;
			COLOR32 col = NftrViewerSampleGlyph(data, glyph, srcX, srcY, data->renderTransparent);
			COLOR32 backCol = checker[((x ^ y) >> 2) & 1];

			//alpha blend to checker
			unsigned int a = (col >> 24) & 0xFF;
			unsigned int r = ((a * ((col >>  0) & 0xFF)) + (255 - a) * ((backCol >>  0) & 0xFF)) / 255;
			unsigned int g = ((a * ((col >>  8) & 0xFF)) + (255 - a) * ((backCol >>  8) & 0xFF)) / 255;
			unsigned int b = ((a * ((col >> 16) & 0xFF)) + (255 - a) * ((backCol >> 16) & 0xFF)) / 255;
			fb->px[x + y * fb->width] = (r << 16) | (g << 8) | (b << 0);
		}
	}

}

static LRESULT CALLBACK NftrViewerGlyphListSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data_) {
	switch (msg) {
	}
	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void NftrViewerOnCreate(NFTRVIEWERDATA *data) {
	HWND hWnd = data->hWnd;

	data->scale = 16;
	data->showBorders = 1;
	data->curGlyph = 0;
	data->renderTransparent = GetMenuState(GetMenu(data->editorMgr->hWnd), ID_VIEW_RENDERTRANSPARENCY, MF_BYCOMMAND);
	data->spaceX = 1;
	data->spaceY = 1;

	RECT posCellEditor;
	NftrViewerCalcPosCellEditor(data, &posCellEditor);

	LPWSTR depths[] = { L"1", L"2", L"3", L"4", L"5", L"6", L"7" };

	data->hWndMargin = CreateWindow(NFTR_VIEWER_MARGIN_CLASS, L"", WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN, 
		posCellEditor.left, posCellEditor.top, posCellEditor.right - posCellEditor.left, posCellEditor.bottom - posCellEditor.top,
		hWnd, NULL, NULL, NULL);

	data->hWndImportCMap = CreateButton(hWnd, L"Import Code Map", 0, 0, 100, 22, FALSE);
	data->hWndExportCMap = CreateButton(hWnd, L"Export Code Map", 0, 0, 100, 22, FALSE);

	data->hWndLabelGlyphProp = CreateStatic(hWnd, L"Glyph Properties:", 200, 0, 100, 22);
	data->hWndLabelGlyphWidth = CreateStatic(hWnd, L" Glyph width:", 200, 0, 75, 22);
	data->hWndLabelGlyphLeading = CreateStatic(hWnd, L" Glyph leading:", 200, 27, 75, 22);
	data->hWndLabelGlyphTrailing = CreateStatic(hWnd, L" Glyph trailing:", 200, 54, 75, 22);
	data->hWndInputGlyphWidth = CreateEdit(hWnd, L"0", 280, 0, 50, 22, TRUE);
	data->hWndInputGlyphLeading = CreateEdit(hWnd, L"0", 280, 27, 50, 22, FALSE);
	data->hWndInputGlyphTrailing = CreateEdit(hWnd, L"0", 280, 54, 50, 22, FALSE);

	data->hWndLabelFontProp = CreateStatic(hWnd, L"Font Properties:", 200, 0, 100, 22);
	data->hWndDepthLabel = CreateStatic(hWnd, L" Bit depth:", posCellEditor.left, 0, 100, 22);
	data->hWndDepthList = CreateCombobox(hWnd, depths, sizeof(depths) / sizeof(depths[0]), posCellEditor.left + 100, 0, 100, 100, 0);
	data->hWndLabelCellSize = CreateStatic(hWnd, L" Cell size: ", 200, 0, 75, 22);
	data->hWndInputCellWidth = CreateButton(hWnd, L"0", 200, 0, 50, 22, FALSE);
	data->hWndInputCellHeight = CreateButton(hWnd, L"0", 200, 0, 50, 22, FALSE);
	data->hWndLabelAscent = CreateStatic(hWnd, L" Ascent:", 200, 0, 75, 22);
	data->hWndInputAscent = CreateEdit(hWnd, L"0", 200, 0, 75, 22, TRUE);

	data->hWndLabelPrevProp = CreateStatic(hWnd, L"Preview Properties:", 200, 0, 100, 22);
	data->hWndLabelPrevSpace = CreateStatic(hWnd, L" Spacing:", 200, 0, 75, 22);
	data->hWndInputPrevSpaceX = CreateEdit(hWnd, L"1", 200, 0, 50, 22, FALSE);
	data->hWndInputPrevSpaceY = CreateEdit(hWnd, L"1", 200, 0, 50, 22, FALSE);

	data->hWndPreviewInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN | WS_HSCROLL | WS_VSCROLL,
		0, 0, 100, 100, hWnd, NULL, NULL, NULL);

	NftrViewerCacheInit(data);
	DWORD lvStyle = WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VSCROLL | LVS_SINGLESEL | LVS_ICON | LVS_SHOWSELALWAYS | LVS_AUTOARRANGE | LVS_OWNERDATA;
	data->hWndGlyphList = CreateWindow(WC_LISTVIEW, L"", lvStyle, 0, 0, posCellEditor.left, posCellEditor.bottom,
		hWnd, NULL, NULL, NULL);

	//set extended style
	//ListView_SetExtendedListViewStyle(data->hWndGlyphList, LVS_EX_JUSTIFYCOLUMNS | LVS_EX_SNAPTOGRID);
	SendMessage(data->hWndGlyphList, LVM_SETVIEW, LV_VIEW_ICON, 0);
	SetWindowSubclass(data->hWndGlyphList, NftrViewerGlyphListSubclassProc, 1, 0);

	//init columns
	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	lvc.pszText = L"";
	lvc.fmt = LVCFMT_LEFT;
	ListView_InsertColumn(data->hWndGlyphList, 0, &lvc);

	//create image list
	HIMAGELIST hLarge = ImageList_Create(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, ILC_MASK | ILC_COLOR24, GLYPH_CACHE_SIZE, 1);
	ListView_SetImageList(data->hWndGlyphList, hLarge, LVSIL_NORMAL);

	FbCreate(&data->fbPreview, hWnd, 1, 1);
	data->previewText = _wcsdup(L"Preview text...");
	UiEditSetText(data->hWndPreviewInput, data->previewText);

	//create command list
	UiCtlMgrInit(&data->mgr, data);
	UiCtlMgrAddCommand(&data->mgr, data->hWndPreviewInput, EN_CHANGE, NftrViewerCmdPreviewTextChanged);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputPrevSpaceX, EN_CHANGE, NftrViewerCmdPrevSpaceXChanged);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputPrevSpaceY, EN_CHANGE, NftrViewerCmdPrevSpaceYChanged);
	UiCtlMgrAddCommand(&data->mgr, data->hWndImportCMap, BN_CLICKED, NftrViewerCmdImportCodeMap);
	UiCtlMgrAddCommand(&data->mgr, data->hWndExportCMap, BN_CLICKED, NftrViewerCmdExportCodeMap);
	UiCtlMgrAddCommand(&data->mgr, data->hWndDepthList, CBN_SELCHANGE, NftrViewerCmdSetBitDepth);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputAscent, EN_CHANGE, NftrViewerCmdSetAscent);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputGlyphLeading, EN_CHANGE, NftrViewerCmdSetGlyphLeading);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputGlyphWidth, EN_CHANGE, NftrViewerCmdSetGlyphWidth);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputGlyphTrailing, EN_CHANGE, NftrViewerCmdSetGlyphTrailing);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputCellWidth, BN_CLICKED, NftrViewerCmdSetCellWidth);
	UiCtlMgrAddCommand(&data->mgr, data->hWndInputCellHeight, BN_CLICKED, NftrViewerCmdSetCellHeight);
}

static LRESULT NftrViewerOnSize(NFTRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	float scale = GetDpiScale();

	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	RECT rcCellEditor, rcPreviewInput, rcPreviewRender, rcPalette;
	NftrViewerCalcPosCellEditor(data, &rcCellEditor);
	NftrViewerCalcPosPreview(data, &rcPreviewInput, &rcPreviewRender);
	NftrViewerCalcPosPalette(data, &rcPalette);

	int padSize = UI_SCALE_COORD(5, scale);
	int ctlWidth = UI_SCALE_COORD(100, scale);
	int ctlWidthMid = UI_SCALE_COORD(80, scale);
	int ctlWidthNarrow = UI_SCALE_COORD(60, scale);
	int ctlWidthSmall = UI_SCALE_COORD(40, scale);
	int ctlWidthTiny = UI_SCALE_COORD(30, scale);
	int ctlHeight = UI_SCALE_COORD(22, scale);
	int ctlHeightPad = UI_SCALE_COORD(24, scale); // 2px pad

	RECT rcBox;
	int giBoxX = rcClient.right - padSize - ctlWidthMid - ctlWidthNarrow;
	int giBoxY = padSize;
	int giBoxW = rcClient.right - giBoxX;
	int giBoxH = rcCellEditor.bottom;
	rcBox.left = giBoxX;
	rcBox.top = giBoxY;
	rcBox.right = giBoxX + giBoxW;
	rcBox.bottom = giBoxY + giBoxH;

	MoveWindow(data->hWndMargin, rcCellEditor.left, rcCellEditor.top, rcCellEditor.right - rcCellEditor.left,
		rcCellEditor.bottom - rcCellEditor.top, TRUE);
	MoveWindow(data->hWndImportCMap, 0, rcClient.bottom - ctlHeight, rcCellEditor.left / 2, ctlHeight, TRUE);
	MoveWindow(data->hWndExportCMap, rcCellEditor.left / 2, rcClient.bottom - ctlHeight, rcCellEditor.left - rcCellEditor.left / 2, ctlHeight, TRUE);
	MoveWindow(data->hWndGlyphList, 0, 0, rcCellEditor.left, rcClient.bottom - ctlHeight, TRUE);

	//glyph properties panel
	int glyphBoxY = giBoxY;
	MoveWindow(data->hWndLabelGlyphProp, giBoxX, glyphBoxY + ctlHeightPad * 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndLabelGlyphWidth, giBoxX, glyphBoxY + ctlHeightPad * 1, ctlWidthMid, ctlHeight, TRUE);
	MoveWindow(data->hWndInputGlyphWidth, giBoxX + ctlWidthMid, glyphBoxY + ctlHeightPad * 1, ctlWidthNarrow, ctlHeight, TRUE);
	MoveWindow(data->hWndLabelGlyphLeading, giBoxX, glyphBoxY + ctlHeightPad * 2, ctlWidthMid, ctlHeight, TRUE);
	MoveWindow(data->hWndInputGlyphLeading, giBoxX + ctlWidthMid, glyphBoxY + ctlHeightPad * 2, ctlWidthNarrow, ctlHeight, TRUE);
	MoveWindow(data->hWndLabelGlyphTrailing, giBoxX, glyphBoxY + ctlHeightPad * 3, ctlWidthMid, ctlHeight, TRUE);
	MoveWindow(data->hWndInputGlyphTrailing, giBoxX + ctlWidthMid, glyphBoxY + ctlHeightPad * 3, ctlWidthNarrow, ctlHeight, TRUE);

	//font properties panel
	int fontBoxY = giBoxY + ctlHeightPad * 4 + UI_SCALE_COORD(scale, 10);
	MoveWindow(data->hWndLabelFontProp, giBoxX, fontBoxY + ctlHeightPad * 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndDepthLabel, giBoxX, fontBoxY + ctlHeightPad * 1, ctlWidthMid, ctlHeight, TRUE);
	MoveWindow(data->hWndDepthList, giBoxX + ctlWidthMid, fontBoxY + ctlHeightPad * 1, ctlWidthSmall, ctlHeight, TRUE);
	MoveWindow(data->hWndLabelCellSize, giBoxX, fontBoxY + ctlHeightPad * 2, ctlWidthMid, ctlHeight, TRUE);
	MoveWindow(data->hWndInputCellWidth, giBoxX + ctlWidthMid, fontBoxY + ctlHeightPad * 2, ctlWidthTiny, ctlHeight, TRUE);
	MoveWindow(data->hWndInputCellHeight, giBoxX + ctlWidthMid + ctlWidthTiny, fontBoxY + ctlHeightPad * 2, ctlWidthTiny, ctlHeight, TRUE);
	MoveWindow(data->hWndLabelAscent, giBoxX, fontBoxY + ctlHeightPad * 3, ctlWidthMid, ctlHeight, TRUE);
	MoveWindow(data->hWndInputAscent, giBoxX + ctlWidthMid, fontBoxY + ctlHeightPad * 3, ctlWidthNarrow, ctlHeight, TRUE);

	//font properties panel
	int prevBoxY = fontBoxY + ctlHeightPad * 4 + UI_SCALE_COORD(scale, 10);
	MoveWindow(data->hWndLabelPrevProp, giBoxX, prevBoxY + ctlHeightPad * 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndLabelPrevSpace, giBoxX, prevBoxY + ctlHeightPad * 1, ctlWidthMid, ctlHeight, TRUE);
	MoveWindow(data->hWndInputPrevSpaceX, giBoxX + ctlWidthMid, prevBoxY + ctlHeightPad * 1, ctlWidthTiny, ctlHeight, TRUE);
	MoveWindow(data->hWndInputPrevSpaceY, giBoxX + ctlWidthMid + ctlWidthTiny, prevBoxY + ctlHeightPad * 1, ctlWidthTiny, ctlHeight, TRUE);

	MoveWindow(data->hWndPreviewInput, rcPreviewInput.left, rcPreviewInput.top,
		rcPreviewInput.right - rcPreviewInput.left, rcPreviewInput.bottom - rcPreviewInput.top, TRUE);
	InvalidateRect(data->hWnd, &rcPreviewRender, FALSE);
	InvalidateRect(data->hWnd, &rcPalette, TRUE);

	InvalidateRect(data->hWnd, &rcBox, TRUE);

	if (wParam == SIZE_RESTORED) InvalidateRect(data->hWnd, NULL, TRUE); //full update
	return DefMDIChildProc(data->hWnd, WM_SIZE, wParam, lParam);
}

static void NftrViewerOnPaint(NFTRVIEWERDATA *data) {
	if (data->nftr == NULL) return;

	float scale = GetDpiScale();
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(data->hWnd, &ps);

	RECT rcCellEditor, rcClient, rcPreview;
	GetClientRect(data->hWnd, &rcClient);
	NftrViewerCalcPosCellEditor(data, &rcCellEditor);
	NftrViewerCalcPosPreview(data, NULL, &rcPreview);

	HFONT hGuiFont = GetGUIFont();
	HFONT hFontOld = SelectObject(hDC, hGuiFont);
	
	//draw preview box
	int ctlHeight = UI_SCALE_COORD(22, scale);
	int previewX = rcPreview.left;
	int previewY = rcPreview.top;
	int previewWidth = rcPreview.right - rcPreview.left - 2;   // -2 for borders
	int previewHeight = rcPreview.bottom - rcPreview.top - 2;  // -2 for borders

	HPEN hOutline = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
	HBRUSH hbrOld = SelectObject(hDC, GetStockObject(NULL_BRUSH));
	HPEN hOldPen = SelectObject(hDC, hOutline);
	Rectangle(hDC, rcPreview.left, rcPreview.top, rcPreview.right, rcPreview.bottom);
	SelectObject(hDC, hOldPen);
	SelectObject(hDC, hbrOld);
	DeleteObject(hOutline);

	//preview render
	if (previewWidth > 0 && previewHeight > 0) {
		FbSetSize(&data->fbPreview, previewWidth, previewHeight);

		//fill with first palette color
		COLOR32 bgColor = ColorConvertFromDS(data->palette[0]) | 0xFF000000;
		bgColor = REVERSE(bgColor);
		for (int i = 0; i < data->fbPreview.width * data->fbPreview.height; i++) {
			data->fbPreview.px[i] = bgColor;
		}

		NftrViewerRenderString(data, data->fbPreview.px, data->fbPreview.width, data->fbPreview.height, data->previewText);

		FbDraw(&data->fbPreview, hDC, previewX + 1, previewY + 1, previewWidth, previewHeight, 0, 0);
	}

	//draw palette label
	RECT rcPltLabel;
	NftrViewerCalcPosPalette(data, &rcPltLabel);
	rcPltLabel.right = rcPltLabel.left + UI_SCALE_COORD(50, scale);
	SetBkColor(hDC, GetSysColor(COLOR_BTNFACE));
	DrawText(hDC, L" Palette:", -1, &rcPltLabel, DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

	//draw selections for alpha
	unsigned int nAlpha = 1 << data->nftr->bpp;
	for (unsigned int i = 0; i < nAlpha; i++) {
		COLOR32 c = ColorConvertFromDS(data->palette[i]);
		HBRUSH hbr = CreateSolidBrush(c);
		SelectObject(hDC, hbr);

		HPEN hPen;
		if (i == data->selectedColor) {
			hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
		} else {
			hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
		}
		SelectObject(hDC, hPen);

		int squareX = rcPltLabel.right + i * ctlHeight + 1;
		int squareY = previewY - ctlHeight + 1;
		int squareSize = ctlHeight - 2;
		if (i == data->selectedColor) {
			squareX++;
			squareY++;
			squareSize -= 2;
		}


		Rectangle(hDC, squareX, squareY, squareX + squareSize, squareY + squareSize);

		DeleteObject(hbr);
		DeleteObject(hPen);
	}

	SelectObject(hDC, hFontOld);
	EndPaint(data->hWnd, &ps);
}

static void NftrViewerOnNotify(NFTRVIEWERDATA *data, HWND hWnd, WPARAM wParam, LPNMLISTVIEW nm) {
	switch (nm->hdr.code) {
		case LVN_GETDISPINFO:
		{
			NMLVDISPINFO *di = (NMLVDISPINFO *) nm;
			if (di->item.iSubItem != 0) break;
			
			//fill out item structure
			if (di->item.mask & LVIF_COLFMT) {
				di->item.piColFmt = NULL;
			}
			if (di->item.mask & LVIF_COLUMNS) {
				di->item.cColumns = 0;
			}
			if (di->item.mask & LVIF_IMAGE) {
				int iImage = NftrViewerCacheGetByCP(data, data->nftr->glyphs[di->item.iItem].cp);
				di->item.iImage = iImage; // re-use imagelist images
			}
			if (di->item.mask & LVIF_TEXT) {
				//buffer is valid until the next call
				static WCHAR textbuf[32];
				NftrViewerGetGlyphListText(data, di->item.iItem, textbuf);
				di->item.pszText = textbuf;
			}
		}
		case LVN_ODCACHEHINT:
		{
			NMLVCACHEHINT *hint = (NMLVCACHEHINT *) nm;
			break;
		}
		case LVN_ODFINDITEM:
		{
			NMLVFINDITEM *fi = (NMLVFINDITEM *) nm;
			break;
		}
		case LVN_ITEMCHANGED:
		{
			if (nm->uNewState & LVIS_SELECTED) {
				//selection changed
				NftrViewerSetCurrentGlyphByIndex(data, nm->iItem, FALSE);
			}
			break;
		}
		case NM_RCLICK:
		{
			LPNMITEMACTIVATE lpnma = (LPNMITEMACTIVATE) nm;
			int item = lpnma->iItem;

			HMENU hPopup;
			if (item == -1) {
				//no item
				NftrViewerSetCurrentGlyphByIndex(data, data->curGlyph, TRUE);
				hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 7);
			} else {
				//yes item
				NftrViewerSetCurrentGlyphByIndex(data, item, TRUE);
				hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 6);
			}

			POINT mouse;
			GetCursorPos(&mouse);
			TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, data->hWnd, NULL);
			DeleteObject(hPopup);
			break;
		}
		case NM_CLICK:
		case NM_DBLCLK:
		{
			LPNMITEMACTIVATE nma = (LPNMITEMACTIVATE) nm;
			if (nma->iItem == -1) {
				//item being unselected. Mark variable to cancel the deselection.
				ListView_SetItemState(data->hWndGlyphList, data->curGlyph, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
			break;
		}
	}
}


static void NftrViewerCopyCurrentCharacter(NFTRVIEWERDATA *data) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	HANDLE hString = GlobalAlloc(GMEM_MOVEABLE, 2 * sizeof(WCHAR));
	WCHAR *textbuf = (WCHAR *) GlobalLock(hString);
	textbuf[0] = NftrDecodeCharacter(data->nftr, glyph->cp);
	textbuf[1] = L'\0';
	GlobalUnlock(hString);

	OpenClipboard(data->hWnd);
	EmptyClipboard();
	SetClipboardData(CF_UNICODETEXT, hString);
	CloseClipboard();
}

static void NftrViewerCopyCurrentGlyph(NFTRVIEWERDATA *data) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	COLOR32 *pxbuf = (COLOR32 *) calloc(data->nftr->cellWidth * data->nftr->cellHeight, sizeof(COLOR32));
	for (int i = 0; i < data->nftr->cellWidth * data->nftr->cellHeight; i++) {
		//fill with background color
		pxbuf[i] = ColorConvertFromDS(data->palette[0]);
	}

	NftrViewerRenderGlyph(data->nftr, pxbuf, data->nftr->cellWidth, data->nftr->cellHeight, 0, 0, glyph, data->palette);
	ImgSwapRedBlue(pxbuf, data->nftr->cellWidth, data->nftr->cellHeight);

	OpenClipboard(data->hWnd);
	EmptyClipboard();
	copyBitmap(pxbuf, data->nftr->cellWidth, data->nftr->cellHeight);
	CloseClipboard();
	free(pxbuf);
}

static void NftrViewerDeleteCurrentGlyph(NFTRVIEWERDATA *data) {
	NftrViewerDeleteGlyph(data, data->curGlyph);
}

static void NftrViewerReassignCurrentGlyph(NFTRVIEWERDATA *data) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	while (1) {
		int inputCP = NftrViewerPromptCharacter(data, L"Reassign Glyph", L"Enter a new character or code point:", glyph->cp);
		if (inputCP == -1) return; // exit

		//lookup code point
		NFTR_GLYPH *exist = NftrViewerGetGlyphByCP(data, inputCP);
		if (exist == NULL) {
			//reassign
			NftrViewerReassignGlyph(data, data->curGlyph, inputCP);
			return;
		}
		
		//try again
		MessageBox(data->editorMgr->hWnd, L"Character already assigned.", L"Error", MB_ICONERROR);
	}
}

static void NftrViewerMakeCurrentGlyphInvalid(NFTRVIEWERDATA *data) {
	//mark all glyphs valid, except the current glyph
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);

	for (int i = 0; i < data->nftr->nGlyph; i++) {
		int isInvalid = (i == data->curGlyph);
		if (isInvalid != data->nftr->glyphs[i].isInvalid) {
			data->nftr->glyphs[i].isInvalid = isInvalid;
		}
	}

	NftrViewerFontUpdated(data);
	InvalidateRect(data->hWndGlyphList, NULL, FALSE);
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);
}

static void NftrViewerGoTo(NFTRVIEWERDATA *data) {
	uint16_t defCP = 0;
	while (1) {
		int inputCP = NftrViewerPromptCharacter(data, L"Enter Character", L"Enter a character or code point:", defCP);
		if (inputCP == -1) return; // exit

		//lookup code point
		NFTR_GLYPH *exist = NftrViewerGetGlyphByCP(data, inputCP);
		if (exist != NULL) {
			//goto
			NftrViewerSetCurrentGlyphByCodePoint(data, inputCP, TRUE);
			ListView_EnsureVisible(data->hWndGlyphList, data->curGlyph, FALSE);
			return;
		}

		//error, no try again
		MessageBox(data->editorMgr->hWnd, L"Character not assigned.", L"Error", MB_ICONERROR);
		break;
	}
}

static void NftrViewerNewGlyph(NFTRVIEWERDATA *data) {
	uint16_t defCP = 0;
	while (1) {
		int inputCP = NftrViewerPromptCharacter(data, L"Enter Character", L"Enter a new character or code point:", defCP);
		if (inputCP == -1) return; // exit

		//lookup code point
		NFTR_GLYPH *exist = NftrViewerGetGlyphByCP(data, inputCP);
		if (exist == NULL) {
			//create
			NftrViewerCreateGlyph(data, inputCP);
			NftrViewerSetCurrentGlyphByCodePoint(data, inputCP, TRUE);
			ListView_EnsureVisible(data->hWndGlyphList, data->curGlyph, FALSE);
			return;
		}

		//try again
		MessageBox(data->editorMgr->hWnd, L"Character already assigned.", L"Error", MB_ICONERROR);
	}
}

static int NftrViewerSelectFontDialog(NFTRVIEWERDATA *data) {
	//select font, using last selected font as default
	CHOOSEFONT cf = { 0 };
	cf.lStructSize = sizeof(cf);
	cf.hwndOwner = data->editorMgr->hWnd;
	cf.lpLogFont = &data->lastFont;
	cf.Flags = CF_FORCEFONTEXIST | CF_INACTIVEFONTS;
	if (data->lastFontSet) cf.Flags |= CF_INITTOLOGFONTSTRUCT;

	return ChooseFont(&cf);
}

static int NftrViewerBmpColumnHasNonWhite(COLOR32 *px, int width, int height, int x) {
	for (int y = 0; y < height; y++) {
		if ((px[x + y * width] & 0x00FFFFFF) != 0xFFFFFF) {
			return 1;
		}
	}
	return 0;
}

static void NftrViewerGenerateGlyphsFromFont(NFTRVIEWERDATA *data, NFTR_GLYPH *glyph, int nGlyph) {
	//we have a font, create it indirectly.
	HFONT hFont = CreateFontIndirect(&data->lastFont);
	if (hFont == NULL) return;
	data->lastFontSet = 1;

	//create device context
	HDC hDesktopDC = GetDC(NULL);
	HDC hDC = CreateCompatibleDC(hDesktopDC);
	ReleaseDC(NULL, hDesktopDC);
	SelectObject(hDC, hFont);

	//get text metrics
	ABC *abc = (ABC *) calloc(65536, sizeof(ABC));
	TEXTMETRIC textMetrics = { 0 };
	GetTextMetricsW(hDC, &textMetrics);
	if (!GetCharABCWidths(hDC, 0, 0xFFFF, abc)) {
		free(abc);
		abc = NULL;
	}
	DeleteDC(hDC);

	int ascent = textMetrics.tmAscent;
	int height = textMetrics.tmHeight;
	int maxWidth = textMetrics.tmMaxCharWidth;

	//create glyph frame buffer
	FrameBuffer fb;
	FbCreate(&fb, NULL, maxWidth + 3, height); // +1 on the left, +2 on right for rounding
	SelectObject(fb.hDC, hFont);

	for (int i = 0; i < nGlyph; i++) {
		//render white background
		memset(fb.px, 0xFF, fb.width * fb.height * sizeof(COLOR32));
		int cp = glyph->cp;

		//render glyph code point
		WCHAR str[2];
		RECT rcText = { 0 };
		str[0] = glyph->cp;
		str[1] = L'\0';
		rcText.left = 1; // 1-px padding on left to account for rounding
		rcText.right = rcText.left + maxWidth;
		rcText.bottom = height;
		if (abc != NULL) {
			//adjust based on A
			rcText.left -= abc[cp].abcA;
			rcText.right -= abc[cp].abcA;
		}
		DrawText(fb.hDC, str, 1, &rcText, DT_SINGLELINE | DT_NOPREFIX);

		//clear glyph
		memset(glyph->px, 0, data->nftr->cellWidth * data->nftr->cellHeight);

		//calculate glyph bounding horizontal
		int offsX = 0, maxX = fb.width;
		for (int x = 0; x < fb.width; x++) {
			if (NftrViewerBmpColumnHasNonWhite(fb.px, fb.width, fb.height, x)) break;
			offsX++;
		}
		for (int x = fb.width - 1; x >= 0; x--) {
			if (NftrViewerBmpColumnHasNonWhite(fb.px, fb.width, fb.height, x)) break;
			maxX = x;
		}

		//clamp max X to offset X (prevent negative B space)
		if (maxX < offsX) maxX = offsX;

		//render
		unsigned int cMax = (1 << data->nftr->bpp) - 1;
		for (int y = 0; y < data->nftr->cellHeight; y++) {
			for (int x = 0; x < data->nftr->cellWidth; x++) {
				int srcX = x + offsX;
				int srcY = y - data->nftr->pxAscent + ascent - 1;

				//sample
				if (srcX >= 0 && srcX < fb.width && srcY >= 0 && srcY < fb.height) {
					COLOR32 col = fb.px[srcX + srcY * fb.width];
					unsigned int b = (col >> 0) & 0xFF;
					unsigned int g = (col >> 8) & 0xFF;
					unsigned int r = (col >> 16) & 0xFF;
					unsigned int l = 255 - (r + b + 2 * g + 2) / 4;

					unsigned int q = (l * cMax * 2 + 255) / 510;
					glyph->px[x + y * data->nftr->cellWidth] = q;
				}
			}
		}

		//calculate width
		int width = maxX - offsX;
		int spaceA = offsX - 1; // cancel 1-pixel horizontal offset to catch A space rounding
		int spaceC = -spaceA;   // counteract A spacing from leading overhang
		if (abc != NULL) {
			//if the width is 0, then our scan is bad.
			if (width == 0) {
				spaceA = abc[cp].abcA;
				width = abc[cp].abcB;
				spaceC = abc[cp].abcC;
			} else {
				//ABC data present, set AC space
				spaceA += abc[cp].abcA;
				spaceC += abc[cp].abcC;
			}

			//if B space exceeds glyph width, adjust C space
			int spaceAB = spaceA + width;
			if (spaceAB != (abc[cp].abcA + abc[cp].abcB)) {
				//adjust C space based on difference in AB spaces to preserve total spacing
				spaceC += abc[cp].abcB - spaceAB;
			}
		} else {
			//no ABC data present
			if (width == 0) {
				//set space width to width of average character
				width = textMetrics.tmAveCharWidth;
			}
		}

		//add negative C space depending on the preview spacing.
		spaceC -= data->spaceX;

		if (width == 0) {
			//if width is 0, then all of the spacing is meaningless except the total spacing. We'll
			//push all the spacing to the A space for maximum compatibility.
			spaceA += spaceC;
			spaceC = 0;
		} else if (width > data->nftr->cellWidth) {
			//clamp width to cell size, adding excess to C space to preserve total width
			spaceC += (width - data->nftr->cellWidth);
			width = data->nftr->cellWidth;
		}

		glyph->spaceLeft = spaceA;
		glyph->spaceRight = spaceC;
		glyph->width = width;
		NftrViewerCacheInvalidateGlyphByCP(data, glyph->cp);
		glyph++;
	}

	if (abc != NULL) free(abc);
	FbDestroy(&fb);
	DeleteObject(hFont);

	NftrViewerFontUpdated(data);
	InvalidateRect(data->hWndGlyphList, NULL, TRUE);
}

static void NftrViewerGenerateGlyph(NFTRVIEWERDATA *data) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	if (!NftrViewerSelectFontDialog(data)) return;

	//font fix-up
	if (data->nftr->bpp == 1) {
		data->lastFont.lfQuality = NONANTIALIASED_QUALITY;
	}

	NftrViewerGenerateGlyphsFromFont(data, glyph, 1);
	NftrViewerSetCurrentGlyphByCodePoint(data, glyph->cp, FALSE);
	InvalidateRect(data->hWndGlyphList, NULL, TRUE);
}

static void NftrViewerGenerateGlyphsForWholeFont(NFTRVIEWERDATA *data) {
	if (!NftrViewerSelectFontDialog(data)) return;

	//font fix-up
	if (data->nftr->bpp == 1) {
		data->lastFont.lfQuality = NONANTIALIASED_QUALITY;
	}

	NftrViewerGenerateGlyphsFromFont(data, data->nftr->glyphs, data->nftr->nGlyph);
	NftrViewerCacheInvalidateAll(data);
	InvalidateRect(data->hWndGlyphList, NULL, TRUE);
	
	NFTR_GLYPH *cur = NftrViewerGetCurrentGlyph(data);
	if (cur != NULL) {
		NftrViewerSetCurrentGlyphByCodePoint(data, cur->cp, FALSE);
	}
}

static void NftrViewerGenerateGlyphRange(NFTRVIEWERDATA *data) {
	if (!NftrViewerSelectFontDialog(data)) return;

	//get range
	uint16_t defCP = 0;
	int inputCP1 = NftrViewerPromptCharacter(data, L"Enter First Character", L"Enter a character or code point:", defCP);
	if (inputCP1 == -1) return; // exit

	int inputCP2 = NftrViewerPromptCharacter(data, L"Enter Last Character", L"Enter a character or code point:", defCP);
	if (inputCP2 == -1) return; // exit

	//check bounds (swap)
	if (inputCP1 > inputCP2) {
		int temp = inputCP1;
		inputCP1 = inputCP2;
		inputCP2 = temp;
	}

	//find first glyph with CP >= inputCP1
	int glyphStart = -1, nGlyph = 0;
	for (int i = 0; i < data->nftr->nGlyph; i++) {

		//starting glyph
		if (glyphStart == -1 && data->nftr->glyphs[i].cp >= (uint16_t) inputCP1) {
			glyphStart = i;
		}

		//increase count
		if (glyphStart != -1 && data->nftr->glyphs[i].cp <= (uint16_t) inputCP2) {
			nGlyph++;
		}
	}

	if (glyphStart != -1) {
		NftrViewerGenerateGlyphsFromFont(data, &data->nftr->glyphs[glyphStart], nGlyph);
		NftrViewerCacheInvalidateAll(data);
		InvalidateRect(data->hWndGlyphList, NULL, TRUE);

		NFTR_GLYPH *cur = NftrViewerGetCurrentGlyph(data);
		if (cur != NULL) {
			NftrViewerSetCurrentGlyphByCodePoint(data, cur->cp, FALSE);
		}
	}
}

static void NftrViewerNewGlyphRange(NFTRVIEWERDATA *data) {
	uint16_t defCP = 0;
	
	int inputCP1 = NftrViewerPromptCharacter(data, L"Enter First Character", L"Enter a character or code point:", defCP);
	if (inputCP1 == -1) return; // exit
	
	int inputCP2 = NftrViewerPromptCharacter(data, L"Enter Last Character", L"Enter a character or code point:", defCP);
	if (inputCP2 == -1) return; // exit

	//check bounds (swap)
	if (inputCP1 > inputCP2) {
		int temp = inputCP1;
		inputCP1 = inputCP2;
		inputCP2 = temp;
	}

	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);

	//add glyphs in range
	int nAdd = 0, iLastAdd = -1;
	for (int i = inputCP1; i <= inputCP2; i++) {
		//check exists
		NFTR_GLYPH *glyph = NftrViewerGetGlyphByCP(data, i);
		if (glyph != NULL) continue;

		//add
		NftrViewerCreateGlyph(data, i);
		iLastAdd = i;
		nAdd++;
	}

	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);

	if (iLastAdd != -1) {
		//show
		int newIndex = NftrViewerGetGlyphIndexByCP(data, iLastAdd);
		NftrViewerSetCurrentGlyphByCodePoint(data, iLastAdd, TRUE);
		ListView_EnsureVisible(data->hWndGlyphList, newIndex, FALSE);
	}

	//confirm add
	WCHAR buf[32];
	wsprintfW(buf, L"Added %d glyph(s).", nAdd);
	MessageBox(data->editorMgr->hWnd, buf, L"Success", MB_ICONINFORMATION);
}

static void NftrViewerExport(NFTRVIEWERDATA *data) {
	HWND hWndMain = data->editorMgr->hWnd;
	LPWSTR path = saveFileDialog(hWndMain, L"Export Font", L"PNG Files (*.png)\0*.png\0All Files\0*.*", L"png");
	if (path == NULL) return;

	//create export dialog
	HWND hWndModal = CreateWindow(L"NftrExportClass", L"Export Font", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndMain, NULL, NULL, NULL);
	SendMessage(hWndModal, NV_INITIALIZE, (WPARAM) path, (LPARAM) data);
	DoModal(hWndModal);
	free(path);
}

static unsigned int NftrViewerLcFontGetPitch(const unsigned char *buf, unsigned int size) {
	//iterate over factors of the size, greater than 2
	for (unsigned int i = 0; i < size; i++) {
		if (i <= 2 || (i & 1)) continue; // must have space for code point, even size
		if (size % i) continue; // not a factor of the size

		//check the code points (code points must be strictly ascending)
		uint16_t cpLast = 0;
		unsigned int nGlyph = size / i;
		int invalid = 0;
		for (unsigned int j = 0; j < nGlyph; j++) {
			const unsigned char *pGlyph = buf + j * i;
			uint16_t cp = (pGlyph[0] << 8) | (pGlyph[1]);

			if (j > 0 && cp <= cpLast) {
				invalid = 1;
				break;
			}

			cpLast = cp;
		}

		if (invalid) continue;

		//if we reach here, we have a valid size. The smaller size is more likely to be valid.
		return i;
	}
	return UINT_MAX;
}

static void NftrViewerImportLcFont(NFTRVIEWERDATA *data) {
	HWND hWndMain = data->editorMgr->hWnd;
	LPWSTR path = openFileDialog(hWndMain, L"Import LC Font", L"LC Font Files (*.dat)\0*.dat\0All Files\0*.*", L"dat");
	if (path == NULL) return;

	unsigned int size;
	unsigned char *buf = ObjReadWholeFile(path, &size);
	free(path);

	if (buf == NULL) {
		MessageBox(hWndMain, L"Could not open the LC font.", L"Error", MB_ICONERROR);
		return;
	}

	//get LC font pitch
	unsigned int pitch = NftrViewerLcFontGetPitch(buf, size);
	if (pitch == UINT_MAX) {
		MessageBox(hWndMain, L"Not an LC font.", L"Error", MB_ICONERROR);
		free(buf);
		return;
	}

	//glyph size
	unsigned int glyphSize = pitch - 2;
	unsigned int nGlyph = size / pitch;

	unsigned int cellWidth = 8, cellHeight = 0;
	switch (glyphSize) {
		//HACK
		case 0x08: // f08han, f08zen
		case 0x0A: // f10han
		case 0x0C: // f12han
		case 0x10: // f16han
			cellWidth = 8;
			break;
		case 0x14: // f10zen
		case 0x18: // f12zen
		case 0x20: // f16zen
			cellWidth = 16;
			break;
	}
	cellHeight = glyphSize * 8 / cellWidth;

	//depending on the size of the current cell, expand to the smallest of the two
	unsigned int newCellWidth = cellWidth, newCellHeight = cellHeight;
	unsigned int oldCellWidth = data->nftr->cellWidth, oldCellHeight = data->nftr->cellHeight;
	if (newCellWidth < oldCellWidth) newCellWidth = oldCellWidth;
	if (newCellHeight < oldCellHeight) newCellHeight = oldCellHeight;
	NftrViewerSetCellSize(data, newCellWidth, newCellHeight);

	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);

	int mapFullToHalf = MessageBox(hWndMain, L"Map full-width glyphs to half-width?", L"Import LC Font", MB_ICONQUESTION | MB_YESNO) == IDYES;

	//add glyphs in range
	int nAdd = 0, iLastAdd = -1;
	unsigned int maxAddWidth = 0;
	for (unsigned int i = 0; i < nGlyph; i++) {
		const unsigned char *pGlyph = buf + i * pitch;
		const unsigned char *pGlyphbm = pGlyph + 2;

		uint16_t cp = (pGlyph[0] << 8) | (pGlyph[1]); // SJIS
		cp = (uint16_t) NftrViewerDecodeSjisCharacter(cp);
		if (!cp) continue;

		//map full to half width
		if (mapFullToHalf) {
			if (cp >= 0xFF01 && cp <= 0xFF5E) cp = cp - 0xFF01 + 0x21;
		}

		//check exists
		NFTR_GLYPH *glyph = NftrViewerGetGlyphByCP(data, cp);
		if (glyph != NULL) continue;

		//add
		NftrViewerCreateGlyph(data, cp);
		iLastAdd = (int) cp;
		nAdd++;

		//render
		glyph = NftrViewerGetGlyphByCP(data, cp);

		unsigned int xMin = cellWidth, xMax = 0;
		for (unsigned int y = 0; y < cellHeight; y++) {
			for (unsigned int x = 0; x < cellWidth; x++) {
				unsigned int pxno = y * cellWidth + x;
				unsigned int pxval = (pGlyphbm[pxno / 8] >> (7 - (pxno % 8))) & 1;

				if (pxval) {
					//update bounds
					if (x > xMax) xMax = x;
					if (x < xMin) xMin = x;
				}

				glyph->px[x + y * newCellWidth] = pxval;
			}
		}

		//adjust glyph to the left
		if (xMin == cellWidth) xMin = 0;
		if (xMin > 0) {
			for (unsigned int y = 0; y < cellHeight; y++) {
				for (unsigned int x = 0; x < (cellWidth - xMin); x++) {
					glyph->px[y * newCellWidth + x] = glyph->px[y * cellWidth + x + xMin];
				}
				for (unsigned int x = cellWidth - xMin; x < cellWidth; x++) {
					glyph->px[y * newCellWidth + x] = 0;
				}
			}
		}
		xMax -= xMin;

		//set bounds of glyph
		unsigned int width = xMax + 1;
		glyph->width = width;
		glyph->spaceLeft = 0;

		//track widest added glyph
		if (width > maxAddWidth) maxAddWidth = width;
	}
	free(buf);

	//if the widest added glyph is narrower than the required size, we may shrink the cell size horizontally.
	if (maxAddWidth > oldCellWidth) {
		NftrViewerSetCellSize(data, maxAddWidth, newCellHeight);
	}

	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);

	if (iLastAdd != -1) {
		//show
		int newIndex = NftrViewerGetGlyphIndexByCP(data, iLastAdd);
		NftrViewerSetCurrentGlyphByCodePoint(data, iLastAdd, TRUE);
		ListView_EnsureVisible(data->hWndGlyphList, newIndex, FALSE);
	}

	//confirm add
	WCHAR textbuf[32];
	wsprintfW(textbuf, L"Imported %d glyph(s).", nAdd);
	MessageBox(data->editorMgr->hWnd, textbuf, L"Success", MB_ICONINFORMATION);

}

static void NftrViewerOnMenuCommand(NFTRVIEWERDATA *data, int idMenu) {
	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
			InvalidateRect(data->ted.hWndViewer, NULL, FALSE);
			break;
		case ID_FILE_SAVE:
			EditorSave(data->hWnd);
			break;
		case ID_FILE_SAVEAS:
			EditorSaveAs(data->hWnd);
			break;
		case ID_FILE_EXPORT:
			NftrViewerExport(data);
			break;
		case ID_VIEW_RENDERTRANSPARENCY:
		{
			int state = GetMenuState(GetMenu(data->editorMgr->hWnd), ID_VIEW_RENDERTRANSPARENCY, MF_BYCOMMAND);
			data->renderTransparent = state;
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		}
		case ID_FONTMENU_COPYCHARACTER:
			NftrViewerCopyCurrentCharacter(data);
			break;
		case ID_FONTMENU_COPYGLYPH:
			NftrViewerCopyCurrentGlyph(data);
			break;
		case ID_FONTMENU_DELETE:
			NftrViewerDeleteCurrentGlyph(data);
			break;
		case ID_FONTMENU_REASSIGN:
			NftrViewerReassignCurrentGlyph(data);
			break;
		case ID_FONTMENU_MAKEINVALID:
			NftrViewerMakeCurrentGlyphInvalid(data);
			break;
		case ID_FONTMENU2_GOTO:
		case ID_FONTMENU_GOTO:
			NftrViewerGoTo(data);
			break;
		case ID_FONTMENU2_NEWGLYPH:
		case ID_FONTMENU_NEWGLYPH:
			NftrViewerNewGlyph(data);
			break;
		case ID_FONTMENU_GENERATE:
			NftrViewerGenerateGlyph(data);
			break;
		case ID_FONTMENU2_GENERATEALL:
		case ID_FONTMENU_GENERATEALL:
			NftrViewerGenerateGlyphsForWholeFont(data);
			break;
		case ID_FONTMENU2_NEWGLYPHRANGE:
		case ID_FONTMENU_NEWGLYPHRANGE:
			NftrViewerNewGlyphRange(data);
			break;
		case ID_FONTMENU2_GENERATEGLYPHRANGE:
		case ID_FONTMENU_GENERATEGLYPHRANGE:
			NftrViewerGenerateGlyphRange(data);
			break;
		case ID_FONTMENU2_IMPORTLCFONT:
		case ID_FONTMENU_IMPORTLCFONT:
			NftrViewerImportLcFont(data);
			break;
	}
}


// ----- UI control commands

static void NftrViewerCmdPreviewTextChanged(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//preview text updated
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	wchar_t *buf = UiEditGetText(data->hWndPreviewInput);

	if (data->previewText != NULL) {
		free(data->previewText);
	}
	data->previewText = buf;

	RECT rcPreview;
	NftrViewerCalcPosPreview(data, NULL, &rcPreview);
	InvalidateRect(data->hWnd, &rcPreview, FALSE);
}

static void NftrViewerCmdPrevSpaceXChanged(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//preview X spacing update
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	data->spaceX = GetEditNumber(hWndCtl);
	NftrViewerUpdatePreview(data);
}

static void NftrViewerCmdPrevSpaceYChanged(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//preview Y spacing update
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	data->spaceY = GetEditNumber(hWndCtl);
	NftrViewerUpdatePreview(data);
}

static void NftrViewerCmdImportCodeMap(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//import code map
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	HWND hWndMain = data->editorMgr->hWnd;

	if (data->nftr->hasCodeMap) {
		MessageBox(hWndMain, L"Font already has a code map.", L"Error", MB_ICONERROR);
		return;
	}

	LPWSTR path = openFileDialog(hWndMain, L"Select Code Map File", L"BNCMP Files (*.bncmp)\0*.bncmp\0All Files\0*.*\0", L"");
	if (path == NULL) return;

	int size;
	void *buf = ObjReadWholeFile(path, &size);
	free(path);
	if (BncmpIdentify(buf, size) == BNCMP_TYPE_INVALID) {
		free(buf);
		MessageBox(hWndMain, L"Invalid code map file.", L"Error", MB_ICONERROR);
		return;
	}

	int status = BncmpRead(data->nftr, buf, size);
	if (status != OBJ_STATUS_SUCCESS) {
		free(buf);
		MessageBox(hWndMain, L"Code map size mismatch.", L"Error", MB_ICONERROR);
		return;
	}

	//success
	free(buf);

	NftrViewerUpdateAllGlyphTextAndImage(data);
	NftrViewerFontUpdated(data);
}

static void NftrViewerCmdExportCodeMap(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//export code map
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	HWND hWndMain = data->editorMgr->hWnd;
	if (!data->nftr->hasCodeMap) {
		MessageBox(hWndMain, L"Font has no code map.", L"Error", MB_ICONERROR);
		return;
	}

	int cmapFormat = BNCMP_TYPE_INVALID;
	switch (data->nftr->header.format) {
		case NFTR_TYPE_BNFR_11:
			cmapFormat = BNCMP_TYPE_BNCMP_11; break;
		case NFTR_TYPE_BNFR_12:
			cmapFormat = BNCMP_TYPE_BNCMP_12; break;
	}

	if (cmapFormat == BNCMP_TYPE_INVALID) {
		wchar_t textbuf[64];
		const char *fmt = ObjGetFormatNameByType(FILE_TYPE_FONT, data->nftr->header.format);
		wsprintfW(textbuf, L"%S format does not use a separate code map.", fmt);
		MessageBox(hWndMain, textbuf, L"Error", MB_ICONERROR);
		return;
	}

	LPWSTR filename = saveFileDialog(hWndMain, L"Select Code Map File", L"BNCMP Files (*.bncmp)\0*.bncmp\0All Files\0*.*\0", L"");
	if (filename == NULL) return;

	BSTREAM stm;
	bstreamCreate(&stm, NULL, 0);
	BncmpWrite(data->nftr, &stm);

	DWORD dwWritten;
	HANDLE hFile = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, stm.buffer, stm.size, &dwWritten, NULL);
	CloseHandle(hFile);

	bstreamFree(&stm);
	free(filename);
}

static void NftrViewerCmdSetBitDepth(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//set bit depth
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	int sel = UiCbGetCurSel(hWndCtl);
	int depth = sel + 1;
	NftrViewerSetBitDepth(data, depth, FALSE);
}

static void NftrViewerCmdSetAscent(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change ascent
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	data->nftr->pxAscent = GetEditNumber(hWndCtl);
	NftrViewerFontUpdated(data);
}

static void NftrViewerCmdSetGlyphLeading(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change glyph leading
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	glyph->spaceLeft = GetEditNumber(hWndCtl);
	NftrViewerFontUpdated(data);
}

static void NftrViewerCmdSetGlyphWidth(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change glyph width
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	glyph->width = GetEditNumber(hWndCtl);
	NftrViewerFontUpdated(data);
}

static void NftrViewerCmdSetGlyphTrailing(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change glyph trailing
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	glyph->spaceRight = GetEditNumber(hWndCtl);
	NftrViewerFontUpdated(data);
}

static void NftrViewerCmdSetCellWidth(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change cell width
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	
	WCHAR textbuf[16];
	wsprintfW(textbuf, L"%d", data->nftr->cellWidth);
	int s = PromptUserText(data->editorMgr->hWnd, L"Input", L"Cell Width:", textbuf, sizeof(textbuf));
	if (s) {
		NftrViewerSetCellSize(data, _wtol(textbuf), data->nftr->cellHeight);
	}
}

static void NftrViewerCmdSetCellHeight(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change cell height
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	
	WCHAR textbuf[16];
	wsprintfW(textbuf, L"%d", data->nftr->cellHeight);
	int s = PromptUserText(data->editorMgr->hWnd, L"Input", L"Cell Height:", textbuf, sizeof(textbuf));
	if (s) {
		NftrViewerSetCellSize(data, data->nftr->cellWidth, _wtol(textbuf));
	}
}



static void NftrViewerOnCommand(NFTRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	UiCtlMgrOnCommand(&data->mgr, data->hWnd, wParam, lParam);
	if (lParam) {
		//
	} else if (HIWORD(wParam) == 0) {
		NftrViewerOnMenuCommand(data, LOWORD(wParam));
	} else if (HIWORD(wParam) == 1) {
		//NftrViewerOnAccelerator(data, LOWORD(wParam));
	}
}


static int NftrViewerGetPltColorByPoint(NFTRVIEWERDATA *data, int x, int y) {
	float scale = GetDpiScale();

	//if mouse is within the palette selection, change selected palette color.
	RECT rcPalette;
	NftrViewerCalcPosPalette(data, &rcPalette);

	if (x >= rcPalette.left && y >= rcPalette.top && x < rcPalette.right && y < rcPalette.bottom) {
		int btnSize = UI_SCALE_COORD(22, scale);
		int iCol = (x - rcPalette.left - UI_SCALE_COORD(50, scale));

		if (iCol >= 0) {
			iCol /= btnSize;

			int nPaletteColors = 1 << data->nftr->bpp;
			if (iCol < nPaletteColors) {
				return iCol;
			}
		}
	}
	return -1;
}

static void NftrViewerOnLButtonDown(NFTRVIEWERDATA *data) {
	POINT ptMouse;
	GetCursorPos(&ptMouse);
	ScreenToClient(data->hWnd, &ptMouse);

	int pltColor = NftrViewerGetPltColorByPoint(data, ptMouse.x, ptMouse.y);
	if (pltColor != -1) {

		if (!data->dblClickTimer || pltColor != data->dblClickElement) {
			//start double-click timer
			data->dblClickTimer = 1;
			data->dblClickElement = pltColor;
			SetTimer(data->hWnd, TIMER_DBLCLICK, TIMER_DBLCLICK_DURATION, NULL);
		} else {
			//double-click: choose color
			HWND hWndMain = data->editorMgr->hWnd;

			if (NpChooseColor15(hWndMain, hWndMain, &data->palette[pltColor])) {
				//update
				RECT rcPalette, rcPreview;
				NftrViewerCalcPosPalette(data, &rcPalette);
				NftrViewerCalcPosPreview(data, NULL, &rcPreview);
				NftrViewerCacheInvalidateAll(data);
				InvalidateRect(data->hWnd, &rcPalette, FALSE);
				InvalidateRect(data->hWnd, &rcPreview, FALSE);
				InvalidateRect(data->hWndPreview, NULL, FALSE);
				InvalidateRect(data->hWndGlyphList, NULL, FALSE);
			}
			data->dblClickTimer = 0;
		}


		RECT rcPalette;
		NftrViewerCalcPosPalette(data, &rcPalette);

		NftrViewerSetCurrentColor(data, pltColor);
		InvalidateRect(data->hWnd, &rcPalette, TRUE);
	}
}

static void NftrViewerOnTimer(NFTRVIEWERDATA *data, int id, LPARAM lParam) {
	switch (id) {
		case TIMER_DBLCLICK:
		{
			KillTimer(data->hWnd, id);
			data->dblClickTimer = 0;
			break;
		}
	}
}

static LRESULT CALLBACK NftrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
			NftrViewerOnCreate(data);
			break;
		case WM_PAINT:
			NftrViewerOnPaint(data);
			break;
		case WM_COMMAND:
			NftrViewerOnCommand(data, wParam, lParam);
			break;
		case WM_LBUTTONDOWN:
			NftrViewerOnLButtonDown(data);
			break;
		case NV_INITIALIZE:
		{
			LPCWSTR path = (LPCWSTR) wParam;
			NFTR *nftr = (NFTR *) lParam;

			if (path != NULL) EditorSetFile(hWnd, path);
			data->nftr = nftr;

			NftrViewerSetGlyphListSize(data, data->nftr->nGlyph);

			data->frameData.contentWidth = nftr->cellWidth * data->scale;
			data->frameData.contentHeight = nftr->cellHeight * data->scale;

			NftrViewerSetBitDepth(data, data->nftr->bpp, TRUE);
			data->selectedColor = (1 << nftr->bpp) - 1;
			NftrViewerSetCurrentGlyphByIndex(data, 0, TRUE);

			//populate font info
			WCHAR textbuf[16];
			wsprintfW(textbuf, L"%d", data->nftr->cellWidth);
			SendMessage(data->hWndInputCellWidth, WM_SETTEXT, -1, (LPARAM) textbuf);
			wsprintfW(textbuf, L"%d", data->nftr->cellHeight);
			SendMessage(data->hWndInputCellHeight, WM_SETTEXT, -1, (LPARAM) textbuf);
			SetEditNumber(data->hWndInputAscent, data->nftr->pxAscent);

			TedUpdateSize((EDITOR_DATA *) data, &data->ted, nftr->cellWidth, nftr->cellHeight);
			InvalidateRect(data->hWnd, NULL, FALSE);
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			TedMarginPaint(data->hWndMargin, (EDITOR_DATA *) data, &data->ted);

			RECT rcCell;
			NftrViewerCalcPosCellEditor(data, &rcCell);
			MoveWindow(data->hWndMargin, rcCell.left, rcCell.top, rcCell.right - rcCell.left + 1, rcCell.bottom - rcCell.top, TRUE);
			MoveWindow(data->hWndMargin, rcCell.left, rcCell.top, rcCell.right - rcCell.left, rcCell.bottom - rcCell.top, TRUE);

			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			break;
		}
		case NV_ZOOMUPDATED:
			SendMessage(data->ted.hWndViewer, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->ted.hWndViewer, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			TedUpdateMargins(&data->ted);
			break;
		case WM_NOTIFY:
			NftrViewerOnNotify(data, hWnd, wParam, (LPNMLISTVIEW) lParam);
			break;
		case WM_TIMER:
			NftrViewerOnTimer(data, wParam, lParam);
			break;
		case WM_SIZE:
			return NftrViewerOnSize(data, wParam, lParam);
		case WM_DESTROY:
			NftrViewerCacheFree(data);
			UiCtlMgrFree(&data->mgr);
			FbDestroy(&data->fbPreview);
			if (data->previewText != NULL) free(data->previewText);
			break;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

static void NftrViewerMarginOnLButtonUp(NFTRVIEWERDATA *data) {
	TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
	NftrViewerUpdateGlyphListImage(data, data->curGlyph);
	InvalidateRect(data->hWndGlyphList, NULL, FALSE);
	NftrViewerFontUpdated(data);
}

static LRESULT CALLBACK NftrViewerMarginWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NFTRVIEWERDATA *data = EditorGetData(hWndEditor);

	switch (msg) {
		case WM_CREATE:
		{
			data->hWndPreview = CreateWindow(NFTR_VIEWER_PREVIEW_CLASS, L"", WS_VISIBLE | WS_CHILD,
				MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, 100, 100, hWnd, NULL, NULL, NULL);

			TedInit(&data->ted, hWnd, data->hWndPreview, 1, 1);
			data->ted.allowSelection = 0;
			data->ted.tileHoverCallback = NftrViewerPreviewHoverCallback;
			data->ted.renderCallback = NftrViewerPreviewPaintCallback;
			data->ted.getCursorProc = NftrViewerGetCursorProc;
			data->ted.isSelectionModeCallback = NftrViewerIsSelectionModeCallback;
			data->ted.updateCursorCallback = NftrViewerUpdateCursorCallback;
			data->ted.suppressHighlightCallback = NftrViewerSuppressHighlightCallback;
			break;
		}
		case WM_PAINT:
			TedMarginPaint(hWnd, (EDITOR_DATA *) data, &data->ted);
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		case WM_ERASEBKGND:
			return TedMainOnEraseBkgnd((EDITOR_DATA *) data, &data->ted, wParam, lParam);
		case WM_LBUTTONUP:
			NftrViewerMarginOnLButtonUp(data);
			break;
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			TedMainOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
			break;
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			int width = rcClient.right;
			int height = rcClient.bottom;
			
			MoveWindow(data->hWndPreview, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, width - MARGIN_TOTAL_SIZE, height - MARGIN_TOTAL_SIZE, TRUE);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void CellViewerPreviewOnCreate(NFTRVIEWERDATA *data) {

}

static void NftrViewerPreviewOnLButtonDown(NFTRVIEWERDATA *data) {
	TedViewerOnLButtonDown((EDITOR_DATA *) data, &data->ted);

	//hit test
	int hit = data->ted.mouseDownHit;
	int hitType = hit & HIT_TYPE_MASK;

	//get scroll info
	int scrollX, scrollY;
	TedGetScroll(&data->ted, &scrollX, &scrollY);

	//get smouse pixel coordinate
	int pxX = -1, pxY = -1;
	if (hitType == HIT_CONTENT) {
		pxX = (data->ted.mouseX + scrollX) / data->scale;
		pxY = (data->ted.mouseY + scrollY) / data->scale;
	}

	NftrViewerPutPixel(data, pxX, pxY);
	NftrViewerUpdateGlyphListImage(data, data->curGlyph);
	InvalidateRect(data->hWndGlyphList, NULL, FALSE);

	NftrViewerFontUpdated(data);
	TedUpdateMargins(&data->ted);
}

static void NftrViewerPreviewOnLButtonUp(NFTRVIEWERDATA *data) {
	//TedReleaseCursor((EDITOR_DATA *) data, &data->ted);
}

static LRESULT CALLBACK NftrViewerCellEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndMargin = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	HWND hWndViewer = (HWND) GetWindowLongPtr(hWndMargin, GWL_HWNDPARENT);
	NFTRVIEWERDATA *data = EditorGetData(hWndViewer);
	if (GetWindowLongPtr(hWnd, 0) == (LONG_PTR) NULL) {
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}

	if (data != NULL && data->nftr != NULL) {
		data->frameData.contentWidth = data->nftr->cellWidth * data->scale;
		data->frameData.contentHeight = data->nftr->cellHeight * data->scale;
	}

	switch (msg) {
		case WM_CREATE:
			CellViewerPreviewOnCreate(data);
			break;
		case WM_PAINT:
			TedOnViewerPaint((EDITOR_DATA *) data, &data->ted);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_LBUTTONDOWN:
			NftrViewerPreviewOnLButtonDown(data);
			break;
		case WM_LBUTTONUP:
			NftrViewerMarginOnLButtonUp(data);
			break;
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			TedViewerOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
			break;
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			TedUpdateMargins(&data->ted);
			TedUpdateCursor((EDITOR_DATA *) data, &data->ted);
			return DefChildProc(hWnd, msg, wParam, lParam);
		case WM_SETCURSOR:
			return TedSetCursor((EDITOR_DATA *) data, &data->ted, wParam, lParam);
		case WM_SIZE:
		{
			if (data->nftr != NULL && ObjIsValid(&data->nftr->header)) {
				UpdateScrollbarVisibility(hWnd);

				SCROLLINFO info;
				info.cbSize = sizeof(info);
				info.nMin = 0;
				info.nMax = data->frameData.contentWidth;
				info.fMask = SIF_RANGE;
				SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

				info.nMax = data->frameData.contentHeight;
				SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			}
			return DefChildProc(hWnd, msg, wParam, lParam);
		}

	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

typedef struct NftrExportData_ {
	NFTRVIEWERDATA *data;
	HWND hWndExportMarkers;
	HWND hWndExportCodeMap;
	HWND hWndExportUnmapped;
	HWND hWndExport;
	LPWSTR path;
} NftrExportData;

static void NftrViewerGetGlyphExportPadding(NFTR_GLYPH *glyph, unsigned int *pSpaceL, unsigned int *pSpaceR) {
	unsigned int w = glyph->width;
	unsigned int lSpace = 0, rSpace = 0;

	if (glyph->spaceLeft >= 0) {
		lSpace = (unsigned int) glyph->spaceLeft;
	} else if (((unsigned int) -glyph->spaceLeft) > w) {
		rSpace = ((unsigned int) -glyph->spaceLeft) - w;
	}

	if (glyph->spaceRight >= 0) {
		if ((unsigned int) glyph->spaceRight > rSpace) rSpace = (unsigned int) glyph->spaceRight;
	} else if (((unsigned int) -glyph->spaceRight) > w) {
		if (((unsigned int) -glyph->spaceRight) - w > lSpace) lSpace = ((unsigned int) -glyph->spaceRight) - w;
	}

	//add left and right space
	lSpace++;
	rSpace++;
	*pSpaceL = lSpace;
	*pSpaceR = rSpace;
}

static COLOR32 *NftrViewerExportImage(NFTR *nftr, int markers, int map, int unmapped, int *pWidth, int *pHeight) {
	//check parameters
	if (map && !nftr->hasCodeMap) map = 0; // for a font with no code map, do not export using a code map
	if (!map) unmapped = 0;                // when not exporting with a code map, cannot export unmapped

	//cell arrangement
	unsigned int nGlyph = 65536;
	unsigned int nCellX = 256, nCellY = 256;
	if (!map) {
		//list only glyphs (no code points): arrange by nCellX=sqrt(nGlyph)
		nGlyph = nftr->nGlyph;
		nCellX = (int) sqrt((double) nGlyph);
		nCellY = (nGlyph + nCellX - 1) / nCellX;
	}

	//compute cell bounding box
	unsigned int cellW = nftr->cellWidth, cellH = nftr->cellHeight;

	//compute required width of cell to represent cell spacing
	if (markers) {
		//find min and max of left and right spacing (starting from zeros)
		unsigned int maxWidth = 2;
		for (int i = 0; i < nftr->nGlyph; i++) {
			NFTR_GLYPH *glyph = &nftr->glyphs[i];
			
			unsigned int w = glyph->width;
			unsigned int lSpace = 0, rSpace = 0;
			NftrViewerGetGlyphExportPadding(glyph, &lSpace, &rSpace);

			//add left and right space
			w += lSpace + rSpace;
			if (w > maxWidth) maxWidth = w;
		}

		cellW = maxWidth;
	}

	unsigned int fullCellW = cellW + 1;
	unsigned int fullCellH = cellH + 3; // 2-pixel border + 1-pixel marker
	if (!markers) {
		//do not include extra pixels for markers
		fullCellW = cellW;
		fullCellH = cellH;
	}

	//compute sheet size
	unsigned int imgWidth = fullCellW * nCellX;
	unsigned int imgHeight = fullCellH * nCellY;
	if (markers) {
		//right and bottom border
		imgWidth++;
		imgHeight++;
	}

	//pixel buffer
	COLOR32 *px = (COLOR32 *) calloc(imgWidth * imgHeight, sizeof(COLOR32));
	memset(px, 0xFF, imgWidth * imgHeight * sizeof(COLOR32));

	//put markers
	if (markers) {
		//horizontal grid marks
		for (unsigned int y = 0; y <= nCellY; y++) {
			for (unsigned int x = 0; x < imgWidth; x++) {
				px[(y * fullCellH) * imgWidth + x] = 0xFFFF0000; // blue
			}
		}

		//vertical grid marks
		for (unsigned int x = 0; x <= nCellX; x++) {
			for (unsigned int y = 0; y < imgHeight; y++) {
				px[y * imgWidth + (x * fullCellW)] = 0xFFFF0000; // blue
			}
		}
	}

	//invalid glyph index
	int iInvalid = NftrGetInvalidGlyphIndex(nftr);

	//put glyph images
	for (unsigned int i = 0; i < nGlyph; i++) {
		unsigned int cellX = i % nCellX;
		unsigned int cellY = i / nCellX;

		unsigned int cellPx = cellX * fullCellW;
		unsigned int cellPy = cellY * fullCellH;
		if (markers) {
			//left+top border
			cellPx++;
			cellPy++;
		}

		//draw cell bottom and baseline marker
		if (markers) {
			//width marker
			for (unsigned int x = 0; x < cellW; x++) {
				px[(cellPy + cellH + 0) * imgWidth + (cellPx + x)] = 0xFFFF0000; // blue
				px[(cellPy + cellH + 1) * imgWidth + (cellPx + x)] = 0xFF000000; // black
			}

			//baseline
			if (nftr->pxAscent < nftr->cellHeight) {
				px[(cellPy + nftr->pxAscent) * imgWidth + (cellPx - 1)] = 0xFF0000FF; // red
				px[(cellPy + nftr->pxAscent) * imgWidth + (cellPx + cellW)] = 0xFF0000FF; // red
			}
		}

		//get glyph index
		int glyphno = i;
		if (map) {
			glyphno = NftrGetGlyphIndexByCP(nftr, i);
			if (glyphno == -1) {
				if (unmapped) glyphno = iInvalid;
			}
		}
		if (glyphno < 0 || glyphno >= nftr->nGlyph) continue;

		//draw glyph
		NFTR_GLYPH *glyph = &nftr->glyphs[glyphno];
		unsigned int lSpaceMin, rSpaceMin;
		NftrViewerGetGlyphExportPadding(glyph, &lSpaceMin, &rSpaceMin);

		//adjust spacing to center glyph in cell
		unsigned int lSpace = 0, rSpace = 0;
		if (markers) {
			lSpace = (cellW - glyph->width) / 2;
			if (lSpace < lSpaceMin) lSpace = lSpaceMin;
			if ((lSpace + glyph->width + rSpaceMin) > cellW) lSpace = cellW - (glyph->width + rSpaceMin);

			rSpace = cellW - lSpace - glyph->width;
		}

		//render
		for (int y = 0; y < nftr->cellHeight; y++) {
			for (int x = 0; x < glyph->width; x++) {
				unsigned int pxval = glyph->px[x + y * nftr->cellWidth];
				pxval = 255 - (pxval * 255) / ((1 << nftr->bpp) - 1);

				COLOR32 col = pxval | (pxval << 8) | (pxval << 16) | 0xFF000000;
				px[(cellPy + y) * imgWidth + (cellPx + lSpace + x)] = col;
			}
		}

		//draw markers
		if (markers) {
			COLOR32 *markRow = &px[(cellPy + cellH + 1) * imgWidth + cellPx];

			//A space
			for (int x = 0; x < (int) (lSpace - glyph->spaceLeft); x++) markRow[x] |= 0x000000FF; // red

			//B space
			for (int x = 0; x < glyph->width; x++) markRow[lSpace + x] |= 0x0000FF00; // green

			//C space
			for (int x = lSpace + glyph->width + glyph->spaceRight; x < (int) cellW; x++) markRow[x] |= 0x00FF0000; // blue
		}
	}

	//return
	*pWidth = imgWidth;
	*pHeight = imgHeight;
	return px;
}

static LRESULT CALLBACK NftrViewerExportWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NftrExportData *data = (NftrExportData *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			data = (NftrExportData *) calloc(1, sizeof(NftrExportData));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			//UI controls
			data->hWndExportMarkers = CreateCheckbox(hWnd, L"Export Markers", 10, 10, 200, 22, TRUE);
			data->hWndExportCodeMap = CreateCheckbox(hWnd, L"Use Code Map", 10, 37, 200, 22, TRUE);
			data->hWndExportUnmapped = CreateCheckbox(hWnd, L"Export Unmapped", 10, 64, 200, 22, TRUE);
			data->hWndExport = CreateButton(hWnd, L"Export", 110, 91, 100, 22, TRUE);

			SetWindowSize(hWnd, 220, 123);
			SetGUIFont(hWnd);
			break;
		}
		case NV_INITIALIZE:
		{
			data->data = (NFTRVIEWERDATA *) lParam;
			data->path = (LPWSTR) wParam;

			//for a font without a code map, cannot export a code map.
			int hasCodeMap = data->data->nftr->hasCodeMap;
			SendMessage(data->hWndExportCodeMap, BM_SETCHECK, hasCodeMap ? BST_CHECKED : BST_UNCHECKED, 0);
			
			if (!hasCodeMap) {
				EnableWindow(data->hWndExportUnmapped, FALSE);
			}

			break;
		}
		case WM_COMMAND:
		{
			HWND hWndCtl = (HWND) lParam;
			int notif = HIWORD(wParam), idCtl = LOWORD(wParam);
			if ((hWndCtl == data->hWndExport || idCtl == IDOK) && notif == BN_CLICKED) {
				int markers = GetCheckboxChecked(data->hWndExportMarkers);
				int map = GetCheckboxChecked(data->hWndExportCodeMap);
				int unmapped = GetCheckboxChecked(data->hWndExportUnmapped);

				int width, height;
				COLOR32 *px = NftrViewerExportImage(data->data->nftr, markers, map, unmapped, &width, &height);

				//write export image
				ImgWrite(px, width, height, data->path);

				free(px);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if (hWndCtl == data->hWndExportCodeMap && notif == BN_CLICKED) {
				EnableWindow(data->hWndExportUnmapped, GetCheckboxChecked(hWndCtl));
			} else if (idCtl == IDCANCEL && notif == BN_CLICKED) {
				SendMessage(hWnd, WM_CLOSE, 0, 0);
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

void RegisterNftrViewerClass(void) {
	NftrRegisterFormats();

	int features = EDITOR_FEATURE_GRIDLINES | EDITOR_FEATURE_ZOOM;
	EDITOR_CLASS *cls = EditorRegister(NFTR_VIEWER_CLASS_NAME, NftrViewerWndProc, L"Font Editor", sizeof(NFTRVIEWERDATA), features);

	EditorAddFilter(cls, NFTR_TYPE_BNFR_11, L"bnfr", L"BNFR Files (*.bnfr)\0*.bnfr\0");
	EditorAddFilter(cls, NFTR_TYPE_BNFR_12, L"bnfr", L"BNFR Files (*.bnfr)\0*.bnfr\0");
	EditorAddFilter(cls, NFTR_TYPE_BNFR_20, L"bnfr", L"BNFR Files (*.bnfr)\0*.bnfr\0");
	EditorAddFilter(cls, NFTR_TYPE_NFTR_01, L"nftr", L"NFTR Files (*.nftr)\0*.nftr\0");
	EditorAddFilter(cls, NFTR_TYPE_NFTR_10, L"nftr", L"NFTR Files (*.nftr)\0*.nftr\0");
	EditorAddFilter(cls, NFTR_TYPE_NFTR_11, L"nftr", L"NFTR Files (*.nftr)\0*.nftr\0");
	EditorAddFilter(cls, NFTR_TYPE_NFTR_12, L"nftr", L"NFTR Files (*.nftr)\0*.nftr\0");
	EditorAddFilter(cls, NFTR_TYPE_GF_NFTR_11, L"nftr", L"NFTR Files (*.nftr)\0*.nftr\0");
	EditorAddFilter(cls, NFTR_TYPE_STARFY, L"bin", L"bin Files (*.bin)\0*.bin\0");

	RegisterGenericClass(NFTR_VIEWER_MARGIN_CLASS, NftrViewerMarginWndProc, sizeof(void *));
	RegisterGenericClass(NFTR_VIEWER_PREVIEW_CLASS, NftrViewerCellEditorWndProc, sizeof(void *));
	RegisterGenericClass(L"NftrExportClass", NftrViewerExportWndProc, sizeof(void *));
}

static HWND CreateNftrViewerInternal(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path, NFTR *nftr) {
	HWND hWndEditor = EditorCreate(NFTR_VIEWER_CLASS_NAME, x, y, width, height, hWndParent);
	SendMessage(hWndEditor, NV_INITIALIZE, (WPARAM) path, (LPARAM) nftr);
	return hWndEditor;
}

HWND CreateNftrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NFTR *nftr) {
	return CreateNftrViewerInternal(x, y, width, height, hWndParent, NULL, nftr);
}
