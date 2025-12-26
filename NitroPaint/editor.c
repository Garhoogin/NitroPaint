#include <Shlwapi.h>
#include <Uxtheme.h>

#include "nitropaint.h"
#include "resource.h"
#include "editor.h"
#include "combo2d.h"

//zoom menu IDs
static const unsigned short sZoomMenuIds[] = { ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800, ID_ZOOM_1600, ID_ZOOM_3200, ID_ZOOM_6400 };
static const unsigned char sZoomLevels[] = { 1, 2, 4, 8, 16, 32, 64 };

typedef struct EditorDestroyCallbackEntry_ {
	EditorDestroyCallback callback;
	void *param;
} EditorDestroyCallbackEntry;

typedef struct EditorCreateCallbackEntry_ {
	EditorCreatedCallback callback;
	void *param;
	int type;
} EditorCreateCallbackEntry;


static int EditorGetMenuCommandByZoom(int zoom) {
	for (unsigned int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		if (zoom == (int) sZoomLevels[i]) return sZoomMenuIds[i];
	}
	return ID_ZOOM_100;
}

static int EditorGetZoomByMenuCommand(int command) {
	for (unsigned int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		if (command == (int) sZoomMenuIds[i]) return sZoomLevels[i];
	}
	return 1;
}

static void EditorUpdateZoomMenu(EDITOR_DATA *data) {
	HWND hWndMain = data->editorMgr->hWnd;
	for (unsigned int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		CheckMenuItem(GetMenu(hWndMain), sZoomMenuIds[i], (data->scale == (int) sZoomLevels[i]) ? MF_CHECKED : MF_UNCHECKED);
	}
}

static void EditorSetZoom(EDITOR_DATA *data, int scale) {
	//set menu
	int cmd = EditorGetMenuCommandByZoom(scale);
	SendMessage(data->hWnd, WM_COMMAND, cmd, 0);
}

static void EditorZoomIn(EDITOR_DATA *data) {
	//get next size up from current scale
	int nextZoom = sZoomLevels[sizeof(sZoomLevels) / sizeof(sZoomLevels[0]) - 1];
	for (unsigned int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]) - 1; i++) {
		if (data->scale == (int) sZoomLevels[i]) {
			nextZoom = sZoomLevels[i + 1]; // next up
			break;
		}
	}

	//set zoom
	EditorSetZoom(data, nextZoom);
}

static void EditorZoomOut(EDITOR_DATA *data) {
	//get next size down from current scale
	int nextZoom = sZoomLevels[0];
	for (unsigned int i = 1; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		if (data->scale == (int) sZoomLevels[i]) {
			nextZoom = sZoomLevels[i - 1]; // next down
			break;
		}
	}

	//set zoom
	EditorSetZoom(data, nextZoom);
}


static EditorManager *EditorGetManager(HWND hWndMgr) {
	return (EditorManager *) GetWindowLongPtr(hWndMgr, 0);
}

void EditorMgrInit(HWND hWndMgr) {
	EditorManager *mgr = EditorGetManager(hWndMgr);
	mgr->hWnd = hWndMgr;
	StListCreateInline(&mgr->classList, EDITOR_CLASS *, NULL);
	StListCreateInline(&mgr->editorList, EDITOR_DATA *, NULL);
	StListCreateInline(&mgr->createCallbacks, EditorCreateCallbackEntry, NULL);
}

StStatus EditorRegisterCreateCallback(EditorManager *mgr, int type, EditorCreatedCallback callback, void *param) {
	EditorCreateCallbackEntry entry = { 0 };
	entry.callback = callback;
	entry.param = param;
	entry.type = type;
	return StListAdd(&mgr->createCallbacks, &entry);
}

void EditorRemoveCreateCallback(EditorManager *mgr, int type, EditorCreatedCallback callback, void *param) {
	EditorCreateCallbackEntry entry = { 0 };
	entry.callback = callback;
	entry.param = param;
	entry.type = type;

	size_t index = StListIndexOf(&mgr->createCallbacks, &entry);
	if (index != ST_INDEX_NOT_FOUND) {
		StListRemove(&mgr->createCallbacks, index);
	}
}

StStatus EditorGetAllByType(HWND hWndMgr, int type, StList *list) {
	EditorManager *mgr = EditorGetManager(hWndMgr);
	StStatus status = ST_STATUS_OK;

	//count editors of the specified type
	for (size_t i = 0; i < mgr->editorList.length; i++) {
		EDITOR_DATA *data = *(EDITOR_DATA **) StListGetPtr(&mgr->editorList, i);
		if (data->file == NULL) continue;

		if (type == FILE_TYPE_INVALID || type == data->file->type) {
			status = StListAdd(list, &data);
			if (!ST_SUCCEEDED(status)) break;
		}
	}
	return status;
}

void EditorInvalidateAllByType(HWND hWndMgr, int type) {
	EditorManager *mgr = EditorGetManager(hWndMgr);

	for (size_t i = 0; i < mgr->editorList.length; i++) {
		EDITOR_DATA *data = *(EDITOR_DATA **) StListGetPtr(&mgr->editorList, i);
		if (data->file == NULL) continue;

		if (type == FILE_TYPE_INVALID || data->file->type == type) {
			InvalidateRect(data->hWnd, NULL, FALSE);
		}
	}
}

HWND EditorFindByObject(HWND hWndParent, OBJECT_HEADER *obj) {
	//find edtor
	EditorManager *mgr = EditorGetManager(hWndParent);
	for (size_t i = 0; i < mgr->editorList.length; i++) {
		EDITOR_DATA *ed = *(EDITOR_DATA **) StListGetPtr(&mgr->editorList, i);
		if (ed->file == obj) return ed->hWnd;
	}

	return NULL;
}

int EditorIsValid(HWND hWndMgr, HWND hWnd) {
	EditorManager *mgr = EditorGetManager(hWndMgr);

	for (size_t i = 0; i < mgr->editorList.length; i++) {
		EDITOR_DATA *data = *(EDITOR_DATA **) StListGetPtr(&mgr->editorList, i);
		if (data->hWnd == hWnd) return 1;
	}

	// not found
	return 0;
}

int EditorGetType(HWND hWnd) {
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	if (data == NULL) return FILE_TYPE_INVALID; // not an editor

	return data->file->type;
}


static LPCWSTR EditorStatusToString(int status) {
	switch (status) {
		case EDITOR_STATUS_CANCELLED:
			return L"Canceled";
	}
	return ObjStatusToString(status);
}


static BOOL CALLBACK EditorSetThemeProc(HWND hWnd, LPARAM lParam) {
	SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
	return TRUE;
}

static EDITOR_CLASS *EditorGetClass(HWND hWnd) {
	return (EDITOR_CLASS *) GetClassLongPtr(hWnd, EDITOR_CD_CLASSINFO);
}

static void EditorHandleMenu(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	//get editor data
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	HWND hWndMain = data->editorMgr->hWnd;

	//process editor menu common messages
	switch (LOWORD(wParam)) {
		case ID_VIEW_GRIDLINES:
		{
			int state = GetMenuState(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_BYCOMMAND);
			state = !state;

			if (state) {
				CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_CHECKED);
			} else {
				CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
			}
			data->showBorders = state;
			break;
		}
		case ID_ZOOM_100:
		case ID_ZOOM_200:
		case ID_ZOOM_400:
		case ID_ZOOM_800:
		case ID_ZOOM_1600:
		case ID_ZOOM_3200:
		case ID_ZOOM_6400:
		{
			int scale = EditorGetZoomByMenuCommand(LOWORD(wParam));
			data->scalePrev = data->scale;
			data->scale = scale;

			EditorUpdateZoomMenu(data);
			SendMessage(hWnd, NV_ZOOMUPDATED, data->scale, 0);
			break;
		}
		case ID_EDIT_COMMENT:
		{
			OBJECT_HEADER *obj = EditorGetObject(hWnd);
			WCHAR textBuffer[256] = { 0 };
			if (obj != NULL && obj->comment != NULL) {
				for (unsigned int i = 0; i < strlen(obj->comment); i++) {
					textBuffer[i] = (WCHAR) obj->comment[i];
				}
			}

			int result = PromptUserText(hWndMain, L"File Comment", L"Comment:", textBuffer, sizeof(textBuffer));
			if (result) {
				int len = wcslen(textBuffer);

				if (obj->comment != NULL) free(obj->comment);
				obj->comment = calloc(len + 1, 1);
				for (int i = 0; i < len; i++) {
					obj->comment[i] = (char) textBuffer[i];
				}
			}
			break;
		}
	}
}

static void EditorHandleActivate(HWND hWnd, HWND to) {
	if (hWnd != to) return; //focusing away from this window

	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	HWND hWndMain = data->editorMgr->hWnd;
	HMENU hMenu = GetMenu(hWndMain);
	int features = data->cls->features;

	//enable/disable menus depending on supported features
	if (features & EDITOR_FEATURE_GRIDLINES) {
		EnableMenuItem(hMenu, ID_VIEW_GRIDLINES, MF_ENABLED);
	} else {
		CheckMenuItem(hMenu, ID_VIEW_GRIDLINES, MF_UNCHECKED);
		EnableMenuItem(hMenu, ID_VIEW_GRIDLINES, MF_DISABLED);
	}

	if (features & EDITOR_FEATURE_ZOOM) {
		//enable zoom levels
		for (int i = 0; i < sizeof(sZoomMenuIds) / sizeof(sZoomMenuIds[0]); i++) {
			EnableMenuItem(hMenu, sZoomMenuIds[i], MF_ENABLED);
		}
	} else {
		//uncheck and disable zoom levels
		for (int i = 0; i < sizeof(sZoomMenuIds) / sizeof(sZoomMenuIds[0]); i++) {
			CheckMenuItem(hMenu, sZoomMenuIds[i], MF_UNCHECKED);
			EnableMenuItem(hMenu, sZoomMenuIds[i], MF_DISABLED);
		}
	}

	if (features & EDITOR_FEATURE_UNDO) {
		EnableMenuItem(hMenu, ID_EDIT_UNDO, MF_ENABLED);
		EnableMenuItem(hMenu, ID_EDIT_REDO, MF_ENABLED);
	} else {
		EnableMenuItem(hMenu, ID_EDIT_UNDO, MF_DISABLED);
		EnableMenuItem(hMenu, ID_EDIT_REDO, MF_DISABLED);
	}

	//if we support gridlines, handle gridlines checkbox
	if (features & EDITOR_FEATURE_GRIDLINES) {
		if (data->showBorders) {
			CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_CHECKED);
		} else {
			CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
		}
		InvalidateRect(hWnd, NULL, FALSE);
	}

	//if we support zoom, handle zoom checkboxes
	if (features & EDITOR_FEATURE_ZOOM) {
		EditorUpdateZoomMenu(data);
		InvalidateRect(hWnd, NULL, FALSE);
	}
}

static void EditorTerminateCombo(EDITOR_DATA *data) {
	HWND hWndParent = (HWND) GetWindowLongPtr((HWND) GetWindowLongPtr(data->hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	COMBO2D *combo = (COMBO2D *) data->file->combo;

	//first unlink all child objects. This will prevent us from accidentally infinitely
	//recursing as each editor tries to close each other.
	int nLinks = combo->links.length;
	while (nLinks > 0) {
		//unlink
		OBJECT_HEADER *obj;
		StListGet(&combo->links, 0, &obj);
		combo2dUnlink(combo, obj);
		nLinks--;

		//skip own object (already closing)
		if (obj == data->file) continue;

		//find the window associated with the object and close it
		HWND hWndEditor = EditorFindByObject(hWndParent, obj);
		if (hWndEditor != NULL) {
			//editor found, send close message
			SendMessage(hWndEditor, WM_CLOSE, 0, 0);
			//if (IsWindow(hWndEditor)) {
				//window still exists, close fail
				//break;
			//}
		} else {
			//no editor found, free the object
			ObjFree(obj);
		}
	}
}

static LRESULT CALLBACK EditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	//check class long for initialized. If so, pass to window proc
	EDITOR_CLASS *cls = EditorGetClass(hWnd);
	if (cls != NULL && cls->lpfnWndProc != NULL) {
		//check data exists. If not, create it here.
		int wndInited = GetWindowLongPtr(hWnd, EDITOR_WD_INITIALIZED);
		if (!wndInited) {
			size_t dataSize = cls->dataSize;
			EDITOR_DATA *data = (EDITOR_DATA *) calloc(1, dataSize);
			SetWindowLongPtr(hWnd, EDITOR_WD_DATA, (LONG_PTR) data);
			SetWindowLongPtr(hWnd, EDITOR_WD_INITIALIZED, 1);
			data->hWnd = hWnd;
			data->cls = cls;

			//initialize destroy callback
			StListCreate(&data->destroyCallbacks, sizeof(EditorDestroyCallbackEntry), NULL);

			//add window to the editor list
			HWND hWndMain = (HWND) GetWindowLongPtr((HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
			EditorManager *mgr = EditorGetManager(hWndMain);
			StListAdd(&mgr->editorList, &data);
			data->editorMgr = mgr;
		}

		//handle common editor messages
		EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
		EditorManager *mgr = data == NULL ? NULL : data->editorMgr;
		switch (msg) {
			case NV_GETTYPE:
				if (data != NULL && data->file != NULL) {
					return data->file->type;
				} else {
					return FILE_TYPE_INVALID;
				}
			case WM_COMMAND:
				//common command messages
				if (lParam == 0 && HIWORD(wParam) == 0) {
					//menu command message, handle basic ones
					EditorHandleMenu(hWnd, wParam, lParam);
				}
				break;
			case WM_MDIACTIVATE:
				EditorHandleActivate(hWnd, (HWND) lParam);
				break;
			case WM_CLOSE:
				//if marked dirty, give alert
				if (data->dirty) {
					WCHAR buf[128];
					wsprintfW(buf, L"Unsaved Changes - %s", cls->title);
					int status = MessageBox(hWnd, L"You have unsaved changes. Close anyway?", buf, MB_ICONWARNING | MB_YESNO);

					if (status == IDNO) {
						return 0;
					}
				}

				//if editor is editing a combination object, close out editors for the other objects.
				if (data->file->combo != NULL) {
					EditorTerminateCombo(data);
				}
				break;
			case NV_ZOOMIN:
				EditorZoomIn(data);
				break;
			case NV_ZOOMOUT:
				EditorZoomOut(data);
				break;
			case WM_MOUSEWHEEL:
				//handle zoom via ctrl+scroll
				if (LOWORD(wParam) & MK_CONTROL) {
					if (cls->features & EDITOR_FEATURE_ZOOM) {
						int delta = GET_WHEEL_DELTA_WPARAM(wParam);

						HWND hWndMain = data->editorMgr->hWnd;
						if (delta > 0) {
							//scroll down
							EditorZoomIn(data);
						} else if (delta < 0) {
							//scroll up
							EditorZoomOut(data);
						}
					}
				}
				break;
#if(g_useDarkTheme)
			case WM_CTLCOLORSTATIC:
			case WM_CTLCOLORBTN:
				SetBkMode((HDC) wParam, TRANSPARENT);
				SetTextColor((HDC) wParam, RGB(255, 255, 255));
				return (LRESULT) (HBRUSH) GetClassLongPtr(hWnd, GCL_HBRBACKGROUND);
			case WM_CTLCOLORDLG:
				break;
			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORLISTBOX:
				SetBkMode((HDC) wParam, TRANSPARENT);
				SetTextColor((HDC) wParam, RGB(255, 255, 255));
				SelectObject((HDC) wParam, (HPEN) GetClassLongPtr(hWnd, EDITOR_CD_LIGHTPEN));
				return (LRESULT) (HBRUSH) GetClassLongPtr(hWnd, EDITOR_CD_LIGHTBRUSH);
#endif
		}

		//call proc
		LRESULT res = cls->lpfnWndProc(hWnd, msg, wParam, lParam);

		//WM_DESTROY should free data
		if (msg == WM_DESTROY) {
			if (data != NULL) {
				//call all destroy callback subscribers.
				for (size_t i = 0; i < data->destroyCallbacks.length; i++) {
					EditorDestroyCallbackEntry *ent = StListGetPtr(&data->destroyCallbacks, i);
					ent->callback(data, ent->param);
				}

				//free callback list
				StListFree(&data->destroyCallbacks);

				if (ObjIsValid(data->file)) ObjFree(data->file);
				SetWindowLongPtr(hWnd, EDITOR_WD_DATA, 0);

				//remove editor from the editor list
				EditorManager *mgr = data->editorMgr;
				size_t idx = StListIndexOf(&mgr->editorList, &data);
				if (idx <= ST_INDEX_MAX) {
					StListRemove(&mgr->editorList, idx);
				}
				free(data->file); // ownership assumed
				free(data);
			}
		}

		//call registered creation callbacks
		if (msg == NV_INITIALIZE && data != NULL && data->file != NULL) {
			//run through the list of callbacks
			for (size_t i = 0; i < mgr->createCallbacks.length; i++) {
				EditorCreateCallbackEntry *ent = StListGetPtr(&mgr->createCallbacks, i);
				if (ent->type == data->file->type) {
					ent->callback(data, ent->param);
				}
			}
		}

		//WM_CREATE set theme
		if (msg == WM_CREATE) {
#if(g_useDarkTheme)
			EnumChildWindows(hWnd, EditorSetThemeProc, 0);
#endif
		}

		return res;
	}

	//else, this is a dummy window. pass through.
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

EDITOR_CLASS *EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize, int features) {
	EDITOR_CLASS *ec = (EDITOR_CLASS *) calloc(1, sizeof(EDITOR_CLASS));
	if (ec == NULL) return NULL;

	//register editor-common window class.
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme ? CreateSolidBrush(RGB(32, 32, 32)) : (HBRUSH) (COLOR_BTNFACE + 1);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = lpszClassName;
	wcex.lpfnWndProc = EditorWndProc;
	wcex.cbWndExtra = EDITOR_WD_SIZE;
	wcex.cbClsExtra = EDITOR_CD_SIZE;
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	ATOM aClass = RegisterClassEx(&wcex);

	if (!aClass) {
		//class registration failure
		free(ec);
		return NULL;
	}

	//if success, set class data
	HWND hWndDummy = CreateWindow(lpszClassName, L"", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
	ec->hLightBrush = CreateSolidBrush(RGB(51, 51, 51));
	ec->hLightPen = CreatePen(PS_SOLID, 1, RGB(51, 51, 51));
	ec->title = title;
	ec->dataSize = dataSize;
	ec->features = features;
	SetClassLongPtr(hWndDummy, EDITOR_CD_CLASSINFO, (LONG_PTR) ec); // mark initialized
	DestroyWindow(hWndDummy);

	ec->aclass = aClass;
	StMapCreate(&ec->filters, sizeof(int), sizeof(EditorFilter));

	ec->lpfnWndProc = lpfnWndProc; // mark ready
	return ec;
}

void EditorAddFilter(EDITOR_CLASS *cls, int format, LPCWSTR extension, LPCWSTR filter) {
	//add filter
	EditorFilter filterExt;
	filterExt.extension = extension;
	filterExt.filter = filter;
	StMapPut(&cls->filters, &format, &filterExt);
}

static int EditorGetFilterLength(LPCWSTR filter) {
	const WCHAR *pos = filter;
	const WCHAR *end = filter;
	while (1) {
		while (*pos) pos++;

		//first NUL character, 2 indicates the end of the filter
		pos++;
		if (*pos == L'\0') {
			pos++;
			return pos - filter;
		}
	}
}

static int EditorSaveInternal(HWND hWnd);
static int EditorSaveAsInternal(HWND hWnd);

static int EditorSaveAsInternal(HWND hWnd) {
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);

	int format = data->file->format;
	EditorFilter filterExt = { 0 };
	StStatus status = StMapGet(&data->cls->filters, &format, &filterExt);
	if (!ST_SUCCEEDED(status)) {
		int format0 = 0;
		StMapGet(&data->cls->filters, &format0, &filterExt);
	}

	//append generic All Files filter
	LPWSTR dlgFilter = NULL;
	if (filterExt.filter != NULL) {
		LPCWSTR suffix = L"All Files\0*.*\0";
		int baselen = EditorGetFilterLength(filterExt.filter);
		int suffixlen = EditorGetFilterLength(suffix);

		dlgFilter = (WCHAR *) calloc(baselen - 1 + suffixlen, sizeof(WCHAR));
		memcpy(dlgFilter, filterExt.filter, (baselen - 1) * sizeof(WCHAR));
		memcpy(dlgFilter + baselen - 1, suffix, suffixlen * sizeof(WCHAR));
	}

	LPWSTR path = saveFileDialog(hWnd, L"Save As...", dlgFilter, filterExt.extension);
	if (dlgFilter != NULL) free(dlgFilter);
	if (path == NULL) return EDITOR_STATUS_CANCELLED;

	EditorSetFile(hWnd, path);
	free(path);
	return EditorSaveInternal(hWnd);
}

static int EditorSaveInternal(HWND hWnd) {
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	EDITOR_CLASS *cls = EditorGetClass(hWnd);

	//do we need to open a Save As dialog?
	if (data->szOpenFile[0] == L'\0') {
		return EditorSaveAsInternal(hWnd);
	}

	//if the saved object is combo type, set file for all objects.
	if (data->file->combo != NULL) {
		COMBO2D *combo = (COMBO2D *) data->file->combo;

		for (size_t i = 0; i < combo->links.length; i++) {
			OBJECT_HEADER *obj = *(OBJECT_HEADER **) StListGetPtr(&combo->links, i);
			if (obj == data->file) continue; // skip current file

			HWND hWndEditor = EditorFindByObject(data->editorMgr->hWnd, obj);
			EditorSetFile(hWndEditor, data->szOpenFile);
		}
	}

	//else save
	ObjUpdateLinks(data->file, ObjGetFileNameFromPath(data->szOpenFile));
	data->dirty = 0;
	return ObjWriteFile(data->file, data->szOpenFile);
}

static void EditorSaveStatus(HWND hWnd, int status) {
	switch (status) {
		case OBJ_STATUS_SUCCESS:
		case EDITOR_STATUS_CANCELLED:
			//do not display an error message
			break;
		default:
		{
			WCHAR textbuf[64];
			wsprintfW(textbuf, L"Save failed with error: %s\n", EditorStatusToString(status));
			MessageBox(hWnd, textbuf, L"Error", MB_ICONERROR);
		}
	}

}

int EditorSaveAs(HWND hWnd) {
	int status = EditorSaveAsInternal(hWnd);

	//if the status was not successful, display it to the user.
	EditorSaveStatus(hWnd, status);

	return status;
}

int EditorSave(HWND hWnd) {
	int status = EditorSaveInternal(hWnd);

	//if the status was not successful, display it to the user.
	EditorSaveStatus(hWnd, status);

	return status;
}

void EditorSetFile(HWND hWnd, LPCWSTR filename) {
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	EDITOR_CLASS *cls = EditorGetClass(hWnd);
	LPCWSTR title = cls->title;

	//if global config dictates, use only file name
	LPCWSTR fullname = filename;
	if (!g_configuration.fullPaths) filename = GetFileName(filename);

	//if file is 0-length or NULL, just use title. Else, use "file - title"
	if (filename == NULL || filename[0] == L'\0') {
		SendMessage(hWnd, WM_SETTEXT, wcslen(title), (LPARAM) title);
		memset(data->szOpenFile, 0, sizeof(data->szOpenFile));
	} else {
		int filelen = wcslen(filename), titlelen = wcslen(title);
		int totallen = filelen + titlelen + 3;
		WCHAR *buffer = (WCHAR *) calloc(totallen + 1, sizeof(WCHAR));

		memcpy(buffer, filename, filelen * sizeof(WCHAR));
		memcpy(buffer + filelen, L" - ", 3 * sizeof(WCHAR));
		memcpy(buffer + filelen + 3, title, (titlelen + 1) * sizeof(WCHAR));
		SendMessage(hWnd, WM_SETTEXT, totallen, (LPARAM) buffer);
		free(buffer);

		memcpy(data->szOpenFile, fullname, (wcslen(fullname) + 1) * sizeof(WCHAR));
	}
}

void *EditorGetData(HWND hWnd) {
	//if not initialized, no data exists.
	int hasData = GetWindowLongPtr(hWnd, EDITOR_WD_INITIALIZED);
	if (!hasData) return NULL;

	return (void *) GetWindowLongPtr(hWnd, EDITOR_WD_DATA);
}

void EditorSetData(HWND hWnd, void *data) {
	SetWindowLongPtr(hWnd, EDITOR_WD_DATA, (LONG_PTR) data);
}

OBJECT_HEADER *EditorGetObject(HWND hWnd) {
	EDITOR_DATA *data = EditorGetData(hWnd);
	if (data == NULL) return NULL;

	OBJECT_HEADER *obj = data->file;
	if (!ObjIsValid(obj)) return NULL;
	return obj;
}

HWND EditorCreate(LPCWSTR lpszClassName, int x, int y, int width, int height, HWND hWndParent) {
	//create
	DWORD dwExStyle = WS_EX_CLIENTEDGE | WS_EX_MDICHILD;
	DWORD dwStyle = WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN;
	HWND hWnd = CreateWindowEx(dwExStyle, lpszClassName, L"", dwStyle, x, y, width, height, hWndParent, NULL, NULL, NULL);
	ShowWindow(hWnd, SW_HIDE);

	EDITOR_CLASS *cls = EditorGetClass(hWnd);
	LPCWSTR title = cls->title;
	SendMessage(hWnd, WM_SETTEXT, wcslen(title), (LPARAM) title);

	//show (only if size is specified, else remain hidden)
	if (width && height) ShowWindow(hWnd, SW_SHOW);
	return hWnd;
}

void EditorRegisterDestroyCallback(EDITOR_DATA *data, EditorDestroyCallback callback, void *param) {
	//add to callback list
	EditorDestroyCallbackEntry ent;
	ent.callback = callback;
	ent.param = param;
	StListAdd(&data->destroyCallbacks, &ent);
}

void EditorRemoveDestroyCallback(EDITOR_DATA *data, EditorDestroyCallback callback, void *param) {
	//search for and remove from list
	EditorDestroyCallbackEntry ent;
	ent.callback = callback;
	ent.param = param;

	size_t index = StListIndexOf(&data->destroyCallbacks, &ent);
	if (index != ST_INDEX_NOT_FOUND) {
		StListRemove(&data->destroyCallbacks, index);
	}
}
