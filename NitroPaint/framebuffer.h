#pragma once
#include <Windows.h>

#include "color.h"

typedef struct FrameBuffer_ {
	int width;                         // current framebuffer width
	int height;                        // current framebuffer height
	int size;                          // size of framebuffer in pixels
	COLOR32 *px;                       // pixel buffer
	HBITMAP hBitmap;                   // bitmap
	HDC hDC;                           // device context
} FrameBuffer;

void FbCreate(FrameBuffer *fb, HWND hWnd, int width, int height);
void FbDestroy(FrameBuffer *fb);

void FbSetSize(FrameBuffer *fb, int width, int height);
void FbDraw(FrameBuffer *fb, HDC hDC, int x, int y, int width, int height, int srcX, int srcY);


// ----- rendering helpers

void FbPutPixel(FrameBuffer *fb, int x, int y, COLOR32 col);
void FbDrawLine(FrameBuffer *fb, COLOR32 col, int x1, int y1, int x2, int y2);
void FbRenderSolidCircle(FrameBuffer *fb, int cx, int cy, int cr, COLOR32 col);
