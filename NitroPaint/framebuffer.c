#include "framebuffer.h"

#include <math.h>

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


// ----- rendering helper routines

static void SwapInts(int *i1, int *i2) {
	int temp = *i1;
	*i1 = *i2;
	*i2 = temp;
}

static void SwapPoints(int *x1, int *y1, int *x2, int *y2) {
	SwapInts(x1, x2);
	SwapInts(y1, y2);
}

void FbPutPixel(FrameBuffer *fb, int x, int y, COLOR32 col) {
	if (x < 0 || x >= fb->width) return;
	if (y < 0 || y >= fb->height) return;

	fb->px[x + y * fb->width] = col;
}

void FbDrawLine(FrameBuffer *fb, COLOR32 col, int x1, int y1, int x2, int y2) {
	//compute deltas
	int dx = x2 - x1, dy = y2 - y1;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	//if dx and dy are zero, put one pixel (avoid divide by zero)
	if (dx == 0 && dy == 0) {
		if (x1 >= 0 && y1 >= 0 && x1 < fb->width && y1 < fb->height) {
			fb->px[x1 + y1 * fb->width] = col;
		}
		return;
	}

	//draw horizontally or vertically
	if (dx >= dy) {
		//draw left->right
		if (x2 < x1) SwapPoints(&x1, &y1, &x2, &y2);

		//scan
		for (int i = 0; i <= dx; i++) {
			int px = i + x1;
			int py = ((i * (y2 - y1)) * 2 + dx) / (dx * 2) + y1;
			if (px >= 0 && py >= 0 && px < fb->width && py < fb->height) {
				fb->px[px + py * fb->width] = col;
			}
		}
	} else {
		//draw top->bottom. ensure top point first
		if (y2 < y1) SwapPoints(&x1, &y1, &x2, &y2);

		//scan
		for (int i = 0; i <= dy; i++) {
			int px = ((i * (x2 - x1)) * 2 + dy) / (dy * 2) + x1;
			int py = i + y1;
			if (px >= 0 && py >= 0 && px < fb->width && py < fb->height) {
				fb->px[px + py * fb->width] = col;
			}
		}
	}
}

void FbRenderSolidCircle(FrameBuffer *fb, int cx, int cy, int cr, COLOR32 col) {
	int r2 = cr * cr;
	col = REVERSE(col);

	//use midpoint circle algorithm
	for (int x = 0; x < cr; x++) {
		//compute intersection
		int y = (int) (sqrt(r2 - x * x) + 0.5f);
		if (y < x) break;

		FbPutPixel(fb, cx + x, cy + y, col);
		FbPutPixel(fb, cx - x, cy + y, col);
		FbPutPixel(fb, cx + x, cy - y, col);
		FbPutPixel(fb, cx - x, cy - y, col);
		FbPutPixel(fb, cx + y, cy + x, col);
		FbPutPixel(fb, cx - y, cy + x, col);
		FbPutPixel(fb, cx + y, cy - x, col);
		FbPutPixel(fb, cx - y, cy - x, col);

		//fixes 1-pixel corner artifacts
		if (y == (x + 1)) break;
	}
}

