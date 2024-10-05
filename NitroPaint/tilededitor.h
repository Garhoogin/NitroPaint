#pragma once

#include <Windows.h>

#include "color.h"
#include "framebuffer.h"
#include "editor.h"

#define MARGIN_SIZE          8
#define MARGIN_BORDER_SIZE   2
#define MARGIN_TOTAL_SIZE    (MARGIN_SIZE+MARGIN_BORDER_SIZE)

#define SEL_BORDER_THICKNESS 8


//hit test constants for selection
#define HIT_SEL_LEFT            0x0001
#define HIT_SEL_RIGHT           0x0002
#define HIT_SEL_TOP             0x0004
#define HIT_SEL_BOTTOM          0x0008
#define HIT_SEL_CONTENT         0x0010

//hit test constants for margin
#define HIT_MARGIN_LEFT         0x0001
#define HIT_MARGIN_TOP          0x0002
#define HIT_MARGIN_SEL          0x0004

//hit test contents
#define HIT_FLAGS_MASK          0x000F
#define HIT_TYPE_MASK           0x7000

#define HIT_SEL                 0x4000
#define HIT_CONTENT             0x5000
#define HIT_NOWHERE             0x6000
#define HIT_MARGIN              0x7000


typedef HCURSOR (*TedGetCursorProc)             (HWND hWnd, int hit);
typedef void    (*TedTileHoverCallback)         (HWND hWnd, int tileX, int tileY);
typedef void    (*TedRenderCallback)            (HWND hWnd, FrameBuffer *fb, int scrollX, int scrollY, int renderWidth, int renderHeight);
typedef int     (*TedSuppressHighlightCallback) (HWND hWnd);
typedef int     (*TedIsSelectionModeCallback)   (HWND hWnd);
typedef void    (*TedUpdateCursorCallback)      (HWND hWnd, int pxX, int pxY);

typedef struct TedData_ {
	FrameBuffer fb;
	FrameBuffer fbMargin;

	//callback routines for interaction with the owner
	TedGetCursorProc getCursorProc;                         // callback to get cursor
	TedTileHoverCallback tileHoverCallback;                 // callback to indicate change in hovered tile
	TedRenderCallback renderCallback;                       // callback to render graphics
	TedSuppressHighlightCallback suppressHighlightCallback; // callback to suppress tile highlight
	TedIsSelectionModeCallback isSelectionModeCallback;     // callback to determine selection mode
	TedUpdateCursorCallback updateCursorCallback;           // callback to update cursor

	BOOL mouseOver;
	HWND hWndLastMouse; // last mouse event (cleared on mouse leave)
	int lastMouseX;     // mouse client Y prev
	int lastMouseY;     // mouse client X prev
	int mouseX;         // mouse client X
	int mouseY;         // mouse client Y
	int hoverX;         // the currently hovered char X
	int hoverY;         // the currently hovered char Y
	int hoverIndex;     // the currently hovered char index
	int contextHoverX;  // char X the context menu was activated on
	int contextHoverY;  // char Y the context menu was activated on

	int mouseDown;      // mouse L button down
	int mouseDownHit;   // hit test when mouse-down
	int mouseDownTop;   // mouse L button down (on top margin)
	int mouseDownLeft;  // mouse L button down (on left margin)
	int dragStartX;     // mouse drag start X client position
	int dragStartY;     // mouse drag start Y client position
	int selStartX;      // selection start char X
	int selStartY;      // selection start char Y
	int selEndX;        // selection end char X
	int selEndY;        // selection end char Y
	int selDragStartX;  // selection start X at start of drag gesture
	int selDragStartY;  // selection start Y at start of drag gesture

	int tilesX;         // width in tiles
	int tilesY;         // height in tiles

	HWND hWnd;
	HWND hWndViewer;
} TedData;



// ----- margin functions

void TedMarginPaint(HWND hWnd, EDITOR_DATA *data, TedData *ted);
void TedUpdateMargins(TedData *data);


// ----- viewer functions

void TedOnViewerPaint(EDITOR_DATA *data, TedData *ted);
void TedGetScroll(TedData *data, int *scrollX, int *scrollY);


// ----- general functions

int TedHitTest(EDITOR_DATA *data, TedData *ted, int x, int y);
void TedUpdateCursor(EDITOR_DATA *data, TedData *ted);
void TedReleaseCursor(EDITOR_DATA *data, TedData *ted);
int TedHasSelection(TedData *ted);
void TedDeselect(TedData *ted);
int TedGetSelectionBounds(TedData *ted, int *x, int *y, int *width, int *height);
int TedIsSelectedAll(TedData *ted);
void TedSelectAll(TedData *ted);
void TedOffsetSelection(TedData *ted, int dx, int dy);
void TedMakeSelectionCornerEnd(TedData *ted, int hit);
void TedUpdateSize(EDITOR_DATA *data, TedData *ted, int tilesX, int tilesY);


// ----- message handling functions

BOOL TedSetCursor(EDITOR_DATA *data, TedData *ted, WPARAM wParam, LPARAM lParam);
void TedMainOnMouseMove(EDITOR_DATA *data, TedData *ted, UINT msg, WPARAM wParam, LPARAM lParam);
void TedOnLButtonDown(EDITOR_DATA *data, TedData *ted);
void TedOnRButtonDown(TedData *ted);
void TedViewerOnMouseMove(EDITOR_DATA *data, TedData *ted, UINT msg, WPARAM wParam, LPARAM lParam);
void TedViewerOnLButtonDown(EDITOR_DATA *data, TedData *ted);
void TedViewerOnKeyDown(EDITOR_DATA *data, TedData *ted, WPARAM wParam, LPARAM lParam);


void TedInit(TedData *ted, HWND hWnd, HWND hWndViewer);
void TedDestroy(TedData *ted);
