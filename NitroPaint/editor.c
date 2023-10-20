#include <Shlwapi.h>
#include <Uxtheme.h>

#include "nitropaint.h"
#include "editor.h"

static BOOL CALLBACK EditorSetThemeProc(HWND hWnd, LPARAM lParam) {
	SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
	return TRUE;
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

ATOM EditorRegister(LPCWSTR lpszClassName, WNDPROC lpfnWndProc, LPCWSTR title, size_t dataSize) {
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
