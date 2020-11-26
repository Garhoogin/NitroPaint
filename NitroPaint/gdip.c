#include "gdip.h"
#include <Windows.h>

static int startup = 0;

DWORD * gdipReadImage(LPWSTR lpszFileName, int * pWidth, int * pHeight) {
	if (!startup) {
		STARTUPINPUT si = { 0 };
		si.GdiplusVersion = 1;
		ULONG_PTR token;
		GdiplusStartup(&token, &si, 0);
		startup = 1;
	}

	RECT r = { 0 };
	BYTE * gp = NULL;
	BITMAPDATA bm;
	GdipCreateBitmapFromFile(lpszFileName, (void *) &gp);
	GdipGetImageWidth(gp, (INT *) &(r.right));
	GdipGetImageHeight(gp, (INT *) &(r.bottom));
	GdipBitmapLockBits(gp, (void *) &r, 3, 0x0026200A, (void *) &bm);
	DWORD * px = (DWORD *) calloc(r.right * r.bottom, 4);
	*pWidth = r.right;
	*pHeight = r.bottom;
	DWORD * scan0 = (DWORD *) bm.d1[4];
	for (int i = 0; i < r.right * r.bottom; i++) {
		DWORD d = scan0[i];
		d = ((d & 0xFF) << 16) | (d & 0xFF00FF00) | ((d >> 16) & 0xFF);
		px[i] = d;
	}
	GdipDisposeImage(gp);
	return px;
}

void writeImage(DWORD *px, int width, int height, LPWSTR name) {
	if (!startup) {
		STARTUPINPUT si = { 0 };
		si.GdiplusVersion = 1;
		ULONG_PTR token;
		GdiplusStartup(&token, &si, 0);
		startup = 1;
	}
	BYTE clsid[] = { 0x06, 0xf4, 0x7c, 0x55, 0x04, 0x1a, 0xd3, 0x11, 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e };
	void *bitmap = NULL;
	GdipCreateBitmapFromScan0(width, height, width * 4, 0x0026200A, px, &bitmap);
	GdipSaveImageToFile(bitmap, name, clsid, NULL);
	GdipDisposeImage(bitmap);
}