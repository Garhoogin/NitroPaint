#include <Windows.h>
#include <CommCtrl.h>
#include <Uxtheme.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <stdio.h>

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
#include "nftrviewer.h"
#include "lyteditor.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "version.lib")


//implementation of _ftol2_sse and _fltused
#ifdef _MSC_VER

int _fltused; // for LINK

long __declspec(naked) _ftol2_sse(double d) {
	//pentium 4+ implementation
	_asm {
		push ebp
		mov ebp, esp
		sub esp, 8
		and esp, ~7
		fstp qword ptr [esp]
		cvttsd2si eax, [esp]
		leave
		ret
	}
	__assume(0);
}

#endif


size_t my_strnlen(const char *_Str, size_t _MaxCount);
size_t my_wcsnlen(const wchar_t *_Str, size_t _MaxCount);
#define strnlen my_strnlen
#define wcsnlen my_wcsnlen

// implementation of strnlen (comment out if you have an implementation of strnlen)
size_t my_strnlen(const char *_Str, size_t _MaxCount) {
	size_t len = 0;
	while (*(_Str++) && len < _MaxCount) len++;
	return len;
}

// implementation of wcsnlen (comment out if you have an implementation of wcsnlen)
size_t my_wcsnlen(const wchar_t *_Str, size_t _MaxCount) {
	size_t len = 0;
	while (*(_Str++) && len < _MaxCount) len++;
	return len;
}

static const char *GetVersionFetch(void) {
	WCHAR path[MAX_PATH];
	GetModuleFileName(NULL, path, MAX_PATH);

	DWORD handle;
	DWORD dwSize = GetFileVersionInfoSize(path, &handle);
	if (dwSize == 0) {
		return "";
	}

	BYTE *buf = (BYTE *) calloc(1, dwSize);
	if (!GetFileVersionInfo(path, handle, dwSize, buf)) {
		free(buf);
		return "";
	}

	UINT size;
	VS_FIXEDFILEINFO *info;
	if (!VerQueryValue(buf, L"\\", &info, &size)) {
		free(buf);
		return "";
	}

	//format version
	static char buffer[24] = { 0 };
	DWORD ms = info->dwFileVersionMS, ls = info->dwFileVersionLS;
	sprintf(buffer, "%d.%d.%d.%d", HIWORD(ms), LOWORD(ms), HIWORD(ls), LOWORD(ls));
	return buffer;
}

const char *NpGetVersion(void) {
	//fetch version once
	static const char *text = NULL;
	if (text == NULL) text = GetVersionFetch();

	return text;
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

int NpGetSuggestedViewerScale(void) {
	float dpiScale = GetDpiScale();
	if (dpiScale <= 1.0f) return 1; // 1x

	//round up to a power of 2 scale
	float log2Scale = 1.0f;
	while (log2Scale < dpiScale && log2Scale < 8.0f) log2Scale *= 2.0f;
	return (int) log2Scale;
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

static int EnsurePngClipboardFormat(void) {
	static int fmt = 0;
	if (fmt) return fmt;

	return fmt = RegisterClipboardFormat(L"PNG");
}

static int EnsureOpxDibAlphaPalClipboardFormat(void) {
	static int fmt = 0;
	if (fmt) return fmt;

	return fmt = RegisterClipboardFormat(L"OPX_DIB_ALPHA_PAL");
}

static int EnsureOpxDibAlphaImgClipboardFormat(void) {
	static int fmt = 0;
	if (fmt) return fmt;

	return fmt = RegisterClipboardFormat(L"OPX_DIB_ALPHA_IMG");
}

void ClipCopyBitmapEx(const void *bits, unsigned int width, unsigned int height, int indexed, const COLOR32 *pltt, unsigned int nPltt) {
	//assume clipboard is already owned and emptied

	//fix maximum size of palette
	if (nPltt > 0x100) nPltt = 0x100;
	if (!indexed) nPltt = 0;

	//compute size of bitmap data
	unsigned int nBpp = indexed ? 8 : 32;
	unsigned int stride = ((width * nBpp + 7) / 8 + 3) & ~3; // round to integer byte count, up to a multiple of 4 bytes
	unsigned int sizeDibV5 = sizeof(BITMAPV5HEADER) + nPltt * sizeof(COLOR32) + height * stride;
	unsigned int sizeDib = sizeof(BITMAPINFOHEADER) + nPltt * sizeof(COLOR32) + height * stride;

	//set DIBv5, DIB
	HGLOBAL hBmi5 = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeDibV5);
	HGLOBAL hBmi = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeDib);

	//PNG data
	void *png = NULL;
	unsigned int pngSize = 0;

	BITMAPV5HEADER *bmi5 = (BITMAPV5HEADER *) GlobalLock(hBmi5);
	bmi5->bV5Size = sizeof(*bmi5);
	bmi5->bV5Width = width;
	bmi5->bV5Height = height;
	bmi5->bV5Planes = 1;
	bmi5->bV5BitCount = nBpp;
	bmi5->bV5Compression = indexed ? BI_RGB : BI_BITFIELDS;
	bmi5->bV5ClrUsed = nPltt;
	bmi5->bV5ClrImportant = nPltt;
	bmi5->bV5RedMask   = 0x00FF0000;
	bmi5->bV5GreenMask = 0x0000FF00;
	bmi5->bV5BlueMask  = 0x000000FF;
	bmi5->bV5AlphaMask = 0xFF000000;
	bmi5->bV5CSType = LCS_sRGB;
	bmi5->bV5Intent = LCS_GM_IMAGES;

	BITMAPINFOHEADER *bmi = (BITMAPINFOHEADER *) GlobalLock(hBmi);
	bmi->biSize = sizeof(*bmi);
	bmi->biWidth = width;
	bmi->biHeight = height;
	bmi->biPlanes = 1;
	bmi->biBitCount = nBpp;
	bmi->biCompression = BI_RGB;
	bmi->biClrUsed = nPltt;
	bmi->biClrImportant = nPltt;

	if (indexed) {
		//indexed: copy bits direct
		const unsigned char *px = (const unsigned char *) bits;
		COLOR32 *cDest = (COLOR32 *) (bmi + 1);
		COLOR32 *cDest5 = (COLOR32 *) (bmi5 + 1);
		unsigned char *bDest = (unsigned char *) (cDest + nPltt);
		unsigned char *bDest5 = (unsigned char *) (cDest5 + nPltt);

		int transparentPalette = 0;

		//put palette
		for (unsigned int i = 0; i < nPltt; i++) {
			COLOR32 c = pltt[i];
			c = REVERSE(c);

			cDest[i] = c;
			cDest5[i] = c;

			if ((c >> 24) != 0xFF) transparentPalette = 1;
		}
		
		//put colors
		for (unsigned int y = 0; y < height; y++) {
			memcpy(bDest + (height - 1 - y) * stride, px + y * width, width);
			memcpy(bDest5 + (height - 1 - y) * stride, px + y * width, width);
		}

		if (transparentPalette) {
			//OPX palette alpha info
			HGLOBAL hAlpha = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(BITMAPINFOHEADER) + 0x10 * 0x10 + 0x100 * sizeof(COLOR32));
			BITMAPINFOHEADER *pAlphaBmi = (BITMAPINFOHEADER *) GlobalLock(hAlpha);
			pAlphaBmi->biSize = sizeof(BITMAPINFOHEADER);
			pAlphaBmi->biWidth = 0x10;
			pAlphaBmi->biHeight = 0x10;
			pAlphaBmi->biClrUsed = 0x100;
			pAlphaBmi->biClrImportant = 0x100;
			pAlphaBmi->biPlanes = 1;
			pAlphaBmi->biBitCount = 8;

			COLOR32 *alphaPltt = (COLOR32 *) (pAlphaBmi + 1);
			for (unsigned int i = 0; i < nPltt; i++) {
				unsigned int a = pltt[i] >> 24;
				alphaPltt[i] = a | (a << 8) | (a << 16);
			}

			unsigned char *alphaMap = (unsigned char *) (alphaPltt + 0x100);
			for (unsigned int i = 0; i < 0x100; i++) {
				alphaMap[i] = i ^ 0xF0; // increasing bits, upside down
			}

			GlobalUnlock(hAlpha);
			SetClipboardData(EnsureOpxDibAlphaPalClipboardFormat(), hAlpha);
		}

		//PNG info
		ImgWriteMemIndexed(bits, width, height, pltt, nPltt, &png, &pngSize);
	} else {
		//non-indexed: write raw bits, using alpha premultiplication (required for software like paint.NET)
		const COLOR32 *px = (const COLOR32 *) bits;
		COLOR32 *cDest5 = (COLOR32 *) (bmi5 + 1);
		COLOR32 *cDest = (COLOR32 *) (bmi + 1);

		int isTransparent = 0;
		for (unsigned int y = 0; y < height; y++) {
			for (unsigned int x = 0; x < width; x++) {
				COLOR32 c = px[x + y * width];
				c = REVERSE(c);

				//alpha premultiplication:
				unsigned int a = c >> 24;
				unsigned int r = ((c >>  0) & 0xFF) * a / 255;
				unsigned int g = ((c >>  8) & 0xFF) * a / 255;
				unsigned int b = ((c >> 16) & 0xFF) * a / 255;
				COLOR32 c2 = r | (g << 8) | (b << 16) | (a << 24);

				cDest[x + (height - 1 - y) * width] = c;   // non-premultiplied
				cDest5[x + (height - 1 - y) * width] = c2; // premultiplied

				if ((c >> 24) != 0xFF) isTransparent = 1;
			}
		}

		//copy alpha bitmap
		if (isTransparent) {
			unsigned int alphaStride = (width + 3) & ~3;
			HGLOBAL hBmiAlpha = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE, sizeof(BITMAPINFO) + 0x400 + height * alphaStride);
			BITMAPINFOHEADER *bmi = (BITMAPINFOHEADER *) GlobalLock(hBmiAlpha);
			bmi->biSize = sizeof(*bmi);
			bmi->biWidth = width;
			bmi->biHeight = height;
			bmi->biPlanes = 1;
			bmi->biBitCount = 8;
			bmi->biCompression = BI_RGB;
			bmi->biClrUsed = 256;
			bmi->biClrImportant = 256;

			COLOR32 *pGray = (COLOR32 *) (bmi + 1);
			unsigned char *pAlpha = (unsigned char *) (pGray + 0x100);
			for (unsigned int i = 0; i < 0x100; i++) pGray[i] = i | (i << 8) | (i << 16);

			for (unsigned int y = 0; y < height; y++) {
				for (unsigned int x = 0; x < width; x++) {
					pAlpha[(height - 1 - y) * alphaStride + x] = px[y * width + x] >> 24;
				}
			}

			GlobalUnlock(hBmiAlpha);
			SetClipboardData(EnsureOpxDibAlphaImgClipboardFormat(), hBmiAlpha);
		}
	}

	GlobalUnlock(hBmi5);
	GlobalUnlock(hBmi);
	SetClipboardData(CF_DIBV5, hBmi5);
	SetClipboardData(CF_DIB, hBmi);

	if (png != NULL) {
		HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, pngSize);
		void *pGlobal = GlobalLock(hGlobal);
		memcpy(pGlobal, png, pngSize);
		free(png);
		GlobalUnlock(hGlobal);

		int fmtPng = EnsurePngClipboardFormat();
		SetClipboardData(fmtPng, hGlobal);
	}
}

void copyBitmap(COLOR32 *px, int width, int height) {
	ClipCopyBitmapEx(px, width, height, 0, NULL, 0);
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

static COLOR32 *ClipGetClipboardDIBFromHGlobal(HGLOBAL hGlobal, int *pWidth, int *pHeight, unsigned char **indexed, COLOR32 **pplt, int *pPaletteSize) {
	//create bitmap file header
	BITMAPINFO *pbmi = (BITMAPINFO *) GlobalLock(hGlobal);
	SIZE_T dibSize = GetBitmapSize(pbmi);
	BITMAPFILEHEADER *bmfh = (BITMAPFILEHEADER *) malloc(sizeof(BITMAPFILEHEADER) + dibSize);
	memcpy(bmfh + 1, pbmi, dibSize);
	bmfh->bfType = 0x4D42; //'BM'
	bmfh->bfSize = sizeof(BITMAPFILEHEADER) + GetBitmapSize(pbmi);
	bmfh->bfReserved1 = 0;
	bmfh->bfReserved2 = 0;
	bmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + GetBitmapOffsetBits(pbmi);

	COLOR32 *pxDib = ImgReadMemEx((unsigned char *) bmfh, bmfh->bfSize, pWidth, pHeight, indexed, pplt, pPaletteSize);
	free(bmfh);
	GlobalUnlock(hGlobal);
	return pxDib;
}

static COLOR32 *ClipGetClipboardDIB(int *pWidth, int *pHeight, unsigned char **indexed, COLOR32 **pplt, int *pPaletteSize) {
	HGLOBAL hDib = GetClipboardData(CF_DIB);
	if (hDib == NULL) {
		//no DIB
		*pWidth = *pHeight = *pPaletteSize = 0;
		*indexed = NULL;
		*pplt = NULL;
		return NULL;
	}

	COLOR32 *pxDib = ClipGetClipboardDIBFromHGlobal(hDib, pWidth, pHeight, indexed, pplt, pPaletteSize);
	
	//get opacity info for an indexed palette.
	if (*indexed != NULL) {
		//indexed image: get palette alpha data if it exists
		HGLOBAL hOpx = GetClipboardData(EnsureOpxDibAlphaPalClipboardFormat());

		//exists
		if (hOpx != NULL) {
			//the color table should hold the alpha values, referenced by the bitmap
			//image data with 1 pixel to 1 palette color.
			int alphaW, alphaH;
			COLOR32 *alphabm = ClipGetClipboardDIBFromHGlobal(hOpx, &alphaW, &alphaH, NULL, NULL, NULL);

			int nAlpha = alphaW * alphaH;
			if (nAlpha >= *pPaletteSize) nAlpha = *pPaletteSize;
			for (int i = 0; i < alphaW * alphaH && i < *pPaletteSize; i++) {
				COLOR32 c = alphabm[i];
				unsigned int a = (c & 0xFF);
				(*pplt)[i] = ((*pplt)[i] & 0x00FFFFFF) | (a << 24);
			}

			free(alphabm);

			//rewrite
			for (int i = 0;  i < *pWidth * *pHeight; i++) {
				pxDib[i] = (*pplt)[(*indexed)[i]];
			}
		}
	} else {
		//direct color image: get alpha bitmap if it exists
		HGLOBAL hAlpha = GetClipboardData(EnsureOpxDibAlphaImgClipboardFormat());

		//exists
		if (hAlpha != NULL) {
			//read bitmap: alpha channel per pixel of bitmap
			int alphaW, alphaH;
			COLOR32 *alphabm = ClipGetClipboardDIBFromHGlobal(hAlpha, &alphaW, &alphaH, NULL, NULL, NULL);

			for (int y = 0; y < alphaH; y++) {
				if (y >= *pHeight) break;

				for (int x = 0; x < alphaW; x++) {
					if (x >= *pWidth) break;

					//set alpha
					pxDib[y * *pWidth + x] = (pxDib[y * *pWidth + x] & 0x00FFFFFF) | ((alphabm[x + y * alphaW] & 0xFF) << 24);
				}
			}

			free(alphabm);
		}
	}

	return pxDib;
}

static COLOR32 *ClipGetClipboardPNG(int *pWidth, int *pHeight, unsigned char **indexed, COLOR32 **pplt, int *pPaletteSize) {
	int fmt = EnsurePngClipboardFormat();
	HGLOBAL hPng = GetClipboardData(fmt);
	if (hPng == NULL) {
		//no clipboard data
		*pWidth = *pHeight = *pPaletteSize = 0;
		*indexed = NULL;
		*pplt = NULL;
		return NULL;
	}

	void *pngData = GlobalLock(hPng);
	SIZE_T size = GlobalSize(hPng);

	COLOR32 *px = ImgReadMemEx(pngData, size, pWidth, pHeight, indexed, pplt, pPaletteSize);

	GlobalUnlock(hPng);
	return px;
}

static COLOR32 *ClipGetClipboardFileImage(int *pWidth, int *pHeight, unsigned char **indexed, COLOR32 **pplt, int *pPaletteSize) {
	HANDLE hGDrop = GetClipboardData(CF_HDROP);
	if (hGDrop != NULL) {
		HDROP hDrop = (HDROP) GlobalLock(hGDrop);

		//try to read 
		unsigned int nFile = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
		for (unsigned int i = 0; i < nFile; i++) {
			unsigned int bufsiz = DragQueryFileW(hDrop, i, NULL, 0);
			WCHAR *buf = (WCHAR *) calloc(bufsiz + 1, sizeof(WCHAR));
			DragQueryFile(hDrop, i, buf, bufsiz + 1);

			//try parse
			COLOR32 *px = ImgReadEx(buf, pWidth, pHeight, indexed, pplt, pPaletteSize);
			free(buf);

			if (px != NULL) {
				GlobalUnlock(hGDrop);
				return px;
			}
		}

		GlobalUnlock(hGDrop);
	}

	//no data
	*pWidth = *pHeight = 0;
	if (indexed != NULL) *indexed = NULL;
	if (pplt != NULL) *pplt = NULL;
	if (pPaletteSize != NULL) *pPaletteSize = 0;
	return NULL;
}

COLOR32 *GetClipboardBitmap(int *pWidth, int *pHeight, unsigned char **indexed, COLOR32 **pplt, int *pPaletteSize) {
	//if we've made it this far, we have DIB data on the clipboard. Read it now.
	int dibWidth = 0, dibHeight = 0, pngWidth = 0, pngHeight = 0, dibPaletteSize = 0, pngPaletteSize = 0;
	unsigned char *dibIndex = NULL, *pngIndex = NULL;
	COLOR32 *dibPalette = NULL, *pngPalette = NULL;
	COLOR32 *pxDib = ClipGetClipboardDIB(&dibWidth, &dibHeight, &dibIndex, &dibPalette, &dibPaletteSize);
	COLOR32 *pxPng = ClipGetClipboardPNG(&pngWidth, &pngHeight, &pngIndex, &pngPalette, &pngPaletteSize);

	//check also 

	//if neither format available...
	if (pxPng == NULL && pxDib == NULL) {
		//check clipboard for file
		int fileWidth, fileHeight, filePaletteSize;
		unsigned char *fileIndexed;
		COLOR32 *filePalette;
		COLOR32 *pxFile = ClipGetClipboardFileImage(&fileWidth, &fileHeight, &fileIndexed, &filePalette, &filePaletteSize);
		if (pxFile != NULL) {
			//found
			*pWidth = fileWidth;
			*pHeight = fileHeight;
			if (indexed != NULL) *indexed = fileIndexed;
			else if (fileIndexed != NULL) free(fileIndexed);
			if (pplt != NULL) *pplt = filePalette;
			else if (filePalette != NULL) free(filePalette);
			if (pPaletteSize != NULL) *pPaletteSize = filePaletteSize;
			return pxFile;
		}

		*pWidth = 0;
		*pHeight = 0;
		if (pngPalette != NULL) free(pngPalette);
		if (dibPalette != NULL) free(dibPalette);
		if (indexed != NULL) *indexed = NULL;
		if (pplt != NULL) *pplt = NULL;
		if (pPaletteSize != NULL) *pPaletteSize = 0;
		return NULL;
	}

	//else, we have both data available. Read the PNG data and compare against the DIB data.
	int usePng = 0;
	if (pxDib != NULL && pxPng == NULL) {
		//has only PNG
		usePng = 0;
	} else if (pxDib == NULL && pxPng != NULL) {
		//has only DIB
		usePng = 1;
	} else {
		//we have both DIB and PNG. Use DIB if it is indexed and the PNG is not, otherwise use the PNG.
		if (dibIndex != NULL && pngIndex == NULL) usePng = 0;
		else usePng = 1;
	}

	if (usePng) {
		if (pxDib != NULL) free(pxDib);
		if (dibIndex != NULL) free(dibIndex);
		if (dibPalette != NULL) free(dibPalette);

		*pWidth = pngWidth;
		*pHeight = pngHeight;
		if (pplt != NULL) *pplt = pngPalette;
		else free(pngPalette);
		if (indexed != NULL) *indexed = pngIndex;
		else free(pngIndex);
		if (pPaletteSize != NULL) *pPaletteSize = pngPaletteSize;
		return pxPng;
	} else {
		if (pxPng != NULL) free(pxPng);
		if (pngIndex != NULL) free(pngIndex);
		if (pngPalette != NULL) free(pngPalette);

		*pWidth = dibWidth;
		*pHeight = dibHeight;
		if (pplt != NULL) *pplt = dibPalette;
		else free(dibPalette);
		if (indexed != NULL) *indexed = dibIndex;
		else free(dibIndex);
		if (pPaletteSize != NULL) *pPaletteSize = dibPaletteSize;
		return pxDib;
	}
}


COLOR32 *ActRead(const unsigned char *buffer, unsigned int size, unsigned int *pnColors) {
	//two valid file sizes (first: no count+transparent index)
	if (size != 0x300 && size != 0x304) return NULL;

	unsigned int nColors = 0x100;
	unsigned int transparentColor = 0;
	if (size == 0x304) {
		nColors = *(const uint16_t *) (buffer + 0x300);
		transparentColor = *(const uint16_t *) (buffer + 0x302);

		if (nColors > 0x100) return NULL;
	}

	COLOR32 *cols = (COLOR32 *) calloc(nColors, sizeof(COLOR32));
	if (cols == NULL) return NULL; // error

	//read color palette colors
	for (unsigned int i = 0; i < nColors; i++) {
		const unsigned char *srcCol = &buffer[3 * i];
		cols[i] = srcCol[0] | (srcCol[1] << 8) | (srcCol[2] << 16) | 0xFF000000;
	}

	//process transparent color
	if (size == 0x304 && transparentColor < nColors) {
		cols[transparentColor] &= 0x00FFFFFF; // alpha=0
	}

	*pnColors = 0x100;
	return cols;
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

static char *propGetProperty(const char *ptr, unsigned int size, const char *name) {
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

static const char *propNextLine(const char *ptr, const char *end) {
	while (ptr < end && *ptr != '\n' && *ptr != '\r') {
		//scan for newline
		ptr++;
	}

	//scan forward all whitespace
	while (ptr < end && (*ptr <= ' ' && *ptr > '\0')) ptr++;
	return ptr;
}

static const char *propToValue(const char *ptr, const char *end) {
	//scan for :
	while (ptr < end && *ptr != ':') {
		ptr++;
	}

	//scan forward all whitespace
	while (ptr < end && (*ptr <= ' ' && *ptr > '\0')) ptr++;
	return ptr;
}

static void parseOffsetSizePair(const char *pair, int *offset, int *size) {
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

static int specIsSpec(char *buffer, int size) {
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

		NCLR *nclr = NULL;
		NCGR *ncgr = NULL;
		NSCR *nscr = NULL;

		//read applicable sections
		if (pltRef != NULL) {
			nclr = (NCLR *) calloc(1, sizeof(NCLR));
			PalRead(nclr, dfc->data + pltOffset, pltSize);
			nclr->header.format = NCLR_TYPE_COMBO;
			combo2dLink(combo, &nclr->header);
		}
		if (chrRef != NULL) {
			ncgr = (NCGR *) calloc(1, sizeof(NCGR));
			ChrRead(ncgr, dfc->data + chrOffset, chrSize);
			ncgr->header.format = NCGR_TYPE_COMBO;
			combo2dLink(combo, &ncgr->header);
		}
		if (scrRef != NULL) {
			nscr = (NSCR *) calloc(1, sizeof(NSCR));
			ScrRead(nscr, dfc->data + scrOffset, scrSize);
			nscr->header.format = NSCR_TYPE_COMBO;
			combo2dLink(combo, &nscr->header);
		}

		//if there is already an NCLR open, close it.
		if (pltRef != NULL) {
			if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
			data->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, nclr);

			EditorSetFile(data->hWndNclrViewer, pathBuffer);
		}

		//if there is already an NCGR open, close it.
		if (chrRef != NULL) {
			if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
			data->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, ncgr);
			InvalidateRect(data->hWndNclrViewer, NULL, FALSE);

			EditorSetFile(data->hWndNcgrViewer, pathBuffer);
		}

		//create NSCR editor and make it active
		if (scrRef != NULL) {
			HWND hWndNscrViewer = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, nscr);

			EditorSetFile(hWndNscrViewer, pathBuffer);
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
			data->hWndNcerViewer = CreateNcerViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
			break;
		case FILE_TYPE_NSBTX:
			CreateNsbtxViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
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
		case FILE_TYPE_FONT:
			CreateNftrViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_BNLL:
			CreateBnllViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_BNCL:
			CreateBnclViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
			break;
		case FILE_TYPE_BNBL:
			CreateBnblViewer(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, path);
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
			for (unsigned int i = 0; i < combo->links.length; i++) {
				OBJECT_HEADER *object;
				StListGet(&combo->links, i, &object);
				int type = object->type;

				HWND h = NULL;
				switch (type) {

					case FILE_TYPE_PALETTE:
						object->combo = (void *) combo;

						//if there is already an NCLR open, close it.
						if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
						h = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, (NCLR *) object);
						data->hWndNclrViewer = h;
						break;

					case FILE_TYPE_CHARACTER:
						object->combo = (void *) combo;

						//if there is already an NCGR open, close it.
						if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
						h = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, (NCGR *) object);
						data->hWndNcgrViewer = h;
						InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
						break;

					case FILE_TYPE_SCREEN:
						//create NSCR and make it active
						object->combo = (void *) combo;
						h = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, (NSCR *) object);
						break;

					case FILE_TYPE_CELL:
						//create NCER and make it active
						object->combo = (void *) combo;
						h = CreateNcerViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (NCER *) object);
						break;

					case FILE_TYPE_NANR:
						object->combo = (void *) combo;
						h = CreateNanrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (NANR *) object);
						break;
				}

				//set compression type for all links
				(*(OBJECT_HEADER **) StListGetPtr(&combo->links, i))->compression = compressionType;

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
		case FILE_TYPE_FONT:
			return 8;
		case FILE_TYPE_TEXTURE:
			return 9;
		case FILE_TYPE_NSBTX:
			return 10;
	}
	return 0;
}

static int SortWindowsProc(const void *p1, const void *p2) {
	HWND h1 = *(HWND *) p1;
	HWND h2 = *(HWND *) p2;
	int type1 = EditorGetType(h1);
	int type2 = EditorGetType(h2);
	
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
	NSCRVIEWERDATA *nscrViewerData = (NSCRVIEWERDATA *) EditorGetData(hWnd);
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
		EditorMgrInit(hWnd);
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
													 FILTER_ALL  FILTER_PALETTE FILTER_CHARACTER FILTER_SCREEN  FILTER_CELL
													 FILTER_ANIM FILTER_COMBO2D FILTER_TEXARC    FILTER_TEXTURE FILTER_FONT
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

						NCER *ncer = (NCER *) calloc(1, sizeof(NCER));
						CellInit(ncer, NCER_TYPE_NCER);
						ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
						ncer->nCells = 1;
						ncer->cells = (NCER_CELL *) calloc(1, sizeof(NCER_CELL));
						ncer->cells[0].attr = NULL;
						ncer->cells[0].nAttribs = 0;
						ncer->cells[0].cellAttr = 0;

						//if a character editor is open, use its mapping mode
						HWND hWndCharacterEditor = data->hWndNcgrViewer;
						if (hWndCharacterEditor != NULL) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndCharacterEditor);
							ncer->mappingMode = ncgrViewerData->ncgr->mappingMode;
						}

						data->hWndNcerViewer = CreateNcerViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, ncer);
						break;
					}
					case ID_NEW_NEWPALETTE:
					{
						if (data->hWndNclrViewer != NULL) DestroyChild(data->hWndNclrViewer);
						data->hWndNclrViewer = NULL;

						HWND h = CreateWindow(L"NewPaletteClass", L"New Palette", WS_CAPTION | WS_BORDER | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWnd, NULL, NULL, NULL);
						DoModal(h);
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
						NANR *nanr = (NANR *) calloc(1, sizeof(NANR));
						ObjInit(&nanr->header, FILE_TYPE_NANR, NANR_TYPE_NANR);

						nanr->nSequences = 1;
						nanr->sequences = (NANR_SEQUENCE *) calloc(1, sizeof(NANR_SEQUENCE));
						nanr->sequences[0].nFrames = 1;
						nanr->sequences[0].mode = NANR_SEQ_MODE_FORWARD;
						nanr->sequences[0].type = NANR_SEQ_TYPE_INDEX_SRT | (NANR_SEQ_TYPE_CELL << 16);
						nanr->sequences[0].startFrameIndex = 0;
						nanr->sequences[0].frames = (FRAME_DATA *) calloc(1, sizeof(FRAME_DATA));
						nanr->sequences[0].frames[0].nFrames = 4;
						nanr->sequences[0].frames[0].pad_ = 0xBEEF;
						nanr->sequences[0].frames[0].animationData = calloc(1, sizeof(ANIM_DATA_SRT));
						memset(nanr->sequences[0].frames[0].animationData, 0, sizeof(ANIM_DATA_SRT));

						ANIM_DATA_SRT *srt = (ANIM_DATA_SRT *) nanr->sequences[0].frames[0].animationData;
						srt->sx = 4096; // 1.0
						srt->sy = 4096; // 1.0

						HWND h = CreateNanrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, nanr);
						ShowWindow(h, SW_SHOW);
						break;
					}
					case ID_NEW_NEWTEXTUREARCHIVE:
					{
						TexArc *nsbtx = (TexArc *) calloc(1, sizeof(TexArc));
						TexarcInit(nsbtx, NSBTX_TYPE_NNS);
						
						//no need to init further
						CreateNsbtxViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, nsbtx);
						break;
					}
					case ID_NEW_NEWFONT:
					{
						//init sensible defaults
						NFTR *nftr = (NFTR *) calloc(1, sizeof(NFTR));
						NftrInit(nftr, NFTR_TYPE_NFTR_10);
						nftr->bpp = 1;
						nftr->hasCodeMap = 1;
						nftr->cellWidth = 8;
						nftr->cellHeight = 12;
						nftr->pxAscent = 10;
						nftr->lineHeight = 11;
						nftr->charset = FONT_CHARSET_UTF16;

						CreateNftrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, nftr);
						break;
					}
					case ID_NEWLAYOUT_LETTERLAYOUT:
					{
						BNLL *bnll = (BNLL *) calloc(1, sizeof(BNLL));
						BnllInit(bnll, BNLL_TYPE_BNLL);
						bnll->nMsg = 1;
						bnll->messages = (BnllMessage *) calloc(1, sizeof(BnllMessage));
						bnll->messages[0].pos.x.pos = 128;
						bnll->messages[0].pos.y.pos = 96;
						CreateBnllViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, bnll);
						break;
					}
					case ID_NEWLAYOUT_CELLLAYOUT:
					{
						BNCL *bncl = (BNCL *) calloc(1, sizeof(BNCL));
						BnclInit(bncl, BNCL_TYPE_BNCL);
						bncl->nCell = 1;
						bncl->cells = (BnclCell *) calloc(1, sizeof(BnclCell));
						bncl->cells[0].pos.x.pos = 128;
						bncl->cells[0].pos.y.pos = 96;
						CreateBnclViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, bncl);
						break;
					}
					case ID_NEWLAYOUT_BUTTONLAYOUT:
					{
						BNBL *bnbl = (BNBL *) calloc(1, sizeof(BNBL));
						BnblInit(bnbl, BNLL_TYPE_BNLL);
						bnbl->nRegion = 1;
						bnbl->regions = (BnblRegion *) calloc(1, sizeof(BnblRegion));
						bnbl->regions[0].pos.x.pos = 128;
						bnbl->regions[0].pos.y.pos = 96;
						bnbl->regions[0].width = 64;
						bnbl->regions[0].height = 64;
						CreateBnblViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, bnbl);
						break;
					}
					case ID_FILE_CONVERTTO:
					{
						HWND hWndFocused = (HWND) SendMessage(data->hWndMdi, WM_MDIGETACTIVE, 0, 0);
						if (hWndFocused == NULL) break;
						
						int editorType = EditorGetType(hWndFocused);
						if (editorType == FILE_TYPE_INVALID) break;

						EDITOR_DATA *editorData = (EDITOR_DATA *) EditorGetData(hWndFocused);
						if (editorData == NULL) break;

						LPCWSTR *formats = ObjGetFormatNamesByType(editorData->file->type);
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
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(data->hWndNcgrViewer);
							ncgrViewerData->transparent = state;
						}
						EditorInvalidateAllByType(hWnd, FILE_TYPE_CHAR);
						EditorInvalidateAllByType(hWnd, FILE_TYPE_SCREEN);
						EditorInvalidateAllByType(hWnd, FILE_TYPE_CELL);

						//update all screen editors
						for (size_t i = 0; i < data->edMgr.editorList.length; i++) {
							EDITOR_DATA *ed = *(EDITOR_DATA **) StListGetPtr(&data->edMgr.editorList, i);
							if (ed->file->type == FILE_TYPE_SCREEN) {
								SetNscrEditorTransparentProc(ed->hWnd, (void *) state);
							}
						}
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
						if (hWndFocus == NULL || EditorGetType(hWndFocus) != FILE_TYPE_SCREEN) {
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
						WCHAR *path = UiDlgBrowseForFolder(hWnd, L"Select texture folder...");
						if (path == NULL) break;

						BatchTexShowVramStatistics(hWnd, path);
						free(path);
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
					case ID_TOOLS_INDEXIMAGE:
					{
						HWND h = CreateWindow(L"IndexImageClass", L"Color Reduction", WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX),
							CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWnd, NULL, NULL, NULL);
						DoModal(h);
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
	HWND hWndColor0Setting;
	HWND hWndAlignmentCheckbox;
	HWND hWndAlignment;
	NpBalanceControl balance;
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

	NCLR *nclr;
	NCGR *ncgr;
	NSCR *nscr;

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
	nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, createData->nclr);
	nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, createData->ncgr);

	OBJECT_HEADER *palobj = EditorGetObject(nitroPaintStruct->hWndNclrViewer);
	OBJECT_HEADER *chrobj = EditorGetObject(nitroPaintStruct->hWndNcgrViewer);
	OBJECT_HEADER *scrobj = NULL;

	HWND hWndNscrViewer = NULL;
	if (createData->genParams.bgType != BGGEN_BGTYPE_BITMAP) {
		hWndNscrViewer = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMdi, createData->nscr);
		scrobj = EditorGetObject(hWndNscrViewer);
	} else {
		free(createData->nscr);
	}

	//link data
	ObjLinkObjects(palobj, chrobj);
	if (scrobj != NULL) ObjLinkObjects(chrobj, scrobj);

	//if a character base was used, the BG screen viewer might guess the character base incorrectly. 
	//in these cases, we need to set the correct character base here.
	if (hWndNscrViewer != NULL && createData->genParams.characterSetting.base > 0) {
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
	BgGenerate(params->createData->nclr, params->createData->ncgr, params->createData->nscr, 
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

void NpCreateBalanceInput(NpBalanceControl *ctl, HWND hWnd, int x, int y, int width) {
	int bottomY = y + 18;

	CreateStatic(hWnd, L"Balance:", x + 10, bottomY, 100, 22);
	CreateStatic(hWnd, L"Color Balance:", x + 10, bottomY + 27, 100, 22);
	CreateStaticAligned(hWnd, L"Lightness", x + 10 + 110, bottomY, 50, 22, SCA_RIGHT);
	CreateStaticAligned(hWnd, L"Color", x + 10 + 110 + 50 + 200, bottomY, 50, 22, SCA_LEFT);
	CreateStaticAligned(hWnd, L"Green", x + 10 + 110, bottomY + 27, 50, 22, SCA_RIGHT);
	CreateStaticAligned(hWnd, L"Red", x + 10 + 110 + 50 + 200, bottomY + 27, 50, 22, SCA_LEFT);
	ctl->hWndBalance = CreateTrackbar(hWnd, x + 10 + 110 + 50, bottomY, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
	ctl->hWndColorBalance = CreateTrackbar(hWnd, x + 10 + 110 + 50, bottomY + 27, 200, 22, BALANCE_MIN, BALANCE_MAX, BALANCE_DEFAULT);
	ctl->hWndEnhanceColors = CreateCheckbox(hWnd, L"Enhance Colors", x + 10, bottomY + 27 * 2, 200, 22, TRUE);
	CreateGroupbox(hWnd, L"Color", x, y, width, 3 * 27 - 5 + 10 + 10 + 10);
}

void NpGetBalanceSetting(NpBalanceControl *ctl, RxBalanceSetting *balance) {
	balance->balance = GetTrackbarPosition(ctl->hWndBalance);
	balance->colorBalance = GetTrackbarPosition(ctl->hWndColorBalance);
	balance->enhanceColors = GetCheckboxChecked(ctl->hWndEnhanceColors);
}

BOOL NpChooseColor15(HWND hWndMain, HWND hWndParent, COLOR *pColor) {
	NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);

	CHOOSECOLORW cc = { 0 };
	cc.lStructSize = sizeof(cc);
	cc.hInstance = NULL;
	cc.hwndOwner = hWndParent;
	cc.rgbResult = ColorConvertFromDS(*pColor);
	cc.lpCustColors = nitroPaintStruct->tmpCust;
	cc.Flags = 0x103;

	BOOL (__stdcall *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
	if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;
	if (ChooseColorFunction(&cc)) {
		*pColor = ColorConvertToDS(cc.rgbResult);
		return TRUE;
	} else {
		return FALSE;
	}
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
			HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			EnableWindow(hWndParent, FALSE);

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

			LPCWSTR bitDepths[] = {
				L"(4bpp) Text 16x16",
				L"(8bpp) Text 256x1",
				L"(8bpp) Affine 256x1",
				L"(8bpp) Affine EXT 256x16",
				L"(8bpp) Bitmap"
			};
			CreateStatic(hWnd, L"Format:", rightX, topY, 50, 22);
			data->nscrCreateDropdown = CreateCombobox(hWnd, bitDepths, sizeof(bitDepths) / sizeof(*bitDepths), rightX + 55, topY, 150, 22, 3);
			data->nscrCreateDither = CreateCheckbox(hWnd, L"Dither", rightX, topY + 27, 100, 22, FALSE);
			CreateStatic(hWnd, L"Diffuse:", rightX, topY + 27 * 2, 50, 22);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", rightX + 55, topY + 27 * 2, 100, 22, TRUE);
			CreateStatic(hWnd, L"Tile Base:", rightX, topY + 27 * 3, 50, 22);
			data->hWndTileBase = CreateEdit(hWnd, L"0", rightX + 55, topY + 27 * 3, 100, 22, TRUE);
			data->hWndAlignmentCheckbox = CreateCheckbox(hWnd, L"Align Size:", rightX, topY + 27 * 4, 75, 22, TRUE);
			data->hWndAlignment = CreateEdit(hWnd, L"32", rightX + 75, topY + 27 * 4, 80, 22, TRUE);
			EnableWindow(data->hWndDiffuse, FALSE);

			LPCWSTR formatNames[] = {
				L"NITRO-System",     // NCLR, NCGR, NSCR
				L"NITRO-CHARACTER",  // NCL , NCG , NSC
				L"IRIS-CHARACTER",   // ICL , ICG , ISC
				L"AGB-CHARACTER",    // ACL , ACG , ASC
				L"iMageStudio 5",    // 5BG (combo)
				L"Hudson",
				L"Hudson 2",
				L"Raw",
				L"Raw Compressed"
			};
			CreateStatic(hWnd, L"Format:", rightX, middleY, 50, 22);
			data->hWndFormatDropdown = CreateCombobox(hWnd, formatNames, sizeof(formatNames) / sizeof(*formatNames), rightX + 55, middleY, 150, 22, 0);

			NpCreateBalanceInput(&data->balance, hWnd, 10, bottomY - 18, 10 + 2 * boxWidth);

			//not actually buttons ;)
			CreateGroupbox(hWnd, L"Palette", 10, 42, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Graphics", 10 + boxWidth + 10, 42, boxWidth, boxHeight);
			CreateGroupbox(hWnd, L"Char compression", 10, 42 + boxHeight + 10, boxWidth, boxHeight2);
			CreateGroupbox(hWnd, L"Output", 10 + boxWidth + 10, 42 + boxHeight + 10, boxWidth, boxHeight2);
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

					createData->nclr = (NCLR *) calloc(1, sizeof(NCLR));
					createData->ncgr = (NCGR *) calloc(1, sizeof(NCGR));
					createData->nscr = (NSCR *) calloc(1, sizeof(NSCR));

					//global setting
					BgGenerateParameters params;
					params.fmt = SendMessage(data->hWndFormatDropdown, CB_GETCURSEL, 0, 0);
					NpGetBalanceSetting(&data->balance, &params.balance);

					//dither setting
					params.dither.dither = GetCheckboxChecked(data->nscrCreateDither);
					params.dither.diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;

					//palette region
					params.compressPalette = GetCheckboxChecked(data->hWndRowLimit);
					params.color0Mode = SendMessage(data->hWndColor0Setting, CB_GETCURSEL, 0, 0);
					params.paletteRegion.base = GetEditNumber(data->hWndPaletteInput);
					params.paletteRegion.count = GetEditNumber(data->hWndPalettesInput);
					params.paletteRegion.offset = GetEditNumber(data->hWndPaletteOffset);
					params.paletteRegion.length = GetEditNumber(data->hWndPaletteSize);

					//character setting
					params.bgType = SendMessage(data->nscrCreateDropdown, CB_GETCURSEL, 0, 0);
					params.characterSetting.base = GetEditNumber(data->hWndTileBase);
					params.characterSetting.alignment = doAlign ? GetEditNumber(data->hWndAlignment) : 1;
					params.characterSetting.compress = GetCheckboxChecked(data->hWndMergeTiles);
					params.characterSetting.nMax = GetEditNumber(data->hWndMaxChars);

					memcpy(&createData->genParams, &params, sizeof(params));
					threadedNscrCreate(progressData, createData, bbits, width, height, &params);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					DoModalEx(hWndProgress, FALSE);
				} else if (hWndControl == data->hWndMergeTiles) {
					//enable/disable max chars field
					int state = GetCheckboxChecked(hWndControl);
					EnableWindow(data->hWndMaxChars, state);
				} else if (hWndControl == data->hWndAlignmentCheckbox) {
					//enable/disable alignment amount field
					int state = GetCheckboxChecked(hWndControl);
					EnableWindow(data->hWndAlignment, state);
				} else if (hWndControl == data->nscrCreateDither) {
					//enable/disable diffusion amount field
					int state = GetCheckboxChecked(hWndControl);
					EnableWindow(data->hWndDiffuse, state);
				}
			} else if (HIWORD(wParam) == CBN_SELCHANGE) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->nscrCreateDropdown) {
					int index = SendMessage(hWndControl, CB_GETCURSEL, 0, 0);

					//bitmap mode: character compression is unavailable
					EnableWindow(data->hWndMaxChars, index != BGGEN_BGTYPE_BITMAP && GetCheckboxChecked(data->hWndMergeTiles));
					EnableWindow(data->hWndMergeTiles, index != BGGEN_BGTYPE_BITMAP);

					//certain formats may only use one palette.
					switch (index) {
						case BGGEN_BGTYPE_TEXT_16x16:
						case BGGEN_BGTYPE_AFFINEEXT_256x16:
							EnableWindow(data->hWndPalettesInput, TRUE);
							EnableWindow(data->hWndPaletteInput, TRUE);
							break;
						case BGGEN_BGTYPE_TEXT_256x1:
						case BGGEN_BGTYPE_AFFINE_256x1:
						case BGGEN_BGTYPE_BITMAP:
							//only one palette permitted, disable input
							EnableWindow(data->hWndPalettesInput, FALSE);
							EnableWindow(data->hWndPaletteInput, FALSE);
							SetEditNumber(data->hWndPalettesInput, 1);
							SetEditNumber(data->hWndPaletteInput, 0);
							break;
					}

					//max palette size per BG type
					switch (index) {
						case BGGEN_BGTYPE_TEXT_16x16:
							SetEditNumber(data->hWndPaletteSize, 16);
							break;
						case BGGEN_BGTYPE_TEXT_256x1:
						case BGGEN_BGTYPE_AFFINE_256x1:
						case BGGEN_BGTYPE_AFFINEEXT_256x16:
						case BGGEN_BGTYPE_BITMAP:
							SetEditNumber(data->hWndPaletteSize, 256);
							break;
					}

					//max character count per format
					switch (index) {
						case BGGEN_BGTYPE_TEXT_16x16:
						case BGGEN_BGTYPE_TEXT_256x1:
						case BGGEN_BGTYPE_AFFINEEXT_256x16:
							SetEditNumber(data->hWndMaxChars, 1024);
							break;
						case BGGEN_BGTYPE_AFFINE_256x1:
							SetEditNumber(data->hWndMaxChars, 256);
							break;
					}

					//tile base and alignment setting validity
					switch (index) {
						case BGGEN_BGTYPE_BITMAP:
							EnableWindow(data->hWndTileBase, FALSE);
							EnableWindow(data->hWndAlignmentCheckbox, FALSE);
							EnableWindow(data->hWndAlignment, FALSE);
							break;
						case BGGEN_BGTYPE_TEXT_16x16:
						case BGGEN_BGTYPE_TEXT_256x1:
						case BGGEN_BGTYPE_AFFINE_256x1:
						case BGGEN_BGTYPE_AFFINEEXT_256x16:
							EnableWindow(data->hWndTileBase, TRUE);
							EnableWindow(data->hWndAlignmentCheckbox, TRUE);
							EnableWindow(data->hWndAlignment, TRUE);
							break;
					}
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

					HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
					EnableWindow(hWndMain, TRUE);
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
			HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			EnableWindow(hWndMain, TRUE);
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
			
			HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			EnableWindow(hWndParent, FALSE);
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
				EnableWindow(data->hWndNtfiInput, needsNtfi);
				EnableWindow(data->hWndNtfiBrowseButton, needsNtfi);

				//only direct doesn't need and NTFP.
				int needsNtfp = fmt != CT_DIRECT;
				EnableWindow(data->hWndNtfpInput, needsNtfp);
				EnableWindow(data->hWndNtfpBrowseButton, needsNtfp);

				//update
				InvalidateRect(hWnd, NULL, FALSE);
			} else if (hWndControl == data->hWndConvertButton) {
				WCHAR src[MAX_PATH + 1];
				int width = GetEditNumber(data->hWndWidthInput);
				int format = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0) + 1;

				int bppArray[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
				int bpp = bppArray[format];

				int ntftSize = 0, ntfpSize = 0, ntfiSize = 0;
				BYTE *ntft = NULL, *ntfp = NULL, *ntfi = NULL;
					
				//read files
				DWORD dwSizeHigh, dwRead;
				char palName[17] = { 0 };
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
				texture.texels.cmp = (uint16_t *) ntfi;
				texture.texels.texImageParam = (format << 26) | ((ilog2(width) - 3) << 20) | ((ilog2(height) - 3) << 23);
				texture.texels.height = height;
				texture.palette.name = calloc(strlen(palName) + 1, 1);
				memcpy(texture.palette.name, palName, strlen(palName));

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

			LPCWSTR *formats = ObjGetFormatNamesByType(editorData->file->type);
			formats++; //skip invalid
			while (*formats != NULL) {
				SendMessage(hWndFormatCombobox, CB_ADDSTRING, wcslen(*formats), (LPARAM) *formats);
				formats++;
			}
			SendMessage(hWndFormatCombobox, CB_SETCURSEL, editorData->file->format - 1, 0);
			LPCWSTR *compressions = g_ObjCompressionNames;
			while (*compressions != NULL) {
				SendMessage(hWndCompressionCombobox, CB_ADDSTRING, wcslen(*compressions), (LPARAM) *compressions);
				compressions++;
			}
			SendMessage(hWndCompressionCombobox, CB_SETCURSEL, editorData->file->compression, 0);

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
				EDITOR_DATA *editorData = (EDITOR_DATA *) EditorGetData(hWnd);
				editorData->file->format = fmt;
				editorData->file->compression = comp;

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

					NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
					PalInit(nclr, palFormat);
					nclr->colors = (COLOR *) calloc(256, sizeof(COLOR));
					nclr->nColors = 256;
					nclr->nBits = nBits;
					
					NCGR *ncgr = (NCGR *) calloc(1, sizeof(NCGR));
					ChrInit(ncgr, charFormat);
					ncgr->header.compression = compression;
					ncgr->nBits = nBits;
					ncgr->mappingMode = mapping;
					ncgr->tilesX = 32;
					ncgr->tilesY = height;
					ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
					ncgr->tiles = (unsigned char **) calloc(ncgr->nTiles, sizeof(unsigned char *));
					ncgr->attr = (unsigned char *) calloc(ncgr->nTiles, 1);
					for (int i = 0; i < ncgr->nTiles; i++) {
						ncgr->tiles[i] = (BYTE *) calloc(64, 1);
					}

					//link objects
					ObjLinkObjects(&nclr->header, &ncgr->header);

					if (nitroPaintStruct->hWndNclrViewer != NULL) DestroyChild(nitroPaintStruct->hWndNclrViewer);
					if (nitroPaintStruct->hWndNcgrViewer != NULL) DestroyChild(nitroPaintStruct->hWndNcgrViewer);
					nitroPaintStruct->hWndNclrViewer = NULL;
					nitroPaintStruct->hWndNcgrViewer = NULL;
					nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, hWndMdi, nclr);
					nitroPaintStruct->hWndNcgrViewer = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, hWndMdi, ncgr);

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
			StList scrEditors;
			StListCreateInline(&scrEditors, NSCRVIEWERDATA *, NULL);
			EditorGetAllByType(hWndMain, FILE_TYPE_SCREEN, &scrEditors);

			NSCRVIEWERDATA *nscrViewerData = NULL;
			if (scrEditors.length > 0) nscrViewerData = *(NSCRVIEWERDATA **) StListGetPtr(&scrEditors, 0);
			StListFree(&scrEditors);

			if (nscrViewerData != NULL) {
				//has open screen editor, duplicate its setting
				NSCR *nscr = nscrViewerData->nscr;

				if (nscr->fmt == SCREENFORMAT_AFFINE) defFmt = 2;
				else if (nscr->colorMode == SCREENCOLORMODE_16x16) defFmt = 0;
				else if (nscr->colorMode == SCREENCOLORMODE_256x1) defFmt = 1;
				else if (nscr->colorMode == SCREENCOLORMODE_256x16) defFmt = 3;
			} else {
				//no open screen editor, search for open character editor
				HWND hWndNcgrViewer = nitroPaintStruct->hWndNcgrViewer;
				if (hWndNcgrViewer != NULL) {
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

				NSCR *nscr = (NSCR *) calloc(1, sizeof(NSCR));
				ScrInit(nscr, NSCR_TYPE_NSCR);
				nscr->fmt = format;
				nscr->colorMode = colorMode;
				nscr->tilesX = tilesX;
				nscr->tilesY = tilesY;
				nscr->dataSize = tilesX * tilesY * sizeof(uint16_t);
				nscr->data = (uint16_t *) calloc(tilesX * tilesY, sizeof(uint16_t));
				CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, nitroPaintStruct->hWndMdi, nscr);

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
				NSCR *nscr = nscrViewerData->nscr;
				int tilesX = nscr->tilesX;
				int tilesY = nscr->tilesY;

				int newTilesX = tilesX / x;
				int newTilesY = tilesY / y;
				for (int i = 0; i < y; i++) {
					for (int j = 0; j < x; j++) {
						NSCR *newNscr = (NSCR *) calloc(1, sizeof(NSCR));
						ScrInit(newNscr, nscr->header.format);
						newNscr->fmt = nscr->fmt;
						newNscr->colorMode = nscr->colorMode;
						newNscr->clearValue = nscr->clearValue;
						newNscr->gridWidth = nscr->gridWidth;
						newNscr->gridHeight = nscr->gridHeight;
						newNscr->nHighestIndex = nscr->nHighestIndex;
						newNscr->tilesX = newTilesX;
						newNscr->tilesY = newTilesY;
						newNscr->dataSize = newTilesX * newTilesY * sizeof(uint16_t);

						newNscr->data = (uint16_t *) calloc(newTilesX * newTilesY, sizeof(uint16_t));
						for (int tileY = 0; tileY < newTilesY; tileY++) {
							for (int tileX = 0; tileX < newTilesX; tileX++) {
								uint16_t src = nscr->data[tileX + j * newTilesX + (tileY + i * newTilesY) * tilesX];
								newNscr->data[tileX + tileY * newTilesX] = src;
							}
						}

						HWND h2 = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 50, nitroPaintStruct->hWndMdi, newNscr);
						NscrViewerSetTileBase(h2, nscrViewerData->tileBase);
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
				unsigned int foreWidth, foreHeight, backWidth, backHeight, blendWidth, blendHeight;
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
				for (unsigned int y = 0; y < blendHeight; y++) {
					for (unsigned int x = 0; x < blendWidth; x++) {
						COLOR32 blendC = blend[x + y * blendWidth];
						COLOR32 bgC = background[x + y * backWidth];
						if (blendC == bgC) {
							blend[x + y * blendWidth] = 0;
						}
					}
				}

				//if we're in tiled mode, write background to transparent pixels in occupied tiles
				if (isTiled) {
					unsigned int tilesX = blendWidth / 8, tilesY = blendHeight / 8;
					for (unsigned int tileY = 0; tileY < tilesY; tileY++) {
						for (unsigned int tileX = 0; tileX < tilesX; tileX++) {

							//find transparent pixel in the tile
							int hasTransparent = 0, hasOpaque = 0;
							for (unsigned int i = 0; i < 64; i++) {
								unsigned int x = i % 8;
								unsigned int y = i / 8;
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
								for (unsigned int i = 0; i < 64; i++) {
									unsigned int x = i % 8;
									unsigned int y = i / 8;
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
	StList editors;
} LINKEDITDATA;

LRESULT CALLBACK LinkEditWndPRoc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	LINKEDITDATA *data = (LINKEDITDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case WM_CREATE:
		{
			data = (LINKEDITDATA *) calloc(1, sizeof(LINKEDITDATA));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			CreateStatic(hWnd, L"Format:", 10, 10, 50, 22);
			data->hWndFormat = CreateCombobox(hWnd, gComboFormats + 1, COMBO2D_TYPE_MAX - 1, 70, 10, 300, 100, COMBO2D_TYPE_5BG - 1);
			CreateStatic(hWnd, L"Objects:", 10, 37, 50, 22);
			data->hWndObjects = CreateCheckedListView(hWnd, 70, 37, 300, 200);
			data->hWndOK = CreateButton(hWnd, L"OK", 170, 252, 100, 22, TRUE);
			data->combo = NULL;

			HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			StListCreateInline(&data->editors, EDITOR_DATA *, NULL);
			EditorGetAllByType(hWndMain, FILE_TYPE_INVALID, &data->editors);

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
			for (size_t i = 0; i < data->editors.length; i++) {
				int remove = 0;

				OBJECT_HEADER *obj2 = (*(EDITOR_DATA **) StListGetPtr(&data->editors, i))->file;
				if (obj2 == NULL) {
					remove = 1;
				} else if (combo != NULL) {
					if (obj2->combo != NULL && obj2->combo != combo) remove = 1;
				} else {
					if (obj2->combo != NULL) remove = 1;
				}

				//remove
				if (remove) {
					StListRemove(&data->editors, i);
					i--;
				}
			}
			
			for (size_t i = 0; i < data->editors.length; i++) {
				WCHAR buf[MAX_PATH];
				EDITOR_DATA *ed = *(EDITOR_DATA **) StListGetPtr(&data->editors, i);

				SendMessage(ed->hWnd, WM_GETTEXT, (WPARAM) MAX_PATH, (LPARAM) buf);
				AddCheckedListViewItem(data->hWndObjects, buf, i, (hWndEditor == ed->hWnd) || (combo != NULL && ed->file->combo == combo));
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
						for (size_t i = 0; i < data->editors.length; i++) {
							if (CheckedListViewIsChecked(data->hWndObjects, i)) {
								combo2dLink(combo, (*(EDITOR_DATA **) StListGetPtr(&data->editors, i))->file);
							}
						}
					} else {
						data->combo->header.format = fmt;

						//first, add any links we don't have, then remove the removed ones.
						for (size_t i = 0; i < data->editors.length; i++) {
							OBJECT_HEADER *obj = (*(EDITOR_DATA **) StListGetPtr(&data->editors, i))->file;

							if (CheckedListViewIsChecked(data->hWndObjects, i) && obj->combo == NULL) {
								combo2dLink(data->combo, obj);
							}
						}

						for (size_t i = 0; i < data->editors.length; i++) {
							OBJECT_HEADER *obj = (*(EDITOR_DATA **) StListGetPtr(&data->editors, i))->file;

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
				StListFree(&data->editors);
				free(data);
				SetWindowLongPtr(hWnd, 0, 0);
			}
			break;
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

typedef struct NEWPALETTEDATA_ {
	HWND hWndPaletteDepth;
	HWND hWndPaletteCount;
	HWND hWndOK;
} NEWPALETTEDATA;

static LRESULT CALLBACK NewPaletteWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NEWPALETTEDATA *data = (NEWPALETTEDATA *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			data = (NEWPALETTEDATA *) calloc(1, sizeof(NEWPALETTEDATA));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			LPCWSTR depthOptions[] = { L"4 bit", L"8 bit" };
			LPCWSTR paletteCounts[] = { L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"10", L"11", L"12", L"13", L"14", L"15", L"16" };

			CreateStatic(hWnd, L"Palette Depth:", 10, 10, 75, 22);
			CreateStatic(hWnd, L"Palette Count:", 10, 37, 75, 22);
			data->hWndPaletteDepth = CreateCombobox(hWnd, depthOptions, 2, 95, 10, 100, 100, 0);
			data->hWndPaletteCount = CreateCombobox(hWnd, paletteCounts, 16, 95, 37, 100, 100, 15);
			data->hWndOK = CreateButton(hWnd, L"Create", 95, 64, 100, 22, TRUE);

			SetWindowSize(hWnd, 205, 96);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
			if ((hWndControl == data->hWndOK || LOWORD(wParam) == IDOK) && notif == BN_CLICKED) {
				int depthSel = SendMessage(data->hWndPaletteDepth, CB_GETCURSEL, 0, 0);
				int countSel = SendMessage(data->hWndPaletteCount, CB_GETCURSEL, 0, 0) + 1;

				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);

				NCLR *nclr = (NCLR *) calloc(1, sizeof(NCLR));
				PalInit(nclr, NCLR_TYPE_NCLR);
				nclr->nBits = depthSel ? 8 : 4;
				nclr->nColors = countSel << nclr->nBits;
				nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
				nclr->extPalette = (depthSel && countSel > 1);
				nitroPaintStruct->hWndNclrViewer = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, nitroPaintStruct->hWndMdi, nclr);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if (LOWORD(wParam) == IDCANCEL) {
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
	return DefWindowProc(hWnd, msg, wParam, lParam);
}


typedef struct IndexImageData_ {
	HWND hWnd;
	HWND hWndFromFile;
	HWND hWndFromClip;

	HWND hWnd15bit;
	HWND hWndDither;
	HWND hWndDiffuse;
	HWND hWndColors;
	HWND hWndAlphaMode;

	HWND hWndUseFixedPalette;
	HWND hWndFixedPalettePath;
	HWND hWndBrowseFixedPalette;

	NpBalanceControl balance;

	HWND hWndPreview1;
	HWND hWndPreview2;

	HWND hWndSaveFile;
	HWND hWndSaveClip;
	HWND hWndRun;

	COLOR32 *px;
	unsigned int width;
	unsigned int height;

	unsigned int nUsedPltt;
	COLOR32 pltt[256];
	COLOR32 *reduced;
	int *indices;

	FrameBuffer fbSource;
	FrameBuffer fbReduced;
} RedGuiData;

static void RedGuiProcessReduction(RedGuiData *data) {
	//get fixed palette
	COLOR32 *fixedPalette = NULL;
	unsigned int fixedPaletteSize = 0;
	if (GetCheckboxChecked(data->hWndUseFixedPalette)) {
		WCHAR buf[MAX_PATH + 1];
		SendMessage(data->hWndFixedPalettePath, WM_GETTEXT, MAX_PATH, (LPARAM) buf);

		unsigned int fileSize = 0;
		unsigned char *filebuf = ObjReadWholeFile(buf, &fileSize);

		if (filebuf != NULL) {
			fixedPalette = ActRead(filebuf, fileSize, &fixedPaletteSize);
			free(filebuf);
		}
		
		if (fixedPalette == NULL) {
			MessageBox(data->hWnd, L"The fixed palette file is invalid or could not be read.", L"Error", MB_ICONERROR);
		}
	}

	RxBalanceSetting balance;
	unsigned int nColors = GetEditNumber(data->hWndColors);
	NpGetBalanceSetting(&data->balance, &balance);

	RxFlag flag = RX_FLAG_NO_PRESERVE_ALPHA | RX_FLAG_SORT_ONLY_USED;
	if (!GetCheckboxChecked(data->hWnd15bit)) flag |= RX_FLAG_NO_MASK_BITS;

	unsigned int plttOffs = 0;
	switch (SendMessage(data->hWndAlphaMode, CB_GETCURSEL, 0, 0)) {
		case 0: // Mode=None
			flag |= RX_FLAG_ALPHA_MODE_NONE;
			plttOffs = 0; // no offset
			break;
		case 1: // Mode=Color0
			flag |= RX_FLAG_ALPHA_MODE_RESERVE;
			plttOffs = 1; // offset 1st color
			break;
		case 2: // Mode=Palette
			flag |= RX_FLAG_ALPHA_MODE_PALETTE;
			plttOffs = 0; // no offset
			break;
	}

	unsigned int nColUse;
	if (fixedPalette == NULL) {
		//create a color palette
		RxCreatePaletteEx(data->px, data->width, data->height,
			data->pltt + plttOffs, nColors - plttOffs, balance.balance, balance.colorBalance, balance.enhanceColors, flag, &nColUse);
		if ((flag & RX_FLAG_ALPHA_MODE_MASK) == RX_FLAG_ALPHA_MODE_RESERVE) {
			data->pltt[0] = 0; // transparent
		}
	} else {
		//use the fixed palette
		nColUse = fixedPaletteSize;
		memcpy(data->pltt, fixedPalette, fixedPaletteSize * sizeof(COLOR32));
	}
	data->nUsedPltt = plttOffs + nColUse;

	//copy bits
	if (data->reduced != NULL) free(data->reduced);
	if (data->indices != NULL) free(data->indices);
	data->reduced = (COLOR32 *) calloc(data->width * data->height, sizeof(COLOR32));
	data->indices = (int *) calloc(data->width * data->height, sizeof(int));
	memcpy(data->reduced, data->px, data->width * data->height * sizeof(COLOR32));

	//reduce
	float diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;
	if (!GetCheckboxChecked(data->hWndDither)) diffuse = 0.0f;
	RxReduceImageEx(data->reduced, data->indices, data->width, data->height, data->pltt, nColUse + plttOffs,
		flag, diffuse, balance.balance, balance.colorBalance, balance.enhanceColors);

	if (fixedPalette != NULL) free(fixedPalette);
	InvalidateRect(data->hWndPreview2, NULL, FALSE);
}

static void RedGuiUpdateScrollbars(RedGuiData *data) {
	SCROLLINFO infoH = { 0 }, infoV = { 0 };
	infoH.cbSize = infoV.cbSize = sizeof(SCROLLINFO);
	infoH.nMin = infoV.nMin = 0;
	infoH.nMax = data->width;
	infoV.nMax = data->height;
	infoH.fMask = infoV.fMask = SIF_RANGE;

	SetScrollInfo(data->hWndPreview1, SB_HORZ, &infoH, TRUE);
	SetScrollInfo(data->hWndPreview2, SB_HORZ, &infoH, TRUE);
	SetScrollInfo(data->hWndPreview1, SB_VERT, &infoV, TRUE);
	SetScrollInfo(data->hWndPreview2, SB_VERT, &infoV, TRUE);
	RedrawWindow(data->hWndPreview1, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
	RedrawWindow(data->hWndPreview2, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
	UpdateScrollbarVisibility(data->hWndPreview1);
	UpdateScrollbarVisibility(data->hWndPreview2);

	RECT rcClient;
	GetClientRect(data->hWndPreview1, &rcClient);
	SendMessage(data->hWndPreview1, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
	GetClientRect(data->hWndPreview2, &rcClient);
	SendMessage(data->hWndPreview2, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
}

static void RedGuiSetSourceImage(RedGuiData *data, COLOR32 *px, unsigned int width, unsigned int height) {
	//set new buffer
	if (data->px != NULL) free(data->px);
	data->px = px;
	data->width = width;
	data->height = height;
	InvalidateRect(data->hWndPreview1, NULL, FALSE);

	//set scroll
	RedGuiUpdateScrollbars(data);

	//reduction
	RedGuiProcessReduction(data);
}

static LRESULT CALLBACK RedGuiIndexImageWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	RedGuiData *data = (RedGuiData *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			data = (RedGuiData *) calloc(1, sizeof(RedGuiData));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
			data->hWnd = hWnd;
			int boxWidth = 430;
			int boxHeight1 = 105;
			int leftX = 10 + 10;
			int rightX = leftX + boxWidth + 10;
			int bottomY = 10 + 18;

			LPCWSTR modes[] = { L"None", L"Color 0", L"Palette" };

			//Input
			data->hWndFromFile = CreateButton(hWnd, L"From File", leftX, bottomY, 150, 22, FALSE);
			data->hWndFromClip = CreateButton(hWnd, L"From Clipboard", leftX + 155, bottomY, 150, 22, FALSE);
			data->hWnd15bit = CreateCheckbox(hWnd, L"15-bit Color", leftX + 315, bottomY, 100, 22, TRUE);
			data->hWndDither = CreateCheckbox(hWnd, L"Dither:", leftX, bottomY + 27, 75, 22, FALSE);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", leftX + 75, bottomY + 27, 50, 22, TRUE);
			CreateStatic(hWnd, L"Colors:", leftX, bottomY + 54, 75, 22);
			data->hWndColors = CreateEdit(hWnd, L"256", leftX + 75, bottomY + 54, 50, 22, TRUE);
			CreateStatic(hWnd, L"Transparency:", leftX + 135, bottomY + 27, 75, 22);
			data->hWndAlphaMode = CreateCombobox(hWnd, modes, sizeof(modes) / sizeof(modes[0]), leftX + 210, bottomY + 27, 75, 22, 0);

			data->hWndUseFixedPalette = CreateCheckbox(hWnd, L"Fixed Palette", leftX + 135, bottomY + 54, 80, 22, FALSE);
			data->hWndFixedPalettePath = CreateEdit(hWnd, L"", leftX + 135 + 85, bottomY + 54, 100, 22, FALSE);
			data->hWndBrowseFixedPalette = CreateButton(hWnd, L"...", leftX + 135 + 85 + 100, bottomY + 54, 25, 22, FALSE);
			EnableWindow(data->hWndFixedPalettePath, FALSE);
			EnableWindow(data->hWndBrowseFixedPalette, FALSE);

			//Balance
			NpCreateBalanceInput(&data->balance, hWnd, 10 + boxWidth + 10, 10, boxWidth);

			//command
			data->hWndSaveClip = CreateButton(hWnd, L"Save Clipboard", (30 + 2 * boxWidth) / 2 - 100 - 210, bottomY + boxHeight1 + 10, 200, 22, FALSE);
			data->hWndRun = CreateButton(hWnd, L"Run", (30 + 2 * boxWidth) / 2 - 100, bottomY + boxHeight1 + 10, 200, 22, TRUE);
			data->hWndSaveFile = CreateButton(hWnd, L"Save File", (30 + 2 * boxWidth) / 2 + 110, bottomY + boxHeight1 + 10, 200, 22, FALSE);

			CreateGroupbox(hWnd, L"Image", 10, 10, boxWidth, boxHeight1);

			data->hWndPreview1 = CreateWindowEx(WS_EX_CLIENTEDGE, L"IndexImagePreview", L"Source",
				WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_CENTER | WS_HSCROLL | WS_VSCROLL,
				10, bottomY + boxHeight1 + 10 + 27, 256, 256, hWnd, NULL, NULL, NULL);
			data->hWndPreview2 = CreateWindowEx(WS_EX_CLIENTEDGE, L"IndexImagePreview", L"Reduced",
				WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_CENTER | WS_HSCROLL | WS_VSCROLL,
				10 + 256, bottomY + boxHeight1 + 10 + 27, 256, 256, hWnd, NULL, NULL, NULL);

			SetGUIFont(hWnd);

			//test
			COLOR32 *px = (COLOR32 *) calloc(32 * 32, sizeof(COLOR32));
			int width = 32;
			int height = 32;
			for (int y = 0; y < 32; y++) {
				for (int x = 0; x < 32; x++) {
					px[x + y * 32] = ((y * 255 / 31) << 24) | ((x * 255) / 31);
				}
			}
			RedGuiSetSourceImage(data, px, width, height);
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			float dpiScale = GetDpiScale();
			int bottomY = 10 + 18;
			int boxHeight1 = 105;
			int boxY = bottomY + boxHeight1 + 10 + 27;
			int prevHeight = rcClient.bottom - UI_SCALE_COORD(boxY + 10, dpiScale);
			int prevWidth = (rcClient.right - UI_SCALE_COORD(20, dpiScale)) / 2;


			MoveWindow(data->hWndPreview1, UI_SCALE_COORD(10, dpiScale), UI_SCALE_COORD(boxY, dpiScale),
				prevWidth, prevHeight, TRUE);
			MoveWindow(data->hWndPreview2, UI_SCALE_COORD(10, dpiScale) + prevWidth, UI_SCALE_COORD(boxY, dpiScale),
				rcClient.right - UI_SCALE_COORD(20, dpiScale) - prevWidth, prevHeight, TRUE);

			RedGuiUpdateScrollbars(data);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndCtl = (HWND) lParam;
			int notif = HIWORD(wParam), idCtl = LOWORD(wParam);
			if (hWndCtl == data->hWndFromClip && notif == BN_CLICKED) {
				//from clipboard
				if (OpenClipboard(hWnd)) {

					int width, height;
					COLOR32 *px = GetClipboardBitmap(&width, &height, NULL, NULL, NULL);
					if (px != NULL) {
						RedGuiSetSourceImage(data, px, width, height);
					}

					CloseClipboard();
				}
			} else if (hWndCtl == data->hWndFromFile) {
				//from file
				LPWSTR path = openFileDialog(hWnd, L"Select Bitmap", L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0All Files\0*.*\0", L"");
				if (path == NULL) break;

				int width, height;
				COLOR32 *px = ImgRead(path, &width, &height);
				if (px != NULL) {
					RedGuiSetSourceImage(data, px, width, height);
				}

				free(path);
			} else if ((hWndCtl == data->hWndRun || idCtl == IDOK) && notif == BN_CLICKED) {
				//run reduction
				RedGuiProcessReduction(data);
			} else if (hWndCtl == data->hWndSaveFile && notif == BN_CLICKED) {
				//save file
				LPWSTR path = saveFileDialog(hWnd, L"Save image", L"PNG files (*.png)\0*.png\0All Files\0*.*\0", L"png");
				if (path == NULL) break;

				//write bitmap data indexed
				unsigned char *bits = (unsigned char *) calloc(data->width * data->height, sizeof(unsigned char));
				for (unsigned int i = 0; i < data->width * data->height; i++) {
					bits[i] = (unsigned char) data->indices[i];
				}
				ImgWriteIndexed(bits, data->width, data->height, data->pltt, data->nUsedPltt, path);
				free(bits);

				free(path);
			} else if (hWndCtl == data->hWndSaveClip && notif == BN_CLICKED) {
				//save clipboard
				if (OpenClipboard(hWnd)) {
					EmptyClipboard();

					unsigned char *bits = (unsigned char *) calloc(data->width * data->height, sizeof(unsigned char));
					for (unsigned int i = 0; i < data->width * data->height; i++) {
						bits[i] = (unsigned char) data->indices[i];
					}
					ClipCopyBitmapEx(bits, data->width, data->height, 1, data->pltt, data->nUsedPltt);
					free(bits);

					CloseClipboard();
				}
			} else if (hWndCtl == data->hWndUseFixedPalette && notif == BN_CLICKED) {
				int enabled = GetCheckboxChecked(data->hWndUseFixedPalette);
				EnableWindow(data->hWndFixedPalettePath, enabled);
				EnableWindow(data->hWndBrowseFixedPalette, enabled);
			} else if (hWndCtl == data->hWndBrowseFixedPalette && notif == BN_CLICKED) {
				LPWSTR path = openFileDialog(hWnd, L"Select a Color Palette", L"ACT Files (*.act)\0*.act\0All Files\0*.*\0", L"act");
				if (path == NULL) break;

				SendMessage(data->hWndFixedPalettePath, WM_SETTEXT, wcslen(path), (LPARAM) path);
				free(path);
			} else if (idCtl == IDCANCEL && notif == BN_CLICKED) {
				//exit
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}

		case WM_DESTROY:
			if (data != NULL) {
				if (data->px != NULL) free(data->px);
				if (data->reduced != NULL) free(data->reduced);
				if (data->indices != NULL) free(data->indices);
				FbDestroy(&data->fbSource);
				FbDestroy(&data->fbReduced);
				free(data);
				SetWindowLongPtr(hWnd, 0, (LONG_PTR) NULL);
			}
			break;

	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK RedGuiIndexImagePreviewProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	RedGuiData *data = (RedGuiData *) GetWindowLongPtr(hWndParent, 0);
	FRAMEDATA *frame = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
	
	int isSrc = 0;
	FrameBuffer *fb = NULL;
	if (data != NULL) {
		isSrc = (hWnd == data->hWndPreview1) || (data->hWndPreview1 == NULL);
		fb = isSrc ? &data->fbSource : &data->fbReduced;
	}

	if (data != NULL && frame != NULL) {
		frame->contentWidth = data->width;
		frame->contentHeight = data->height;
	}

	switch (msg) {
		case WM_CREATE:
		{
			FbCreate(fb, hWnd, 1, 1);
			frame = (FRAMEDATA *) calloc(1, sizeof(FRAMEDATA));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) frame);

			frame->contentWidth = data->width;
			frame->contentHeight = data->height;
			break;
		}
		case WM_SIZE:
		{
			if (frame != NULL) {
				UpdateScrollbarVisibility(hWnd);

				SCROLLINFO info;
				info.cbSize = sizeof(info);
				info.nMin = 0;
				info.nMax = frame->contentWidth;
				info.fMask = SIF_RANGE;
				SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

				info.nMax = frame->contentHeight;
				SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			}
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			int clientWidth = rcClient.right, clientHeight = rcClient.bottom;

			FbSetSize(fb, clientWidth, clientHeight);

			int scale = 1;
			int scrollX = 0, scrollY = 0;

			SCROLLINFO sifV = { 0 }, sifH = { 0 };
			sifV.cbSize = sifH.cbSize = sizeof(SCROLLINFO);
			sifV.fMask = sifH.fMask = SIF_POS;
			GetScrollInfo(hWnd, SB_HORZ, &sifH);
			GetScrollInfo(hWnd, SB_VERT, &sifV);
			scrollX = sifH.nPos, scrollY = sifV.nPos;

			const COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };

			for (int y = 0; y < clientHeight; y++) {
				for (int x = 0; x < clientWidth; x++) {
					unsigned int srcX = (x + scrollX) / scale, srcY = (y + scrollY) / scale;

					COLOR32 col = 0;
					if (srcX < data->width && srcY < data->height) {
						if (isSrc) col = data->px != NULL ? data->px[srcY * data->width + srcX] : 0;
						else col = data->reduced != NULL ? data->reduced[srcY * data->width + srcX] : 0;
					}

					unsigned int a = col >> 24;
					COLOR32 back = checker[((x ^ y) >> 3) & 1];
					if (a < 255) {
						unsigned int r = (((col >>  0) & 0xFF) * a + ((back >>  0) & 0xFF) * (255 - a)) / 255;
						unsigned int g = (((col >>  8) & 0xFF) * a + ((back >>  8) & 0xFF) * (255 - a)) / 255;
						unsigned int b = (((col >> 16) & 0xFF) * a + ((back >> 16) & 0xFF) * (255 - a)) / 255;
						col = r | (g << 8) | (b << 16);
					}

					fb->px[y * fb->width + x] = REVERSE(col);
				}
			}

			FbDraw(fb, hDC, 0, 0, clientWidth, clientHeight, 0, 0);

			EndPaint(hWnd, &ps);
			break;
		}
		case WM_HSCROLL:
		case WM_VSCROLL:
		{
			LRESULT r = DefChildProc(hWnd, msg, wParam, lParam);
			HWND hWndOther = isSrc ? data->hWndPreview2 : data->hWndPreview1;

			if (data != NULL && hWndOther != NULL) {
				SCROLLINFO sifV = { 0 }, sifH = { 0 };
				sifV.cbSize = sifH.cbSize = sizeof(SCROLLINFO);
				sifV.fMask = sifH.fMask = SIF_POS;
				GetScrollInfo(hWnd, SB_HORZ, &sifH);
				GetScrollInfo(hWnd, SB_VERT, &sifV);
				int scrollX = sifH.nPos, scrollY = sifV.nPos;

				SetScrollPos(hWndOther, SB_HORZ, scrollX, TRUE);
				SetScrollPos(hWndOther, SB_VERT, scrollY, TRUE);
				InvalidateRect(hWndOther, NULL, FALSE);
			}

			return r;
		}
		case WM_DESTROY:
		{
			free(frame);
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) NULL);
			break;
		}
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}


static void RegisterImageDialogClass(void) {
	RegisterGenericClass(L"ImageDialogClass", ImageDialogProc, sizeof(LPVOID));
}

static void RegisterFormatConversionClass(void) {
	RegisterGenericClass(L"ConvertFormatDialogClass", ConvertFormatDialogProc, 3 * sizeof(LPVOID));
}

static void RegisterSpriteSheetDialogClass(void) {
	RegisterGenericClass(L"SpriteSheetDialogClass", SpriteSheetDialogProc, sizeof(LPVOID));
}

static void RegisterScreenDialogClass(void) {
	RegisterGenericClass(L"NewScreenDialogClass", NewScreenDialogProc, sizeof(LPVOID));
}

static void RegisterScreenSplitDialogClass(void) {
	RegisterGenericClass(L"ScreenSplitDialogClass", ScreenSplitDialogProc, sizeof(LPVOID));
}

static void RegisterTextPromptClass(void) {
	RegisterGenericClass(L"TextPromptClass", TextInputWndProc, sizeof(LPVOID) * 5); //2 HWNDs, status, out info
}

static void RegisterAlphaBlendClass(void) {
	RegisterGenericClass(L"AlphaBlendClass", AlphaBlendWndProc, sizeof(LPVOID) * 6);
}

static void RegisterLinkEditClass(void) {
	RegisterGenericClass(L"LinkEditClass", LinkEditWndPRoc, sizeof(LPVOID));
}

static void RegisterNewPaletteClass(void) {
	RegisterGenericClass(L"NewPaletteClass", NewPaletteWndProc, sizeof(LPVOID));
}

static void RegisterIndexImageClass(void) {
	RegisterGenericClass(L"IndexImageClass", RedGuiIndexImageWndProc, sizeof(LPVOID));
	RegisterGenericClass(L"IndexImagePreview", RedGuiIndexImagePreviewProc, sizeof(LPVOID));
}

static BOOL NpCfgWriteInt(LPCWSTR section, LPCWSTR prop, int val) {
	WCHAR buf[32];
	wsprintfW(buf, L"%d", val);
	return WritePrivateProfileString(section, prop, buf, g_configPath);
}

static int NpCfgReadInt(LPCWSTR section, LPCWSTR prop, int def) {
	//read value from profile, if not the default then return
	int val0 = GetPrivateProfileInt(section, prop, def, g_configPath);
	if (val0 != def) return def;

	//try reading with a different default to catch a default setting
	int val1 = GetPrivateProfileInt(section, prop, def ^ 1, g_configPath);
	if (val1 == val0) return val1;

	//put the default value
	NpCfgWriteInt(section, prop, def);
	return def;
}

static LPWSTR NpCfgReadStr(LPCWSTR section, LPCWSTR prop, LPCWSTR def) {
	WCHAR *buf = (WCHAR *) calloc(MAX_PATH + 1, sizeof(WCHAR));
	if (buf == NULL) return NULL;

	GetPrivateProfileStringW(section, prop, def, buf, MAX_PATH + 1, g_configPath);
	return buf;
}

static void ReadConfiguration(void) {
	g_configuration.fullPaths              = NpCfgReadInt(L"NitroPaint", L"FullPaths",         0);
	g_configuration.renderTransparent      = NpCfgReadInt(L"NitroPaint", L"RenderTransparent", 0);
	g_configuration.dpiAware               = NpCfgReadInt(L"NitroPaint", L"DPIAware",          1);
	g_configuration.allowMultipleInstances = NpCfgReadInt(L"NitroPaint", L"AllowMultiple",     0);
	g_configuration.backgroundPath         = NpCfgReadStr(L"NitroPaint", L"Background",        L"");
	g_configuration.nclrViewerConfiguration.useDSColorPicker = NpCfgReadInt(L"NclrViewer", L"UseDSColorPicker", 1);
	g_configuration.ncgrViewerConfiguration.gridlines        = NpCfgReadInt(L"NcgrViewer", L"Gridlines",        1);
	g_configuration.nscrViewerConfiguration.gridlines        = NpCfgReadInt(L"NscrViewer", L"Gridlines",        0);

	//load background image
	if (g_configuration.backgroundPath[0] != L'\0') {
		unsigned int width = 0, height = 0;
		COLOR32 *bits = ImgRead(g_configuration.backgroundPath, &width, &height);
		if (bits != NULL) {
			ImgSwapRedBlue(bits, width, height);
			HBITMAP hbm = CreateBitmap(width, height, 1, 32, bits);
			g_configuration.hbrBackground = CreatePatternBrush(hbm);
			free(bits);
		}
	}
}

VOID SetConfigPath() {
	LPWSTR name = L"nitropaint.ini";
	g_configPath = calloc(MAX_PATH + 1, sizeof(WCHAR));
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

static void RegisterClasses(void) {
	RegisterNcgrViewerClass();
	RegisterNclrViewerClass();
	RegisterNscrViewerClass();
	RegisterNcerViewerClass();
	RegisterCreateDialogClass();
	RegisterNsbtxViewerClass();
	RegisterNftrViewerClass();
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
	RegisterNewPaletteClass();
	RegisterIndexImageClass();
	RegisterLytEditor();
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
	//fetch version
	(void) NpGetVersion();

	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	HACCEL hAccel = LoadAccelerators(hInstance, (LPCWSTR) IDR_ACCELERATOR1);
	CoInitialize(NULL);

	SetConfigPath();
	ReadConfiguration();
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
