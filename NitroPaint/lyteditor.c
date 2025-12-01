#include "lyteditor.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "ncerviewer.h"
#include "nftrviewer.h"
#include "resource.h"


#define LYT_HIT_TYPE_MASK       0x8000 // hit type mask
#define LYT_HIT_ID_MASK         0x7FFF // hit ID mask
#define LYT_HIT_NOWHERE         0x0000 // hit type: nowhere
#define LYT_HIT_ELEM            0x8000 // hit type: layout element


static void LytEditorUnregisterFontByIndex(LYTEDITOR *data, int i);
static void LytEditorUnregisterFontByData(LYTEDITOR *data, NFTRVIEWERDATA *nftrViewerData);


static void LytEditorOnFontEditorDestroyed(EDITOR_DATA *editorData, void *param) {
	LYTEDITOR *data = (LYTEDITOR *) param;

	//unregister the font editor
	for (int i = 0; i < LYT_EDITOR_MAX_FONTS; i++) {
		if (data->registeredFontEditors[i] == (NFTRVIEWERDATA *) editorData) {
			data->registeredFontEditors[i] = NULL;
		}
	}
}

//
// Unregister the font at the specified slot.
//
static void LytEditorUnregisterFontByIndex(LYTEDITOR *data, int i) {
	NFTRVIEWERDATA *old = data->registeredFontEditors[i];
	if (old == NULL) return;

	//set entry to NULL
	data->registeredFontEditors[i] = NULL;

	//unregister the old font. An editor may be registered multiply to different slots.
	//if there is only one instance of the old editor in the registered list, we'll remove
	//the destroy callback from the list.
	int found = 0;
	for (int j = 0; j < LYT_EDITOR_MAX_FONTS; j++) {
		if (j == i) continue;

		if (data->registeredFontEditors[j] == old) {
			found = 1;
			break;
		}
	}

	//if it did not otherwise exist, remove it.
	//if (!found) {
		EditorRemoveDestroyCallback((EDITOR_DATA *) old, LytEditorOnFontEditorDestroyed, (void *) data);
	//}
}

//
// Unregister a font in any slot.
//
static void LytEditorUnregisterFontByData(LYTEDITOR *data, NFTRVIEWERDATA *nftrViewerData) {
	//remove all instances.
	for (int i = 0; i < LYT_EDITOR_MAX_FONTS; i++) {
		if (data->registeredFontEditors[i] == nftrViewerData) {
			//remove reference
			data->registeredFontEditors[i] = NULL;
		}
	}

	//remove callback
	EditorRemoveDestroyCallback((EDITOR_DATA *) nftrViewerData, LytEditorOnFontEditorDestroyed, (void *) data);
}

//
// Register a font at a given index.
//
static void LytEditorRegisterFont(LYTEDITOR *ed, NFTRVIEWERDATA *fontViewerData, int i) {
	if (i < 0 || i >= LYT_EDITOR_MAX_FONTS) return;

	//unregister the old font.
	if (ed->registeredFontEditors[i] != NULL) {
		LytEditorUnregisterFontByIndex(ed, i);
	}

	//register the new font
	ed->registeredFontEditors[i] = fontViewerData;
	EditorRegisterDestroyCallback((EDITOR_DATA *) fontViewerData, LytEditorOnFontEditorDestroyed, (void *) ed);
}

static NCLR *LytEditorGetAssociatedPalette(LYTEDITOR *data) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) ((EDITOR_DATA *) data->data)->editorMgr;
	HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
	if (hWndNclrViewer == NULL) return NULL;

	return (NCLR *) EditorGetObject(hWndNclrViewer);
}

static NCGR *LytEditorGetAssociatedCharacter(LYTEDITOR *data) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) ((EDITOR_DATA *) data->data)->editorMgr;
	HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
	if (hWndNcgrViewer == NULL) return NULL;

	return (NCGR *) EditorGetObject(hWndNcgrViewer);
}

static NCER *LytEditorGetAssociatedCellBank(LYTEDITOR *data) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) ((EDITOR_DATA *) data->data)->editorMgr;
	HWND hWndNcerViewer = nitroPaintStruct->hWndNcerViewer;
	if (hWndNcerViewer == NULL) return NULL;

	return (NCER *) EditorGetObject(hWndNcerViewer);
}

static NCER_CELL *CLytEditorGetCell(LYTEDITOR *data, int i) {
	NCER *ncer = LytEditorGetAssociatedCellBank(data);
	if (ncer == NULL) return NULL;

	if (i < 0 || i >= ncer->nCells) return NULL;
	return &ncer->cells[i];
}

static NFTR_GLYPH *GetGlyph(NFTR *nftr, wchar_t chr) {
	NFTR_GLYPH *glyph = NftrGetGlyphByCP(nftr, chr);
	if (glyph != NULL) return glyph;

	return NftrGetInvalidGlyph(nftr);
}

static int LLytEditorMeasureTextLineWidth(LYTEDITOR *ed, NFTRVIEWERDATA *nftrViewerData, int spaceX, const wchar_t **ppos) {
	//we will scan the string for either a null terminator or a line feed, and ignore carriage returns.
	const wchar_t *str = *ppos;

	//if the registered font is NULL, report placeholder statistics.
	int defCharWidth = 6; // latin average width

	int curLineWidth = 0;
	while (1) {
		wchar_t chr = *(str++);

		switch (chr) {
			case L'\n':
			case L'\0':
				//newline or terminator: advance line
				goto End;
			case L'\r':
				//ignore
				break;
			default:
			{
				//else: normal character
				int chrWidth = defCharWidth;
				if (nftrViewerData != NULL) {
					NFTR_GLYPH *glyph = GetGlyph(nftrViewerData->nftr, chr);
					if (glyph != NULL) {
						chrWidth = glyph->width + glyph->spaceLeft + glyph->spaceRight;
					}
				}

				curLineWidth += chrWidth + spaceX;
				break;
			}
		}
	}

End:
	//check at end
	if (curLineWidth > spaceX) curLineWidth -= spaceX; // BUG: incorrect logic, but replicates jnlib render logic
	*ppos = str - 1;
	return curLineWidth;
}

static void LLytEditorMeasureText(LYTEDITOR *ed, int fontID, int spaceX, int spaceY, const wchar_t *str, int *pWidth, int *pHeight) {
	NFTRVIEWERDATA *nftrViewerData = NULL;
	if (fontID >= 0 && fontID < LYT_EDITOR_MAX_FONTS) {
		nftrViewerData = ed->registeredFontEditors[fontID];
	}

	//if the registered font is NULL, report placeholder statistics.
	int lineHeight = 12;  // line height of NITRO_LC_Font_s
	int defCharWidth = 6; // latin average width
	if (nftrViewerData != NULL) {
		lineHeight = nftrViewerData->nftr->lineHeight;
	}

	//if the string is NULL, use a placeholder invalid glyph.
	if (str == NULL) {
		str = L"\xffff";
	}

	int maxLineWidth = 0, curLineWidth = 0;
	int nLine = 0;
	while (1) {
		//repeat until we reach the end of the string
		int lineWidth = LLytEditorMeasureTextLineWidth(ed, nftrViewerData, spaceX, &str);
		if (lineWidth > maxLineWidth) maxLineWidth = lineWidth;

		nLine++;
		if (*str == L'\0') break;

		str++; // advance the new-line
	}

	*pWidth = maxLineWidth;
	*pHeight = nLine * (lineHeight + spaceY) - spaceY;
}

static int LLytEditorCalcOffsetForAlignment(LYTEDITOR *ed, NFTRVIEWERDATA *nftrViewerData, const wchar_t *str, int spaceX, JLytOrigin alignX, int width) {
	//calculate line width
	int lineWidth = LLytEditorMeasureTextLineWidth(ed, nftrViewerData, spaceX, &str);

	switch (alignX) {
		case JLYT_ORIG_X_LEFT:
			return 0; // no alignemnt adjust
		case JLYT_ORIG_X_CENTER:
			//adjust by half difference
			return (width - lineWidth + 1) / 2;
		case JLYT_ORIG_X_RIGHT:
			//adjust by difference
			return width - lineWidth;
	}

	return 0;
}

//
// Get the number of elements in the associated layout file.
//
static int LytEditorGetElementCount(LYTEDITOR *ed) {
	//get the relevant count
	switch (ed->type) {
		case FILE_TYPE_BNLL: return ((BNLLEDITORDATA *) ed->data)->bnll->nMsg;
		case FILE_TYPE_BNCL: return ((BNCLEDITORDATA *) ed->data)->bncl->nCell;
		case FILE_TYPE_BNBL: return ((BNBLEDITORDATA *) ed->data)->bnbl->nRegion;
	}
	return 0;
}

//
// Get a layout element by index.
//
static void *LytEditorGetElement(LYTEDITOR *ed, int i) {
	int count = LytEditorGetElementCount(ed);
	if (i < 0 || i >= count) return NULL;

	//get the relevant element
	switch (ed->type) {
		case FILE_TYPE_BNLL: return &((BNLLEDITORDATA *) ed->data)->bnll->messages[i];
		case FILE_TYPE_BNCL: return &((BNCLEDITORDATA *) ed->data)->bncl->cells[i];
		case FILE_TYPE_BNBL: return &((BNBLEDITORDATA *) ed->data)->bnbl->regions[i];
	}
	return NULL;
}

static void LytEditorGetElementName(LYTEDITOR *ed, int i, WCHAR *textbuf) {
	LPCWSTR type = L"Element";
	switch (ed->type) {
		case FILE_TYPE_BNLL: type = L"Message"; break;
		case FILE_TYPE_BNCL: type = L"Cell"; break;
		case FILE_TYPE_BNBL: type = L"Region"; break;
	}

	wsprintfW(textbuf, L"%s %d", type, i);
}

//
// Adjust an element's position based on its alignment.
//
static void LytEditorAdjustElementPosition(int *pX, int *pY, int width, int height, JLytOrigin origX, JLytOrigin origY) {
	switch (origX) {
		case JLYT_ORIG_X_LEFT:
			break;
		case JLYT_ORIG_X_CENTER:
			*pX -= (width + 1) / 2;
			break;
		case JLYT_ORIG_X_RIGHT:
			*pX -= width;
			break;
	}
	switch (origY) {
		case JLYT_ORIG_Y_TOP:
			break;
		case JLYT_ORIG_Y_CENTER:
			*pY -= (height + 1) / 2;
			break;
		case JLYT_ORIG_Y_BOTTOM:
			*pY -= height;
			break;
	}
}

static void LytEditorGetElementSize(LYTEDITOR *data, const void *elem, int *pWidth, int *pHeight) {
	*pWidth = 32;  // PLACEHOLDER
	*pHeight = 32;

	//get the type
	switch (data->type) {
		case FILE_TYPE_BNLL:
		{
			const BnllMessage *msg = (const BnllMessage *) elem;
			LLytEditorMeasureText(data, msg->font, msg->spaceX, msg->spaceY, msg->msg, pWidth, pHeight);
			break;
		}
		case FILE_TYPE_BNCL:
		{
			const BnclCell *lytCell = (const BnclCell *) elem;
			NCER_CELL *cell = CLytEditorGetCell(data, lytCell->cell);
			if (cell != NULL) {
				//bounding info
				*pWidth = cell->maxX - cell->minX;
				*pHeight = cell->maxY - cell->minY;
			}
			break;
		}
		case FILE_TYPE_BNBL:
		{
			const BnblRegion *rgn = (const BnblRegion *) elem;
			*pWidth = rgn->width;
			*pHeight = rgn->height;
			break;
		}
	}
}

static void LytEditorGetElementBounds(LYTEDITOR *data, const void *elem, int *pX, int *pY, int *pWidth, int *pHeight) {
	const JLytPosition *pos = (const JLytPosition *) elem;
	int width, height;
	LytEditorGetElementSize(data, elem, &width, &height);

	int x = pos->x.pos;
	int y = pos->y.pos;
	LytEditorAdjustElementPosition(&x, &y, width, height, pos->x.origin, pos->y.origin);

	*pX = x;
	*pY = y;
	*pWidth = width;
	*pHeight = height;
}



static void LytEditorGetScroll(LYTEDITOR *data, int *scrollX, int *scrollY) {
	HWND hWnd = data->hWndPreview;

	//get scroll info
	SCROLLINFO scrollH = { 0 }, scrollV = { 0 };
	scrollH.cbSize = scrollV.cbSize = sizeof(scrollH);
	scrollH.fMask = scrollV.fMask = SIF_ALL;
	GetScrollInfo(hWnd, SB_HORZ, &scrollH);
	GetScrollInfo(hWnd, SB_VERT, &scrollV);

	*scrollX = scrollH.nPos;
	*scrollY = scrollV.nPos;
}

static int LytEditorHitTest(LYTEDITOR *data, int ptX, int ptY) {
	//convert client coordinates to screen coordinates
	int scrollX, scrollY;
	int scale = ((EDITOR_DATA *) data->data)->scale;
	LytEditorGetScroll(data, &scrollX, &scrollY);

	ptX = (ptX + scrollX) / scale;
	ptY = (ptY + scrollY) / scale;

	//test each element
	int nElem = LytEditorGetElementCount(data);
	for (int i = nElem - 1; i >= 0; i--) {
		void *elem = LytEditorGetElement(data, i);
		if (elem == NULL) continue;

		//draw element
		int x, y, width, height;
		LytEditorGetElementBounds(data, elem, &x, &y, &width, &height);

		if (ptX < x) continue;
		if (ptY < y) continue;
		if ((ptX - x) >= width) continue;
		if ((ptY - y) >= height) continue;

		return LYT_HIT_ELEM | i;
	}

	//nowhere
	return LYT_HIT_NOWHERE;
}

static void LytEditorUpdateContentSize(LYTEDITOR *data) {
	int scale = ((EDITOR_DATA *) data->data)->scale;
	int contentWidth = 512 * scale, contentHeight = 256 * scale;

	SCROLLINFO info;
	info.cbSize = sizeof(info);
	info.nMin = 0;
	info.nMax = contentWidth;
	info.fMask = SIF_RANGE;
	SetScrollInfo(data->hWndPreview, SB_HORZ, &info, TRUE);

	info.nMax = contentHeight;
	SetScrollInfo(data->hWndPreview, SB_VERT, &info, TRUE);
	RECT rcClient;
	GetClientRect(data->hWndPreview, &rcClient);
	SendMessage(data->hWndPreview, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
}

static LYTEDITOR *LytEditorAlloc(LYTEDITOR *ed, HWND hWnd) {
	OBJECT_HEADER *obj = EditorGetObject(hWnd);
	if (obj != NULL) {
		ed->type = obj->type;
	} else {
		ed->type = FILE_TYPE_INVALID;
	}
	ed->hWnd = hWnd;
	ed->data = EditorGetData(hWnd);
	return ed;
}

static void LytEditorFree(LYTEDITOR *ed) {
	ed->hWnd = NULL;
	ed->data = NULL;
	ed->type = FILE_TYPE_INVALID;
	FbDestroy(&ed->fb);
}

static void LLytEditorSetCurrentElement(BNLLEDITORDATA *data, const BnllMessage *msg) {
	//set UI controls
	SendMessage(data->hWndAlignX, CB_SETCURSEL, msg->alignment.x, 0);
	SendMessage(data->hWndAlignY, CB_SETCURSEL, msg->alignment.y, 0);
	SendMessage(data->hWndFontInput, CB_SETCURSEL, msg->font, 0);
	SendMessage(data->hWndPaletteInput, CB_SETCURSEL, msg->palette, 0);
	SetEditNumber(data->hWndColorInput, msg->color);
	SetEditNumber(data->hWndSpacingX, msg->spaceX);
	SetEditNumber(data->hWndSpacingY, msg->spaceY);

	//text
	if (msg->msg == NULL) {
		//uncheck message and disable input
		SendMessage(data->hWndMessageLabel, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(data->hWndMessageInput, WM_SETTEXT, 0, (LPARAM) L"");
		EnableWindow(data->hWndMessageInput, FALSE);
	} else {
		//check message and enable input
		SendMessage(data->hWndMessageLabel, BM_SETCHECK, BST_CHECKED, 0);
		SendMessage(data->hWndMessageInput, WM_SETTEXT, 0, (LPARAM) msg->msg);
		EnableWindow(data->hWndMessageInput, TRUE);
	}
	RedrawWindow(data->hWndMessageInput, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static void CLytEditorSetCurrentElement(BNCLEDITORDATA *data, const BnclCell *cell) {
	//set UI controls
	SetEditNumber(data->hWndCellInput, cell->cell);
}

static void BLytEditorSetCurrentElement(BNBLEDITORDATA *data, const BnblRegion *rgn) {
	//set UI controls
	SetEditNumber(data->hWndWidthInput, rgn->width);
	SetEditNumber(data->hWndHeightInput, rgn->height);
}

static void LytEditorUiUpdatePositionInputs(LYTEDITOR *data) {
	const void *elem = LytEditorGetElement(data, data->curElem);
	const JLytPosition *pos = (const JLytPosition *) elem;

	int updating = data->updating;
	data->updating = 1;
	SetEditNumber(data->hWndPositionX, pos->x.pos);
	SetEditNumber(data->hWndPositionY, pos->y.pos);
	data->updating = updating;
}

static void LytEditorUiUpdateOriginInputs(LYTEDITOR *data) {
	const void *elem = LytEditorGetElement(data, data->curElem);
	const JLytPosition *pos = (const JLytPosition *) elem;

	int updating = data->updating;
	data->updating = 1;
	SendMessage(data->hWndOriginXDropdown, CB_SETCURSEL, pos->x.origin, 0);
	SendMessage(data->hWndOriginYDropdown, CB_SETCURSEL, pos->y.origin, 0);
	data->updating = updating;
}

static void LytEditorSetCurrentElement(LYTEDITOR *data, int i) {
	int nMax = LytEditorGetElementCount(data);
	if (i < -1 || i >= nMax) return;

	//set
	data->curElem = i;
	InvalidateRect(data->hWndPreview, NULL, FALSE);
	/*if (i != -1) */SendMessage(data->hWndElementDropdown, CB_SETCURSEL, i, 0);

	//populate controls
	if (i != -1) {
		const void *elem = LytEditorGetElement(data, i);
		const JLytPosition *pos = (const JLytPosition *) elem;

		data->updating = 1;
		LytEditorUiUpdatePositionInputs(data);
		LytEditorUiUpdateOriginInputs(data);

		//update editor-specific controls
		switch (data->type) {
			case FILE_TYPE_BNLL: LLytEditorSetCurrentElement((BNLLEDITORDATA *) data->data, (const BnllMessage *) elem); break;
			case FILE_TYPE_BNCL: CLytEditorSetCurrentElement((BNCLEDITORDATA *) data->data, (const BnclCell *) elem); break;
			case FILE_TYPE_BNBL: BLytEditorSetCurrentElement((BNBLEDITORDATA *) data->data, (const BnblRegion *) elem); break;
		}

		data->updating = 0;
	}
}


// --- UI commands (general)

static void LytEditorOnSelectElement(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set current element
	LytEditorSetCurrentElement(ed, SendMessage(hWndCtl, CB_GETCURSEL, 0, 0));
}

static void LytEditorOnSetPositionX(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set position
	JLytPosition *ppos = LytEditorGetElement(ed, ed->curElem);
	if (ppos != NULL) {
		ppos->x.pos = GetEditNumber(hWndCtl);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LytEditorOnSetPositionY(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set position
	JLytPosition *ppos = LytEditorGetElement(ed, ed->curElem);
	if (ppos != NULL) {
		ppos->y.pos = GetEditNumber(hWndCtl);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LytEditorOnSetOriginX(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set origin
	JLytPosition *ppos = LytEditorGetElement(ed, ed->curElem);
	if (ppos != NULL) {
		ppos->x.origin = (JLytOrigin) SendMessage(hWndCtl, CB_GETCURSEL, 0, 0);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LytEditorOnSetOriginY(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set origin
	JLytPosition *ppos = LytEditorGetElement(ed, ed->curElem);
	if (ppos != NULL) {
		ppos->y.origin = (JLytOrigin) SendMessage(hWndCtl, CB_GETCURSEL, 0, 0);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LytEditorOnAddElement(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;

	//add an element
	void *pbuf = NULL;
	int elemSize = 0, nElem = LytEditorGetElementCount(ed);
	switch (ed->type) {
		case FILE_TYPE_BNLL:
			pbuf = ((BNLLEDITORDATA *) ed->data)->bnll->messages;
			elemSize = sizeof(BnllMessage);
			break;
		case FILE_TYPE_BNCL:
			pbuf = ((BNCLEDITORDATA *) ed->data)->bncl->cells;
			elemSize = sizeof(BnclCell);
			break;
		case FILE_TYPE_BNBL:
			pbuf = ((BNBLEDITORDATA *) ed->data)->bnbl->regions;
			elemSize = sizeof(BnblRegion);
			break;
	}

	int i = ed->curElem;
	if (i < 0 || i >= nElem) return;

	//insert at i+1
	pbuf = realloc(pbuf, (nElem + 1) * elemSize);
	void *p1 = (void *) (((uintptr_t) pbuf) + (i + 1) * elemSize);
	if (i < (nElem - 1)) {
		void *p2 = (void *) (((uintptr_t) pbuf) + (i + 2) * elemSize);
		memmove(p2, p1, (nElem - i - 1) * elemSize);
	}
	memset(p1, 0, elemSize);
	nElem++;

	//put back
	switch (ed->type) {
		case FILE_TYPE_BNLL:
			((BNLLEDITORDATA *) ed->data)->bnll->messages = (BnllMessage *) pbuf;
			((BNLLEDITORDATA *) ed->data)->bnll->nMsg = nElem;
			break;
		case FILE_TYPE_BNCL:
			((BNCLEDITORDATA *) ed->data)->bncl->cells = (BnclCell *) pbuf;
			((BNCLEDITORDATA *) ed->data)->bncl->nCell = nElem;
			break;
		case FILE_TYPE_BNBL:
			((BNBLEDITORDATA *) ed->data)->bnbl->regions = (BnblRegion *) pbuf;
			((BNBLEDITORDATA *) ed->data)->bnbl->nRegion = nElem;
			break;
	}
	
	//add new string
	WCHAR textbuf[32];
	LytEditorGetElementName(ed, nElem - 1, textbuf);
	SendMessage(ed->hWndElementDropdown, CB_ADDSTRING, 0, (LPARAM) textbuf);

	//select new element
	LytEditorSetCurrentElement(ed, ed->curElem + 1);
	InvalidateRect(ed->hWndPreview, NULL, FALSE);
}

static void LytEditorOnRemoveElement(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;

	//remove the current element
	void *pbuf = NULL;
	int elemSize = 0, nElem = LytEditorGetElementCount(ed);
	switch (ed->type) {
		case FILE_TYPE_BNLL:
			pbuf = ((BNLLEDITORDATA *) ed->data)->bnll->messages;
			elemSize = sizeof(BnllMessage);
			break;
		case FILE_TYPE_BNCL:
			pbuf = ((BNCLEDITORDATA *) ed->data)->bncl->cells;
			elemSize = sizeof(BnclCell);
			break;
		case FILE_TYPE_BNBL:
			pbuf = ((BNBLEDITORDATA *) ed->data)->bnbl->regions;
			elemSize = sizeof(BnblRegion);
			break;
	}

	int i = ed->curElem;
	if (i < 0 || i >= nElem) return;

	//get delete and next pointers
	void *p1 = (void *) (((uintptr_t) pbuf) + (i + 0) * elemSize);
	void *p2 = (void *) (((uintptr_t) pbuf) + (i + 1) * elemSize);
	if (ed->type == FILE_TYPE_BNLL) {
		//for BNLL: free message text if present
		BnllMessage *msg = (BnllMessage *) p1;
		if (msg->msg != NULL) free(msg->msg);
	}

	//move elements
	memmove(p1, p2, (nElem - i - 1) * elemSize);
	nElem--;
	pbuf = realloc(pbuf, nElem * elemSize);

	//put back
	switch (ed->type) {
		case FILE_TYPE_BNLL:
			((BNLLEDITORDATA *) ed->data)->bnll->messages = (BnllMessage *) pbuf;
			((BNLLEDITORDATA *) ed->data)->bnll->nMsg = nElem;
			break;
		case FILE_TYPE_BNCL:
			((BNCLEDITORDATA *) ed->data)->bncl->cells = (BnclCell *) pbuf;
			((BNCLEDITORDATA *) ed->data)->bncl->nCell = nElem;
			break;
		case FILE_TYPE_BNBL:
			((BNBLEDITORDATA *) ed->data)->bnbl->regions = (BnblRegion *) pbuf;
			((BNBLEDITORDATA *) ed->data)->bnbl->nRegion = nElem;
			break;
	}

	//delete last layout element from dropdown
	SendMessage(ed->hWndElementDropdown, CB_DELETESTRING, nElem, 0);

	//select next element
	int sel = ed->curElem;
	if (sel >= nElem) {
		sel--;
		LytEditorSetCurrentElement(ed, sel);
	}

	InvalidateRect(ed->hWndPreview, NULL, FALSE);
}



// ----- UI commands (BNLL)

static void LLytEditorOnSetAlignX(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set origin
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		pmsg->alignment.x = (JLytOrigin) SendMessage(hWndCtl, CB_GETCURSEL, 0, 0);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnSetAlignY(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set origin
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		pmsg->alignment.y = (JLytOrigin) SendMessage(hWndCtl, CB_GETCURSEL, 0, 0);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnSetSpaceX(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set spacing
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		pmsg->spaceX= GetEditNumber(hWndCtl);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnSetSpaceY(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set spacing
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		pmsg->spaceY = GetEditNumber(hWndCtl);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnSetFont(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set font
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		pmsg->font = SendMessage(hWndCtl, CB_GETCURSEL, 0, 0);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnSetPalette(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set palette
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		pmsg->palette = SendMessage(hWndCtl, CB_GETCURSEL, 0, 0);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnSetColor(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set message
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		pmsg->color = GetEditNumber(hWndCtl) & 0xFF;
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnSetMessage(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set message
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		int len = SendMessage(data->hWndMessageInput, WM_GETTEXTLENGTH, 0, 0);
		WCHAR *buf = (WCHAR *) calloc(len + 1, sizeof(WCHAR));
		SendMessage(data->hWndMessageInput, WM_GETTEXT, len + 1, (LPARAM) buf);

		//put buffer
		if (pmsg->msg != NULL) free(pmsg->msg);
		pmsg->msg = buf;
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnClickedMessageCheckbox(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;
	if (ed->updating) return;

	//set message
	BnllMessage *pmsg = (BnllMessage *) LytEditorGetElement(ed, ed->curElem);
	if (pmsg != NULL) {
		//get check state
		int checked = GetCheckboxChecked(hWndCtl);

		if (checked) {
			//checked: enable message input
			EnableWindow(data->hWndMessageInput, TRUE);

			//get message
			int len = SendMessage(data->hWndMessageInput, WM_GETTEXTLENGTH, 0, 0);
			WCHAR *buf = (WCHAR *) calloc(len + 1, sizeof(WCHAR));
			SendMessage(data->hWndMessageInput, WM_GETTEXT, len + 1, (LPARAM) buf);

			//set message
			if (pmsg->msg != NULL) free(pmsg->msg);
			pmsg->msg = buf;
		} else {
			//unchecked: disable message input
			EnableWindow(data->hWndMessageInput, FALSE);

			//free message
			if (pmsg->msg != NULL) free(pmsg->msg);
			pmsg->msg = NULL;
		}
		RedrawWindow(data->hWndMessageInput, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void LLytEditorOnClickedEditFonts(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) ed->data;

	//create modal
	HWND hWndMain = data->editorMgr->hWnd;
	HWND hWndModal = CreateWindow(L"ReferenceTargetClass", L"Register Reference Target",
		WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndMain, NULL, NULL, NULL);
	SendMessage(hWndModal, NV_INITIALIZE, 0, (LPARAM) &data->editor);
	DoModal(hWndModal);
}


// ----- UI commands (BNCL)

static void CLytEditorOnSetCell(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set cell
	BnclCell *elem = (BnclCell *) LytEditorGetElement(ed, ed->curElem);
	if (elem != NULL) {
		elem->cell = GetEditNumber(hWndCtl);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}


// ----- UI commands (BNBL)

static void BLytEditorOnSetWidth(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set origin
	BnblRegion *elem = (BnblRegion *) LytEditorGetElement(ed, ed->curElem);
	if (elem != NULL) {
		elem->width = GetEditNumber(hWndCtl);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}

static void BLytEditorOnSetHeight(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	LYTEDITOR *ed = (LYTEDITOR *) param;
	if (ed->updating) return;

	//set origin
	BnblRegion *elem = (BnblRegion *) LytEditorGetElement(ed, ed->curElem);
	if (elem != NULL) {
		elem->height = GetEditNumber(hWndCtl);
		InvalidateRect(ed->hWndPreview, NULL, FALSE);
	}
}


static void LLytEditorOnCreate(BNLLEDITORDATA *data) {
	LPCWSTR alignXs[] = { L"Left", L"Center", L"Right" };
	LPCWSTR alignYs[] = { L"Top", L"Center", L"Bottom" };
	LPCWSTR nums16[] = { L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"10", L"11", L"12", L"13", L"14", L"15" };

	data->hWndAlignmentLabel = CreateStatic(data->hWnd, L"Alignment:", 0, 0, 0, 0);
	data->hWndAlignX = CreateCombobox(data->hWnd, alignXs, 3, 0, 0, 0, 0, 0);
	data->hWndAlignY = CreateCombobox(data->hWnd, alignYs, 3, 0, 0, 0, 0, 0);
	data->hWndSpacingLabel = CreateStatic(data->hWnd, L"Spacing:", 0, 0, 0, 0);
	data->hWndSpacingX = CreateEdit(data->hWnd, L"0", 0, 0, 0, 0, FALSE);
	data->hWndSpacingY = CreateEdit(data->hWnd, L"0", 0, 0, 0, 0, FALSE);
	data->hWndFontLabel = CreateStatic(data->hWnd, L"Font:", 0, 0, 0, 0);
	data->hWndFontInput = CreateCombobox(data->hWnd, nums16, 16, 0, 0, 0, 0, 0);
	data->hWndPaletteLabel = CreateStatic(data->hWnd, L"Palette:", 0, 0, 0, 0);
	data->hWndPaletteInput = CreateCombobox(data->hWnd, nums16, 16, 0, 0, 0, 0, 0);
	data->hWndColorLabel = CreateStatic(data->hWnd, L"Color:", 0, 0, 0, 0);
	data->hWndColorInput = CreateEdit(data->hWnd, L"0", 0, 0, 0, 0, TRUE);
	data->hWndMessageLabel = CreateCheckbox(data->hWnd, L"Message:", 0, 0, 0, 0, FALSE);
	data->hWndMessageInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_WANTRETURN | ES_AUTOHSCROLL | ES_AUTOVSCROLL, 0, 0, 0, 0, data->hWnd, NULL, NULL, NULL);
	data->hWndEditFonts = CreateButton(data->hWnd, L"Register Fonts", 0, 0, 0, 0, FALSE);

	//register callbacks
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndAlignX, CBN_SELCHANGE, LLytEditorOnSetAlignX);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndAlignY, CBN_SELCHANGE, LLytEditorOnSetAlignY);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndSpacingX, EN_CHANGE, LLytEditorOnSetSpaceX);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndSpacingY, EN_CHANGE, LLytEditorOnSetSpaceY);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndFontInput, CBN_SELCHANGE, LLytEditorOnSetFont);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndPaletteInput, CBN_SELCHANGE, LLytEditorOnSetPalette);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndColorInput, EN_CHANGE, LLytEditorOnSetColor);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndMessageInput, EN_CHANGE, LLytEditorOnSetMessage);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndEditFonts, BN_CLICKED, LLytEditorOnClickedEditFonts);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndMessageLabel, BN_CLICKED, LLytEditorOnClickedMessageCheckbox);
}

static void LLytEditorOnSize(BNLLEDITORDATA *data, const RECT *rcClient) {
	int paneWidth = 210;
	int paneX = rcClient->right - paneWidth + 5;
	int paneY = 5;
	MoveWindow(data->hWndAlignmentLabel, paneX, paneY + 81, 70, 22, TRUE);
	MoveWindow(data->hWndAlignX, paneX + 70, paneY + 81, 65, 22, TRUE);
	MoveWindow(data->hWndAlignY, paneX + 135, paneY + 81, 65, 22, TRUE);
	MoveWindow(data->hWndSpacingLabel, paneX, paneY + 108, 70, 22, TRUE);
	MoveWindow(data->hWndSpacingX, paneX + 70, paneY + 108, 65, 22, TRUE);
	MoveWindow(data->hWndSpacingY, paneX + 135, paneY + 108, 65, 22, TRUE);
	MoveWindow(data->hWndFontLabel, paneX, paneY + 135, 70, 22, TRUE);
	MoveWindow(data->hWndFontInput, paneX + 70, paneY + 135, 130, 22, TRUE);
	MoveWindow(data->hWndPaletteLabel, paneX, paneY + 162, 70, 22, TRUE);
	MoveWindow(data->hWndPaletteInput, paneX + 70, paneY + 162, 130, 22, TRUE);
	MoveWindow(data->hWndColorLabel, paneX, paneY + 189, 70, 22, TRUE);
	MoveWindow(data->hWndColorInput, paneX + 70, paneY + 189, 130, 22, TRUE);
	MoveWindow(data->hWndMessageLabel, paneX, paneY + 216, 70, 22, TRUE);
	MoveWindow(data->hWndMessageInput, paneX + 70, paneY + 216, 130, 100, TRUE);
	MoveWindow(data->hWndEditFonts, paneX + 70, paneY + 321, 130, 22, TRUE);
}

static void CLytEditorOnCreate(BNCLEDITORDATA *data) {
	data->hWndCellLabel = CreateStatic(data->hWnd, L"Cell:", 0, 0, 0, 0);
	data->hWndCellInput = CreateEdit(data->hWnd, L"0", 0, 0, 0, 0, TRUE);

	//UI commands
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndCellInput, EN_CHANGE, CLytEditorOnSetCell);
}

static void CLytEditorOnSize(BNCLEDITORDATA *data, const RECT *rcClient) {
	int paneWidth = 210;
	int paneX = rcClient->right - paneWidth + 5;
	int paneY = 5;
	MoveWindow(data->hWndCellLabel, paneX, paneY + 81, 70, 22, TRUE);
	MoveWindow(data->hWndCellInput, paneX + 70, paneY + 81, 130, 22, TRUE);
}

static void BLytEditorOnCreate(BNBLEDITORDATA *data) {
	data->hWndWidthLabel = CreateStatic(data->hWnd, L"Size:", 0, 0, 0, 0);
	data->hWndWidthInput = CreateEdit(data->hWnd, L"0", 0, 0, 0, 0, TRUE);
	//data->hWndHeightLabel = CreateStatic(data->hWnd, L"Height:", 0, 0, 0, 0);
	data->hWndHeightInput = CreateEdit(data->hWnd, L"0", 0, 0, 0, 0, TRUE);

	// UI commands
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndWidthInput, EN_CHANGE, BLytEditorOnSetWidth);
	UiCtlMgrAddCommand(&data->editor.mgr, data->hWndHeightInput, EN_CHANGE, BLytEditorOnSetHeight);
}

static void BLytEditorOnSize(BNBLEDITORDATA *data, const RECT *rcClient) {
	int paneWidth = 210;
	int paneX = rcClient->right - paneWidth + 5;
	int paneY = 5;
	MoveWindow(data->hWndWidthLabel, paneX, paneY + 81, 70, 22, TRUE);
	MoveWindow(data->hWndWidthInput, paneX + 70, paneY + 81, 65, 22, TRUE);
	MoveWindow(data->hWndHeightInput, paneX + 135, paneY + 81, 65, 22, TRUE);
}

static void LytEditorOnInitialize(HWND hWnd, LYTEDITOR *ed, WPARAM wParam, LPARAM lParam) {
	LPCWSTR path = (LPCWSTR) wParam;
	OBJECT_HEADER *obj = (OBJECT_HEADER *) lParam;

	//set file name
	if (path != NULL) {
		EditorSetFile(hWnd, path);
	}

	//set object
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	data->file = obj;

	//set scale and initialize scroll parameters
	data->scale = 2;
	data->frameData.contentWidth = 512 * data->scale;
	data->frameData.contentHeight = 256 * data->scale;

	//init editor stuff
	LytEditorAlloc(ed, hWnd);
	UiCtlMgrInit(&ed->mgr, (void *) ed);

	ed->curElem = -1;

	LPCWSTR xOrigins[] = { L"Left", L"Center", L"Right" };
	LPCWSTR yOrigins[] = { L"Top", L"Center", L"Bottom" };

	//create viewer and controls
	ed->hWndPreview = CreateWindowEx(0, L"LytPreview", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
	ed->hWndElementLabel = CreateStatic(hWnd, L"Element:", 0, 0, 0, 0);
	ed->hWndElementDropdown = CreateCombobox(hWnd, NULL, 0, 0, 0, 0, 0, 0);
	ed->hWndAddElement = CreateButton(hWnd, L"+", 0, 0, 0, 0, FALSE);
	ed->hWndRemoveElement = CreateButton(hWnd, L"-", 0, 0, 0, 0, FALSE);
	ed->hWndOriginLabel = CreateStatic(hWnd, L"Origin:", 0, 0, 0, 0);
	ed->hWndOriginXDropdown = CreateCombobox(hWnd, xOrigins, 3, 0, 0, 0, 0, 0);
	ed->hWndOriginYDropdown = CreateCombobox(hWnd, yOrigins, 3, 0, 0, 0, 0, 0);
	ed->hWndPositionLabel = CreateStatic(hWnd, L"Position:", 0, 0, 0, 0);
	ed->hWndPositionX = CreateEdit(hWnd, L"0", 0, 0, 0, 0, FALSE);
	ed->hWndPositionY = CreateEdit(hWnd, L"0", 0, 0, 0, 0, FALSE);

	//create editor-specific controls
	switch (ed->type) {
		case FILE_TYPE_BNLL:
			LLytEditorOnCreate((BNLLEDITORDATA *) data);
			break;
		case FILE_TYPE_BNCL:
			CLytEditorOnCreate((BNCLEDITORDATA *) data);
			break;
		case FILE_TYPE_BNBL:
			BLytEditorOnCreate((BNBLEDITORDATA *) data);
			break;
	}

	FbCreate(&ed->fb, ed->hWndPreview, 1, 1);
	ed->bgColor = 0x00F0F0F0;

	//populate dropdown
	int nElem = LytEditorGetElementCount(ed);
	for (int i = 0; i < nElem; i++) {
		WCHAR textbuf[32];
		LytEditorGetElementName(ed, i, textbuf);
		SendMessage(ed->hWndElementDropdown, CB_ADDSTRING, 0, (LPARAM) textbuf);
	}
	LytEditorSetCurrentElement(ed, 0);

	//register callbacks
	UiCtlMgrAddCommand(&ed->mgr, ed->hWndElementDropdown, CBN_SELCHANGE, LytEditorOnSelectElement);
	UiCtlMgrAddCommand(&ed->mgr, ed->hWndPositionX, EN_CHANGE, LytEditorOnSetPositionX);
	UiCtlMgrAddCommand(&ed->mgr, ed->hWndPositionY, EN_CHANGE, LytEditorOnSetPositionY);
	UiCtlMgrAddCommand(&ed->mgr, ed->hWndOriginXDropdown, CBN_SELCHANGE, LytEditorOnSetOriginX);
	UiCtlMgrAddCommand(&ed->mgr, ed->hWndOriginYDropdown, CBN_SELCHANGE, LytEditorOnSetOriginY);
	UiCtlMgrAddCommand(&ed->mgr, ed->hWndAddElement, BN_CLICKED, LytEditorOnAddElement);
	UiCtlMgrAddCommand(&ed->mgr, ed->hWndRemoveElement, BN_CLICKED, LytEditorOnRemoveElement);
}

static void LytEditorOnSize(LYTEDITOR *data) {
	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	//position elements
	int paneWidth = 210;
	int paneX = rcClient.right - paneWidth + 5;
	int paneY = 5;
	MoveWindow(data->hWndPreview, 0, 0, rcClient.right - paneWidth, rcClient.bottom, TRUE);
	MoveWindow(data->hWndElementLabel, paneX, paneY + 0, 70, 22, TRUE);
	MoveWindow(data->hWndElementDropdown, paneX + 70, paneY + 0, 130-44, 22, TRUE);
	MoveWindow(data->hWndRemoveElement, paneX + 70 + 130 - 44, paneY + 0, 22, 22, TRUE);
	MoveWindow(data->hWndAddElement, paneX + 70 + 130 - 44 + 22, paneY + 0, 22, 22, TRUE);
	MoveWindow(data->hWndOriginLabel, paneX, paneY + 27, 70, 22, TRUE);
	MoveWindow(data->hWndOriginXDropdown, paneX + 70, paneY + 27, 65, 22, TRUE);
	MoveWindow(data->hWndOriginYDropdown, paneX + 135, paneY + 27, 65, 22, TRUE);
	MoveWindow(data->hWndPositionLabel, paneX, paneY + 54, 70, 22, TRUE);
	MoveWindow(data->hWndPositionX, paneX + 70, paneY + 54, 65, 22, TRUE);
	MoveWindow(data->hWndPositionY, paneX + 135, paneY + 54, 65, 22, TRUE);
	
	//move editor-specific controls
	switch (data->type) {
		case FILE_TYPE_BNLL:
			LLytEditorOnSize((BNLLEDITORDATA *) data->data, &rcClient);
			break;
		case FILE_TYPE_BNCL:
			CLytEditorOnSize((BNCLEDITORDATA *) data->data, &rcClient);
			break;
		case FILE_TYPE_BNBL:
			BLytEditorOnSize((BNBLEDITORDATA *) data->data, &rcClient);
			break;
	}
}

static void LytEditorOnMenuCommand(LYTEDITOR *data, int idMenu) {
	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		case ID_FILE_SAVE:
			EditorSave(data->hWnd);
			break;
		case ID_FILE_SAVEAS:
			EditorSaveAs(data->hWnd);
			break;
	}
}

static void LytEditorOnCommand(LYTEDITOR *data, WPARAM wParam, LPARAM lParam) {
	UiCtlMgrOnCommand(&data->mgr, data->hWnd, wParam, lParam);
	if (lParam) {
		//UiCtlMgrOnCommand(&data->mgr, data->hWnd, wParam, lParam);
	} else if (HIWORD(wParam) == 0) {
		LytEditorOnMenuCommand(data, LOWORD(wParam));
	} else if (HIWORD(wParam) == 1) {
		//LytEditorOnAccelerator(hWnd, LOWORD(wParam));
	}
}


static void LytEditorRenderHighContrastPixel(FrameBuffer *fb, int x, int y, int chno) {
	if (x < 0 || x >= fb->width) return;
	if (y < 0 || y >= fb->height) return;

	COLOR32 *cdest = &fb->px[x + y * fb->width];
	if ((*cdest >> 24) == 0) {
		unsigned int val = (*cdest >> (8 * chno)) & 0xFF;
		if (val < 0x80) val = 0xFF;
		else val = 0x00;

		*cdest = (*cdest & ~(0xFF << (8 * chno))) | (val << (8 * chno));
		*cdest |= 0xFF000000;
	}
}

static void LytEditorRenderDottedLineH(FrameBuffer *fb, int y, int ch, int dotted) {
	for (int x = 0; x < fb->width; x++) {
		if (((x ^ y) & 1) || !dotted) LytEditorRenderHighContrastPixel(fb, x, y, ch);
	}
}

static void LytEditorRenderDottedLineV(FrameBuffer *fb, int x, int ch, int dotted) {
	for (int y = 0; y < fb->height; y++) {
		if (((x ^ y) & 1) || !dotted) LytEditorRenderHighContrastPixel(fb, x, y, ch);
	}
}

static void LytEditorRenderSolidLineH(FrameBuffer *fb, int y, int width, COLOR32 c) {
	if (y < 0 || y >= fb->height) return;
	for (int x = 0; x < fb->width && x < width; x++) {
		fb->px[x + y * fb->width] = REVERSE(c);
	}
}

static void LytEditorRenderSolidLineV(FrameBuffer *fb, int x, int height, COLOR32 c) {
	if (x < 0 || x >= fb->width) return;
	for (int y = 0; y < fb->height && y < height; y++) {
		fb->px[x + y * fb->width] = REVERSE(c);
	}
}

static int CLytIsCellBankBNCD(NCER *ncer) {
	if (ncer == NULL) return 0;
	if (ncer->header.format != NCER_TYPE_COMBO) return 0;

	COMBO2D *combo = (COMBO2D *) ncer->header.combo;
	return combo->header.format == COMBO2D_TYPE_BNCD;
}

static void CLytEditorDrawCell(LYTEDITOR *data, NCLR *nclr, NCGR *ncgr, NCER *ncer, int scrollX, int scrollY, int scale, const BnclCell *bnclCell) {
	//draw
	int x, y, width, height;
	LytEditorGetElementBounds(data, bnclCell, &x, &y, &width, &height);

	//for a BNCL, we adjust the position by the bounding box so that the origin refers to the correct point.
	if (ncer != NULL && !CLytIsCellBankBNCD(ncer)) {
		//BNCD files use a (0,0) origin, but other formats do not. In these cases, adjust by the width.
		NCER_CELL *cell = CLytEditorGetCell(data, bnclCell->cell);
		x += width / 2;
		y += height / 2;
	}

	//render cell
	memset(data->cellbuf, 0, sizeof(data->cellbuf));
	if (ncer != NULL && bnclCell->cell < ncer->nCells) {
		CellRender(data->cellbuf, NULL, ncer, ncgr, nclr, bnclCell->cell, NULL, x - 256, y - 128, 1.0, 0.0, 0.0, 1.0, 0, 0);
	}

	//blit cell to window
	for (int y = 0; y < data->fb.height; y++) {
		int srcY = (y + scrollY) / scale;
		for (int x = 0; x < data->fb.width; x++) {
			int srcX = (x + scrollX) / scale;

			if (srcX >= 0 && srcX < 512 && srcY >= 0 && srcY < 256) {
				COLOR32 c = data->cellbuf[srcY * 512 + srcX];
				if (c >> 24) {
					data->fb.px[x + y * data->fb.width] = c;
				}
			}

		}
	}
}

static void BLytEditorDrawRegion(LYTEDITOR *data, NCLR *nclr, NCGR *ncgr, NCER *ncer, int scrollX, int scrollY, int scale, const BnblRegion *rgn) {
	int x, y, width, height;
	LytEditorGetElementBounds(data, rgn, &x, &y, &width, &height);

	COLOR32 fillColor = 0xFFFFFF;
	FbFillRect(&data->fb, x * scale - scrollX, y * scale - scrollY, width * scale, height * scale, fillColor);
}

static void LLytEditorDrawMessage(LYTEDITOR *data, NCLR *nclr, NCGR *ncgr, NCER *ncer, int scrollX, int scrollY, int scale, const BnllMessage *msg) {
	//message text
	const wchar_t *pstr = msg->msg;
	if (pstr == NULL) pstr = L"\xffff";

	//get message position
	int x, y, width, height;
	LytEditorGetElementBounds(data, msg, &x, &y, &width, &height);

	//get registered font
	NFTRVIEWERDATA *fontEditorData = data->registeredFontEditors[msg->font];
	if (fontEditorData == NULL) return; // TODO

	//check font code map
	NFTR *nftr = fontEditorData->nftr;
	if (!nftr->hasCodeMap) return;

	//start position. Initialize (X,Y) with top-left, but adjust the X for alignment.
	int curX = x, curY = y;
	curX += LLytEditorCalcOffsetForAlignment(data, fontEditorData, pstr, msg->spaceX, msg->alignment.x, width);

	//draw loop
	while (*pstr) {
		//handle character
		wchar_t cc = *(pstr++);
		if (cc == L'\n') {
			//advance line
			curX = x;
			curX += LLytEditorCalcOffsetForAlignment(data, fontEditorData, pstr, msg->spaceX, msg->alignment.x, width);
			curY += nftr->lineHeight + msg->spaceY;
		} else if (cc == L'\r') {
			//do nothing
		} else {
			NFTR_GLYPH *glyph = NftrGetGlyphByCP(nftr, cc);
			if (glyph == NULL) glyph = NftrGetInvalidGlyph(nftr);
			if (glyph == NULL) continue;

			//subtract offset horizontal
			curX += glyph->spaceLeft;

			//draw glyph at position
			for (int cellY = 0; cellY < nftr->cellHeight; cellY++) {
				for (int cellX = 0; cellX < nftr->cellWidth; cellX++) {
					unsigned char pxv = glyph->px[cellX + cellY * nftr->cellWidth];

					//check bounds
					//if ((curX + cellX) < x) continue;
					//if ((curY + cellY) < y) continue;
					//if ((curX + cellX) >= (x + width)) continue;
					//if ((curY + cellY) >= (y + height)) continue;
					if (pxv == 0) continue;

					//determine color to paint.
					COLOR32 col = 0xFFFFFFFF;
					if (nclr != NULL) {
						col = 0xFF000000;

						//compute palette color index.
						int cidx = (msg->palette << nclr->nBits) + msg->color + pxv;

						//for BNFR files, we'll subtract 1 from the index because BNFR rendering routines
						//write only one color. The NFTR rendering routines render the colors directly from
						//the bitmap.
						switch (nftr->header.format) {
							case NFTR_TYPE_BNFR_11:
							case NFTR_TYPE_BNFR_12:
							case NFTR_TYPE_BNFR_20:
								cidx--;
								break;
						}

						if (cidx < nclr->nColors) {
							col = ColorConvertFromDS(nclr->colors[cidx]);
							col = REVERSE(col);
						}
					}

					//put pixel
					for (int scaleY = 0; scaleY < scale; scaleY++) {
						int fbY = (curY + cellY) * scale - scrollY + scaleY;
						if (fbY < 0 || fbY >= data->fb.height) continue;

						for (int scaleX = 0; scaleX < scale; scaleX++) {
							int fbX = (curX + cellX) * scale - scrollX + scaleX;
							if (fbX >= 0 && fbX < data->fb.width) {
								data->fb.px[fbX + fbY * data->fb.width] = col;
							}
						}
					}
				}
			}

			//advance position
			curX += glyph->width + glyph->spaceRight + msg->spaceX;
		}
	}
}

static void LytEditorOnPaint(LYTEDITOR *data) {
	if (data == NULL) return;

	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(data->hWndPreview, &ps);

	RECT rcClient;
	GetClientRect(data->hWndPreview, &rcClient);
	FbSetSize(&data->fb, rcClient.right, rcClient.bottom);

	NCLR *nclr = LytEditorGetAssociatedPalette(data);
	NCGR *ncgr = LytEditorGetAssociatedCharacter(data);
	NCER *ncer = LytEditorGetAssociatedCellBank(data);
	COLOR32 bgColor = 0;
	if (nclr != NULL && nclr->nColors > 0) {
		bgColor = ColorConvertFromDS(nclr->colors[0]);
		bgColor = REVERSE(bgColor);
	}

	//draw background
	for (int y = 0; y < data->fb.height; y++) {
		for (int x = 0; x < data->fb.width; x++) {
			data->fb.px[y * data->fb.width + x] = bgColor;
		}
	}
	
	EDITOR_DATA *editorData = (EDITOR_DATA *) data->data;
	int scale = editorData->scale;
	int scrollX, scrollY;
	LytEditorGetScroll(data, &scrollX, &scrollY);

	if (editorData->showBorders) {
		//draw guidelines (16x16px)
		int startY = (-scrollY) % (16 * scale);
		if (startY < 0) startY += 16 * scale;
		int startX = (-scrollX) % (16 * scale);
		if (startX < 0) startX += 16 * scale;

		for (int y = 0; y < data->fb.height; y += 16) {
			LytEditorRenderDottedLineH(&data->fb, startY + y * scale, 1, 1);
		}
		for (int x = 0; x < data->fb.width; x += 16) {
			LytEditorRenderDottedLineV(&data->fb, startX + x * scale, 1, 1);
		}

		//draw guidelines (screen)
		LytEditorRenderSolidLineH(&data->fb, 192 * scale - scrollY, data->fb.width, 0xFFFF00);
		LytEditorRenderSolidLineV(&data->fb, 256 * scale - scrollX, data->fb.height, 0xFFFF00);
	}

	//draw layout elements
	int nElem = LytEditorGetElementCount(data);
	for (int i = 0; i < nElem; i++) {
		void *elem = LytEditorGetElement(data, i);
		if (elem == NULL) continue;

		//draw element
		int x, y, width, height;
		LytEditorGetElementBounds(data, elem, &x, &y, &width, &height);


		int effHit = data->hit;
		if (data->mouseDown) effHit = data->mouseDownHit;
		int effHitType = effHit & LYT_HIT_TYPE_MASK;

		//mark the currently selected element with a green border.
		COLOR32 borderColor = 0xFF0000;
		if (i == data->curElem) {
			borderColor = 0x00FF00;
		}
		
		//mark a hovered element with a yellow border.
		if (effHitType == LYT_HIT_ELEM) {
			int hitno = effHit & LYT_HIT_ID_MASK;
			if (hitno == i) {
				borderColor = 0xFFFF00;
			}
		}

		//draw element
		switch (data->type) {
			case FILE_TYPE_BNLL: LLytEditorDrawMessage(data, nclr, ncgr, ncer, scrollX, scrollY, scale, (const BnllMessage *) elem); break;
			case FILE_TYPE_BNCL: CLytEditorDrawCell(data, nclr, ncgr, ncer, scrollX, scrollY, scale, (const BnclCell *) elem); break;
			case FILE_TYPE_BNBL: BLytEditorDrawRegion(data, nclr, ncgr, ncer, scrollX, scrollY, scale, (const BnblRegion *) elem); break;
		}

		//only draw the borders when the element is not being dragged.
		if (!(data->mouseDown && effHitType == LYT_HIT_ELEM && (effHit & LYT_HIT_ID_MASK) == i && data->mouseDragged)) {
			FbDrawRect(&data->fb, x * scale - scrollX, y * scale - scrollY, width * scale, height * scale, borderColor);
		}
	}

	FbDraw(&data->fb, hDC, 0, 0, rcClient.right, rcClient.bottom, 0, 0);
	EndPaint(data->hWndPreview, &ps);
}



static LRESULT CALLBACK LytPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	EDITOR_DATA *editorData = (EDITOR_DATA *) EditorGetData(hWndEditor);

	if (frameData == NULL) {
		//get frame data pointer
		HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
		EDITOR_DATA *editorData = (EDITOR_DATA *) EditorGetData(hWndEditor);
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) editorData);
		frameData = &editorData->frameData;
	}

	if (frameData != NULL && editorData != NULL) {
		EDITOR_DATA *ed = (EDITOR_DATA *) frameData;
		frameData->contentWidth = 512 * ed->scale;
		frameData->contentHeight = 256 * ed->scale;
	}

	LYTEDITOR *data = NULL;
	if (editorData != NULL) {
		switch (editorData->file->type) {
			case FILE_TYPE_BNLL: data = &((BNLLEDITORDATA *) editorData)->editor; break;
			case FILE_TYPE_BNCL: data = &((BNCLEDITORDATA *) editorData)->editor; break;
			case FILE_TYPE_BNBL: data = &((BNBLEDITORDATA *) editorData)->editor; break;
		}
	}

	switch (msg) {
		case WM_PAINT:
			LytEditorOnPaint(data);
			break;
		case WM_LBUTTONDOWN:
		{
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);

			data->mouseX = pt.x;
			data->mouseY = pt.y;
			data->hit = LytEditorHitTest(data, data->mouseX, data->mouseY);
			data->mouseDownHit = data->hit;
			data->mouseDownX = data->mouseX;
			data->mouseDownY = data->mouseY;
			data->mouseDragged = 0;

			data->mouseDown = 1;
			data->updating = 1;
			SetFocus(hWnd);
			SetCapture(hWnd);

			//if we hit, initialize the element mouse down position
			int hitI = -1;
			if ((data->mouseDownHit & LYT_HIT_TYPE_MASK) == LYT_HIT_ELEM) {
				hitI = data->mouseDownHit & LYT_HIT_ID_MASK;
				JLytPosition *pos = LytEditorGetElement(data, hitI);
				if (pos != NULL) {
					data->dragStartX = pos->x.pos;
					data->dragStartY = pos->y.pos;
					data->curElem = hitI;
				}
			}
			LytEditorSetCurrentElement(data, hitI);

			break;
		}
		case WM_LBUTTONUP:
		{
			data->mouseDown = 0;
			data->updating = 0;
			ReleaseCapture();
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_MOUSEMOVE:
		{
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);

			int prevHit = data->hit;
			data->mouseX = pt.x;
			data->mouseY = pt.y;
			data->hit = LytEditorHitTest(data, data->mouseX, data->mouseY);

			if (prevHit != data->hit || (data->mouseDown && (data->mouseDownHit & LYT_HIT_TYPE_MASK) == LYT_HIT_ELEM)) {
				//only repaint if the hit test changes, or if we're dragging a layout element
				InvalidateRect(hWnd, NULL, FALSE);
			}

			if (data->mouseDown) {
				int hit = data->mouseDownHit;
				if ((hit & LYT_HIT_TYPE_MASK) == LYT_HIT_ELEM) {
					int elemI = hit & LYT_HIT_ID_MASK;
					JLytPosition *ppos = LytEditorGetElement(data, elemI);

					if (ppos != NULL) {
						int scale = editorData->scale;
						ppos->x.pos = (data->dragStartX * scale + (data->mouseX - data->mouseDownX)) / scale;
						ppos->y.pos = (data->dragStartY * scale + (data->mouseY - data->mouseDownY)) / scale;
						LytEditorUiUpdatePositionInputs(data);
					}
				}
				data->mouseDragged = 1;
			}

			TRACKMOUSEEVENT tme = { 0 };
			tme.cbSize = sizeof(tme);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hWnd;
			TrackMouseEvent(&tme);
			break;
		}
		case WM_MOUSELEAVE:
		{
			data->mouseX = -1;
			data->mouseY = -1;
			data->hit = LYT_HIT_NOWHERE;
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_KEYDOWN:
		{
			int sel = data->curElem;
			JLytPosition *ppos = (JLytPosition *) LytEditorGetElement(data, sel);
			if (ppos == NULL) break;

			int keycode = wParam;
			switch (keycode) {
				case VK_UP:
					ppos->y.pos--;
					break;
				case VK_DOWN:
					ppos->y.pos++;
					break;
				case VK_LEFT:
					ppos->x.pos--;
					break;
				case VK_RIGHT:
					ppos->x.pos++;
					break;
			}
			LytEditorUiUpdatePositionInputs(data);
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
		{
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
		case NV_RECALCULATE:
		{
			LytEditorUpdateContentSize(data);
			break;
		}
		case WM_SIZE:
		{
			UpdateScrollbarVisibility(hWnd);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = frameData->contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = frameData->contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}



static LRESULT CALLBACK BnllEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	BNLLEDITORDATA *data = (BNLLEDITORDATA *) EditorGetData(hWnd);
	LYTEDITOR *ed = &data->editor;
	switch (msg) {
		case WM_CREATE:
		{
			//TEST: register a font editor open
			StList fontEditorList;
			StListCreateInline(&fontEditorList, EDITOR_DATA *, NULL);
			EditorGetAllByType(data->editorMgr->hWnd, FILE_TYPE_FONT, &fontEditorList);
			if (fontEditorList.length > 0) {
				NFTRVIEWERDATA *fontEditorData = *(NFTRVIEWERDATA **) StListGetPtr(&fontEditorList, 0);
				for (int i = 0; i < LYT_EDITOR_MAX_FONTS; i++) LytEditorRegisterFont(ed, fontEditorData, i);
			}
			StListFree(&fontEditorList);

			data->showBorders = 1;
			data->scale = 2;
			break;
		}
		case WM_PAINT:
			InvalidateRect(ed->hWndPreview, NULL, FALSE);
			break;
		case NV_INITIALIZE:
			LytEditorOnInitialize(hWnd, ed, wParam, lParam);
			LytEditorOnSize(ed);
			SetGUIFont(hWnd);
			break;
		case NV_ZOOMUPDATED:
			SendMessage(ed->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(ed->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		case WM_COMMAND:
			LytEditorOnCommand(ed, wParam, lParam);
			break;
		case WM_SIZE:
			LytEditorOnSize(ed);
			break;
		case WM_DESTROY:
		{
			//unregister all font destroy callbacks
			for (int i = 0; i < LYT_EDITOR_MAX_FONTS; i++) {
				LytEditorUnregisterFontByIndex(ed, i);
			}
			LytEditorFree(&data->editor);
			UiCtlMgrFree(&ed->mgr);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK BnclEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	BNCLEDITORDATA *data = (BNCLEDITORDATA *) EditorGetData(hWnd);
	LYTEDITOR *ed = &data->editor;
	switch (msg) {
		case WM_CREATE:
			data->showBorders = 1;
			data->scale = 2;
			break;
		case WM_PAINT:
			InvalidateRect(ed->hWndPreview, NULL, FALSE);
			break;
		case NV_INITIALIZE:
			LytEditorOnInitialize(hWnd, ed, wParam, lParam);
			LytEditorOnSize(ed);
			SetGUIFont(hWnd);
			break;
		case NV_ZOOMUPDATED:
			SendMessage(ed->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(ed->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		case WM_COMMAND:
			LytEditorOnCommand(ed, wParam, lParam);
			break;
		case WM_SIZE:
			LytEditorOnSize(ed);
			break;
		case WM_DESTROY:
			LytEditorFree(&data->editor);
			UiCtlMgrFree(&ed->mgr);
			break;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK BnblEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	BNBLEDITORDATA *data = (BNBLEDITORDATA *) EditorGetData(hWnd);
	LYTEDITOR *ed = &data->editor;
	switch (msg) {
		case WM_CREATE:
			data->showBorders = 1;
			data->scale = 2;
			break;
		case WM_PAINT:
			InvalidateRect(ed->hWndPreview, NULL, FALSE);
			break;
		case NV_INITIALIZE:
			LytEditorOnInitialize(hWnd, ed, wParam, lParam);
			LytEditorOnSize(ed);
			SetGUIFont(hWnd);
			break;
		case NV_ZOOMUPDATED:
			SendMessage(ed->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(ed->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		case WM_COMMAND:
			LytEditorOnCommand(ed, wParam, lParam);
			break;
		case WM_SIZE:
			LytEditorOnSize(ed);
			break;
		case WM_DESTROY:
			LytEditorFree(&data->editor);
			UiCtlMgrFree(&ed->mgr);
			break;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

typedef struct RefTargetData_ {
	HWND hWndLabels[LYT_EDITOR_MAX_FONTS];
	HWND hWndDropdowns[LYT_EDITOR_MAX_FONTS];
	HWND hWndOK;
	HWND hWndCancel;
	LYTEDITOR *editor;
	StList dataList;
} RefTargetData;

static LRESULT CALLBACK LytReferenceTargetProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	RefTargetData *data = (RefTargetData *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			data = (RefTargetData *) calloc(sizeof(RefTargetData), 1);
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			LPCWSTR defaults[] = { L"(None)" };

			//create controls
			for (int i = 0; i < 16; i++) {
				WCHAR text[32];
				wsprintfW(text, L"Font %d:", i);
				data->hWndLabels[i] = CreateStatic(hWnd, text, 10, 10 + 27 * i, 50, 22);
				data->hWndDropdowns[i] = CreateCombobox(hWnd, defaults, 1, 10 + 50, 10 + 27 * i, 250, 100, 0);
			}
			data->hWndOK = CreateButton(hWnd, L"OK", 165, 10 + 27 * 16, 145, 22, TRUE);
			data->hWndCancel = CreateButton(hWnd, L"Cancel", 10, 10 + 27 * 16, 145, 22, FALSE);
			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 320, 15 + 27 * 17);
			break;
		}
		case NV_INITIALIZE:
		{
			//get editor
			data->editor = (LYTEDITOR *) lParam;

			//get fonts
			HWND hWndMain = ((EDITOR_DATA *) data->editor->data)->editorMgr->hWnd;
			StListCreateInline(&data->dataList, EDITOR_DATA *, NULL);
			EditorGetAllByType(hWndMain, FILE_TYPE_FONT, &data->dataList);
			int nFont = data->dataList.length;

			//get title text
			for (int i = 0; i < nFont; i++) {
				NFTRVIEWERDATA *fd = *(NFTRVIEWERDATA **) StListGetPtr(&data->dataList, i);
				
				WCHAR *path = fd->szOpenFile, *tmp;
				tmp = wcsrchr(path, L'\\');
				if (tmp != NULL) path = tmp + 1;
				tmp = wcsrchr(path, L'/');
				if (tmp != NULL) path = tmp + 1;

				for (int j = 0; j < LYT_EDITOR_MAX_FONTS; j++) {
					SendMessage(data->hWndDropdowns[j], CB_ADDSTRING, wcslen(path), (LPARAM) path);
				}
			}

			//get current linkage
			for (int i = 0; i < LYT_EDITOR_MAX_FONTS; i++) {
				NFTRVIEWERDATA *curr = data->editor->registeredFontEditors[i];
				if (curr == NULL) continue; // not set (leave default selection of none)

				//search
				size_t findJ = StListIndexOf(&data->dataList, &curr);
				SendMessage(data->hWndDropdowns[i], CB_SETCURSEL, findJ + 1, 0);
			}

			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
			int id = LOWORD(wParam);
			if ((hWndControl == data->hWndOK || id == IDOK) && notif == BN_CLICKED) {
				//set linkage from selection.

				for (int i = 0; i < LYT_EDITOR_MAX_FONTS; i++) {
					int selI = SendMessage(data->hWndDropdowns[i], CB_GETCURSEL, 0, 0);

					if (selI == 0) {
						//0: none -> unregister
						LytEditorUnregisterFontByIndex((LYTEDITOR *) data->editor, i);
					} else {
						//1: 1-based index
						NFTRVIEWERDATA *dataI = *(NFTRVIEWERDATA **) StListGetPtr(&data->dataList, selI - 1);
						LytEditorRegisterFont((LYTEDITOR *) data->editor, dataI, i);
					}
				}

				//repaint
				InvalidateRect(data->editor->hWnd, NULL, FALSE);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if ((hWndControl == data->hWndCancel || id == IDCANCEL) && notif == BN_CLICKED) {
				//close window, do not update linkage.
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_DESTROY:
		{
			StListFree(&data->dataList);
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) NULL);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static HWND LytEditorCreateInternal(LPCWSTR className, int x, int y, HWND hWndParent, LPCWSTR path, OBJECT_HEADER *obj) {
	HWND hWnd = EditorCreate(className, x, y, 0, 0, hWndParent);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) path, (LPARAM) obj);
	SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_JLAYOUT)));
	ShowWindow(hWnd, SW_SHOW);
	return hWnd;
}

static void LytEditorReleaseInvalidFile(OBJECT_HEADER *hdr, HWND hWndParent) {
	free(hdr);
	MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
}

HWND CreateBnllViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	BNLL *bnll = (BNLL *) calloc(1, sizeof(BNLL));
	if (BnllReadFile(bnll, path) == OBJ_STATUS_SUCCESS) {
		return LytEditorCreateInternal(L"BnllEditorClass", x, y, hWndParent, path, &bnll->header);
	}

	LytEditorReleaseInvalidFile(&bnll->header, hWndParent);
	return NULL;
}

HWND CreateBnclViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	BNCL *bncl = (BNCL *) calloc(1, sizeof(BNCL));
	if (BnclReadFile(bncl, path) == OBJ_STATUS_SUCCESS) {
		return LytEditorCreateInternal(L"BnclEditorClass", x, y, hWndParent, path, &bncl->header);
	}

	LytEditorReleaseInvalidFile(&bncl->header, hWndParent);
	return NULL;
}

HWND CreateBnblViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	BNBL *bnbl = (BNBL *) calloc(1, sizeof(BNBL));
	if (BnblReadFile(bnbl, path) == OBJ_STATUS_SUCCESS) {
		return LytEditorCreateInternal(L"BnblEditorClass", x, y, hWndParent, path, &bnbl->header);
	}

	LytEditorReleaseInvalidFile(&bnbl->header, hWndParent);
	return NULL;
}

HWND CreateBnllViewerImmediate(int x, int y, int width, int height, HWND hWndParent, BNLL *bnll) {
	return LytEditorCreateInternal(L"BnllEditorClass", x, y, hWndParent, NULL, &bnll->header);
}

HWND CreateBnclViewerImmediate(int x, int y, int width, int height, HWND hWndParent, BNCL *bncl) {
	return LytEditorCreateInternal(L"BnclEditorClass", x, y, hWndParent, NULL, &bncl->header);
}

HWND CreateBnblViewerImmediate(int x, int y, int width, int height, HWND hWndParent, BNBL *bnbl) {
	return LytEditorCreateInternal(L"BnblEditorClass", x, y, hWndParent, NULL, &bnbl->header);
}

void RegisterLytEditor(void) {
	int features = EDITOR_FEATURE_GRIDLINES | EDITOR_FEATURE_ZOOM;
	EDITOR_CLASS *clsBnll = EditorRegister(L"BnllEditorClass", BnllEditorWndProc, L"BNLL Editor", sizeof(BNLLEDITORDATA), features);
	EDITOR_CLASS *clsBncl = EditorRegister(L"BnclEditorClass", BnclEditorWndProc, L"BNCL Editor", sizeof(BNCLEDITORDATA), features);
	EDITOR_CLASS *clsBnbl = EditorRegister(L"BnblEditorClass", BnblEditorWndProc, L"BNBL Editor", sizeof(BNBLEDITORDATA), features);

	EditorAddFilter(clsBnll, BNLL_TYPE_BNLL, L"bnll", L"BNLL Files (*.bnll)\0*.bnll\0");
	EditorAddFilter(clsBncl, BNCL_TYPE_BNCL, L"bncl", L"BNCL Files (*.bncl)\0*.bncl\0");
	EditorAddFilter(clsBnbl, BNBL_TYPE_BNBL, L"bnbl", L"BNBL Files (*.bnbl)\0*.bnbl\0");

	RegisterGenericClass(L"LytPreview", LytPreviewWndProc, sizeof(void *));
	RegisterGenericClass(L"ReferenceTargetClass", LytReferenceTargetProc, sizeof(void *));
}
