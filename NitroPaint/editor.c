#include <Shlwapi.h>
#include <Uxtheme.h>

#include "nitropaint.h"
#include "resource.h"
#include "editor.h"
#include "combo2d.h"

typedef struct EditorDestroyCallbackEntry_ {
	EditorDestroyCallback callback;
	void *param;
} EditorDestroyCallbackEntry;


static BOOL CALLBACK EditorSetThemeProc(HWND hWnd, LPARAM lParam) {
	SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
	return TRUE;
}

static void EditorHandleMenu(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	//get editor data
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);

	//process editor menu common messages
	switch (LOWORD(wParam)) {
		case ID_VIEW_GRIDLINES:
		{
			HWND hWndMain = getMainWindow(hWnd);
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
		{
			int scale = MainGetZoomByCommand(LOWORD(wParam));
			data->scalePrev = data->scale;
			data->scale = scale;

			int checkBox = MainGetZoomCommand(scale);
			int other = MainGetZoomCommand(data->scalePrev);
			CheckMenuItem(GetMenu(getMainWindow(hWnd)), other, MF_UNCHECKED);
			CheckMenuItem(GetMenu(getMainWindow(hWnd)), checkBox, MF_CHECKED);
			break;
		}
		case ID_EDIT_COMMENT:
		{
			HWND hWndMain = getMainWindow(hWnd);
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

	HWND hWndMain = getMainWindow(hWnd);
	HMENU hMenu = GetMenu(hWndMain);
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	int features = GetClassLong(hWnd, EDITOR_CD_FEATURES);

	//enable/disable menus depending on supported features
	if (features & EDITOR_FEATURE_GRIDLINES) {
		EnableMenuItem(hMenu, ID_VIEW_GRIDLINES, MF_ENABLED);
	} else {
		CheckMenuItem(hMenu, ID_VIEW_GRIDLINES, MF_UNCHECKED);
		EnableMenuItem(hMenu, ID_VIEW_GRIDLINES, MF_DISABLED);
	}

	if (features & EDITOR_FEATURE_ZOOM) {
		EnableMenuItem(hMenu, ID_ZOOM_100, MF_ENABLED);
		EnableMenuItem(hMenu, ID_ZOOM_200, MF_ENABLED);
		EnableMenuItem(hMenu, ID_ZOOM_400, MF_ENABLED);
		EnableMenuItem(hMenu, ID_ZOOM_800, MF_ENABLED);
		EnableMenuItem(hMenu, ID_ZOOM_1600, MF_ENABLED);
	} else {
		CheckMenuItem(hMenu, ID_ZOOM_100, MF_UNCHECKED);
		CheckMenuItem(hMenu, ID_ZOOM_200, MF_UNCHECKED);
		CheckMenuItem(hMenu, ID_ZOOM_400, MF_UNCHECKED);
		CheckMenuItem(hMenu, ID_ZOOM_800, MF_UNCHECKED);
		CheckMenuItem(hMenu, ID_ZOOM_1600, MF_UNCHECKED);
		EnableMenuItem(hMenu, ID_ZOOM_100, MF_DISABLED);
		EnableMenuItem(hMenu, ID_ZOOM_200, MF_DISABLED);
		EnableMenuItem(hMenu, ID_ZOOM_400, MF_DISABLED);
		EnableMenuItem(hMenu, ID_ZOOM_800, MF_DISABLED);
		EnableMenuItem(hMenu, ID_ZOOM_1600, MF_DISABLED);
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
		int checkBox = ID_ZOOM_100;
		if (data->scale == 2) {
			checkBox = ID_ZOOM_200;
		} else if (data->scale == 4) {
			checkBox = ID_ZOOM_400;
		} else if (data->scale == 8) {
			checkBox = ID_ZOOM_800;
		} else if (data->scale == 16) {
			checkBox = ID_ZOOM_1600;
		}
		int ids[] = { ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800, ID_ZOOM_1600 };
		for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
			int id = ids[i];
			CheckMenuItem(GetMenu(hWndMain), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
		}
		InvalidateRect(hWnd, NULL, FALSE);
	}
}

// ----- editor find

typedef struct EditorFindStruct_ {
	OBJECT_HEADER *obj;
	HWND hWnd;
} EditorFindStruct;

static BOOL CALLBACK EditorFindByObjectEnumProc(HWND hWnd, LPARAM lParam) {
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	if (data == NULL) return TRUE;

	EditorFindStruct *find = (EditorFindStruct *) lParam;
	if (&data->file == find->obj) {
		//found
		find->hWnd = hWnd;
		return FALSE;
	}
	return TRUE;
}

static HWND EditorFindByObject(HWND hWndParent, OBJECT_HEADER *obj) {
	//enumerate child windows to find
	EditorFindStruct findStruct;
	findStruct.obj = obj;
	findStruct.hWnd = NULL;

	EnumChildWindows(hWndParent, EditorFindByObjectEnumProc, (LPARAM) &findStruct);
	return findStruct.hWnd;
}

static void EditorTerminateCombo(EDITOR_DATA *data) {
	HWND hWndParent = (HWND) GetWindowLongPtr(data->hWnd, GWL_HWNDPARENT);
	COMBO2D *combo = (COMBO2D *) data->file.combo;

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
		if (obj == &data->file) continue;

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
	int inited = GetClassLongPtr(hWnd, EDITOR_CD_INITIALIZED);
	if (inited) {
		WNDPROC proc = (WNDPROC) GetClassLongPtr(hWnd, EDITOR_CD_WNDPROC);

		//check data exists. If not, create it here.
		int wndInited = GetWindowLongPtr(hWnd, EDITOR_WD_INITIALIZED);
		if (!wndInited) {
			size_t dataSize = GetClassLongPtr(hWnd, EDITOR_CD_DATA_SIZE);
			EDITOR_DATA *data = (EDITOR_DATA *) calloc(1, dataSize);
			SetWindowLongPtr(hWnd, EDITOR_WD_DATA, (LONG_PTR) data);
			SetWindowLongPtr(hWnd, EDITOR_WD_INITIALIZED, 1);
			data->hWnd = hWnd;

			//initialize destroy callback
			StListCreate(&data->destroyCallbacks, sizeof(EditorDestroyCallbackEntry), NULL);
		}

		//handle common editor messages
		EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
		switch (msg) {
			case NV_GETTYPE:
				if (data != NULL) {
					return data->file.type;
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
					wsprintfW(buf, L"Unsaved Changes - %s", GetClassLongPtr(hWnd, EDITOR_CD_TITLE));
					int status = MessageBox(hWnd, L"You have unsaved changes. Close anyway?", buf, MB_ICONWARNING | MB_YESNO);

					if (status == IDNO) {
						return 0;
					}
				}

				//if editor is editing a combination object, close out editors for the other objects.
				if (data->file.combo != NULL) {
					EditorTerminateCombo(data);
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
		LRESULT res = proc(hWnd, msg, wParam, lParam);

		//WM_DESTROY should free data
		if (msg == WM_DESTROY) {
			EDITOR_DATA *data = (EDITOR_DATA *) GetWindowLongPtr(hWnd, EDITOR_WD_DATA);
			if (data != NULL) {
				//call all destroy callback subscribers.
				for (size_t i = 0; i < data->destroyCallbacks.length; i++) {
					EditorDestroyCallbackEntry *ent = StListGetPtr(&data->destroyCallbacks, i);
					ent->callback(data, ent->param);
				}

				//free callback list
				StListFree(&data->destroyCallbacks);

				if (ObjIsValid(&data->file)) ObjFree(&data->file);
				SetWindowLongPtr(hWnd, EDITOR_WD_DATA, 0);
				free(data);
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

	//else, this is a dummy window.
	//on the first WM_DESTORY, mark class as initialized.
	if (msg == WM_DESTROY) {
		SetClassLong(hWnd, EDITOR_CD_INITIALIZED, 1);
	}

	//pass through
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

EDITOR_CLASS *EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize, int features) {
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

	//if success, set class data
	if (aClass) {
		EDITOR_CLASS *ec = (EDITOR_CLASS *) calloc(1, sizeof(EDITOR_CLASS));
		HWND hWndDummy = CreateWindow(lpszClassName, L"", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
		SetClassLongPtr(hWndDummy, EDITOR_CD_TITLE, (LONG_PTR) title);
		SetClassLongPtr(hWndDummy, EDITOR_CD_WNDPROC, (LONG_PTR) lpfnWndProc);
		SetClassLongPtr(hWndDummy, EDITOR_CD_DATA_SIZE, dataSize);
		SetClassLongPtr(hWndDummy, EDITOR_CD_LIGHTBRUSH, (LONG_PTR) CreateSolidBrush(RGB(51, 51, 51)));
		SetClassLongPtr(hWndDummy, EDITOR_CD_LIGHTPEN, (LONG_PTR) CreatePen(PS_SOLID, 1, RGB(51, 51, 51)));
		SetClassLongPtr(hWndDummy, EDITOR_CD_FEATURES, features);
		SetClassLongPtr(hWndDummy, EDITOR_CD_CLASSINFO, (LONG_PTR) ec);
		DestroyWindow(hWndDummy);

		ec->aclass = aClass;
		ec->nFilters = 0;
		ec->filters = ec->extensions = NULL;
		return ec;
	}

	return NULL;
}

void EditorAddFilter(EDITOR_CLASS *cls, int format, LPCWSTR extension, LPCWSTR filter) {
	//if format not within the bounds, add it.
	if (format >= cls->nFilters) {
		cls->extensions = (LPCWSTR *) realloc((LPWSTR *) cls->extensions, (format + 1) * sizeof(LPCWSTR *));
		cls->filters = (LPCWSTR *) realloc((LPWSTR *) cls->filters, (format + 1) * sizeof(LPCWSTR *));
		for (int i = cls->nFilters; i < format; i++) {
			cls->extensions[i] = NULL;
			cls->filters[i] = NULL;
		}
		cls->nFilters = format + 1;
	}
	cls->extensions[format] = extension;
	cls->filters[format] = filter;
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

int EditorSaveAs(HWND hWnd) {
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	EDITOR_CLASS *cls = (EDITOR_CLASS *) GetClassLong(hWnd, EDITOR_CD_CLASSINFO);

	int format = data->file.format;
	LPCWSTR filter = format < cls->nFilters ? cls->filters[format] : NULL;
	LPCWSTR extension = format < cls->nFilters ? cls->extensions[format] : NULL;
	if (filter == NULL && cls->nFilters > 0) filter = cls->filters[0];
	if (extension == NULL && cls->nFilters > 0) extension = cls->extensions[0];

	//append generic All Files filter
	LPWSTR dlgFilter = NULL;
	if (filter != NULL) {
		LPCWSTR suffix = L"All Files\0*.*\0";
		int baselen = EditorGetFilterLength(filter);
		int suffixlen = EditorGetFilterLength(suffix);

		dlgFilter = (WCHAR *) calloc(baselen - 1 + suffixlen, sizeof(WCHAR));
		memcpy(dlgFilter, filter, (baselen - 1) * sizeof(WCHAR));
		memcpy(dlgFilter + baselen - 1, suffix, suffixlen * sizeof(WCHAR));
	}

	LPWSTR path = saveFileDialog(hWnd, L"Save As...", dlgFilter, extension);
	if (dlgFilter != NULL) free(dlgFilter);
	if (path == NULL) return EDITOR_STATUS_CANCELLED;

	EditorSetFile(hWnd, path);
	free(path);
	return EditorSave(hWnd);
}

int EditorSave(HWND hWnd) {
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);
	EDITOR_CLASS *cls = (EDITOR_CLASS *) GetClassLong(hWnd, EDITOR_CD_CLASSINFO);

	//do we need to open a Save As dialog?
	if (data->szOpenFile[0] == L'\0') {
		return EditorSaveAs(hWnd);
	}

	//else save
	ObjUpdateLinks(&data->file, ObjGetFileNameFromPath(data->szOpenFile));
	data->dirty = 0;
	if (data->file.writer != NULL) return ObjWriteFile(data->szOpenFile, &data->file, data->file.writer);

	//needs to be written
	return OBJ_STATUS_UNSUPPORTED;
}

void EditorSetFile(HWND hWnd, LPCWSTR filename) {
	LPCWSTR title = (LPCWSTR) GetClassLongPtr(hWnd, EDITOR_CD_TITLE);
	EDITOR_DATA *data = (EDITOR_DATA *) EditorGetData(hWnd);

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

	OBJECT_HEADER *obj = &data->file;
	if (!ObjIsValid(obj)) return NULL;
	return obj;
}

HWND EditorCreate(LPCWSTR lpszClassName, int x, int y, int width, int height, HWND hWndParent) {
	//create
	DWORD dwExStyle = WS_EX_CLIENTEDGE | WS_EX_MDICHILD;
	DWORD dwStyle = WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN;
	HWND hWnd = CreateWindowEx(dwExStyle, lpszClassName, L"", dwStyle, x, y, width, height, hWndParent, NULL, NULL, NULL);
	ShowWindow(hWnd, SW_HIDE);

	LPCWSTR title = (LPCWSTR) GetClassLongPtr(hWnd, EDITOR_CD_TITLE);
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
