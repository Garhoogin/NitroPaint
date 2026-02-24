#pragma once
#include <Windows.h>

#include "nmcr.h"
#include "childwindow.h"
#include "editor.h"
#include "framebuffer.h"
#include "nanrviewer.h"


typedef struct McPlayer_ {
	int playing;      // animation sequence is playing
	int multiCell;    // index in multi-cell bank of current multi-cell
	
	int nPlayers;              // number of tracking nodes
	AnmSeqPlayer *nodePlayers; // node players
} McPlayer;

typedef struct {
	EDITOR_BASIC_MEMBERS;
	NMCR *nmcr;
	
	FrameBuffer fb;
	McPlayer player;
	COLOR32 rendered[512 * 256];

	NANRVIEWERDATA *nanrViewer;        // associated animation editor (animation sequences taken from here)

	WCHAR listviewItemBuffers[2][32];
	HWND hWndMultiCellList;

} NMCRVIEWERDATA;

VOID RegisterNmcrViewerClass(VOID);

HWND CreateNmcrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NMCR *nmcr);
