#pragma once
#include <Windows.h>

#include "ui.h"
#include "filecommon.h"
#include "childwindow.h"
#include "struct.h"

typedef struct EditorFilter_ {
	LPCWSTR filter;           // file filter
	LPCWSTR extension;        // file extension
} EditorFilter;

typedef struct EDITOR_CLASS_ {
	ATOM aclass;              // the ATOM associated with this editor class
	size_t dataSize;          // size of window data per editor
	int features;             // editor features
	StMap filters;            // int -> EditorFilter

	const wchar_t *title;     // editor title
	WNDPROC lpfnWndProc;      // window procedure

	HPEN hLightPen;
	HBRUSH hLightBrush;
} EDITOR_CLASS;

typedef struct EditorManager_ {
	HWND hWnd;          // handle of window owning the manager
	StList classList;   // list of classes
	StList editorList;  // list of editors
} EditorManager;

// ----- Class data slots
#define EDITOR_CD_SLOT(n)     ((n)*sizeof(void*))
#define EDITOR_CD_SIZE        (1*sizeof(void*))

#define EDITOR_CD_CLASSINFO   EDITOR_CD_SLOT(0)


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
	HWND hWnd;                   \
	int scale;                   \
	int scalePrev;               \
	int showBorders;             \
	int dirty;                   \
	StList destroyCallbacks;     \
	EDITOR_CLASS *cls;           \
	EditorManager *editorMgr;    \
	WCHAR szOpenFile[MAX_PATH]


// ----- Basic editor window data struct
typedef struct EDITOR_DATA_ {
	EDITOR_BASIC_MEMBERS;

	//first part of file object info
	OBJECT_HEADER file;

	//after here may vary...
} EDITOR_DATA;

// ----- editor destroyed callback function
typedef void (*EditorDestroyCallback) (EDITOR_DATA *data, void *param);


void EditorMgrInit(HWND hWndMgr);

StStatus EditorGetAllByType(HWND hWndMgr, int type, StList *list);

HWND EditorFindByObject(HWND hWndMgr, OBJECT_HEADER *obj);

void EditorInvalidateAllByType(HWND hWndMgr, int type);

int EditorIsValid(HWND hWndMgr, HWND hWnd);

int EditorGetType(HWND hWnd);



EDITOR_CLASS *EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize, int features);

void EditorAddFilter(EDITOR_CLASS *cls, int format, LPCWSTR extension, LPCWSTR filter);

void EditorSetFile(HWND hWnd, LPCWSTR file);

void *EditorGetData(HWND hWnd);

OBJECT_HEADER *EditorGetObject(HWND hWnd);

void EditorSetData(HWND hWnd, void *data);

int EditorSave(HWND hWnd);

int EditorSaveAs(HWND hWnd);

HWND EditorCreate(LPCWSTR lpszClassName, int x, int y, int width, int height, HWND hWndParent);

void EditorRegisterDestroyCallback(EDITOR_DATA *data, EditorDestroyCallback callback, void *param);

void EditorRemoveDestroyCallback(EDITOR_DATA *data, EditorDestroyCallback callback, void *param);

#include "nitropaint.h"
