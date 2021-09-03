#include <Windows.h>
#include <CommCtrl.h>

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

extern HICON g_appIcon;

#define NV_INITIALIZE (WM_USER+1)

#define NANRVIEWER_TIMER_TICK 1

int getTotalFrameCount(NANR_SEQUENCE *sequence) {
	int nFrames = 0;
	for (int i = 0; i < sequence->nFrames; i++) {
		nFrames += sequence->frames[i].nFrames;
	}
	return nFrames;
}

int getAnimationFrameFromFrame(NANR_SEQUENCE *sequence, int drawFrameIndex) {
	if (sequence == NULL) return 0;
	for (int i = 0; i < sequence->nFrames; i++) {
		drawFrameIndex -= sequence->frames[i].nFrames;
		if (drawFrameIndex < 0) return i;
	}
	return sequence->nFrames - 1;
}

int getFrameIndex(FRAME_DATA *frame) {
	ANIM_DATA *f = (ANIM_DATA *) frame->animationData;
	return f->index;
}

void getFrameScaleRotate(FRAME_DATA *frame, int *sx, int *sy, int *rotZ, int element) {
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

void setFrameScaleRotate(FRAME_DATA *frame, int sx, int sy, int rotZ, int element) {
	if (element == 1) {
		ANIM_DATA_SRT *f = (ANIM_DATA_SRT *) frame->animationData;
		f->sx = sx;
		f->sy = sy;
		f->rotZ = rotZ;
	}
}

void getFrameTranslate(FRAME_DATA *frame, int *px, int *py, int element) {
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

void setFrameTranslate(FRAME_DATA *frame, int px, int py, int element) {
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

int getDrawFrameIndex(NANR_SEQUENCE *sequence, int frame) {
	if (sequence == NULL) return 0;
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

DWORD *nanrDrawFrame(DWORD *frameBuffer, NCLR *nclr, NCGR *ncgr, NCER *ncer, NANR *nanr, int sequenceIndex, int frame) {
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

	for (int i = 0; i < 256 * 512; i++) {
		int x = i % 512;
		int y = i / 512;
		int p = ((x >> 2) ^ (y >> 2)) & 1;
		DWORD c = p ? 0xFFFFFF : 0xC0C0C0;
		frameBuffer[i] = c;
	}

	//now, determine the type of frame and how to draw it.
	int animType = type & 0xFFFF;
	if (animType == 0) { //index
		ANIM_DATA *animData = (ANIM_DATA *) frameData->animationData;

		NCER_CELL *cell = ncer->cells + animData->index;
		int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
		ncerRenderWholeCell2(frameBuffer, cell, ncgr, nclr, translateX, translateY, 0, -1);
	} else if (animType == 1) { //SRT
		ANIM_DATA_SRT *animData = (ANIM_DATA_SRT *) frameData->animationData;

		//TODO: implement SRT
		NCER_CELL *cell = ncer->cells + animData->index;
		int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
		ncerRenderWholeCell2(frameBuffer, cell, ncgr, nclr, translateX + animData->px, translateY + animData->py, 0, -1);
	} else if (animType == 2) { //index+translation
		ANIM_DATA_T *animData = (ANIM_DATA_T *) frameData->animationData;

		NCER_CELL *cell = ncer->cells + animData->index;
		int translateX = 256 - (cell->maxX + cell->minX) / 2, translateY = 128 - (cell->maxY + cell->minY) / 2;
		ncerRenderWholeCell2(frameBuffer, cell, ncgr, nclr, translateX + animData->px, translateY + animData->py, 0, -1);
	}

	return frameBuffer;
}

VOID PaintNanrFrame(HWND hWnd, HDC hDC) {
	NCLR *nclr = NULL;
	NCGR *ncgr = NULL;
	NCER *ncer = NULL;
	HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	if (nitroPaintStruct->hWndNclrViewer) {
		HWND hWndNclrViewer = nitroPaintStruct->hWndNclrViewer;
		NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLong(hWndNclrViewer, 0);
		nclr = &nclrViewerData->nclr;
	}
	if (nitroPaintStruct->hWndNcgrViewer) {
		HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
		NCGRVIEWERDATA *ncgrViewerData = (NCLRVIEWERDATA *) GetWindowLong(hWndNcgrViewer, 0);
		ncgr = &ncgrViewerData->ncgr;
	}
	if (nitroPaintStruct->hWndNcerViewer) {
		HWND hWndNcerViewer = nitroPaintStruct->hWndNcerViewer;
		NCERVIEWERDATA *ncerViewerData = (NCLRVIEWERDATA *) GetWindowLong(hWndNcerViewer, 0);
		ncer = &ncerViewerData->ncer;
	}

	if (nclr != NULL && ncgr != NULL && ncer != NULL) {
		NANR *nanr = &data->nanr;
		int frame = data->frame;
		int sequence = data->sequence;

		nanrDrawFrame(data->frameBuffer, nclr, ncgr, ncer, nanr, sequence, frame);

		HBITMAP hBitmap = CreateBitmap(512, 256, 1, 32, data->frameBuffer);
		HDC hOffDC = CreateCompatibleDC(hDC);
		SelectObject(hOffDC, hBitmap);
		BitBlt(hDC, 0, 0, 512, 256, hOffDC, 0, 0, SRCCOPY);
		DeleteObject(hOffDC);
		DeleteObject(hBitmap);
	}
}

void showChild(HWND hWnd, BOOL show) {
	DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
	if (show) dwStyle |= WS_VISIBLE;
	else dwStyle &= ~WS_VISIBLE;
	SetWindowLong(hWnd, GWL_STYLE, dwStyle);
	if (show) RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
}

void updateOptionalControls(HWND hWnd, int n) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
	WCHAR bf[16];

	int px, py, sx, sy, rotZ;
	int element = sequence->type & 0xFFFF;
	getFrameTranslate(sequence->frames + n, &px, &py, element);
	getFrameScaleRotate(sequence->frames + n, &sx, &sy, &rotZ, element);

	int len = wsprintfW(bf, L"%d", px);
	SendMessage(data->hWndTranslateX, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"%d", py);
	SendMessage(data->hWndTranslateY, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"%d.%04d", sx >> 12, (abs(sx) & 0xFFF) * 10000 / 4096);
	SendMessage(data->hWndScaleX, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"%d.%04d", sy >> 12, (abs(sy) & 0xFFF) * 10000 / 4096);
	SendMessage(data->hWndScaleY, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"%d", rotZ);
	SendMessage(data->hWndRotateAngle, WM_SETTEXT, len, (LPARAM) bf);
}

void nanrViewerSetFrame(HWND hWnd, int n, BOOL updateListBox) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	//update controls
	WCHAR bf[16];
	NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
	int len = wsprintfW(bf, L"Frame %d", n);
	SendMessage(data->hWndFrameCounter, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"%d", sequence->frames[n].nFrames);
	SendMessage(data->hWndFrameCount, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"%d", ((ANIM_DATA *) (sequence->frames[n].animationData))->index);
	SendMessage(data->hWndIndex, WM_SETTEXT, len, (LPARAM) bf);
	
	int element = sequence->type & 0xFFFF;
	if (element > 0) {
		updateOptionalControls(hWnd, n);
	}

	if(updateListBox) SendMessage(data->hWndFrameList, LB_SETCURSEL, n, 0);

	InvalidateRect(hWnd, NULL, FALSE);
}

void nanrViewerUpdatePropertySelection(HWND hWnd, int element) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
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
	updateOptionalControls(hWnd, getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame)));
	InvalidateRect(hWnd, NULL, TRUE);
}

void nanrViewerSetSequence(HWND hWnd, int n) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

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
	nanrViewerUpdatePropertySelection(hWnd, element);

	nanrViewerSetFrame(hWnd, 0, FALSE);
	InvalidateRect(hWnd, NULL, TRUE);
}

static BOOL g_tickThreadRunning = FALSE;
static HWND *g_tickWindows = NULL;
static int g_nTickWindows = 0;
static CRITICAL_SECTION g_tickWindowCriticalSection;
static BOOL g_tickWindowCriticalSectionInitialized = FALSE;
static HANDLE g_hTickThread = NULL;

DWORD CALLBACK NanrViewerTicker(LPVOID lpParam) {
	HWND hWnd = (HWND) lpParam;
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

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

void setupWindowAnimationTick(HWND hWnd) {
	if (!g_tickWindowCriticalSectionInitialized) {
		InitializeCriticalSection(&g_tickWindowCriticalSection);
		g_tickWindowCriticalSectionInitialized = TRUE;
	}
	EnterCriticalSection(&g_tickWindowCriticalSection);
	if (!g_tickThreadRunning) {
		DWORD tid;
		g_hTickThread = CreateThread(NULL, 0, NanrViewerTicker, (LPVOID) hWnd, 0, &tid);
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

void destroyWindowAnimationTick(HWND hWnd) {
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

LRESULT CALLBACK NanrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (NANRVIEWERDATA *) calloc(1, sizeof(*data));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			data->hWnd = hWnd;
			data->sequence = 0;
			data->frameBuffer = (DWORD *) calloc(256 * 512, 4);

			CreateWindow(L"STATIC", L"Sequence:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 522, 10, 70, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Frames:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 632, 95, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Index:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 632, 122, 50, 22, hWnd, NULL, NULL, NULL);

			data->hWndTranslateLabel = CreateWindow(L"STATIC", L"Translate:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 632, 149, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndScaleLabel = CreateWindow(L"STATIC", L"Scale:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 632, 149 + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndRotateLabel = CreateWindow(L"STATIC", L"Rotation:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 632, 149 + 27 + 27, 50, 22, hWnd, NULL, NULL, NULL);
			
			data->hWndAnimationDropdown = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 602, 10, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndPauseButton = CreateWindow(L"BUTTON", L"Play", WS_VISIBLE | WS_CHILD, 522, 37, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndStepButton = CreateWindow(L"BUTTON", L"Step", WS_VISIBLE | WS_CHILD, 577, 37, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndFrameCounter = CreateWindow(L"STATIC", L"Frame 0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 632, 37, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndFrameList = CreateWindowEx(0, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_STANDARD | LBS_NOINTEGRALHEIGHT , 522, 95, 105, 139, hWnd, NULL, NULL, NULL);
			data->hWndPlayMode = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 522, 64, 105, 100, hWnd, NULL, NULL, NULL);
			data->hWndAnimationType = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 627, 64, 105, 100, hWnd, NULL, NULL, NULL);
			data->hWndAnimationElement = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 732, 64, 105, 100, hWnd, NULL, NULL, NULL);
			data->hWndFrameCount = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 687, 95, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndIndex = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 687, 95 + 22 + 5, 50, 22, hWnd, NULL, NULL, NULL);

			data->hWndTranslateX = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 632 + 55, 149, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndTranslateY = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 632 + 110, 149, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndScaleX = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1.0000", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 632 + 55, 149 + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndScaleY = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1.0000", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 632 + 110, 149 + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndRotateAngle = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 632 + 55, 149 + 27 + 27, 50, 22, hWnd, NULL, NULL, NULL);

			data->hWndDeleteFrame = CreateWindow(L"BUTTON", L"-", WS_VISIBLE | WS_CHILD, 522, 245 - 11, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndAddFrame = CreateWindow(L"BUTTON", L"+", WS_VISIBLE | WS_CHILD, 577, 245 - 11, 50, 22, hWnd, NULL, NULL, NULL);

			setupWindowAnimationTick(hWnd);
			break;
		}
		case WM_TIMER:
		{
			if (data->playing) {
				data->frame++;
				InvalidateRect(hWnd, NULL, FALSE);

				NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
				nanrViewerSetFrame(hWnd, getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame)), TRUE);
			}
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			PaintNanrFrame(hWnd, hDC);

			EndPaint(hWnd, &ps);
			break;
		}
		case NV_INITIALIZE:
		{
			LPWSTR path = (LPWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->nanr, (NANR *) lParam, sizeof(NANR));
			WCHAR titleBuffer[MAX_PATH + 15];
			if (!g_configuration.fullPaths) path = GetFileName(path);
			memcpy(titleBuffer, path, wcslen(path) * 2 + 2);
			memcpy(titleBuffer + wcslen(titleBuffer), L" - NANR Viewer", 30);
			SetWindowText(hWnd, titleBuffer);

			for (int i = 0; i < data->nanr.nSequences; i++) {
				WCHAR buf[16];
				int len = wsprintfW(buf, L"%d", i);
				SendMessage(data->hWndAnimationDropdown, CB_ADDSTRING, len, (LPARAM) buf);
			}

			LPWSTR modes[] = { L"Forward", L"Forward loop", L"Reverse", L"Reverse loop" };
			for (int i = 1; i < 5; i++) {
				LPWSTR mode = modes[i - 1];
				SendMessage(data->hWndPlayMode, CB_ADDSTRING, wcslen(mode), (LPARAM) mode);
			}

			SendMessage(data->hWndAnimationType, CB_ADDSTRING, 4, (LPARAM) L"Cell");
			SendMessage(data->hWndAnimationType, CB_ADDSTRING, 9, (LPARAM) L"Multicell");
			SendMessage(data->hWndAnimationElement, CB_ADDSTRING, 5, (LPARAM) L"Index");
			SendMessage(data->hWndAnimationElement, CB_ADDSTRING, 9, (LPARAM) L"Index+SRT");
			SendMessage(data->hWndAnimationElement, CB_ADDSTRING, 7, (LPARAM) L"Index+T");

			SendMessage(data->hWndAnimationDropdown, CB_SETCURSEL, 0, 0);
			
			nanrViewerSetSequence(hWnd, 0);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl == NULL && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
					case ID_FILE_SAVE:
					{
						nanrWrite(&data->nanr, data->szOpenFile);
						break;
					}
				}
			}
			if (hWndControl != NULL) {
				NANR_SEQUENCE *sequence = data->nanr.sequences + data->sequence;
				int startFrameIndex = getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame));
				FRAME_DATA *frameData = NULL;
				if (sequence != NULL) {
					frameData = sequence->frames + startFrameIndex;
				}

				WORD notif = HIWORD(wParam);
				if (hWndControl == data->hWndAnimationDropdown && notif == CBN_SELCHANGE) {
					int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

					nanrViewerSetSequence(hWnd, idx);
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

					nanrViewerSetFrame(hWnd, getAnimationFrameFromFrame(sequence, getDrawFrameIndex(sequence, data->frame)), TRUE);
				} else if (hWndControl == data->hWndFrameList && notif == LBN_SELCHANGE) {
					int idx = SendMessage(data->hWndFrameList, LB_GETCURSEL, 0, 0);
					data->playing = 0;

					int nFrameIndex = 0;
					for (int i = 0; i < idx; i++) {
						nFrameIndex += sequence->frames[i].nFrames;
					}
					data->frame = nFrameIndex;

					nanrViewerSetFrame(hWnd, idx, FALSE);
				} else if (hWndControl == data->hWndPlayMode && notif == CBN_SELCHANGE) {
					int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

					int mode = idx + 1;
					sequence->mode = mode;
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (hWndControl == data->hWndAnimationElement && notif == LBN_SELCHANGE) {
					int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

					int oldElement = sequence->type & 0xFFFF;
					sequence->type = (sequence->type & 0xFFFF0000) | idx;
					int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_SRT) };

					for (int i = 0; i < sequence->nFrames; i++) {

						int index = getFrameIndex(sequence->frames + i);
						int sx, sy, rotZ, px, py;
						getFrameScaleRotate(sequence->frames + i, &sx, &sy, &rotZ, oldElement);
						getFrameTranslate(sequence->frames + i, &px, &py, oldElement);

						sequence->frames[i].animationData = realloc(sequence->frames[i].animationData, sizes[idx]);
						if (idx == 0) {
							((ANIM_DATA *) sequence->frames[i].animationData)->pad_ = 0xBEEF; //in keeping with Nintendo tradition
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

					nanrViewerUpdatePropertySelection(hWnd, idx);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (hWndControl == data->hWndAnimationType && notif == LBN_SELCHANGE) {
					int idx = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

					sequence->type = (sequence->type & 0xFFFF) | ((idx + 1) << 16);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (hWndControl == data->hWndFrameCount && notif == EN_CHANGE) {
					WCHAR bf[16];
					SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) bf);
					int frameCount = _wtol(bf);

					sequence->frames[startFrameIndex].nFrames = frameCount;
				} else if (hWndControl == data->hWndIndex && notif == EN_CHANGE) {
					WCHAR bf[16];
					SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) bf);
					int index = _wtol(bf);

					ANIM_DATA *f = (ANIM_DATA *) sequence->frames[startFrameIndex].animationData;
					f->index = index;
					InvalidateRect(hWnd, NULL, FALSE);
				} else if ((hWndControl == data->hWndTranslateX || hWndControl == data->hWndTranslateY) && notif == EN_CHANGE) {
					WCHAR bf[16];
					SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) bf);
					int val = _wtol(bf);

					int element = sequence->type & 0xFFFF;
					int x, y;
					getFrameTranslate(sequence->frames + startFrameIndex, &x, &y, element);
					if (hWndControl == data->hWndTranslateX) {
						x = val;
					} else {
						y = val;
					}
					setFrameTranslate(sequence->frames + startFrameIndex, x, y, element);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if ((hWndControl == data->hWndScaleX || hWndControl == data->hWndScaleY) && notif == EN_CHANGE) {
					WCHAR bf[16];
					SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) bf);
					float fVal = my_wtof(bf); //standard library I use doesn't export _wtof or wcstof, so I guess I do that myself
					int val = (int) (fVal * 4096.0f + (fVal < 0.0f ? -0.5f : 0.5f));

					int element = sequence->type & 0xFFFF;
					int sx, sy, rotZ;
					getFrameScaleRotate(sequence->frames + startFrameIndex, &sx, &sy, &rotZ, element);
					if (hWndControl == data->hWndScaleX) {
						sx = val;
					} else {
						sy = val;
					}
					setFrameScaleRotate(sequence->frames + startFrameIndex, sx, sy, rotZ, element);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (hWndControl == data->hWndRotateAngle && notif == EN_CHANGE) {
					WCHAR bf[16];
					SendMessage(hWndControl, WM_GETTEXT, 16, (LPARAM) bf);
					int val = _wtol(bf);

					int element = sequence->type & 0xFFFF;
					int sx, sy, rotZ;
					getFrameScaleRotate(sequence->frames + startFrameIndex, &sx, &sy, &rotZ, element);
					rotZ = val;
					setFrameScaleRotate(sequence->frames + startFrameIndex, sx, sy, rotZ, element);
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
							f->pad_ = 0xBEEF;
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

					nanrViewerSetSequence(hWnd, data->sequence);
					data->frame = frame;
					nanrViewerSetFrame(hWnd, frameIndex + 1, TRUE);
				} else if (hWndControl == data->hWndDeleteFrame && notif == BN_CLICKED) {
					int frameIndex = SendMessage(data->hWndFrameList, LB_GETCURSEL, 0, 0);
					if (sequence->nFrames <= 0) break;

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

					nanrViewerSetSequence(hWnd, data->sequence);
					data->frame = frame;
					nanrViewerSetFrame(hWnd, frameIndex, TRUE);
				}

			}
			break;
		}
		case WM_DESTROY:
		{
			DWORD *frameBuffer = data->frameBuffer;
			data->frameBuffer = NULL;
			nanrFree(&data->nanr);
			free(frameBuffer);
			free(data);
			destroyWindowAnimationTick(hWnd);
			break;
		}
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNanrViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NanrViewerClass";
	wcex.lpfnWndProc = NanrViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

HWND CreateNanrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NANR nanr;
	int n = nanrReadFile(&nanr, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NanrViewerClass", L"NANR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nanr);
	return h;
}