#pragma once
#include <Windows.h>

#include "color.h"

int isTGA(BYTE *buffer, DWORD dwSize);

COLOR32 *gdipReadImage(LPCWSTR lpszFileName, int *pWidth, int *pHeight);

COLOR32 *gdipReadImageEx(LPCWSTR lpszFileName, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **pImagePalette, int *pPaletteSize);

void writeImage(COLOR32 *pixels, int width, int height, LPCWSTR lpszFileName);


//
// Read an image and return an array of pixels and size.
//
COLOR32 *imageRead(LPCWSTR path, int *pWidth, int *pHeight);

//
// Read an image and return an array of pixels, size, palette, and the original
// image index data.
//
COLOR32 *imageReadIndexed(LPCWSTR path, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **outPalette, int *outPaletteSize);

//
// Write an array of pixels to an image file.
//
HRESULT imageWrite(COLOR32 *px, int width, int height, LPCWSTR path);

//
// Write an indexed image to a file from a given color palette and index data.
// Index data is 8 bits per pixel with the stride equal to the image's width.
//
HRESULT imageWriteIndexed(unsigned char *bits, int width, int height, COLOR32 *palette, int paletteSize, LPCWSTR path);
