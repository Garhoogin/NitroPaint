#pragma once
#include "nitropaint.h"
#include "childwindow.h"
#include "ui.h"
#include "filecommon.h"

typedef struct EDITOR_CLASS_ {
	ATOM aclass;
	int nFilters;
	LPCWSTR *filters;
	LPCWSTR *extensions;
} EDITOR_CLASS;

// ----- Class data slots
#define EDITOR_CD_SLOT(n)     ((n)*sizeof(void*))
#define EDITOR_CD_SIZE        (8*sizeof(void*))

#define EDITOR_CD_TITLE       EDITOR_CD_SLOT(0)
#define EDITOR_CD_WNDPROC     EDITOR_CD_SLOT(1)
#define EDITOR_CD_INITIALIZED EDITOR_CD_SLOT(2)
#define EDITOR_CD_DATA_SIZE   EDITOR_CD_SLOT(3)
#define EDITOR_CD_LIGHTBRUSH  EDITOR_CD_SLOT(4)
#define EDITOR_CD_LIGHTPEN    EDITOR_CD_SLOT(5)
#define EDITOR_CD_FEATURES    EDITOR_CD_SLOT(6)
#define EDITOR_CD_CLASSINFO   EDITOR_CD_SLOT(7)


// ----- Window data slots
#define EDITOR_WD_SLOT(n)     ((n)*sizeof(void*))
#define EDITOR_WD_SIZE        (2*sizeof(void*))

#define EDITOR_WD_DATA        EDITOR_WD_SLOT(0)
#define EDITOR_WD_INITIALIZED EDITOR_WD_SLOT(1)

// ----- editor features bitmap
#define EDITOR_FEATURE_ZOOM      (1<<0)
#define EDITOR_FEATURE_GRIDLINES (1<<1)
#define EDITOR_FEATURE_UNDO      (1<<2)

// ----- editor status codes
#define EDITOR_STATUS_SUCCESS    OBJ_STATUS_SUCCESS
#define EDITOR_STATUS_CANCELLED  (OBJ_STATUS_MAX+1)

#define EDITOR_BASIC_MEMBERS     \
	FRAMEDATA frameData;         \
	int scale;                   \
	int showBorders;             \
	WCHAR szOpenFile[MAX_PATH]


// ----- Basic editor window data struct
typedef struct EDITOR_DATA_ {
	EDITOR_BASIC_MEMBERS;

	//first part of file object info
	OBJECT_HEADER file;

	//after here may vary...
} EDITOR_DATA;


EDITOR_CLASS *EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize, int features);

void EditorAddFilter(EDITOR_CLASS *cls, int format, LPCWSTR extension, LPCWSTR filter);

void EditorSetFile(HWND hWnd, LPCWSTR file);

void *EditorGetData(HWND hWnd);

OBJECT_HEADER *EditorGetObject(HWND hWnd);

void EditorSetData(HWND hWnd, void *data);

int EditorSave(HWND hWnd);

int EditorSaveAs(HWND hWnd);

HWND EditorCreate(LPCWSTR lpszClassName, int x, int y, int width, int height, HWND hWndParent);
