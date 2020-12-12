#include <Windows.h>
#include <CommCtrl.h>
#include <Uxtheme.h>

#include "nitropaint.h"
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
#include "nsbtx.h"

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

	DWORD dwMagic = *(DWORD *) buffer;
	DWORD test = '0XTB';

	int format = 0;

	if (dwSize > 4) {
		switch (dwMagic) {
			case 'NCLR':
			case 'RLCN':
			{
				format = 1;
				break;
			}
			case 'NCGR':
			case 'RGCN':
			{
				format = 2;
				break;
			}
			case 'NSCR':
			case 'RCSN':
			{
				format = 3;
				break;
			}
			case 'NCER':
			case 'RECN':
			{
				format = 4;
				break;
			}
			case 'BTX0':
			case '0XTB':
			{
				format = 5;
				break;
			}
		}
	}
	if(format == 0){
		if (nclrIsValidHudson(buffer, dwSize)) format = 1;
		else if (nscrIsValidHudson(buffer, dwSize)) format = 3;
		else if (ncgrIsValidHudson(buffer, dwSize)) format = 2;

	}
	if (format == 1) {

		//if there is already an NCLR open, close it.
		if (data->hWndNclrViewer) DestroyWindow(data->hWndNclrViewer);
		data->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, path);

		if (data->hWndNcerViewer) InvalidateRect(data->hWndNcerViewer, NULL, FALSE);
	} else if (format == 2) {
		//if there is already an NCGR open, close it.
		if (data->hWndNcgrViewer) DestroyWindow(data->hWndNcgrViewer);
		data->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, path);
		if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
	} else if (format == 3) {
		//if there is already an NSCR open, close it.
		if (data->hWndNscrViewer) DestroyWindow(data->hWndNscrViewer);
		data->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);
	} else if (format == 4) {
		if (data->hWndNcerViewer) DestroyWindow(data->hWndNcerViewer);
		data->hWndNcerViewer = CreateNcerViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);

		if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
	} else if (format == 5) {
		HWND h = CreateNsbtxViewer(CW_USEDEFAULT, CW_USEDEFAULT, 450, 350, data->hWndMdi, path);
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
			createStruct.hWindowMenu = GetSubMenu(GetMenu(hWnd), 2);
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
					OpenFileByName(hWnd, argv[i]);
				}
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
					case ID_OPEN_OPENNCGR:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open NCGR", L"NCGR Files (*.ncgr)\0*.ncgr\0All Files\0*.*\0", L"ncgr");
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
					case ID_HELP_ABOUT:
					{
						MessageBox(hWnd, L"GUI NCLR/NCGR Editor/NSCR Viewer. Made by Garhoogin with help from Gericom, Xgone, and ProfessorDoktorGamer.", L"About NitroPaint", MB_ICONINFORMATION);
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
	HWND nscrCreateBin;
	HWND hWndPaletteInput;
	HWND hWndPalettesInput;
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
			CreateWindow(L"STATIC", L"Create bin:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 145, 50, 22, hWnd, NULL, NULL, NULL);

			data->nscrCreateInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 10, 200, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateInputButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 270, 10, 50, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateDither = CreateWindow(L"BUTTON", NULL, WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 70, 64, 22, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateDropdown = CreateWindow(WC_COMBOBOX, NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 70, 37, 100, 100, hWnd, NULL, NULL, NULL);
			data->nscrCreateButton = CreateWindow(L"BUTTON", L"Create", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 70, 172, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPalettesInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 70, 91, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 70, 118, 100, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateBin = CreateWindow(L"BUTTON", L"", BS_AUTOCHECKBOX | WS_CHILD | WS_VISIBLE, 70, 145, 22, 22, hWnd, NULL, NULL, NULL);

			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 1, (LPARAM) L"4");
			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 1, (LPARAM) L"8");
			SendMessage(data->nscrCreateDropdown, CB_SETCURSEL, 1, 0);

			SetWindowSize(hWnd, 330, 204);
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
					int bin = SendMessage(data->nscrCreateBin, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int bitsOptions[] = {4, 8};
					int bits = bitsOptions[SendMessage(data->nscrCreateDropdown, CB_GETCURSEL, 0, 0)];
					SendMessage(data->hWndPaletteInput, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int palette = _wtoi(location);
					SendMessage(data->hWndPalettesInput, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int nPalettes = _wtoi(location);
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
					nscrCreate(bbits, width, height, bits, dither, nclrLocation, ncgrLocation, nscrLocation, palette, nPalettes, bin);

					free(bbits);

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					HWND hWndMdi = nitroPaintStruct->hWndMdi;
					if (nitroPaintStruct->hWndNscrViewer) DestroyWindow(nitroPaintStruct->hWndNscrViewer);
					if (nitroPaintStruct->hWndNcgrViewer) DestroyWindow(nitroPaintStruct->hWndNcgrViewer);
					if (nitroPaintStruct->hWndNclrViewer) DestroyWindow(nitroPaintStruct->hWndNclrViewer);
					nitroPaintStruct->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, nclrLocation);
					nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, ncgrLocation);
					nitroPaintStruct->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, nscrLocation);
					//InvalidateRect(hWndMain, NULL, TRUE);
					free(nclrLocation);
					free(ncgrLocation);
					free(nscrLocation);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
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
	RegisterClassEx(&wcex);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));

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

	RegisterNcgrViewerClass();
	RegisterNclrViewerClass();
	RegisterNscrViewerClass();
	RegisterNcerViewerClass();
	RegisterCreateDialogClass();
	RegisterNsbtxViewerClass();

	InitCommonControls();

	HWND hWnd = CreateWindowEx(0, g_lpszNitroPaintClassName, L"NitroPaint", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
	ShowWindow(hWnd, SW_SHOW);

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