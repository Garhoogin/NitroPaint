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
	HWND hWndPreview;
	HWND hWndCharacterLabel;
	HWND hWndCharacterNumber;
	HWND hWndPaletteLabel;
	HWND hWndPaletteNumber;
	HWND hWndApply;
	HWND hWndTileBaseLabel;
	HWND hWndTileBase;

	int hoverX;
	int hoverY;
	int contextHoverX;
	int contextHoverY;
	int editingX;
	int editingY;
	int verifyColor;
	int verifyFrames;
	int tileBase;

	int mouseDown;
	int selStartX; //-1 when no selection.
	int selStartY;
	int selEndX;
	int selEndY;
} NSCRVIEWERDATA;

void NscrViewerSetTileBase(HWND hWnd, int tileBase);

VOID RegisterNscrViewerClass(VOID);

HWND CreateNscrViewer(int x, int y, int width, int height, HWND hWndParent, LPWSTR path);