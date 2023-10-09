#pragma once
#include <Windows.h>

#include "color.h"

int ImgIsValidTGA(const unsigned char *buffer, unsigned int dwSize);

COLOR32 *ImgReadEx(LPCWSTR lpszFileName, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **pImagePalette, int *pPaletteSize);

//
// Read an image and return an array of pixels and size.
//
COLOR32 *ImgRead(LPCWSTR path, int *pWidth, int *pHeight);

//
// Write an array of pixels to an image file.
//
HRESULT ImgWrite(COLOR32 *px, int width, int height, LPCWSTR path);

//
// Write an indexed image to a file from a given color palette and index data.
// Index data is 8 bits per pixel with the stride equal to the image's width.
//
HRESULT ImgWriteIndexed(unsigned char *bits, int width, int height, COLOR32 *palette, int paletteSize, LPCWSTR path);

//
// Flip an image horizontally and/or vertically.
//
void ImgFlip(COLOR32 *px, int width, int height, int hFlip, int vFlip);

//
// Swap red and blue color channels in an image.
//
void ImgSwapRedBlue(COLOR32 *px, int width, int height);

//
// Count the number of unique colors in an image (counting transparent as a color), and otherwise ignoring the alpha channel.
//
int ImgCountColors(COLOR32 *px, int nPx);

//
// Crop an image with a source bounding box.
//
COLOR32 *ImgCrop(COLOR32 *px, int width, int height, int srcX, int srcY, int srcWidth, int srcHeight);

//
// Crop an image with a source bounding box and write pixel data to a buffer.
//
void ImgCropInPlace(COLOR32 *px, int width, int height, COLOR32 *out, int srcX, int srcY, int srcWidth, int srcHeight);

