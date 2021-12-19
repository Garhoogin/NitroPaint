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
#include "nanrviewer.h"
#include "exceptions.h"
#include "gdip.h"
#include "tileeditor.h"
#include "textureeditor.h"
#include "nsbtx.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dbghelp.lib")

int _fltused;

extern long _ftol(double d);

long _ftol2_sse(float f) { //ugly hack
	return _ftol(f);
}

int __wgetmainargs(int *argc, wchar_t ***argv, wchar_t ***env, int doWildCard, int *startInfo);

HICON g_appIcon = NULL;

LPWSTR g_lpszNitroPaintClassName = L"NitroPaintClass";

extern EXCEPTION_DISPOSITION __cdecl ExceptionHandler(EXCEPTION_RECORD *exceptionRecord, void *establisherFrame, CONTEXT *contextRecord, void *dispatcherContext);

HANDLE g_hEvent = NULL;

LPWSTR saveFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension) {
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

LPWSTR openFileDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension) {
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

typedef struct EDITORDATA_ {
	FRAMEDATA frameData;
	WCHAR szOpenFile[MAX_PATH];
	OBJECT_HEADER objectHeader;
} EDITORDATA;

CONFIGURATIONSTRUCT g_configuration;
LPWSTR g_configPath;

#define NV_SETDATA (WM_USER+2)

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
	DWORD dwSize = 0;
	char *buffer = (char *) fileReadWhole(path, &dwSize);

	int format = fileIdentify(buffer, dwSize, path);
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
		case FILE_TYPE_TEXTURE:
			CreateTextureEditor(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_NANR:
			CreateNanrViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_IMAGE:
			CreateTextureEditor(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_COMBO2D:
		{
			//if there is already an NCLR open, close it.
			if (data->hWndNclrViewer) DestroyWindow(data->hWndNclrViewer);
			data->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, path);

			//if there is already an NCGR open, close it.
			if (data->hWndNcgrViewer) DestroyWindow(data->hWndNcgrViewer);
			data->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, path);
			InvalidateRect(data->hWndNclrViewer, NULL, FALSE);

			//if there is already an NSCR open, close it.
			if (data->hWndNscrViewer) DestroyWindow(data->hWndNscrViewer);
			data->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);

			//create a combo frame
			COMBO2D *combo = (COMBO2D *) calloc(1, sizeof(COMBO2D));
			NCLR *nclr = &((NCLRVIEWERDATA *) GetWindowLongPtr(data->hWndNclrViewer, 0))->nclr;
			NCGR *ncgr = &((NCGRVIEWERDATA *) GetWindowLongPtr(data->hWndNcgrViewer, 0))->ncgr;
			NSCR *nscr = &((NSCRVIEWERDATA *) GetWindowLongPtr(data->hWndNscrViewer, 0))->nscr;
			combo->nclr = nclr;
			combo->ncgr = ncgr;
			combo->nscr = nscr;
			nclr->combo2d = combo;
			ncgr->combo2d = combo;
			nscr->combo2d = combo;

			combo->header.compression = COMPRESSION_NONE;
			combo->header.dispose = NULL;
			combo->header.size = sizeof(COMBO2D);
			combo->header.type = FILE_TYPE_COMBO2D;
			combo->header.format = combo2dIsValid(buffer, dwSize);
			break;
		}
		default:
			break;
	}
	free(buffer);
}

VOID HandleSwitch(LPWSTR lpSwitch) {
	if (!wcsncmp(lpSwitch, L"EVENT:", 6)) {
		g_hEvent = (HANDLE) _wtol(lpSwitch + 6);
	}
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NITROPAINTSTRUCT *data = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWnd, 0);
	if (!data) {
		data = (NITROPAINTSTRUCT *) calloc(1, sizeof(NITROPAINTSTRUCT));
		SetWindowLongPtr(hWnd, 0, (LONG) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			CLIENTCREATESTRUCT createStruct;
			createStruct.hWindowMenu = GetSubMenu(GetMenu(hWnd), 4);
			createStruct.idFirstChild = 200;
			data->hWndMdi = CreateWindowEx(WS_EX_CLIENTEDGE, L"MDICLIENT", L"MDI", WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL, 0, 0, 500, 500, hWnd, NULL, NULL, &createStruct);
#if(g_useDarkTheme)
				SetWindowTheme(data->hWndMdi, L"DarkMode_Explorer", NULL);
#endif

			//subclass the MDI client window.
			OldMdiClientWndProc = (WNDPROC) SetWindowLongPtr(data->hWndMdi, GWL_WNDPROC, (LONG_PTR) NewMdiClientWndProc);
			DragAcceptFiles(hWnd, TRUE);

			//open command line argument's files
			int argc;
			wchar_t **argv;
			wchar_t **env;
			int startInfo;
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
			if (g_configuration.renderTransparent) {
				CheckMenuItem(GetMenu(hWnd), ID_VIEW_RENDERTRANSPARENCY, MF_CHECKED);
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
			if (HIWORD(wParam) == 1 && lParam == 0) {
				//translate accelerator into actual commands
				WORD accel = LOWORD(wParam);
				switch (accel) {
					case ID_ACCELERATOR_UNDO:
						PostMessage(hWnd, WM_COMMAND, ID_EDIT_UNDO, 0);
						break;
					case ID_ACCELERATOR_REDO:
						PostMessage(hWnd, WM_COMMAND, ID_EDIT_REDO, 0);
						break;
					case ID_ACCELERATOR_SAVE:
						PostMessage(hWnd, WM_COMMAND, ID_FILE_SAVE, 0);
						break;
					case ID_ACCELERATOR_SAVEALL:
						PostMessage(hWnd, WM_COMMAND, ID_FILE_SAVEALL, 0);
						break;
					case ID_ACCELERATOR_EXPORT:
						PostMessage(hWnd, WM_COMMAND, ID_FILE_EXPORT, 0);
						break;
					case ID_ACCELERATOR_OPEN:
						PostMessage(hWnd, WM_COMMAND, ID_FILE_OPEN40085, 0);
						break;
				}
			}
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
					case ID_FILE_OPEN40085:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open", 
													 L"All Supported Files\0*.nclr;*.rlcn;*.ntfp;*.nbfp;*.bin;*.pltt;*.ncgr;*.rgcn;*.ncbr;*.nbfc;*.char;*.nscr;*.rcsn;*.nbfs;*.ncer;*.recn;*.nanr;*.rnan;*.dat;*.nsbmd;*.nsbtx;*.tga\0"
													 L"Palette Files (*.nclr, *.rlcn, *ncl.bin, *icl.bin, *.ntfp, *.nbfp, *.pltt, *.bin)\0*.nclr;*.rlcn;*ncl.bin;*.ntfp;*.nbfp;*.pltt;*.bin\0"
													 L"Graphics Files (*.ncgr, *.rgcn, *.ncbr, *ncg.bin, *icg.bin, *.nbfc, *.char, *.bin)\0*.ncgr;*.rgcn;*.ncbr;*.nbfc;*.char;*.bin\0"
													 L"Screen Files (*.nscr, *.rcsn, *nsc.bin, *isc.bin, *.nbfs, *.bin)\0*.nscr;*.rcsn;*.nbfs;*.bin\0"
													 L"Cell Files (*.ncer, *.recn, *.bin)\0*.ncer;*.recn;*.bin\0"
													 L"Animation Files (*.nanr, *.rnan)\0*.nanr;*.rnan\0"
													 L"Combination Files (*.dat)\0*.dat\0"
													 L"Texture Archives (*.nsbtx, *.nsbmd)\0*.nsbtx;*.nsbmd\0"
													 L"Textures (*.tga)\0*.tga\0"
													 L"All Files (*.*)\0*.*\0",
													 L"");
						if (path == NULL) break;

						OpenFileByName(hWnd, path);
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
					case ID_NEW_NEWTEXTURE:
					{
						LPWSTR filter = L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0";

						LPWSTR path = openFileDialog(hWnd, L"Open Image", filter, L"");
						if (path == NULL) break;

						HWND h = CreateTextureEditor(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
						free(path);
						break;
					}
					case ID_FILE_CONVERTTO:
					{
						HWND hWndFocused = (HWND) SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, 0);
						if (hWndFocused == NULL) break;
						if (hWndFocused != data->hWndNclrViewer && hWndFocused != data->hWndNcgrViewer
							&& hWndFocused != data->hWndNscrViewer && hWndFocused != data->hWndNcerViewer) break;

						EDITORDATA *editorData = (EDITORDATA *) GetWindowLongPtr(hWndFocused, 0);
						LPCWSTR *formats = getFormatNamesFromType(editorData->objectHeader.type);
						if (formats == NULL || formats[0] == NULL)  break;

						HWND h = CreateWindow(L"ConvertFormatDialogClass", L"Convert Format", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						SendMessage(h, NV_SETDATA, 0, (LPARAM) editorData);
						SetActiveWindow(h);
						SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_DISABLED);
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
					case ID_VIEW_RENDERTRANSPARENCY:
					{
						int state = GetMenuState(GetMenu(hWnd), ID_VIEW_RENDERTRANSPARENCY, MF_BYCOMMAND);
						state = !state;
						if (state) {
							WritePrivateProfileStringW(L"NitroPaint", L"RenderTransparent", L"1", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_RENDERTRANSPARENCY, MF_CHECKED);
						} else {
							WritePrivateProfileStringW(L"NitroPaint", L"RenderTransparent", L"0", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_RENDERTRANSPARENCY, MF_UNCHECKED);
						}
						g_configuration.renderTransparent = state;

						//update viewers
						if (data->hWndNcgrViewer != NULL) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(data->hWndNcgrViewer, 0);
							ncgrViewerData->transparent = state;
							InvalidateRect(data->hWndNcgrViewer, NULL, FALSE);
						}
						if (data->hWndNscrViewer != NULL) {
							NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(data->hWndNscrViewer, 0);
							nscrViewerData->transparent = state;
							InvalidateRect(data->hWndNscrViewer, NULL, FALSE);
						}
						break;
					}
					case ID_NTFT_NTFT40084:
					{
						CreateWindow(L"NtftConvertDialogClass", L"NTFT To Nitro TGA", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						break;
					}
				}
			}
			HWND hWndActive = (HWND) SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, (LPARAM) NULL);
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
	HWND hWndMergeTiles;
	HWND hWndPaletteSize;
	HWND hWndPaletteOffset;
	HWND hWndRowLimit;
	HWND hWndMaxChars;
	HWND hWndDiffuse;
} CREATEDIALOGDATA;

BOOL WINAPI SetGUIFontProc(HWND hWnd, LPARAM lParam) {
	HFONT hFont = (HFONT) lParam;
	SendMessage(hWnd, WM_SETFONT, (WPARAM) hFont, TRUE);
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

typedef struct {
	PROGRESSDATA *data;
	DWORD *bbits;
	int width;
	int height;
	int bits;
	int dither;
	float diffuse;
	CREATENSCRDATA *createData;
	int palette;
	int nPalettes;
	int fmt;
	int tileBase;
	int mergeTiles;
	int paletteSize;
	int paletteOffset;
	int rowLimit;
	int nMaxChars;
} THREADEDNSCRCREATEPARAMS;

DWORD WINAPI threadedNscrCreateInternal(LPVOID lpParameter) {
	THREADEDNSCRCREATEPARAMS *params = lpParameter;
	nscrCreate(params->bbits, params->width, params->height, params->bits, params->dither, params->diffuse,
			   params->createData->szNclrPath, params->createData->szNcgrPath, params->createData->szNscrPath,
			   params->palette, params->nPalettes, params->fmt, params->tileBase, params->mergeTiles,
			   params->paletteSize, params->paletteOffset, params->rowLimit, params->nMaxChars,
			   &params->data->progress1, &params->data->progress1Max, &params->data->progress2, &params->data->progress2Max);
	params->data->waitOn = 1;
	return 0;
}

void threadedNscrCreate(PROGRESSDATA *data, DWORD *bbits, int width, int height, int bits, int dither, float diffuse, 
						CREATENSCRDATA *createData, int palette, int nPalettes, int fmt, int tileBase, int mergeTiles, 
						int paletteSize, int paletteOffset, int rowLimit, int nMaxChars) {
	THREADEDNSCRCREATEPARAMS *params = calloc(1, sizeof(*params));
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
	params->mergeTiles = mergeTiles;
	params->paletteSize = paletteSize;
	params->paletteOffset = paletteOffset;
	params->rowLimit = rowLimit;
	params->nMaxChars = nMaxChars;
	params->diffuse = diffuse;
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
			CreateWindow(L"STATIC", L"Diffuse:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 91, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palettes:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 118, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 145, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Size:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 172, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Offset:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 199, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Row limit:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 226, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Tile base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 253, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Merge tiles", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 280, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Maximum:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 307, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 334, 50, 22, hWnd, NULL, NULL, NULL);

			data->nscrCreateInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 10, 200, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateInputButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 270, 10, 50, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateDropdown = CreateWindow(WC_COMBOBOX, NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 70, 37, 100, 100, hWnd, NULL, NULL, NULL);
			data->nscrCreateDither = CreateWindow(L"BUTTON", NULL, WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 70, 64, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndDiffuse = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 70, 91, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPalettesInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 70, 118, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 70, 145, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteSize = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"256", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 70, 172, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteOffset = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 70, 199, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndRowLimit = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 70, 226, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndTileBase = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 70, 253, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndMergeTiles = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 70, 280, 22, 22, hWnd, NULL, NULL, NULL);
			data->hWndMaxChars = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1024", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, 70, 307, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndFormatDropdown = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 70, 334, 100, 100, hWnd, NULL, NULL, NULL);
			data->nscrCreateButton = CreateWindow(L"BUTTON", L"Create", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 70, 361, 100, 22, hWnd, NULL, NULL, NULL);

			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 1, (LPARAM) L"4");
			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 1, (LPARAM) L"8");
			SendMessage(data->nscrCreateDropdown, CB_SETCURSEL, 1, 0);
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 5, (LPARAM) L"Nitro");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 6, (LPARAM) L"Hudson");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 8, (LPARAM) L"Hudson 2");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 3, (LPARAM) L"Raw");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 15, (LPARAM) L"Raw Compressed");
			SendMessage(data->hWndFormatDropdown, CB_SETCURSEL, 0, 0);
			SendMessage(data->hWndMergeTiles, BM_SETCHECK, BST_CHECKED, 0);

			SetWindowSize(hWnd, 330, 393);
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
					LPWSTR location = openFileDialog(hWnd, L"Select Image", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0", L"");
					if (!location) break;
					SendMessage(data->nscrCreateInput, WM_SETTEXT, (WPARAM) wcslen(location), (LPARAM) location);
					free(location);
				}  else if (hWndControl == data->nscrCreateButton) {
					WCHAR location[MAX_PATH + 1];
					int dither = SendMessage(data->nscrCreateDither, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int merge = SendMessage(data->hWndMergeTiles, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int rowLimit = SendMessage(data->hWndRowLimit, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int fmt = SendMessage(data->hWndFormatDropdown, CB_GETCURSEL, 0, 0);
					int bitsOptions[] = {4, 8};
					int bits = bitsOptions[SendMessage(data->nscrCreateDropdown, CB_GETCURSEL, 0, 0)];
					SendMessage(data->hWndPaletteInput, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int palette = _wtoi(location);
					SendMessage(data->hWndPalettesInput, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int nPalettes = _wtoi(location);
					SendMessage(data->hWndTileBase, WM_GETTEXT, MAX_PATH + 1, (LPARAM) location);
					int tileBase = _wtoi(location);
					SendMessage(data->hWndPaletteSize, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);
					int paletteSize = _wtoi(location);
					SendMessage(data->hWndPaletteOffset, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);
					int paletteOffset = _wtoi(location);
					SendMessage(data->hWndMaxChars, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);
					int nMaxChars = _wtoi(location);
					SendMessage(data->hWndDiffuse, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);
					float diffuse = ((float) _wtoi(location)) * 0.01f;
					SendMessage(data->nscrCreateInput, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);

					LPCWSTR nclrFilter = L"NCLR Files (*.nclr)\0*.nclr;*.rlcn\0All Files\0*.*\0";
					LPCWSTR ncgrFilter = L"NCGR Files (*.ncgr)\0*.ncgr;*.rgcn\0All Files\0*.*\0";
					LPCWSTR nscrFilter = L"NSCR Files (*.nscr)\0*.nscr;*.rcsn\0All Files\0*.*\0";
					switch (fmt) {
						case 1:
						case 2:
							nclrFilter = L"bin files (*.bin)\0*.bin\0All Files\0*.*\0";
							ncgrFilter = L"bin files (*.bin)\0*.bin\0All Files\0*.*\0";
							nscrFilter = L"bin files (*.bin)\0*.bin\0All Files\0*.*\0";
							break;
						case 3:
						case 4:
							nclrFilter = L"ncl.bin files (*.bin)\0*ncl.bin\0All Files\0*.*\0";
							ncgrFilter = L"ncg.bin files (*.bin)\0*ncg.bin\0All Files\0*.*\0";
							nscrFilter = L"nsc.bin files (*.bin)\0*nsc.bin\0All Files\0*.*\0";
							break;
					}

					LPWSTR nclrLocation = saveFileDialog(hWnd, L"Save NCLR", nclrFilter, L"nclr");
					if (!nclrLocation) break;
					LPWSTR ncgrLocation = saveFileDialog(hWnd, L"Save NCGR", ncgrFilter, L"ncgr");
					if (!ncgrLocation) {
						free(nclrLocation);
						break;
					}
					LPWSTR nscrLocation = saveFileDialog(hWnd, L"Save NSCR", nscrFilter, L"nscr");
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

					threadedNscrCreate(progressData, bbits, width, height, bits, dither, diffuse, createData, palette, nPalettes, fmt, tileBase, merge, paletteSize, paletteOffset, rowLimit, nMaxChars);

					free(nclrLocation);
					free(ncgrLocation);
					free(nscrLocation);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					SetActiveWindow(hWndProgress);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);

				} else if (hWndControl == data->hWndMergeTiles) {
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;

					HWND hWndMaxChars = data->hWndMaxChars;
					if (state) {
						SetWindowLong(hWndMaxChars, GWL_STYLE, GetWindowLong(hWndMaxChars, GWL_STYLE) & ~WS_DISABLED);
					} else {
						SetWindowLong(hWndMaxChars, GWL_STYLE, GetWindowLong(hWndMaxChars, GWL_STYLE) | WS_DISABLED);
					}
					InvalidateRect(hWnd, NULL, FALSE);
				}
			} else if (HIWORD(wParam) == CBN_SELCHANGE) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->nscrCreateDropdown) {
					int index = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					LPCWSTR sizes[] = { L"16", L"256" };
					SendMessage(data->hWndPaletteSize, WM_SETTEXT, wcslen(sizes[index]), (LPARAM) sizes[index]);
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
			CreateWindow(L"STATIC", L"Palette:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Character compression:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 69, 150, 22, hWnd, NULL, NULL, NULL);
			SetWindowSize(hWnd, 520, 128);
			SetTimer(hWnd, 1, 100, NULL);
			SetGUIFont(hWnd);
			break;
		}
		case WM_TIMER:
		{
			PROGRESSDATA *data = (PROGRESSDATA *) GetWindowLongPtr(hWnd, 0);
			if (data) {
				if (data->hWndProgress1 == NULL) {
					data->hWndProgress1 = CreateWindow(PROGRESS_CLASSW, L"", WS_VISIBLE | WS_CHILD, 10, 42, 500, 22, hWnd, NULL, NULL, NULL);
				}
				if (data->hWndProgress2 == NULL) {
					data->hWndProgress2 = CreateWindow(PROGRESS_CLASSW, L"", WS_VISIBLE | WS_CHILD, 10, 96, 500, 22, hWnd, NULL, NULL, NULL);
				}
				SendMessage(data->hWndProgress1, PBM_SETRANGE, 0, 0 | (data->progress1Max << 16));
				SendMessage(data->hWndProgress2, PBM_SETRANGE, 0, 0 | (data->progress2Max << 16));
				SendMessage(data->hWndProgress1, PBM_SETPOS, data->progress1, 0);
				SendMessage(data->hWndProgress2, PBM_SETPOS, data->progress2, 0);
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
	HWND hWndNtftInput;
	HWND hWndNtftBrowseButton;
	HWND hWndNtfpInput;
	HWND hWndNtfpBrowseButton;
	HWND hWndNtfiInput;
	HWND hWndNtfiBrowseButton;
	HWND hWndFormat;
	HWND hWndWidthInput;
	HWND hWndConvertButton;
} NTFTCONVERTDATA;

extern int ilog2(int x);

LRESULT CALLBACK NtftConvertDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NTFTCONVERTDATA *data = (NTFTCONVERTDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (NTFTCONVERTDATA *) calloc(1, sizeof(NTFTCONVERTDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			SetWindowSize(hWnd, 280, 177);
			CreateWindow(L"STATIC", L"NTFT:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"NTFP:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"NTFI:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 64, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 91, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Width:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 118, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndNtftInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 10, 170, 22, hWnd, NULL, NULL, NULL);
			data->hWndNtftBrowseButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 240, 10, 30, 22, hWnd, NULL, NULL, NULL);
			data->hWndNtfpInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 37, 170, 22, hWnd, NULL, NULL, NULL);
			data->hWndNtfpBrowseButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 240, 37, 30, 22, hWnd, NULL, NULL, NULL);
			data->hWndNtfiInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 64, 170, 22, hWnd, NULL, NULL, NULL);
			data->hWndNtfiBrowseButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, 240, 64, 30, 22, hWnd, NULL, NULL, NULL);
			data->hWndFormat = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 70, 91, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndWidthInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"8", WS_VISIBLE | WS_CHILD | ES_NUMBER, 70, 118, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndConvertButton = CreateWindow(L"BUTTON", L"Convert", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 70, 145, 100, 22, hWnd, NULL, NULL, NULL);
			SetGUIFont(hWnd);

			//populate the dropdown list
			WCHAR bf[16];
			int len;
			for (int i = 1; i <= CT_DIRECT; i++) {
				char *str = stringFromFormat(i);
				len = 0;
				while (*str) {
					bf[len] = *str;
					str++;
					len++;
				}
				bf[len] = L'\0';
				SendMessage(data->hWndFormat, CB_ADDSTRING, len, (LPARAM) bf);
			}
			SendMessage(data->hWndFormat, CB_SETCURSEL, 6, 0);
			
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) | WS_DISABLED);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl) {
				if (hWndControl == data->hWndNtftBrowseButton) {
					LPWSTR path = openFileDialog(hWnd, L"Open NTFT", L"NTFT Files (*.ntft)\0*.ntft\0All Files\0*.*\0", L"ntft");
					if (!path) break;
					SendMessage(data->hWndNtftInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
					free(path);
				} else if (hWndControl == data->hWndNtfpBrowseButton) {
					LPWSTR path = openFileDialog(hWnd, L"Open NTFP", L"NTFP Files (*.ntfp)\0*.ntfp\0All Files\0*.*\0", L"ntfp");
					if (!path) break;
					SendMessage(data->hWndNtfpInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
					free(path);
				} else if (hWndControl == data->hWndNtfiBrowseButton) {
					LPWSTR path = openFileDialog(hWnd, L"Open NTFI", L"NTFI Files (*.ntfi)\0*.ntfi\0All Files\0*.*\0", L"ntfi");
					if (!path) break;
					SendMessage(data->hWndNtfiInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
					free(path);
				} else if (hWndControl == data->hWndConvertButton) {
					WCHAR src[MAX_PATH + 1];
					SendMessage(data->hWndWidthInput, WM_GETTEXT, 16, (LPARAM) src);
					int width = _wtol(src);
					int format = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0) + 1;

					int bppArray[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
					int bpp = bppArray[format];

					int ntftSize = 0, ntfpSize = 0, ntfiSize = 0;
					BYTE *ntft = NULL, *ntfp = NULL, *ntfi = NULL;
					
					//read files
					DWORD dwSizeHigh, dwRead;
					char palName[16] = { 0 };
					SendMessage(data->hWndNtftInput, WM_GETTEXT, MAX_PATH, (LPARAM) src);
					if (wcslen(src)) {
						HANDLE hFile = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						ntftSize = GetFileSize(hFile, &dwSizeHigh);
						ntft = malloc(ntftSize);
						ReadFile(hFile, ntft, ntftSize, &dwRead, NULL);
						CloseHandle(hFile);
					}

					SendMessage(data->hWndNtfpInput, WM_GETTEXT, MAX_PATH, (LPARAM) src);
					if (wcslen(src)) {
						HANDLE hFile = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						ntfpSize = GetFileSize(hFile, &dwSizeHigh);
						ntfp = malloc(ntfpSize);
						ReadFile(hFile, ntfp, ntfpSize, &dwRead, NULL);
						CloseHandle(hFile);

						//populate palette name. Scan for last \ or /
						int lastIndex = -1;
						int i;
						for (i = 0; i < wcslen(src); i++) {
							if (src[i] == '\\' || src[i] == '/') lastIndex = i;
						}
						LPCWSTR end = src + lastIndex + 1;

						//copy until first ., NUL, or 12 characters are copied
						for (i = 0; i < 12; i++) {
							WCHAR c = end[i];
							if (c == L'.' || c == L'\0') break;
							palName[i] = (char) c;
						}
						memcpy(palName + i, "_pl", 3);
					}

					SendMessage(data->hWndNtfiInput, WM_GETTEXT, MAX_PATH, (LPARAM) src);
					if (wcslen(src)) {
						HANDLE hFile = CreateFile(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
						ntfiSize = GetFileSize(hFile, &dwSizeHigh);
						ntfi = malloc(ntfiSize);
						ReadFile(hFile, ntfi, ntfiSize, &dwRead, NULL);
						CloseHandle(hFile);
					}

					//sort out texture format requirements
					BOOL requiresNtft = TRUE;
					BOOL requiresNtfp = (format != CT_DIRECT);
					BOOL requiresNtfi = (format == CT_4x4);

					BOOL abortConvert = FALSE;
					if (requiresNtft && ntft == NULL) {
						MessageBox(hWnd, L"Texture format requires NTFT.", L"Requires NTFT", MB_ICONERROR);
						abortConvert = TRUE;
					}
					if (requiresNtfp && ntfp == NULL) {
						MessageBox(hWnd, L"Texture format requires NTFP.", L"Requires NTFP", MB_ICONERROR);
						abortConvert = TRUE;
					}
					if (requiresNtfi && ntfi == NULL) {
						MessageBox(hWnd, L"Texture format requires NTFI.", L"Requires NTFI", MB_ICONERROR);
						abortConvert = TRUE;
					}
					if (abortConvert) {
						if (ntft != NULL) free(ntft);
						if (ntfp != NULL) free(ntfp);
						if (ntfi != NULL) free(ntfi);
						break;
					}

					//ok now actually convert
					int height = ntftSize * 8 / bpp / width;
					TEXTURE texture = { 0 };
					texture.palette.pal = (COLOR *) ntfp;
					texture.palette.nColors = ntfpSize / 2;
					texture.texels.texel = ntft;
					texture.texels.cmp = (short *) ntfi;
					texture.texels.texImageParam = (format << 26) | ((ilog2(width) - 3) << 20) | ((ilog2(height) - 3) << 23);
					memcpy(&texture.palette.name, palName, 16);

					LPWSTR out = saveFileDialog(hWnd, L"Save Nitro TGA", L"TGA Files\0*.tga\0All Files\0*.*\0", L"tga");
					if (!out) {
						if (ntft != NULL) free(ntft);
						if (ntfp != NULL) free(ntfp);
						if (ntfi != NULL) free(ntfi);
						break;
					}
					writeNitroTGA(out, &texture.texels, &texture.palette);

					if (ntft != NULL) free(ntft);
					if (ntfp != NULL) free(ntfp);
					if (ntfi != NULL) free(ntfi);

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					DestroyWindow(hWnd);
					OpenFileByName(hWndMain, out);
					free(out);
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

LRESULT CALLBACK ConvertFormatDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
		{
			SetWindowSize(hWnd, 230, 96);
			break;
		}
		case NV_SETDATA:
		{
			EDITORDATA *editorData = (EDITORDATA *) lParam;
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) editorData);

			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Compression:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 37, 100, 22, hWnd, NULL, NULL, NULL);
			HWND hWndFormatCombobox = CreateWindow(WC_COMBOBOXW, L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 120, 10, 100, 100, hWnd, NULL, NULL, NULL);
			HWND hWndCompressionCombobox = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 120, 37, 100, 100, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Set", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 120, 64, 100, 22, hWnd, NULL, NULL, NULL);

			LPCWSTR *formats = getFormatNamesFromType(editorData->objectHeader.type);
			formats++; //skip invalid
			while (*formats != NULL) {
				SendMessage(hWndFormatCombobox, CB_ADDSTRING, wcslen(*formats), (LPARAM) *formats);
				formats++;
			}
			SendMessage(hWndFormatCombobox, CB_SETCURSEL, editorData->objectHeader.format - 1, 0);
			LPCWSTR *compressions = compressionNames;
			while (*compressions != NULL) {
				SendMessage(hWndCompressionCombobox, CB_ADDSTRING, wcslen(*compressions), (LPARAM) *compressions);
				compressions++;
			}
			SendMessage(hWndCompressionCombobox, CB_SETCURSEL, editorData->objectHeader.compression, 0);

			SetWindowLong(hWnd, sizeof(LPVOID), (LONG) hWndFormatCombobox);
			SetWindowLong(hWnd, sizeof(LPVOID) * 2, (LONG) hWndCompressionCombobox);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl && HIWORD(wParam) == BN_CLICKED) {
				int fmt = SendMessage((HWND) GetWindowLong(hWnd, sizeof(LPVOID)), CB_GETCURSEL, 0, 0) + 1;
				int comp = SendMessage((HWND) GetWindowLong(hWnd, sizeof(LPVOID) * 2), CB_GETCURSEL, 0, 0);
				EDITORDATA *editorData = (EDITORDATA *) GetWindowLongPtr(hWnd, 0);
				editorData->objectHeader.format = fmt;
				editorData->objectHeader.compression = comp;

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterFormatConversionClass() {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"ConvertFormatDialogClass";
	wcex.lpfnWndProc = ConvertFormatDialogProc;
	wcex.cbWndExtra = sizeof(LPVOID) * 3;
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
		result = result && WritePrivateProfileStringW(L"NitroPaint", L"PaletteAlgorithm", L"0", lpszPath);
		result = result && WritePrivateProfileString(L"NitroPaint", L"RenderTransparent", L"1", lpszPath);
	}
	g_configuration.nclrViewerConfiguration.useDSColorPicker = GetPrivateProfileInt(L"NclrViewer", L"UseDSColorPicker", 0, lpszPath);
	g_configuration.ncgrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NcgrViewer", L"Gridlines", 1, lpszPath);
	g_configuration.nscrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NscrViewer", L"Gridlines", 0, lpszPath);
	g_configuration.fullPaths = GetPrivateProfileInt(L"NitroPaint", L"FullPaths", 1, lpszPath);
	g_configuration.renderTransparent = GetPrivateProfileInt(L"NitroPaint", L"RenderTransparent", 1, lpszPath);
}

VOID SetConfigPath() {
	LPWSTR name = L"nitropaint.ini";
	g_configPath = calloc(MAX_PATH + 1, 1);
	DWORD nLength = GetModuleFileNameW(GetModuleHandleW(NULL), g_configPath, MAX_PATH);
	int endOffset = 0;
	for (unsigned int i = 0; i < nLength; i++) {
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
	RegisterFormatConversionClass();
	RegisterNanrViewerClass();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	HACCEL hAccel = LoadAccelerators(hInstance, (LPCWSTR) IDR_ACCELERATOR1);

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
		if (!TranslateAccelerator(hWnd, hAccel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return msg.wParam;
}

int main(void) {
	return WinMain(GetModuleHandle(NULL), NULL, NULL, 0);
}