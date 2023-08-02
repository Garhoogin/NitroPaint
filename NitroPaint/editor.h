#pragma once
#include "nitropaint.h"
#include "ui.h"

// ----- Class data slots
#define EDITOR_CD_SLOT(n)     ((n)*sizeof(void*))
#define EDITOR_CD_SIZE        (4*sizeof(void*))

#define EDITOR_CD_TITLE       EDITOR_CD_SLOT(0)
#define EDITOR_CD_WNDPROC     EDITOR_CD_SLOT(1)
#define EDITOR_CD_INITIALIZED EDITOR_CD_SLOT(2)
#define EDITOR_CD_DATA_SIZE   EDITOR_CD_SLOT(3)


// ----- Window data slots
#define EDITOR_WD_SLOT(n)     ((n)*sizeof(void*))
#define EDITOR_WD_SIZE        (2*sizeof(void*))

#define EDITOR_WD_DATA        EDITOR_WD_SLOT(0)
#define EDITOR_WD_INITIALIZED EDITOR_WD_SLOT(1)


ATOM EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize);

void EditorSetTitle(HWND hWnd, LPCWSTR file);

void *EditorGetData(HWND hWnd);

void EditorSetData(HWND hWnd, void *data);

HWND EditorCreate(LPCWSTR lpszClassName, int x, int y, int width, int height, HWND hWndParent);
