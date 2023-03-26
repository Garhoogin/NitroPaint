#pragma once
#include "color.h"

#define g_useDarkTheme 0

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
// Copy a bitmap to the clipboard.
//
void copyBitmap(COLOR32 *img, int width, int height);

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
LPWSTR GetFileName(LPWSTR lpszPath);

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
