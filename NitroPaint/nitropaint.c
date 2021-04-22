#include <Windows.h>
#include <CommCtrl.h>
#include <Uxtheme.h>

#include "nitropaint.h"
#include "filecommon.h"
#include "palette.h"
#include "resource.h"
#include "tiler.h"
#include "nsbtxviewer.h"
#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "nscrviewer.h"
#include "ncerviewer.h"
#include "exceptions.h"
#include "gdip.h"
#include "tileeditor.h"
#include "textureeditor.h"
#include "nsbtx.h"
#include "ntft.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dbghelp.lib")

int _fltused;

long _ftol2_sse(float f) { //ugly hack
	return _ftol(f);
}

int __wgetmainargs(int *argc, wchar_t ***argv, wchar_t ***env, int doWildCard, int *startInfo);

HICON g_appIcon = NULL;

LPWSTR g_lpszNitroPaintClassName = L"NitroPaintClass";

extern EXCEPTION_DISPOSITION __cdecl ExceptionHandler(EXCEPTION_RECORD *exceptionRecord, void *establisherFrame, CONTEXT *contextRecord, void *dispatcherContext);

HANDLE g_hEvent = NULL;

LPWSTR saveFileDialog(HWND hWnd, LPWSTR title, LPWSTR filter, LPWSTR extension) {
	OPENFILENAME o = { 0 };
	WCHAR fbuff[MAX_PATH + 1] = { 0 };
	ZeroMemory(&o, sizeof(o));
	o.lStructSize = sizeof(o);
	o.hwndOwner = hWnd;
	o.nMaxFile = MAX_PATH;
	o.lpstrTitle = title;
	o.lpstrFilter = filter;
	o.nMaxCustFilter = 255;
	o.lpstrFile = fbuff;
	o.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	o.lpstrDefExt = extension;

	if (GetSaveFileName(&o)) {

		LPWSTR fname = (LPWSTR) calloc(wcslen(fbuff) + 1, 2);
		memcpy(fname, fbuff, wcslen(fbuff) * 2);
		return fname;
	}
	return NULL;
}

LPWSTR openFileDialog(HWND hWnd, LPWSTR title, LPWSTR filter, LPWSTR extension) {
	OPENFILENAME o = { 0 };
	WCHAR fname[MAX_PATH + 1] = { 0 };
	o.lStructSize = sizeof(o);
	o.hwndOwner = hWnd;
	o.nMaxFile = MAX_PATH;
	o.lpstrTitle = title;
	o.lpstrFilter = filter;
	o.nMaxCustFilter = 255;
	o.lpstrFile = fname;
	o.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	o.lpstrDefExt = extension;
	if (GetOpenFileName(&o)) {
		LPWSTR fname2 = (LPWSTR) calloc(wcslen(fname) + 1, 2);
		memcpy(fname2, fname, wcslen(fname) * 2);
		return fname2;
	}
	return NULL;
}

LPWSTR GetFileName(LPWSTR lpszPath) {
	WCHAR *current = lpszPath;
	while (*lpszPath) {
		WCHAR c = *lpszPath;
		if (c == '\\' || c == '/') current = lpszPath;
		lpszPath++;
	}
	return current + 1;
}

CONFIGURATIONSTRUCT g_configuration;
LPWSTR g_configPath;

WNDPROC OldMdiClientWndProc = NULL;
LRESULT WINAPI NewMdiClientWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_ERASEBKGND:
		{
			if (!g_useDarkTheme) break;
			HDC hDC = (HDC) wParam;

			RECT rc;
			GetClientRect(hWnd, &rc);

			SelectObject(hDC, GetStockObject(NULL_PEN));
			HBRUSH black = CreateSolidBrush(RGB(64, 64, 64));
			HBRUSH oldBrush = SelectObject(hDC, black);
			Rectangle(hDC, 0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1);
			SelectObject(hDC, oldBrush);
			DeleteObject(black);
			return 1;
		}
		//scrollbars on MDI clients are weird. This makes them a little less weird.
		case WM_HSCROLL:
		case WM_VSCROLL:
		{
			WORD ctl = LOWORD(wParam);
			if (ctl == SB_THUMBTRACK) ctl = SB_THUMBPOSITION;
			wParam = (HIWORD(wParam) << 16) | ctl;
			break;
		}
	}
	return OldMdiClientWndProc(hWnd, msg, wParam, lParam);
}

BOOL CALLBACK CascadeChildrenEnumProc(HWND hWnd, LPARAM lParam) {
	LONG style = GetWindowLong(hWnd, GWL_STYLE);
	LONG exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
	if (!(exStyle & WS_EX_MDICHILD)) return TRUE;
	PINT currentPos = (PINT) lParam;

	if (style & WS_MAXIMIZE) ShowWindow(hWnd, SW_RESTORE);//SetWindowLong(hWnd, GWL_STYLE, style & ~WS_MAXIMIZE);
	//MoveWindow(hWnd, *currentPos, *currentPos, 500, 500, TRUE);
	SetWindowPos(hWnd, HWND_TOP, *currentPos, *currentPos, 0, 0, SWP_NOSIZE);
	SetFocus(hWnd);

	*currentPos += 26;
	return TRUE;
}

VOID CascadeChildren(HWND hWndMdi) {
	int cascadeX = 0;
	EnumChildWindows(hWndMdi, CascadeChildrenEnumProc, (LPARAM) &cascadeX);
}

BOOL CALLBACK SaveAllProc(HWND hWnd, LPARAM lParam) {
	HWND hWndMdi = (HWND) lParam;
	if ((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT) != hWndMdi) return TRUE;
	SendMessage(hWnd, WM_COMMAND, ID_FILE_SAVE, 0);
	return TRUE;
}

VOID OpenFileByName(HWND hWnd, LPCWSTR path) {
	NITROPAINTSTRUCT *data = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWnd, 0);
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSize = GetFileSize(hFile, NULL), dwRead;
	LPBYTE buffer = (LPBYTE) calloc(dwSize, 1);
	ReadFile(hFile, buffer, dwSize, &dwRead, NULL);
	CloseHandle(hFile);

	int format = fileIdentify(buffer, dwSize);
	switch (format) {
		case FILE_TYPE_PALETTE:
			//if there is already an NCLR open, close it.
			if (data->hWndNclrViewer) DestroyWindow(data->hWndNclrViewer);
			data->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, path);
			if (data->hWndNcerViewer) InvalidateRect(data->hWndNcerViewer, NULL, FALSE);
			break;
		case FILE_TYPE_CHARACTER:
			//if there is already an NCGR open, close it.
			if (data->hWndNcgrViewer) DestroyWindow(data->hWndNcgrViewer);
			data->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, path);
			if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
			break;
		case FILE_TYPE_SCREEN:
			//if there is already an NSCR open, close it.
			if (data->hWndNscrViewer) DestroyWindow(data->hWndNscrViewer);
			data->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);
			break;
		case FILE_TYPE_CELL:
			//if there is already an NCER open, close it.
			if (data->hWndNcerViewer) DestroyWindow(data->hWndNcerViewer);
			data->hWndNcerViewer = CreateNcerViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);
			if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
			break;
		case FILE_TYPE_NSBTX:
			CreateNsbtxViewer(CW_USEDEFAULT, CW_USEDEFAULT, 450, 350, data->hWndMdi, path);
			break;
		default:
			break;
	}
}

VOID HandleSwitch(LPWSTR lpSwitch) {
	if (!wcsncmp(lpSwitch, L"EVENT:", 6)) {
		g_hEvent = (HANDLE) _wtol(lpSwitch + 6);
	}
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NITROPAINTSTRUCT *data = GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (NITROPAINTSTRUCT *) calloc(1, sizeof(NITROPAINTSTRUCT));
		SetWindowLongPtr(hWnd, 0, (LONG) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			CLIENTCREATESTRUCT createStruct;
			createStruct.hWindowMenu = GetSubMenu(GetMenu(hWnd), 3);
			createStruct.idFirstChild = 200;
			data->hWndMdi = CreateWindowEx(WS_EX_CLIENTEDGE, L"MDICLIENT", L"MDI", WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL, 0, 0, 500, 500, hWnd, NULL, NULL, &createStruct);
#if(g_useDarkTheme)
				SetWindowTheme(data->hWndMdi, L"DarkMode_Explorer", NULL);
#endif

			//subclass the MDI client window.
			OldMdiClientWndProc = SetWindowLongPtr(data->hWndMdi, GWL_WNDPROC, NewMdiClientWndProc);
			DragAcceptFiles(hWnd, TRUE);

			//open command line argument's files
			int argc;
			wchar_t **argv;
			wchar_t **env;
			int *startInfo;
			__wgetmainargs(&argc, &argv, &env, 1, &startInfo);
			if (argc > 1) {
				argc--;
				argv++;
				for (int i = 0; i < argc; i++) {
					LPWSTR arg = argv[i];
					if (arg[0] != L'/') {
						OpenFileByName(hWnd, argv[i]);
					} else {
						//command line switch
						HandleSwitch(arg + 1);
					}
				}
			}

			//check config data
			if (g_configuration.nclrViewerConfiguration.useDSColorPicker) {
				CheckMenuItem(GetMenu(hWnd), ID_VIEW_USE15BPPCOLORCHOOSER, MF_CHECKED);
			}
			if (g_configuration.fullPaths) {
				CheckMenuItem(GetMenu(hWnd), ID_VIEW_FULLFILEPATHS, MF_CHECKED);
			}
			return 1;
		}
		case WM_PAINT:
		{
			break;
		}
		case WM_SIZE:
		{
			MoveWindow(data->hWndMdi, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
			break;
		}
		case WM_DROPFILES:
		{
			HDROP hDrop = (HDROP) wParam;
			int nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
			for (int i = 0; i < nFiles; i++) {
				WCHAR path[MAX_PATH + 1] = { 0 };
				DragQueryFile(hDrop, i, path, MAX_PATH);
				OpenFileByName(hWnd, path);
			}
			DragFinish(hDrop);
			return 0;
		}
		case WM_COMMAND:
		{
			if (HIWORD(wParam) == 0 && lParam == 0) {
				WORD menuID = LOWORD(wParam);
				switch (menuID) {
					case ID_WINDOW_CASCADE:
					{
						CascadeChildren(data->hWndMdi);
						break;
					}
					case ID_FILE_EXIT:
						PostQuitMessage(0);
						break;
					case ID_OPEN_OPENNCLR:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open NCLR", L"NCLR Files (*.nclr)\0*.nclr\0All Files\0*.*\0", L"nclr");
						if (!path) break;

						//if there is already an NCLR open, close it.
						if (data->hWndNclrViewer) DestroyWindow(data->hWndNclrViewer);
						data->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, path);

						if (data->hWndNcerViewer) InvalidateRect(data->hWndNcerViewer, NULL, FALSE);
						free(path);
						break;
					}
					case ID_OPEN_OPENNCBR:
					case ID_OPEN_OPENNCGR:
					{
						LPWSTR path;
						if(menuID == ID_OPEN_OPENNCGR) path = openFileDialog(hWnd, L"Open NCGR", L"NCGR Files (*.ncgr)\0*.ncgr\0All Files\0*.*\0", L"ncgr");
						else path = openFileDialog(hWnd, L"Open NCBR", L"NCBR Files (*.ncbr)\0*.ncbr\0All Files\0*.*\0", L"ncbr");
						if (!path) break;

						//if there is already an NCGR open, close it.
						if (data->hWndNcgrViewer) DestroyWindow(data->hWndNcgrViewer);
						data->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, path);
						if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);

						free(path);
						break;
					}
					case ID_OPEN_OPENNSCR:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open NSCR", L"NSCR Files (*.nscr)\0*.nscr\0All Files\0*.*\0", L"nscr");
						if (!path) break;

						//if there is already an NSCR open, close it.
						if (data->hWndNscrViewer) DestroyWindow(data->hWndNscrViewer);
						data->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);

						free(path);
						break;
					}
					case ID_OPEN_OPENNCER:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open NCER", L"NCER Files (*.ncer)\0*.ncer\0All Files\0*.*\0", L"ncer");
						if (!path) break;

						if (data->hWndNcerViewer) DestroyWindow(data->hWndNcerViewer);
						data->hWndNcerViewer = CreateNcerViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);

						if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
						free(path);
						break;
					}
					case ID_OPEN_OPENNSBTX:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open NSBTX", L"NSBTX Files (*.nsbtx)\0*.nsbtx\0All Files\0*.*\0", L"nsbtx");
						if (!path) break;

						CreateNsbtxViewer(CW_USEDEFAULT, CW_USEDEFAULT, 450, 350, data->hWndMdi, path);

						free(path);
						break;
					}
					case ID_NTFT_IMAGE:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open Image", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
						if (!path) break;
						LPWSTR dest = saveFileDialog(hWnd, L"Save NTFT", L"NTFT Files (*.ntft)\0*.ntft\0All Files\0*.*\0", L"ntft");
						if (!dest) {
							free(path);
							break;
						}

						int width, height;
						NTFT ntft;
						DWORD *bits = gdipReadImage(path, &width, &height);
						ntftCreate(&ntft, bits, width * height);
						ntftWrite(&ntft, dest);
						free(ntft.px);

						free(path);
						free(dest);
						break;
					}
					case ID_NTFT_NTFT:
					{
						//create window
						CreateWindow(L"NtftConvertDialogClass", L"Convert NTFT", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						break;
					}
					case ID_FILE_SAVEALL:
					{
						HWND hWndMdi = data->hWndMdi;
						EnumChildWindows(hWndMdi, SaveAllProc, (LPARAM) hWndMdi);
						break;
					}
					case ID_NEW_NEWNCGR40015: //NCGR+NSCR
					{
						HWND h = CreateWindow(L"CreateDialogClass", L"Create NSCR", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						break;
					}
					case ID_NEW_NEWTEXTURE:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open Image", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
						if (path == NULL) break;

						HWND h = CreateTextureEditor(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
						free(path);
						break;
					}
					case ID_HELP_ABOUT:
					{
						MessageBox(hWnd, L"GUI NCLR/NCGR Editor/NSCR Viewer. Made by Garhoogin with help from Gericom, Xgone, and ProfessorDoktorGamer.", L"About NitroPaint", MB_ICONINFORMATION);
						break;
					}
					case ID_VIEW_USE15BPPCOLORCHOOSER:
					{
						int state = GetMenuState(GetMenu(hWnd), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND);
						state = !state;
						if (state) {
							WritePrivateProfileStringW(L"NclrViewer", L"UseDSColorPicker", L"1", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_USE15BPPCOLORCHOOSER, MF_CHECKED);
						} else {
							WritePrivateProfileStringW(L"NclrViewer", L"UseDSColorPicker", L"0", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_USE15BPPCOLORCHOOSER, MF_UNCHECKED);
						}
						break;
					}
					case ID_VIEW_FULLFILEPATHS:
					{
						int state = GetMenuState(GetMenu(hWnd), ID_VIEW_FULLFILEPATHS, MF_BYCOMMAND);
						state = !state;
						if (state) {
							WritePrivateProfileStringW(L"NitroPaint", L"FullPaths", L"1", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_FULLFILEPATHS, MF_CHECKED);
						} else {
							WritePrivateProfileStringW(L"NitroPaint", L"FullPaths", L"0", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_FULLFILEPATHS, MF_UNCHECKED);
						}
						g_configuration.fullPaths = state;
						break;
					}
				}
			}
			HWND hWndActive = SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, (LPARAM) NULL);
			SendMessage(hWndActive, msg, wParam, lParam);
			break;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}
	if (data->hWndMdi) {
		return DefFrameProc(hWnd, data->hWndMdi, msg, wParam, lParam);
	} else {
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
}

typedef struct {
	HWND nscrCreateInput;
	HWND nscrCreateInputButton;
	HWND nscrCreateDither;
	HWND nscrCreateDropdown;
	HWND nscrCreateButton;
	HWND hWndPaletteInput;
	HWND hWndPalettesInput;
	HWND hWndTileBase;
	HWND hWndFormatDropdown;
} CREATEDIALOGDATA;

BOOL WINAPI SetGUIFontProc(HWND hWnd, LPARAM lParam) {
	HFONT hFont = (HFONT) lParam;
	SendMessage(hWnd, WM_SETFONT, hFont, TRUE);
	return TRUE;
}

VOID SetGUIFont(HWND hWnd) {
	HFONT hFont = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
	EnumChildWindows(hWnd, SetGUIFontProc, (LPARAM) hFont);
}

typedef struct {
	WCHAR szNclrPath[MAX_PATH + 1];
	WCHAR szNcgrPath[MAX_PATH + 1];
	WCHAR szNscrPath[MAX_PATH + 1];
	HWND hWndMain;
	DWORD *bbits;
} CREATENSCRDATA;

#define NV_SETDATA (WM_USER+2)

void nscrCreateCallback(void *data) {
	CREATENSCRDATA *createData = (CREATENSCRDATA *) data;
	HWND hWndMain = createData->hWndMain;
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	HWND hWndMdi = nitroPaintStruct->hWndMdi;

	if (nitroPaintStruct->hWndNscrViewer) DestroyWindow(nitroPaintStruct->hWndNscrViewer);
	if (nitroPaintStruct->hWndNcgrViewer) DestroyWindow(nitroPaintStruct->hWndNcgrViewer);
	if (nitroPaintStruct->hWndNclrViewer) DestroyWindow(nitroPaintStruct->hWndNclrViewer);
	nitroPaintStruct->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, createData->szNclrPath);
	nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, createData->szNcgrPath);
	nitroPaintStruct->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, createData->szNscrPath);

	free(createData->bbits);
	free(data);
}

DWORD WINAPI threadedNscrCreateInternal(LPVOID lpParameter) {
	struct {
		PROGRESSDATA *data;
		DWORD *bbits;
		int width;
		int height;
		int bits;
		int dither;
		CREATENSCRDATA *createData;
		int palette;
		int nPalettes;
		int fmt;
		int tileBase;
	} *params = lpParameter;
	nscrCreate(params->bbits, params->width, params->height, params->bits, params->dither,
			   params->createData->szNclrPath, params->createData->szNcgrPath, params->createData->szNscrPath,
			   params->palette, params->nPalettes, params->fmt, params->tileBase);
	params->data->waitOn = 1;
	return 0;
}

void threadedNscrCreate(PROGRESSDATA *data, DWORD *bbits, int width, int height, int bits, int dither, CREATENSCRDATA *createData, int palette, int nPalettes, int fmt, int tileBase) {
	struct {
		PROGRESSDATA *data;
		DWORD *bbits;
		int width;
		int height;
		int bits;
		int dither;
		CREATENSCRDATA *createData;
		int palette;
		int nPalettes;
		int fmt;
		int tileBase;
	} *params = calloc(1, sizeof(*params));
	params->data = data;
	params->bbits = bbits;
	params->width = width;
	params->height = height;
	params->bits = bits;
	params->dither = dither;
	params->createData = createData;
	params->palette = palette;
	params->nPalettes = nPalettes;
	params->fmt = fmt;
	params->tileBase = tileBase;
	CreateThread(NULL, 0, threadedNscrCreateInternal, (LPVOID) params, 0, NULL);
}

LRESULT WINAPI CreateDialogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CREATEDIALOGDATA *data = (CREATEDIALOGDATA *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = calloc(sizeof(CREATEDIALOGDATA), 1);
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) | WS_DISABLED);

			CreateWindow(L"STATIC", L"Bitmap:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Bits:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Dither:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 64, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palettes:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 91, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 118, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Tile base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 145, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 172, 50, 22, hWnd, NULL, NULL, NULL);

			data->nscrCreateInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 10, 200, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateInputButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 270, 10, 50, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateDither = CreateWindow(L"BUTTON", NULL, WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 70, 64, 22, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateDropdown = CreateWindow(WC_COMBOBOX, NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 70, 37, 100, 100, hWnd, NULL, NULL, NULL);
			data->nscrCreateButton = CreateWindow(L"BUTTON", L"Create", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 70, 199, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPalettesInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 70, 91, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 70, 118, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndTileBase = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 70, 145, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndFormatDropdown = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 70, 172, 100, 100, hWnd, NULL, NULL, NULL);

			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 1, (LPARAM) L"4");
			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 1, (LPARAM) L"8");
			SendMessage(data->nscrCreateDropdown, CB_SETCURSEL, 1, 0);
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 5, L"Nitro");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 6, L"Hudson");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 8, L"Hudson 2");
			SendMessage(data->hWndFormatDropdown, CB_SETCURSEL, 0, 0);

			SetWindowSize(hWnd, 330, 231);
			SetGUIFont(hWnd);
			return 1;
		}
		case WM_CLOSE:
		{
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndParent);
			break;
		}
		case WM_COMMAND:
		{
			if (HIWORD(wParam) == BN_CLICKED) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->nscrCreateInputButton) {
					LPWSTR location = openFileDialog(hWnd, L"Select Image", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
					if (!location) break;
					SendMessage(data->nscrCreateInput, WM_SETTEXT, (WPARAM) wcslen(location), (LPARAM) location);
					free(location);
				}  else if (hWndControl == data->nscrCreateButton) {
					WCHAR location[MAX_PATH + 1];
					int dither = SendMessage(data->nscrCreateDither, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int fmt = SendMessage(data->hWndFormatDropdown, CB_GETCURSEL, 0, 0);
					int bitsOptions[] = {4, 8};
					int bits = bitsOptions[SendMessage(data->nscrCreateDropdown, CB_GETCURSEL, 0, 0)];
					SendMessage(data->hWndPaletteInput, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int palette = _wtoi(location);
					SendMessage(data->hWndPalettesInput, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int nPalettes = _wtoi(location);
					SendMessage(data->hWndTileBase, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int tileBase = _wtoi(location);
					SendMessage(data->nscrCreateInput, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);

					LPWSTR nclrLocation = saveFileDialog(hWnd, L"Save NCLR", L"NCLR Files (*.nclr)\0*.nclr\0All Files\0*.*", L"nclr");
					if (!nclrLocation) break;
					LPWSTR ncgrLocation = saveFileDialog(hWnd, L"Save NCGR", L"NCGR Files (*.ncgr)\0*.ncgr\0All Files\0*.*", L"ncgr");
					if (!ncgrLocation) {
						free(nclrLocation);
						break;
					}
					LPWSTR nscrLocation = saveFileDialog(hWnd, L"Save NSCR", L"NSCR Files (*.nscr)\0*.nscr\0All Files\0*.*", L"nscr");
					if (!nscrLocation) {
						free(nclrLocation);
						free(ncgrLocation);
						break;
					}

					int width, height;
					DWORD * bbits = gdipReadImage(location, &width, &height);

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					HWND hWndMdi = nitroPaintStruct->hWndMdi;

					HWND hWndProgress = CreateWindow(L"ProgressWindowClass", L"In Progress...", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), CW_USEDEFAULT, CW_USEDEFAULT, 300, 100, hWndMain, NULL, NULL, NULL);
					ShowWindow(hWndProgress, SW_SHOW);
					CREATENSCRDATA *createData = (CREATENSCRDATA *) calloc(1, sizeof(CREATENSCRDATA));
					CopyMemory(createData->szNclrPath, nclrLocation, 2 * (wcslen(nclrLocation) + 1));
					CopyMemory(createData->szNcgrPath, ncgrLocation, 2 * (wcslen(ncgrLocation) + 1));
					CopyMemory(createData->szNscrPath, nscrLocation, 2 * (wcslen(nscrLocation) + 1));
					createData->hWndMain = hWndMain;
					createData->bbits = bbits;
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					progressData->data = createData;
					progressData->callback = nscrCreateCallback;
					SendMessage(hWndProgress, NV_SETDATA, 0, (LPARAM) progressData);

					threadedNscrCreate(progressData, bbits, width, height, bits, dither, createData, palette, nPalettes, fmt, tileBase);

					free(nclrLocation);
					free(ncgrLocation);
					free(nscrLocation);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					SetActiveWindow(hWndProgress);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);

				}
			}
			break;
		}
		case WM_DESTROY:
		{
			free(data);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterCreateDialogClass() {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"CreateDialogClass";
	wcex.lpfnWndProc = CreateDialogWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

LRESULT WINAPI ProgressWindowWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
		{
			CreateWindow(L"STATIC", L"In progress...", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			SetTimer(hWnd, 1, 50, NULL);
			SetGUIFont(hWnd);
			break;
		}
		case WM_TIMER:
		{
			PROGRESSDATA *data = (PROGRESSDATA *) GetWindowLongPtr(hWnd, 0);
			if (data) {
				if (data->waitOn) {
					KillTimer(hWnd, 1);
					if (data->callback) data->callback(data->data);
					DestroyWindow(hWnd);
				}
			}
			break;
		}
		case NV_SETDATA:
		{
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) lParam);
			break;
		}
		case WM_DESTROY:
		{
			PROGRESSDATA *data = (PROGRESSDATA *) GetWindowLongPtr(hWnd, 0);
			if (data) {
				free(data);
				SetWindowLongPtr(hWnd, 0, 0);
			}
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
		case WM_CLOSE:
		{

			return 0;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterProgressWindowClass() {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"ProgressWindowClass";
	wcex.lpfnWndProc = ProgressWindowWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

typedef struct {
	HWND hWndFileInput;
	HWND hWndBrowseButton;
	HWND hWndWidthInput;
	HWND hWndConvertButton;
} NTFTCONVERTDATA;

LRESULT CALLBACK NtftConvertDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NTFTCONVERTDATA *data = (NTFTCONVERTDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (NTFTCONVERTDATA *) calloc(1, sizeof(NTFTCONVERTDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			SetWindowSize(hWnd, 50 + 10 + 200 + 10 + 10, 10 + 10 + 22 + 5 + 22 + 5 + 22);
			CreateWindow(L"STATIC", L"Input:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Width:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndFileInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 10, 170, 22, hWnd, NULL, NULL, NULL);
			data->hWndBrowseButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 240, 10, 30, 22, hWnd, NULL, NULL, NULL);
			data->hWndWidthInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"8", WS_VISIBLE | WS_CHILD | ES_NUMBER, 70, 37, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndConvertButton = CreateWindow(L"BUTTON", L"Convert", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 70, 64, 100, 22, hWnd, NULL, NULL, NULL);
			SetGUIFont(hWnd);
			
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) | WS_DISABLED);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl) {
				if (hWndControl == data->hWndBrowseButton) {
					LPWSTR path = openFileDialog(hWnd, L"Open NTFT", L"NTFT Files (*.ntft)\0*.ntft\0All Files\0*.*\0", L"ntft");
					if (!path) break;
					SendMessage(data->hWndFileInput, WM_SETTEXT, wcslen(path), (LPARAM) path);

					//try to guess a size
					NTFT ntft;
					int r = ntftReadFile(&ntft, path);
					if (!r) {
						int nPx = ntft.nPx;
						free(ntft.px);
						int guessWidth = 8;
						while (1) {
							if (nPx / guessWidth <= guessWidth) break;
							guessWidth *= 2;
						}

						WCHAR buffer[16];
						int len = wsprintfW(buffer, L"%d", guessWidth);
						SendMessage(data->hWndWidthInput, WM_SETTEXT, len, (LPARAM) buffer);
					}

					free(path);
				} else if (hWndControl == data->hWndConvertButton) {
					LPWSTR out = saveFileDialog(hWnd, L"Save Image", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
					if (!out) break;
					WCHAR src[MAX_PATH + 1];
					SendMessage(data->hWndWidthInput, WM_GETTEXT, 16, (LPARAM) src);
					int width = _wtol(src);
					SendMessage(data->hWndFileInput, WM_GETTEXT, MAX_PATH, (LPARAM) src);

					NTFT ntft;
					ntftReadFile(&ntft, src);
					int nPx = ntft.nPx;

					DWORD *px = (DWORD *) malloc(nPx * 4);
					for (int i = 0; i < nPx; i++){
						COLOR c = ntft.px[i];
						DWORD converted = ColorConvertFromDS(c);
						if (c & 0x8000) converted |= 0xFF000000;
						px[i] = REVERSE(converted);
					}

					writeImage(px, width, nPx / width, out);
					free(ntft.px);
					free(px);
					free(out);
					DestroyWindow(hWnd);
				}
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndParent);
			break;
		}
		case WM_DESTROY:
		{
			free(data);
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterNtftConvertDialogClass() {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NtftConvertDialogClass";
	wcex.lpfnWndProc = NtftConvertDialogProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID ReadConfiguration(LPWSTR lpszPath) {
	DWORD dwAttributes = GetFileAttributes(lpszPath);
	if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
		
		/*HANDLE hFile = CreateFile(lpszPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		CloseHandle(hFile);*/
		BOOL result = TRUE;
		result = result && WritePrivateProfileStringW(L"NclrViewer", L"UseDSColorPicker", L"0", lpszPath);
		result = result && WritePrivateProfileStringW(L"NcgrViewer", L"Gridlines", L"1", lpszPath);
		result = result && WritePrivateProfileStringW(L"NscrViewer", L"Gridlines", L"0", lpszPath);
		result = result && WritePrivateProfileStringW(L"NitroPaint", L"FullPaths", L"1", lpszPath);
	}
	g_configuration.nclrViewerConfiguration.useDSColorPicker = GetPrivateProfileInt(L"NclrViewer", L"UseDSColorPicker", 0, lpszPath);
	g_configuration.ncgrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NcgrViewer", L"Gridlines", 1, lpszPath);
	g_configuration.nscrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NscrViewer", L"Gridlines", 0, lpszPath);
	g_configuration.fullPaths = GetPrivateProfileInt(L"NitroPaint", L"FullPaths", 1, lpszPath);
}

VOID SetConfigPath() {
	LPWSTR name = L"nitropaint.ini";
	g_configPath = calloc(MAX_PATH + 1, 1);
	DWORD nLength = GetModuleFileNameW(GetModuleHandleW(NULL), g_configPath, MAX_PATH);
	int endOffset = 0;
	for (int i = 0; i < nLength; i++) {
		if (g_configPath[i] == L'\\' || g_configPath[i] == '/') endOffset = i + 1;
	}
	memcpy(g_configPath + endOffset, name, wcslen(name) * 2 + 2);
}

void RegisterClasses() {
	RegisterNcgrViewerClass();
	RegisterNclrViewerClass();
	RegisterNscrViewerClass();
	RegisterNcerViewerClass();
	RegisterCreateDialogClass();
	RegisterNsbtxViewerClass();
	RegisterProgressWindowClass();
	RegisterNtftConvertDialogClass();
	RegisterTextureEditorClass();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));

	SetConfigPath();
	ReadConfiguration(g_configPath);

	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hInstance = hInstance;
	wcex.style = 0;
	wcex.hbrBackground = NULL;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = g_lpszNitroPaintClassName;
	wcex.lpfnWndProc = WndProc;
	wcex.cbWndExtra = sizeof(PVOID);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);

	RegisterClasses();

	InitCommonControls();

	HWND hWnd = CreateWindowEx(0, g_lpszNitroPaintClassName, L"NitroPaint", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
	ShowWindow(hWnd, SW_SHOW);
	if (g_hEvent != NULL) SetEvent(g_hEvent);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}

int main(void) {
	return WinMain(GetModuleHandle(NULL), NULL, NULL, 0);
}