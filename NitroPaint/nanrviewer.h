#pragma once
#include "nanr.h"
#include "ncer.h"
#include "ncgr.h"
#include "nclr.h"
#include "childwindow.h"

#include <Windows.h>

typedef struct {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];
	NANR nanr;
	int frame;
	int sequence;
	int playing;
	DWORD *frameBuffer;
	BOOL ignoreInputMsg;

	HWND hWndAnimationDropdown;
	HWND hWndPauseButton;
	HWND hWndStepButton;
	HWND hWndFrameCounter;
	HWND hWndFrameList;
	HWND hWndPlayMode; //animation play mode
	HWND hWndAnimationType;
	HWND hWndAnimationElement;

	HWND hWndFrameCount;
	HWND hWndIndex;

	HWND hWndTranslateLabel;
	HWND hWndTranslateX;
	HWND hWndTranslateY;

	HWND hWndScaleLabel;
	HWND hWndScaleX;
	HWND hWndScaleY;
	HWND hWndRotateLabel;
	HWND hWndRotateAngle;

	HWND hWndAddFrame;
	HWND hWndDeleteFrame;
	HWND hWndAddSequence;
	HWND hWndDeleteSequence;

	HWND hWnd;
} NANRVIEWERDATA;

DWORD *nanrDrawFrame(DWORD *frameBuffer, NCLR *nclr, NCGR *ncgr, NCER *ncer, NANR *nanr, int sequenceIndex, int frame, int checker, int ofsX, int ofsY);

VOID RegisterNanrViewerClass(VOID);

HWND CreateNanrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNanrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NANR *nanr);
