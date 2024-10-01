#include <Windows.h>
#include "editor.h"
#include "childwindow.h"
#include "ncgr.h"
#include "framebuffer.h"

typedef enum ChrViewerMode_ {
	CHRVIEWER_MODE_SELECT,            // create selections
	CHRVIEWER_MODE_PEN,               // pen draw tool
	CHRVIEWER_MODE_FILL,              // flood fill tool
	CHRVIEWER_MODE_EYEDROP,           // select color tool
	CHRVIEWER_MODE_STAMP              // stamp repeated tile tool
} ChrViewerMode;

typedef struct {
	EDITOR_BASIC_MEMBERS;
	NCGR ncgr;
	FrameBuffer fb;
	FrameBuffer fbMargin;

	ChrViewerMode mode; // current edit mode
	ChrViewerMode lastMode;

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


	int selectedPalette;
	int useAttribute;
	int verifyStart;
	int verifyEnd;
	int verifySelMode;
	int verifyFrames;
	int transparent;
	HWND hWndViewer;
	HWND hWndCharacterLabel;
	HWND hWndPaletteDropdown;
	HWND hWndUseAttribute;
	HWND hWndWidthLabel;
	HWND hWndWidthDropdown;
	HWND hWndTileEditorWindow;
	HWND hWndExpand;
	HWND hWndExpandRowsInput;
	HWND hWndExpandColsInput;
	HWND hWndExpandButton;
	HWND hWnd8bpp;
} NCGRVIEWERDATA;

VOID RegisterNcgrViewerClass(VOID);

HWND CreateNcgrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNcgrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCGR *ncgr);
