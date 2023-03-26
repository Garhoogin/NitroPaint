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
#include "ui.h"

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

LPWSTR openFilesDialog(HWND hWnd, LPCWSTR title, LPCWSTR filter, LPCWSTR extension) {
	OPENFILENAME o = { 0 };
	int bufLen = (MAX_PATH + 1) * 32;  //32 max-paths should be enough
	LPWSTR fname = (LPWSTR) calloc(bufLen + 1, sizeof(WCHAR));
	o.lStructSize = sizeof(o);
	o.hwndOwner = hWnd;
	o.nMaxFile = bufLen;
	o.lpstrTitle = title;
	o.lpstrFilter = filter;
	o.nMaxCustFilter = 255;
	o.lpstrFile = fname;
	o.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT;
	o.lpstrDefExt = extension;
	if (GetOpenFileName(&o)) {
		//replace all NUL characters with | (not allowed, so we use as a separator)
		LPWSTR ptr = fname;
		while (1) {
			if (*ptr == L'\0' && ptr[1] == L'\0') break;
			if (*ptr == L'\0') {
				*ptr = L'|';
			}
			ptr++;
		}
		return fname;
	}
	free(fname);
	return NULL;
}

int getPathCount(LPCWSTR paths) {
	//count number of pipe characters
	int nPipes = 0;
	for (unsigned int i = 0; i < wcslen(paths); i++) {
		if (paths[i] == L'|') nPipes++;
	}
	if (nPipes == 0) return 1;
	return nPipes;
}

void getPathFromPaths(LPCWSTR paths, int index, WCHAR *path) {
	//if no pipe, copy as is
	int firstPipe = -1;
	for (unsigned int i = 0; i < wcslen(paths); i++) {
		if (paths[i] == L'|') {
			firstPipe = i;
			break;
		}
	}

	//return path as-is
	if (firstPipe == -1) {
		memcpy(path, paths, (wcslen(paths) + 1) * sizeof(WCHAR));
		return;
	}

	//copy up to first pipe
	memcpy(path, paths, firstPipe * sizeof(WCHAR));
	path[firstPipe] = L'\0';

	//add \ if missing
	if (firstPipe == 0 || path[firstPipe - 1] != L'\\' || path[firstPipe - 1] != L'/') {
		path[firstPipe] = L'\\';
		path[firstPipe + 1] = L'\0';
	}

	//find segment
	int seg = 0;
	int ofs = wcslen(path);
	for (unsigned int i = 0; i < wcslen(paths); i++) {
		if (paths[i] == L'|') {
			seg++;
			continue;
		}
		if (seg == index + 1) {
			path[ofs] = paths[i];
			ofs++;
		}
	}
	path[ofs] = L'\0';
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

int PromptUserText(HWND hWndParent, LPCWSTR title, LPCWSTR prompt, LPWSTR text, int maxLength) {
	//create a prompt
	int status = 0;
	HWND hWnd = CreateWindow(L"TextPromptClass", title, WS_SYSMENU | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT,
		300, 200, hWndParent, NULL, NULL, NULL);
	SendMessage(hWnd, NV_INITIALIZE, (WPARAM) prompt, (LPARAM) text);
	SetWindowLong(hWnd, 4 * sizeof(void *), maxLength);
	SetWindowLong(hWnd, 0 * sizeof(void *), (LONG) &status);
	ShowWindow(hWnd, SW_SHOW);
	DoModal(hWnd);
	return status;
}

void SetGUIFont(HWND hWnd);

LRESULT CALLBACK TextInputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndOK = (HWND) GetWindowLongPtr(hWnd, 1 * sizeof(void *));
	HWND hWndEdit = (HWND) GetWindowLongPtr(hWnd, 2 * sizeof(void *));
	WCHAR *outBuffer = (WCHAR *) GetWindowLong(hWnd, 3 * sizeof(void *));

	switch (msg) {
		case WM_CREATE:
			SetWindowLong(hWnd, 0 * sizeof(void *), 0); //status
			SetWindowSize(hWnd, 225, 96);
			break;
		case NV_INITIALIZE:
		{
			LPCWSTR prompt = (LPCWSTR) wParam;
			LPWSTR textBuffer = (LPWSTR) lParam;
			CreateStatic(hWnd, prompt, 10, 10, 205, 22);
			hWndEdit = CreateEdit(hWnd, textBuffer, 10, 37, 205, 22, FALSE);
			HWND hWndCancel = CreateButton(hWnd, L"Cancel", 10, 64, 100, 22, FALSE);
			hWndOK = CreateButton(hWnd, L"OK", 115, 64, 100, 22, TRUE);

			//set focus and select all
			SetFocus(hWndEdit);
			SendMessage(hWndEdit, EM_SETSEL, 0, -1);

			SetWindowLong(hWnd, 1 * sizeof(void *), (LONG) hWndOK);
			SetWindowLong(hWnd, 2 * sizeof(void *), (LONG) hWndEdit);
			SetWindowLong(hWnd, 3 * sizeof(void *), (LONG) textBuffer);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			WORD notif = HIWORD(wParam);
			WORD idc = LOWORD(wParam);
			if (notif == BN_CLICKED && (hWndControl != NULL || idc)) {
				
				//if OK, set status to 1 and copy text.
				int *pStatus = (int *) GetWindowLong(hWnd, 0 * sizeof(void *));
				*pStatus = 0;
				if (hWndControl == hWndOK || idc == IDOK) {
					//get length of user text. If it's too long, we should let them know.
					int textLength = SendMessage(hWndEdit, WM_GETTEXTLENGTH, 0, 0);
					int bufferLength = GetWindowLong(hWnd, 4 * sizeof(void *));
					if (textLength + 1 > bufferLength) {
						WCHAR strbuf[48];
						wsprintfW(strbuf, L"Too long. Maximum length: %d", bufferLength - 1);
						MessageBox(hWnd, strbuf, L"Too Long", MB_ICONERROR);
						SetFocus(hWndEdit);
						SendMessage(hWndEdit, EM_SETSEL, 0, -1);
						break;
					} else {
						//success, copy out and raise status high
						SendMessage(hWndEdit, WM_GETTEXT, bufferLength, (LPARAM) outBuffer);
						*pStatus = 1;
					}
				}

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void copyBitmap(COLOR32 *img, int width, int height) {
	HGLOBAL hDib = NULL;
	int dibSize = width * height * 3 + sizeof(BITMAPINFOHEADER);
	hDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dibSize);

	//populate common info
	BITMAPINFOHEADER *bmi = (BITMAPINFOHEADER *) GlobalLock(hDib);
	BYTE *bmiBits = (BYTE *) (bmi + 1);
	bmi->biSize = sizeof(BITMAPINFOHEADER);
	bmi->biCompression = BI_RGB;
	bmi->biHeight = height;
	bmi->biWidth = width;
	bmi->biPlanes = 1;
	bmi->biBitCount = 24;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			COLOR32 c = img[x + (height - 1 - y) * width];
			bmiBits[(x + y * width) * 3 + 0] = (c >> 16) & 0xFF;
			bmiBits[(x + y * width) * 3 + 1] = (c >> 8) & 0xFF;
			bmiBits[(x + y * width) * 3 + 2] = (c >> 0) & 0xFF;
		}
	}

	GlobalUnlock(hDib);
	SetClipboardData(CF_DIB, hDib);
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

BOOL CALLBACK CloseAllProc(HWND hWnd, LPARAM lParam) {
	HWND hWndMdi = (HWND) lParam;
	if ((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT) != hWndMdi) return TRUE;
	DestroyChild(hWnd);
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
		while (ptr != end  && *ptr != ':' && *ptr != '\r' && *ptr != '\n') ptr++;
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

		//create NSCR editor and make it active
		if (scrRef != NULL) {
			HWND hWndNscrViewer = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, &nscr);

			NSCR *pNscr = &((NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0))->nscr;
			combo->nscr = pNscr;
			memcpy(((NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0))->szOpenFile, pathBuffer, 2 * (wcslen(pathBuffer) + 1));
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
			//create editor
			CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);
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

			//create NSCR and make it active
			HWND hWndNscrViewer = NULL;
			if (combo2dFormatHasScreen(type)) {
				hWndNscrViewer = CreateNscrViewer(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, path);
			}

			//create a combo frame
			COMBO2D *combo = (COMBO2D *) calloc(1, sizeof(COMBO2D));
			combo2dRead(combo, buffer, dwSize);
			NCLR *nclr = NULL;
			NCGR *ncgr = NULL;
			NSCR *nscr = NULL;

			if (combo2dFormatHasPalette(type)) nclr = &((NCLRVIEWERDATA *) GetWindowLongPtr(data->hWndNclrViewer, 0))->nclr;
			if (combo2dFormatHasCharacter(type)) ncgr = &((NCGRVIEWERDATA *) GetWindowLongPtr(data->hWndNcgrViewer, 0))->ncgr;
			if (combo2dFormatHasScreen(type)) nscr = &((NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0))->nscr;

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

//general editor utilities for main window

int GetEditorType(HWND hWndEditor) {
	return SendMessage(hWndEditor, NV_GETTYPE, 0, 0);
}

BOOL CALLBACK InvalidateAllEditorsProc(HWND hWnd, LPARAM lParam) {
	int editorType = GetEditorType(hWnd);
	if (editorType == lParam || (editorType != FILE_TYPE_INVALID && lParam == FILE_TYPE_INVALID)) {
		InvalidateRect(hWnd, NULL, FALSE);
	}
	return TRUE;
}

void InvalidateAllEditors(HWND hWndMain, int type) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
	HWND hWndMdi = nitroPaintStruct->hWndMdi;
	EnumChildWindows(hWndMdi, InvalidateAllEditorsProc, type);
}

BOOL CALLBACK EnumAllEditorsProc(HWND hWnd, LPARAM lParam) {
	struct { BOOL (*pfn) (HWND, void *); void *param; int type; } *data = (void *) lParam;
	int type = GetEditorType(hWnd);
	if (type == data->type || (type != FILE_TYPE_INVALID && data->type == FILE_TYPE_INVALID)) {
		return data->pfn(hWnd, data->param);
	}
	return TRUE;
}

void EnumAllEditors(HWND hWndMain, int type, BOOL (*pfn) (HWND, void *), void *param) {
	struct { BOOL (*pfn) (HWND, void *); void *param; int type; } data = { pfn, param, type };
	EnumChildWindows(hWndMain, EnumAllEditorsProc, (LPARAM) &data);
}

BOOL GetAllEditorsProc(HWND hWnd, void *param) {
	struct { int nCounted; HWND *buffer; int bufferSize; } *work = param;
	if (work->nCounted < work->bufferSize) {
		work->buffer[work->nCounted] = hWnd;
	}
	work->nCounted++;
	return TRUE;
}

int GetAllEditors(HWND hWndMain, int type, HWND *editors, int bufferSize) {
	struct { int nCounted; HWND *buffer; int bufferSize; } param = { 0, editors, bufferSize };
	EnumAllEditors(hWndMain, type, GetAllEditorsProc, (void *) &param);
	return param.nCounted;
}

BOOL SetNscrEditorTransparentProc(HWND hWnd, void *param) {
	NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	int state = (int) param;
	nscrViewerData->transparent = state;
	return TRUE;
}

VOID HandleSwitch(LPWSTR lpSwitch) {
	if (!wcsncmp(lpSwitch, L"EVENT:", 6)) {
		g_hEvent = (HANDLE) _wtol(lpSwitch + 6);
	}
}

VOID OpenFileByNameRemote(HWND hWnd, LPCWSTR szFile) {
	COPYDATASTRUCT cds = { 0 };
	cds.dwData = NPMSG_OPENFILE;
	cds.cbData = (wcslen(szFile) + 1) * 2;
	cds.lpData = (PVOID) szFile;
	SendMessage(hWnd, WM_COPYDATA, 0, (LPARAM) &cds);
}

VOID ProcessCommandLine(HWND hWnd, BOOL remoteWindow) {
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
				if (!remoteWindow) {
					OpenFileByName(hWnd, argv[i]);
				} else {
					OpenFileByNameRemote(hWnd, argv[i]);
				}
			} else {
				//command line switch
				HandleSwitch(arg + 1);
			}
		}
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
			ProcessCommandLine(hWnd, FALSE);

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
			if (!g_configuration.allowMultipleInstances) {
				CheckMenuItem(GetMenu(hWnd), ID_VIEW_SINGLE, MF_CHECKED);
			}
			return 1;
		}
		case WM_COPYDATA:
		{
			HWND hWndOrigin = (HWND) wParam;
			COPYDATASTRUCT *copyData = (COPYDATASTRUCT *) lParam;
			int type = copyData->dwData;

			switch (type) {
				case NPMSG_OPENFILE:
					OpenFileByName(hWnd, (LPCWSTR) copyData->lpData);
					break;
			}
			break;
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
					case ID_ACCELERATOR_CLOSEALL:
						PostMessage(hWnd, WM_COMMAND, ID_FILE_CLOSEALL, 0);
						break;
					case ID_ACCELERATOR_EXPORT:
						PostMessage(hWnd, WM_COMMAND, ID_FILE_EXPORT, 0);
						break;
					case ID_ACCELERATOR_OPEN:
						PostMessage(hWnd, WM_COMMAND, ID_FILE_OPEN40085, 0);
						break;
					case ID_ACCELERATOR_ZOOMIN:
					case ID_ACCELERATOR_ZOOMIN2:
						MainZoomIn(hWnd);
						break;
					case ID_ACCELERATOR_ZOOMOUT:
					case ID_ACCELERATOR_ZOOMOUT2:
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
													 L"All Supported Files\0*.nclr;*.rlcn;*.ncl;*.5pl;*.5pc;*.ntfp;*.nbfp;*.bin;*.pltt;*.ncgr;*.rgcn;*.ncbr;*.nbfc;*.char;*.nscr;*.rcsn;*.nbfs;*.ncer;*.recn;*.nanr;*.rnan;*.dat;*.nsbmd;*.nsbtx;*.bmd;*.bnr;*.tga\0"
													 L"Palette Files (*.nclr, *.rlcn, *.ncl, *.5pl, *.5pc, *ncl.bin, *icl.bin, *.ntfp, *.nbfp, *.pltt, *.bin)\0*.nclr;*.rlcn;*.ncl;*.5pc;*.5pl;*ncl.bin;*.ntfp;*.nbfp;*.pltt;*.bin\0"
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
					case ID_FILE_CLOSEALL:
					{
						HWND hWndMdi = data->hWndMdi;
						EnumChildWindows(hWndMdi, CloseAllProc, (LPARAM) hWndMdi);
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
					case ID_NEW_NEWPALETTE:
					{
						if (data->hWndNclrViewer != NULL) DestroyChild(data->hWndNclrViewer);
						data->hWndNclrViewer = NULL;

						NCLR nclr;
						nclrInit(&nclr, NCLR_TYPE_NCLR);
						nclr.nColors = 256;
						nclr.nBits = 4;
						nclr.nPalettes = 16;
						nclr.colors = (COLOR *) calloc(256, sizeof(COLOR));
						data->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, &nclr);
						break;
					}
					case ID_NEW_NEWSCREEN:
					{
						HWND h = CreateWindow(L"NewScreenDialogClass", L"New Screen", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						ShowWindow(h, SW_SHOW);
						SetActiveWindow(h);
						SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_DISABLED);
						break;
					}
					case ID_FILE_CONVERTTO:
					{
						HWND hWndFocused = (HWND) SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, 0);
						if (hWndFocused == NULL) break;

						int editorType = GetEditorType(hWndFocused);
						if (editorType != FILE_TYPE_PALETTE && editorType != FILE_TYPE_CHAR
							&& editorType != FILE_TYPE_SCREEN && editorType != FILE_TYPE_CELL) break;

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
					case ID_VIEW_SINGLE:
					{
						int state = GetMenuState(GetMenu(hWnd), ID_VIEW_SINGLE, MF_BYCOMMAND);
						state = !state;
						if (state) {
							WritePrivateProfileStringW(L"NitroPaint", L"AllowMultiple", L"0", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_SINGLE, MF_CHECKED);
						} else {
							WritePrivateProfileStringW(L"NitroPaint", L"AllowMultiple", L"1", g_configPath);
							CheckMenuItem(GetMenu(hWnd), ID_VIEW_SINGLE, MF_UNCHECKED);
						}
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
						}
						InvalidateAllEditors(hWnd, FILE_TYPE_CHAR);
						EnumAllEditors(hWnd, FILE_TYPE_SCREEN, SetNscrEditorTransparentProc, (void *) state);
						InvalidateAllEditors(hWnd, FILE_TYPE_SCREEN);
						break;
					}
					case ID_NTFT_NTFT40084:
					{
						CreateWindow(L"NtftConvertDialogClass", L"NTFT To Texture", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
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
					case ID_SCREEN_SPLITSCREEN:
					{
						HWND hWndFocus = (HWND) SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, 0);
						
						//if not screen, warn user
						if (hWndFocus == NULL || GetEditorType(hWndFocus) != FILE_TYPE_SCREEN) {
							MessageBox(hWnd, L"NO screen active.", L"Error", MB_ICONERROR);
							break;
						}
						HWND h = CreateWindow(L"ScreenSplitDialogClass", L"Split Screen", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						SetActiveWindow(h);
						setStyle(hWnd, TRUE, WS_DISABLED);
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
	HWND hWndAlignmentCheckbox;
	HWND hWndAlignment;
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

void RegisterGenericClass(LPCWSTR lpszClassName, WNDPROC pWndProc, int cbWndExtra) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = lpszClassName;
	wcex.lpfnWndProc = pWndProc;
	wcex.cbWndExtra = cbWndExtra;
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
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

	if (nitroPaintStruct->hWndNcgrViewer) DestroyChild(nitroPaintStruct->hWndNcgrViewer);
	if (nitroPaintStruct->hWndNclrViewer) DestroyChild(nitroPaintStruct->hWndNclrViewer);
	nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, &createData->nclr);
	nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, &createData->ncgr);
	CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, &createData->nscr);

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
	int alignment;
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
			   params->palette, params->nPalettes, params->fmt, params->tileBase, params->mergeTiles, params->alignment,
			   params->paletteSize, params->paletteOffset, params->rowLimit, params->nMaxChars,
			   params->color0Setting, params->balance, params->colorBalance, params->enhanceColors,
			   &params->data->progress1, &params->data->progress1Max, &params->data->progress2, &params->data->progress2Max,
			   &params->createData->nclr, &params->createData->ncgr, &params->createData->nscr);
	params->data->waitOn = 1;
	return 0;
}

void threadedNscrCreate(PROGRESSDATA *data, DWORD *bbits, int width, int height, int bits, int dither, float diffuse, 
						CREATENSCRDATA *createData, int palette, int nPalettes, int fmt, int tileBase, int mergeTiles,
						int alignment, int paletteSize, int paletteOffset, int rowLimit, int nMaxChars, int color0Setting,
						int balance, int colorBalance, int enhanceColors) {
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
	params->alignment = alignment;
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
			setStyle(hWndParent, TRUE, WS_DISABLED);

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

			CreateStatic(hWnd, L"Bitmap:", 10, 10, 50, 22);
			data->nscrCreateInput = CreateEdit(hWnd, L"", 70, 10, width - 10 - 50 - 70, 22, FALSE);
			data->nscrCreateInputButton = CreateButton(hWnd, L"...", width - 10 - 50, 10, 50, 22, FALSE);

			LPCWSTR color0Settings[] = { L"Fixed", L"Average", L"Edge", L"Contrasting" };
			CreateStatic(hWnd, L"Palettes:", leftX, topY, 50, 22);
			data->hWndPalettesInput = CreateEdit(hWnd, L"1", leftX + 55, topY, 100, 22, TRUE);
			CreateStatic(hWnd, L"Base:", leftX, topY + 27, 50, 22);
			data->hWndPaletteInput = CreateEdit(hWnd, L"0", leftX + 55, topY + 27, 100, 22, TRUE);
			CreateStatic(hWnd, L"Size:", leftX, topY + 27 * 2, 50, 22);
			data->hWndPaletteSize = CreateEdit(hWnd, L"256", leftX + 55, topY + 27 * 2, 100, 22, TRUE);
			CreateStatic(hWnd, L"Offset:", leftX, topY + 27 * 3, 50, 22);
			data->hWndPaletteOffset = CreateEdit(hWnd, L"0", leftX + 55, topY + 27 * 3, 100, 22, TRUE);
			CreateStatic(hWnd, L"Color 0:", leftX, topY + 27 * 4, 50, 22);
			data->hWndColor0Setting = CreateCombobox(hWnd, color0Settings, sizeof(color0Settings) / sizeof(*color0Settings), leftX + 55, topY + 27 * 4, 100, 22, 0);
			data->hWndRowLimit = CreateCheckbox(hWnd, L"Compress", leftX, topY + 27 * 5, 100, 22, FALSE);

			data->hWndMergeTiles = CreateCheckbox(hWnd, L"Compress", leftX, middleY, 100, 22, TRUE);
			CreateStatic(hWnd, L"Max Characters:", leftX, middleY + 27, 100, 22);
			data->hWndMaxChars = CreateEdit(hWnd, L"1024", leftX + 105, middleY + 27, 100, 22, TRUE);

			LPCWSTR bitDepths[] = { L"4 bit", L"8 bit" };
			CreateStatic(hWnd, L"Depth:", rightX, topY, 50, 22);
			data->nscrCreateDropdown = CreateCombobox(hWnd, bitDepths, sizeof(bitDepths) / sizeof(*bitDepths), rightX + 55, topY, 100, 22, 1);
			data->nscrCreateDither = CreateCheckbox(hWnd, L"Dither", rightX, topY + 27, 100, 22, FALSE);
			CreateStatic(hWnd, L"Diffuse:", rightX, topY + 27 * 2, 50, 22);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", rightX + 55, topY + 27 * 2, 100, 22, TRUE);
			CreateStatic(hWnd, L"Tile Base:", rightX, topY + 27 * 3, 50, 22);
			data->hWndTileBase = CreateEdit(hWnd, L"0", rightX + 55, topY + 27 * 3, 100, 22, TRUE);
			data->hWndAlignmentCheckbox = CreateCheckbox(hWnd, L"Align Size:", rightX, topY + 27 * 4, 75, 22, TRUE);
			data->hWndAlignment = CreateEdit(hWnd, L"32", rightX + 75, topY + 27 * 4, 80, 22, TRUE);
			setStyle(data->hWndDiffuse, TRUE, WS_DISABLED);

			LPCWSTR formatNames[] = { L"NITRO-System", L"Hudson", L"Hudson 2", L"NITRO-CHARACTER", L"Raw", L"Raw Compressed" };
			CreateStatic(hWnd, L"Format:", rightX, middleY, 50, 22);
			data->hWndFormatDropdown = CreateCombobox(hWnd, formatNames, sizeof(formatNames) / sizeof(*formatNames), rightX + 55, middleY, 150, 22, 0);

			CreateStatic(hWnd, L"Balance:", leftX, bottomY, 100, 22);
			CreateStatic(hWnd, L"Color Balance:", leftX, bottomY + 27, 100, 22);
			data->hWndEnhanceColors = CreateCheckbox(hWnd, L"Enhance Colors", leftX, bottomY + 27 * 2, 200, 22, FALSE);

			CreateStaticAligned(hWnd, L"Lightness", leftX + 110, bottomY, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Color", leftX + 110 + 50 + 200, bottomY, 50, 22, SCA_LEFT);
			CreateStaticAligned(hWnd, L"Green", leftX + 110, bottomY + 27, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Red", leftX + 110 + 50 + 200, bottomY + 27, 50, 22, SCA_LEFT);
			data->hWndBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			data->hWndColorBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY + 27, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);

			//not actually buttons ;)
			CreateGroupbox(hWnd, L"Palette", 10, 42, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Graphics", 10 + boxWidth + 10, 42, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Char compression", 10, 42 + boxHeight + 10, boxWidth, boxHeight2);
			CreateGroupbox(hWnd, L"Output", 10 + boxWidth + 10, 42 + boxHeight + 10, boxWidth, boxHeight2);
			CreateGroupbox(hWnd, L"Color", 10, 42 + boxHeight + 10 + boxHeight2 + 10, 10 + 2 * boxWidth, boxHeight3);
			data->nscrCreateButton = CreateButton(hWnd, L"Generate", width / 2 - 200 / 2, height - 32, 200, 22, TRUE);

			SetWindowSize(hWnd, width, height);
			SetGUIFont(hWnd);
			return 1;
		}
		case WM_CLOSE:
		{
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			setStyle(hWndParent, FALSE, WS_DISABLED);
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
				} else if (hWndControl == data->nscrCreateButton) {
					WCHAR location[MAX_PATH + 1];
					int dither = GetCheckboxChecked(data->nscrCreateDither);
					int merge = GetCheckboxChecked(data->hWndMergeTiles);
					int rowLimit = GetCheckboxChecked(data->hWndRowLimit);
					int doAlign = GetCheckboxChecked(data->hWndAlignmentCheckbox);
					int fmt = SendMessage(data->hWndFormatDropdown, CB_GETCURSEL, 0, 0);
					int bitsOptions[] = { 4, 8 };
					int bits = bitsOptions[SendMessage(data->nscrCreateDropdown, CB_GETCURSEL, 0, 0)];
					int palette = GetEditNumber(data->hWndPaletteInput);
					int nPalettes = GetEditNumber(data->hWndPalettesInput);
					int tileBase = GetEditNumber(data->hWndTileBase);
					int paletteSize = GetEditNumber(data->hWndPaletteSize);
					int paletteOffset = GetEditNumber(data->hWndPaletteOffset);
					int nMaxChars = GetEditNumber(data->hWndMaxChars);
					int alignment = doAlign ? GetEditNumber(data->hWndAlignment) : 1;
					float diffuse = ((float) GetEditNumber(data->hWndDiffuse)) * 0.01f;
					SendMessage(data->nscrCreateInput, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);
					int balance = GetTrackbarPosition(data->hWndBalance);
					int colorBalance = GetTrackbarPosition(data->hWndColorBalance);
					int enhanceColors = GetCheckboxChecked(data->hWndEnhanceColors);
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
						nPalettes, fmt, tileBase, merge, alignment, paletteSize, paletteOffset, rowLimit, nMaxChars,
						color0Setting, balance, colorBalance, enhanceColors);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					SetActiveWindow(hWndProgress);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);

				} else if (hWndControl == data->hWndMergeTiles) {
					//enable/disable max chars field
					int state = GetCheckboxChecked(hWndControl);
					setStyle(data->hWndMaxChars, !state, WS_DISABLED);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (hWndControl == data->hWndAlignmentCheckbox) {
					//enable/disable alignment amount field
					int state = GetCheckboxChecked(hWndControl);
					setStyle(data->hWndAlignment, !state, WS_DISABLED);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (hWndControl == data->nscrCreateDither) {
					//enable/disable diffusion amount field
					int state = GetCheckboxChecked(hWndControl);
					setStyle(data->hWndDiffuse, !state, WS_DISABLED);
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
	RegisterGenericClass(L"CreateDialogClass", CreateDialogWndProc, sizeof(LPVOID));
}

LRESULT WINAPI ProgressWindowWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
		{
			CreateStatic(hWnd, L"Palette:", 10, 10, 100, 22);
			CreateStatic(hWnd, L"Character compression:", 10, 69, 150, 22);
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
					setStyle(hWndMain, FALSE, WS_DISABLED);
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
			setStyle(hWndMain, FALSE, WS_DISABLED);
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
			CreateStatic(hWnd, L"Format:", 10, 10, 50, 22);
			CreateStatic(hWnd, L"NTFT:", 10, 37, 50, 22);
			CreateStatic(hWnd, L"NTFP:", 10, 64, 50, 22);
			CreateStatic(hWnd, L"NTFI:", 10, 91, 50, 22);
			CreateStatic(hWnd, L"Width:", 10, 118, 50, 22);
			data->hWndFormat = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, 70, 10, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndNtftInput = CreateEdit(hWnd, L"", 70, 37, 170, 22, FALSE);
			data->hWndNtftBrowseButton = CreateButton(hWnd, L"...", 240, 37, 30, 22, FALSE);
			data->hWndNtfpInput = CreateEdit(hWnd, L"", 70, 64, 170, 22, FALSE);
			data->hWndNtfpBrowseButton = CreateButton(hWnd, L"...", 240, 64, 30, 22, FALSE);
			data->hWndNtfiInput = CreateEdit(hWnd, L"", 70, 91, 170, 22, FALSE);
			data->hWndNtfiBrowseButton = CreateButton(hWnd, L"...", 240, 91, 30, 22, FALSE);
			data->hWndWidthInput = CreateEdit(hWnd, L"8", 70, 118, 100, 22, TRUE);
			data->hWndConvertButton = CreateButton(hWnd, L"Convert", 70, 145, 100, 22, TRUE);
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
			SendMessage(data->hWndFormat, CB_SETCURSEL, CT_4x4 - 1, 0);
			
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			setStyle(hWndParent, TRUE, WS_DISABLED);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
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
				} else if (hWndControl == data->hWndFormat && notif == CBN_SELCHANGE) {
					//every format needs NTFT. But not all NTFI or NTFP
					int fmt = SendMessage(hWndControl, CB_GETCURSEL, 0, 0) + 1; //1-based since entry 0 corresponds to format 1
					
					//only 4x4 needs NTFI.
					int needsNtfi = fmt == CT_4x4;
					setStyle(data->hWndNtfiInput, !needsNtfi, WS_DISABLED);
					setStyle(data->hWndNtfiBrowseButton, !needsNtfi, WS_DISABLED);

					//only direct doesn't need and NTFP.
					int needsNtfp = fmt != CT_DIRECT;
					setStyle(data->hWndNtfpInput, !needsNtfp, WS_DISABLED);
					setStyle(data->hWndNtfpBrowseButton, !needsNtfp, WS_DISABLED);

					//update
					InvalidateRect(hWnd, NULL, FALSE);
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

					//texture editor takes ownership of texture data, no need to free
					HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
					HWND hWndMdi = nitroPaintStruct->hWndMdi;
					CreateTextureEditorImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndMdi, &texture);

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
	RegisterGenericClass(L"NtftConvertDialogClass", NtftConvertDialogProc, sizeof(LPVOID));
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

			CreateStatic(hWnd, L"Format:", 10, 10, 100, 22);
			CreateStatic(hWnd, L"Compression:", 10, 37, 100, 22);
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

			CreateStatic(hWnd, GetFileName(path), 10, 10, 200, 22);
			data->hWndBg = CreateButton(hWnd, L"Create BG", 10, 42, 200, 22, FALSE);;
			data->hWndTexture = CreateButton(hWnd, L"Create Texture", 10, 74, 200, 22, FALSE);
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
					setStyle(hWndMain, TRUE, WS_DISABLED);
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
			setStyle(hWndMain, FALSE, WS_DISABLED);
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
			LPCWSTR mappings[] = {
				L"Char 2D", L"Char 1D 32K", L"Char 1D 64K", L"Char 1D 128K", L"Char 1D 256K"
			};
			LPCWSTR formats[] = {
				L"NITRO-System", L"Hudson", L"Hudson 2", L"Raw", L"Raw Compressed"
			};

			CreateStatic(hWnd, L"8 bit:", 10, 10, 50, 22);
			data->hWndBitDepth = CreateCheckbox(hWnd, L"", 70, 10, 22, 22, FALSE);
			CreateStatic(hWnd, L"Mapping:", 10, 42, 50, 22);
			data->hWndMapping = CreateCombobox(hWnd, mappings, sizeof(mappings) / sizeof(*mappings), 70, 42, 200, 100, 0);
			CreateStatic(hWnd, L"Format:", 10, 74, 50, 22);
			data->hWndFormat = CreateCombobox(hWnd, formats, sizeof(formats) / sizeof(*formats), 70, 74, 100, 100, 0);
			data->hWndCreate = CreateButton(hWnd, L"Create", 70, 106, 100, 22, TRUE);
			SetWindowSize(hWnd, 280, 138);
			SetGUIFont(hWnd);

			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			setStyle(hWndParent, TRUE, WS_DISABLED);
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
					int is8bpp = GetCheckboxChecked(data->hWndBitDepth);
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
					ncgr.header.compression = compression;
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
			setStyle(hWndMain, FALSE, WS_DISABLED);
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

typedef struct CREATESCREENDATA_ {
	HWND hWndWidth;
	HWND hWndHeight;
	HWND hWndCreate;
} CREATESCREENDATA;

LRESULT CALLBACK NewScreenDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CREATESCREENDATA *data = (CREATESCREENDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (CREATESCREENDATA *) calloc(1, sizeof(CREATESCREENDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			CreateStatic(hWnd, L"Width:", 10, 10, 75, 22);
			CreateStatic(hWnd, L"Height:", 10, 37, 75, 22);
			
			data->hWndWidth = CreateEdit(hWnd, L"256", 85, 10, 100, 22, TRUE);
			data->hWndHeight = CreateEdit(hWnd, L"256", 85, 37, 100, 22, TRUE);
			data->hWndCreate = CreateButton(hWnd, L"Create", 85, 64, 100, 22, TRUE);
			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 195, 96);
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL && hWndControl == data->hWndCreate) {
				HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);

				int width = GetEditNumber(data->hWndWidth);
				int tilesX = width / 8;
				int height = GetEditNumber(data->hWndHeight);
				int tilesY = height / 8;

				NSCR nscr;
				nscrInit(&nscr, NSCR_TYPE_NSCR);
				nscr.nWidth = tilesX * 8;
				nscr.nHeight = tilesY * 8;
				nscr.dataSize = tilesX * tilesY * sizeof(uint16_t);
				nscr.data = (uint16_t *) calloc(tilesX * tilesY, sizeof(uint16_t));
				CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, nitroPaintStruct->hWndMdi, &nscr);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			setStyle(hWndMain, FALSE, WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
		case WM_DESTROY:
		{
			if (data != NULL) {
				free(data);
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

typedef struct SCREENSPLITDIALOGDATA_ {
	HWND hWndX;
	HWND hWndY;
	HWND hWndComplete;
} SCREENSPLITDIALOGDATA;

LRESULT CALLBACK ScreenSplitDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SCREENSPLITDIALOGDATA *data = (SCREENSPLITDIALOGDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (SCREENSPLITDIALOGDATA *) calloc(1, sizeof(SCREENSPLITDIALOGDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			CreateStatic(hWnd, L"X Screens:", 10, 10, 75, 22);
			CreateStatic(hWnd, L"Y Screens:", 10, 37, 75, 22);

			data->hWndX = CreateEdit(hWnd, L"1", 85, 10, 100, 22, TRUE);
			data->hWndY = CreateEdit(hWnd, L"1", 85, 37, 100, 22, TRUE);
			data->hWndComplete = CreateButton(hWnd, L"Complete", 85, 64, 100, 22, TRUE);

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 195, 96);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl == data->hWndComplete) {
				int x = GetEditNumber(data->hWndX);
				int y = GetEditNumber(data->hWndY);

				HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
				HWND hWndScreen = (HWND) SendMessage(nitroPaintStruct->hWndMdi, WM_MDIGETACTIVE, 0, 0);
				NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndScreen, 0);
				NSCR *nscr = &nscrViewerData->nscr;
				int tilesX = nscr->nWidth / 8;
				int tilesY = nscr->nHeight / 8;

				int newTilesX = tilesX / x;
				int newTilesY = tilesY / y;

				NSCR newNscr;
				nscrInit(&newNscr, nscr->header.format);
				newNscr.nWidth = newTilesX * 8;
				newNscr.nHeight = newTilesY * 8;
				newNscr.dataSize = newTilesX * newTilesY * sizeof(uint16_t);
				for (int i = 0; i < y; i++) {
					for (int j = 0; j < x; j++) {
						newNscr.data = (uint16_t *) calloc(newTilesX * newTilesY, sizeof(uint16_t));
						for (int tileY = 0; tileY < newTilesY; tileY++) {
							for (int tileX = 0; tileX < newTilesX; tileX++) {
								uint16_t src = nscr->data[tileX + j * newTilesX + (tileY + i * newTilesY) * tilesX];
								newNscr.data[tileX + tileY * newTilesX] = src;
							}
						}

						CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 50, nitroPaintStruct->hWndMdi, &newNscr);
					}
				}

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			setStyle(hWndMain, FALSE, WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
		case WM_DESTROY:
		{
			if (data != NULL)
				free(data);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterImageDialogClass() {
	RegisterGenericClass(L"ImageDialogClass", ImageDialogProc, sizeof(LPVOID));
}

void RegisterFormatConversionClass() {
	RegisterGenericClass(L"ConvertFormatDialogClass", ConvertFormatDialogProc, 3 * sizeof(LPVOID));
}

void RegisterSpriteSheetDialogClass() {
	RegisterGenericClass(L"SpriteSheetDialogClass", SpriteSheetDialogProc, sizeof(LPVOID));
}

void RegisterScreenDialogClass() {
	RegisterGenericClass(L"NewScreenDialogClass", NewScreenDialogProc, sizeof(LPVOID));
}

void RegisterScreenSplitDialogClass() {
	RegisterGenericClass(L"ScreenSplitDialogClass", ScreenSplitDialogProc, sizeof(LPVOID));
}

void RegisterTextPromptClass() {
	RegisterGenericClass(L"TextPromptClass", TextInputWndProc, sizeof(LPVOID) * 5); //2 HWNDs, status, out info
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
		result = result && WritePrivateProfileStringW(L"NitroPaint", L"AllowMultiple", L"0", lpszPath);
	}
	g_configuration.nclrViewerConfiguration.useDSColorPicker = GetPrivateProfileInt(L"NclrViewer", L"UseDSColorPicker", 0, lpszPath);
	g_configuration.ncgrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NcgrViewer", L"Gridlines", 1, lpszPath);
	g_configuration.nscrViewerConfiguration.gridlines = GetPrivateProfileInt(L"NscrViewer", L"Gridlines", 0, lpszPath);
	g_configuration.fullPaths = GetPrivateProfileInt(L"NitroPaint", L"FullPaths", 1, lpszPath);
	g_configuration.renderTransparent = GetPrivateProfileInt(L"NitroPaint", L"RenderTransparent", 1, lpszPath);
	g_configuration.backgroundPath = (LPWSTR) calloc(MAX_PATH, sizeof(WCHAR));
	g_configuration.dpiAware = GetPrivateProfileInt(L"NitroPaint", L"DPIAware", 1, lpszPath);
	g_configuration.allowMultipleInstances = GetPrivateProfileInt(L"NitroPaint", L"AllowMultiple", 0, lpszPath);
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

VOID CheckExistingAppWindow() {
	if (g_configuration.allowMultipleInstances) return;
	HWND hWndNP = FindWindow(L"NitroPaintClass", NULL);
	if (hWndNP == NULL) return;

	//forward to existing window
	ProcessCommandLine(hWndNP, TRUE);
	SetForegroundWindow(hWndNP);
	ExitProcess(0);
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
	RegisterScreenDialogClass();
	RegisterScreenSplitDialogClass();
	RegisterTextPromptClass();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	HACCEL hAccel = LoadAccelerators(hInstance, (LPCWSTR) IDR_ACCELERATOR1);
	CoInitialize(NULL);

	SetConfigPath();
	ReadConfiguration(g_configPath);
	CheckExistingAppWindow();
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