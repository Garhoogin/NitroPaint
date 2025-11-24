#pragma once
#include <Windows.h>

#include "nmcr.h"
#include "childwindow.h"
#include "editor.h"


typedef struct {
	EDITOR_BASIC_MEMBERS;
	NMCR *nmcr;
	int multiCell;
	int frame;
	int *frameTimes;   //time currently spent on the current frame of each sequence
	int *frameNumbers; //current frame index of each active sequence
} NMCRVIEWERDATA;

VOID RegisterNmcrViewerClass(VOID);

HWND CreateNmcrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);
