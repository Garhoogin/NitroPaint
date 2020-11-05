#include <Windows.h>
#include "childwindow.h"
#include "nscr.h"

typedef struct {
	FRAMEDATA frameData;
	NSCR nscr;
	WCHAR szOpenFile[MAX_PATH];
	int showBorders;
	int scale;

	HWND hWndTileEditor;

	int hoverX;
	int hoverY;
	int contextHoverX;
	int contextHoverY;
	int editingX;
	int editingY;
} NSCRVIEWERDATA;

VOID RegisterNscrViewerClass(VOID);

HWND CreateNscrViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);