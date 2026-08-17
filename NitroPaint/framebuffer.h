#pragma once
#include <Windows.h>

#include "color.h"

typedef enum FbBufferType_ {
	FB_TYPE_INVALID,
	FB_TYPE_DEVICE,
	FB_TYPE_MEMORY
} FbBufferType;

typedef struct FrameBuffer_ {
	FbBufferType type;                 // type of frame buffer
	int width;                         // current framebuffer width
	int height;                        // current framebuffer height
	COLOR32 *px;                       // pixel buffer
	HBITMAP hBitmap;                   // bitmap
	HDC hDC;                           // device context
} FrameBuffer;

int FbCreateOnWindow(FrameBuffer *fb, HWND hWnd, int width, int height);
void FbDestroy(FrameBuffer *fb);

void FbSetSize(FrameBuffer *fb, int width, int height);
void FbDraw(FrameBuffer *fb, HDC hDC, int x, int y, int width, int height, int srcX, int srcY);


// ----- rendering helpers

void FbPutPixel(FrameBuffer *fb, int x, int y, COLOR32 col);
void FbDrawLine(FrameBuffer *fb, COLOR32 col, int x1, int y1, int x2, int y2);
void FbDrawCircle(FrameBuffer *fb, int cx, int cy, int cr, COLOR32 col);
void FbDrawRect(FrameBuffer *fb, int x, int y, int width, int height, COLOR32 col);
void FbFillRect(FrameBuffer *fb, int x, int y, int width, int height, COLOR32 col);
void FbFillBlendRect(FrameBuffer *fb, int x, int y, int width, int height, COLOR32 col);
