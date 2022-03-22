#pragma once
#include "nmcr.h"
#include "childwindow.h"

#include <Windows.h>

typedef struct {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];
	NMCR nmcr;
	int multiCell;
	int frame;
	int *frameTimes;   //time currently spent on the current frame of each sequence
	int *frameNumbers; //current frame index of each active sequence
} NMCRVIEWERDATA;

VOID RegisterNmcrViewerClass(VOID);

HWND CreateNmcrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);
