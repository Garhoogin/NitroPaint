#include <Shlwapi.h>
#include <Uxtheme.h>

#include "nitropaint.h"
#include "resource.h"
#include "editor.h"

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
		{
			int scale = 1;
			if (LOWORD(wParam) == ID_ZOOM_100) scale = 1;
			if (LOWORD(wParam) == ID_ZOOM_200) scale = 2;
			if (LOWORD(wParam) == ID_ZOOM_400) scale = 4;
			if (LOWORD(wParam) == ID_ZOOM_800) scale = 8;
			data->scale = scale;

			int checkBox = ID_ZOOM_100;
			if (scale == 2) {
				checkBox = ID_ZOOM_200;
			} else if (scale == 4) {
				checkBox = ID_ZOOM_400;
			} else if (scale == 8) {
				checkBox = ID_ZOOM_800;
			}
			int ids[] = { ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800 };
			for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
				int id = ids[i];
				CheckMenuItem(GetMenu(getMainWindow(hWnd)), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
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
	} else {
		CheckMenuItem(hMenu, ID_ZOOM_100, MF_UNCHECKED);
		CheckMenuItem(hMenu, ID_ZOOM_200, MF_UNCHECKED);
		CheckMenuItem(hMenu, ID_ZOOM_400, MF_UNCHECKED);
		CheckMenuItem(hMenu, ID_ZOOM_800, MF_UNCHECKED);
		EnableMenuItem(hMenu, ID_ZOOM_100, MF_DISABLED);
		EnableMenuItem(hMenu, ID_ZOOM_200, MF_DISABLED);
		EnableMenuItem(hMenu, ID_ZOOM_400, MF_DISABLED);
		EnableMenuItem(hMenu, ID_ZOOM_800, MF_DISABLED);
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
		}
		int ids[] = { ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800 };
		for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
			int id = ids[i];
			CheckMenuItem(GetMenu(hWndMain), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
		}
		InvalidateRect(hWnd, NULL, FALSE);
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
			void *data = calloc(1, dataSize);
			SetWindowLongPtr(hWnd, EDITOR_WD_DATA, (LONG_PTR) data);
			SetWindowLongPtr(hWnd, EDITOR_WD_INITIALIZED, 1);
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
			void *data = (void *) GetWindowLongPtr(hWnd, EDITOR_WD_DATA);
			if (data != NULL) {
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

ATOM EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize, int features) {
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
		HWND hWndDummy = CreateWindow(lpszClassName, L"", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
		SetClassLongPtr(hWndDummy, EDITOR_CD_TITLE, (LONG_PTR) title);
		SetClassLongPtr(hWndDummy, EDITOR_CD_WNDPROC, (LONG_PTR) lpfnWndProc);
		SetClassLongPtr(hWndDummy, EDITOR_CD_DATA_SIZE, dataSize);
		SetClassLongPtr(hWndDummy, EDITOR_CD_LIGHTBRUSH, (LONG_PTR) CreateSolidBrush(RGB(51, 51, 51)));
		SetClassLongPtr(hWndDummy, EDITOR_CD_LIGHTPEN, (LONG_PTR) CreatePen(PS_SOLID, 1, RGB(51, 51, 51)));
		SetClassLongPtr(hWndDummy, EDITOR_CD_FEATURES, features);
		DestroyWindow(hWndDummy);
	}

	return aClass;
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

HWND EditorCreate(LPCWSTR lpszClassName, int x, int y, int width, int height, HWND hWndParent) {
	//create
	DWORD dwExStyle = WS_EX_CLIENTEDGE | WS_EX_MDICHILD;
	DWORD dwStyle = WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN;
	HWND hWnd = CreateWindowEx(dwExStyle, lpszClassName, L"", dwStyle, x, y, width, height, hWndParent, NULL, NULL, NULL);
	LPCWSTR title = (LPCWSTR) GetClassLongPtr(hWnd, EDITOR_CD_TITLE);
	SendMessage(hWnd, WM_SETTEXT, wcslen(title), (LPARAM) title);

	//show
	ShowWindow(hWnd, SW_SHOW);
	return hWnd;
}
