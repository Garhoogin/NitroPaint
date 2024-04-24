#pragma once
#include <Windows.h>

#include "ui.h"
#include "color.h"
#include "filecommon.h"

#define g_useDarkTheme 0

extern HICON g_appIcon;

typedef struct {
	BOOL useDarkTheme;
	BOOL fullPaths;
	BOOL renderTransparent;
	BOOL dpiAware;
	BOOL allowMultipleInstances;
	HBRUSH hbrBackground;
	LPWSTR backgroundPath;
	struct {
		BOOL useDSColorPicker;
	} nclrViewerConfiguration;
	struct {
		BOOL gridlines;
	} ncgrViewerConfiguration;
	struct {
		BOOL gridlines;
	} nscrViewerConfiguration;
} CONFIGURATIONSTRUCT;

extern CONFIGURATIONSTRUCT g_configuration;
extern LPWSTR g_configPath;

typedef struct {
	HWND hWndMdi;
	HWND hWndNclrViewer;
	HWND hWndNcgrViewer;
	HWND hWndNcerViewer;
	HWND hWndNanrViewer;
	HWND hWndNmcrViewer;
	HWND hWndNsbtxViewer;
} NITROPAINTSTRUCT;

typedef struct {
	int waitOn;
	void *data; //data passed to callback once the progress has finished
	void (*callback) (void *data); //function called once the wait is finished
	int progress1;
	int progress1Max;
	int progress2;
	int progress2Max;

	HWND hWndProgress1;
	HWND hWndProgress2;
} PROGRESSDATA;

//WM_COPYDATA types
#define NPMSG_OPENFILE       1

//common filter names
#define FILTER_NAME_PALETTE        L"Palette Files"
#define FILTER_NAME_CHARACTER      L"Graphics Files"
#define FILTER_NAME_SCREEN         L"Screen Files"
#define FILTER_NAME_CELL           L"Cell Files"
#define FILTER_NAME_ANIM           L"Animation Files"
#define FILTER_NAME_COMBO2D        L"Combination Files"
#define FILTER_NAME_TEXARC         L"Texture Archives"
#define FILTER_NAME_TEXTURE        L"Texture Files"
#define FILTER_NAME_ALL            L"All Supported Files"
#define FILTER_NAME_IMAGE          L"Supported Image Files"

//common filter file extensions
#define FILTER_EXTS_PALETTE        L"*.nclr;*.rlcn;*.ncl;*.icl;*.acl;*.5pc;*.5pl;*.ntfp;*.nbfp;*.pltt;*.bin"
#define FILTER_EXTS_CHARACTER      L"*.ncgr;*.rgcn;*.ncbr;*.ncg;*.icg;*.acg;*.nbfc;*.char;*.bin"
#define FILTER_EXTS_SCREEN         L"*.nscr;*.rcsn;*.nsc;*.isc;*.asc;*.nbfs;*.bin"
#define FILTER_EXTS_CELL           L"*.ncer;*.recn;*.bin"
#define FILTER_EXTS_ANIM           L"*.nanr;*.rnan;*.bin"
#define FILTER_EXTS_COMBO2D        L"*.mbb;*.dat;*.bnr;*.bin"
#define FILTER_EXTS_TEXARC         L"*.nsbtx;*.nsbmd;*.bmd"
#define FILTER_EXTS_TEXTURE        L"*.tga;*.5tx;*.tds;*.nnstga"
#define FILTER_EXTS_IMAGE          L"*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga"
#define FILTER_EXTS_ALL            L"*.nclr;*.rlcn;*.ncl;*.icl;*.acl;*.5pl;*.5pc;*.ntfp;*.nbfp;*.bin;*.pltt;*.ncgr;*.rgcn;*.ncbr;*.ncg;*.icg;*.acg;*.nbfc;*.char;*.nscr;*.rcsn;*.nsc;*.isc;*.asc;*.nbfs;*.ncer;*.recn;*.nanr;*.rnan;*.dat;*.nsbmd;*.nsbtx;*.bmd;*.bnr;*.tga;*.5tx;*.tds"

//common filters
#define FILTER_PALETTE    FILTER_NAME_PALETTE   L"\0" FILTER_EXTS_PALETTE   L"\0"
#define FILTER_CHARACTER  FILTER_NAME_CHARACTER L"\0" FILTER_EXTS_CHARACTER L"\0"
#define FILTER_SCREEN     FILTER_NAME_SCREEN    L"\0" FILTER_EXTS_SCREEN    L"\0"
#define FILTER_CELL       FILTER_NAME_CELL      L"\0" FILTER_EXTS_CELL      L"\0"
#define FILTER_ANIM       FILTER_NAME_ANIM      L"\0" FILTER_EXTS_ANIM      L"\0"
#define FILTER_COMBO2D    FILTER_NAME_COMBO2D   L"\0" FILTER_EXTS_COMBO2D   L"\0"
#define FILTER_TEXARC     FILTER_NAME_TEXARC    L"\0" FILTER_EXTS_TEXARC    L"\0"
#define FILTER_TEXTURE    FILTER_NAME_TEXTURE   L"\0" FILTER_EXTS_TEXTURE   L"\0"
#define FILTER_ALL        FILTER_NAME_ALL       L"\0" FILTER_EXTS_ALL       L"\0"
#define FILTER_IMAGE      FILTER_NAME_IMAGE     L"\0" FILTER_EXTS_IMAGE     L"\0"
#define FILTER_ALLFILES   L"All Files\0*.*\0"

//
// Get the DPI scaling for the current monitor.
//
float GetDpiScale(void);

//
// Handle configuring nonclient DPI scaling for the Window under the current configuration.
// Call during WM_NCCREATE.
//
void HandleNonClientDpiScale(HWND hWnd);

//
// Apply a DPI scaling factor to a window.
//
void DpiScaleChildren(HWND hWnd, float scale);

//
// Enable non-client DPI scaling of a window if DPI awareness is enabled.
//
void DoHandleNonClientDpiScale(HWND hWnd);

//
// Call to handle WM_DPICHANGED.
//
LRESULT HandleWindowDpiChange(HWND hWnd, WPARAM wParam, LPARAM lParam);

//
// Register a generic window class.
//
void RegisterGenericClass(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, int cbWndExtra);

//
// Makes a window and its children use the default GUI font.
//
void SetGUIFont(HWND hWnd);

//
// Create an open file dialog
//
LPWSTR openFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension);

//
// Create an open files dialog
//
LPWSTR openFilesDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension);

//
// Create a save file dialog
//
LPWSTR saveFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension);

//
// Copy a bitmap to the clipboard.
//
void copyBitmap(COLOR32 *px, int width, int height);

//
// Creates a text prompt
//
int PromptUserText(HWND hWnd, LPCWSTR title, LPCWSTR prompt, LPWSTR text, int maxLength);

//
// Get number of paths from string
//
int getPathCount(LPCWSTR paths);

//
// Read a path from a multi-file string
//
void getPathFromPaths(LPCWSTR paths, int index, WCHAR *path);

//
// Get the current zoom level
//
int MainGetZoom(HWND hWnd);

//
// Set the current zoom level
//
void MainSetZoom(HWND hWnd, int zoom);

//
// Zoom in the main window
//
VOID MainZoomIn(HWND hWnd);

//
// Zoom out the main window
//
VOID MainZoomOut(HWND hWnd);

//
// Get a file name from a file path (does not edit the source string)
//
LPCWSTR GetFileName(LPCWSTR lpszPath);

//
// Get the type of editor by its window handle. It may return one of the macros
// starting with FILE_TYPE.
//
int GetEditorType(HWND hWndEditor);

//
// Invalidate all editor windows of a specified type. Alternatively, specify
// FILE_TYPE_UNKNOWN for type to invalidate all editor windows that are editing
// a valid file.
//
void InvalidateAllEditors(HWND hWndMain, int type);

//
// Enumerate all editor windows of a specified type.
//
void EnumAllEditors(HWND hWndMain, int type, BOOL (*pfn) (HWND, void *), void *param);

//
// Get a list of editors of a certain type into an array. Returns the total
// number of open editor windows of the specified type. Can pass in a length of
// of 0 to retrieve only the editor count.
//
int GetAllEditors(HWND hWndMain, int type, HWND *editors, int bufferSize);

//
// Gets the editor associated with an object by its pointer. 
//
HWND GetEditorFromObject(HWND hWndMain, OBJECT_HEADER *obj);


//common viewer window messages
#define NV_INITIALIZE (WM_USER+1)
#define NV_SETTITLE (WM_USER+2)
#define NV_INITIALIZE_IMMEDIATE (WM_USER+3)
#define NV_RECALCULATE (WM_USER+4)
#define NV_PICKFILE (WM_USER+5)
#define NV_SETDATA (WM_USER+6)
#define NV_INITIMPORTDIALOG (WM_USER+7)
#define NV_SETPATH (WM_USER+8)
#define NV_GETTYPE (WM_USER+9)
#define NV_XTINVALIDATE (WM_USER+10)
#define NV_CHILDNOTIF (WM_USER+11)
#define NV_UPDATEPREVIEW (WM_USER+12)
