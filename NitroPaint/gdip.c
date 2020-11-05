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

void writeImage(DWORD * pixels, int width, int height, LPWSTR lpszFileName) {
	BITMAPFILEHEADER header;

	HANDLE hFile = CreateFile(lpszFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	BYTE bitmapHeader[] = {
		'B', 'M',
		0, 0, 0, 0, //file size
		0, 0, 0, 0,
		0x36, 0, 0, 0, //offset to bitmap data
	};
	BYTE infoHeader[] = {
		40, 0, 0, 0,
		0, 0, 0, 0, //width
		0, 0, 0, 0, //height
		1, 0,
		32, 0, //bits per pixel
		0, 0, 0, 0,
		0, 0, 0, 0,
		100, 0, 0, 0,
		100, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0
	};

	*(int *) (infoHeader + 4) = width;
	*(int *) (infoHeader + 8) = height;


	//LPDWORD reversed = HeapAlloc(GetProcessHeap(), 0, width * height * 4);
	/*for (int i = 0; i < width * height; i++) {
	DWORD d = pixels[i];
	//d = (d & 0xFF00FF00) | ((d & 0xFF) << 16) | ((d >> 16) & 0xFF);

	reversed[i] = d;
	}*/
	LPVOID tmp = calloc(width, 4);
	for (int i = 0; i < height >> 1; i++) {
		int offs = i * width;
		int offs2 = (height - 1 - i) * width;
		CopyMemory(tmp, pixels + offs, width * 4);
		CopyMemory(pixels + offs, pixels + offs2, width * 4);
		CopyMemory(pixels + offs2, tmp, width * 4);
	}
	free(tmp);

	DWORD dwWritten;
	WriteFile(hFile, bitmapHeader, sizeof(bitmapHeader), &dwWritten, NULL);
	WriteFile(hFile, infoHeader, sizeof(infoHeader), &dwWritten, NULL);
	WriteFile(hFile, pixels, width * height * 4, &dwWritten, NULL);

	CloseHandle(hFile);
	//	HeapFree(GetProcessHeap(), 0, reversed);

}