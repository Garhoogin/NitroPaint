#pragma once
#include <Windows.h>


extern INT __stdcall GdiplusStartup(void * n, void * n2, int n3);
extern INT __stdcall GdipCreateBitmapFromFile(LPWSTR str, void * bmp);
extern INT __stdcall GdipGetImageWidth(void * img, INT * width);
extern INT __stdcall GdipGetImageHeight(void * img, INT * width);
extern INT __stdcall GdipBitmapLockBits(void * img, RECT * rc, int n1, int n2, void * n3);
extern INT __stdcall GdipDisposeImage(void * img);
extern INT __stdcall GdipCreateBitmapFromScan0(INT n1, INT n2, INT n3, INT n4, void *n5, void *n6);
extern INT __stdcall GdipSaveImageToFile(void *n1, LPWSTR n2, void *n3, void *n4);


typedef struct {
	DWORD d1[6];
} BITMAPDATA;

typedef struct {
	DWORD GdiplusVersion;
	DWORD DebugEventCallback;
	BOOL SuppressBackgroundThread;
	BOOL SuppressExternalCodecs;
} STARTUPINPUT;

#pragma comment(lib, "gdiplus.lib")

int isTGA(BYTE *buffer, DWORD dwSize);

DWORD * gdipReadImage(LPWSTR lpszFileName, int * pWidth, int * pHeight);

void writeImage(DWORD * pixels, int width, int height, LPWSTR lpszFileName);