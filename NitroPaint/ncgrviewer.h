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
	HWND hWndViewer;
	HWND hWndPaletteDropdown;
	HWND hWndWidthLabel;
	HWND hWndWidthDropdown;
	HWND hWndTileEditorWindow;
} NCGRVIEWERDATA;

VOID RegisterNcgrViewerClass(VOID);

HWND CreateNcgrViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);