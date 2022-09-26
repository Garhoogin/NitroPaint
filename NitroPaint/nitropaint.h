#pragma once

#define g_useDarkTheme 0

typedef struct {
	BOOL useDarkTheme;
	BOOL fullPaths;
	BOOL renderTransparent;
	BOOL dpiAware;
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
	HWND hWndNscrViewer;
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

//
// Create an open file dialog
//
LPWSTR openFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension);

//
// Create a save file dialog
//
LPWSTR saveFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension);

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
