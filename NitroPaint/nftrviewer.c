#include <Windows.h>
#include <CommCtrl.h>

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

static COLOR32 *NftrViewerRenderSingleGlyphListPreview(NFTR *nftr, NFTR_GLYPH *glyph, COLOR32 col, COLOR32 *temp) {
	int cellWidth = nftr->cellWidth;
	int cellHeight = nftr->cellHeight;

	unsigned int alphaMax = (1 << nftr->bpp) - 1;
	for (int y = 0; y < cellHeight; y++) {
		for (int x = 0; x < cellWidth; x++) {
			unsigned char c = glyph->px[x + y * cellWidth];

			COLOR32 drawColor = col;
			unsigned int alpha = (c * 510 + alphaMax) / (alphaMax * 2);
			unsigned int drawR = (drawColor >>  0) & 0xFF;
			unsigned int drawG = (drawColor >>  8) & 0xFF;
			unsigned int drawB = (drawColor >> 16) & 0xFF;


			unsigned int r = ((alpha * drawR + (255 - alpha) * 0xFF) * 2 + 255) / 510;
			unsigned int g = ((alpha * drawG + (255 - alpha) * 0xFF) * 2 + 255) / 510;
			unsigned int b = ((alpha * drawB + (255 - alpha) * 0xFF) * 2 + 255) / 510;

			temp[x + y * cellWidth] = 0xFF000000 | (r << 16) | (g << 8) | (b << 0);
		}
	}

	//produce scaled+cropped image
	COLOR32 *scaled = ImgScaleEx(temp, cellWidth, cellHeight, PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, IMG_SCALE_FIT);
	return scaled;
}

static void NftrViewerRenderGlyphMasks(NFTR *nftr, NFTR_GLYPH *glyph, COLOR32 col, HBITMAP *pColorbm, HBITMAP *pMaskbm) {
	int cellWidth = nftr->cellWidth, cellHeight = nftr->cellHeight;

	//render each glyph to the imagelist
	COLOR32 *px = (COLOR32 *) calloc(cellWidth * cellHeight, sizeof(COLOR32));
	COLOR32 *imglist = NftrViewerRenderSingleGlyphListPreview(nftr, glyph, col, px);
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
			return ent->image;
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
	NftrViewerRenderGlyphMasks(&data->nftr, glyph, 0x00000, &hbmColor, &hbmMask);
	ImageList_Replace(himl, newImage, hbmColor, hbmMask);
	DeleteObject(hbmColor);
	DeleteObject(hbmMask);

	return newImage;
}


static void NftrViewerRenderGlyph(NFTRVIEWERDATA *data, COLOR32 *pxbuf, int width, int height, int x, int y, NFTR_GLYPH *glyph) {
	for (int cellY = 0; cellY < data->nftr.cellHeight; cellY++) {
		for (int cellX = 0; cellX < data->nftr.cellWidth; cellX++) {
			int destX = x + cellX;
			int destY = y + cellY;
			if (destX < 0 || destX >= width || destY < 0 || destY >= height) continue;

			int col = glyph->px[cellX + cellY * data->nftr.cellWidth];
			if (!col) continue;

			//put pixel
			COLOR32 c = ColorConvertFromDS(data->palette[col]);
			pxbuf[destX + destY * width] = REVERSE(c) | 0xFF000000;
		}
	}
}

static void NftrViewerRenderString(NFTRVIEWERDATA *data, COLOR32 *pxbuf, int width, int height, const wchar_t *str) {
	if (str == NULL || !data->nftr.hasCodeMap) return;

	//render glyph string
	int x = 0, y = 0;

	wchar_t c;
	int nCharsLine = 0;
	while ((c = *(str++)) != L'\0') {
		if (c == L'\r') continue; // ignore carriage returns
		if (c == L'\n') {
			//new line (TODO: text orientation)
			x = 0;
			y += data->nftr.lineHeight + data->spaceY;
			nCharsLine = 0;
			continue;
		}

		NFTR_GLYPH *glyph = NftrViewerGetGlyphByCP(data, (uint16_t) c);
		if (glyph == NULL) glyph = NftrViewerGetDefaultGlyph(data);

		if (glyph != NULL) {
			//draw
			if (nCharsLine > 0) x += glyph->spaceLeft;
			NftrViewerRenderGlyph(data, pxbuf, width, height, x, y, glyph);

			//advance position (TODO: consider text orientation)
			x += glyph->width + glyph->spaceRight + data->spaceX;
			nCharsLine++;
		}
	}
}


// ----- data manipulation routines

static NFTR_GLYPH *NftrViewerGetGlyph(NFTRVIEWERDATA *data, int i) {
	if (i < 0 || i >= data->nftr.nGlyph) return NULL;
	return &data->nftr.glyphs[i];
}

static NFTR_GLYPH *NftrViewerGetCurrentGlyph(NFTRVIEWERDATA *data) {
	return NftrViewerGetGlyph(data, data->curGlyph);
}

static int NftrViewerGetGlyphIndexByCP(NFTRVIEWERDATA *data, uint16_t cp) {
	return NftrGetGlyphIndexByCP(&data->nftr, cp);
}

static NFTR_GLYPH *NftrViewerGetGlyphByCP(NFTRVIEWERDATA *data, uint16_t cp) {
	return NftrGetGlyphByCP(&data->nftr, cp);
}

static NFTR_GLYPH *NftrViewerGetDefaultGlyph(NFTRVIEWERDATA *data) {
	for (int i = 0; i < data->nftr.nGlyph; i++) {
		if (data->nftr.glyphs[i].isInvalid) return &data->nftr.glyphs[i];
	}
	return NULL;
}

static void NftrViewerPutPixel(NFTRVIEWERDATA *data, int x, int y) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	if (x >= 0 && y >= 0 && x < data->nftr.cellWidth && y < data->nftr.cellHeight) {
		glyph->px[x + y * data->nftr.cellWidth] = data->selectedColor;
	}
}

static void NftrViewerSetCurrentColor(NFTRVIEWERDATA *data, unsigned int col) {
	unsigned int maxCol = (1 << data->nftr.bpp) - 1;
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
	if (!data->nftr.hasCodeMap) return;

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

	int n = WideCharToMultiByte(932, WC_ERR_INVALID_CHARS, bufU, -1, bufJ, 3, NULL, NULL);
	if (n < 1) return -1; // error

	//for SJIS characters with 1 byte
	if (bufJ[1] == '\0') return (unsigned char) bufJ[0];

	//for SJIS characters with 2 byte
	uint16_t jis = 0;
	jis |= ((unsigned char) bufJ[0]) << 8;
	jis |= ((unsigned char) bufJ[1]) << 0;
	return jis;
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
	HWND hWndMain = getMainWindow(data->hWnd);
	WCHAR textbuf[16];
	wsprintf(textbuf, L"%c+%04X", data->nftr.charset == FONT_CHARSET_SJIS ? 'J' : 'U', defCP);

	while (1) {
		int s = PromptUserText(hWndMain, L"Reassign Glyph", L"Enter new character or code point:", textbuf, sizeof(textbuf));
		if (!s) return -1;

		int inputCP = NftrViewerParseCharacter(textbuf, data->nftr.charset == FONT_CHARSET_SJIS);
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

	if (data->nftr.hasCodeMap) {
		if (data->nftr.charset == FONT_CHARSET_SJIS) {
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

	if (data->nftr.glyphs[i].isInvalid) {
		wsprintfW(textbuf + wcslen(textbuf), L"\n**Invalid**");
	}
}

static void NftrViewerUpdateGlyphListImage(NFTRVIEWERDATA *data, int i) {
	if (i < 0 || i >= data->nftr.nGlyph) return;

	//refresh list view
	NftrViewerCacheInvalidateGlyphByIndex(data, i);
	InvalidateRect(data->hWndGlyphList, NULL, FALSE);
}

static void NftrViewerReassignGlyph(NFTRVIEWERDATA *data, int i, uint16_t inputCP) {
	//first, set the code point on the glyph and sort the glyph list.
	data->nftr.glyphs[i].cp = inputCP;
	NftrEnsureSorted(&data->nftr);

	//next, get the new index of glyph.
	int newI = NftrViewerGetGlyphIndexByCP(data, inputCP);
	NFTR_GLYPH *glyph = &data->nftr.glyphs[newI];

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
	memmove(&data->nftr.glyphs[i], &data->nftr.glyphs[i + 1], (data->nftr.nGlyph - i - 1) * sizeof(NFTR_GLYPH));
	data->nftr.nGlyph--;
	data->nftr.glyphs = (NFTR_GLYPH *) realloc(data->nftr.glyphs, data->nftr.nGlyph * sizeof(NFTR_GLYPH));

	//reassign glyph list items
	NftrViewerFullRefreshGlyphList(data, i, data->nftr.nGlyph - 1);

	//delete last from list view
	ListView_SetItemCount(data->hWndGlyphList, data->nftr.nGlyph);

	//set selection
	int iNewSel = i;
	if (iNewSel >= data->nftr.nGlyph) iNewSel = data->nftr.nGlyph - 1;
	NftrViewerSetCurrentGlyphByIndex(data, i, TRUE);
}

static void NftrViewerCreateGlyph(NFTRVIEWERDATA *data, int cc) {
	//allocate new glyph
	data->nftr.nGlyph++;
	data->nftr.glyphs = (NFTR_GLYPH *) realloc(data->nftr.glyphs, data->nftr.nGlyph * sizeof(NFTR_GLYPH));

	NFTR_GLYPH *last = &data->nftr.glyphs[data->nftr.nGlyph - 1];
	memset(last, 0, sizeof(NFTR_GLYPH));
	last->cp = cc;
	last->px = (unsigned char *) calloc(data->nftr.cellWidth * data->nftr.cellHeight, 1);
	last->width = data->nftr.cellWidth;

	//sort
	NftrEnsureSorted(&data->nftr);

	//refresh all but the last glyph
	NFTR_GLYPH *ins = NftrViewerGetGlyphByCP(data, cc);
	NftrViewerFullRefreshGlyphList(data, ins - data->nftr.glyphs, data->nftr.nGlyph - 2);

	//add and update last item
	ListView_SetItemCount(data->hWndGlyphList, data->nftr.nGlyph);
	NftrViewerFullRefreshGlyphList(data, data->nftr.nGlyph - 1, data->nftr.nGlyph - 1);
	NftrViewerFontUpdated(data);
}

static void NftrViewerSetBitDepth(NFTRVIEWERDATA *data, int depth, BOOL setDropdown) {
	int depthOld = data->nftr.bpp;

	//update bit depth
	if (depth != depthOld) {
		NftrSetBitDepth(&data->nftr, depth);
	}

	//update color palette
	int nShade = 1 << data->nftr.bpp;
	for (int i = 0; i < nShade; i++) {
		int l = 31 - (i * 62 + nShade - 1) / (2 * (nShade - 1));
		COLOR c = l | (l << 5) | (l << 10);
		data->palette[i] = c;
	}

	//update dropdown
	if (setDropdown) {
		//set font parameters
		int bppsel = 0;
		switch (depth) {
			case 1: bppsel = 0; break;
			case 2: bppsel = 1; break;
			case 4: bppsel = 2; break;
		}
		SendMessage(data->hWndDepthList, CB_SETCURSEL, bppsel, 0);
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
		NftrViewerFullRefreshGlyphList(data, 0, data->nftr.nGlyph - 1);
		NftrViewerFontUpdated(data);
	}
}

static void NftrViewerSetCellSize(NFTRVIEWERDATA *data, int width, int height) {
	//if size is the same, do nothing
	if (width == data->nftr.cellWidth && height == data->nftr.cellHeight) return;

	//update params
	NftrSetCellSize(&data->nftr, width, height);

	//update UI
	TedUpdateSize((EDITOR_DATA *) data, &data->ted, width, height);
	NftrViewerUpdateAllGlyphTextAndImage(data);
	NftrViewerFontUpdated(data);

	//update inputs
	WCHAR textbuf[16];
	wsprintfW(textbuf, L"%d", data->nftr.cellWidth);
	SendMessage(data->hWndInputCellWidth, WM_SETTEXT, -1, (LPARAM) textbuf);
	wsprintfW(textbuf, L"%d", data->nftr.cellHeight);
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
	NFTR_GLYPH *glyph = NULL;
	if (data->curGlyph != -1 && data->curGlyph < data->nftr.nGlyph) {
		glyph = &data->nftr.glyphs[data->curGlyph];
	}

	unsigned int cmax = (1 << data->nftr.bpp) - 1;
	static const COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };

	COLOR32 drawColor = data->palette[cmax];
	unsigned int drawR = (drawColor >>  0) & 0xFF;
	unsigned int drawG = (drawColor >>  8) & 0xFF;
	unsigned int drawB = (drawColor >> 16) & 0xFF;

	for (int y = 0; y < renderHeight; y++) {
		for (int x = 0; x < renderWidth; x++) {

			int srcX = (x + scrollX) / data->scale;
			int srcY = (y + scrollY) / data->scale;

			unsigned int cidx = 0;
			if (glyph != NULL) {
				cidx = glyph->px[srcX + srcY * data->nftr.cellWidth];
			}

			COLOR32 col = 0;
			if (data->renderTransparent) {
				//render transparent: use last palette color and alpha blend to checker
				COLOR32 backCol = checker[((x ^ y) >> 2) & 1];

				unsigned int r = ((cidx * drawR) + (cmax - cidx) * ((backCol >>  0) & 0xFF)) / cmax;
				unsigned int g = ((cidx * drawG) + (cmax - cidx) * ((backCol >>  8) & 0xFF)) / cmax;
				unsigned int b = ((cidx * drawB) + (cmax - cidx) * ((backCol >> 16) & 0xFF)) / cmax;
				col = (r << 0) | (g << 8) | (b << 16);
			} else {
				//no render transparent: use color palette
				col = ColorConvertFromDS(data->palette[cidx]);
			}
			fb->px[x + y * fb->width] = REVERSE(col);
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
	HWND hWndMain = getMainWindow(hWnd);

	data->scale = 16;
	data->showBorders = 1;
	data->curGlyph = 0;
	data->renderTransparent = GetMenuState(GetMenu(hWndMain), ID_VIEW_RENDERTRANSPARENCY, MF_BYCOMMAND);
	data->spaceX = 1;
	data->spaceY = 1;

	RECT posCellEditor;
	NftrViewerCalcPosCellEditor(data, &posCellEditor);

	LPWSTR depths[] = { L"1", L"2", L"4" };

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
	SendMessage(data->hWndPreviewInput, WM_SETTEXT, 0, (LPARAM) data->previewText);

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
		memset(data->fbPreview.px, -1, data->fbPreview.width * data->fbPreview.height * sizeof(COLOR32)); // fill white

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
	unsigned int nAlpha = 1 << data->nftr.bpp;
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
				int iImage = NftrViewerCacheGetByCP(data, data->nftr.glyphs[di->item.iItem].cp);
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
	textbuf[0] = (WCHAR) glyph->cp;
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

	COLOR32 *pxbuf = (COLOR32 *) calloc(data->nftr.cellWidth * data->nftr.cellHeight, sizeof(COLOR32));
	for (int i = 0; i < data->nftr.cellWidth * data->nftr.cellHeight; i++) {
		//fill with background color
		pxbuf[i] = ColorConvertFromDS(data->palette[0]);
	}

	NftrViewerRenderGlyph(data, pxbuf, data->nftr.cellWidth, data->nftr.cellHeight, 0, 0, glyph);
	ImgSwapRedBlue(pxbuf, data->nftr.cellWidth, data->nftr.cellHeight);

	OpenClipboard(data->hWnd);
	EmptyClipboard();
	copyBitmap(pxbuf, data->nftr.cellWidth, data->nftr.cellHeight);
	CloseClipboard();
	free(pxbuf);
}

static void NftrViewerDeleteCurrentGlyph(NFTRVIEWERDATA *data) {
	NftrViewerDeleteGlyph(data, data->curGlyph);
}

static void NftrViewerReassignCurrentGlyph(NFTRVIEWERDATA *data) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	HWND hWndMain = getMainWindow(data->hWnd);
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
		MessageBox(hWndMain, L"Character already assigned.", L"Error", MB_ICONERROR);
	}
}

static void NftrViewerMakeCurrentGlyphInvalid(NFTRVIEWERDATA *data) {
	//mark all glyphs valid, except the current glyph
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 0, 0);

	for (int i = 0; i < data->nftr.nGlyph; i++) {
		int isInvalid = (i == data->curGlyph);
		if (isInvalid != data->nftr.glyphs[i].isInvalid) {
			data->nftr.glyphs[i].isInvalid = isInvalid;
		}
	}

	NftrViewerFontUpdated(data);
	InvalidateRect(data->hWndGlyphList, NULL, FALSE);
	SendMessage(data->hWndGlyphList, WM_SETREDRAW, 1, 0);
}

static void NftrViewerGoTo(NFTRVIEWERDATA *data) {
	uint16_t defCP = 0;
	HWND hWndMain = getMainWindow(data->hWnd);
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
		MessageBox(hWndMain, L"Character not assigned.", L"Error", MB_ICONERROR);
		break;
	}
}

static void NftrViewerNewGlyph(NFTRVIEWERDATA *data) {
	uint16_t defCP = 0;
	HWND hWndMain = getMainWindow(data->hWnd);
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
		MessageBox(hWndMain, L"Character already assigned.", L"Error", MB_ICONERROR);
	}
}

static int NftrViewerSelectFontDialog(NFTRVIEWERDATA *data) {
	//select font, using last selected font as default
	CHOOSEFONT cf = { 0 };
	cf.lStructSize = sizeof(cf);
	cf.hwndOwner = getMainWindow(data->hWnd);
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
		memset(glyph->px, 0, data->nftr.cellWidth * data->nftr.cellHeight);

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
		unsigned int cMax = (1 << data->nftr.bpp) - 1;
		for (int y = 0; y < data->nftr.cellHeight; y++) {
			for (int x = 0; x < data->nftr.cellWidth; x++) {
				int srcX = x + offsX;
				int srcY = y - data->nftr.pxAscent + ascent - 1;

				//sample
				if (srcX >= 0 && srcX < fb.width && srcY >= 0 && srcY < fb.height) {
					COLOR32 col = fb.px[srcX + srcY * fb.width];
					unsigned int b = (col >> 0) & 0xFF;
					unsigned int g = (col >> 8) & 0xFF;
					unsigned int r = (col >> 16) & 0xFF;
					unsigned int l = 255 - (r + b + 2 * g + 2) / 4;

					unsigned int q = (l * cMax * 2 + 255) / 510;
					glyph->px[x + y * data->nftr.cellWidth] = q;
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
		} else if (width > data->nftr.cellWidth) {
			//clamp width to cell size, adding excess to C space to preserve total width
			spaceC += (width - data->nftr.cellWidth);
			width = data->nftr.cellWidth;
		}

		glyph->spaceLeft = spaceA;
		glyph->spaceRight = spaceC;
		glyph->width = width;
		glyph++;
	}

	if (abc != NULL) free(abc);
	FbDestroy(&fb);
	DeleteObject(hFont);

	NftrViewerCacheInvalidateGlyphByCP(data, glyph->cp);
	NftrViewerFontUpdated(data);
	InvalidateRect(data->hWndGlyphList, NULL, TRUE);
}

static void NftrViewerGenerateGlyph(NFTRVIEWERDATA *data) {
	NFTR_GLYPH *glyph = NftrViewerGetCurrentGlyph(data);
	if (glyph == NULL) return;

	if (!NftrViewerSelectFontDialog(data)) return;

	//font fix-up
	if (data->nftr.bpp == 1) {
		data->lastFont.lfQuality = NONANTIALIASED_QUALITY;
	}

	NftrViewerGenerateGlyphsFromFont(data, glyph, 1);
	NftrViewerSetCurrentGlyphByCodePoint(data, glyph->cp, FALSE);
	InvalidateRect(data->hWndGlyphList, NULL, TRUE);
}

static void NftrViewerGenerateGlyphsForWholeFont(NFTRVIEWERDATA *data) {
	if (!NftrViewerSelectFontDialog(data)) return;

	//font fix-up
	if (data->nftr.bpp == 1) {
		data->lastFont.lfQuality = NONANTIALIASED_QUALITY;
	}

	NftrViewerGenerateGlyphsFromFont(data, data->nftr.glyphs, data->nftr.nGlyph);
	NftrViewerCacheInvalidateAll(data);
	InvalidateRect(data->hWndGlyphList, NULL, TRUE);
	
	NFTR_GLYPH *cur = NftrViewerGetCurrentGlyph(data);
	if (cur != NULL) {
		NftrViewerSetCurrentGlyphByCodePoint(data, cur->cp, FALSE);
	}
}

static void NftrViewerOnMenuCommand(NFTRVIEWERDATA *data, int idMenu) {
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
			EditorSave(data->hWnd);
			break;
		case ID_FILE_SAVEAS:
			EditorSaveAs(data->hWnd);
			break;
		case ID_VIEW_RENDERTRANSPARENCY:
		{
			HWND hWndMain = getMainWindow(data->hWnd);
			int state = GetMenuState(GetMenu(hWndMain), ID_VIEW_RENDERTRANSPARENCY, MF_BYCOMMAND);
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
			NftrViewerGoTo(data);
			break;
		case ID_FONTMENU2_NEWGLYPH:
			NftrViewerNewGlyph(data);
			break;
		case ID_FONTMENU_GENERATE:
			NftrViewerGenerateGlyph(data);
			break;
		case ID_FONTMENU2_GENERATEALL:
			NftrViewerGenerateGlyphsForWholeFont(data);
			break;
	}
}


// ----- UI control commands

static void NftrViewerCmdPreviewTextChanged(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//preview text updated
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	int length = SendMessage(data->hWndPreviewInput, WM_GETTEXTLENGTH, 0, 0);
	wchar_t *buf = (wchar_t *) calloc(length + 1, sizeof(wchar_t));
	SendMessage(data->hWndPreviewInput, WM_GETTEXT, length + 1, (LPARAM) buf);

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
	HWND hWndMain = getMainWindow(data->hWnd);

	if (data->nftr.hasCodeMap) {
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

	int status = BncmpRead(&data->nftr, buf, size);
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
	HWND hWndMain = getMainWindow(data->hWnd);
	if (!data->nftr.hasCodeMap) {
		MessageBox(hWndMain, L"Font has no code map.", L"Error", MB_ICONERROR);
		return;
	}

	int cmapFormat = BNCMP_TYPE_INVALID;
	switch (data->nftr.header.format) {
		case NFTR_TYPE_BNFR_11:
			cmapFormat = BNCMP_TYPE_BNCMP_11; break;
		case NFTR_TYPE_BNFR_12:
			cmapFormat = BNCMP_TYPE_BNCMP_12; break;
	}

	if (cmapFormat == BNCMP_TYPE_INVALID) {
		WCHAR textbuf[64];
		wsprintfW(textbuf, L"%s format does not use a separate code map.", fontFormatNames[data->nftr.header.format]);
		MessageBox(hWndMain, textbuf, L"Error", MB_ICONERROR);
		return;
	}

	LPWSTR filename = saveFileDialog(hWndMain, L"Select Code Map File", L"BNCMP Files (*.bncmp)\0*.bncmp\0All Files\0*.*\0", L"");
	if (filename == NULL) return;

	BSTREAM stm;
	bstreamCreate(&stm, NULL, 0);
	BncmpWrite(&data->nftr, &stm);

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
	int sel = SendMessage(hWndCtl, CB_GETCURSEL, 0, 0);
	int depth = 1 << sel;
	NftrViewerSetBitDepth(data, depth, FALSE);
}

static void NftrViewerCmdSetAscent(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change ascent
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	data->nftr.pxAscent = GetEditNumber(hWndCtl);
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
	HWND hWndMain = getMainWindow(data->hWnd);
	WCHAR textbuf[16];
	wsprintfW(textbuf, L"%d", data->nftr.cellWidth);
	int s = PromptUserText(hWndMain, L"Input", L"Cell Width:", textbuf, sizeof(textbuf));
	if (s) {
		NftrViewerSetCellSize(data, _wtol(textbuf), data->nftr.cellHeight);
	}
}

static void NftrViewerCmdSetCellHeight(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	//change cell height
	NFTRVIEWERDATA *data = (NFTRVIEWERDATA *) param;
	HWND hWndMain = getMainWindow(data->hWnd);
	WCHAR textbuf[16];
	wsprintfW(textbuf, L"%d", data->nftr.cellHeight);
	int s = PromptUserText(hWndMain, L"Input", L"Cell Height:", textbuf, sizeof(textbuf));
	if (s) {
		NftrViewerSetCellSize(data, data->nftr.cellWidth, _wtol(textbuf));
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

			int nPaletteColors = 1 << data->nftr.bpp;
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
			HWND hWndMain = getMainWindow(data->hWnd);
			COLOR orig = data->palette[pltColor];

			CHOOSECOLOR cc = { 0 };
			cc.lStructSize = sizeof(cc);
			cc.hInstance = NULL;
			cc.hwndOwner = hWndMain;
			cc.rgbResult = ColorConvertFromDS(orig);
			cc.lpCustColors = data->dlgCustomColors;
			cc.Flags = 0x103;
			BOOL (WINAPI *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
			if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;

			BOOL b = ChooseColorFunction(&cc);
			if (b) {
				//set color
				data->palette[pltColor] = ColorConvertToDS(cc.rgbResult);

				//update
				RECT rcPalette, rcPreview;
				NftrViewerCalcPosPalette(data, &rcPalette);
				NftrViewerCalcPosPreview(data, NULL, &rcPreview);
				InvalidateRect(data->hWnd, &rcPalette, FALSE);
				InvalidateRect(data->hWnd, &rcPreview, FALSE);
				InvalidateRect(data->hWndPreview, NULL, FALSE);
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
			memcpy(&data->nftr, nftr, sizeof(NFTR));

			NftrViewerSetGlyphListSize(data, data->nftr.nGlyph);

			data->frameData.contentWidth = nftr->cellWidth * data->scale;
			data->frameData.contentHeight = nftr->cellHeight * data->scale;

			NftrViewerSetBitDepth(data, data->nftr.bpp, TRUE);
			data->selectedColor = (1 << nftr->bpp) - 1;
			NftrViewerSetCurrentGlyphByIndex(data, 0, TRUE);

			//populate font info
			WCHAR textbuf[16];
			wsprintfW(textbuf, L"%d", data->nftr.cellWidth);
			SendMessage(data->hWndInputCellWidth, WM_SETTEXT, -1, (LPARAM) textbuf);
			wsprintfW(textbuf, L"%d", data->nftr.cellHeight);
			SendMessage(data->hWndInputCellHeight, WM_SETTEXT, -1, (LPARAM) textbuf);
			wsprintfW(textbuf, L"%d", data->nftr.pxAscent);
			SendMessage(data->hWndInputAscent, WM_SETTEXT, -1, (LPARAM) textbuf);

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
		case WM_LBUTTONUP:
			NftrViewerMarginOnLButtonUp(data);
			break;
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_NCMOUSELEAVE:
			TedMainOnMouseMove((EDITOR_DATA *) data, &data->ted, msg, wParam, lParam);
			break;
		case WM_ERASEBKGND:
			return 1;
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

	if (data != NULL) {
		data->frameData.contentWidth = data->nftr.cellWidth * data->scale;
		data->frameData.contentHeight = data->nftr.cellHeight * data->scale;
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
			if (ObjIsValid(&data->nftr.header)) {
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

void RegisterNftrViewerClass(void) {
	int features = EDITOR_FEATURE_GRIDLINES | EDITOR_FEATURE_ZOOM;
	EditorRegister(NFTR_VIEWER_CLASS_NAME, NftrViewerWndProc, L"Font Editor", sizeof(NFTRVIEWERDATA), features);

	RegisterGenericClass(NFTR_VIEWER_MARGIN_CLASS, NftrViewerMarginWndProc, sizeof(void *));
	RegisterGenericClass(NFTR_VIEWER_PREVIEW_CLASS, NftrViewerCellEditorWndProc, sizeof(void *));
}

HWND CreateNftrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NFTR nftr;
	if (NftrReadFile(&nftr, path)) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	HWND hWndEditor = EditorCreate(NFTR_VIEWER_CLASS_NAME, x, y, width, height, hWndParent);
	SendMessage(hWndEditor, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nftr);
	return hWndEditor;
}

HWND CreateNftrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NFTR *nftr) {
	HWND hWndEditor = EditorCreate(NFTR_VIEWER_CLASS_NAME, x, y, width, height, hWndParent);
	SendMessage(hWndEditor, NV_INITIALIZE, 0, (LPARAM) nftr);
	return hWndEditor;
}
