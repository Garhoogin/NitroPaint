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
