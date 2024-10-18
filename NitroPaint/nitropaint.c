#include <Windows.h>
#include <CommCtrl.h>
#include <Uxtheme.h>
#include <Shlwapi.h>
#include <ShlObj.h>

#include "nitropaint.h"
#include "filecommon.h"
#include "palette.h"
#include "resource.h"
#include "nsbtxviewer.h"
#include "ncgrviewer.h"
#include "nclrviewer.h"
#include "nscrviewer.h"
#include "ncerviewer.h"
#include "nanrviewer.h"
#include "exceptions.h"
#include "gdip.h"
#include "textureeditor.h"
#include "nsbtx.h"
#include "nmcrviewer.h"
#include "colorchooser.h"
#include "ui.h"
#include "texconv.h"
#include "bggen.h"
#include "editor.h"
#include "preview.h"

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


// ----- DPI scale code

static int sKnownDpi = 0;
static int sDontScaleGuiFont = 0;
static float sDpiScale = 1.0f;

float GetDpiScale(void) {

	//if not yet calculated, return
	if (!sKnownDpi) {

		if (!g_configuration.dpiAware) {
			sDpiScale = 1.0f; //no awareness
		} else {
			HDC hDC = GetDC(NULL);
			sDpiScale = ((float) GetDeviceCaps(hDC, LOGPIXELSX)) / (float) USER_DEFAULT_SCREEN_DPI;
			ReleaseDC(NULL, hDC);
		}
		sKnownDpi = 1;
	}

	return sDpiScale;
}

HFONT GetGUIFont(void) {
	static float cachedScale = 0.0f;
	static HFONT hFont = NULL;

	if (sDpiScale != cachedScale || hFont == NULL) {
		cachedScale = sDpiScale;

		HFONT hGuiFont = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
		if (hFont != NULL && hFont != hGuiFont) {
			DeleteObject(hFont);
		}

		if (sDpiScale != 1.0f && !sDontScaleGuiFont) {
			//scale the font by the DPI scaling factor.
			LOGFONT lfont;
			GetObject(hGuiFont, sizeof(lfont), &lfont);

			lfont.lfHeight = -(int) ((-lfont.lfHeight) * sDpiScale + 0.5f);
			hFont = CreateFontIndirect(&lfont);
		} else {
			//use stock object
			hFont = hGuiFont;
		}
	}

	return hFont;
}

void HandleNonClientDpiScale(HWND hWnd) {
	if (!g_configuration.dpiAware) return;

	//if DPI aware, configure nonclient DPI awareness for Windows 10+
	HMODULE hUser32 = GetModuleHandle(L"USER32.DLL");
	BOOL (WINAPI *EnableNonClientDpiScalingFunc) (HWND hWnd) = (BOOL (WINAPI *) (HWND))
		GetProcAddress(hUser32, "EnableNonClientDpiScaling");

	if (EnableNonClientDpiScalingFunc != NULL) EnableNonClientDpiScalingFunc(hWnd);
}

static BOOL CALLBACK DpiScaleProc(HWND hWnd, LPARAM lParam) {
	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	float scale = *(float *) lParam;

	//get bounds
	RECT rc;
	POINT topLeft = { 0 };
	GetWindowRect(hWnd, &rc);
	ClientToScreen(hWndParent, &topLeft);

	//scale by DPI scaling factor
	rc.left = (int) ((rc.left - topLeft.x) * scale + 0.5f);
	rc.right = (int) ((rc.right - topLeft.x) * scale + 0.5f);
	rc.top = (int) ((rc.top - topLeft.y) * scale + 0.5f);
	rc.bottom = (int) ((rc.bottom - topLeft.y) * scale + 0.5f);

	//move
	MoveWindow(hWnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, FALSE);
	EnumChildWindows(hWnd, DpiScaleProc, lParam);
	return TRUE;
}

void DpiScaleChildren(HWND hWnd, float dpiScale) {
	//enumerate child windows: scale
	EnumChildWindows(hWnd, DpiScaleProc, (LPARAM) &dpiScale);
}

LRESULT HandleWindowDpiChange(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	//update window in response to DPI change
	RECT *rcSug = (RECT *) lParam;
	int dpiX = LOWORD(wParam);
	sDpiScale = ((float) dpiX) / (float) USER_DEFAULT_SCREEN_DPI;
	sKnownDpi = 1;

	//check: DPI different from last? calculate scaling factor (this might be called
	//several times over the course of a DPI change: make sure all DPI scaling is
	//handled appropriately)
	static float lastScale = 1.0f;
	static float divLast = 1.0f;
	if (sDpiScale != lastScale) {
		divLast = sDpiScale / lastScale;
		lastScale = sDpiScale;
	}
	DpiScaleChildren(hWnd, divLast);
	SetGUIFont(hWnd);

	SetWindowPos(hWnd, NULL, rcSug->left, rcSug->top, rcSug->right - rcSug->left, rcSug->bottom - rcSug->top,
		SWP_NOZORDER | SWP_NOACTIVATE);
	return 0;
}

void DoHandleNonClientDpiScale(HWND hWnd) {
	if (!g_configuration.dpiAware) return;

	HMODULE hUser32 = GetModuleHandle(L"USER32.DLL");
	BOOL (WINAPI *pfnEnbleNonClientDpiScaling) (HWND hWnd) = (BOOL (WINAPI *) (HWND)) 
		GetProcAddress(hUser32, "EnableNonClientDpiScaling");
	if (pfnEnbleNonClientDpiScaling != NULL) {
		pfnEnbleNonClientDpiScaling(hWnd);
	}
}




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

int BatchTextureDialog(HWND hWndParent);
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
			if (notif == BN_CLICKED && hWndControl != NULL) {
				
				//if OK, set status to 1 and copy text.
				int *pStatus = (int *) GetWindowLong(hWnd, 0 * sizeof(void *));
				*pStatus = 0;
				if (hWndControl == hWndOK) {
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
	return DefModalProc(hWnd, msg, wParam, lParam);
}



// ----- clipboard functions

void copyBitmap(COLOR32 *px, int width, int height) {
	//assume clipboard is already owned and emptied

	//set DIBv5
	HGLOBAL hBmi = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(BITMAPV5HEADER) + width * height * sizeof(COLOR32));
	BITMAPV5HEADER *bmi = (BITMAPV5HEADER *) GlobalLock(hBmi);
	bmi->bV5Size = sizeof(*bmi);
	bmi->bV5Width = width;
	bmi->bV5Height = height;
	bmi->bV5Planes = 1;
	bmi->bV5BitCount = 32;
	bmi->bV5Compression = BI_BITFIELDS;
	bmi->bV5RedMask = 0x00FF0000;
	bmi->bV5GreenMask = 0x0000FF00;
	bmi->bV5BlueMask = 0x000000FF;
	bmi->bV5AlphaMask = 0xFF000000;
	bmi->bV5CSType = LCS_sRGB;
	bmi->bV5Intent = LCS_GM_ABS_COLORIMETRIC;

	COLOR32 *cDest = (COLOR32 *) (bmi + 1);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			COLOR32 c = px[x + y * width];
			c = REVERSE(c);
			cDest[x + (height - 1 - y) * width] = c;
		}
	}
	GlobalUnlock(hBmi);
	SetClipboardData(CF_DIBV5, hBmi);
}

static int EnsurePngClipboardFormat(void) {
	static int fmt = 0;
	if (fmt) return fmt;
	
	return fmt = RegisterClipboardFormat(L"PNG");
}

static int GetBitmapPaletteSize(BITMAPINFOHEADER *pbmi) {
	//get color palette size
	int paletteSize = 0;
	if (pbmi->biCompression == BI_BITFIELDS) {
		paletteSize = 3; //bitfields: 3 "palette colors"
	} else {
		paletteSize = pbmi->biClrUsed;
		if (pbmi->biBitCount <= 8 && paletteSize == 0) {
			//use max palette size
			paletteSize = 1 << pbmi->biBitCount;
		}
	}
	return paletteSize;
}

static size_t GetBitmapOffsetBits(BITMAPINFO *pbmi) {
	return sizeof(BITMAPINFOHEADER) + 4 * GetBitmapPaletteSize(&pbmi->bmiHeader);
}

static size_t GetBitmapSize(BITMAPINFO *pbmi) {
	size_t sizeBits = pbmi->bmiHeader.biSizeImage;
	int compression = pbmi->bmiHeader.biCompression;
	if (sizeBits == 0 && (compression == BI_RGB || compression == BI_BITFIELDS)) {
		int height = pbmi->bmiHeader.biHeight;
		if (height < 0) height = -height;

		int stride = (pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biBitCount + 7) / 8;
		stride = (stride + 3) & ~3;

		sizeBits = stride * height;
	}

	return GetBitmapOffsetBits(pbmi) + sizeBits;
}

COLOR32 *GetClipboardBitmap(int *pWidth, int *pHeight, unsigned char **indexed, COLOR32 **pplt, int *pPaletteSize) {
	//get handles to clipboard DIB and PNG objects.
	HGLOBAL hDib = GetClipboardData(CF_DIB);
	HGLOBAL hPng = GetClipboardData(EnsurePngClipboardFormat());

	//if neither format is available, return failure
	if (hDib == NULL && hPng == NULL) {
		*pWidth = 0;
		*pHeight = 0;
		*indexed = NULL;
		*pplt = NULL;
		*pPaletteSize = 0;
		return NULL;
	}

	//if PNG but no DIB, return PNG data
	if (hPng != NULL && hDib == NULL) {
		void *pngData = GlobalLock(hPng);
		SIZE_T size = GlobalSize(hPng);

		COLOR32 *px = ImgReadMemEx(pngData, size, pWidth, pHeight, indexed, pplt, pPaletteSize);

		GlobalUnlock(hPng);
		return px;
	}

	//if we've made it this far, we have DIB data on the clipboard. Read it now.
	BITMAPINFO *pbmi = (BITMAPINFO *) GlobalLock(hDib);
	SIZE_T dibSize = GetBitmapSize(pbmi);
	BITMAPFILEHEADER *bmfh = (BITMAPFILEHEADER *) malloc(sizeof(BITMAPFILEHEADER) + dibSize);
	memcpy(bmfh + 1, pbmi, dibSize);
	bmfh->bfType = 0x4D42; //'BM'
	bmfh->bfSize = sizeof(BITMAPFILEHEADER) + GetBitmapSize(pbmi);
	bmfh->bfReserved1 = 0;
	bmfh->bfReserved2 = 0;
	bmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + GetBitmapOffsetBits(pbmi);

	int dibWidth, dibHeight, dibPaletteSize = 0;
	COLOR32 *dibPalette = NULL;
	unsigned char *dibIndex = NULL;
	COLOR32 *pxDib = ImgReadMemEx((unsigned char *) bmfh, bmfh->bfSize, &dibWidth, &dibHeight, &dibIndex, &dibPalette, &dibPaletteSize);
	free(bmfh);
	GlobalUnlock(hDib);

	//if DIB but no PNG, return DIB data
	if (hDib != NULL && hPng == NULL) {
		//return DIB
		*pWidth = dibWidth;
		*pHeight = dibHeight;
		*pplt = dibPalette;
		*indexed = dibIndex;
		*pPaletteSize = dibPaletteSize;
		return pxDib;
	}

	//else, we have both data available. Read the PNG data and compare against the DIB data.
	void *png = GlobalLock(hPng);
	int pngWidth, pngHeight, pngPaletteSize = 0;
	COLOR32 *pngPalette = NULL;
	unsigned char *pngIndex = NULL;
	COLOR32 *pxPng = ImgReadMemEx(png, GlobalSize(hPng), &pngWidth, &pngHeight, &pngIndex, &pngPalette, &pngPaletteSize);
	GlobalUnlock(hPng);

	//now try to determine which to return. If the PNG has transparent/translucent pixels, return the PNG data.
	int usePng = 0;
	if (pxPng != NULL) {
		for (int i = 0; i < pngWidth * pngHeight; i++) {
			COLOR32 c = pxPng[i];
			int a = (c >> 24);
			if (a != 0xFF) {
				usePng = 1;
				break;
			}
		}
	}

	if (usePng) {
		if (pxDib != NULL) free(pxDib);
		if (dibIndex != NULL) free(dibIndex);
		if (dibPalette != NULL) free(dibPalette);

		*pWidth = pngWidth;
		*pHeight = pngHeight;
		*pplt = pngPalette;
		*indexed = pngIndex;
		*pPaletteSize = pngPaletteSize;
		return pxPng;
	} else {
		if (pxPng != NULL) free(pxPng);
		if (pngIndex != NULL) free(pngIndex);
		if (pngPalette != NULL) free(pngPalette);

		*pWidth = dibWidth;
		*pHeight = dibHeight;
		*pplt = dibPalette;
		*indexed = dibIndex;
		*pPaletteSize = dibPaletteSize;
		return pxDib;
	}
}




LPCWSTR GetFileName(LPCWSTR lpszPath) {
	const WCHAR *current = lpszPath;
	while (*lpszPath) {
		WCHAR c = *lpszPath;
		if (c == L'\\' || c == L'/') current = lpszPath;
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

const char *propNextLine(const char *ptr, const char *end) {
	while (ptr < end && *ptr != '\n' && *ptr != '\r') {
		//scan for newline
		ptr++;
	}

	//scan forward all whitespace
	while (ptr < end && (*ptr <= ' ' && *ptr > '\0')) ptr++;
	return ptr;
}

const char *propToValue(const char *ptr, const char *end) {
	//scan for :
	while (ptr < end && *ptr != ':') {
		ptr++;
	}

	//scan forward all whitespace
	while (ptr < end && (*ptr <= ' ' && *ptr > '\0')) ptr++;
	return ptr;
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
	char *buffer = (char *) ObjReadWholeFile(path, &dwSize);

	//test: Is this a specification file to open a file with?
	if (specIsSpec(buffer, dwSize)) {
		char *refName = propGetProperty(buffer, dwSize, "File");
		char *pltRef = propGetProperty(buffer, dwSize, "PLT");
		char *chrRef = propGetProperty(buffer, dwSize, "CHR");
		char *scrRef = propGetProperty(buffer, dwSize, "SCR");

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
		void *fp = ObjReadWholeFile(pathBuffer, &comboSize);

		//refName is the name of the file to read.
		COMBO2D *combo = (COMBO2D *) calloc(1, sizeof(COMBO2D));
		combo2dInit(combo, COMBO2D_TYPE_DATAFILE);
		combo->header.dispose = NULL;
		combo->header.compression = COMPRESSION_NONE;
		combo->extraData = (DATAFILECOMBO *) calloc(1, sizeof(DATAFILECOMBO));

		int pltOffset = 0, pltSize = 0, chrOffset = 0, chrSize = 0, scrOffset = 0, scrSize = 0;
		if (pltRef != NULL) parseOffsetSizePair(pltRef, &pltOffset, &pltSize);
		if (chrRef != NULL) parseOffsetSizePair(chrRef, &chrOffset, &chrSize);
		if (scrRef != NULL) parseOffsetSizePair(scrRef, &scrOffset, &scrSize);

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
			PalRead(&nclr, dfc->data + pltOffset, pltSize);
			nclr.header.format = NCLR_TYPE_COMBO;
		}
		if (chrRef != NULL) {
			ChrRead(&ncgr, dfc->data + chrOffset, chrSize);
			ncgr.header.format = NCGR_TYPE_COMBO;
		}
		if (scrRef != NULL) {
			ScrRead(&nscr, dfc->data + scrOffset, scrSize);
			nscr.header.format = NSCR_TYPE_COMBO;
		}

		//if there is already an NCLR open, close it.
		if (pltRef != NULL) {
			if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
			data->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, &nclr);

			NCLR *pNclr = (NCLR *) EditorGetObject(data->hWndNclrViewer);
			combo2dLink(combo, &pNclr->header);
			memcpy(((NCLRVIEWERDATA *) GetWindowLongPtr(data->hWndNclrViewer, 0))->szOpenFile, pathBuffer, 2 * (wcslen(pathBuffer) + 1));
		}

		//if there is already an NCGR open, close it.
		if (chrRef != NULL) {
			if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
			data->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, &ncgr);
			InvalidateRect(data->hWndNclrViewer, NULL, FALSE);


			NCGR *pNcgr = (NCGR *) EditorGetObject(data->hWndNcgrViewer);
			combo2dLink(combo, &pNcgr->header);
			memcpy(((NCGRVIEWERDATA *) GetWindowLongPtr(data->hWndNcgrViewer, 0))->szOpenFile, pathBuffer, 2 * (wcslen(pathBuffer) + 1));
		}

		//create NSCR editor and make it active
		if (scrRef != NULL) {
			HWND hWndNscrViewer = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, &nscr);

			NSCR *pNscr = (NSCR *) EditorGetObject(hWndNscrViewer);
			combo2dLink(combo, &pNscr->header);
			memcpy(((NSCRVIEWERDATA *) GetWindowLongPtr(hWndNscrViewer, 0))->szOpenFile, pathBuffer, 2 * (wcslen(pathBuffer) + 1));
		}
		free(pathBuffer);

		free(refName);
		if (pltRef != NULL) free(pltRef);
		if (chrRef != NULL) free(chrRef);
		if (scrRef != NULL) free(scrRef);
		goto cleanup;
	}

	int format = ObjIdentify(buffer, dwSize, path);
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
			//since we're kind of stepping around things a bit, we need to decompress here if applicable
			int decompressedSize = dwSize;
			int compressionType = CxGetCompressionType(buffer, dwSize);
			char *decompressed = CxDecompress(buffer, dwSize, &decompressedSize);
			int type = combo2dIsValid(decompressed, decompressedSize);

			//read combo
			COMBO2D *combo = (COMBO2D *) calloc(1, sizeof(COMBO2D));
			combo2dRead(combo, decompressed, decompressedSize);
			if (compressionType != COMPRESSION_NONE) combo->header.compression = compressionType;
			free(decompressed);

			//open the component objects
			for (int i = 0; i < combo->nLinks; i++) {
				OBJECT_HEADER *object = combo->links[i];
				int type = object->type;

				HWND h = NULL;
				OBJECT_HEADER *copy = combo->links[i];
				switch (type) {

					case FILE_TYPE_PALETTE:
						object->combo = (void *) combo;

						//if there is already an NCLR open, close it.
						if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
						h = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, (NCLR *) object);
						data->hWndNclrViewer = h;
						copy = &((EDITOR_DATA *) EditorGetData(h))->file;
						break;

					case FILE_TYPE_CHARACTER:
						object->combo = (void *) combo;

						//if there is already an NCGR open, close it.
						if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
						h = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, (NCGR *) object);
						data->hWndNcgrViewer = h;
						InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
						copy = &((EDITOR_DATA *) EditorGetData(h))->file;
						break;

					case FILE_TYPE_SCREEN:
						//create NSCR and make it active
						object->combo = (void *) combo;
						h = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, (NSCR *) object);
						copy = &((EDITOR_DATA *) EditorGetData(h))->file;
						break;
				}

				//if we created a copy, free the original and keep the copy
				if (copy != combo->links[i]) {
					free(combo->links[i]);
					combo->links[i] = copy;
				}

				//set compression type for all links
				combo->links[i]->compression = compressionType;

				//point the editor window at the right file
				EditorSetFile(h, path);
			}
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

NITROPAINTSTRUCT *NpGetData(HWND hWndMain) {
	return (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
}

static const int sZoomMenuCommands[] = {
	ID_ZOOM_100,
	ID_ZOOM_200,
	ID_ZOOM_400,
	ID_ZOOM_800,
	ID_ZOOM_1600
};

static const int sZoomLevels[] = {
	1, 2, 4, 8, 16
};

int MainGetZoomByCommand(int cmd) {
	for (int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		if (cmd == sZoomMenuCommands[i]) return sZoomLevels[i];
	}
	return 1;
}

int MainGetZoomCommand(int zoom) {
	for (int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		if (zoom == sZoomLevels[i]) return sZoomMenuCommands[i];
	}
	return -1;
}

int MainGetZoom(HWND hWnd) {
	HMENU hMenu = GetMenu(hWnd);
	for (int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		if (GetMenuState(hMenu, sZoomMenuCommands[i], MF_BYCOMMAND)) return sZoomLevels[i];
	}
	return 1;
}

void MainSetZoom(HWND hWnd, int zoom) {
	int cmd = -1;
	for (int i = 0; i < sizeof(sZoomLevels) / sizeof(sZoomLevels[0]); i++) {
		if (sZoomLevels[i] == zoom) {
			cmd = sZoomMenuCommands[i];
			break;
		}
	}
	if (cmd == -1) return;

	SendMessage(hWnd, WM_COMMAND, cmd, 0);
}

VOID MainZoomIn(HWND hWnd) {
	int zoom = MainGetZoom(hWnd);
	zoom *= 2;
	if (zoom > 16) zoom = 16;
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
	NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
	HWND hWndMdi = nitroPaintStruct->hWndMdi;
	EnumChildWindows(hWndMdi, InvalidateAllEditorsProc, type);
}

BOOL CALLBACK EnumAllEditorsProc(HWND hWnd, LPARAM lParam) {
	struct { BOOL (*pfn) (HWND, void *); void *param; int type; } *data = (void *) lParam;
	int type = GetEditorType(hWnd);
	if ((type == data->type && type != FILE_TYPE_INVALID) || (type != FILE_TYPE_INVALID && data->type == FILE_TYPE_INVALID)) {
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

static BOOL GetEditorFromObjectProc(HWND hWnd, void *param) {
	struct { OBJECT_HEADER *obj; HWND hWnd; } *data = param;
	EDITOR_DATA *ed = (EDITOR_DATA *) EditorGetData(hWnd);

	if (&ed->file == data->obj) {
		//found
		data->hWnd = hWnd;
		return FALSE;
	}
	return TRUE;
}

HWND GetEditorFromObject(HWND hWndMain, OBJECT_HEADER *obj) {
	struct { OBJECT_HEADER *obj; HWND hWnd; } data = { obj, NULL };
	EnumAllEditors(hWndMain, FILE_TYPE_INVALID, GetEditorFromObjectProc, &data);
	return data.hWnd;
}

static BOOL CALLBACK CountWindowsProc(HWND hWnd, LPARAM lParam) {
	(*(int *) lParam)++;
	return TRUE;
}

static BOOL CALLBACK ListWindowsProc(HWND hWnd, LPARAM lParam) {
	HWND **ppos = (HWND **) lParam;
	**ppos = hWnd;
	(*ppos)++;
	return TRUE;
}

static int SortWindowsComputeOrder(int type) {
	switch (type) {
		case FILE_TYPE_INVALID:
			return 0;
		case FILE_TYPE_PALETTE:
			return 1;
		case FILE_TYPE_CHAR:
			return 2;
		case FILE_TYPE_SCREEN:
			return 3;
		case FILE_TYPE_CELL:
			return 4;
		case FILE_TYPE_NANR:
			return 5;
		case FILE_TYPE_NMCR:
			return 6;
		case FILE_TYPE_NMAR:
			return 7;
		case FILE_TYPE_TEXTURE:
			return 8;
		case FILE_TYPE_NSBTX:
			return 9;
	}
	return 0;
}

static int SortWindowsProc(const void *p1, const void *p2) {
	HWND h1 = *(HWND *) p1;
	HWND h2 = *(HWND *) p2;
	int type1 = GetEditorType(h1);
	int type2 = GetEditorType(h2);
	
	if (type1 == type2) return 0;
	return SortWindowsComputeOrder(type1) - SortWindowsComputeOrder(type2);
}

void EnumChildWindowsInHierarchy(HWND hWnd, WNDENUMPROC lpEnumFunc, LPARAM lParam) {
	//count windows
	int nChildren = 0;
	EnumChildWindows(hWnd, CountWindowsProc, (LPARAM) &nChildren);

	//get window list
	HWND *list = (HWND *) calloc(nChildren, sizeof(HWND));
	HWND *pos = list;
	EnumChildWindows(hWnd, ListWindowsProc, (LPARAM) &pos);

	//sort by hierachical status (invalids first, then pal->chr->scr->cel->anm, tex->texarc)
	qsort(list, nChildren, sizeof(HWND), SortWindowsProc);

	//call proc for each
	for (int i = 0; i < nChildren; i++) {
		BOOL b = lpEnumFunc(list[i], lParam);
		if (!b) break;
	}

	//free
	free(list);
}

BOOL SetNscrEditorTransparentProc(HWND hWnd, void *param) {
	NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	int state = (int) param;
	nscrViewerData->transparent = state;
	return TRUE;
}

BOOL CALLBACK UpdatePreviewProc(HWND hWnd, LPARAM lParam) {
	SendMessage(hWnd, NV_UPDATEPREVIEW, 0, 0);
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
	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
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
		case WM_NCCREATE:
			//handle DPI awareness
			HandleNonClientDpiScale(hWnd);
			break;
		case 0x02E0://WM_DPICHANGED:
			//handle DPI update
			return HandleWindowDpiChange(hWnd, wParam, lParam);
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
													 FILTER_ALL  FILTER_PALETTE FILTER_CHARACTER FILTER_SCREEN FILTER_CELL
													 FILTER_ANIM FILTER_COMBO2D FILTER_TEXARC    FILTER_TEXTURE
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
						EnumChildWindowsInHierarchy(hWndMdi, SaveAllProc, (LPARAM) hWndMdi);
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
						HWND h = CreateWindow(L"CreateDialogClass", L"Create BG", WS_CAPTION | WS_BORDER | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						DoModal(h);
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
						DoModal(h);
						break;
					}
					case ID_NEW_NEWCELLBANK:
					{
						if (data->hWndNcerViewer != NULL) DestroyChild(data->hWndNcerViewer);
						data->hWndNcerViewer = NULL;

						NCER ncer;
						CellInit(&ncer, NCER_TYPE_NCER);
						ncer.mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
						ncer.nCells = 1;
						ncer.cells = (NCER_CELL *) calloc(1, sizeof(NCER_CELL));
						ncer.cells[0].attr = (WORD *) calloc(3, 2);
						ncer.cells[0].nAttribs = 1;
						ncer.cells[0].cellAttr = 0;

						//if a character editor is open, use its mapping mode
						HWND hWndCharacterEditor;
						int nCharEditors = GetAllEditors(hWnd, FILE_TYPE_CHARACTER, &hWndCharacterEditor, 1);
						if (nCharEditors) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndCharacterEditor);
							ncer.mappingMode = ncgrViewerData->ncgr.mappingMode;
						}

						data->hWndNcerViewer = CreateNcerViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 50, data->hWndMdi, &ncer);
						break;
					}
					case ID_NEW_NEWPALETTE:
					{
						if (data->hWndNclrViewer != NULL) DestroyChild(data->hWndNclrViewer);
						data->hWndNclrViewer = NULL;

						NCLR nclr;
						PalInit(&nclr, NCLR_TYPE_NCLR);
						nclr.nColors = 256;
						nclr.nBits = 4;
						nclr.nPalettes = 16;
						nclr.colors = (COLOR *) calloc(256, sizeof(COLOR));
						data->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, &nclr);
						break;
					}
					case ID_NEW_NEWSCREEN:
					{
						HWND h = CreateWindow(L"NewScreenDialogClass", L"New Screen", WS_CAPTION | WS_BORDER | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						DoModal(h);
						break;
					}
					case ID_NEW_NEWANIMATION:
					{
						NANR nanr = { 0 };
						ObjInit(&nanr.header, FILE_TYPE_NANR, NANR_TYPE_NANR);

						nanr.nSequences = 1;
						nanr.sequences = (NANR_SEQUENCE *) calloc(1, sizeof(NANR_SEQUENCE));
						nanr.sequences[0].nFrames = 1;
						nanr.sequences[0].mode = 1;
						nanr.sequences[0].type = 0 | (1 << 16);
						nanr.sequences[0].startFrameIndex = 0;
						nanr.sequences[0].frames = (FRAME_DATA *) calloc(1, sizeof(FRAME_DATA));
						nanr.sequences[0].frames[0].nFrames = 1;
						nanr.sequences[0].frames[0].pad_ = 0xBEEF;
						nanr.sequences[0].frames[0].animationData = calloc(1, sizeof(ANIM_DATA));
						memset(nanr.sequences[0].frames[0].animationData, 0, sizeof(ANIM_DATA));

						HWND h = CreateNanrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, &nanr);
						ShowWindow(h, SW_SHOW);
						break;
					}
					case ID_NEW_NEWTEXTUREARCHIVE:
					{
						TexArc nsbtx;
						TexarcInit(&nsbtx, NSBTX_TYPE_NNS);
						
						//no need to init further
						CreateNsbtxViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 450, 350, data->hWndMdi, &nsbtx);
						break;
					}
					case ID_FILE_CONVERTTO:
					{
						HWND hWndFocused = (HWND) SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, 0);
						if (hWndFocused == NULL) break;
						
						int editorType = GetEditorType(hWndFocused);
						if (editorType == FILE_TYPE_INVALID) break;

						EDITOR_DATA *editorData = (EDITOR_DATA *) EditorGetData(hWndFocused);
						if (editorData == NULL) break;

						LPCWSTR *formats = ObjGetFormatNamesByType(editorData->file.type);
						if (formats == NULL || formats[0] == NULL)  break;

						HWND h = CreateWindow(L"ConvertFormatDialogClass", L"Convert Format", WS_CAPTION | WS_BORDER | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						SendMessage(h, NV_SETDATA, 0, (LPARAM) editorData);
						DoModal(h);
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
						InvalidateAllEditors(hWnd, FILE_TYPE_CELL);
						break;
					}
					case ID_FILE_PREVIEWTARGET:
					{
						int inited = GetMenuState(GetMenu(hWnd), ID_FILE_PREVIEWTARGET, MF_BYCOMMAND);
						if (!inited) {
							//try init
							int status = PreviewInit();
							if (status) {
								//success
								CheckMenuItem(GetMenu(hWnd), ID_FILE_PREVIEWTARGET, MF_CHECKED);

								//update preview of all editors
								EnumChildWindows(data->hWndMdi, UpdatePreviewProc, 0);
							} else {
								//failure
								MessageBox(hWnd, L"Could not connect.", L"Could not connect", MB_ICONERROR);
							}
						} else {
							//deinit
							PreviewEnd();
							CheckMenuItem(GetMenu(hWnd), ID_FILE_PREVIEWTARGET, MF_UNCHECKED);
						}
						break;
					}
					case ID_NTFT_NTFT40084:
					{
						HWND h = CreateWindow(L"NtftConvertDialogClass", L"NTFT To Texture", WS_CAPTION | WS_BORDER | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						DoModal(h);
						break;
					}
					case ID_TOOLS_ALPHABLEND:
					{
						HWND h = CreateWindow(L"AlphaBlendClass", L"Alpha Blend", WS_CAPTION | WS_BORDER | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						DoModal(h);
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
						DoModal(h);
						break;
					}
					case ID_BATCHPROCESSING_TEXTURECONVERSION:
					{
						BatchTextureDialog(hWnd);
						break;
					}
					case ID_TOOLS_TEXTUREVRAMSUMMARY:
					{
						//select directory
						WCHAR path[MAX_PATH];

						BROWSEINFO bf;
						bf.hwndOwner = getMainWindow(hWnd);
						bf.pidlRoot = NULL;
						bf.pszDisplayName = path;
						bf.lpszTitle = L"Select texture folder...";
						bf.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_VALIDATE; //I don't much like the new dialog style
						bf.lpfn = NULL;
						bf.lParam = 0;
						bf.iImage = 0;
						PIDLIST_ABSOLUTE idl = SHBrowseForFolder(&bf);

						if (idl == NULL) {
							break;
						}
						SHGetPathFromIDList(idl, path);
						CoTaskMemFree(idl);

						BatchTexShowVramStatistics(hWnd, path);
						break;
					}
					case ID_TOOLS_EDITLINK:
					{
						//must have an active window open.
						HWND hWndActive = (HWND) SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, (LPARAM) NULL);
						if (hWndActive == NULL || EditorGetObject(hWndActive) == NULL) {
							MessageBox(hWnd, L"No editor focused.", L"No editor focused", MB_ICONERROR);
						} else {
							//do modal dialog
							HWND hWndDlg = CreateWindow(L"LinkEditClass", L"Edit Links", WS_CAPTION | WS_BORDER | WS_SYSMENU, CW_USEDEFAULT,
								CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWnd, NULL, NULL, NULL);
							SendMessage(hWndDlg, NV_INITIALIZE, 0, (LPARAM) hWndActive);
							DoModal(hWndDlg);
						}
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
	HWND hWndAffine;
} CREATEDIALOGDATA;

BOOL WINAPI SetGUIFontProc(HWND hWnd, LPARAM lParam) {
	HFONT hFont = (HFONT) lParam;
	SendMessage(hWnd, WM_SETFONT, (WPARAM) hFont, TRUE);
	return TRUE;
}

VOID SetGUIFont(HWND hWnd) {
	HFONT hFont = GetGUIFont();
	EnumChildWindows(hWnd, SetGUIFontProc, (LPARAM) hFont);
}

void RegisterGenericClass(LPCWSTR lpszClassName, WNDPROC pWndProc, int cbWndExtra) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = g_useDarkTheme ? CreateSolidBrush(RGB(32, 32, 32)) : (HBRUSH) (COLOR_BTNFACE + 1);
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
	COLOR32 *bbits;

	NCLR nclr;
	NCGR ncgr;
	NSCR nscr;

	//copy of parameters for creation callback
	BgGenerateParameters genParams;
} CREATENSCRDATA;

void nscrCreateCallback(void *data) {
	CREATENSCRDATA *createData = (CREATENSCRDATA *) data;
	HWND hWndMain = createData->hWndMain;
	NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
	HWND hWndMdi = nitroPaintStruct->hWndMdi;

	if (nitroPaintStruct->hWndNcgrViewer) DestroyChild(nitroPaintStruct->hWndNcgrViewer);
	if (nitroPaintStruct->hWndNclrViewer) DestroyChild(nitroPaintStruct->hWndNclrViewer);
	nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, &createData->nclr);
	nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, &createData->ncgr);
	HWND hWndNscrViewer = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, &createData->nscr);

	OBJECT_HEADER *palobj = EditorGetObject(nitroPaintStruct->hWndNclrViewer);
	OBJECT_HEADER *chrobj = EditorGetObject(nitroPaintStruct->hWndNcgrViewer);
	OBJECT_HEADER *scrobj = EditorGetObject(hWndNscrViewer);

	//link data
	ObjLinkObjects(palobj, chrobj);
	ObjLinkObjects(chrobj, scrobj);

	//if a character base was used, the BG screen viewer might guess the character base incorrectly. 
	//in these cases, we need to set the correct character base here.
	if (createData->genParams.characterSetting.base > 0) {
		NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(hWndNscrViewer);
		nscrViewerData->tileBase = createData->genParams.characterSetting.base;
		SetEditNumber(nscrViewerData->hWndTileBase, nscrViewerData->tileBase);
	}


	free(createData->bbits);
	free(data);
}

typedef struct {
	PROGRESSDATA *data;
	CREATENSCRDATA *createData;
	COLOR32 *bbits;
	int width;
	int height;
	BgGenerateParameters params;
} THREADEDNSCRCREATEPARAMS;

DWORD WINAPI threadedNscrCreateInternal(LPVOID lpParameter) {
	THREADEDNSCRCREATEPARAMS *params = lpParameter;
	BgGenerate(&params->createData->nclr, &params->createData->ncgr, &params->createData->nscr, 
			   params->bbits, params->width, params->height, &params->params,
			   &params->data->progress1, &params->data->progress1Max, &params->data->progress2, &params->data->progress2Max);
	params->data->waitOn = 1;
	return 0;
}

void threadedNscrCreate(PROGRESSDATA *data, CREATENSCRDATA *createData, COLOR32 *bbits, int width, int height,
						BgGenerateParameters *generateParams) {
	THREADEDNSCRCREATEPARAMS *params = calloc(1, sizeof(*params));
	params->data = data;
	params->bbits = bbits;
	params->width = width;
	params->height = height;
	params->createData = createData;
	memcpy(&params->params, generateParams, sizeof(BgGenerateParameters));
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
			data->hWndAffine = CreateCheckbox(hWnd, L"Affine Mode", rightX, topY + 27 * 5, 100, 22, TRUE);
			setStyle(data->hWndDiffuse, TRUE, WS_DISABLED);

			LPCWSTR formatNames[] = { L"NITRO-System", L"NITRO-CHARACTER", L"IRIS-CHARACTER", L"AGB-CHARACTER", L"Hudson", L"Hudson 2", L"Raw", L"Raw Compressed" };
			CreateStatic(hWnd, L"Format:", rightX, middleY, 50, 22);
			data->hWndFormatDropdown = CreateCombobox(hWnd, formatNames, sizeof(formatNames) / sizeof(*formatNames), rightX + 55, middleY, 150, 22, 0);

			CreateStatic(hWnd, L"Balance:", leftX, bottomY, 100, 22);
			CreateStatic(hWnd, L"Color Balance:", leftX, bottomY + 27, 100, 22);

			CreateStaticAligned(hWnd, L"Lightness", leftX + 110, bottomY, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Color", leftX + 110 + 50 + 200, bottomY, 50, 22, SCA_LEFT);
			CreateStaticAligned(hWnd, L"Green", leftX + 110, bottomY + 27, 50, 22, SCA_RIGHT);
			CreateStaticAligned(hWnd, L"Red", leftX + 110 + 50 + 200, bottomY + 27, 50, 22, SCA_LEFT);
			data->hWndBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			data->hWndColorBalance = CreateTrackbar(hWnd, leftX + 110 + 50, bottomY + 27, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
			data->hWndEnhanceColors = CreateCheckbox(hWnd, L"Enhance Colors", leftX, bottomY + 27 * 2, 200, 22, FALSE);

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
					int doAlign = GetCheckboxChecked(data->hWndAlignmentCheckbox);
					const int bitsOptions[] = { 4, 8 };
					SendMessage(data->nscrCreateInput, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) location);

					if (*location == L'\0') {
						MessageBox(hWnd, L"No image input specified.", L"No Input", MB_ICONERROR);
						break;
					}

					int width, height;
					COLOR32 *bbits = ImgRead(location, &width, &height);

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
					HWND hWndMdi = nitroPaintStruct->hWndMdi;

					HWND hWndProgress = CreateWindow(L"ProgressWindowClass", L"In Progress...", 
						WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME), 
						CW_USEDEFAULT, CW_USEDEFAULT, 300, 100, hWndMain, NULL, NULL, NULL);
					CREATENSCRDATA *createData = (CREATENSCRDATA *) calloc(1, sizeof(CREATENSCRDATA));
					createData->hWndMain = hWndMain;
					createData->bbits = bbits;
					PROGRESSDATA *progressData = (PROGRESSDATA *) calloc(1, sizeof(PROGRESSDATA));
					progressData->data = createData;
					progressData->callback = nscrCreateCallback;
					SendMessage(hWndProgress, NV_SETDATA, 0, (LPARAM) progressData);

					//global setting
					BgGenerateParameters params;
					params.fmt = SendMessage(data->hWndFormatDropdown, CB_GETCURSEL, 0, 0);
					params.balance.balance = GetTrackbarPosition(data->hWndBalance);
					params.balance.colorBalance = GetTrackbarPosition(data->hWndColorBalance);
					params.balance.enhanceColors = GetCheckboxChecked(data->hWndEnhanceColors);

					//dither setting
					params.dither.dither = GetCheckboxChecked(data->nscrCreateDither);
					params.dither.diffuse = ((float) GetEditNumber(data->hWndDiffuse)) * 0.01f;

					//palette region
					params.compressPalette = GetCheckboxChecked(data->hWndRowLimit);
					params.color0Mode = SendMessage(data->hWndColor0Setting, CB_GETCURSEL, 0, 0);
					params.paletteRegion.base = GetEditNumber(data->hWndPaletteInput);
					params.paletteRegion.count = GetEditNumber(data->hWndPalettesInput);
					params.paletteRegion.offset = GetEditNumber(data->hWndPaletteOffset);
					params.paletteRegion.length = GetEditNumber(data->hWndPaletteSize);

					//character setting
					params.nBits = bitsOptions[SendMessage(data->nscrCreateDropdown, CB_GETCURSEL, 0, 0)];
					params.characterSetting.base = GetEditNumber(data->hWndTileBase);
					params.characterSetting.alignment = doAlign ? GetEditNumber(data->hWndAlignment) : 1;
					params.characterSetting.compress = GetCheckboxChecked(data->hWndMergeTiles);
					params.characterSetting.nMax = GetEditNumber(data->hWndMaxChars);
					params.affine = GetCheckboxChecked(data->hWndAffine);

					memcpy(&createData->genParams, &params, sizeof(params));
					threadedNscrCreate(progressData, createData, bbits, width, height, &params);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					DoModalEx(hWndProgress, FALSE);
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
				} else if (hWndControl == data->hWndAffine) {
					//limit palette size if applicable
					int state = GetCheckboxChecked(hWndControl);
					if (!state) {
						SetEditNumber(data->hWndPalettesInput, 1);
						SetEditNumber(data->hWndPaletteInput, 0);
					}
				}
			} else if (HIWORD(wParam) == CBN_SELCHANGE) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->nscrCreateDropdown) {
					int index = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);
					LPCWSTR sizes[] = { L"16", L"256" };
					SendMessage(data->hWndPaletteSize, WM_SETTEXT, wcslen(sizes[index]), (LPARAM) sizes[index]);

					//if setting to 4-bit depth, then affine mode can't be selected.
					int disableAffine = (index == 0);
					setStyle(data->hWndAffine, disableAffine, WS_DISABLED);
					InvalidateRect(data->hWndAffine, NULL, FALSE);

					//uncheck affine mode if 4 bit selected
					if (index == 0) SendMessage(data->hWndAffine, BM_SETCHECK, BST_UNCHECKED, 0);
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
	return DefModalProc(hWnd, msg, wParam, lParam);
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
	return DefModalProc(hWnd, msg, wParam, lParam);
}

void RegisterProgressWindowClass() {
	RegisterGenericClass(L"ProgressWindowClass", ProgressWindowWndProc, sizeof(LPVOID));
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
				const char *str = TxNameFromTexFormat(i);
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
				texture.texels.height = height;
				memcpy(&texture.palette.name, palName, 16);

				//texture editor takes ownership of texture data, no need to free
				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
				HWND hWndMdi = nitroPaintStruct->hWndMdi;
				CreateTextureEditorImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndMdi, &texture);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_DESTROY:
		{
			free(data);
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
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
			EDITOR_DATA *editorData = (EDITOR_DATA *) lParam;
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) editorData);

			CreateStatic(hWnd, L"Format:", 10, 10, 100, 22);
			CreateStatic(hWnd, L"Compression:", 10, 37, 100, 22);
			HWND hWndFormatCombobox = CreateWindow(WC_COMBOBOXW, L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 120, 10, 100, 100, hWnd, NULL, NULL, NULL);
			HWND hWndCompressionCombobox = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 120, 37, 100, 100, hWnd, NULL, NULL, NULL);
			CreateButton(hWnd, L"Set", 120, 64, 100, 22, TRUE);

			LPCWSTR *formats = ObjGetFormatNamesByType(editorData->file.type);
			formats++; //skip invalid
			while (*formats != NULL) {
				SendMessage(hWndFormatCombobox, CB_ADDSTRING, wcslen(*formats), (LPARAM) *formats);
				formats++;
			}
			SendMessage(hWndFormatCombobox, CB_SETCURSEL, editorData->file.format - 1, 0);
			LPCWSTR *compressions = g_ObjCompressionNames;
			while (*compressions != NULL) {
				SendMessage(hWndCompressionCombobox, CB_ADDSTRING, wcslen(*compressions), (LPARAM) *compressions);
				compressions++;
			}
			SendMessage(hWndCompressionCombobox, CB_SETCURSEL, editorData->file.compression, 0);

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
				EDITOR_DATA *editorData = (EDITOR_DATA *) GetWindowLongPtr(hWnd, 0);
				editorData->file.format = fmt;
				editorData->file.compression = comp;

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

VOID CreateImageDialog(HWND hWnd, LPCWSTR path) {
	HWND h = CreateWindow(L"ImageDialogClass", L"Image Conversion", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX), CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, hWnd, NULL, NULL, NULL);
	SendMessage(h, NV_SETDATA, 0, (LPARAM) path);
	DoModal(h);
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
			data->hWndBg = CreateButton(hWnd, L"Create BG", 10, 42, 200, 22, FALSE);
			data->hWndTexture = CreateButton(hWnd, L"Create Texture", 10, 74, 200, 22, FALSE);
			SetWindowSize(hWnd, 220, 106);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
			HWND hWndMdi = nitroPaintStruct->hWndMdi;

			if (hWndControl != NULL) {
				if (hWndControl == data->hWndBg) {
					HWND h = CreateWindow(L"CreateDialogClass", L"Create BG", WS_CAPTION | WS_BORDER | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);

					//pre-populate path
					CREATEDIALOGDATA *cdData = (CREATEDIALOGDATA *) GetWindowLongPtr(h, 0);
					SendMessage(cdData->nscrCreateInput, WM_SETTEXT, wcslen(data->szPath), (LPARAM) data->szPath);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					DoModal(h);
				} else if (hWndControl == data->hWndTexture) {
					CreateTextureEditor(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndMdi, data->szPath);
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
	return DefModalProc(hWnd, msg, wParam, lParam);
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
				L"NITRO-System", L"NITRO-CHARACTER", L"IRIS-CHARACTER", L"AGB-CHARACTER", L"Hudson", L"Hudson 2", L"Raw", L"Raw Compressed"
			};

			CreateStatic(hWnd, L"8 bit:", 10, 10, 50, 22);
			data->hWndBitDepth = CreateCheckbox(hWnd, L"", 70, 10, 22, 22, FALSE);
			CreateStatic(hWnd, L"Mapping:", 10, 42, 50, 22);
			data->hWndMapping = CreateCombobox(hWnd, mappings, sizeof(mappings) / sizeof(*mappings), 70, 42, 200, 100, 1);
			CreateStatic(hWnd, L"Format:", 10, 74, 50, 22);
			data->hWndFormat = CreateCombobox(hWnd, formats, sizeof(formats) / sizeof(*formats), 70, 74, 150, 100, 0);
			data->hWndCreate = CreateButton(hWnd, L"Create", 70, 106, 150, 22, TRUE);
			SetWindowSize(hWnd, 280, 138);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
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

					int charFormats[] = { NCGR_TYPE_NCGR, NCGR_TYPE_NC, NCGR_TYPE_IC, NCGR_TYPE_AC, NCGR_TYPE_HUDSON, NCGR_TYPE_HUDSON2, NCGR_TYPE_BIN, NCGR_TYPE_BIN };
					int palFormats[] = { NCLR_TYPE_NCLR, NCLR_TYPE_NC, NCLR_TYPE_BIN, NCLR_TYPE_BIN, NCLR_TYPE_HUDSON, NCLR_TYPE_HUDSON, NCLR_TYPE_BIN, NCLR_TYPE_BIN };
					int compression = format == 7 ? COMPRESSION_LZ77 : COMPRESSION_NONE;
					int charFormat = charFormats[format];
					int palFormat = palFormats[format];

					NCLR nclr;
					PalInit(&nclr, palFormat);
					nclr.colors = (COLOR *) calloc(256, sizeof(COLOR));
					nclr.nColors = 256;
					nclr.nPalettes = (nBits == 8) ? 1 : 16;
					nclr.totalSize = nclr.nColors * 2;
					nclr.nBits = nBits;
					
					NCGR ncgr;
					ChrInit(&ncgr, charFormat);
					ncgr.header.compression = compression;
					ncgr.nBits = nBits;
					ncgr.mappingMode = mapping;
					ncgr.tilesX = 32;
					ncgr.tilesY = height;
					ncgr.nTiles = ncgr.tilesX * ncgr.tilesY;
					ncgr.tiles = (unsigned char **) calloc(ncgr.nTiles, sizeof(unsigned char *));
					ncgr.attr = (unsigned char *) calloc(ncgr.nTiles, 1);
					for (int i = 0; i < ncgr.nTiles; i++) {
						ncgr.tiles[i] = (BYTE *) calloc(64, 1);
					}

					if (nitroPaintStruct->hWndNclrViewer != NULL) DestroyChild(nitroPaintStruct->hWndNclrViewer);
					if (nitroPaintStruct->hWndNcgrViewer != NULL) DestroyChild(nitroPaintStruct->hWndNcgrViewer);
					nitroPaintStruct->hWndNclrViewer = NULL;
					nitroPaintStruct->hWndNcgrViewer = NULL;
					nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, &nclr);
					nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, hWndMdi, &ncgr);

					//link (objects get shallow copied by the editor)
					OBJECT_HEADER *palobj = EditorGetObject(nitroPaintStruct->hWndNclrViewer);
					OBJECT_HEADER *chrobj = EditorGetObject(nitroPaintStruct->hWndNcgrViewer);
					ObjLinkObjects(palobj, chrobj);

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
	return DefModalProc(hWnd, msg, wParam, lParam);
}

typedef struct CREATESCREENDATA_ {
	HWND hWndWidth;
	HWND hWndHeight;
	HWND hWndCreate;
	HWND hWndFormat;
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
			HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);

			CreateStatic(hWnd, L"Width (dots):", 10, 10, 75, 22);
			CreateStatic(hWnd, L"Height (dots):", 10, 37, 75, 22);
			CreateStatic(hWnd, L"Format:", 10, 64, 75, 22);

			LPCWSTR formats[] = {
				L"Text 16x16",
				L"Text 256x1",
				L"Affine 256x1",
				L"Affine Extended 256x16"
			};
			int defFmt = 0;

			//determine which default format to use. If we have an open BG screen, copy its settings.
			HWND hWndNscrViewer = NULL;
			if (GetAllEditors(hWndMain, FILE_TYPE_SCREEN, &hWndNscrViewer, 1)) {
				//has open screen editor, duplicate its setting
				NSCR *nscr = (NSCR *) EditorGetData(hWndNscrViewer);

				if (nscr->fmt == SCREENFORMAT_AFFINE) defFmt = 2;
				else if (nscr->colorMode == SCREENCOLORMODE_16x16) defFmt = 0;
				else if (nscr->colorMode == SCREENCOLORMODE_256x1) defFmt = 1;
				else if (nscr->colorMode == SCREENCOLORMODE_256x16) defFmt = 3;
			} else {
				//no open screen editor, search for open character editor
				HWND hWndNcgrViewer = NULL;
				if (GetAllEditors(hWndMain, FILE_TYPE_CHARACTER, &hWndNcgrViewer, 1)) {
					NCGR *ncgr = (NCGR *) EditorGetObject(hWndNcgrViewer);

					if (ncgr->nBits == 4) defFmt = 0;
					else if (ncgr->nBits == 8 && ncgr->nTiles <= 256) defFmt = 2;
					else if (ncgr->nBits == 8) defFmt = 3;
				}
			}
			
			data->hWndWidth = CreateEdit(hWnd, L"256", 85, 10, 150, 22, TRUE);
			data->hWndHeight = CreateEdit(hWnd, L"256", 85, 37, 150, 22, TRUE);
			data->hWndFormat = CreateCombobox(hWnd, formats, sizeof(formats) / sizeof(formats[0]), 85, 64, 150, 100, defFmt);
			data->hWndCreate = CreateButton(hWnd, L"Create", 85, 91, 150, 22, TRUE);
			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 245, 123);
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL && hWndControl == data->hWndCreate) {
				HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);

				int width = GetEditNumber(data->hWndWidth);
				int tilesX = width / 8;
				int height = GetEditNumber(data->hWndHeight);
				int tilesY = height / 8;
				int fmtSel = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0);

				//translate format selection to format and color mode
				int colorMode = SCREENCOLORMODE_16x16, format = SCREENFORMAT_TEXT;
				switch (fmtSel) {
					case 0:
						colorMode = SCREENCOLORMODE_16x16;
						format = SCREENFORMAT_TEXT;
						break;
					case 1:
						colorMode = SCREENCOLORMODE_256x1;
						format = SCREENFORMAT_TEXT;
						break;
					case 2:
						colorMode = SCREENCOLORMODE_256x1;
						format = SCREENFORMAT_AFFINE;
						break;
					case 3:
						colorMode = SCREENCOLORMODE_256x16;
						format = SCREENFORMAT_AFFINEEXT;
						break;
				}

				NSCR nscr;
				ScrInit(&nscr, NSCR_TYPE_NSCR);
				nscr.fmt = format;
				nscr.colorMode = colorMode;
				nscr.tilesX = tilesX;
				nscr.tilesY = tilesY;
				nscr.dataSize = tilesX * tilesY * sizeof(uint16_t);
				nscr.data = (uint16_t *) calloc(tilesX * tilesY, sizeof(uint16_t));
				CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, nitroPaintStruct->hWndMdi, &nscr);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
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
	return DefModalProc(hWnd, msg, wParam, lParam);
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
				NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
				HWND hWndScreen = (HWND) SendMessage(nitroPaintStruct->hWndMdi, WM_MDIGETACTIVE, 0, 0);
				NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) GetWindowLongPtr(hWndScreen, 0);
				NSCR *nscr = &nscrViewerData->nscr;
				int tilesX = nscr->tilesX;
				int tilesY = nscr->tilesY;

				int newTilesX = tilesX / x;
				int newTilesY = tilesY / y;

				NSCR newNscr;
				ScrInit(&newNscr, nscr->header.format);
				newNscr.tilesX = newTilesX;
				newNscr.tilesY = newTilesY;
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
		case WM_DESTROY:
		{
			if (data != NULL)
				free(data);
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK AlphaBlendWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndForegroundPath, hWndForegroundBrowse, hWndBackgroundPath, hWndBackgroundBrowse;
	HWND hWndTiledCheckbox, hWndSave;
	hWndForegroundPath = (HWND) GetWindowLongPtr(hWnd, 0 * sizeof(LPVOID));
	hWndForegroundBrowse = (HWND) GetWindowLongPtr(hWnd, 1 * sizeof(LPVOID));
	hWndBackgroundPath = (HWND) GetWindowLongPtr(hWnd, 2 * sizeof(LPVOID));
	hWndBackgroundBrowse = (HWND) GetWindowLongPtr(hWnd, 3 * sizeof(LPVOID));
	hWndTiledCheckbox = (HWND) GetWindowLongPtr(hWnd, 4 * sizeof(LPVOID));
	hWndSave = (HWND) GetWindowLongPtr(hWnd, 5 * sizeof(LPVOID));

	switch (msg) {
		case WM_CREATE:
		{
			CreateStatic(hWnd, L"Background:", 10, 10, 70, 22);
			CreateStatic(hWnd, L"Foreground:", 10, 37, 70, 22);
			hWndForegroundPath = CreateEdit(hWnd, L"", 90, 37, 200, 22, FALSE);
			hWndForegroundBrowse = CreateButton(hWnd, L"...", 290, 37, 30, 22, FALSE);
			hWndBackgroundPath = CreateEdit(hWnd, L"", 90, 10, 200, 22, FALSE);
			hWndBackgroundBrowse = CreateButton(hWnd, L"...", 290, 10, 30, 22, FALSE);
			hWndTiledCheckbox = CreateCheckbox(hWnd, L"Tiled", 10, 64, 100, 22, TRUE);
			hWndSave = CreateButton(hWnd, L"Save", 245, 96, 75, 22, TRUE);

			SetWindowLongPtr(hWnd, 0 * sizeof(LPVOID), (LONG_PTR) hWndForegroundPath);
			SetWindowLongPtr(hWnd, 1 * sizeof(LPVOID), (LONG_PTR) hWndForegroundBrowse);
			SetWindowLongPtr(hWnd, 2 * sizeof(LPVOID), (LONG_PTR) hWndBackgroundPath);
			SetWindowLongPtr(hWnd, 3 * sizeof(LPVOID), (LONG_PTR) hWndBackgroundBrowse);
			SetWindowLongPtr(hWnd, 4 * sizeof(LPVOID), (LONG_PTR) hWndTiledCheckbox);
			SetWindowLongPtr(hWnd, 5 * sizeof(LPVOID), (LONG_PTR) hWndSave);

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 330, 128);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
			if (hWndControl == hWndForegroundBrowse && notif == BN_CLICKED) {
				//set foreground path
				LPWSTR filter = L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0";
				LPWSTR path = openFileDialog(hWnd, L"Open Image", filter, L"");
				if (path == NULL) break;

				SendMessage(hWndForegroundPath, WM_SETTEXT, wcslen(path), (LPARAM) path);
				free(path);
			} else if (hWndControl == hWndBackgroundBrowse && notif == BN_CLICKED) {
				//set background path
				LPWSTR filter = L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0";
				LPWSTR path = openFileDialog(hWnd, L"Open Image", filter, L"");
				if (path == NULL) break;

				SendMessage(hWndBackgroundPath, WM_SETTEXT, wcslen(path), (LPARAM) path);
				free(path);
			} else if (hWndControl == hWndSave && notif == BN_CLICKED) {
				//get paths
				WCHAR forepath[MAX_PATH], backpath[MAX_PATH];
				int isTiled = GetCheckboxChecked(hWndTiledCheckbox);
				SendMessage(hWndForegroundPath, WM_GETTEXT, MAX_PATH, (LPARAM) forepath);
				SendMessage(hWndBackgroundPath, WM_GETTEXT, MAX_PATH, (LPARAM) backpath);

				//read images
				int foreWidth, foreHeight, backWidth, backHeight, blendWidth, blendHeight;
				COLOR32 *foreground = ImgRead(forepath, &foreWidth, &foreHeight);
				COLOR32 *background = ImgRead(backpath, &backWidth, &backHeight);
				if (foreground == NULL || background == NULL) {
					MessageBox(hWnd, L"File not found.", L"Not found", MB_ICONERROR);
					if (foreground != NULL) free(foreground);
					if (background != NULL) free(background);
					break;
				}
				COLOR32 *blend = ImgComposite(background, backWidth, backHeight, foreground, foreWidth, foreHeight, &blendWidth, &blendHeight);

				//write
				LPWSTR out = saveFileDialog(hWnd, L"Save Image", L"PNG files (*.png)\0*.png\0All Files\0*.*\0", L".png");
				if (out == NULL) {
					free(foreground);
					free(background);
					break;
				}

				//cut out any pixel matching the background
				for (int y = 0; y < blendHeight; y++) {
					for (int x = 0; x < blendWidth; x++) {
						COLOR32 blendC = blend[x + y * blendWidth];
						COLOR32 bgC = background[x + y * backWidth];
						if (blendC == bgC) {
							blend[x + y * blendWidth] = 0;
						}
					}
				}

				//if we're in tiled mode, write background to transparent pixels in occupied tiles
				if (isTiled) {
					int tilesX = blendWidth / 8, tilesY = blendHeight / 8;
					for (int tileY = 0; tileY < tilesY; tileY++) {
						for (int tileX = 0; tileX < tilesX; tileX++) {

							//find transparent pixel in the tile
							int hasTransparent = 0, hasOpaque = 0;
							for (int i = 0; i < 64; i++) {
								int x = i % 8;
								int y = i / 8;
								COLOR32 c = blend[(x + tileX * 8) + (y + tileY * 8) * blendWidth];
								if ((c >> 24) == 0) {
									hasTransparent = 1;
								}
								if ((c >> 24)) {
									hasOpaque = 1;
								}
							}

							//if has opaque and transparent, copy background to transparent pixels
							if (hasTransparent && hasOpaque) {
								for (int i = 0; i < 64; i++) {
									int x = i % 8;
									int y = i / 8;
									COLOR32 c = blend[(x + tileX * 8) + (y + tileY * 8) * blendWidth];
									if ((c >> 24) == 0) {
										COLOR32 b = background[(x + tileX * 8) + (y + tileY * 8) * backWidth];
										blend[(x + tileX * 8) + (y + tileY * 8) * blendWidth] = b;
									}
								}
							}
						}
					}
				}

				ImgWrite(blend, blendWidth, blendHeight, out);

				free(out);
				free(foreground);
				free(background);
				free(blend);
			}
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

typedef struct LINKEDITDATA_ {
	HWND hWndFormat;
	HWND hWndOK;
	HWND hWndObjects;

	COMBO2D *combo;
	HWND *hWndEditors;
	int nEditors;
} LINKEDITDATA;

LRESULT CALLBACK LinkEditWndPRoc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	LINKEDITDATA *data = (LINKEDITDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case WM_CREATE:
		{
			if (data == NULL) {
				data = (LINKEDITDATA *) calloc(1, sizeof(LINKEDITDATA));
				SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
			}

			CreateStatic(hWnd, L"Format:", 10, 10, 50, 22);
			data->hWndFormat = CreateCombobox(hWnd, gComboFormats + 1, COMBO2D_TYPE_MAX - 1, 70, 10, 300, 100, COMBO2D_TYPE_5BG - 1);
			CreateStatic(hWnd, L"Objects:", 10, 37, 50, 22);
			data->hWndObjects = CreateCheckedListView(hWnd, 70, 37, 300, 200);
			data->hWndOK = CreateButton(hWnd, L"OK", 170, 252, 100, 22, TRUE);
			data->combo = NULL;

			HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			data->nEditors = GetAllEditors(hWndMain, FILE_TYPE_INVALID, NULL, 0);
			data->hWndEditors = (HWND *) calloc(data->nEditors, sizeof(HWND));
			GetAllEditors(hWndMain, FILE_TYPE_INVALID, data->hWndEditors, data->nEditors);

			SetWindowSize(hWnd, 380, 284);
			SetGUIFont(hWnd);
			break;
		}
		case NV_INITIALIZE:
		{
			//active editor window in lParam.
			HWND hWndEditor = (HWND) lParam;
			OBJECT_HEADER *obj = (OBJECT_HEADER *) EditorGetObject(hWndEditor);

			//get combo if the object is a part of one
			COMBO2D *combo = (COMBO2D *) obj->combo;
			data->combo = combo;

			//filter the window list based on applicability. If we're not in a combo, filter out all those
			//that are. If we are in one, filter out all those not in the same one.
			for (int i = 0; i < data->nEditors; i++) {
				int remove = 0;

				OBJECT_HEADER *obj2 = (OBJECT_HEADER *) EditorGetObject(data->hWndEditors[i]);
				if (obj2 == NULL) {
					remove = 1;
				} else if (combo != NULL) {
					if (obj2->combo != NULL && obj2->combo != combo) remove = 1;
				} else {
					if (obj2->combo != NULL) remove = 1;
				}

				//remove
				if (remove) {
					memmove(data->hWndEditors + i, data->hWndEditors + i + 1, (data->nEditors - i - 1) * sizeof(HWND));
					i--;
					data->nEditors--;
				}
			}
			
			for (int i = 0; i < data->nEditors; i++) {
				WCHAR buf[MAX_PATH];
				HWND hWndThisEditor = data->hWndEditors[i];
				OBJECT_HEADER *obj = (OBJECT_HEADER *) EditorGetObject(hWndThisEditor);

				SendMessage(hWndThisEditor, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) buf);
				AddCheckedListViewItem(data->hWndObjects, buf, i, (hWndEditor == hWndThisEditor) || (combo != NULL && obj->combo == combo));
			}

			//populate type field
			if (combo != NULL) {
				SendMessage(data->hWndFormat, CB_SETCURSEL, combo->header.format - 1, TRUE);
			}

			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				if (hWndControl == data->hWndOK) {
					int fmt = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0) + 1;

					//if no combo in data, create a new one.
					if (data->combo == NULL) {
						COMBO2D *combo = (COMBO2D *) calloc(1, sizeof(COMBO2D));
						combo2dInit(combo, fmt);
						
						//for each window selected, link
						for (int i = 0; i < data->nEditors; i++) {
							HWND hWndThisEditor = data->hWndEditors[i];
							if (CheckedListViewIsChecked(data->hWndObjects, i)) {
								combo2dLink(combo, EditorGetObject(hWndThisEditor));
							}
						}
					} else {
						data->combo->header.format = fmt;

						//first, add any links we don't have, then remove the removed ones.
						for (int i = 0; i < data->nEditors; i++) {
							HWND hWndThisEditor = data->hWndEditors[i];
							OBJECT_HEADER *obj = EditorGetObject(hWndThisEditor);

							if (CheckedListViewIsChecked(data->hWndObjects, i) && obj->combo == NULL) {
								combo2dLink(data->combo, obj);
							}
						}

						for (int i = 0; i < data->nEditors; i++) {
							HWND hWndThisEditor = data->hWndEditors[i];
							OBJECT_HEADER *obj = EditorGetObject(hWndThisEditor);

							if (!CheckedListViewIsChecked(data->hWndObjects, i) && obj->combo == data->combo) {
								combo2dUnlink(data->combo, obj);
							}
						}
					}

					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}
			}
			break;
		}
		case WM_DESTROY:
			if (data != NULL) {
				if (data->hWndEditors != NULL) free(data->hWndEditors);
				free(data);
				SetWindowLongPtr(hWnd, 0, 0);
			}
			break;
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
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

void RegisterAlphaBlendClass() {
	RegisterGenericClass(L"AlphaBlendClass", AlphaBlendWndProc, sizeof(LPVOID) * 6);
}

void RegisterLinkEditClass() {
	RegisterGenericClass(L"LinkEditClass", LinkEditWndPRoc, sizeof(LPVOID));
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
		COLOR32 *bits = ImgRead(g_configuration.backgroundPath, &width, &height);
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
	RegisterAlphaBlendClass();
	RegisterLinkEditClass();
}

void InitializeDpiAwareness(void) {
	if (!g_configuration.dpiAware) return;

	HMODULE hUser32 = GetModuleHandle(L"USER32.DLL");
	BOOL (WINAPI *SetProcessDPIAwareFunc) (void) = (BOOL (WINAPI *) (void))
		GetProcAddress(hUser32, "SetProcessDPIAware");
	BOOL (WINAPI *SetProcessDpiAwarenessContextFunc) (void *) = (BOOL (WINAPI *) (void *))
		GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
	if (SetProcessDPIAwareFunc != NULL) {

		//try for per-monitor awareness if supported
		HMODULE hShCore = LoadLibrary(L"SHCORE.DLL");
		HRESULT(WINAPI *SetProcessDpiAwarenessFunc) (int) = (HRESULT(WINAPI *) (int))
			GetProcAddress(hShCore, "SetProcessDpiAwareness");
		if (SetProcessDpiAwarenessContextFunc != NULL) {
			//Windows 10 and above...
			SetProcessDpiAwarenessContextFunc((void *) -4); //per-monitor v2
		} else if (SetProcessDpiAwarenessFunc != NULL) {
			//Windows 8.1 and above...
			SetProcessDpiAwarenessFunc(2); //per-monitor DPI aware
		} else {
			//Default to basic DPI awareness
			sDontScaleGuiFont = 1; //system DPI awareness scales the GUI font for some reason
			SetProcessDPIAwareFunc(); //system DPI aware
		}
		FreeLibrary(hShCore);

	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	HACCEL hAccel = LoadAccelerators(hInstance, (LPCWSTR) IDR_ACCELERATOR1);
	CoInitialize(NULL);

	SetConfigPath();
	ReadConfiguration(g_configPath);
	CheckExistingAppWindow();
	InitializeDpiAwareness();

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
