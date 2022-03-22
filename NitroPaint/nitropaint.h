#pragma once

#define g_useDarkTheme 0

typedef struct {
	BOOL useDarkTheme;
	BOOL fullPaths;
	BOOL renderTransparent;
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

LPWSTR openFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension);

LPWSTR saveFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension);

LPWSTR GetFileName(LPWSTR lpszPath);