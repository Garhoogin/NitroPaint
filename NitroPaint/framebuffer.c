#include "framebuffer.h"

#include <math.h>

int FbCreateOnWindow(FrameBuffer *fb, HWND hWnd, int width, int height) {
	fb->type = FB_TYPE_INVALID;

	//create framebuffer compatible with window DC
	HDC hDC = GetDC(hWnd);
	if (hDC == NULL) return 0;

	int status = FbCreateDeviceCompatible(fb, hDC, width, height);

	ReleaseDC(hWnd, hDC);
	return status;
}

int FbCreateDeviceCompatible(FrameBuffer *fb, HDC hDC, int width, int height) {
	fb->type = FB_TYPE_INVALID;

	//create compatible DC
	fb->hDC = CreateCompatibleDC(hDC);
	if (fb->hDC == NULL) return 0;

	fb->width = 0;
	fb->height = 0;
	fb->hBitmap = NULL;
	fb->type = FB_TYPE_DEVICE;
	FbSetSize(fb, width, height);
	return 1;
}


static void FbiDestroyForDevice(FrameBuffer *fb) {
	//delete the DC
	if (fb->hDC != NULL) {
		DeleteDC(fb->hDC);
		fb->hDC = NULL;
	}

	//delete the bitmap
	if (fb->hBitmap != NULL) {
		DeleteObject(fb->hBitmap);
		fb->hBitmap = NULL;
	}
}

void FbDestroy(FrameBuffer *fb) {
	switch (fb->type) {
		case FB_TYPE_INVALID:
			//not valid frame buffer
			break;
		case FB_TYPE_DEVICE:
			//device type frame buffer
			FbiDestroyForDevice(fb);
			break;
		case FB_TYPE_MEMORY:
			//TODO
			break;

	}
	

	fb->type = FB_TYPE_INVALID;
	fb->width = 0;
	fb->height = 0;
	fb->px = NULL;
}

void FbSetSize(FrameBuffer *fb, int width, int height) {
	//set minimum dimension
	if (width < 1 || height < 1) {
		width = 1;
		height = 1;
	}

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

static void FbiPutPixel(FrameBuffer *fb, int x, int y, COLOR32 col) {
	fb->px[x + y * fb->width] = col;
}

void FbPutPixel(FrameBuffer *fb, int x, int y, COLOR32 col) {
	if (x < 0 || x >= fb->width) return;
	if (y < 0 || y >= fb->height) return;

	FbiPutPixel(fb, x, y, col);
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

void FbDrawCircle(FrameBuffer *fb, int cx, int cy, int cr, COLOR32 col) {
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

void FbDrawRect(FrameBuffer *fb, int x, int y, int width, int height, COLOR32 col) {
	FbDrawLine(fb, col, x, y, x + width - 1, y);
	FbDrawLine(fb, col, x, y + height - 1, x + width - 1, y + height - 1);
	
	FbDrawLine(fb, col, x, y, x, y + height - 1);
	FbDrawLine(fb, col, x + width - 1, y, x + width - 1, y + height - 1);
}

void FbFillRect(FrameBuffer *fb, int x, int y, int width, int height, COLOR32 col) {
	//fill blend rectangle, set alpha=1.0
	FbFillBlendRect(fb, x, y, width, height, col | 0xFF000000);
}

void FbFillBlendRect(FrameBuffer *fb, int x, int y, int width, int height, COLOR32 col) {
	//alpha weighting of foreground and background
	unsigned int aFore = (col >> 24);
	unsigned int aBack = 255 - aFore;
	if (aFore == 0) return;

	//if X<0, adjust X to 0
	if (x < 0) {
		width += x;
		x = 0;
	}
	if ((x + width) > fb->width) {
		width = fb->width - x;
	}

	//if Y<0, adjust Y to 0
	if (y < 0) {
		height += y;
		y = 0;
	}
	if ((y + height) > fb->height) {
		height = fb->height - y;
	}

	unsigned int r = ((col >>  0) & 0xFF) * aFore;
	unsigned int g = ((col >>  8) & 0xFF) * aFore;
	unsigned int b = ((col >> 16) & 0xFF) * aFore;

	//fill
	for (int i = 0; i < height; i++) {
		COLOR32 *row = &fb->px[(y + i) * fb->width + x];
		for (int j = 0; j < width; j++) {
			//blend pixel color
			if (aFore == 0xFF) {
				//drawing color directly
				row[j] = col;
			} else {
				//blending color

				COLOR32 src = row[j];
				unsigned int destR = ((src >>  0) & 0xFF) * aBack + r;
				unsigned int destG = ((src >>  8) & 0xFF) * aBack + g;
				unsigned int destB = ((src >> 16) & 0xFF) * aBack + b;

				//scale down
				destR = (destR * 2 + 255) / 510;
				destG = (destG * 2 + 255) / 510;
				destB = (destB * 2 + 255) / 510;
				row[j] = destR | (destG << 8) | (destB << 16) | 0xFF000000;
			}
		}
	}
}

