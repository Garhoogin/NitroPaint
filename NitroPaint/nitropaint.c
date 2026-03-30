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
#include "mesgeditor.h"
#include "combo2d.h"

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

typedef struct PaletteSwapEntry_ {
	WCHAR path[MAX_PATH];
	COLOR32 *px;
	unsigned int width;
	unsigned int height;
} PaletteSwapEntry;

typedef struct PaletteSwapData_ {
	PaletteSwapEntry entries[RX_PALETTE_MAX_COUNT];
	unsigned int nEntries;

	HWND hWndCancel;
	HWND hWndOK;
	HWND hWndTextureFormat;
	HWND hWndC0xp;
	HWND hWndType;
	HWND hWndDither;
	HWND hWndDiffuse;
	HWND hWndFileLabels[RX_PALETTE_MAX_COUNT];
	HWND hWndPaletteNames[RX_PALETTE_MAX_COUNT];
	HWND hWndUpButtons[RX_PALETTE_MAX_COUNT];
	HWND hWndDownButtons[RX_PALETTE_MAX_COUNT];

	NpBalanceControl balance;
} PaletteSwapData;

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

NITROPAINTSTRUCT *NpGetData(HWND hWndMain) {
	return (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
}

static int NpGetPaletteFormatForPreset(void) {
	switch (g_configuration.preset) {
		default:
		case NP_PRESET_NITROSYSTEM    : return NCLR_TYPE_NCLR;
		case NP_PRESET_NITROCHARACTER : return NCLR_TYPE_NC;
		case NP_PRESET_IRIS_CHARACTER : return NCLR_TYPE_BIN;
		case NP_PRESET_AGB_CHARACTER  : return NCLR_TYPE_BIN;
		case NP_PRESET_IMAGESTUDIO    : return 0;              // combo
		case NP_PRESET_GRIT           : return 0;              // combo
		case NP_PRESET_RAW            : return NCLR_TYPE_BIN;
	}
}

static int NpGetCharacterFormatForPreset(void) {
	switch (g_configuration.preset) {
		default:
		case NP_PRESET_NITROSYSTEM    : return NCGR_TYPE_NCGR;
		case NP_PRESET_NITROCHARACTER : return NCGR_TYPE_NC;
		case NP_PRESET_IRIS_CHARACTER : return NCGR_TYPE_IC;
		case NP_PRESET_AGB_CHARACTER  : return NCGR_TYPE_AC;
		case NP_PRESET_IMAGESTUDIO    : return 0;              // combo
		case NP_PRESET_GRIT           : return 0;              // combo
		case NP_PRESET_RAW            : return NCGR_TYPE_BIN;
	}
}

static int NpGetScreenFormatForPreset(void) {
	switch (g_configuration.preset) {
		default:
		case NP_PRESET_NITROSYSTEM    : return NSCR_TYPE_NSCR;
		case NP_PRESET_NITROCHARACTER : return NSCR_TYPE_NC;
		case NP_PRESET_IRIS_CHARACTER : return NSCR_TYPE_IC;
		case NP_PRESET_AGB_CHARACTER  : return NSCR_TYPE_AC;
		case NP_PRESET_IMAGESTUDIO    : return 0;              // combo
		case NP_PRESET_GRIT           : return 0;              // combo
		case NP_PRESET_RAW            : return NSCR_TYPE_BIN;
	}
}

static int NpGetCellFormatForPreset(void) {
	switch (g_configuration.preset) {
		default:
		case NP_PRESET_NITROSYSTEM : return NCER_TYPE_NCER;
		//case NP_PRESET_NITROCHARACTER: // TODO
		//case NP_PRESET_IRIS_CHARACTER: // TODO
		//case NP_PRESET_AGB_CHARACTER: // TODO
		case NP_PRESET_GRIT        : return 0;                 // combo
		case NP_PRESET_RAW         : return NCER_TYPE_HUDSON;
	}
}

static int NpGetAnimFormatForPreset(void) {
	switch (g_configuration.preset) {
		default:
		case NP_PRESET_NITROSYSTEM : return NANR_TYPE_NANR;
		case NP_PRESET_GRIT        : return 0;               // combo
	}
}

static int NpGetComboFormatForPreset(void) {
	switch (g_configuration.preset) {
		default: return COMBO2D_TYPE_INVALID;
		case NP_PRESET_IMAGESTUDIO : return COMBO2D_TYPE_5BG;
		case NP_PRESET_GRIT        : return COMBO2D_TYPE_GRF_BG;
	}
}

static int NpGetTextureFormatForPreset(void) {
	switch (g_configuration.preset) {
		default:
		case NP_PRESET_NITROSYSTEM : return TEXTURE_TYPE_NNSTGA;
		case NP_PRESET_IMAGESTUDIO : return TEXTURE_TYPE_ISTUDIO;
		case NP_PRESET_GRIT        : return TEXTURE_TYPE_GRF;
	}
}

static HWND NpOpenObject(HWND hWnd, ObjHeader *object);
static HWND NpOpenObjectAtPath(HWND hWnd, ObjHeader *object, const wchar_t *path);

static void NpOpenCombo(HWND hWnd, COMBO2D *combo, const wchar_t *path) {
	for (unsigned int i = 0; i < combo->links.length; i++) {
		ObjHeader *object;
		StListGet(&combo->links, i, &object);

		if (path == NULL) {
			NpOpenObject(hWnd, object);
		} else {
			NpOpenObjectAtPath(hWnd, object, path);
		}
	}
}

static HWND NpOpenObject(HWND hWnd, ObjHeader *object) {
	NITROPAINTSTRUCT *data = NpGetData(hWnd);
	HWND h = NULL;

	switch (object->type) {
		case FILE_TYPE_PALETTE:
			//if there is already an NCLR open, close it.
			if (data->hWndNclrViewer) DestroyChild(data->hWndNclrViewer);
			h = CreateNclrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 257, data->hWndMdi, (NCLR *) object);
			data->hWndNclrViewer = h;
			if (data->hWndNcerViewer) InvalidateRect(data->hWndNcerViewer, NULL, FALSE);
			break;
		case FILE_TYPE_CHARACTER:
			//if there is already an NCGR open, close it.
			if (data->hWndNcgrViewer) DestroyChild(data->hWndNcgrViewer);
			h = CreateNcgrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, data->hWndMdi, (NCGR *) object);
			data->hWndNcgrViewer = h;
			InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
			break;
		case FILE_TYPE_SCREEN:
			h = CreateNscrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, data->hWndMdi, (NSCR *) object);
			break;
		case FILE_TYPE_CELL:
			if (data->hWndNcerViewer) DestroyChild(data->hWndNcerViewer);
			h = CreateNcerViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (NCER *) object);
			data->hWndNcerViewer = h;
			if (data->hWndNclrViewer) InvalidateRect(data->hWndNclrViewer, NULL, FALSE);
			break;
		case FILE_TYPE_NANR:
			h = CreateNanrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (NANR *) object);
			data->hWndNanrViewer = h;
			break;
		case FILE_TYPE_NSBTX:
			h = CreateNsbtxViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (TexArc *) object);
			break;
		case FILE_TYPE_FONT:
			h = CreateNftrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (NFTR *) object);
			break;
		case FILE_TYPE_BNLL:
			h = CreateBnllViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (BNLL *) object);
			break;
		case FILE_TYPE_BNCL:
			h = CreateBnclViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (BNCL *) object);
			break;
		case FILE_TYPE_BNBL:
			h = CreateBnblViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (BNBL *) object);
			break;
		case FILE_TYPE_TEXTURE:
			h = CreateTextureEditorImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (TextureObject *) object);
			break;

		case FILE_TYPE_NMCR:
			h = CreateNmcrViewerImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (NMCR *) object);
			data->hWndNmcrViewer = h;
			break;
		case FILE_TYPE_MESG:
			h = CreateMesgEditorImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi, (MesgFile *) object);
			break;

		case FILE_TYPE_COMBO2D:
		{
			//create an editor for each sub-object (recursive)
			COMBO2D *combo = (COMBO2D *) object;
			NpOpenCombo(hWnd, combo, NULL);
			break;
		}
	}
	return h;
}

static HWND NpOpenObjectAtPath(HWND hWndMain, ObjHeader *object, const wchar_t *path) {
	HWND h = NpOpenObject(hWndMain, object);
	if (h != NULL) EditorSetFile(h, path);

	return h;
}

static void NpEnsureComboObject(HWND hWndMain, ObjHeader *obj, int combofmt) {
	if (combofmt == COMBO2D_TYPE_INVALID) return;

	//first, search for another editor owning a combo file in the same format.
	StList editors;
	StListCreateInline(&editors, EDITOR_DATA *, NULL);
	EditorGetAllByType(hWndMain, FILE_TYPE_INVALID, &editors);

	COMBO2D *combo = NULL;
	for (size_t i = 0; i < editors.length; i++) {
		EDITOR_DATA *ed = *(EDITOR_DATA **) StListGetPtr(&editors, i);
		if (ed->file->combo == NULL) continue;

		COMBO2D *found = (COMBO2D *) ed->file->combo;
		if ((COMBO2D *) obj->combo == found) continue; // skip objects linked to this one already

		if (found->header.format == combofmt) {
			combo = found;
			break;
		}
	}

	//if the object is not linked to a combo, then we add it to one.
	if (obj->combo == NULL) {
		//if the combo was not found, create and initialize a new one.
		if (combo == NULL) {
			//create a combo and link it
			combo = (COMBO2D *) ObjAlloc(FILE_TYPE_COMBO2D, combofmt);
		}

		combo2dLink(combo, obj);
	} else {
		if (combo != NULL) {
			//otherwise, this object is already in a combo. Add it to the found one.
			COMBO2D *curr = (COMBO2D *) obj->combo;
			unsigned int nObj = curr->links.length;
			for (unsigned int i = 0; i < nObj; i++) {
				//move links (beware: combo is deallocated when all links are deleted!)
				ObjHeader *obj2 = *(ObjHeader **) StListGetPtr(&curr->links, 0);
				combo2dUnlink(curr, obj2);
				combo2dLink(combo, obj2);
			}
		}
	}

	StListFree(&editors);
}

static void CompressFileDialog(HWND hWndParent, const unsigned char *buf, unsigned int size) {
	HWND h = CreateWindow(L"CompressFileDialog", L"Compress File", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) size, (LPARAM) buf);
	DoModal(h);
}

static void DecompressFileDialog(HWND hWndParent, const unsigned char *buf, unsigned int size) {
	int compression = CxGetCompressionType(buf, size);
	if (compression == COMPRESSION_NONE) {
		MessageBox(hWndParent, L"The file is not of a recognized compression format.", L"Error", MB_ICONERROR);
		return;
	}

	unsigned int uncompSize;
	unsigned char *uncomp = CxDecompress(buf, size, compression, &uncompSize);

	//save
	LPWSTR path = saveFileDialog(hWndParent, L"Save File", L"All Files\0*.*\0", L"");
	if (path != NULL) {

		HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != NULL) {
			DWORD dwWritten;
			WriteFile(hFile, uncomp, uncompSize, &dwWritten, NULL);
			CloseHandle(hFile);
		}
	}

	free(uncomp);

}

VOID OpenFileByNameAs(HWND hWnd, LPCWSTR path) {
	HWND h = CreateWindow(L"OpenAsDialogClass", L"Open As", WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWnd, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) hWnd);
	DoModal(h);
}

void OpenFileByContent(HWND hWnd, const unsigned char *buffer, unsigned int size, const wchar_t *path, int compression, int type, int format) {
	if (type == FILE_TYPE_IMAGE) {
		//create texture editor
		NITROPAINTSTRUCT *np = NpGetData(hWnd);
		CreateTextureEditorFromUnconverted(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, np->hWndMdi, buffer, size, path);
	} else {
		//open the file
		ObjHeader *obj = NULL;
		int status = ObjReadBuffer(&obj, buffer, size, type, format, compression);
		if (!OBJ_SUCCEEDED(status)) return;

		//open in an editor
		if (obj->type == FILE_TYPE_COMBO2D) {
			NpOpenCombo(hWnd, (COMBO2D *) obj, path);
		} else {
			NpOpenObjectAtPath(hWnd, obj, path);
		}
	}
}

VOID OpenFileByName(HWND hWnd, LPCWSTR path) {
	NITROPAINTSTRUCT *data = NpGetData(hWnd);
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
		COMBO2D *combo = (COMBO2D *) ObjAlloc(FILE_TYPE_COMBO2D, COMBO2D_TYPE_DATAFILE);
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

		ObjHeader *nclr = NULL, *ncgr = NULL, *nscr = NULL;

		//read applicable sections
		if (pltRef != NULL) {
			ObjReadBuffer(&nclr, dfc->data + pltOffset, pltSize, FILE_TYPE_PALETTE, NCLR_TYPE_BIN, COMPRESSION_NONE);
			nclr->format = 0; // clear the format field
			combo2dLink(combo, nclr);
			free(pltRef);
		}
		if (chrRef != NULL) {
			ObjReadBuffer(&ncgr, dfc->data + chrOffset, chrSize, FILE_TYPE_CHARACTER, NCGR_TYPE_BIN, COMPRESSION_NONE);
			ncgr->format = 0; // clear the format field
			combo2dLink(combo, ncgr);
			free(chrRef);
		}
		if (scrRef != NULL) {
			ObjReadBuffer(&nscr, dfc->data + scrOffset, scrSize, FILE_TYPE_SCREEN, NSCR_TYPE_BIN, COMPRESSION_NONE);
			nscr->format = 0; // clear the format field
			combo2dLink(combo, nscr);
			free(scrRef);
		}

		//open combo
		NpOpenCombo(hWnd, combo, pathBuffer);

		free(pathBuffer);
		free(refName);
		goto cleanup;
	}

	//identify the kind of object
	int compression, format;
	int type = ObjIdentify(buffer, dwSize, path, FILE_TYPE_INVALID, &compression, &format);

	switch (type) {
		case FILE_TYPE_IMAGE:
			CreateImageDialog(hWnd, path);
			break;
		case FILE_TYPE_PALETTE:
		case FILE_TYPE_CHARACTER:
		case FILE_TYPE_SCREEN:
		case FILE_TYPE_CELL:
		case FILE_TYPE_NSBTX:
		case FILE_TYPE_NANR:
		case FILE_TYPE_FONT:
		case FILE_TYPE_BNLL:
		case FILE_TYPE_BNCL:
		case FILE_TYPE_BNBL:
		case FILE_TYPE_MESG:
		case FILE_TYPE_TEXTURE:
		case FILE_TYPE_NMCR:
		case FILE_TYPE_COMBO2D:
		{
			ObjHeader *obj = NULL;
			int status = ObjReadBuffer(&obj, buffer, dwSize, type, format, compression);
			if (OBJ_SUCCEEDED(status)) {
				if (obj->type == FILE_TYPE_COMBO2D) {
					NpOpenCombo(hWnd, (COMBO2D *) obj, path);
				} else {
					NpOpenObjectAtPath(hWnd, obj, path);
				}
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

static void NpCheckCurrentPreset(HWND hWndMain) {
	const unsigned short ids[] = {
		ID_PRESETS_NITROSYSTEM, ID_PRESETS_NITROCHARACTER, ID_PRESETS_GRF, ID_PRESETS_RAW,
		ID_PRESETS_ISIRISCHARACTER, ID_PRESETS_ISAGBCHARACTER, ID_PRESETS_IMAGESTUDIO
	};

	for (int i = 0; i <= NP_PRESET_MAX; i++) {
		int state = i == g_configuration.preset;
		CheckMenuItem(GetMenu(hWndMain), ids[i], state ? MF_CHECKED : MF_UNCHECKED);
	}
}

static void NpSetPreset(HWND hWndMain, int preset) {
	g_configuration.preset = preset;

	WCHAR buf[16];
	wsprintfW(buf, L"%d", preset);
	WritePrivateProfileStringW(L"NitroPaint", L"Preset", buf, g_configPath);

	NpCheckCurrentPreset(hWndMain);
}

static void MainZoomIn(HWND hWnd) {
	NITROPAINTSTRUCT *np = NpGetData(hWnd);
	HWND hWndChild = (HWND) SendMessage(np->hWndMdi, WM_MDIGETACTIVE, 0, 0);

	if (hWndChild != NULL) {
		SendMessage(hWndChild, NV_ZOOMIN, 0, 0);
	}
}

static void MainZoomOut(HWND hWnd) {
	NITROPAINTSTRUCT *np = NpGetData(hWnd);
	HWND hWndChild = (HWND) SendMessage(np->hWndMdi, WM_MDIGETACTIVE, 0, 0);

	if (hWndChild != NULL) {
		SendMessage(hWndChild, NV_ZOOMOUT, 0, 0);
	}
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
		case FILE_TYPE_TEXTURE:
		case FILE_TYPE_NSBTX:
		case FILE_TYPE_BNBL:
		case FILE_TYPE_BNCL:
		case FILE_TYPE_BNLL:
		case FILE_TYPE_MESG:
			return 11; // don't care
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
			NpCheckCurrentPreset(hWnd);
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
					case ID_FILE_OPENAS:
					{
						LPWSTR path = openFileDialog(hWnd, L"Open As", L"All Files (*.*)\0*.*\0", L"");
						if (path == NULL) break;

						OpenFileByNameAs(hWnd, path);
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

						//take as unconverted image file.
						unsigned int size;
						unsigned char *buffer = ObjReadWholeFile(path, &size);

						HWND h = CreateTextureEditorFromUnconverted(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, data->hWndMdi,
							buffer, size, path);
						EditorSetFile(h, path);
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

						NCER *ncer = (NCER *) ObjAlloc(FILE_TYPE_CELL, NpGetCellFormatForPreset());
						ncer->mappingMode = GX_OBJVRAMMODE_CHAR_1D_32K;
						ncer->nCells = 1;
						ncer->cells = (NCER_CELL *) calloc(1, sizeof(NCER_CELL));
						ncer->cells[0].attr = NULL;
						ncer->cells[0].nAttribs = 0;
						ncer->cells[0].cellAttr = 0;

						//create combo for object if necessary
						if (ncer->header.format == 0) {
							NpEnsureComboObject(hWnd, &ncer->header, NpGetComboFormatForPreset());
						}

						//if a character editor is open, use its mapping mode
						HWND hWndCharacterEditor = data->hWndNcgrViewer;
						if (hWndCharacterEditor != NULL) {
							NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) EditorGetData(hWndCharacterEditor);
							ncer->mappingMode = ncgrViewerData->ncgr->mappingMode;
						}

						NpOpenObject(hWnd, &ncer->header);
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
						NANR *nanr = (NANR *) ObjAlloc(FILE_TYPE_NANR, NpGetAnimFormatForPreset());

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

						if (nanr->header.format == 0) {
							NpEnsureComboObject(hWnd, &nanr->header, NpGetComboFormatForPreset());
						}

						HWND h = NpOpenObject(hWnd, &nanr->header);
						ShowWindow(h, SW_SHOW);
						break;
					}
					case ID_NEW_NEWTEXTUREARCHIVE:
					{
						TexArc *nsbtx = (TexArc *) ObjAlloc(FILE_TYPE_NSBTX, NSBTX_TYPE_NNS);
						NpOpenObject(hWnd, &nsbtx->header);
						break;
					}
					case ID_NEW_NEWFONT:
					{
						//init sensible defaults
						NFTR *nftr = (NFTR *) ObjAlloc(FILE_TYPE_FONT, NFTR_TYPE_NFTR_10);
						nftr->bpp = 1;
						nftr->hasCodeMap = 1;
						nftr->cellWidth = 8;
						nftr->cellHeight = 12;
						nftr->pxAscent = 10;
						nftr->lineHeight = 11;
						nftr->charset = FONT_CHARSET_UTF16;

						NpOpenObject(hWnd, &nftr->header);
						break;
					}
					case ID_NEWLAYOUT_LETTERLAYOUT:
					{
						BNLL *bnll = (BNLL *) ObjAlloc(FILE_TYPE_BNLL, BNLL_TYPE_BNLL);
						bnll->nMsg = 1;
						bnll->messages = (BnllMessage *) calloc(1, sizeof(BnllMessage));
						bnll->messages[0].pos.x.pos = 128;
						bnll->messages[0].pos.y.pos = 96;
						NpOpenObject(hWnd, &bnll->header);
						break;
					}
					case ID_NEWLAYOUT_CELLLAYOUT:
					{
						BNCL *bncl = (BNCL *) ObjAlloc(FILE_TYPE_BNCL, BNCL_TYPE_BNCL);
						bncl->nCell = 1;
						bncl->cells = (BnclCell *) calloc(1, sizeof(BnclCell));
						bncl->cells[0].pos.x.pos = 128;
						bncl->cells[0].pos.y.pos = 96;
						NpOpenObject(hWnd, &bncl->header);
						break;
					}
					case ID_NEWLAYOUT_BUTTONLAYOUT:
					{
						BNBL *bnbl = (BNBL *) ObjAlloc(FILE_TYPE_BNBL, BNBL_TYPE_BNBL);
						bnbl->nRegion = 1;
						bnbl->regions = (BnblRegion *) calloc(1, sizeof(BnblRegion));
						bnbl->regions[0].pos.x.pos = 128;
						bnbl->regions[0].pos.y.pos = 96;
						bnbl->regions[0].width = 64;
						bnbl->regions[0].height = 64;
						NpOpenObject(hWnd, &bnbl->header);
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
					case ID_PRESETS_NITROSYSTEM:
						NpSetPreset(hWnd, NP_PRESET_NITROSYSTEM);
						break;
					case ID_PRESETS_NITROCHARACTER:
						NpSetPreset(hWnd, NP_PRESET_NITROCHARACTER);
						break;
					case ID_PRESETS_ISIRISCHARACTER:
						NpSetPreset(hWnd, NP_PRESET_IRIS_CHARACTER);
						break;
					case ID_PRESETS_ISAGBCHARACTER:
						NpSetPreset(hWnd, NP_PRESET_AGB_CHARACTER);
						break;
					case ID_PRESETS_IMAGESTUDIO:
						NpSetPreset(hWnd, NP_PRESET_IMAGESTUDIO);
						break;
					case ID_PRESETS_GRF:
						NpSetPreset(hWnd, NP_PRESET_GRIT);
						break;
					case ID_PRESETS_RAW:
						NpSetPreset(hWnd, NP_PRESET_RAW);
						break;
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
					case ID_COMPRESSION_COMPRESSFILE:
					{
						LPWSTR path = openFileDialog(hWnd, L"Compress File...", L"All Files\0*.*\0", L"");
						if (path == NULL) break;

						unsigned int size;
						void *buf = ObjReadWholeFile(path, &size);
						free(path);

						CompressFileDialog(hWnd, buf, size);
						free(buf);
						break;
					}
					case ID_COMPRESSION_DECOMPRESSFILE:
					{
						LPWSTR path = openFileDialog(hWnd, L"Decompress File...", L"All Files\0*.*\0", L"");
						if (path == NULL) break;

						unsigned int size;
						void *buf = ObjReadWholeFile(path, &size);
						free(path);

						DecompressFileDialog(hWnd, buf, size);
						free(buf);
						break;
					}
					case ID_TEXTURE_CREATEPALETTESWAP:
					{
						LPWSTR paths = openFilesDialog(hWnd, L"Select Images", FILTER_IMAGE, L"");
						if (paths == NULL) break;

						wchar_t pathbuf[MAX_PATH + 1];
						int nPaths = getPathCount(paths);

						//check image dimensions are all equal
						int width = 0, height = 0, invalid = 0;
						for (int i = 0; i < nPaths; i++) {
							getPathFromPaths(paths, i, pathbuf);

							int width2, height2;
							COLOR32 *px = ImgRead(pathbuf, &width2, &height2);
							free(px);

							if (i == 0) {
								width = width2;
								height = height2;
							} else {
								if (width != width2 || height != height2) {
									invalid = 1;
									break;
								}
							}
						}

						//if the dimensions were invalid, report an error
						if (invalid) {
							MessageBox(hWnd, L"The images must all have equal dimension.", L"Create Palette Swap", MB_ICONERROR);
							free(paths);
							break;
						}

						//image dimensions must be valid texture image dimensions
						if (!TxDimensionIsValid(width) || (height > 1024)) {
							MessageBox(hWnd, L"Textures must have dimensions as powers of two greater than or equal to 8, and not exceeding 1024.",
								L"Create Palette Swap", MB_ICONERROR);
							free(paths);
							break;
						}

						//checks succeed, create palette swap dialog

						//dialog structure:
						//  [ File: C:\...\image1.png   Palette Name: image1_pl    ^ v ]
						//

						HWND h = CreateWindow(L"PaletteSwapClass", L"Create Palette Swap",
							WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
							CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWnd, NULL, NULL, NULL);

						for (int i = 0; i < nPaths; i++) {
							PaletteSwapEntry ent = { 0 };
							getPathFromPaths(paths, i, ent.path);

							ent.px = ImgRead(ent.path, &ent.width, &ent.height);
							SendMessage(h, NV_SETDATA, 0, (LPARAM) &ent);
						}
						SendMessage(h, NV_INITIALIZE, 0, 0);
						DoModal(h);

						free(paths);

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

	HWND hWndNclrViewer = NpOpenObject(hWndMain, &createData->nclr->header);
	HWND hWndNcgrViewer = NpOpenObject(hWndMain, &createData->ncgr->header);
	HWND hWndNscrViewer = NULL;

	ObjHeader *palobj = EditorGetObject(nitroPaintStruct->hWndNclrViewer);
	ObjHeader *chrobj = EditorGetObject(nitroPaintStruct->hWndNcgrViewer);
	ObjHeader *scrobj = NULL;

	if (createData->genParams.bgType != BGGEN_BGTYPE_BITMAP) {
		hWndNscrViewer = NpOpenObject(hWndMain, &createData->nscr->header);
		scrobj = EditorGetObject(hWndNscrViewer);
	} else {
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

			LPCWSTR color0Settings[] = { L"Fixed", L"Average", L"Edge", L"Contrasting", L"Used" };
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
				L"GRF",
				L"Raw",
				L"Raw Compressed"
			};

			int deffmt = 0;
			switch (g_configuration.preset) {
				case NP_PRESET_NITROSYSTEM: deffmt = 0; break;
				case NP_PRESET_NITROCHARACTER: deffmt = 1; break;
				case NP_PRESET_IMAGESTUDIO: deffmt = 4; break;
				case NP_PRESET_GRIT: deffmt = 7; break;
				case NP_PRESET_RAW: deffmt = 9; break;
			}

			CreateStatic(hWnd, L"Format:", rightX, middleY, 50, 22);
			data->hWndFormatDropdown = CreateCombobox(hWnd, formatNames, sizeof(formatNames) / sizeof(*formatNames), rightX + 55, middleY, 150, 22, deffmt);

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
					UiEditSetText(data->nscrCreateInput, location);
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

					createData->nclr = NULL;
					createData->ncgr = NULL;
					createData->nscr = NULL;

					//global setting
					BgGenerateParameters params;
					params.fmt = UiCbGetCurSel(data->hWndFormatDropdown);
					NpGetBalanceSetting(&data->balance, &params.balance);

					//dither setting
					params.dither.dither = GetCheckboxChecked(data->nscrCreateDither);
					params.dither.diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;

					//palette region
					params.compressPalette = GetCheckboxChecked(data->hWndRowLimit);
					params.color0Mode = UiCbGetCurSel(data->hWndColor0Setting);
					params.paletteRegion.base = GetEditNumber(data->hWndPaletteInput);
					params.paletteRegion.count = GetEditNumber(data->hWndPalettesInput);
					params.paletteRegion.offset = GetEditNumber(data->hWndPaletteOffset);
					params.paletteRegion.length = GetEditNumber(data->hWndPaletteSize);

					//character setting
					params.bgType = UiCbGetCurSel(data->nscrCreateDropdown);
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
					int index = UiCbGetCurSel(hWndControl);

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

static void *ReadWholeFile(const wchar_t *path, unsigned int *pSize) {
	DWORD dwRead, dwSizeHigh;
	HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	unsigned int size = GetFileSize(hFile, &dwSizeHigh);
	void *buffer = malloc(size);
	ReadFile(hFile, buffer, size, &dwRead, NULL);
	CloseHandle(hFile);

	*pSize = size;
	return buffer;
}

typedef struct {
	HWND hWnd;

	HWND hWndNtftInput;
	HWND hWndNtftBrowseButton;
	HWND hWndNtfpInput;
	HWND hWndNtfpBrowseButton;
	HWND hWndNtfiInput;
	HWND hWndNtfiBrowseButton;
	HWND hWndFormat;
	HWND hWndWidthInput;
	HWND hWndConvertButton;

	//preview work
	void *texel;
	void *index;
	void *palette;
	unsigned int texelSize;
	unsigned int indexSize;
	unsigned int paletteSize;

	FrameBuffer fb;
} NTFTCONVERTDATA;

extern int ilog2(int x);

static void NtftConvertUpdatePreview(NTFTCONVERTDATA *data) {
	unsigned int fmt = UiCbGetCurSel(data->hWndFormat) + 1;
	unsigned int c0xp = 0; // TODO
	unsigned int texS = UiCbGetCurSel(data->hWndWidthInput);
	unsigned int width = 8 << texS;

	//get the size of texel data
	unsigned int texelSize = data->texelSize;
	if (fmt == CT_4x4) {
		//limit texel size by index size
		unsigned int indexSize = data->indexSize;
		if (texelSize > indexSize * 2) texelSize = indexSize * 2;
	}

	//use format and width to derive the height
	static const unsigned int bppArray[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
	unsigned int bpp = bppArray[fmt];
	unsigned int height = (texelSize * 8) / bpp / width;
	if (height > 1024) height = 1024; // clamp height for a large file
	if (fmt == CT_4x4) {
		height &= ~3; // round down height to a multiple of 4 pixels
	}

	//get texture T dimension (smallest number such that 8<<t >= height
	unsigned int texT = 0;
	while ((8u << texT) < height) texT++;

	//texture
	TEXTURE texture = { 0 };
	texture.texels.texel = data->texel;
	texture.texels.cmp = data->index;
	texture.texels.texImageParam = (fmt << 26) | (texS << 20) | (texT << 23) | (c0xp << 20);
	texture.texels.height = height;
	texture.palette.pal = (COLOR *) data->palette;
	texture.palette.nColors = data->paletteSize / 2;

	//render
	COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	TxRender(px, &texture.texels, &texture.palette);

	//scale to size
	unsigned int previewWidth = 128;
	unsigned int previewHeight = 128;
	COLOR32 *preview = ImgScaleEx(px, width, height, previewWidth, previewHeight, IMG_SCALE_FIT);
	free(px);

	FbSetSize(&data->fb, previewWidth ,previewHeight);
	for (unsigned int y = 0; y < previewHeight; y++) {
		for (unsigned int x = 0; x < previewWidth; x++) {
			COLOR32 col = preview[x + y * previewWidth];
			data->fb.px[x + y * previewWidth] = TedAlphaBlendColor(REVERSE(col), x, y);
		}
	}
	free(preview);

	InvalidateRect(data->hWnd, NULL, FALSE);
}

LRESULT CALLBACK NtftConvertDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NTFTCONVERTDATA *data = (NTFTCONVERTDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (NTFTCONVERTDATA *) calloc(1, sizeof(NTFTCONVERTDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			static LPCWSTR widths[] = { L"8", L"16", L"32", L"64", L"128", L"256", L"512", L"1024" };

			data->hWnd = hWnd;
			SetWindowSize(hWnd, 280 + 10 + 128, 177);
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
			data->hWndWidthInput = CreateCombobox(hWnd, widths, 8, 70, 118, 100, 22, 0);
			data->hWndConvertButton = CreateButton(hWnd, L"Convert", 70, 145, 100, 22, TRUE);
			SetGUIFont(hWnd);

			FbCreate(&data->fb, hWnd, 1, 1);

			//populate the dropdown list
			for (int i = 1; i <= CT_DIRECT; i++) {
				WCHAR bf[16];
				mbstowcs(bf, TxNameFromTexFormat(i), sizeof(bf) / sizeof(bf[0]));
				UiCbAddString(data->hWndFormat, bf);
			}
			UiCbSetCurSel(data->hWndFormat, CT_4x4 - 1);
			
			//set preview
			NtftConvertUpdatePreview(data);

			HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			EnableWindow(hWndParent, FALSE);
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			int x = 240 + 30 + 10;
			int y = 10;

			FbDraw(&data->fb, hDC, x, y, data->fb.width, data->fb.height, 0, 0);

			EndPaint(hWnd, &ps);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
			if (hWndControl == data->hWndNtftBrowseButton) {
				LPWSTR path = openFileDialog(hWnd, L"Open NTFT", L"NTFT Files (*.ntft)\0*.ntft\0All Files\0*.*\0", L"ntft");
				if (!path) break;

				UiEditSetText(data->hWndNtftInput, path);
				free(data->texel);
				data->texel = ReadWholeFile(path, &data->texelSize);
				NtftConvertUpdatePreview(data);

				free(path);
			} else if (hWndControl == data->hWndNtfpBrowseButton) {
				LPWSTR path = openFileDialog(hWnd, L"Open NTFP", L"NTFP Files (*.ntfp)\0*.ntfp\0All Files\0*.*\0", L"ntfp");
				if (!path) break;

				UiEditSetText(data->hWndNtfpInput, path);
				free(data->palette);
				data->palette = ReadWholeFile(path, &data->paletteSize);
				NtftConvertUpdatePreview(data);

				free(path);
			} else if (hWndControl == data->hWndNtfiBrowseButton) {
				LPWSTR path = openFileDialog(hWnd, L"Open NTFI", L"NTFI Files (*.ntfi)\0*.ntfi\0All Files\0*.*\0", L"ntfi");
				if (!path) break;

				UiEditSetText(data->hWndNtfiInput, path);
				free(data->index);
				data->index = ReadWholeFile(path, &data->indexSize);
				NtftConvertUpdatePreview(data);

				free(path);
			} else if (hWndControl == data->hWndFormat && notif == CBN_SELCHANGE) {
				//every format needs NTFT. But not all NTFI or NTFP
				int fmt = UiCbGetCurSel(hWndControl) + 1; //1-based since entry 0 corresponds to format 1

				//only 4x4 needs NTFI.
				int needsNtfi = fmt == CT_4x4;
				EnableWindow(data->hWndNtfiInput, needsNtfi);
				EnableWindow(data->hWndNtfiBrowseButton, needsNtfi);

				//only direct doesn't need and NTFP.
				int needsNtfp = fmt != CT_DIRECT;
				EnableWindow(data->hWndNtfpInput, needsNtfp);
				EnableWindow(data->hWndNtfpBrowseButton, needsNtfp);

				//update
				NtftConvertUpdatePreview(data);
			} else if (hWndControl == data->hWndWidthInput && notif == CBN_SELCHANGE) {
				//update
				NtftConvertUpdatePreview(data);
			} else if (hWndControl == data->hWndConvertButton) {
				WCHAR src[MAX_PATH + 1];
				int width = 8 << UiCbGetCurSel(data->hWndWidthInput);
				int format = UiCbGetCurSel(data->hWndFormat) + 1;

				unsigned int bppArray[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
				unsigned int bpp = bppArray[format];

				unsigned int ntftSize = 0, ntfpSize = 0, ntfiSize = 0;
				BYTE *ntft = NULL, *ntfp = NULL, *ntfi = NULL;
				
				//read files
				char palName[17] = { 0 };
				SendMessage(data->hWndNtftInput, WM_GETTEXT, MAX_PATH, (LPARAM) src);
				if (src[0]) {
					ntft = ReadWholeFile(src, &ntftSize);
				}

				SendMessage(data->hWndNtfpInput, WM_GETTEXT, MAX_PATH, (LPARAM) src);
				if (src[0]) {
					ntfp = ReadWholeFile(src, &ntfpSize);

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
				if (src[0]) {
					ntfi = ReadWholeFile(src, &ntfiSize);
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

				//texture height (round up)
				int height = ntftSize * 8 / bpp / width;
				int l2width = ilog2(width) - 3, l2height = ilog2(height) - 3;
				if ((8 << l2height) < height) l2height++;

				int padWidth = 8 << l2width, padHeight = 8 << l2height;

				//pad data with zeros
				if (padHeight > height) {
					unsigned int ntftPadSize = padWidth * padHeight * bppArray[format] / 8;
					ntft = realloc(ntft, ntftPadSize);
					memset(ntft + ntftSize, 0, ntftPadSize - ntftSize);
					if (ntfi != NULL) {
						ntfi = realloc(ntfi, ntftPadSize / 2);
						memset(ntfi + ntfiSize, 0, ntftPadSize / 2 - ntfiSize);
					}
				}

				//ok now actually convert
				TEXTURE texture = { 0 };
				texture.palette.pal = (COLOR *) ntfp;
				texture.palette.nColors = ntfpSize / 2;
				texture.texels.texel = ntft;
				texture.texels.cmp = (uint16_t *) ntfi;
				texture.texels.texImageParam = (format << 26) | (l2width << 20) | (l2height << 23);
				texture.texels.height = height;
				texture.palette.name = calloc(strlen(palName) + 1, 1);
				memcpy(texture.palette.name, palName, strlen(palName));

				//texture editor takes ownership of texture data, no need to free
				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);
				HWND hWndMdi = nitroPaintStruct->hWndMdi;

				//create texture object
				TextureObject *obj = (TextureObject *) ObjAlloc(FILE_TYPE_TEXTURE, TEXTURE_TYPE_NNSTGA);
				memcpy(&obj->texture, &texture, sizeof(texture));
				CreateTextureEditorImmediate(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWndMdi, obj);

				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_DESTROY:
		{
			FbDestroy(&data->fb);
			free(data->texel);
			free(data->index);
			free(data->palette);
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

			unsigned int nFormat = ObjGetFormatCountByType(editorData->file->type);
			for (unsigned int i = 1; i < nFormat; i++) {
				const char *name = ObjGetFormatNameByType(editorData->file->type, i);

				wchar_t buf[64];
				mbstowcs(buf, name, sizeof(buf) / sizeof(buf[0]));

				UiCbAddString(hWndFormatCombobox, buf);
			}
			UiCbSetCurSel(hWndFormatCombobox, editorData->file->format - 1);

			const char *const *compressions = g_ObjCompressionNames;
			while (*compressions != NULL) {
				wchar_t buf[64];
				mbstowcs(buf, *(compressions++), sizeof(buf) / sizeof(buf[0]));

				UiCbAddString(hWndCompressionCombobox, buf);
			}
			UiCbSetCurSel(hWndCompressionCombobox, editorData->file->compression);

			SetWindowLong(hWnd, sizeof(LPVOID), (LONG) hWndFormatCombobox);
			SetWindowLong(hWnd, sizeof(LPVOID) * 2, (LONG) hWndCompressionCombobox);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl && HIWORD(wParam) == BN_CLICKED) {
				int fmt = UiCbGetCurSel((HWND) GetWindowLong(hWnd, sizeof(LPVOID))) + 1;
				int comp = UiCbGetCurSel((HWND) GetWindowLong(hWnd, sizeof(LPVOID) * 2));
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
					UiEditSetText(cdData->nscrCreateInput, data->szPath);

					SendMessage(hWnd, WM_CLOSE, 0, 0);
					DoModal(h);
				} else if (hWndControl == data->hWndTexture) {

					unsigned int size;
					unsigned char *buffer = ObjReadWholeFile(data->szPath, &size);

					CreateTextureEditorFromUnconverted(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
						hWndMdi, buffer, size, data->szPath);
					free(buffer);
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
				L"NITRO-System", L"NITRO-CHARACTER", L"IRIS-CHARACTER", L"AGB-CHARACTER", L"iMageStudio",
				L"Hudson", L"Hudson 2", L"GRF", L"Raw", L"Raw Compressed"
			};

			//get default format for preset
			int def = 0;
			switch (g_configuration.preset) {
				case NP_PRESET_NITROSYSTEM: def = 0; break;
				case NP_PRESET_NITROCHARACTER: def = 1; break;
				case NP_PRESET_IRIS_CHARACTER: def = 2; break;
				case NP_PRESET_AGB_CHARACTER: def = 3; break;
				case NP_PRESET_IMAGESTUDIO: def = 4; break;
				case NP_PRESET_GRIT: def = 7; break;
				case NP_PRESET_RAW: def = 9; break;
			}

			CreateStatic(hWnd, L"8 bit:", 10, 10, 50, 22);
			data->hWndBitDepth = CreateCheckbox(hWnd, L"", 70, 10, 22, 22, FALSE);
			CreateStatic(hWnd, L"Mapping:", 10, 42, 50, 22);
			data->hWndMapping = CreateCombobox(hWnd, mappings, sizeof(mappings) / sizeof(*mappings), 70, 42, 200, 100, 1);
			CreateStatic(hWnd, L"Format:", 10, 74, 50, 22);
			data->hWndFormat = CreateCombobox(hWnd, formats, sizeof(formats) / sizeof(*formats), 70, 74, 150, 100, def);
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
					int mapping = UiCbGetCurSel(data->hWndMapping);
					int mappings[] = { GX_OBJVRAMMODE_CHAR_2D, GX_OBJVRAMMODE_CHAR_1D_32K, GX_OBJVRAMMODE_CHAR_1D_64K,
						GX_OBJVRAMMODE_CHAR_1D_128K, GX_OBJVRAMMODE_CHAR_1D_256K };
					int heights[] = { 16, 32, 64, 128, 256 };
					int height = heights[mapping];
					mapping = mappings[mapping];
					int format = UiCbGetCurSel(data->hWndFormat);

					int charFormats[] = {
						NCGR_TYPE_NCGR,
						NCGR_TYPE_NC,
						NCGR_TYPE_IC,
						NCGR_TYPE_AC,
						0,                 // iMageStudio
						NCGR_TYPE_HUDSON,
						NCGR_TYPE_HUDSON2,
						0,                 // GRF
						NCGR_TYPE_BIN,     // raw (uncompressed)
						NCGR_TYPE_BIN      // raw (compressed)
					};
					int palFormats[] = {
						NCLR_TYPE_NCLR,
						NCLR_TYPE_NC,
						NCLR_TYPE_BIN,
						NCLR_TYPE_BIN,
						0,                 // iMageStudio 
						NCLR_TYPE_HUDSON, 
						NCLR_TYPE_HUDSON,
						0,                 // GRF
						NCLR_TYPE_BIN,     // raw (uncompressed)
						NCLR_TYPE_BIN      // raw (compressed)
					};
					int compression = format == 9 ? COMPRESSION_LZ77 : COMPRESSION_NONE;
					int charFormat = charFormats[format];
					int palFormat = palFormats[format];

					NCLR *nclr = (NCLR *) ObjAlloc(FILE_TYPE_PALETTE, palFormat);
					nclr->colors = (COLOR *) calloc(256, sizeof(COLOR));
					nclr->nColors = 256;
					nclr->nBits = nBits;
					
					NCGR *ncgr = (NCGR *) ObjAlloc(FILE_TYPE_CHARACTER, charFormat);
					ncgr->header.compression = compression;
					ncgr->nBits = nBits;
					ncgr->mappingMode = mapping;
					ncgr->tilesX = 32;
					ncgr->tilesY = height;
					ncgr->nTiles = ncgr->tilesX * ncgr->tilesY;
					ChrAllocGraphics(ncgr);

					//link objects
					ObjLinkObjects(&nclr->header, &ncgr->header);

					int combofmt = COMBO2D_TYPE_INVALID;
					switch (format) {
						case 4: combofmt = COMBO2D_TYPE_5BG; break;
						case 7: combofmt = COMBO2D_TYPE_GRF_BG; break;
					}

					//link by combo
					if (combofmt != COMBO2D_TYPE_INVALID) {
						COMBO2D *combo = (COMBO2D *) ObjAlloc(FILE_TYPE_COMBO2D, combofmt);
						combo2dLink(combo, &nclr->header);
						combo2dLink(combo, &ncgr->header);

						NpEnsureComboObject(hWndMain, &nclr->header, combofmt);
					}

					NpOpenObject(hWndMain, &nclr->header);
					NpOpenObject(hWndMain, &ncgr->header);

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
				int fmtSel = UiCbGetCurSel(data->hWndFormat);

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

				NSCR *nscr = (NSCR *) ObjAlloc(FILE_TYPE_SCREEN, NpGetScreenFormatForPreset());
				nscr->fmt = format;
				nscr->colorMode = colorMode;
				nscr->tilesX = tilesX;
				nscr->tilesY = tilesY;
				nscr->dataSize = tilesX * tilesY * sizeof(uint16_t);
				nscr->data = (uint16_t *) calloc(tilesX * tilesY, sizeof(uint16_t));

				//create combo for object if necessary
				if (nscr->header.format == 0) {
					NpEnsureComboObject(hWndMain, &nscr->header, NpGetComboFormatForPreset());
				}

				NpOpenObject(hWndMain, &nscr->header);

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
						NSCR *newNscr = (NSCR *) ObjAlloc(FILE_TYPE_SCREEN, nscr->header.format);
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

						HWND h2 = NpOpenObject(hWndMain, &newNscr->header);
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

				UiEditSetText(hWndForegroundPath, path);
				free(path);
			} else if (hWndControl == hWndBackgroundBrowse && notif == BN_CLICKED) {
				//set background path
				LPWSTR filter = L"Supported Image Files\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.tga\0All Files\0*.*\0";
				LPWSTR path = openFileDialog(hWnd, L"Open Image", filter, L"");
				if (path == NULL) break;

				UiEditSetText(hWndBackgroundPath, path);
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
			ObjHeader *obj = (ObjHeader *) EditorGetObject(hWndEditor);

			//get combo if the object is a part of one
			COMBO2D *combo = (COMBO2D *) obj->combo;
			data->combo = combo;

			//filter the window list based on applicability. If we're not in a combo, filter out all those
			//that are. If we are in one, filter out all those not in the same one.
			for (size_t i = 0; i < data->editors.length; i++) {
				int remove = 0;

				ObjHeader *obj2 = (*(EDITOR_DATA **) StListGetPtr(&data->editors, i))->file;
				if (obj2 == NULL) {
					remove = 1;
				} else if (combo != NULL) {
					if (obj2->combo != NULL && (COMBO2D *) obj2->combo != combo) remove = 1;
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
				AddCheckedListViewItem(data->hWndObjects, buf, i, (hWndEditor == ed->hWnd) || (combo != NULL && (COMBO2D *) ed->file->combo == combo));
			}

			//populate type field
			if (combo != NULL) {
				UiCbSetCurSel(data->hWndFormat, combo->header.format - 1);
			}

			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				if (hWndControl == data->hWndOK) {
					int fmt = UiCbGetCurSel(data->hWndFormat) + 1;

					//if no combo in data, create a new one.
					if (data->combo == NULL) {
						COMBO2D *combo = (COMBO2D *) ObjAlloc(FILE_TYPE_COMBO2D, fmt);
						
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
							ObjHeader *obj = (*(EDITOR_DATA **) StListGetPtr(&data->editors, i))->file;

							if (CheckedListViewIsChecked(data->hWndObjects, i) && obj->combo == NULL) {
								combo2dLink(data->combo, obj);
							}
						}

						for (size_t i = 0; i < data->editors.length; i++) {
							ObjHeader *obj = (*(EDITOR_DATA **) StListGetPtr(&data->editors, i))->file;

							if (!CheckedListViewIsChecked(data->hWndObjects, i) && (COMBO2D *) obj->combo == data->combo) {
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
				int depthSel = UiCbGetCurSel(data->hWndPaletteDepth);
				int countSel = UiCbGetCurSel(data->hWndPaletteCount) + 1;

				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = NpGetData(hWndMain);

				NCLR *nclr = (NCLR *) ObjAlloc(FILE_TYPE_PALETTE, NpGetPaletteFormatForPreset());
				nclr->nBits = depthSel ? 8 : 4;
				nclr->nColors = countSel << nclr->nBits;
				nclr->colors = (COLOR *) calloc(nclr->nColors, sizeof(COLOR));
				nclr->extPalette = (depthSel && countSel > 1);

				//create combo for object if necessary
				if (nclr->header.format == 0) {
					NpEnsureComboObject(hWndMain, &nclr->header, NpGetComboFormatForPreset());
				}

				NpOpenObject(hWndMain, &nclr->header);

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

typedef struct OpenAsData_ {
	const wchar_t *path;
	unsigned char *buffer;
	unsigned int size;

	StList formats;
	StList compressions;

	HWND hWndCompressionLabel;
	HWND hWndCompressionDropdown;

	HWND hWndFormatLabel;
	HWND hWndFormatDropdown;

	HWND hWndOK;
	HWND hWndCancel;
} OpenAsData;

static void OpenAsOnCompressionChanged(OpenAsData *data) {
	int compression;
	int sel = UiCbGetCurSel(data->hWndCompressionDropdown);
	StListGet(&data->compressions, sel, &compression);

	//decompress
	unsigned int uncompSize;
	unsigned char *uncomp = CxDecompress(data->buffer, data->size, compression, &uncompSize);

	StListClear(&data->formats);
	StListCreateInline(&data->formats, ObjIdEntry, NULL);
	ObjIdentifyMultipleByType(&data->formats, uncomp, uncompSize, FILE_TYPE_INVALID);

	SendMessage(data->hWndFormatDropdown, CB_RESETCONTENT, 0, 0);
	for (size_t i = 0; i < data->formats.length; i++) {
		ObjIdEntry *ent = StListGetPtr(&data->formats, i);
		const char *typeName = ObjGetFileTypeName(ent->type);

		wchar_t textbuf[64];
		wsprintfW(textbuf, L"%S (%S)", typeName, ent->name);
		UiCbAddString(data->hWndFormatDropdown, textbuf);
	}

	//set default
	UiCbSetCurSel(data->hWndFormatDropdown, 0);

	//if nonzero number of formats responded, enable the OK button.
	if (data->formats.length > 0) {
		EnableWindow(data->hWndOK, TRUE);
	} else {
		EnableWindow(data->hWndOK, FALSE);
	}

	free(uncomp);
}

static LRESULT CALLBACK OpenAsDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	OpenAsData *data = (OpenAsData *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			data = (OpenAsData *) calloc(1, sizeof(OpenAsData));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			//init lists
			StListCreateInline(&data->formats, ObjIdEntry, NULL);
			StListCreateInline(&data->compressions, int, NULL);

			data->hWndCompressionLabel = CreateStatic(hWnd, L"Compression:", 10, 10, 100, 22);
			data->hWndCompressionDropdown = CreateCombobox(hWnd, NULL, 0, 110, 10, 200, 22, 0);
			data->hWndFormatLabel = CreateStatic(hWnd, L"Format:", 10, 37, 100, 22);
			data->hWndFormatDropdown = CreateCombobox(hWnd, NULL, 0, 110, 37, 200, 22, 0);

			data->hWndOK = CreateButton(hWnd, L"Open", 210, 64, 100, 22, TRUE);
			data->hWndCancel = CreateButton(hWnd, L"Cancel", 105, 64, 100, 22, FALSE);

			SetWindowSize(hWnd, 320, 96);
			SetGUIFont(hWnd);
			break;
		}
		case NV_INITIALIZE:
		{
			const wchar_t *path = (const wchar_t *) wParam;
			data->path = path;

			unsigned int size;
			unsigned char *buf = ObjReadWholeFile(path, &size);
			data->buffer = buf;
			data->size = size;

			//detect compression
			int def = 0; // no compression
			for (int i = 0; i < COMPRESSION_MAX; i++) {
				if (CxIsCompressed(buf, size, i)) {
					StListAdd(&data->compressions, &i);
				}
			}
			def = CxGetCompressionType(buf, size);

			for (size_t i = 0; i < data->compressions.length; i++) {
				int type;
				StListGet(&data->compressions, i, &type);

				wchar_t buf[64];
				mbstowcs(buf, g_ObjCompressionNames[type], sizeof(buf) / sizeof(buf[0]));

				UiCbAddString(data->hWndCompressionDropdown, buf);
			}
			UiCbSetCurSel(data->hWndCompressionDropdown, StListIndexOf(&data->compressions, &def));

			OpenAsOnCompressionChanged(data);

			break;
		}
		case WM_COMMAND:
		{
			HWND hWndCtl = (HWND) lParam;
			int idCtl = LOWORD(wParam);
			int cmd = HIWORD(wParam);

			if (hWndCtl == data->hWndCompressionDropdown && cmd == CBN_SELCHANGE) {
				//update detections list
				OpenAsOnCompressionChanged(data);
			} else if ((hWndCtl == data->hWndCancel || idCtl == IDCANCEL) && cmd == BN_CLICKED) {
				//exit
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if ((hWndCtl == data->hWndOK || idCtl == IDOK) && cmd == BN_CLICKED) {

				//get selected parameters
				int iCompression = UiCbGetCurSel(data->hWndCompressionDropdown);
				int iFormat = UiCbGetCurSel(data->hWndFormatDropdown);

				int compression;
				ObjIdEntry idEntry;
				StListGet(&data->compressions, iCompression, &compression);
				StListGet(&data->formats, iFormat, &idEntry);

				//select
				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				OpenFileByContent(hWndMain, data->buffer, data->size, data->path, compression, idEntry.type, idEntry.format);

				//close
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}
		case WM_DESTROY:
		{
			StListFree(&data->compressions);
			StListFree(&data->formats);

			free(data);
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) NULL);
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
	switch (UiCbGetCurSel(data->hWndAlphaMode)) {
		case 0: // Mode=None
			flag |= RX_FLAG_ALPHA_MODE_NONE | RX_FLAG_NO_ALPHA_DITHER;
			plttOffs = 0; // no offset
			break;
		case 1: // Mode=Color0
			flag |= RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_NO_ALPHA_DITHER;
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
		RxCreatePalette(data->px, data->width, data->height,
			data->pltt + plttOffs, nColors - plttOffs, &balance, flag, &nColUse);
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
	RxReduceImage(data->reduced, data->indices, data->width, data->height, data->pltt, nColUse + plttOffs,
		flag, diffuse, &balance);

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

				UiEditSetText(data->hWndFixedPalettePath, path);
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

typedef struct CompressFileData_ {
	unsigned int size;
	const unsigned char *buffer;

	unsigned int compSize;
	unsigned char *comp;

	HWND hWndCompressionFormats;
	HWND hWndStatus;
	HWND hWndOK;
} CompressFileData;

static void CompressDialogUpdate(CompressFileData *data) {
	int sel = UiCbGetCurSel(data->hWndCompressionFormats) + 1;

	free(data->comp);

	unsigned int compSize;
	unsigned char *comp = CxCompress(data->buffer, data->size, sel, &compSize);

	data->compSize = compSize;
	data->comp = comp;

	unsigned int permille = 0;

	if (data->size > 0) {
		if (compSize <= data->size) {
			//reduction
			permille = 1000 - (compSize * 2000 + data->size) / (2 * data->size);
		} else {
			//inflation
			permille = (compSize * 2000 + data->size) / (2 * data->size) - 1000;
		}
	}

	//put label
	WCHAR buf[128];
	wsprintfW(buf, L"Compressed to %d bytes (%d.%d%% %S)", compSize, permille / 10, permille % 10,
		compSize <= data->size ? "reduction" : "inflation");
	SendMessage(data->hWndStatus, WM_SETTEXT, 0, (LPARAM) buf);
}

static LRESULT CALLBACK CompressFileProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	CompressFileData *data = (CompressFileData *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			data = (CompressFileData *) calloc(1, sizeof(CompressFileData));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);

			CreateStatic(hWnd, L"Compression:", 10, 10, 75, 22);
			data->hWndCompressionFormats = CreateCombobox(hWnd, NULL, 0, 85, 10, 200, 22, 0);
			data->hWndStatus = CreateStatic(hWnd, L"", 10, 37, 275, 22);
			data->hWndOK = CreateButton(hWnd, L"Compress", 185, 64, 100, 22, TRUE);

			//add compression formats
			for (int i = 1; i < COMPRESSION_MAX; i++) {
				WCHAR buf[32];
				
				mbstowcs(buf, g_ObjCompressionNames[i], sizeof(buf) / sizeof(buf[0]));
				UiCbAddString(data->hWndCompressionFormats, buf);
			}
			UiCbSetCurSel(data->hWndCompressionFormats, 0);

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 295, 96);
			break;
		}
		case NV_INITIALIZE:
		{
			unsigned int size = wParam;
			const void *buf = (const void *) lParam;
			data->size = size;
			data->buffer = buf;
			CompressDialogUpdate(data);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndCtl = (HWND) lParam;
			if (hWndCtl == data->hWndCompressionFormats && HIWORD(wParam) == CBN_SELCHANGE) {
				CompressDialogUpdate(data);
			} else if ((hWndCtl == data->hWndOK || LOWORD(wParam) == IDOK) && HIWORD(wParam) == BN_CLICKED) {
				//save
				LPWSTR path = saveFileDialog(hWnd, L"Save File", L"All Files\0*.*\0", L"");
				if (path == NULL) break;

				HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile != INVALID_HANDLE_VALUE) {
					DWORD dwWritten;
					WriteFile(hFile, data->comp, data->compSize, &dwWritten, NULL);
					CloseHandle(hFile);

					//close
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}

				free(path);
			}
			break;
		}
		case WM_DESTROY:
		{
			free(data->comp);
			free(data);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}



typedef struct PaletteSwapParams_ {
	COLOR32 *imgCat;
	int *indices;
	unsigned int width;
	unsigned int height;
	unsigned int nLayers;
	COLOR32 *pltt;
	unsigned int plttSize;
	float diffuse;
	int c0xp;
	RxFlag flag;
	const RxBalanceSetting *balance;
	unsigned int *pnUsed;
} PaletteSwapParams;

static DWORD CALLBACK PaletteSwapImpl(LPVOID lpParam) {
	PaletteSwapParams *params = (PaletteSwapParams *) lpParam;

	//create the palette data
	RxReduction *reduction = RxNew(params->balance);
	RxSetPaletteLayers(reduction, params->nLayers);
	RxApplyFlags(reduction, params->flag);
	RxHistAdd(reduction, params->imgCat, params->width, params->height);
	RxHistFinalize(reduction);
	RxComputePalette(reduction, params->plttSize - (params->c0xp ? 1 : 0));
	RxSortPalette(reduction, RX_FLAG_SORT_ONLY_USED | RX_FLAG_SORT_END_DIFFER);

	//read palettes
	unsigned int nUsedColors = reduction->nUsedColors;
	for (unsigned int i = 0; i < params->nLayers; i++) {
		RxGetPalette(reduction, params->pltt + params->plttSize * i, i);
	}
	*params->pnUsed = nUsedColors;

	//index the images
	RxReduceImageWithContext(reduction, params->imgCat, params->indices, params->width, params->height, params->pltt, params->plttSize, params->flag, params->diffuse);
	RxFree(reduction);
	return 0;
}

static void PaletteSwapThreaded(HWND hWnd, COLOR32 *imgCat, int *indices, unsigned int width, unsigned int height, unsigned int nLayers, COLOR32 *pltt, unsigned int plttSize, float diffuse, int c0xp, RxFlag flag, const RxBalanceSetting *balance, unsigned int *pnUsedColors) {

	//modal window
	HWND hWndModal = CreateWindow(L"CompressionProgress", L"Compressing",
		WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 150, hWnd, NULL, NULL, NULL);

	PaletteSwapParams params;
	params.imgCat = imgCat;
	params.indices = indices;
	params.width = width;
	params.height = height;
	params.nLayers = nLayers;
	params.pltt = pltt;
	params.plttSize = plttSize;
	params.diffuse = diffuse;
	params.c0xp = c0xp;
	params.flag = flag;
	params.balance = balance;
	params.pnUsed = pnUsedColors;
	HANDLE hThread = CreateThread(NULL, 0, PaletteSwapImpl, &params, 0, NULL);

	DoModalWait(hWndModal, hThread);

	CloseHandle(hThread);
}

static LRESULT CALLBACK PaletteSwapProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	PaletteSwapData *data = (PaletteSwapData *) GetWindowLongPtr(hWnd, 0);

	//data output types for dropdown
	enum {
		TYPE_MULTIPLE_TEXTURES,
		TYPE_SINGLE_TEXTURE,
		TYPE_TEXARC
	};

	//texture formats for dropdown
	enum {
		FMT_PLTT4,
		FMT_PLTT16,
		FMT_PLTT256
	};

	switch (msg) {
		case WM_CREATE:
		{
			data = (PaletteSwapData *) calloc(1, sizeof(PaletteSwapData));
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
			break;
		}
		case NV_SETDATA:
		{
			if (data->nEntries >= RX_PALETTE_MAX_COUNT) break;

			PaletteSwapEntry *ent = (PaletteSwapEntry *) lParam;
			memcpy(&data->entries[data->nEntries], ent, sizeof(PaletteSwapEntry));
			data->nEntries++;
			break;
		}
		case NV_INITIALIZE:
		{
			//create UI
			int fileWidth = 300;
			for (unsigned int i = 0; i < data->nEntries; i++) {
				int y = 10 + i * 27;
				WCHAR plttName[17] = { 0 };
				TexViewerChoosePaletteName(plttName, data->entries[i].path);

				CreateStatic(hWnd, L"File:", 10, y, 30, 22);
				data->hWndFileLabels[i] = CreateStatic(hWnd, data->entries[i].path, 40, y, fileWidth, 22);
				CreateStatic(hWnd, L"Palette Name:", 40 + fileWidth + 5, y, 75, 22);
				data->hWndPaletteNames[i] = CreateEdit(hWnd, plttName, 40 + fileWidth + 5 + 75, y, 100, 22, FALSE);

				data->hWndUpButtons[i] = CreateButton(hWnd, L"\x25B4", 40 + fileWidth + 5 + 75 + 100 + 5, y - 2, 30, 13, FALSE);
				data->hWndDownButtons[i] = CreateButton(hWnd, L"\x25BE", 40 + fileWidth + 5 + 75 + 100 + 5, y - 2 + 13, 30, 13, FALSE);
			}

			//disable the first up arrow and the last down arrow.
			EnableWindow(data->hWndUpButtons[0], FALSE);
			EnableWindow(data->hWndDownButtons[data->nEntries - 1], FALSE);

			//should color 0 be transparent?
			int c0xp = 0;
			for (unsigned int i = 0; i < data->nEntries; i++) {
				COLOR32 *px = data->entries[i].px;
				for (unsigned int j = 0; j < data->entries[i].width * data->entries[i].height; j++) {
					unsigned int a = px[j] >> 24;
					if (a < 0x80) c0xp = 1;
				}
			}

			int panelY = 10 + data->nEntries * 27 - 5 + 10;

			LPCWSTR types[] = { L"Separate Textures", L"Single Texture, Large Palette", L"Texture Archive" };
			LPCWSTR formats[] = { L"palette4", L"palette16", L"palette256" };

			CreateStatic(hWnd, L"Data Output:", 10, panelY, 100, 22);
			data->hWndType = CreateCombobox(hWnd, types, 3, 110, panelY, 175, 22, 0);

			CreateStatic(hWnd, L"Texture Format:", 10, panelY + 27, 100, 22);
			data->hWndTextureFormat = CreateCombobox(hWnd, formats, sizeof(formats) / sizeof(formats[0]), 110, panelY + 27, 100, 22, 2);
			data->hWndC0xp = CreateCheckbox(hWnd, L"Color 0 is Transparent", 220, panelY + 27, 150, 22, c0xp);

			data->hWndDither = CreateCheckbox(hWnd, L"Dither:", 380, panelY + 27, 60, 22, FALSE);
			data->hWndDiffuse = CreateEdit(hWnd, L"100", 440, panelY + 27, 50, 22, TRUE);
			EnableWindow(data->hWndDiffuse, FALSE);

			int balanceHeight = 3 * 27 - 5 + 10 + 10 + 10;
			int okY = panelY + 27 + 32 + balanceHeight + 10;
			int okX = 40 + fileWidth + 5 + 75 + 5 + 30;

			NpCreateBalanceInput(&data->balance, hWnd, 10, panelY + 27 + 32, okX + 100 - 10);

			data->hWndOK = CreateButton(hWnd, L"OK", okX, okY, 100, 22, TRUE);
			data->hWndCancel = CreateButton(hWnd, L"Cancel", okX - 105, okY, 100, 22, FALSE);

			SetWindowSize(hWnd, okX + 100 + 10, okY + 22 + 10);
			SetGUIFont(hWnd);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int idCtl = LOWORD(wParam);
			int cmd = HIWORD(wParam);
			
			if ((hWndControl == data->hWndOK || idCtl == IDOK) && cmd == BN_CLICKED) {
				HWND hWndMain = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);

				int c0xp = GetCheckboxChecked(data->hWndC0xp);
				int dataType = UiCbGetCurSel(data->hWndType);
				unsigned int nLayers = data->nEntries;

				//get texture format
				int fmtSel = UiCbGetCurSel(data->hWndTextureFormat);
				int texfmt = CT_256COLOR;
				unsigned int maxCols = 256, bpp = 8;
				switch (fmtSel) {
					case FMT_PLTT4   : texfmt = CT_4COLOR;   maxCols =   4; bpp = 2; break;
					case FMT_PLTT16  : texfmt = CT_16COLOR;  maxCols =  16; bpp = 4; break;
					case FMT_PLTT256 : texfmt = CT_256COLOR; maxCols = 256; bpp = 8; break;
				}

				//dither settings
				int dither = GetCheckboxChecked(data->hWndDither);
				float diffuse = ((float) GetEditNumber(data->hWndDiffuse)) / 100.0f;
				if (!dither) diffuse = 0.0f;

				//TODO: ask the user?
				unsigned int plttSize = maxCols;
				RxBalanceSetting balance;
				NpGetBalanceSetting(&data->balance, &balance);

				RxFlag flag = RX_FLAG_NO_WRITEBACK;
				if (1) flag |= RX_FLAG_NO_ALPHA_DITHER; // TODO: user input?
				if (c0xp) flag |= RX_FLAG_ALPHA_MODE_RESERVE;
				else      flag |= RX_FLAG_ALPHA_MODE_NONE;

				unsigned int width = data->entries[0].width, height = data->entries[0].height;
				unsigned int padWidth = 1, padHeight = 1;
				while (padHeight < height) padHeight <<= 1;
				while (padWidth < width) padWidth <<= 1;

				//check that alpha values of corresponding pixels quantize to the same value and throw a warning.
				unsigned int nPx = width * height;
				int differAlpha = 0;
				for (unsigned int i = 1; i < nLayers; i++) {
					COLOR32 *img0 = data->entries[0].px;
					COLOR32 *imgI = data->entries[i].px;

					for (unsigned int j = 0; j < nPx; j++) {
						if ((imgI[j] & 0xFF000000) != (img0[j] & 0xFF000000)) {
							differAlpha = 1;
							break;
						}
					}
				}

				if (differAlpha) {
					int mr = MessageBox(hWnd, L"Corresponding alpha levels differ across images. Continue?", L"Create Palette Swap",
						MB_ICONWARNING | MB_OKCANCEL);
					if (mr == IDCANCEL) break;
				}

				//the alpha levels cannot differ across images, so we will take the lowest alpha across images.
				//when a pixel is transparent, it must be transparent across all images. Taking the average would
				//possibly make transparent pixels have nonzero opacity, which is meaningless as they have no color.
				for (unsigned int i = 0; i < nPx; i++) {
					//get low alpha
					unsigned int minA = 0xFF;
					for (unsigned int j = 0; j < nLayers; j++) {
						unsigned int a = data->entries[j].px[i] >> 24;
						if (a < minA) minA = a;
					}

					//write across images
					for (unsigned int j = 0; j < nLayers; j++) {
						data->entries[j].px[i] = (data->entries[j].px[i] & 0x00FFFFFF) | (minA << 24);
					}
				}

				//concatenate image buffers
				COLOR32 *imgCat = (COLOR32 *) calloc(nPx * nLayers, sizeof(COLOR32));
				for (unsigned int i = 0; i < nLayers; i++) {
					memcpy(imgCat + i * nPx, data->entries[i].px, nPx * sizeof(COLOR32));
				}

				//create the palette data.
				COLOR32 *pltt = (COLOR32 *) calloc(plttSize * nLayers, sizeof(COLOR32));
				int *indices = (int *) calloc(width * height, sizeof(int));

				unsigned int nUsedColors;
				PaletteSwapThreaded(hWnd, imgCat, indices, width, height, nLayers, pltt, plttSize, diffuse, c0xp, flag, &balance, &nUsedColors);

				free(imgCat);

				//create the texel data
				unsigned int texelSize = (padWidth * padHeight * bpp) / 8;
				unsigned char *texel = (unsigned char *) calloc(texelSize, 1);

				unsigned int pxPerByte = 8 / bpp;
				for (unsigned int i = 0; i < nPx; i++) {
					unsigned char icol = (unsigned char) indices[i];

					unsigned int iPx = i / pxPerByte;
					unsigned int shift = (i % pxPerByte) * bpp;
					texel[iPx] |= icol << shift;
				}
				free(indices);

				//compute the TEXIMAGE_PARAM
				uint32_t texImageParam = 0;
				if (c0xp) texImageParam |= (1 << 29);
				texImageParam |= (1 << 17) | (1 << 16);
				texImageParam |= (ilog2(padWidth >> 3) << 20) | (ilog2(padHeight >> 3) << 23);
				texImageParam |= texfmt << 26;

				switch (dataType) {
					case TYPE_MULTIPLE_TEXTURES:
					{
						//create multiple textures
						for (unsigned int i = 0; i < nLayers; i++) {

							TextureObject *tex = (TextureObject *) ObjAlloc(FILE_TYPE_TEXTURE, NpGetTextureFormatForPreset());

							wchar_t *plttName = UiEditGetText(data->hWndPaletteNames[i]);

							unsigned char *texelCopy = malloc(texelSize);
							memcpy(texelCopy, texel, texelSize);

							tex->texture.texels.name = _strdup("pswap");
							tex->texture.texels.texImageParam = texImageParam;
							tex->texture.texels.height = height;
							tex->texture.texels.texel = texelCopy;

							tex->texture.palette.name = TexNarrowResourceNameFromWideChar(plttName);
							tex->texture.palette.nColors = plttSize;
							tex->texture.palette.pal = (COLOR *) calloc(plttSize, sizeof(COLOR));
							for (unsigned int j = 0; j < plttSize; j++) {
								tex->texture.palette.pal[j] = ColorConvertToDS(pltt[i * plttSize + j]);
							}
							free(plttName);

							NpOpenObject(hWndMain, &tex->header);
						}

						free(texel);

						break;
					}
					case TYPE_SINGLE_TEXTURE:
					{
						//create one texture with one large palette
						TextureObject *tex = (TextureObject *) ObjAlloc(FILE_TYPE_TEXTURE, NpGetTextureFormatForPreset());

						wchar_t *plttName = UiEditGetText(data->hWndPaletteNames[0]);

						tex->texture.texels.name = _strdup("pswap");
						tex->texture.texels.texImageParam = texImageParam;
						tex->texture.texels.height = height;
						tex->texture.texels.texel = texel;
						
						tex->texture.palette.name = TexNarrowResourceNameFromWideChar(plttName);
						tex->texture.palette.nColors = plttSize * data->nEntries;
						tex->texture.palette.pal = (COLOR *) calloc(plttSize * data->nEntries, sizeof(COLOR));
						for (unsigned int i = 0; i < plttSize * nLayers; i++) {
							tex->texture.palette.pal[i] = ColorConvertToDS(pltt[i]);
						}

						free(plttName);

						NpOpenObject(hWndMain, &tex->header);

						break;
					}
					case TYPE_TEXARC:
					{
						//create one texture archive
						TexArc *texarc = (TexArc *) ObjAlloc(FILE_TYPE_NSBTX, NSBTX_TYPE_NNS);

						texarc->nTextures = 1;
						texarc->nPalettes = data->nEntries;

						//put texture
						texarc->textures = (TEXELS *) calloc(1, sizeof(TEXELS));
						texarc->textures[0].name = _strdup("pswap");
						texarc->textures[0].texImageParam = texImageParam;
						texarc->textures[0].height = height;
						texarc->textures[0].texel = texel;

						//put palettes
						texarc->palettes = (PALETTE *) calloc(data->nEntries, sizeof(PALETTE));
						for (unsigned int i = 0; i < nLayers; i++) {
							wchar_t *plttName = UiEditGetText(data->hWndPaletteNames[i]);

							texarc->palettes[i].name = TexNarrowResourceNameFromWideChar(plttName);
							texarc->palettes[i].nColors = plttSize;
							texarc->palettes[i].pal = (COLOR *) calloc(plttSize, sizeof(COLOR));
							free(plttName);

							//put palette colors
							for (unsigned int j = 0; j < plttSize; j++) {
								texarc->palettes[i].pal[j] = ColorConvertToDS(pltt[i * plttSize + j]);
							}
						}

						NpOpenObject(hWndMain, &texarc->header);
						break;
					}
				}

				//last: to report the created palette, calculate the number of shared palette colors.
				unsigned int nShared = 0;
				for (unsigned int i = 0; i < nUsedColors; i++) {
					int shared = 1;

					for (unsigned int j = 1; j < nLayers; j++) {
						if (pltt[j * plttSize + i] != pltt[(j - 1) * plttSize + i]) shared = 0;
					}

					if (shared) nShared++;
				}

				free(pltt);

				SendMessage(hWnd, WM_CLOSE, 0, 0);

				//user info message (after dialog closure)
				wchar_t infobuf[64];
				wsprintfW(infobuf, L"Created %d palettes. %d colors used, %d colors shared.", nLayers,
					nUsedColors, nShared);
				MessageBox(hWndMain, infobuf, L"Create Palette Swap", MB_ICONINFORMATION);

			} else if ((hWndControl == data->hWndCancel || idCtl == IDCANCEL) && cmd == BN_CLICKED) {
				//cancel dialog
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if (hWndControl == data->hWndType && cmd == CBN_SELCHANGE) {
				int sel = UiCbGetCurSel(hWndControl);

				int enablePaletteNames = 1;
				if (sel == TYPE_SINGLE_TEXTURE) enablePaletteNames = 0; // in "Large palette" mode, only one palette name exists

				for (unsigned int i = 1; i < data->nEntries; i++) {
					EnableWindow(data->hWndPaletteNames[i], enablePaletteNames);
				}

			} else if (hWndControl == data->hWndDither && cmd == BN_CLICKED) {
				int en = GetCheckboxChecked(hWndControl);
				EnableWindow(data->hWndDiffuse, en);
			} else {

				//check the up/down buttons
				for (unsigned int i = 0; i < data->nEntries - 1; i++) {
					if ((hWndControl == data->hWndDownButtons[i] || hWndControl == data->hWndUpButtons[i + 1]) && cmd == BN_CLICKED) {
						//command to move item i down and item i+1 up

						//swap data in struct
						PaletteSwapEntry temp;
						memcpy(&temp, &data->entries[i], sizeof(temp));
						memcpy(&data->entries[i], &data->entries[i + 1], sizeof(temp));
						memcpy(&data->entries[i + 1], &temp, sizeof(temp));

						//swap the UI items
						SendMessage(data->hWndFileLabels[i], WM_SETTEXT, -1, (LPARAM) data->entries[i].path);
						SendMessage(data->hWndFileLabels[i + 1], WM_SETTEXT, -1, (LPARAM) data->entries[i + 1].path);

						//swap palette names
						wchar_t *pltt1 = UiEditGetText(data->hWndPaletteNames[i]);
						wchar_t *pltt2 = UiEditGetText(data->hWndPaletteNames[i + 1]);
						UiEditSetText(data->hWndPaletteNames[i + 1], pltt1);
						UiEditSetText(data->hWndPaletteNames[i], pltt2);
						free(pltt1);
						free(pltt2);

						break;
					}
				}

			}

			break;
		}
		case WM_DESTROY:
		{
			free(data);
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

static void RegisterOpenAsDialogClass(void) {
	RegisterGenericClass(L"OpenAsDialogClass", OpenAsDialogProc, sizeof(LPVOID));
}

static void RegisterCompressDialogClass(void) {
	RegisterGenericClass(L"CompressFileDialog", CompressFileProc, sizeof(LPVOID));
}

static void RegisterPaletteSwapClass(void) {
	RegisterGenericClass(L"PaletteSwapClass", PaletteSwapProc, sizeof(LPVOID));
}

static BOOL NpCfgWriteInt(LPCWSTR section, LPCWSTR prop, int val) {
	WCHAR buf[32];
	wsprintfW(buf, L"%d", val);
	return WritePrivateProfileString(section, prop, buf, g_configPath);
}

static int NpCfgReadInt(LPCWSTR section, LPCWSTR prop, int def) {
	//read value from profile, if not the default then return
	int val0 = GetPrivateProfileInt(section, prop, def, g_configPath);
	if (val0 != def) return val0;

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
	g_configuration.preset                 = NpCfgReadInt(L"NitroPaint", L"Preset",            0);
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
	MesgEditorRegisterClass();
	RegisterOpenAsDialogClass();
	RegisterCompressDialogClass();
	RegisterPaletteSwapClass();
	combo2dRegisterFormats();
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

static void CheckAvailableProcessorFeatures(void) {
#if defined(_M_IX86) || defined(_M_AMD64)
#if defined(_M_IX86_FP)

	int feature[4];
	__cpuidex(feature, 1, 0);

	//get feature level
	int featureLevel = 0;
	if ((feature[3] >> 25) & 1) featureLevel++;  // SSE
	if ((feature[3] >> 26) & 1) featureLevel++;  // SSE2
	if ((feature[2] >>  9) & 1) featureLevel++;  // SSE3
	if ((feature[2] >> 19) & 1) featureLevel++;  // SSE4.1
	if ((feature[2] >> 20) & 1) featureLevel++;  // SSE4.2
	if ((feature[2] >> 28) & 1) featureLevel++;  // AVX

	int neededFeatureLevel = _M_IX86_FP;

	if (featureLevel < neededFeatureLevel) {
		char msgbuf[128];
		sprintf(msgbuf, "%s is not present on your processor and required to run this app.", "SSE2");

		MessageBoxA(NULL, msgbuf, "Error", MB_ICONERROR);
		ExitProcess(1);
	}

#endif
#endif // _M_IX86
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	CheckAvailableProcessorFeatures();

	//fetch version
	(void) NpGetVersion();

	g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	HACCEL hAccel = LoadAccelerators(hInstance, (LPCWSTR) IDR_ACCELERATOR1);
	CoInitialize(NULL);

	SetConfigPath();
	ReadConfiguration();
	CheckExistingAppWindow();
	InitializeDpiAwareness();

	ObjInitCommon();

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
