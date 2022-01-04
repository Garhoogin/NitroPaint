#include <Windows.h>
#include "childwindow.h"
#include "ncgr.h"

typedef struct {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];
	NCGR ncgr;
	int showBorders;
	int scale;
	int hoverX;
	int hoverY;
	int contextHoverX;
	int contextHoverY;
	int hoverIndex;
	int selectedPalette;
	int verifyColor;
	int verifyFrames;
	int transparent;
	HWND hWndViewer;
	HWND hWndCharacterLabel;
	HWND hWndPaletteDropdown;
	HWND hWndWidthLabel;
	HWND hWndWidthDropdown;
	HWND hWndTileEditorWindow;
	HWND hWndExpand;
	HWND hWnd;
	HWND hWndExpandRowsInput;
	HWND hWndExpandButton;
	HWND hWnd8bpp;
} NCGRVIEWERDATA;

VOID RegisterNcgrViewerClass(VOID);

HWND CreateNcgrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNcgrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCGR *ncgr);
