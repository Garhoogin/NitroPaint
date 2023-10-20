#pragma once
#include "nitropaint.h"
#include "childwindow.h"
#include "ui.h"
#include "filecommon.h"

// ----- Class data slots
#define EDITOR_CD_SLOT(n)     ((n)*sizeof(void*))
#define EDITOR_CD_SIZE        (6*sizeof(void*))

#define EDITOR_CD_TITLE       EDITOR_CD_SLOT(0)
#define EDITOR_CD_WNDPROC     EDITOR_CD_SLOT(1)
#define EDITOR_CD_INITIALIZED EDITOR_CD_SLOT(2)
#define EDITOR_CD_DATA_SIZE   EDITOR_CD_SLOT(3)
#define EDITOR_CD_LIGHTBRUSH  EDITOR_CD_SLOT(4)
#define EDITOR_CD_LIGHTPEN    EDITOR_CD_SLOT(5)


// ----- Window data slots
#define EDITOR_WD_SLOT(n)     ((n)*sizeof(void*))
#define EDITOR_WD_SIZE        (2*sizeof(void*))

#define EDITOR_WD_DATA        EDITOR_WD_SLOT(0)
#define EDITOR_WD_INITIALIZED EDITOR_WD_SLOT(1)


// ----- Basic editor window data struct
typedef struct EDITOR_DATA_ {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];

	//first part of file object info
	OBJECT_HEADER file;

	//after here may vary...
} EDITOR_DATA;


ATOM EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize);

void EditorSetFile(HWND hWnd, LPCWSTR file);

void *EditorGetData(HWND hWnd);

void EditorSetData(HWND hWnd, void *data);

HWND EditorCreate(LPCWSTR lpszClassName, int x, int y, int width, int height, HWND hWndParent);
