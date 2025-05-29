#pragma once
#include "nanr.h"
#include "ncer.h"
#include "ncgr.h"
#include "nclr.h"

#include <Windows.h>

#include "childwindow.h"
#include "editor.h"
#include "ui.h"
#include "framebuffer.h"

typedef struct AnmTransSrt_ {
	float sx;  // scale Y
	float sy;  // scale X
	float rot; // rotation (radians)
	float tx;  // translate X
	float ty;  // translate Y
} AnmTransSrt;

typedef struct {
	EDITOR_BASIC_MEMBERS;
	NANR nanr;

	UiCtlManager mgr;            // UI control manager
	FrameBuffer fb;              // rendering frame buffer

	int currentAnim;             // index of current animation
	int currentFrame;            // index of current frame
	int playing;                 // currently playing
	int curFrameTime;            // current time on animation frame
	int direction;               // current animation direction
	int resetFlag;               // sequence is stopped and will be restarted

	int mouseDown;
	int hit;
	int mouseDownHit;
	int mouseDownX;
	int mouseDownY;
	int mouseDownAnchorX; // anchor X on first mouse down
	int mouseDownAnchorY; // anchor Y on first mouse down
	int mouseDownCircleR; // guide circle radius size on mouse down (to stabilize view)
	int mouseDownCircleX; // guide circle X on mouse down
	int mouseDownCircleY; // guide circle Y on mouse down
	float rotAngleOffset; // rotation angle offset when changing
	AnmTransSrt transStart;

	int anchorX;                 // edit anchor X
	int anchorY;                 // edit anchor Y
	int rotPtX;                  // rotation point X
	int rotPtY;                  // rotation point Y

	WCHAR listviewItemBuffers[2][32];

	COLOR32 cellRender[512 * 256]; // rendered cell buffer

	HWND hWndAnimList;           // animation list
	HWND hWndPreview;            // preview
	HWND hWndNewSequence;

	HWND hWndPlayPause;
	HWND hWndStop;

	HWND hWndPlayMode;           // F, FL, B, BL

	HWND hWndShowFrames;         // Show Frames button
	HWND hWndFrames;             // frame list window
	HWND hWndFrameList;          // frame list control
	WCHAR frameListBuffers[8][16];
	int suppressFrameList;

	int interpResult;
	void *interpData;
	HWND hWndCheckboxLinear;
	HWND hWndCheckboxClockwise;
	HWND hWndInterpFrames;
	HWND hWndInterpDuration;
	HWND hWndInterpOK;

} NANRVIEWERDATA;

VOID RegisterNanrViewerClass(VOID);

HWND CreateNanrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNanrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NANR *nanr);

void AnmViewerUpdateCellBounds(HWND hWnd);
