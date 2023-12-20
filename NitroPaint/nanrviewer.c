#include <Windows.h>
#include <CommCtrl.h>
#include <math.h>

#include "nanrviewer.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "ncerviewer.h"
#include "nitropaint.h"
#include "resource.h"
#include "childwindow.h"
#include "nclr.h"
#include "ncgr.h"
#include "ncer.h"
#include "preview.h"

extern HICON g_appIcon;

#define NANRVIEWER_TIMER_TICK 1

static int getTotalFrameCount(NANR_SEQUENCE *sequence) {
	int nFrames = 0;
	for (int i = 0; i < sequence->nFrames; i++) {
		nFrames += sequence->frames[i].nFrames;
	}
	return nFrames;
}

static int getAnimationFrameFromFrame(NANR_SEQUENCE *sequence, int drawFrameIndex) {
	if (sequence == NULL) return 0;
	for (int i = 0; i < sequence->nFrames; i++) {
		drawFrameIndex -= sequence->frames[i].nFrames;
		if (drawFrameIndex < 0) return i;
	}
	return sequence->nFrames - 1;
}

static int getFrameIndex(FRAME_DATA *frame) {
	ANIM_DATA *f = (ANIM_DATA *) frame->animationData;
	return f->index;
}

static void AnmViewerFrameGetScaleRotate(FRAME_DATA *frame, int *sx, int *sy, int *rotZ, int element) {
	if (element != 1) {
		*sx = *sy = 4096;
		*rotZ = 0;
		return;
	}
	ANIM_DATA_SRT *f = (ANIM_DATA_SRT *) frame->animationData;
	*sx = f->sx;
	*sy = f->sy;
	*rotZ = f->rotZ;
}

static void AnmViewerFrameSetScaleRotate(FRAME_DATA *frame, int sx, int sy, int rotZ, int element) {
	if (element == 1) {
		ANIM_DATA_SRT *f = (ANIM_DATA_SRT *) frame->animationData;
		f->sx = sx;
		f->sy = sy;
		f->rotZ = rotZ;
	}
}

static void AnmViewerFrameGetTranslate(FRAME_DATA *frame, int *px, int *py, int element) {
	if (element == 0) {
		*px = *py = 0;
		return;
	} else if (element == 1) {
		ANIM_DATA_SRT *f = (ANIM_DATA_SRT *) frame->animationData;
		*px = f->px;
		*py = f->py;
	} else {
		ANIM_DATA_T *f = (ANIM_DATA_T *) frame->animationData;
		*px = f->px;
		*py = f->py;
	}
}

static void AnmViewerFrameSetTranslate(FRAME_DATA *frame, int px, int py, int element) {
	if (element == 1) {
		ANIM_DATA_SRT *f = (ANIM_DATA_SRT *) frame->animationData;
		f->px = px;
		f->py = py;
	} else if(element == 2) {
		ANIM_DATA_T *f = (ANIM_DATA_T *) frame->animationData;
		f->px = px;
		f->py = py;
	}
}

static int getDrawFrameIndex(NANR_SEQUENCE *sequence, int frame) {
	if (sequence == NULL || sequence->nFrames == 0) return 0;
	int drawFrameIndex = sequence->startFrameIndex + frame;
	int mode = sequence->mode;
	int nSequenceFrames = getTotalFrameCount(sequence);

	if (mode == 0) { //invalid
		drawFrameIndex = 0;
	} else if (mode == 1) { //forward
		drawFrameIndex = min(drawFrameIndex, nSequenceFrames - 1);
	} else if (mode == 2) { //forward loop
		drawFrameIndex %= nSequenceFrames;
	} else if (mode == 3) { //reverse
		if (drawFrameIndex >= nSequenceFrames) 
			drawFrameIndex = nSequenceFrames - sequence->frames[sequence->nFrames - 1].nFrames - 1 - (drawFrameIndex - nSequenceFrames);
		if (drawFrameIndex < 0) drawFrameIndex = 0;
	} else if (mode == 4) { //reverse loop
		
		//one way: nSequenceFrames
		//other way: nSequenceFrames - sequence->frames[sequence->nFrames - 1].nFrames - sequence->frames[0].nFrames
		int nCycleFrames = nSequenceFrames * 2 - sequence->frames[sequence->nFrames - 1].nFrames - sequence->frames[0].nFrames;

		if(nCycleFrames) drawFrameIndex %= nCycleFrames;
		else drawFrameIndex = 0;
		if (drawFrameIndex >= nSequenceFrames) 
			drawFrameIndex = nSequenceFrames - 1 - sequence->frames[sequence->nFrames - 1].nFrames - (drawFrameIndex - nSequenceFrames);
		if (drawFrameIndex < 0) drawFrameIndex = 0;
	} else { //invalid
		drawFrameIndex = 0;
	}

	return drawFrameIndex;
}

DWORD *nanrDrawFrame(DWORD *frameBuffer, NCLR *nclr, NCGR *ncgr, NCER *ncer, NANR *nanr, int sequenceIndex, int frame, int checker, int ofsX, int ofsY) {
	if (frameBuffer == NULL || ncgr == NULL || ncer == NULL || nanr == NULL) return NULL;
	NANR_SEQUENCE *sequence = nanr->sequences + sequenceIndex;
	int mode = sequence->mode;
	int type = sequence->type;

	//frame is not referring to the frame index, but rather the current frame of animation
	int nFrameEntries = sequence->nFrames;
	int nSequenceFrames = getTotalFrameCount(sequence);

	//using play mode, clamp drawFrameIndex to the range [0, nSequenceFrames)
	int drawFrameIndex = getDrawFrameIndex(sequence, frame);

	//next, determine the animation frame that contains drawFrameIndex.
	int frameIndex = getAnimationFrameFromFrame(sequence, drawFrameIndex);
	FRAME_DATA *frameData = sequence->frames + frameIndex;

	if (checker) {
		for (int i = 0; i < 256 * 512; i++) {
			int x = i % 512;
			int y = i / 512;
			int p = ((x >> 2) ^ (y >> 2)) & 1;
			DWORD c = p ? 0xFFFFFF : 0xC0C0C0;
			frameBuffer[i] = c;
		}
	}

	CHAR_VRAM_TRANSFER *vramTransfer = NULL;

	//if this animation has VRAM transfer operations, simulate them here.
	if (nanr->seqVramTransfers != NULL) {
		if (ncgr->slices != NULL) {
			CHAR_VRAM_TRANSFER dummy = { 0 };
			int transferIndex = nanr->seqVramTransfers[sequenceIndex][frameIndex];
			dummy.srcAddr = ncgr->slices[transferIndex].offset;
			dummy.dstAddr = 0;
			dummy.size = ncgr->slices[transferIndex].size;
			vramTransfer = &dummy;
		}
	}

	//now, determine the type of frame and how to draw it.
	int animType = type & 0xFFFF;
	if (animType == 0) { //index
		ANIM_DATA *animData = (ANIM_DATA *) frameData->animationData;

		NCER_CELL *cell = ncer->cells + animData->index;
		int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
		CellRenderCell(frameBuffer, cell, ncer->mappingMode, ncgr, nclr, vramTransfer, translateX + ofsX, translateY + ofsY, 0, -1, 1.0f, 0.0f, 0.0f, 1.0f);
	} else if (animType == 1) { //SRT
		ANIM_DATA_SRT *animData = (ANIM_DATA_SRT *) frameData->animationData;

		//TODO: implement SRT
		float rotation = ((float) animData->rotZ) / 65536.0f * (2.0f * 3.14159f);
		float scaleX = ((float) animData->sx) / 4096.0f;
		float scaleY = ((float) animData->sy) / 4096.0f;

		//compute transformation matrix (don't bother simulating OAM matrix slots)
		float sinR = (float) sin(rotation);
		float cosR = (float) cos(rotation);
		float a = cosR / scaleX;
		float b = sinR / scaleX;
		float c = -sinR / scaleY;
		float d = cosR / scaleY;

		NCER_CELL *cell = ncer->cells + animData->index;
		int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
		CellRenderCell(frameBuffer, cell, ncer->mappingMode, ncgr, nclr, vramTransfer, translateX + animData->px + ofsX, translateY + animData->py + ofsY, 0, -1, a, b, c, d);
	} else if (animType == 2) { //index+translation
		ANIM_DATA_T *animData = (ANIM_DATA_T *) frameData->animationData;

		NCER_CELL *cell = ncer->cells + animData->index;
		int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
		CellRenderCell(frameBuffer, cell, ncer->mappingMode, ncgr, nclr, vramTransfer, translateX + animData->px + ofsX, translateY + animData->py + ofsY, 0, -1, 1.0f, 0.0f, 0.0f, 1.0f);
	}

	return frameBuffer;
}

static HWND AnmViewerGetAssociatedEditor(HWND hWnd, int type) {
	HWND hWndMain = getMainWindow(hWnd);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	switch (type) {
		case FILE_TYPE_PALETTE:
			return nitroPaintStruct->hWndNclrViewer;
		case FILE_TYPE_CHARACTER:
			return nitroPaintStruct->hWndNcgrViewer;
		case FILE_TYPE_CELL:
			return nitroPaintStruct->hWndNcerViewer;
	}
	return NULL;
}

static void AnmViewerPaintFrame(HWND hWnd, HDC hDC) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);
	NCLR *nclr = NULL;
	NCGR *ncgr = NULL;
	NCER *ncer = NULL;

	HWND hWndNclrViewer = AnmViewerGetAssociatedEditor(hWnd, FILE_TYPE_PALETTE);
	HWND hWndNcgrViewer = AnmViewerGetAssociatedEditor(hWnd, FILE_TYPE_CHARACTER);
	HWND hWndNcerViewer = AnmViewerGetAssociatedEditor(hWnd, FILE_TYPE_CELL);
	if (hWndNclrViewer != NULL) nclr = (NCLR *) EditorGetObject(hWndNclrViewer);
	if (hWndNcgrViewer != NULL) ncgr = (NCGR *) EditorGetObject(hWndNcgrViewer);
	if (hWndNcerViewer != NULL) ncer = (NCER *) EditorGetObject(hWndNcerViewer);

	if (nclr != NULL && ncgr != NULL && ncer != NULL) {
		NANR *nanr = &data->nanr;
		int frame = data->frame;
		int sequence = data->sequence;

		if (nanr->sequences[sequence].nFrames > 0) {
			nanrDrawFrame(data->frameBuffer, nclr, ncgr, ncer, nanr, sequence, frame, 1, 0, 0);

			HBITMAP hBitmap = CreateBitmap(512, 256, 1, 32, data->frameBuffer);
			HDC hOffDC = CreateCompatibleDC(hDC);
			SelectObject(hOffDC, hBitmap);
			BitBlt(hDC, 0, 0, 512, 256, hOffDC, 0, 0, SRCCOPY);
			DeleteObject(hOffDC);
			DeleteObject(hBitmap);
		}
	}
}

static void showChild(HWND hWnd, BOOL show) {
	setStyle(hWnd, show, WS_VISIBLE);
	if (show) RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
}

static void AnmViewerUpdateOptionalControls(HWND hWnd, int n) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);
	NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
	WCHAR bf[16];

	int px, py, sx, sy, rotZ;
	int element = sequence->type & 0xFFFF;
	AnmViewerFrameGetTranslate(sequence->frames + n, &px, &py, element);
	AnmViewerFrameGetScaleRotate(sequence->frames + n, &sx, &sy, &rotZ, element);

	SetEditNumber(data->hWndTranslateX, px);
	SetEditNumber(data->hWndTranslateY, py);
	SetEditNumber(data->hWndRotateAngle, rotZ);

	int len = wsprintfW(bf, L"%d.%04d", sx >> 12, (abs(sx) & 0xFFF) * 10000 / 4096);
	SendMessage(data->hWndScaleX, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"%d.%04d", sy >> 12, (abs(sy) & 0xFFF) * 10000 / 4096);
	SendMessage(data->hWndScaleY, WM_SETTEXT, len, (LPARAM) bf);
}

static void AnmViewerSetFrame(HWND hWnd, int n, BOOL updateListBox) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);

	//update controls
	WCHAR bf[16];
	NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
	int len = wsprintfW(bf, L"Frame %d", n);
	SendMessage(data->hWndFrameCounter, WM_SETTEXT, len, (LPARAM) bf);

	// Prevent SETTEXT message from changing the animation data
	data->ignoreInputMsg = TRUE;

	SetEditNumber(data->hWndFrameCount, sequence->frames[n].nFrames);
	if (n < sequence->nFrames && n >= 0) {
		SetEditNumber(data->hWndIndex, ((ANIM_DATA *) (sequence->frames[n].animationData))->index);
	}
	
	data->ignoreInputMsg = FALSE;
	
	int element = sequence->type & 0xFFFF;
	if (element > 0) {
		AnmViewerUpdateOptionalControls(hWnd, n);
	}

	if(updateListBox) SendMessage(data->hWndFrameList, LB_SETCURSEL, n, 0);

	InvalidateRect(hWnd, NULL, FALSE);
}

static void AnmViewerUpdateFieldsVisibility(HWND hWnd, int element) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);
	switch (element) {
		case 0:
			showChild(data->hWndTranslateLabel, FALSE);
			showChild(data->hWndTranslateX, FALSE);
			showChild(data->hWndTranslateY, FALSE);
			showChild(data->hWndScaleLabel, FALSE);
			showChild(data->hWndScaleX, FALSE);
			showChild(data->hWndScaleY, FALSE);
			showChild(data->hWndRotateLabel, FALSE);
			showChild(data->hWndRotateAngle, FALSE);
			break;
		case 1:
			showChild(data->hWndTranslateLabel, TRUE);
			showChild(data->hWndTranslateX, TRUE);
			showChild(data->hWndTranslateY, TRUE);
			showChild(data->hWndScaleLabel, TRUE);
			showChild(data->hWndScaleX, TRUE);
			showChild(data->hWndScaleY, TRUE);
			showChild(data->hWndRotateLabel, TRUE);
			showChild(data->hWndRotateAngle, TRUE);
			break;
		case 2:
			showChild(data->hWndTranslateLabel, TRUE);
			showChild(data->hWndTranslateX, TRUE);
			showChild(data->hWndTranslateY, TRUE);
			showChild(data->hWndScaleLabel, FALSE);
			showChild(data->hWndScaleX, FALSE);
			showChild(data->hWndScaleY, FALSE);
			showChild(data->hWndRotateLabel, FALSE);
			showChild(data->hWndRotateAngle, FALSE);
			break;
	}
	NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
	AnmViewerUpdateOptionalControls(hWnd, getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame)));
	InvalidateRect(hWnd, NULL, TRUE);
}

static void AnmViewerSetSequence(HWND hWnd, int n) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);

	//update sequence 
	data->sequence = n;
	data->frame = 0;

	//update controls
	WCHAR bf[16];
	NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
	int len = wsprintfW(bf, L"Frame %d", getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame)));
	SendMessage(data->hWndFrameCounter, WM_SETTEXT, len, (LPARAM) bf);

	SendMessage(data->hWndFrameList, LB_RESETCONTENT, 0, 0);
	for (int i = 0; i < sequence->nFrames; i++) {
		len = wsprintfW(bf, L"Frame %02d", i);
		SendMessage(data->hWndFrameList, LB_ADDSTRING, len, (LPARAM) bf);
	}
	SendMessage(data->hWndFrameList, LB_SETCURSEL, 0, 0);
	SendMessage(data->hWndPlayMode, CB_SETCURSEL, sequence->mode - 1, 0);
	SendMessage(data->hWndAnimationType, CB_SETCURSEL, (sequence->type >> 16) - 1, 0);
	SendMessage(data->hWndAnimationElement, CB_SETCURSEL, sequence->type & 0xFFFF, 0);

	int element = sequence->type & 0xFFFF;
	AnmViewerUpdateFieldsVisibility(hWnd, element);

	AnmViewerSetFrame(hWnd, 0, FALSE);
	InvalidateRect(hWnd, NULL, TRUE);
}

static void AnmViewerRefreshSequences(HWND hWnd) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);

	SendMessage(data->hWndAnimationDropdown, CB_RESETCONTENT, 0, 0);
	for (int i = 0; i < data->nanr.nSequences; i++) {
		WCHAR buf[16];
		int len = wsprintfW(buf, L"%d", i);
		SendMessage(data->hWndAnimationDropdown, CB_ADDSTRING, len, (LPARAM) buf);
	}
	SendMessage(data->hWndAnimationDropdown, CB_SETCURSEL, data->sequence, 0);
}

static void AnmViewerUpdatePreview(HWND hWnd) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);
	HWND hWndNcerViewer = AnmViewerGetAssociatedEditor(hWnd, FILE_TYPE_CELL);
	if (hWndNcerViewer == NULL) return;

	NANR *nanr = &data->nanr;
	NCER *ncer = (NCER *) EditorGetObject(hWndNcerViewer);
	if (ncer == NULL) return;

	PreviewLoadObjCell(ncer, nanr, data->sequence);
}

static BOOL g_tickThreadRunning = FALSE;
static HWND *g_tickWindows = NULL;
static int g_nTickWindows = 0;
static CRITICAL_SECTION g_tickWindowCriticalSection;
static BOOL g_tickWindowCriticalSectionInitialized = FALSE;
static HANDLE g_hTickThread = NULL;

DWORD CALLBACK AnmViewerTickerProc(LPVOID lpParam) {
	HWND hWnd = (HWND) lpParam;
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);

	LARGE_INTEGER startTime;
	GetSystemTimeAsFileTime((FILETIME *) &startTime);
	LONG offBy = 0;
	int nFrames = -1;
	while (1) {
		LARGE_INTEGER frameStartTime, frameEndTime;
		GetSystemTimeAsFileTime((FILETIME *) &frameStartTime);
		nFrames++;

		EnterCriticalSection(&g_tickWindowCriticalSection);
		for (int i = 0; i < g_nTickWindows; i++) {
			PostMessage(g_tickWindows[i], WM_TIMER, NANRVIEWER_TIMER_TICK, (LPARAM) NULL);
		}
		LeaveCriticalSection(&g_tickWindowCriticalSection);
		Sleep((166667 - offBy + 5000) / 10000);

		//how off were we?
		GetSystemTimeAsFileTime((FILETIME *) &frameEndTime);
		offBy = ((LONG) (frameEndTime.LowPart - frameStartTime.LowPart)) - (166667 - offBy);
		if (offBy > 166667) offBy = 166667; //don't wanna wait for negative time ;)
	}
}

static void AnmViewerStartAnimTickThread(HWND hWnd) {
	if (!g_tickWindowCriticalSectionInitialized) {
		InitializeCriticalSection(&g_tickWindowCriticalSection);
		g_tickWindowCriticalSectionInitialized = TRUE;
	}
	EnterCriticalSection(&g_tickWindowCriticalSection);
	if (!g_tickThreadRunning) {
		DWORD tid;
		g_hTickThread = CreateThread(NULL, 0, AnmViewerTickerProc, (LPVOID) hWnd, 0, &tid);
		g_tickThreadRunning = TRUE;
	}
	if (g_tickWindows == NULL) {
		g_tickWindows = (HWND *) calloc(1, sizeof(HWND));
		g_tickWindows[0] = hWnd;
		g_nTickWindows++;
	} else {
		g_tickWindows = realloc(g_tickWindows, (g_nTickWindows + 1) * sizeof(HWND));
		g_tickWindows[g_nTickWindows] = hWnd;
		g_nTickWindows++;
	}
	LeaveCriticalSection(&g_tickWindowCriticalSection);
}

static void AnmViewerFreeTickThread(HWND hWnd) {
	EnterCriticalSection(&g_tickWindowCriticalSection);
	int index = -1;
	for (int i = 0; i < g_nTickWindows; i++) {
		if (g_tickWindows[i] == hWnd) {
			index = i;
			break;
		}
	}
	memmove(g_tickWindows + index, g_tickWindows + index + 1, g_nTickWindows - index - 1);
	g_nTickWindows--;
	if (g_nTickWindows == 0) {
		free(g_tickWindows);
		g_tickWindows = NULL;
		TerminateThread(g_hTickThread, 0);
		g_hTickThread = NULL;
		g_tickThreadRunning = FALSE;
	}
	LeaveCriticalSection(&g_tickWindowCriticalSection);
}

//simple string to float, doesn't consider other weird string representations.
float my_wtof(const wchar_t *str) {
	int intPart = _wtol(str), fracPart = 0, denominator = 1;
	
	int indexOfDot = -1;
	while (*str && *str != L'.') {
		str++;
	}
	if (*str == L'.') {
		str++;
		fracPart = _wtol(str);

		int nDecimalDigits = wcslen(str);
		for (int i = 0; i < nDecimalDigits; i++) {
			denominator *= 10;
		}
	}

	return ((float) intPart) + ((float) fracPart) / ((float) denominator);
}

static void AnmViewerOnCreate(NANRVIEWERDATA *data, HWND hWnd) {
	data->hWnd = hWnd;
	data->sequence = 0;
	data->frameBuffer = (DWORD *) calloc(256 * 512, 4);
	data->ignoreInputMsg = FALSE;

	CreateStatic(hWnd, L"Sequence:", 522, 10, 70, 22);
	CreateStatic(hWnd, L"Frames:", 632, 95, 50, 22);
	CreateStatic(hWnd, L"Index:", 632, 122, 50, 22);

	data->hWndTranslateLabel = CreateStatic(hWnd, L"Translate:", 632, 149, 50, 22);
	data->hWndScaleLabel = CreateStatic(hWnd, L"Scale:", 632, 149 + 27, 50, 22);
	data->hWndRotateLabel = CreateStatic(hWnd, L"Rotation:", 632, 149 + 27 + 27, 50, 22);

	LPWSTR playModes[] = { L"Forward", L"Forward loop", L"Reverse", L"Reverse loop" };
	LPWSTR animationTypes[] = { L"Cell", L"Multicell" };
	LPWSTR animationElements[] = { L"Index", L"Index+SRT", L"Index+T" };

	data->hWndAnimationDropdown = CreateCombobox(hWnd, NULL, 0, 602, 10, 100, 100, 0);
	data->hWndAddSequence = CreateButton(hWnd, L"+", 724, 10, 22, 22, FALSE);
	data->hWndDeleteSequence = CreateButton(hWnd, L"-", 702, 10, 22, 22, FALSE);
	data->hWndPauseButton = CreateButton(hWnd, L"Play", 522, 37, 50, 22, FALSE);
	data->hWndStepButton = CreateButton(hWnd, L"Step", 577, 37, 50, 22, FALSE);
	data->hWndFrameCounter = CreateStatic(hWnd, L"Frame 0", 632, 37, 100, 22);
	data->hWndFrameList = CreateWindowEx(0, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_STANDARD | LBS_NOINTEGRALHEIGHT, 522, 95, 105, 139, hWnd, NULL, NULL, NULL);
	data->hWndPlayMode = CreateCombobox(hWnd, playModes, 4, 522, 64, 105, 100, 0);
	data->hWndAnimationType = CreateCombobox(hWnd, animationTypes, 2, 627, 64, 105, 100, 0);
	data->hWndAnimationElement = CreateCombobox(hWnd, animationElements, 3, 732, 64, 105, 100, 0);
	data->hWndFrameCount = CreateEdit(hWnd, L"", 687, 95, 50, 22, TRUE);
	data->hWndIndex = CreateEdit(hWnd, L"", 687, 95 + 22 + 5, 50, 22, TRUE);

	data->hWndTranslateX = CreateEdit(hWnd, L"0", 632 + 55, 149, 50, 22, FALSE);
	data->hWndTranslateY = CreateEdit(hWnd, L"0", 632 + 110, 149, 50, 22, FALSE);
	data->hWndScaleX = CreateEdit(hWnd, L"1.0000", 632 + 55, 149 + 27, 50, 22, FALSE);
	data->hWndScaleY = CreateEdit(hWnd, L"1.0000", 632 + 110, 149 + 27, 50, 22, FALSE);
	data->hWndRotateAngle = CreateEdit(hWnd, L"0", 632 + 55, 149 + 27 + 27, 50, 22, FALSE);

	data->hWndDeleteFrame = CreateButton(hWnd, L"-", 522, 245 - 11, 50, 22, FALSE);
	data->hWndAddFrame = CreateButton(hWnd, L"+", 577, 245 - 11, 50, 22, FALSE);

	AnmViewerStartAnimTickThread(hWnd);
}

static void AnmViewerOnTimer(NANRVIEWERDATA *data, HWND hWnd) {
	if (data->playing) {
		data->frame++;
		InvalidateRect(hWnd, NULL, FALSE);

		NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
		AnmViewerSetFrame(hWnd, getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame)), TRUE);
	}
}

static void AnmViewerOnPaint(NANRVIEWERDATA *data, HWND hWnd) {
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(hWnd, &ps);

	AnmViewerPaintFrame(hWnd, hDC);

	EndPaint(hWnd, &ps);
}

static void AnmViewerOnInitialize(NANRVIEWERDATA *data, HWND hWnd, NANR *nanr, LPWSTR path) {
	if (path != NULL) {
		EditorSetFile(hWnd, path);
	}
	memcpy(&data->nanr, nanr, sizeof(NANR));
	AnmViewerUpdatePreview(hWnd);

	for (int i = 0; i < data->nanr.nSequences; i++) {
		WCHAR buf[16];
		int len = wsprintfW(buf, L"%d", i);
		SendMessage(data->hWndAnimationDropdown, CB_ADDSTRING, len, (LPARAM) buf);
	}

	SendMessage(data->hWndAnimationDropdown, CB_SETCURSEL, 0, 0);

	AnmViewerSetSequence(hWnd, 0);
}

static void AnmViewerOnMenuCommand(NANRVIEWERDATA *data, HWND hWnd, int id) {
	switch (id) {
		case ID_FILE_SAVEAS:
			EditorSaveAs(hWnd);
			break;
		case ID_FILE_SAVE:
			EditorSave(hWnd);
			break;
	}
}

static void AnmViewerOnControlCommand(NANRVIEWERDATA *data, HWND hWnd, HWND hWndControl, int idc, int notif) {
	NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
	int startFrameIndex = getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame));
	FRAME_DATA *frameData = NULL;
	if (sequence != NULL) {
		frameData = sequence->frames + startFrameIndex;
	}

	if (hWndControl == data->hWndAnimationDropdown && notif == CBN_SELCHANGE) {
		int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

		data->playing = 0;
		SendMessage(data->hWndPauseButton, WM_SETTEXT, 4, (LPARAM) L"Play");

		AnmViewerSetSequence(hWnd, idx);
		AnmViewerUpdatePreview(hWnd);
	} else if (hWndControl == data->hWndPauseButton && notif == BN_CLICKED) {
		int playing = data->playing;
		data->playing = !playing;

		if (playing) {
			SendMessage(hWndControl, WM_SETTEXT, 4, (LPARAM) L"Play");
		} else {
			SendMessage(hWndControl, WM_SETTEXT, 5, (LPARAM) L"Pause");
		}
	} else if (hWndControl == data->hWndStepButton && notif == BN_CLICKED) {
		if (data->playing) {
			SendMessage(data->hWndPauseButton, WM_SETTEXT, 4, (LPARAM) L"Play");
			data->playing = 0;
		}

		//advance the animation by 1 frame entry. Tricky!
		for (int i = 0; i < frameData->nFrames; i++) {
			data->frame++;
			int frameIndex = getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame));
			if (frameIndex != startFrameIndex) break;
		}

		AnmViewerSetFrame(hWnd, getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame)), TRUE);
	} else if (hWndControl == data->hWndFrameList && notif == LBN_SELCHANGE) {
		int idx = SendMessage(data->hWndFrameList, LB_GETCURSEL, 0, 0);
		data->playing = 0;

		int nFrameIndex = 0;
		for (int i = 0; i < idx; i++) {
			nFrameIndex += sequence->frames[i].nFrames;
		}
		data->frame = nFrameIndex;

		AnmViewerSetFrame(hWnd, idx, FALSE);
	} else if (hWndControl == data->hWndPlayMode && notif == CBN_SELCHANGE) {
		int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

		int mode = idx + 1;
		sequence->mode = mode;
		InvalidateRect(hWnd, NULL, FALSE);
		AnmViewerUpdatePreview(hWnd);
	} else if (hWndControl == data->hWndAnimationElement && notif == LBN_SELCHANGE) {
		int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

		int oldElement = sequence->type & 0xFFFF;
		sequence->type = (sequence->type & 0xFFFF0000) | idx;
		int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_SRT) };

		for (int i = 0; i < sequence->nFrames; i++) {

			int index = getFrameIndex(sequence->frames + i);
			int sx, sy, rotZ, px, py;
			AnmViewerFrameGetScaleRotate(sequence->frames + i, &sx, &sy, &rotZ, oldElement);
			AnmViewerFrameGetTranslate(sequence->frames + i, &px, &py, oldElement);

			sequence->frames[i].animationData = realloc(sequence->frames[i].animationData, sizes[idx]);
			if (idx == 0) {
			} else if (idx == 1) {
				ANIM_DATA_SRT *f = (ANIM_DATA_SRT *) sequence->frames[i].animationData;
				f->px = px;
				f->py = py;
				f->rotZ = rotZ;
				f->sx = sx;
				f->sy = sy;
			} else if (idx == 2) {
				ANIM_DATA_T *f = (ANIM_DATA_T *) sequence->frames[i].animationData;
				f->pad_ = 0xBEEF;
				f->px = px;
				f->py = py;
			}

		}

		AnmViewerUpdateFieldsVisibility(hWnd, idx);
		InvalidateRect(hWnd, NULL, FALSE);
		AnmViewerUpdatePreview(hWnd);
	} else if (hWndControl == data->hWndAnimationType && notif == LBN_SELCHANGE) {
		int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

		sequence->type = (sequence->type & 0xFFFF) | ((idx + 1) << 16);
		InvalidateRect(hWnd, NULL, FALSE);
		AnmViewerUpdatePreview(hWnd);
	} else if (hWndControl == data->hWndFrameCount && notif == EN_CHANGE) {
		if (!data->ignoreInputMsg) {
			sequence->frames[startFrameIndex].nFrames = GetEditNumber(hWndControl);
		}
	} else if (hWndControl == data->hWndIndex && notif == EN_CHANGE) {
		if (!data->ignoreInputMsg) {
			ANIM_DATA *f = (ANIM_DATA *) sequence->frames[startFrameIndex].animationData;
			f->index = GetEditNumber(hWndControl);;
			InvalidateRect(hWnd, NULL, FALSE);
		}
	} else if ((hWndControl == data->hWndTranslateX || hWndControl == data->hWndTranslateY) && notif == EN_CHANGE) {
		int val = GetEditNumber(hWndControl);

		int element = sequence->type & 0xFFFF;
		int x, y;
		AnmViewerFrameGetTranslate(sequence->frames + startFrameIndex, &x, &y, element);
		if (hWndControl == data->hWndTranslateX) {
			x = val;
		} else {
			y = val;
		}
		AnmViewerFrameSetTranslate(sequence->frames + startFrameIndex, x, y, element);
		InvalidateRect(hWnd, NULL, FALSE);
	} else if ((hWndControl == data->hWndScaleX || hWndControl == data->hWndScaleY) && notif == EN_CHANGE) {
		WCHAR bf[16];
		SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) bf);
		float fVal = my_wtof(bf); //standard library I use doesn't export _wtof or wcstof, so I guess I do that myself
		int val = (int) (fVal * 4096.0f + (fVal < 0.0f ? -0.5f : 0.5f));

		int element = sequence->type & 0xFFFF;
		int sx, sy, rotZ;
		AnmViewerFrameGetScaleRotate(sequence->frames + startFrameIndex, &sx, &sy, &rotZ, element);
		if (hWndControl == data->hWndScaleX) {
			sx = val;
		} else {
			sy = val;
		}
		AnmViewerFrameSetScaleRotate(sequence->frames + startFrameIndex, sx, sy, rotZ, element);
		InvalidateRect(hWnd, NULL, FALSE);
	} else if (hWndControl == data->hWndRotateAngle && notif == EN_CHANGE) {
		int val = GetEditNumber(hWndControl);

		int element = sequence->type & 0xFFFF;
		int sx, sy, rotZ;
		AnmViewerFrameGetScaleRotate(sequence->frames + startFrameIndex, &sx, &sy, &rotZ, element);
		rotZ = val;
		AnmViewerFrameSetScaleRotate(sequence->frames + startFrameIndex, sx, sy, rotZ, element);
		InvalidateRect(hWnd, NULL, FALSE);
	} else if (hWndControl == data->hWndAddFrame && notif == BN_CLICKED) {
		int frameIndex = SendMessage(data->hWndFrameList, LB_GETCURSEL, 0, 0);

		//insert a frame after frameIndex. First, determine the size of a frame.
		int element = sequence->type & 0xFFFF;
		int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_SRT) };

		void *newFrame = calloc(1, sizes[element]);
		int nFrames = sequence->nFrames + 1;
		sequence->frames = realloc(sequence->frames, nFrames * sizeof(FRAME_DATA));

		//element frameIndex+1 now goes to frameIndex+2, ...
		if (frameIndex < nFrames - 2) {
			memmove(sequence->frames + frameIndex + 2, sequence->frames + frameIndex + 1, (nFrames - frameIndex - 2) * sizeof(FRAME_DATA));
		}
		sequence->frames[frameIndex + 1].nFrames = 1;
		sequence->frames[frameIndex + 1].pad_ = 0xBEEF;
		sequence->frames[frameIndex + 1].animationData = newFrame;

		//populate the new frame with valid data
		switch (element) {
			case 0:
			{
				ANIM_DATA *f = (ANIM_DATA *) newFrame;
				(void) f;
				break;
			}
			case 1:
			{
				ANIM_DATA_SRT *f = (ANIM_DATA_SRT *) newFrame;
				f->sx = 4096;
				f->sy = 4096;
				break;
			}
			case 2:
			{
				ANIM_DATA_T *f = (ANIM_DATA_T *) newFrame;
				f->pad_ = 0xBEEF;
				break;
			}
		}
		sequence->nFrames = nFrames;

		//find first frame the new index is in
		int frame = 0;
		for (int i = 0; i < frameIndex + 1; i++) {
			frame += sequence->frames[i].nFrames;
		}

		AnmViewerSetSequence(hWnd, data->sequence);
		data->frame = frame;
		AnmViewerSetFrame(hWnd, frameIndex + 1, TRUE);
		AnmViewerUpdatePreview(hWnd);
	} else if (hWndControl == data->hWndDeleteFrame && notif == BN_CLICKED) {
		int frameIndex = SendMessage(data->hWndFrameList, LB_GETCURSEL, 0, 0);
		if (sequence->nFrames <= 0) return;

		//remove a frame
		int nFrames = sequence->nFrames;
		FRAME_DATA *frames = sequence->frames;
		FRAME_DATA *removeFrame = frames + frameIndex;
		free(removeFrame->animationData);

		if (frameIndex < nFrames - 1) {
			memmove(frames + frameIndex, frames + frameIndex + 1, (nFrames - frameIndex - 1) * sizeof(FRAME_DATA));
		}
		nFrames--;
		sequence->nFrames = nFrames;
		sequence->frames = realloc(sequence->frames, nFrames * sizeof(FRAME_DATA));

		//find first frame the new index is in
		int frame = 0;
		if (frameIndex >= nFrames) frameIndex = nFrames - 1;
		if (frameIndex < 0) frameIndex = 0;
		for (int i = 0; i < frameIndex; i++) {
			frame += sequence->frames[i].nFrames;
		}

		AnmViewerSetSequence(hWnd, data->sequence);
		data->frame = frame;
		AnmViewerSetFrame(hWnd, frameIndex, TRUE);
		AnmViewerUpdatePreview(hWnd);
	} else if (hWndControl == data->hWndAddSequence && notif == BN_CLICKED) {
		//add one sequence
		int seqno = SendMessage(data->hWndAnimationDropdown, CB_GETCURSEL, 0, 0);

		NANR *nanr = &data->nanr;
		nanr->nSequences++;
		nanr->sequences = (NANR_SEQUENCE *) realloc(nanr->sequences, nanr->nSequences * sizeof(NANR_SEQUENCE));

		//move all sequences after seqno
		int nSeq = nanr->nSequences;
		if (seqno < nSeq - 2) {
			memmove(nanr->sequences + seqno + 2, nanr->sequences + seqno + 1, (nSeq - seqno - 2) * sizeof(NANR_SEQUENCE));
		}

		//clear out sequence
		seqno++; //point to added sequence
		nanr->sequences[seqno].nFrames = 1;
		nanr->sequences[seqno].frames = (FRAME_DATA *) calloc(1, sizeof(FRAME_DATA));
		nanr->sequences[seqno].mode = 1;
		nanr->sequences[seqno].type = 0 | (1 << 16);
		nanr->sequences[seqno].startFrameIndex = 0;

		FRAME_DATA *frame = nanr->sequences[seqno].frames;
		frame->nFrames = 1;
		frame->pad_ = 0xBEEF;
		frame->animationData = (ANIM_DATA *) calloc(1, sizeof(ANIM_DATA));

		ANIM_DATA *animData = (ANIM_DATA *) frame->animationData;
		animData->index = 0;

		AnmViewerSetSequence(hWnd, seqno);
		AnmViewerSetFrame(hWnd, 0, TRUE);
		data->frame = 0;
		AnmViewerRefreshSequences(hWnd);
		AnmViewerUpdatePreview(hWnd);
	} else if (hWndControl == data->hWndDeleteSequence && notif == BN_CLICKED) {
		int seqno = SendMessage(data->hWndAnimationDropdown, CB_GETCURSEL, 0, 0);

		NANR *nanr = &data->nanr;
		NANR_SEQUENCE *sequences = nanr->sequences;

		//free sequence
		FRAME_DATA *frames = sequences[seqno].frames;
		for (int i = 0; i < sequences[seqno].nFrames; i++) {
			free(frames[i].animationData);
		}
		free(frames);

		//move sequences
		if (seqno < nanr->nSequences - 1) {
			memmove(sequences + seqno, sequences + seqno + 1, (nanr->nSequences - seqno - 1) * sizeof(NANR_SEQUENCE));
		}
		nanr->nSequences--;
		nanr->sequences = realloc(nanr->sequences, nanr->nSequences * sizeof(NANR_SEQUENCE));

		data->sequence--;
		if (data->sequence < 0) data->sequence = 0;

		data->frame = 0;
		AnmViewerSetSequence(hWnd, data->sequence);
		AnmViewerSetFrame(hWnd, 0, TRUE);
		AnmViewerRefreshSequences(hWnd);
		AnmViewerUpdatePreview(hWnd);
	}
}

static void AnmViewerOnCommand(NANRVIEWERDATA *data, HWND hWnd, WPARAM wParam, LPARAM lParam) {
	if (lParam == 0 && HIWORD(wParam) == 0) {
		//menu command
		AnmViewerOnMenuCommand(data, hWnd, LOWORD(wParam));
	} else if (lParam == 0 && HIWORD(wParam) != 0) {
		//accelerator command
	} else if (lParam != 0) {
		//control command
		AnmViewerOnControlCommand(data, hWnd, (HWND) lParam, LOWORD(wParam), HIWORD(wParam));
	}
}

static void AnmViewerOnDestroy(NANRVIEWERDATA *data, HWND hWnd) {
	DWORD *frameBuffer = data->frameBuffer;
	data->frameBuffer = NULL;
	free(frameBuffer);
	AnmViewerFreeTickThread(hWnd);
}

LRESULT CALLBACK AnmViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
			AnmViewerOnCreate(data, hWnd);
			break;
		case WM_TIMER:
			AnmViewerOnTimer(data, hWnd);
			break;
		case WM_PAINT:
			AnmViewerOnPaint(data, hWnd);
			break;
		case NV_INITIALIZE:
			AnmViewerOnInitialize(data, hWnd, (NANR *) lParam, (LPWSTR) wParam);
			break;
		case NV_UPDATEPREVIEW:
			AnmViewerUpdatePreview(hWnd);
			break;
		case WM_COMMAND:
			AnmViewerOnCommand(data, hWnd, wParam, lParam);
			break;
		case WM_DESTROY:
			AnmViewerOnDestroy(data, hWnd);
			break;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

void RegisterNanrViewerClass(void) {
	int features = 0;
	EDITOR_CLASS *cls = EditorRegister(L"NanrViewerClass", AnmViewerWndProc, L"NANR Viewer", sizeof(NANRVIEWERDATA), features);
	EditorAddFilter(cls, NANR_TYPE_NANR, L"nanr", L"NANR Files (*.nanr)\0*.nanr\0");
	EditorAddFilter(cls, NANR_TYPE_GHOSTTRICK, L"bin", L"Ghost Trick Files (*.bin)\0*.bin\0");
}

HWND CreateNanrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NANR nanr;
	int n = AnmReadFile(&nanr, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}
	HWND h = EditorCreate(L"NanrViewerClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nanr);
	return h;
}

HWND CreateNanrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NANR *nanr) {
	HWND h = EditorCreate(L"NanrViewerClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, 0, (LPARAM) nanr);
	return h;
}
