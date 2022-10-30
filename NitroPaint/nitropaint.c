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
#include "nmcrviewer.h"
#include "colorchooser.h"

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
			if (!g_useDarkTheme && g_configuration.hbrBackground == NULL) break;
			HDC hDC = (HDC) wParam;

			RECT rc;
			GetClientRect(hWnd, &rc);

			SelectObject(hDC, GetStockObject(NULL_PEN));
			HBRUSH black = g_configuration.hbrBackground;
			if(black == NULL) black = CreateSolidBrush(RGB(64, 64, 64));
			HBRUSH oldBrush = SelectObject(hDC, black);
			Rectangle(hDC, 0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1);
			SelectObject(hDC, oldBrush);
			if(black != g_configuration.hbrBackground) DeleteObject(black);
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

char *propGetProperty(const char *ptr, unsigned int size, const char *name) {
	//lookup value in file of Key: Value pairs
	const char *end = ptr + size;
	while (ptr != end) {
		if (*ptr == '#') { //comment string
			while (ptr != end && *ptr != '\r' && *ptr != '\n') ptr++;
			while (ptr != end && (*ptr == '\r' || *ptr == '\n')) ptr++;
			continue;
		}

		//search for colon
		const char *key = ptr;
		while (*ptr != ':' && ptr != end && *ptr != '\r' && *ptr != '\n') ptr++;
		if (ptr == end) return NULL;
		if (*ptr == '\r' || *ptr == '\n') {
			while (ptr != end && (*ptr == '\r' || *ptr == '\n')) ptr++;
			continue;
		}
		if (*ptr == ':') {
			int keyLen = ptr - key;
			ptr++;
			while ((*ptr == ' ' || *ptr == '\t') && ptr != end) ptr++;
			const char *value = ptr;

			if (_strnicmp(key, name, keyLen) == 0 && keyLen == strlen(name)) {
				//take length of value. Read until null terminator or CR/LF
				while (ptr != end && *ptr != '\r' && *ptr != '\n') ptr++;
				int valLen = ptr - value;

				char *cpy = (char *) malloc(valLen + 1);
				cpy[valLen] = '\0';
				memcpy(cpy, value, valLen);
				return cpy;
			}

			//scan to and past end of line
			while (ptr != end && *ptr != '\r' && *ptr != '\n') ptr++;
			while (ptr != end && (*ptr == '\r' || *ptr == '\n')) ptr++;
		}
	}

	//no matches
	return NULL;
}

void parseOffsetSizePair(const char *pair, int *offset, int *size) {
	//advance past whitespace
	const char *ptr = pair;
	while (*ptr <= ' ' && *ptr > '\0') ptr++;

	int tmpOffset = 0, tmpSize = 0;
	char c;
	while ((c = *(ptr++)), (c > ' ' && c != ',')) {
		if (c == 'x' || c == 'X') continue;
		int place = c - '0';
		if (place < 0) place = 0;
		if (place >= 10) {
			place = place + '0' - 'A' + 10;
			if (place >= 16) {
				place = place + 'A' - 'a';
				if (place >= 16) place = 0;
			}
		}

		tmpOffset <<= 4;
		tmpOffset |= place;
	}

	if (*ptr == ',') ptr++;
	while (*ptr <= ' ' && *ptr > '\0') ptr++;

	while ((c = *(ptr++)), (c > ' ')) {
		if (c == 'x' || c == 'X') continue;
		int place = c - '0';
		if (place < 0) place = 0;
		if (place >= 10) {
			place = place + '0' - 'A' + 10;
			if (place >= 16) {
				place = place + 'A' - 'a';
				if (place >= 16) place = 0;
			}
		}

		tmpSize <<= 4;
		tmpSize |= place;
	}

	*offset = tmpOffset;
	*size = tmpSize;
}

int specIsSpec(char *buffer, int size) {
	char *refName = propGetProperty(buffer, size, "File");
	if (refName == NULL) return 0;
	
	char *pltRef = propGetProperty(buffer, size, "PLT");
	char *chrRef = propGetProperty(buffer, size, "CHR");
	char *scrRef = propGetProperty(buffer, size, "SCR");
	if (pltRef == NULL && chrRef == NULL && scrRef == NULL) return 0;

	if (pltRef != NULL) free(pltRef);
	if (chrRef != NULL) free(chrRef);
	if (scrRef != NULL) free(scrRef);
	free(refName);
	return 1;
}

VOID CreateImageDialog(HWND hWnd, LPCWSTR path);

VOID OpenFileByName(HWND hWnd, LPCWSTR path) {
	NITROPAINTSTRUCT *data = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWnd, 0);
	DWORD dwSize = 0;
	char *buffer = (char *) fileReadWhole(path, &dwSize);

	//test: Is this a specification file to open a file with?
	if (specIsSpec(buffer, dwSize)) {
		char *refName = propGetProperty(buffer, dwSize, "File");
		char *pltRef = propGetProperty(buffer, dwSize, "PLT");
		char *chrRef = propGetProperty(buffer, dwSize, "CHR");
		char *scrRef = propGetProperty(buffer, dwSize, "SCR");

		int pltOffset = 0, pltSize = 0, chrOffset = 0, chrSize = 0, scrOffset = 0, scrSize = 0;
		if (pltRef != NULL) parseOffsetSizePair(pltRef, &pltOffset, &pltSize);
		if (chrRef != NULL) parseOffsetSizePair(chrRef, &chrOffset, &chrSize);
		if (scrRef != NULL) parseOffsetSizePair(scrRef, &scrOffset, &scrSize);

		//determine the actual path of the referenced file.
		int lastSlash = -1;
		for (unsigned i = 0; i < wcslen(path); i++) {
			if (path[i] == '\\' || path[i] == '/') lastSlash = i;
		}
		int pathLen = lastSlash + 1;
		int relFileLen = strlen(refName);
		WCHAR *pathBuffer = (WCHAR *) calloc(pathLen + relFileLen + 1, 2);
		memcpy(pathBuffer, path, 2 * pathLen);
		for (int i = 0; i < relFileLen; i++) {
			pathBuffer[i + pathLen] = refName[i];
		}

		unsigned comboSize;
		void *fp = fileReadWhole(pathBuffer, &comboSize);

		//refName is the name of the file to read.
		COMBO2D *combo = (COMBO2D *) calloc(1, sizeof(COMBO2D));
		combo->header.format = COMBO2D_TYPE_DATAFILE;
		combo->header.size = sizeof(COMBO2D);
		combo->header.type = FILE_TYPE_COMBO2D;
		combo->header.dispose = NULL;
		combo->header.compression = COMPRESSION_NONE;
		combo->extraData = (DATAFILECOMBO *) calloc(1, sizeof(DATAFILECOMBO));
		DATAFILECOMBO *dfc = (DATAFILECOMBO *) combo->extraData;
		dfc->pltOffset = pltOffset;
		dfc->pltSize = pltSize;
		dfc->chrOffset = chrOffset;
		dfc->chrSize = chrSize;
		dfc->scrOffset = scrOffset;
		dfc->scrSize = scrSize;
		dfc->data = fp;
		dfc->size = comboSize;

		NCLR nclr;
		NCGR ncgr;
		NSCR nscr;

		//read applicable sections
		if (pltRef != NULL) {
			nclrRead(&nclr, dfc->data + pltOffset, pltSize);
			nclr.header.format = NCLR_TYPE_COMBO;
			nclr.combo2d = combo;
		}
		if (chrRef != NULL) {
			ncgrRead(&ncgr, dfc->data + chrOffset, chrSize);
			ncgr.header.format = NCGR_TYPE_COMBO;
			ncgr.combo2d = combo;
		}
		if (scrRef != NULL) {
			nscrRead(&nscr, dfc->data + scrOffset, scrSize);
			nscr.header.format = NSCR_TYPE_COMBO;
			nscr.combo2d = combo;
		}

		//if there is already an NCLR open, close it.
		if (pltRef != NULL) {
			if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
			data->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, &nclr);

			NCLR *pNclr = &((NCLRVIEWERDATA *) GetWindowLongPtr(data->hWndNclrViewer, 0))->nclr;
			combo->nclr = pNclr;
			memcpy(((NCLRVIEWERDATA *) GetWindowLongPtr(data->hWndNclrViewer, 0))->szOpenFile, pathBuffer, 2 * (wcslen(pathBuffer) + 1));
		}

		//if there is already an NCGR open, close it.
		if (chrRef != NULL) {
			if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
			data->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, &ncgr);
			InvalidateRect(data->hWndNclrViewer, NULL, FALSE);


			NCGR *pNcgr = &((NCGRVIEWERDATA *) GetWindowLongPtr(data->hWndNcgrViewer, 0))->ncgr;
			combo->ncgr = pNcgr;
			memcpy(((NCGRVIEWERDATA *) GetWindowLongPtr(data->hWndNcgrViewer, 0))->szOpenFile, pathBuffer, 2 * (wcslen(pathBuffer) + 1));
		}

		//if there is already an NSCR open, close it.
		if (scrRef != NULL) {
			if (data->hWndNscrViewer) DestroyChild(data->hWndNscrViewer);
			data->hWndNscrViewer = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, &nscr);

			NSCR *pNscr = &((NSCRVIEWERDATA *) GetWindowLongPtr(data->hWndNscrViewer, 0))->nscr;
			combo->nscr = pNscr;
			memcpy(((NSCRVIEWERDATA *) GetWindowLongPtr(data->hWndNscrViewer, 0))->szOpenFile, pathBuffer, 2 * (wcslen(pathBuffer) + 1));
		}
		free(pathBuffer);

		free(refName);
		if (pltRef != NULL) free(pltRef);
		if (chrRef != NULL) free(chrRef);
		if (scrRef != NULL) free(scrRef);
		goto cleanup;
	}

	int format = fileIdentify(buffer, dwSize, path);
	switch (format) {
		case FILE_TYPE_PALETTE:
			//if there is already an NCLR open, close it.
			if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
			data->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, path);
			if (data->hWndNcerViewer) InvalidateRect(data->hWndNcerViewer, NULL, FALSE);
			break;
		case FILE_TYPE_CHARACTER:
			//if there is already an NCGR open, close it.
			if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
			data->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, path);
			if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
			break;
		case FILE_TYPE_SCREEN:
			//if there is already an NSCR open, close it.
			if (data->hWndNscrViewer) DestroyChild(data->hWndNscrViewer);
			data->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);
			break;
		case FILE_TYPE_CELL:
			//if there is already an NCER open, close it.
			if (data->hWndNcerViewer) DestroyChild(data->hWndNcerViewer);
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
			data->hWndNanrViewer = CreateNanrViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_NMCR:
			data->hWndNmcrViewer = CreateNmcrViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_IMAGE:
			CreateImageDialog(hWnd, path);
			break;
		case FILE_TYPE_COMBO2D:
		{
			int type = combo2dIsValid(buffer, dwSize);

			//if there is already an NCLR open, close it.
			if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
			data->hWndNclrViewer = CreateNclrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, path);

			//if there is already an NCGR open, close it.
			if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
			data->hWndNcgrViewer = CreateNcgrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, path);
			InvalidateRect(data->hWndNclrViewer, NULL, FALSE);

			//if there is already an NSCR open, close it.
			if (type == COMBO2D_TYPE_TIMEACE) {
				if (data->hWndNscrViewer) DestroyChild(data->hWndNscrViewer);
				data->hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);
			}

			//create a combo frame
			COMBO2D *combo = (COMBO2D *) calloc(1, sizeof(COMBO2D));
			combo2dRead(combo, buffer, dwSize);
			NCLR *nclr = NULL;
			NCGR *ncgr = NULL;
			NSCR *nscr = NULL;

			if (combo2dFormatHasPalette(type)) nclr = &((NCLRVIEWERDATA *) GetWindowLongPtr(data->hWndNclrViewer, 0))->nclr;
			if (combo2dFormatHasCharacter(type)) ncgr = &((NCGRVIEWERDATA *) GetWindowLongPtr(data->hWndNcgrViewer, 0))->ncgr;
			if (combo2dFormatHasScreen(type)) nscr = &((NSCRVIEWERDATA *) GetWindowLongPtr(data->hWndNscrViewer, 0))->nscr;

			combo->nclr = nclr;
			combo->ncgr = ncgr;
			combo->nscr = nscr;
			if(nclr != NULL) nclr->combo2d = combo;
			if(ncgr != NULL) ncgr->combo2d = combo;
			if(nscr != NULL) nscr->combo2d = combo;

			combo->header.compression = COMPRESSION_NONE;
			combo->header.dispose = NULL;
			combo->header.size = sizeof(COMBO2D);
			combo->header.type = FILE_TYPE_COMBO2D;
			combo->header.format = combo2dIsValid(buffer, dwSize);
			break;
		}
		default: //unrecognized file
		{
			WCHAR bf[MAX_PATH + 19];
			wsprintfW(bf, L"Unrecognied file %s.", GetFileName(path));
			MessageBox(hWnd, bf, L"Unrecognized File", MB_ICONERROR);
			break;
		}
	}

cleanup:
	free(buffer);
}

int MainGetZoom(HWND hWnd) {
	HMENU hMenu = GetMenu(hWnd);
	if (GetMenuState(hMenu, ID_ZOOM_100, MF_BYCOMMAND)) return 1;
	if (GetMenuState(hMenu, ID_ZOOM_200, MF_BYCOMMAND)) return 2;
	if (GetMenuState(hMenu, ID_ZOOM_400, MF_BYCOMMAND)) return 4;
	if (GetMenuState(hMenu, ID_ZOOM_800, MF_BYCOMMAND)) return 8;
	return 0;
}

void MainSetZoom(HWND hWnd, int zoom) {
	if (!zoom) return;
	int menuIndex = -1;
	while (zoom) {
		menuIndex++;
		zoom >>= 1;
	}
	int ids[] = { ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800 };
	SendMessage(hWnd, WM_COMMAND, ids[menuIndex], 0);
}

VOID MainZoomIn(HWND hWnd) {
	int zoom = MainGetZoom(hWnd);
	zoom *= 2;
	if (zoom > 8) zoom = 8;
	MainSetZoom(hWnd, zoom);
}

VOID MainZoomOut(HWND hWnd) {
	int zoom = MainGetZoom(hWnd);
	zoom /= 2;
	if (zoom < 1) zoom = 1;
	MainSetZoom(hWnd, zoom);
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
					case ID_ACCELERATOR_ZOOMIN:
						MainZoomIn(hWnd);
						break;
					case ID_ACCELERATOR_ZOOMOUT:
						MainZoomOut(hWnd);
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
													 L"All Supported Files\0*.nclr;*.rlcn;*.ntfp;*.nbfp;*.bin;*.pltt;*.ncgr;*.rgcn;*.ncbr;*.nbfc;*.char;*.nscr;*.rcsn;*.nbfs;*.ncer;*.recn;*.nanr;*.rnan;*.dat;*.nsbmd;*.nsbtx;*.bmd;*.bnr;*.tga\0"
													 L"Palette Files (*.nclr, *.rlcn, *ncl.bin, *icl.bin, *.ntfp, *.nbfp, *.pltt, *.bin)\0*.nclr;*.rlcn;*ncl.bin;*.ntfp;*.nbfp;*.pltt;*.bin\0"
													 L"Graphics Files (*.ncgr, *.rgcn, *.ncbr, *ncg.bin, *icg.bin, *.nbfc, *.char, *.bin)\0*.ncgr;*.rgcn;*.ncbr;*.nbfc;*.char;*.bin\0"
													 L"Screen Files (*.nscr, *.rcsn, *nsc.bin, *isc.bin, *.nbfs, *.bin)\0*.nscr;*.rcsn;*.nbfs;*.bin\0"
													 L"Cell Files (*.ncer, *.recn, *.bin)\0*.ncer;*.recn;*.bin\0"
													 L"Animation Files (*.nanr, *.rnan)\0*.nanr;*.rnan\0"
													 L"Combination Files (*.dat, *.bnr, *.bin)\0*.dat;*.bnr;*.bin\0"
													 L"Texture Archives (*.nsbtx, *.nsbmd, *.bmd)\0*.nsbtx;*.nsbmd;*.bmd\0"
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
						HWND h = CreateWindow(L"CreateDialogClass", L"Create BG", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
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
					case ID_NEW_NEWSPRITESHEET:
					{
						HWND h = CreateWindow(L"SpriteSheetDialogClass", L"Create Sprite Sheet", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX), CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						ShowWindow(h, SW_SHOW);
						break;
					}
					case ID_NEW_NEWCELLBANK:
					{
						if (data->hWndNcerViewer != NULL) DestroyChild(data->hWndNcerViewer);
						data->hWndNcerViewer = NULL;

						NCER ncer;
						ncerInit(&ncer, NCER_TYPE_NCER);
						ncer.nCells = 1;
						ncer.cells = (NCER_CELL *) calloc(1, sizeof(NCER_CELL));
						ncer.cells[0].attr = (WORD *) calloc(3, 2);
						ncer.cells[0].nAttr = 3;
						ncer.cells[0].nAttribs = 1;
						ncer.cells[0].cellAttr = 0;

						data->hWndNcerViewer = CreateNcerViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 50, data->hWndMdi, &ncer);
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
					case ID_TOOLS_COLORPICKER:
					{
						CHOOSECOLOR cc = { 0 };
						cc.lStructSize = sizeof(cc);
						cc.hInstance = (HWND) (HINSTANCE) GetWindowLong(hWnd, GWL_HINSTANCE); //weird struct definition?
						cc.hwndOwner = hWnd;
						cc.rgbResult = 0;
						cc.Flags = 0x103;
						CustomChooseColor(&cc);
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
	HWND hWndBalance;
	HWND hWndColorBalance;
	HWND hWndEnhanceColors;
	HWND hWndColor0Setting;
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
	HWND hWndMain;
	DWORD *bbits;

	NCLR nclr;
	NCGR ncgr;
	NSCR nscr;
} CREATENSCRDATA;

void nscrCreateCallback(void *data) {
	CREATENSCRDATA *createData = (CREATENSCRDATA *) data;
	HWND hWndMain = createData->hWndMain;
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	HWND hWndMdi = nitroPaintStruct->hWndMdi;

	if (nitroPaintStruct->hWndNscrViewer) DestroyChild(nitroPaintStruct->hWndNscrViewer);
	if (nitroPaintStruct->hWndNcgrViewer) DestroyChild(nitroPaintStruct->hWndNcgrViewer);
	if (nitroPaintStruct->hWndNclrViewer) DestroyChild(nitroPaintStruct->hWndNclrViewer);
	nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, &createData->nclr);
	nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, &createData->ncgr);
	nitroPaintStruct->hWndNscrViewer = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, &createData->nscr);

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
	int color0Setting;
	int balance;
	int colorBalance;
	int enhanceColors;
} THREADEDNSCRCREATEPARAMS;

DWORD WINAPI threadedNscrCreateInternal(LPVOID lpParameter) {
	THREADEDNSCRCREATEPARAMS *params = lpParameter;
	nscrCreate(params->bbits, params->width, params->height, params->bits, params->dither, params->diffuse,
			   params->palette, params->nPalettes, params->fmt, params->tileBase, params->mergeTiles,
			   params->paletteSize, params->paletteOffset, params->rowLimit, params->nMaxChars,
			   params->color0Setting, params->balance, params->colorBalance, params->enhanceColors,
			   &params->data->progress1, &params->data->progress1Max, &params->data->progress2, &params->data->progress2Max,
			   &params->createData->nclr, &params->createData->ncgr, &params->createData->nscr);
	params->data->waitOn = 1;
	return 0;
}

void threadedNscrCreate(PROGRESSDATA *data, DWORD *bbits, int width, int height, int bits, int dither, float diffuse, 
						CREATENSCRDATA *createData, int palette, int nPalettes, int fmt, int tileBase, int mergeTiles, 
						int paletteSize, int paletteOffset, int rowLimit, int nMaxChars, int color0Setting, int balance, 
						int colorBalance, int enhanceColors) {
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
	params->color0Setting = color0Setting;
	params->diffuse = diffuse;
	params->balance = balance;
	params->colorBalance = colorBalance;
	params->enhanceColors = enhanceColors;
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

			int boxWidth = 100 + 100 + 10 + 10 + 10; //box width
			int boxHeight = 6 * 27 - 5 + 10 + 10 + 10; //first row height
			int boxHeight2 = 2 * 27 - 5 + 10 + 10 + 10; //second row height
			int boxHeight3 = 3 * 27 - 5 + 10 + 10 + 10; //third row height
			int width = 30 + 2 * boxWidth; //window width
			int height = 42 + boxHeight + 10 + boxHeight2 + 10 + boxHeight3 + 10 + 22 + 10; //window height

			int leftX = 10 + 10; //left box X
			int rightX = 10 + boxWidth + 10 + 10; //right box X
			int topY = 42 + 10 + 8; //top box Y
			int middleY = 42 + boxHeight + 10 + 10 + 8; //middle box Y
			int bottomY = 42 + boxHeight + 10 + boxHeight2 + 10 + 10 + 8; //bottom box Y

			CreateWindow(L"STATIC", L"Bitmap: ", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 50, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 70, 10, width - 10 - 50 - 70, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateInputButton = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, width - 10 - 50, 10, 50, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Palettes:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndPalettesInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 55, topY, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Base: ", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 55, topY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Size: ", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY + 27 * 2, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteSize = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"256", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 55, topY + 27 * 2, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Offset: ", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY + 27 * 3, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteOffset = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 55, topY + 27 * 3, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color 0:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY + 27 * 4, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndColor0Setting = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, leftX + 55, topY + 27 * 4, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndRowLimit = CreateWindow(L"BUTTON", L"Compress", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, topY + 27 * 5, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndMergeTiles = CreateWindow(L"BUTTON", L"Compress", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, middleY, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Max Characters:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndMaxChars = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1024", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, leftX + 105, middleY + 27, 100, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Depth:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, topY, 50, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateDropdown = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, rightX + 55, topY, 100, 22, hWnd, NULL, NULL, NULL);
			data->nscrCreateDither = CreateWindow(L"BUTTON", L"Dither", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, rightX, topY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Diffuse:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, topY + 27 * 2, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndDiffuse = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, rightX + 55, topY + 27 * 2, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Tile Base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, topY + 27 * 3, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndTileBase = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_AUTOHSCROLL, rightX + 55, topY + 27 * 3, 100, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, middleY, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndFormatDropdown = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, rightX + 55, middleY, 150, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndEnhanceColors = CreateWindow(L"BUTTON", L"Enhance Colors", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, bottomY + 27 * 2, 200, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Lightness", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Green", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Red", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndColorBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY + 27, 200, 22, hWnd, NULL, NULL, NULL);

			//not actually buttons ;)
			CreateWindow(L"BUTTON", L"Palette", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10, 42, boxWidth, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Graphics", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10 + boxWidth + 10, 42, boxWidth, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Char compression", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10, 42 + boxHeight + 10, boxWidth, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Output", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10 + boxWidth + 10, 42 + boxHeight + 10, boxWidth, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Color", WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX, 10, 42 + boxHeight + 10 + boxHeight2 + 10, 10 + 2 * boxWidth, boxHeight3, hWnd, NULL, NULL, NULL);
			data->nscrCreateButton = CreateWindow(L"BUTTON", L"Generate", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, width / 2 - 200 / 2, height - 32, 200, 22, hWnd, NULL, NULL, NULL);

			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 5, (LPARAM) L"4 bit");
			SendMessage(data->nscrCreateDropdown, CB_ADDSTRING, 5, (LPARAM) L"8 bit");
			SendMessage(data->nscrCreateDropdown, CB_SETCURSEL, 1, 0);
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 5, (LPARAM) L"NITRO-System");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 6, (LPARAM) L"Hudson");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 8, (LPARAM) L"Hudson 2");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 8, (LPARAM) L"NITRO-CHARACTER");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 3, (LPARAM) L"Raw");
			SendMessage(data->hWndFormatDropdown, CB_ADDSTRING, 15, (LPARAM) L"Raw Compressed");
			SendMessage(data->hWndFormatDropdown, CB_SETCURSEL, 0, 0);
			SendMessage(data->hWndColor0Setting, CB_ADDSTRING, 5, (LPARAM) L"Fixed");
			SendMessage(data->hWndColor0Setting, CB_ADDSTRING, 7, (LPARAM) L"Average");
			SendMessage(data->hWndColor0Setting, CB_ADDSTRING, 4, (LPARAM) L"Edge");
			SendMessage(data->hWndColor0Setting, CB_ADDSTRING, 11, (LPARAM) L"Contrasting");
			SendMessage(data->hWndColor0Setting, CB_SETCURSEL, 0, 0);
			SendMessage(data->hWndMergeTiles, BM_SETCHECK, BST_CHECKED, 0);

			SendMessage(data->hWndBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);
			SendMessage(data->hWndColorBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndColorBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);

			SetWindowSize(hWnd, width, height);
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
					int balance = SendMessage(data->hWndBalance, TBM_GETPOS, 0, 0);
					int colorBalance = SendMessage(data->hWndColorBalance, TBM_GETPOS, 0, 0);
					int enhanceColors = SendMessage(data->hWndEnhanceColors, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int color0Setting = SendMessage(data->hWndColor0Setting, CB_GETCURSEL, 0, 0);

					if (*location == L'\0') {
						MessageBox(hWnd, L"No image input specified.", L"No Input", MB_ICONERROR);
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
					createData->hWndMain = hWndMain;
					createData->bbits = bbits;
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					progressData->data = createData;
					progressData->callback = nscrCreateCallback;
					SendMessage(hWndProgress, NV_SETDATA, 0, (LPARAM) progressData);

					threadedNscrCreate(progressData, bbits, width, height, bits, dither, diffuse, createData, palette,
									   nPalettes, fmt, tileBase, merge, paletteSize, paletteOffset, rowLimit, nMaxChars,
									   color0Setting, balance, colorBalance, enhanceColors);

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

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
					SetActiveWindow(hWndMain);
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
						unsigned int i;
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

VOID CreateImageDialog(HWND hWnd, LPCWSTR path) {
	HWND h = CreateWindow(L"ImageDialogClass", L"Image Conversion", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX), CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, hWnd, NULL, NULL, NULL);
	SendMessage(h, NV_SETDATA, 0, (LPARAM) path);
	ShowWindow(h, SW_SHOW);
	SetForegroundWindow(h);
	SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_DISABLED);
}

typedef struct {
	WCHAR szPath[MAX_PATH + 1];
	HWND hWndBg;
	HWND hWndTexture;
} IMAGEDLGDATA;

LRESULT CALLBACK ImageDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	IMAGEDLGDATA *data = (IMAGEDLGDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (IMAGEDLGDATA *) calloc(1, sizeof(IMAGEDLGDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case NV_SETDATA:
		{
			LPWSTR path = (LPWSTR) lParam;
			memcpy(data->szPath, path, 2 * (wcslen(path) + 1));

			CreateWindow(L"STATIC", GetFileName(path), WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndBg = CreateWindow(L"BUTTON", L"Create BG", WS_VISIBLE | WS_CHILD, 10, 42, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndTexture = CreateWindow(L"BUTTON", L"Create Texture", WS_VISIBLE | WS_CHILD, 10, 74, 200, 22, hWnd, NULL, NULL, NULL);
			SetWindowSize(hWnd, 220, 106);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndMdi = nitroPaintStruct->hWndMdi;

			if (hWndControl != NULL) {
				if (hWndControl == data->hWndBg) {
					HWND h = CreateWindow(L"CreateDialogClass", L"Create BG", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);

					//pre-populate path
					CREATEDIALOGDATA *cdData = (CREATEDIALOGDATA *) GetWindowLongPtr(h, 0);
					SendMessage(cdData->nscrCreateInput, WM_SETTEXT, wcslen(data->szPath), (LPARAM) data->szPath);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					SetForegroundWindow(h);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
				} else if (hWndControl == data->hWndTexture) {
					CreateTextureEditor(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndMdi, data->szPath);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetForegroundWindow(hWndMain);
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

typedef struct {
	WCHAR szPath[MAX_PATH + 1];
	HWND hWndBitDepth;
	HWND hWndMapping;
	HWND hWndFormat;
	HWND hWndCreate;
} SPRITESHEETDLGDATA;

LRESULT CALLBACK SpriteSheetDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SPRITESHEETDLGDATA *data = (SPRITESHEETDLGDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (SPRITESHEETDLGDATA *) calloc(1, sizeof(SPRITESHEETDLGDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			CreateWindow(L"STATIC", L"8 bit:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndBitDepth = CreateWindow(L"BUTTON", L"", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 70, 10, 22, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Mapping:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 42, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndMapping = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 70, 42, 200, 100, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 74, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndFormat = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 70, 74, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndCreate = CreateWindow(L"BUTTON", L"Create", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 70, 106, 100, 22, hWnd, NULL, NULL, NULL);
			SetWindowSize(hWnd, 280, 138);
			SetGUIFont(hWnd);

			SendMessage(data->hWndMapping, CB_ADDSTRING, 0, (LPARAM) L"Char 2D");
			SendMessage(data->hWndMapping, CB_ADDSTRING, 0, (LPARAM) L"Char 1D 32K");
			SendMessage(data->hWndMapping, CB_ADDSTRING, 0, (LPARAM) L"Char 1D 64K");
			SendMessage(data->hWndMapping, CB_ADDSTRING, 0, (LPARAM) L"Char 1D 128K");
			SendMessage(data->hWndMapping, CB_ADDSTRING, 0, (LPARAM) L"Char 1D 256K");
			SendMessage(data->hWndMapping, CB_SETCURSEL, 0, 0);

			SendMessage(data->hWndFormat, CB_ADDSTRING, 0, (LPARAM) L"NITRO-System");
			SendMessage(data->hWndFormat, CB_ADDSTRING, 0, (LPARAM) L"Hudson");
			SendMessage(data->hWndFormat, CB_ADDSTRING, 0, (LPARAM) L"Hudson 2");
			SendMessage(data->hWndFormat, CB_ADDSTRING, 0, (LPARAM) L"Raw");
			SendMessage(data->hWndFormat, CB_ADDSTRING, 0, (LPARAM) L"Raw Compressed");
			SendMessage(data->hWndFormat, CB_SETCURSEL, 0, 0);

			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_STYLE, GetWindowLong(hWndParent, GWL_STYLE) | WS_DISABLED);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndMdi = nitroPaintStruct->hWndMdi;

			if (hWndControl != NULL) {
				if (hWndControl == data->hWndCreate) {
					int is8bpp = SendMessage(data->hWndBitDepth, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int nBits = is8bpp ? 8 : 4;
					int mapping = SendMessage(data->hWndMapping, CB_GETCURSEL, 0, 0);
					int mappings[] = { GX_OBJVRAMMODE_CHAR_2D, GX_OBJVRAMMODE_CHAR_1D_32K, GX_OBJVRAMMODE_CHAR_1D_64K,
						GX_OBJVRAMMODE_CHAR_1D_128K, GX_OBJVRAMMODE_CHAR_1D_256K };
					int heights[] = { 16, 32, 64, 128, 256 };
					int height = heights[mapping];
					mapping = mappings[mapping];
					int format = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0);

					int charFormats[] = { NCGR_TYPE_NCGR, NCGR_TYPE_HUDSON, NCGR_TYPE_HUDSON2, NCGR_TYPE_BIN, NCGR_TYPE_BIN };
					int palFormats[] = { NCLR_TYPE_NCLR, NCLR_TYPE_HUDSON, NCLR_TYPE_HUDSON, NCLR_TYPE_BIN, NCLR_TYPE_BIN };
					int compression = format == 4 ? COMPRESSION_LZ77 : COMPRESSION_NONE;
					int charFormat = charFormats[format];
					int palFormat = palFormats[format];

					NCLR nclr;
					nclrInit(&nclr, palFormat);
					nclr.colors = (COLOR *) calloc(256, sizeof(COLOR));
					nclr.nColors = 256;
					nclr.totalSize = nclr.nColors * 2;
					nclr.nBits = nBits;
					
					NCGR ncgr;
					ncgrInit(&ncgr, charFormat);
					ncgr.tileWidth = 8;
					ncgr.nBits = nBits;
					ncgr.mappingMode = mapping;
					ncgr.tilesX = 32;
					ncgr.tilesY = height;
					ncgr.nTiles = ncgr.tilesX * ncgr.tilesY;
					ncgr.tiles = (BYTE **) calloc(ncgr.nTiles, sizeof(BYTE *));
					for (int i = 0; i < ncgr.nTiles; i++) {
						ncgr.tiles[i] = (BYTE *) calloc(64, 1);
					}

					if (nitroPaintStruct->hWndNclrViewer != NULL) DestroyChild(nitroPaintStruct->hWndNclrViewer);
					if (nitroPaintStruct->hWndNcgrViewer != NULL) DestroyChild(nitroPaintStruct->hWndNcgrViewer);
					nitroPaintStruct->hWndNclrViewer = NULL;
					nitroPaintStruct->hWndNcgrViewer = NULL;
					nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, &nclr);
					nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, hWndMdi, &ncgr);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetForegroundWindow(hWndMain);
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

void RegisterImageDialogClass() {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"ImageDialogClass";
	wcex.lpfnWndProc = ImageDialogProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
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

void RegisterSpriteSheetDialogClass() {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"SpriteSheetDialogClass";
	wcex.lpfnWndProc = SpriteSheetDialogProc;
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
		result = result && WritePrivateProfileStringW(L"NitroPaint", L"PaletteAlgorithm", L"0", lpszPath);
		result = result && WritePrivateProfileString(L"NitroPaint", L"RenderTransparent", L"1", lpszPath);
		result = result && WritePrivateProfileStringW(L"NitroPaint", L"DPIAware", L"1", lpszPath);
	}
	g_configuration.nclrViewerConfiguration.useDSColorPicker = GetPrivateProfileInt(L"NclrViewer", L"UseDSColorPicker", 0, lpszPath);
	g_configuration.ncgrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NcgrViewer", L"Gridlines", 1, lpszPath);
	g_configuration.nscrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NscrViewer", L"Gridlines", 0, lpszPath);
	g_configuration.fullPaths = GetPrivateProfileInt(L"NitroPaint", L"FullPaths", 1, lpszPath);
	g_configuration.renderTransparent = GetPrivateProfileInt(L"NitroPaint", L"RenderTransparent", 1, lpszPath);
	g_configuration.backgroundPath = (LPWSTR) calloc(MAX_PATH, sizeof(WCHAR));
	g_configuration.dpiAware = GetPrivateProfileInt(L"NitroPaint", L"DPIAware", 1, lpszPath);
	GetPrivateProfileString(L"NitroPaint", L"Background", L"", g_configuration.backgroundPath, MAX_PATH, lpszPath);

	//load background image
	if (g_configuration.backgroundPath[0] != L'\0') {
		int width = 0, height = 0;
		COLOR32 *bits = gdipReadImage(g_configuration.backgroundPath, &width, &height);
		if (bits != NULL) {
			for (int i = 0; i < width * height; i++) bits[i] = REVERSE(bits[i]);
			HBITMAP hbm = CreateBitmap(width, height, 1, 32, bits);
			g_configuration.hbrBackground = CreatePatternBrush(hbm);
			free(bits);
		}
	}
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
	RegisterImageDialogClass();
	RegisterSpriteSheetDialogClass();
	RegisterNmcrViewerClass();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	HACCEL hAccel = LoadAccelerators(hInstance, (LPCWSTR) IDR_ACCELERATOR1);
	CoInitialize(NULL);

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

	//set DPI awareness
	if (g_configuration.dpiAware) {
		HMODULE hUser32 = GetModuleHandle(L"USER32.DLL");
		BOOL (WINAPI *SetProcessDPIAwareFunc) (void) = (BOOL (WINAPI *) (void)) 
			GetProcAddress(hUser32, "SetProcessDPIAware");
		if (SetProcessDPIAwareFunc != NULL) {
			SetProcessDPIAwareFunc();
		}
	}

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