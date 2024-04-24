#include "framebuffer.h"

void FbCreate(FrameBuffer *fb, HWND hWnd, int width, int height) {
	//create DC
	HDC hDC = GetDC(hWnd);
	HDC hCompatDC = CreateCompatibleDC(hDC);
	ReleaseDC(hWnd, hDC);
	fb->hDC = hCompatDC;

	fb->width = 0;
	fb->height = 0;
	fb->size = 0;
	fb->hBitmap = NULL;
	FbSetSize(fb, width, height);
}

void FbDestroy(FrameBuffer *fb) {
	if (fb->hDC != NULL) {
		DeleteDC(fb->hDC);
		fb->hDC = NULL;
	}
	if (fb->hBitmap != NULL) {
		DeleteObject(fb->hBitmap);
		fb->hBitmap = NULL;
	}

	fb->width = 0;
	fb->height = 0;
	fb->size = 0;
	fb->px = NULL;
}

void FbSetSize(FrameBuffer *fb, int width, int height) {
	//if size differs at all in dimension
	if (width != fb->width || height != fb->height) {
		BITMAPINFO bmi = { 0 };
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = DIB_RGB_COLORS;
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height;

		void *bits = NULL;
		HBITMAP hbm = CreateDIBSection(fb->hDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);

		SelectObject(fb->hDC, hbm);
		if (fb->hBitmap != NULL) DeleteObject(fb->hBitmap);
		fb->hBitmap = hbm;
		fb->px = (COLOR32 *) bits;
	}

	//set new size
	fb->width = width;
	fb->height = height;
	fb->size = fb->width * fb->height;
}

void FbDraw(FrameBuffer *fb, HDC hDC, int x, int y, int width, int height, int srcX, int srcY) {
	if (fb->hDC == NULL) return;

	BitBlt(hDC, x, y, width, height, fb->hDC, srcX, srcY, SRCCOPY);
}
