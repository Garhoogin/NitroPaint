#pragma once
#include "nanr.h"
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

	HWND hWnd;
} NANRVIEWERDATA;

VOID RegisterNanrViewerClass(VOID);

HWND CreateNanrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);