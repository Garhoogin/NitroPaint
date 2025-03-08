#include <Windows.h>
#include "editor.h"
#include "childwindow.h"
#include "ncgr.h"
#include "framebuffer.h"
#include "tilededitor.h"

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
	ChrViewerMode mode; // current edit mode
	ChrViewerMode lastMode;

	int selectedPalette;
	int useAttribute;
	int verifyStart;
	int verifyEnd;
	int verifySelMode;
	int verifyFrames;
	int transparent;

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
	
	TedData ted;
} NCGRVIEWERDATA;

void RegisterNcgrViewerClass(void);

void ChrViewerGraphicsSizeUpdated(HWND hWnd);

HWND CreateNcgrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNcgrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCGR *ncgr);

