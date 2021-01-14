#pragma once

#define g_useDarkTheme 0

typedef struct {
	BOOL useDarkTheme;
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
	HWND hWndNsbtxViewer;
} NITROPAINTSTRUCT;

typedef struct {
	int waitOn;
	void *data; //data passed to callback once the progress has finished
	void (*callback) (void *data); //function called once the wait is finished
} PROGRESSDATA;

LPWSTR openFileDialog(HWND hWnd, LPWSTR title, LPWSTR filter, LPWSTR extension);

LPWSTR saveFileDialog(HWND hWnd, LPWSTR title, LPWSTR filter, LPWSTR extension);